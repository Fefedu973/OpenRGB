/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StreamDeckBackgroundController.h"
#include "httplib.h"
#include "../../FrameSurface/FrameSurface.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace streamdeck_background;
using namespace std::chrono_literals;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

static void Check(bool condition, const char* message)
{
    if(!condition) throw std::runtime_error(message);
}

template<class Action> static void Reject(Action action)
{
    bool rejected = false;
    try { action(); } catch(const std::exception&) { rejected = true; }
    Check(rejected, "Invalid input was accepted");
}

template<class Predicate> static void Wait(Predicate predicate, int milliseconds = 4000)
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
    while(!predicate())
    {
        if(Clock::now() > deadline) throw std::runtime_error("Mock condition timed out");
        std::this_thread::sleep_for(5ms);
    }
}

static json NativeLayout()
{
    json positions = json::array();
    for(int y : {5, 102, 199}) for(int x : {11, 108, 205, 302, 399}) positions.push_back({x,y});
    return {{"width",480},{"height",272},{"tileWidth",72},{"tileHeight",72},{"positions",positions}};
}

static std::vector<unsigned char> Solid(unsigned char r, unsigned char g, unsigned char b)
{
    std::vector<unsigned char> out(LED_COUNT * 3);
    for(unsigned int i=0;i<LED_COUNT;++i) { out[i*3]=r;out[i*3+1]=g;out[i*3+2]=b; }
    return out;
}

struct Mock
{
    httplib::Server server;
    std::thread listening;
    std::mutex mutex;
    std::vector<std::string> frames;
    std::vector<Clock::time_point> arrival;
    std::string token = std::string(43,'A');
    int port = 0;
    std::atomic<int> health{0}, layouts{0}, stops{0}, unauthorized{0};
    std::atomic<int> delay_health{0}, delay_frame{0};
    std::atomic<bool> ready{true}, bad_layout{false};
    std::filesystem::path directory, session_file;

    Mock()
    {
        directory = std::filesystem::temp_directory_path() / ("openrgb-streamdeck-mock-" + std::to_string(Clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        session_file = directory / "session.json";
        server.Get("/health", [&](const httplib::Request& req, httplib::Response& res)
        {
            if(!Auth(req,res)) return;
            ++health;
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_health.load()));
            res.set_content(json{{"ready",ready.load()}}.dump(),"application/json");
        });
        server.Get("/layout", [&](const httplib::Request& req, httplib::Response& res)
        {
            if(!Auth(req,res)) return;
            ++layouts;
            auto layout = NativeLayout();
            if(bad_layout) layout["width"]=481;
            res.set_content(layout.dump(),"application/json");
        });
        server.Post("/frame", [&](const httplib::Request& req, httplib::Response& res)
        {
            if(!Auth(req,res)) return;
            Check(req.get_header_value("Content-Type")=="application/octet-stream", "Wrong frame content type");
            Check(req.get_header_value("X-Lease-Ms")=="2000", "Wrong lease");
            Check(req.body.size()==FRAME_BYTES, "Wrong frame length");
            Check(!req.has_header("Origin"), "Origin must not be emitted");
            for(std::size_t i=3;i<req.body.size();i+=4) Check(static_cast<unsigned char>(req.body[i])==255,"Alpha is not opaque");
            {
                std::lock_guard<std::mutex> lock(mutex);
                frames.push_back(req.body);
                arrival.push_back(Clock::now());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_frame.load()));
            res.set_content("{\"accepted\":true}","application/json");
        });
        server.Post("/stop", [&](const httplib::Request& req, httplib::Response& res)
        {
            if(!Auth(req,res)) return;
            ++stops;
            res.set_content("{\"accepted\":true}","application/json");
        });
        port = server.bind_to_any_port("127.0.0.1");
        Check(port >= 1024, "Mock loopback bind failed");
        listening = std::thread([&]{ server.listen_after_bind(); });
        Wait([&]{ return server.is_running(); });
        WriteSession();
    }
    ~Mock()
    {
        server.stop();
        if(listening.joinable()) listening.join();
        std::filesystem::remove(session_file);
        std::filesystem::remove(directory);
    }
    bool Auth(const httplib::Request& req, httplib::Response& res)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(req.get_header_value("Authorization") != "Bearer " + token)
        {
            ++unauthorized;
            res.status=401;
            res.set_content("{\"error\":\"unauthorized\"}","application/json");
            return false;
        }
        return true;
    }
    void WriteSession()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::ofstream out(session_file, std::ios::binary);
        out << json{{"token",token},{"http_port",port},{"pid",1}}.dump();
    }
    void RotateToken()
    {
        { std::lock_guard<std::mutex> lock(mutex);token=std::string(43,'B'); }
        WriteSession();
    }
    std::size_t Count() { std::lock_guard<std::mutex> lock(mutex);return frames.size(); }
    std::string Last() { std::lock_guard<std::mutex> lock(mutex);return frames.back(); }
    Options Config() { return {session_file.u8string(),20}; }
};

