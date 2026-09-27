// SPDX-License-Identifier: GPL-2.0-or-later
#include "RGBController.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace std::chrono_literals;
using room_color::ColorFrame;
using room_color::SubmitResult;
static unsigned checks=0;
static void Check(bool condition, const char* text)
{ ++checks; if(!condition) throw std::runtime_error(text); }

class SyntheticController : public RGBController
{
public:
    SyntheticController()
    {
        name="Synthetic transport only";
        zone z{}; z.name="Two synthetic segments"; z.type=ZONE_TYPE_LINEAR;
        z.leds_count=z.leds_min=z.leds_max=4;
        zones.push_back(z); leds.resize(4); SetupColors();
    }
    ~SyntheticController() override { Release(); Stop(); }
    void Stop() { if(!stopped.exchange(true)) Shutdown(); }
    void Hold() { held=true; }
    void Release() { held=false; gate.notify_all(); }
    void DeviceUpdateLEDs() override
    {
        std::unique_lock<std::mutex> lock(mutex);
        writes.push_back(colors);
        gate.notify_all();
        gate.wait(lock,[&]{return !held.load();});
    }
    bool WaitWrites(size_t count, std::chrono::milliseconds timeout=1000ms)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return gate.wait_for(lock,timeout,[&]{return writes.size()>=count;});
    }
    std::vector<std::vector<RGBColor>> History()
    { std::lock_guard<std::mutex> lock(mutex); return writes; }
    std::unique_lock<std::shared_mutex> LockTopology() { return std::unique_lock<std::shared_mutex>(AccessMutex); }
    void ResizeLocked(unsigned count) { zones[0].leds_count=count; leds.resize(count); SetupColors(); }
private:
    std::atomic<bool> held{false}, stopped{false};
    std::mutex mutex;
    std::condition_variable gate;
    std::vector<std::vector<RGBColor>> writes;
};

static std::shared_ptr<const ColorFrame> Frame(SyntheticController& target, unsigned value)
{
    auto f=std::make_shared<ColorFrame>();f->topology=target.GetColorTopology();
    // Two segments of the same controller plus a repeated index: last wins.
    f->values={{0,value},{2,value+100},{2,value+200}};
    return f;
}
static void Submit(SyntheticController& target, std::shared_ptr<const ColorFrame> frame, unsigned lease=1000)
{
    const auto deadline=std::chrono::steady_clock::now()+1s;
    SubmitResult result;
    do
    {
        result=target.SubmitColorFrame(frame,lease);
        if(result!=SubmitResult::Busy)break;
        std::this_thread::yield();
    } while(std::chrono::steady_clock::now()<deadline);
    Check(result==SubmitResult::Accepted,"frame accepted");
}

