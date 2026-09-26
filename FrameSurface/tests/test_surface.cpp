/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "FrameSurface/FrameSurface.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace room_surface;
using namespace std::chrono_literals;
static void Check(bool condition,const char* text) { if(!condition) throw std::runtime_error(text); }
static std::string Channel(const char* label)
{
    static std::atomic<unsigned int> count{0};
    return std::string("test_")+label+"_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++count);
}
static std::vector<std::uint8_t> Image(unsigned int width,unsigned int height,unsigned char value)
{
    std::vector<std::uint8_t> out(std::size_t(width)*height*4,value);
    for(std::size_t i=3;i<out.size();i+=4) out[i]=255;
    return out;
}
static void TestBounds()
{
    Check(ValidChannel("Ambient_1-test"),"Valid channel rejected");
    for(const auto& name:{std::string(""),std::string("../other"),std::string("a.b"),std::string(65,'a')}) Check(!ValidChannel(name),"Unsafe channel accepted");
    Check(!ValidImage(UINT32_MAX,UINT32_MAX,UINT32_MAX,4,UINT64_MAX,MAX_CAPACITY),"Overflow dimensions accepted");
    Check(!ValidImage(800,600,3199,4,1920000,1920000),"Short stride accepted");
    Check(!ValidImage(800,600,3200,4,1919999,1920000),"Short input accepted");
    Publisher invalid("bad.channel",64);Check(!invalid.IsOpen(),"Invalid publisher opened");
    Publisher excessive(Channel("large"),MAX_CAPACITY+1);Check(!excessive.IsOpen(),"Excessive capacity allocated");
    Frame frame;Reader absent(Channel("absent"));Check(absent.ReadLatest(frame)==FrameStatus::Unavailable,"Absent publisher not handled");
}
static void TestPixelFormatAndTtl()
{
    const auto channel=Channel("rgb");Publisher publisher(channel,256);Check(publisher.IsOpen(),"Publisher unavailable");
    Publisher duplicate(channel,256);Check(!duplicate.IsOpen(),"Two live publishers accepted");
    Reader reader(channel);Frame frame;
    std::vector<std::uint8_t> rgb={10,20,30,40,50,60};
    Check(publisher.PublishRGB(rgb.data(),rgb.size(),2,1,6),"RGB publish failed");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Initial frame absent");
    Check(frame.bgra==std::vector<std::uint8_t>({30,20,10,255,60,50,40,255}),"RGB/BGRA conversion wrong");
    Check(reader.ReadLatest(frame)==FrameStatus::Unchanged,"Same frame copied again");
    std::this_thread::sleep_for(20ms);
    Check(reader.ReadLatest(frame,1)==FrameStatus::Stale,"TTL did not precede unchanged check");
    auto padded=Image(4,2,99); // use three pixels plus four padding bytes per row
    Check(publisher.PublishBGRA(padded.data(),padded.size(),3,2,16),"Padded stride rejected");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Padded frame absent");
    Check(frame.stride==16 && frame.bgra[12]==0 && frame.bgra[15]==0 && frame.bgra[31]==0,"Row padding leaked source memory");
    padded[3]=1;Check(!publisher.PublishBGRA(padded.data(),padded.size(),3,2,16),"Nonopaque alpha accepted");
}
static void TestLatestAndReconnect()
{
    const auto channel=Channel("latest");const std::uint64_t capacity=800*600*4;
    Reader reader(channel);Frame frame;
    std::uint64_t old_generation=0;
    {
        Publisher publisher(channel,capacity);Check(publisher.IsOpen(),"800x600 publisher unavailable");
        const auto start=std::chrono::steady_clock::now();
        for(unsigned int sequence=1;sequence<=60;++sequence)
        {
            auto image=Image(800,600,static_cast<unsigned char>(sequence));
            Check(publisher.PublishBGRA(image.data(),image.size(),800,600,3200),"800x600 publish failed");
            if(sequence==1) Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Reader attach failed");
            std::this_thread::sleep_until(start+std::chrono::microseconds(sequence*16667));
        }
        Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Late reader missed latest frame");
        Check(frame.sequence==60 && frame.width==800 && frame.height==600 && frame.bgra.size()==capacity,"Latest-only metadata mismatch");
        Check(frame.bgra[0]==60 && frame.bgra[capacity-2]==60,"Latest image torn or queued");
        Check(reader.ReadLatest(frame)==FrameStatus::Unchanged,"Historical frames queued");
        old_generation=frame.generation;
    }
    Check(reader.ReadLatest(frame)==FrameStatus::Unavailable,"Closed publisher remained active");
    Publisher replacement(channel,capacity);Check(replacement.IsOpen(),"Publisher reconnect blocked by reader");
    auto image=Image(800,600,7);Check(replacement.PublishBGRA(image.data(),image.size(),800,600,3200),"Replacement publish failed");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame && frame.sequence==1 && frame.generation!=old_generation,"Reconnected sequence was mistaken for old frame");
}
static void TestCorruptionAndBusy()
{
    const auto channel=Channel("invalid");Publisher publisher(channel,1024);Reader reader(channel);Frame frame;
    auto image=Image(4,4,8);Check(publisher.PublishBGRA(image.data(),image.size(),4,4,16),"Fixture publish failed");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Fixture read failed");
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,detail::Name(channel,false).c_str());
    auto* header=static_cast<Header*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,HEADER_BYTES+1024));
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,detail::Name(channel,true).c_str());
    Check(mapping && header && mutex,"Fixture mapping unavailable");
    Header original{};
    { detail::Lock lock(mutex,100);Check(lock.acquired,"Fixture lock failed");original=*header;header->capacity=MAX_CAPACITY+1; }
    Check(reader.ReadLatest(frame)==FrameStatus::Invalid && frame.bgra.size()==64,"Untrusted capacity allocated memory");
    { detail::Lock lock(mutex,100);*header=original;header->stride=UINT32_MAX;header->height=UINT32_MAX; }
    Check(reader.ReadLatest(frame)==FrameStatus::Invalid,"Malformed stride/height accepted");
    { detail::Lock lock(mutex,100);*header=original;header->timestamp_ms=GetTickCount64()+10000; }
    Check(reader.ReadLatest(frame)==FrameStatus::Invalid,"Future timestamp accepted");
    { detail::Lock lock(mutex,100);*header=original; }
    std::atomic<bool> held{false},release{false};
    std::thread holder([&]{detail::Lock lock(mutex,100);held=lock.acquired;while(!release)std::this_thread::sleep_for(1ms);});
    while(!held)std::this_thread::sleep_for(1ms);
    const auto status=reader.ReadLatest(frame,2000,1);release=true;holder.join();
    Check(status==FrameStatus::Busy,"Contention did not drop/pause boundedly");
    UnmapViewOfFile(header);CloseHandle(mapping);CloseHandle(mutex);
}
static void TestContendedCloseAndSameProcessReopen()
{
    const auto channel=Channel("close_busy");Publisher publisher(channel,1024);Reader reader(channel);Frame frame;
    auto image=Image(4,4,4);Check(publisher.PublishBGRA(image.data(),image.size(),4,4,16),"Close fixture publish failed");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame,"Close fixture reader failed");
    const auto previous_generation=frame.generation;
    HANDLE mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,detail::Name(channel,true).c_str());
    Check(mutex!=nullptr,"Close fixture mutex failed");
    std::atomic<bool> held{false},release{false};
    std::thread holder([&]{detail::Lock lock(mutex,100);held=lock.acquired;while(!release)std::this_thread::sleep_for(1ms);});
    while(!held)std::this_thread::sleep_for(1ms);
    const auto start=std::chrono::steady_clock::now();publisher.Close();
    const auto elapsed=std::chrono::steady_clock::now()-start;
    release=true;holder.join();CloseHandle(mutex);
    Check(elapsed<500ms,"Close waited unboundedly for consumer lock");
    Check(reader.ReadLatest(frame)==FrameStatus::Unavailable,"Closed instance survived through its live process PID");
    Publisher replacement(channel,1024);
    Check(replacement.IsOpen(),"Contended Close prevented same-process channel reuse");
    image=Image(4,4,9);Check(replacement.PublishBGRA(image.data(),image.size(),4,4,16),"Reopened publish failed");
    Check(reader.ReadLatest(frame)==FrameStatus::NewFrame && frame.generation!=previous_generation && frame.bgra[0]==9,
          "Existing reader did not reconnect after contended Close");
}
static int Child(const std::string& channel)
{
    Publisher publisher(channel,800*600*4);if(!publisher.IsOpen())return 2;
    auto frame=Image(800,600,77);
    if(!publisher.PublishBGRA(frame.data(),frame.size(),800,600,3200))return 3;
    std::this_thread::sleep_for(700ms);return 0;
}
static void TestOtherProcess()
{
    const auto channel=Channel("process");wchar_t executable[32768]{};
    Check(GetModuleFileNameW(nullptr,executable,32768)>0,"Executable path unavailable");
    std::wstring command=L"\""+std::wstring(executable)+L"\" --child "+std::wstring(channel.begin(),channel.end());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable,&command[0],nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Mock publisher process failed");
    Reader reader(channel);Frame frame;FrameStatus status=FrameStatus::Unavailable;
    for(unsigned int i=0;i<100 && status!=FrameStatus::NewFrame;++i) {status=reader.ReadLatest(frame);std::this_thread::sleep_for(5ms);}
    const bool received=status==FrameStatus::NewFrame && frame.width==800 && frame.bgra[0]==77;
    WaitForSingleObject(process.hProcess,2000);DWORD exit_code=1;GetExitCodeProcess(process.hProcess,&exit_code);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    Check(received && exit_code==0,"Interprocess BGRA frame failed");
}
int main(int argc,char** argv)
{
    if(argc==3 && std::string(argv[1])=="--child")return Child(argv[2]);
    try
    {
        TestBounds();std::cout<<"PASS channel/bounds/overflow\n";
        TestPixelFormatAndTtl();std::cout<<"PASS RGB conversion, opaque BGRA, stride and TTL\n";
        TestLatestAndReconnect();std::cout<<"PASS 60 synthetic 800x600 frames, late consumer, no queue, reconnect\n";
        TestCorruptionAndBusy();std::cout<<"PASS invalid headers, bounded allocation and mutex contention\n";
        TestContendedCloseAndSameProcessReopen();std::cout<<"PASS bounded contended Close and same-process owner lifetime\n";
        TestOtherProcess();std::cout<<"PASS real second-process shared memory\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