static void TestOptionsAndImage()
{
    Options options;
    Check(!ParseOptions(json(),options),"Absent settings enabled controller");
    Check(!ParseOptions(json::object(),options),"Default settings enabled controller");
    Check(!ParseOptions({{"enabled",false}},options),"Disabled settings enabled controller");
    Reject([&]{ParseOptions({{"enabled",true},{"session_file","relative.json"}},options);});
    Reject([&]{ParseOptions({{"enabled","true"}},options);});
    const auto absolute=(std::filesystem::temp_directory_path()/"synthetic.json").u8string();
    Check(ParseOptions({{"enabled",true},{"session_file",absolute},{"fps",20}},options),"Valid settings rejected");
    Check(ParseOptions({{"enabled",true},{"transport","native"},{"native_library",absolute},{"native_lock_directory",absolute}},options)
          && options.transport=="native","Valid optional native transport rejected");
    Reject([&]{ParseOptions({{"enabled",true},{"transport","native"},{"native_library","relative.dll"},{"native_lock_directory",absolute}},options);});
    Reject([&]{ParseOptions({{"enabled",true},{"transport","native"},{"native_library",absolute}},options);});
    Reject([&]{ParseOptions({{"enabled",true},{"transport","unknown"},{"session_file",absolute}},options);});
    Reject([&]{ParseOptions({{"enabled",true},{"session_file",absolute},{"fps",21}},options);});
    Check(ParseOptions({{"enabled",true},{"session_file",absolute},{"frame_surface",{{"channel","ambient-main"},{"stale_ms",1000}}}},options)
          && options.surface_channel=="ambient-main", "Valid image input rejected");
    Reject([&]{ParseOptions({{"enabled",true},{"session_file",absolute},{"frame_surface",{{"channel","../invalid"}}}},options);});
    Reject([&]{ParseOptions({{"enabled",true},{"session_file",absolute},{"frame_surface",{{"channel","safe"},{"stale_ms",2001}}}},options);});
    auto layout=ParseLayout(NativeLayout());
    auto malformed=NativeLayout();malformed["positions"][14]=malformed["positions"][0];
    Reject([&]{ParseLayout(malformed);});
    malformed=NativeLayout();malformed["positions"][0][0]=409;
    Reject([&]{ParseLayout(malformed);});
    const auto solid=EncodeTiles(Solid(17,91,203),layout);
    Check(solid.size()==FRAME_BYTES,"Tile size mismatch");
    for(std::size_t i=0;i<solid.size();i+=4)
        Check(static_cast<unsigned char>(solid[i])==203 && static_cast<unsigned char>(solid[i+1])==91
              && static_cast<unsigned char>(solid[i+2])==17 && static_cast<unsigned char>(solid[i+3])==255,"BGRA order mismatch");
    auto gradient=Solid(0,0,0);
    for(unsigned int y=0;y<HEIGHT;++y)for(unsigned int x=0;x<WIDTH;++x)
    {gradient[(y*WIDTH+x)*3]=static_cast<unsigned char>(x*3);gradient[(y*WIDTH+x)*3+1]=static_cast<unsigned char>(y*5);}
    const auto rendered=EncodeTiles(gradient,layout);
    const auto byte=[&](std::size_t p){return static_cast<unsigned char>(rendered[p]);};
    Check(byte(2)<byte(71*4+2),"Horizontal detail lost inside first key");
    Check(byte(1)<byte(71*72*4+1),"Vertical detail lost inside first key");
    Check(byte(2)<byte(14*72*72*4+2) && byte(1)<byte(14*72*72*4+1),"Native key order/crops incorrect");
    Reject([&]{EncodeTiles({},layout);});

    // Native image sampling is independent of the compatibility matrix and supports padded rows.
    const unsigned int width=800,height=600,stride=width*4+16;
    std::vector<unsigned char> image(stride*height,0);
    for(unsigned int y=0;y<height;++y) for(unsigned int x=0;x<width;++x)
    {
        const auto i=y*stride+x*4;
        image[i]=static_cast<unsigned char>(x/4);image[i+1]=static_cast<unsigned char>(y/3);
        image[i+2]=123;image[i+3]=255;
    }
    const auto native=EncodeSurfaceTiles(image,width,height,stride,layout);
    Check(static_cast<unsigned char>(native[2])==123 && static_cast<unsigned char>(native[0])==4
          && static_cast<unsigned char>(native[1])==4,"Native 800x600 sample or padded stride incorrect");
    Check(static_cast<unsigned char>(native[0])<static_cast<unsigned char>(native[71*4])
          && static_cast<unsigned char>(native[1])<static_cast<unsigned char>(native[71*72*4+1]),"Native image detail lost");
    Reject([&]{EncodeSurfaceTiles(image,width,height,width*4-1,layout);});
}