int main()
{
    try
    {
        SyntheticController slow,fast;
        slow.Hold();Submit(slow,Frame(slow,1));
        Check(slow.WaitWrites(1),"slow I/O started under production AccessMutex");
        const auto blocked_start=std::chrono::steady_clock::now();
        std::vector<std::weak_ptr<const ColorFrame>> replaced;
        double max_submit_ms=0;
        for(unsigned n=2;n<=13;++n)
        {
            auto frame=Frame(slow,n);replaced.push_back(frame);
            const auto begin=std::chrono::steady_clock::now();
            Submit(slow,std::move(frame));
            max_submit_ms=std::max(max_submit_ms,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
            Submit(fast,Frame(fast,n));
            Check(fast.WaitWrites(n-1,200ms),"fast controller progresses while another is blocked");
            std::this_thread::sleep_for(10ms);
        }
        for(size_t i=0;i+1<replaced.size();++i)Check(replaced[i].expired(),"replaced frame allocation released");
        Check(!replaced.back().expired(),"only latest frame retained");
        Check(slow.History().size()==1,"no parallel I/O on slow controller");
        std::this_thread::sleep_until(blocked_start+400ms);
        slow.Release();
        Check(slow.WaitWrites(2),"latest frame delivered after blocked I/O");
        std::this_thread::sleep_for(20ms);
        const auto slow_history=slow.History();
        Check(slow_history.size()==2,"no historical frame backlog");
        Check(slow_history.back()[0]==13 && slow_history.back()[2]==213,"both segments and duplicate order preserved");
        Check(slow_history.back()[1]==0 && slow_history.back()[3]==0,"unlisted LEDs preserved");
        Check(max_submit_ms<100,"batch submission never waits for 400ms device I/O");

        SyntheticController resized;
        const auto old_token=resized.GetColorTopology();
        {
            auto topology_lock=resized.LockTopology();
            Submit(resized,Frame(resized,50));
            resized.ResizeLocked(3);
        }
        std::this_thread::sleep_for(20ms);
        Check(resized.History().empty(),"resize purges pending frame with otherwise valid indices");
        auto stale=std::make_shared<ColorFrame>();stale->topology=old_token;stale->values={{0,99}};
        Check(resized.SubmitColorFrame(stale)==SubmitResult::Stale,"old topology rejected");
        Submit(resized,Frame(resized,7));Check(resized.WaitWrites(1),"new topology works");
        auto invalid=std::make_shared<ColorFrame>();invalid->topology=resized.GetColorTopology();invalid->values={{0,88},{900,99}};
        Submit(resized,invalid);std::this_thread::sleep_for(20ms);
        Check(resized.GetColor(0)==7 && resized.History().size()==1,"invalid indexed frame cannot partially apply");
        Check(resized.SubmitColorFrame(nullptr)==SubmitResult::Invalid,"null frame rejected");
        auto oversized=std::make_shared<ColorFrame>();oversized->topology=resized.GetColorTopology();oversized->values.resize(room_color::MaxUpdates+1);
        Check(resized.SubmitColorFrame(oversized)==SubmitResult::Invalid,"bounded allocation count enforced");
        Check(resized.SubmitColorFrame(Frame(resized,9),99)==SubmitResult::Invalid,"invalid lease rejected");

        SyntheticController imported;
        const auto before_import=imported.GetColorTopology();
        auto description=RGBController::GetDeviceDescriptionJSON(&imported);
        description["colors"]={11,22,33,44};
        RGBController::SetDeviceDescriptionJSON(description,&imported);
        Check(imported.GetColorTopology()!=before_import,"JSON metadata import changes topology before exposure");
        Check(imported.GetColor(2)==33,"JSON colors survive storage reconstruction");
        const auto before_binary=imported.GetColorTopology();
        std::vector<unsigned char> binary(RGBController::GetDeviceDescriptionSize(&imported,7));
        RGBController::GetDeviceDescriptionData(binary.data(),&imported,7);
        Check(RGBController::SetDeviceDescription(binary.data(),static_cast<unsigned>(binary.size()),&imported,7)!=nullptr,"binary metadata import completes without recursive locking");
        Check(imported.GetColorTopology()!=before_binary && imported.GetColor(2)==33,"binary metadata import preserves colors and changes topology");
        auto before_segments=Frame(imported,55);
        segment new_segment{};new_segment.name="Synthetic segment";new_segment.start_idx=1;new_segment.leds_count=2;
        imported.AddSegment(0,new_segment);
        Check(imported.SubmitColorFrame(before_segments)==SubmitResult::Stale,"adding a segment invalidates old routing");
        before_segments=Frame(imported,55);
        imported.ClearSegments(0);
        Check(imported.SubmitColorFrame(before_segments)==SubmitResult::Stale,"removing segments invalidates old routing");
        before_segments=Frame(imported,55);
        zone configured{};configured.leds_count=4;configured.type=ZONE_TYPE_LINEAR;
        configured.flags=ZONE_FLAG_MANUALLY_CONFIGURED_SIZE;
        imported.ConfigureZone(0,configured);
        Check(imported.SubmitColorFrame(before_segments)==SubmitResult::Stale,"zone reconfiguration invalidates old routing");

        SyntheticController expiry;
        expiry.Hold();Submit(expiry,Frame(expiry,1));Check(expiry.WaitWrites(1),"expiry fixture blocked");
        Submit(expiry,Frame(expiry,2),100);std::this_thread::sleep_for(150ms);expiry.Release();
        std::this_thread::sleep_for(30ms);Check(expiry.History().size()==1,"expired frame not replayed");

        SyntheticController closing;
        closing.Hold();Submit(closing,Frame(closing,1));Check(closing.WaitWrites(1),"shutdown fixture blocked");
        Submit(closing,Frame(closing,2));
        std::thread shutdown([&]{closing.Stop();});
        const auto deadline=std::chrono::steady_clock::now()+1s;
        auto next=Frame(closing,3);
        while(closing.SubmitColorFrame(next)==SubmitResult::Accepted && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        const auto shutdown_submission=closing.SubmitColorFrame(next);
        closing.Release();shutdown.join();
        Check(shutdown_submission==SubmitResult::Busy,"shutdown refuses frames without requesting legacy fallback");
        Check(closing.History().size()==1,"shutdown discards pending frame");
        std::cout<<"PASS "<<checks<<" assertions; blocked I/O 400ms, max submission "<<max_submit_ms
                 <<"ms, fast writes "<<fast.History().size()<<", slow writes "<<slow_history.size()<<"; no hardware\n";
        return 0;
    }
    catch(const std::exception& error)
    { std::cerr<<"FAIL "<<error.what()<<'\n';return 1; }
}
