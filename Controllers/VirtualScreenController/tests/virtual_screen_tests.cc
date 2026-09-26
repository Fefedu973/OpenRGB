/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../VirtualScreenController.h"
#include "../../../FrameSurface/FrameSurface.h"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace virtual_screen;
using namespace std::chrono_literals;

static unsigned assertions = 0;
static void Check(bool value) { ++assertions; if(!value) throw std::runtime_error("check " + std::to_string(assertions)); }
template<class F> static void Reject(F f) { bool rejected = false; try { f(); } catch(const std::exception&) { rejected = true; } Check(rejected); }
template<class F> static bool Until(F f,int timeout_ms = 2000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do { if(f()) return true; std::this_thread::sleep_for(5ms); } while(std::chrono::steady_clock::now() < end);
    return false;
}
static std::shared_ptr<const room_image::Frame> Image(unsigned char r,unsigned char g,unsigned char b)
{
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(4*4*4);
    for(std::size_t i = 0; i < pixels->size(); i += 4) { (*pixels)[i]=b;(*pixels)[i+1]=g;(*pixels)[i+2]=r;(*pixels)[i+3]=255; }
    auto frame = std::make_shared<room_image::Frame>(); frame->width=4;frame->height=4;frame->stride=16;frame->sequence=1;frame->pixels=pixels;
    return frame;
}
int main()
{
    try
    {
        using json = nlohmann::json;
        Check(ParseOptions(json::object()).empty());
        auto config = json{{"enabled",true},{"outputs",json::array({{{"id","one"},{"name","First"},{"channel","test-one"}},
                   {{"id","two"},{"name","Second"},{"channel","test-two"},{"width",320},{"height",200}}})}};
        auto options = ParseOptions(config);
        Check(options.size()==2 && options[0].width==800 && options[0].height==600);
        Check(options[0].compatibility_width*options[0].compatibility_height==576);
        auto invalid=config;invalid["outputs"][1]["id"]="one";Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"][1]["channel"]="test-one";Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"][0]["width"]=4096;invalid["outputs"][0]["height"]=4096;Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"][0]["compatibility_width"]=128;Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"][0]["fps"]=61;Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"][0]["width"]=-1;Reject([&]{ParseOptions(invalid);});
        invalid=config;invalid["outputs"]=json::array();for(int i=0;i<17;++i)invalid["outputs"].push_back(config["outputs"][0]);Reject([&]{ParseOptions(invalid);});
#ifdef _WIN32
        const auto suffix=std::to_string(GetCurrentProcessId());
        Options o{"fixture","Fixture","virtual-screen-test-"+suffix,16,12,30,2,2};
        room_surface::Reader reader(o.channel);
        Controller controller(o);
        room_image::Output description;
        Check(controller.GetImageOutput(0,description) && description.width==16 && description.height==12 && description.max_fps==30);
        Check(!controller.GetImageOutput(1,description));
        auto frame=Image(200,100,40);
        Check(controller.SubmitImage(1,frame,{})==room_image::SubmitResult::Unsupported);
        Check(controller.SubmitImage(0,frame,{},99)==room_image::SubmitResult::Invalid);
        auto mapping=room_image::Mapping::Rectangle(0,0,1,1,180,true,false);mapping.brightness=0.5;
        Check(Until([&]{return controller.SubmitImage(0,frame,mapping,500)==room_image::SubmitResult::Accepted;}));
        room_surface::Frame received;
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.bgra[2]==100;}));
        Check(received.width==16 && received.height==12 && received.bgra[1]==50 && received.bgra[0]==20 && received.bgra[3]==255);
        std::shared_ptr<const room_image::Frame> preview;room_image::Mapping preview_mapping;
        Check(Until([&]{return controller.GetImagePreview(0,preview,preview_mapping);}));
        Check(preview==frame && preview_mapping.brightness==0.5);
        controller.SubmitLEDs(std::vector<std::uint32_t>(4,0x0000ff)); // Red fallback is suppressed during native lease.
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.bgra[2]==0;},1500));
        Check(!controller.GetImagePreview(0,preview,preview_mapping));
        controller.SubmitLEDs(std::vector<std::uint32_t>(4,0x0000ff));
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.bgra[2]==255;}));
        Check(received.bgra[0]==0 && received.bgra[1]==0);
        auto outside=room_image::Mapping::Rectangle(2,0,1,1);
        Check(Until([&]{return controller.SubmitImage(0,frame,outside,1000)==room_image::SubmitResult::Accepted;}));
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.bgra[2]==0;}));
        const auto generation=received.generation;
        const auto begin=std::chrono::steady_clock::now();controller.Stop();
        Check(std::chrono::steady_clock::now()-begin < 500ms && controller.GetStatus().stopped);
        Check(controller.SubmitImage(0,frame,{})==room_image::SubmitResult::Busy);
        Controller replacement(o);
        Check(Until([&]{return replacement.SubmitImage(0,frame,{},1000)==room_image::SubmitResult::Accepted;}));
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.generation!=generation && received.bgra[2]==200;}));
        for(unsigned value=10;value<=250;value+=10)
        {
            const auto update=Image(static_cast<unsigned char>(value),0,0);
            Check(Until([&]{return replacement.SubmitImage(0,update,{},1000)==room_image::SubmitResult::Accepted;}));
        }
        Check(Until([&]{return reader.ReadLatest(received)==room_surface::FrameStatus::NewFrame && received.bgra[2]==250;}));
        Check(replacement.GetStatus().replaced >= 1);
        replacement.Stop();
        // Stop may race the worker entering its very first idle wait.
        for(unsigned i=0;i<25;++i) { Controller immediately_stopped(o); immediately_stopped.Stop(); }
        Check(true);
#endif
        std::cout << assertions << " virtual-screen assertions passed; synthetic pixels and local shared memory only.\n";
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