static void TestCoalescingAndLease()
{
    Mock mock;
    Controller controller(mock.Config());
    std::this_thread::sleep_for(70ms);
    Check(mock.health==0 && mock.Count()==0,"Idle controller performed I/O");
    mock.delay_frame=100;
    controller.Submit(Solid(1,2,3));
    Wait([&]{return mock.Count()==1;});
    for(int i=0;i<200;++i)controller.Submit(Solid(static_cast<unsigned char>(i),10,20));
    controller.Submit(Solid(255,33,44));
    Wait([&]{return controller.GetStatus().accepted>=2;});
    const auto latest=mock.Last();
    Check(static_cast<unsigned char>(latest[0])==44 && static_cast<unsigned char>(latest[1])==33
          && static_cast<unsigned char>(latest[2])==255,"Queued stale frames replayed");
    Check(mock.Count()<=3,"Unbounded frame backlog");
    mock.delay_frame=0;
    const auto before=mock.Count();
    Wait([&]{return mock.Count()>before;},1500);
    {
        std::lock_guard<std::mutex> lock(mock.mutex);
        for(std::size_t i=1;i<mock.arrival.size();++i)
            Check(mock.arrival[i]-mock.arrival[i-1]>=45ms,"Frame rate exceeded 20 fps");
    }
    controller.Stop();
    Check(mock.stops==1 && controller.GetStatus().stop_requested,"Shutdown did not request normal background");
    const auto count=mock.Count();
    controller.Submit(Solid(0,0,0));std::this_thread::sleep_for(60ms);
    Check(mock.Count()==count,"Frame emitted after stop");
}

static void TestRotationAndRefusal()
{
    Mock mock;
    mock.bad_layout=true;
    Controller controller(mock.Config());
    controller.Submit(Solid(100,20,50));
    Wait([&]{return controller.GetStatus().failures>0;});
    Check(mock.Count()==0,"Invalid native geometry received a frame");
    mock.bad_layout=false;
    Wait([&]{return controller.GetStatus().accepted>0;});
    const auto previous_health=mock.health.load();
    mock.RotateToken();
    controller.Submit(Solid(50,200,60));
    Wait([&]{return mock.health>previous_health && controller.GetStatus().accepted>=2;});
    Check(mock.unauthorized==0,"Session rotation reused old authorization");
    controller.Stop();
    Check(mock.stops==1,"Rotated session was not released");
}

static void TestTimeoutAndCancellation()
{
    {
        Mock mock;
        mock.delay_frame=900;
        Controller controller(mock.Config());
        controller.Submit(Solid(1,1,1));
        Wait([&]{return mock.Count()>0;});
        const auto start=Clock::now();
        controller.Stop();
        Check(Clock::now()-start<1500ms,"Unbounded stop while HTTP response stalled");
        Check(controller.GetStatus().failures>0,"Timeout was not recorded");
        Check(mock.stops==1,"Lost frame acknowledgment bypassed cleanup");
    }
    {
        Mock mock;
        mock.delay_health=200;
        Controller controller(mock.Config());
        controller.Submit(Solid(255,0,0));
        Wait([&]{return mock.health>0;});
        controller.Stop();
        Check(mock.layouts==0 && mock.Count()==0 && mock.stops==0,"Cancel during handshake sent a frame");
    }
}

static std::vector<unsigned char> SurfaceSolid(unsigned char r, unsigned char g, unsigned char b)
{
    std::vector<unsigned char> image(800*600*4);
    for(std::size_t i=0;i<image.size();i+=4) { image[i]=b;image[i+1]=g;image[i+2]=r;image[i+3]=255; }
    return image;
}

static void TestSurfaceInput()
{
    Mock mock;
    auto config=mock.Config();
    config.surface_channel="StreamDeckMock_"+std::to_string(Clock::now().time_since_epoch().count());
    config.surface_stale_ms=300;
    Controller controller(config);
    controller.Submit(Solid(255,0,0));
    std::this_thread::sleep_for(80ms);
    Check(mock.Count()==0 && mock.health==0 && controller.GetStatus().submitted==0,"Surface mode fell back to matrix input");
    room_surface::Publisher publisher(config.surface_channel,800*600*4);
    Check(publisher.IsOpen(),"Synthetic publisher failed");
    const auto first=SurfaceSolid(12,34,56);
    Check(publisher.PublishBGRA(first.data(),first.size(),800,600,3200),"Synthetic publish failed");
    Wait([&]{return controller.GetStatus().accepted>=1;});
    Check(static_cast<unsigned char>(mock.Last()[0])==56 && static_cast<unsigned char>(mock.Last()[2])==12,"Surface pixel order incorrect");
    Check(controller.GetStatus().surface_width==800 && controller.GetStatus().surface_height==600,"Surface geometry was converted to LED matrix");
    Wait([&]{return mock.stops==1;},2000);
    const auto expired_count=mock.Count();
    std::this_thread::sleep_for(150ms);
    Check(mock.Count()==expired_count && mock.stops==1,"Stale producer was replayed or repeatedly released");
    publisher.Close();
    room_surface::Publisher replacement(config.surface_channel,800*600*4);
    Check(replacement.IsOpen(),"Publisher reconnect failed with active consumer");
    const auto next=SurfaceSolid(90,80,70);
    Check(replacement.PublishBGRA(next.data(),next.size(),800,600,3200),"Replacement publish failed");
    Wait([&]{return mock.Count()>expired_count;});
    Check(static_cast<unsigned char>(mock.Last()[0])==70 && static_cast<unsigned char>(mock.Last()[2])==90,"Publisher generation reset lost new image");
    controller.Stop();
    Check(mock.stops==2,"Surface shutdown did not release new stream");
}

static void TestSurfaceExpiresDuringHandshake()
{
    Mock mock;
    mock.delay_health=220;
    auto config=mock.Config();
    config.surface_channel="SlowSurface_"+std::to_string(Clock::now().time_since_epoch().count());
    config.surface_stale_ms=100;
    room_surface::Publisher publisher(config.surface_channel,800*600*4);
    const auto image=SurfaceSolid(1,2,3);
    Check(publisher.PublishBGRA(image.data(),image.size(),800,600,3200),"Synthetic publish failed");
    Controller controller(config);
    Wait([&]{return mock.health>0;});
    std::this_thread::sleep_for(350ms);
    Check(mock.Count()==0,"Expired image replayed after slow handshake");
    controller.Stop();
}

static std::shared_ptr<const room_image::Frame> Native(unsigned char r,unsigned char g,unsigned char b,unsigned sequence=1)
{
    auto frame=std::make_shared<room_image::Frame>();
    frame->width=800;frame->height=600;frame->stride=3200;frame->sequence=sequence;
    frame->pixels=std::make_shared<const std::vector<unsigned char>>(SurfaceSolid(r,g,b));
    return frame;
}

static void SubmitNative(room_image::RGBControllerImageInterface& sink,std::shared_ptr<const room_image::Frame> frame,
                         const room_image::Mapping& mapping={},unsigned lease=1000)
{
    room_image::SubmitResult result=room_image::SubmitResult::Busy;
    Wait([&]{ result=sink.SubmitImage(0,frame,mapping,lease);return result!=room_image::SubmitResult::Busy; });
    Check(result==room_image::SubmitResult::Accepted,"Generic image submission rejected");
}

static void TestNativeMapping()
{
    auto pixels=std::make_shared<std::vector<unsigned char>>(800*600*4,static_cast<unsigned char>(255));
    for(unsigned y=0;y<600;++y)for(unsigned x=0;x<800;++x)
    {
        const auto i=(y*800+x)*4;
        (*pixels)[i]=y>=300 ? 255 : 0;
        (*pixels)[i+1]=x>=400 ? 255 : 0;
        (*pixels)[i+2]=(x<400 && y<300) || (x>=400 && y>=300) ? 255 : 0;
    }
    room_image::Frame frame;frame.width=800;frame.height=600;frame.stride=3200;frame.pixels=pixels;
    const auto layout=ParseLayout(NativeLayout());
    const auto first=[&](const room_image::Mapping& mapping)
    {
        const auto out=EncodeNativeTiles(frame,mapping,layout);
        return std::vector<unsigned char>{static_cast<unsigned char>(out[0]),static_cast<unsigned char>(out[1]),static_cast<unsigned char>(out[2]),static_cast<unsigned char>(out[3])};
    };
    Check(first({})==std::vector<unsigned char>({0,0,255,255}),"Native identity mapping failed");
    Check(first(room_image::Mapping::Rectangle(.5,0,.5,1))==std::vector<unsigned char>({0,255,0,255}),"Native crop failed");
    Check(first(room_image::Mapping::Rectangle(0,0,1,1,180))==std::vector<unsigned char>({255,255,255,255}),"Native rotation failed");
    Check(first(room_image::Mapping::Rectangle(0,0,1,1,0,true))==std::vector<unsigned char>({0,255,0,255}),"Native mirror failed");
    room_image::Mapping dim;dim.brightness=.5;
    Check(first(dim)==std::vector<unsigned char>({0,0,128,255}),"Generic brightness mapping failed");
    Check(first(room_image::Mapping::Rectangle(2,2,1,1))==std::vector<unsigned char>({0,0,0,255}),"Out-of-scene mapping did not black-fill");
}

static void TestDirectNativeLeaseAndFallback()
{
    Mock mock;Controller controller(mock.Config());
    room_image::RGBControllerImageInterface& sink=controller;
    room_image::Output output;
    Check(sink.GetImageOutput(0,output) && output.width==480 && output.height==272 && output.max_fps==20,"Generic output descriptor missing");
    Check(!sink.GetImageOutput(1,output),"Nonexistent zone advertised");
    const auto image=Native(200,30,10);
    room_image::Mapping invalid;invalid.u_x=std::numeric_limits<double>::quiet_NaN();
    Check(sink.SubmitImage(0,{}, {},1000)==room_image::SubmitResult::Invalid,"Null native image accepted");
    Check(sink.SubmitImage(1,image,{},1000)==room_image::SubmitResult::Unsupported,"Unknown image zone accepted");
    Check(sink.SubmitImage(0,image,invalid,1000)==room_image::SubmitResult::Invalid,"Nonfinite mapping accepted");
    Check(sink.SubmitImage(0,image,{},99)==room_image::SubmitResult::Invalid && sink.SubmitImage(0,image,{},5001)==room_image::SubmitResult::Invalid,"Unbounded native lease accepted");
    SubmitNative(sink,image,{},350);
    std::shared_ptr<const room_image::Frame> preview;room_image::Mapping mapping;
    Wait([&]{return sink.GetImagePreview(0,preview,mapping);});
    Check(preview==image && preview->pixels==image->pixels,"Preview copied rather than shared immutable native image");
    Wait([&]{return controller.GetStatus().accepted>=1;});
    Check(controller.GetStatus().input=="native" && static_cast<unsigned char>(mock.Last()[2])==200,"Direct native frame not delivered");
    for(int i=0;i<150;++i)controller.Submit(Solid(static_cast<unsigned char>(i),0,0));
    controller.Submit(Solid(1,2,250));
    std::this_thread::sleep_for(100ms);
    Check(static_cast<unsigned char>(mock.Last()[2])==200,"LED path overrode live native lease");
    Wait([&]{return controller.GetStatus().input=="led";});
    Check(static_cast<unsigned char>(mock.Last()[0])==250 && static_cast<unsigned char>(mock.Last()[2])==1,"Expiry replayed LED backlog instead of latest matrix");
    Check(!sink.GetImagePreview(0,preview,mapping),"Expired native preview remained visible");
    controller.Stop();
    Check(sink.SubmitImage(0,image,{},1000)==room_image::SubmitResult::Busy,"Stopped image sink accepted work");
}

static void TestNativeCoalescingAndExclusiveSurface()
{
    {
        Mock mock;mock.delay_frame=100;Controller controller(mock.Config());
        SubmitNative(controller,Native(1,2,3));Wait([&]{return mock.Count()>0;});
        std::vector<std::weak_ptr<const room_image::Frame>> old;
        auto pixels=Native(10,200,30)->pixels;
        for(unsigned i=0;i<150;++i)
        {
            auto frame=std::make_shared<room_image::Frame>();frame->width=800;frame->height=600;frame->stride=3200;
            frame->sequence=i+10;frame->pixels=pixels;old.push_back(frame);SubmitNative(controller,frame);
        }
        std::size_t retained=0;for(const auto& ref:old)if(!ref.expired())++retained;
        Check(retained<=2,"Native frames accumulated in a queue");
        Wait([&]{return controller.GetStatus().accepted>=2;});
        Check(static_cast<unsigned char>(mock.Last()[1])==200 && mock.Count()<=3,"Native coalescing replayed old frames");
        controller.Stop();
    }
    {
        Mock mock;auto config=mock.Config();config.surface_channel="exclusive_no_producer";
        Controller controller(config);room_image::Output output;
        Check(!controller.GetImageOutput(0,output),"Externally-owned surface advertised native ownership");
        Check(controller.SubmitImage(0,Native(255,0,0),{},1000)==room_image::SubmitResult::Unsupported,"Direct native image mixed with configured shared surface");
        std::this_thread::sleep_for(60ms);Check(mock.Count()==0,"Exclusive surface sent direct image");controller.Stop();
    }
}

static void TestNativeExpiryWithoutFallback()
{
    {
        Mock mock;Controller controller(mock.Config());SubmitNative(controller,Native(9,8,7),{},150);
        Wait([&]{return mock.Count()>0;});Wait([&]{return mock.stops==1;});
        std::this_thread::sleep_for(100ms);Check(mock.Count()==1 && mock.stops==1,"Expired direct native stream renewed or repeatedly stopped");
        controller.Stop();Check(mock.stops==1,"Expired native cleanup repeated at shutdown");
    }
    {
        Mock mock;mock.delay_health=220;Controller controller(mock.Config());SubmitNative(controller,Native(9,8,7),{},100);
        Wait([&]{return mock.health>0;});std::this_thread::sleep_for(350ms);
        Check(mock.Count()==0,"Expired direct frame replayed after slow connection");controller.Stop();
    }
}

int main()
{
    try
    {
        const auto aggregate=AggregateCompositorStatus({{"ready",true},{"paints",150},{"tiles",std::string(10000,'x')},
            {"lastFrameCoverage",{{"rendered",15},{"empty",7},{"injected",15},{"pixels","excluded"}}},
            {"pacing",{{"ackFps5s",17.2},{"ackLatencyMs",{{"p95",2},{"image","excluded"}}}}},
            {"lastFault",{{"message",std::string(2000,'e')},{"stack","excluded"}}}});
        Check(aggregate.at("ready")==true && aggregate.at("paints")==150,"Aggregate counters missing");
        Check(!aggregate.contains("tiles") && !aggregate["lastFrameCoverage"].contains("pixels"),"Pixel data escaped into metadata");
        Check(aggregate["pacing"]["ackLatencyMs"].size()==1 && aggregate.at("fault").get<std::string>().size()==512,"Unbounded diagnostic field");
        const auto queued=AggregateCompositorStatus({{"stalled",true},{"queuedStalls",2},{"queueRecoveries",1},
            {"pendingAgeMs",3500.5},{"pendingEntered",false},{"pendingPhase","queued"},
            {"nativePointer","0xDEADBEEF"},{"token","not-for-sdk"}});
        Check(queued.size()==6 && queued.at("stalled")==true && queued.at("queuedStalls")==2 && queued.at("queueRecoveries")==1,
              "Queue stall aggregate is missing or exposes unrelated fields");
        Check(queued.at("pendingAgeMs")==3500.5 && queued.at("pendingEntered")==false && queued.at("pendingPhase")=="queued",
              "Queued diagnostics have changed values");
        const auto rendering=AggregateCompositorStatus({{"stalled",false},{"pendingEntered",true},{"pendingPhase","rendering"}});
        Check(rendering.at("pendingPhase")=="rendering" && rendering.at("pendingEntered")==true && rendering.at("stalled")==false,
              "Rendering phase was not preserved");
        const auto idle=AggregateCompositorStatus({{"pendingAgeMs",nullptr},{"pendingEntered",false},{"pendingPhase",nullptr}});
        Check(idle.at("pendingAgeMs").is_null() && !idle.contains("pendingPhase"),"Idle phase must not invent a render state");
        for(const auto& invalid:std::vector<json>{std::string(10000,'x'),"stopped",123,true,json::array(),json::object()})
            Check(!AggregateCompositorStatus({{"pendingPhase",invalid}}).contains("pendingPhase"),"Arbitrary phase data escaped into SDK");
        const auto invalid_scalars=AggregateCompositorStatus({{"stalled",json::object()},{"queuedStalls",json::array()},
            {"queueRecoveries","secret"},{"pendingAgeMs",json::array()},{"pendingEntered","not-a-scalar-counter"}});
        Check(invalid_scalars.empty(),"Non-scalar stall payload escaped into SDK");
        Check(AggregateCompositorStatus(json::array()).empty(),"Old/invalid status must remain compatible");
        std::cout << "PASS bounded aggregate compositor metadata without images\n";
        TestOptionsAndImage();std::cout << "PASS configuration, native layout, BGRA and spatial gradients\n";
        TestCoalescingAndLease();std::cout << "PASS real mock HTTP, coalescing, pacing, lease and stop\n";
        TestRotationAndRefusal();std::cout << "PASS geometry refusal and session rotation\n";
        TestTimeoutAndCancellation();std::cout << "PASS response timeout and handshake cancellation\n";
        TestSurfaceInput();std::cout << "PASS native 800x600 shared image, ignored LEDs, TTL release and producer reconnect\n";
        TestSurfaceExpiresDuringHandshake();std::cout << "PASS image TTL rechecked after HTTP handshake\n";
        TestNativeMapping();std::cout << "PASS generic native crop, rotation, flip, gain and black exterior\n";
        TestDirectNativeLeaseAndFallback();std::cout << "PASS optional image interface, immutable preview, native lease and latest LED fallback\n";
        TestNativeCoalescingAndExclusiveSurface();std::cout << "PASS bounded native shared ownership and exclusive external surface\n";
        TestNativeExpiryWithoutFallback();std::cout << "PASS native expiry cleanup and handshake expiry without fallback\n";
        std::cout << "All Stream Deck background offline tests passed; no Elgato hardware accessed.\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
