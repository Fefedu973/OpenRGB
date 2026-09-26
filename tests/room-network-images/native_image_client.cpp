// SPDX-License-Identifier: GPL-2.0-or-later
// Real NetworkClient integration; no detector or local-device initialization.
#include "NetworkClient.h"
#include "RGBController_Network.h"
#include <QCoreApplication>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
using namespace std::chrono_literals;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class P>static void wait_for(P predicate,int ms,const char* why)
{
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    while(!predicate()){if(std::chrono::steady_clock::now()>=until)throw std::runtime_error(why);std::this_thread::sleep_for(10ms);}
}
static std::shared_ptr<const room_image::Frame> frame(unsigned width,unsigned height,uint64_t seq,uint8_t blue=111)
{
    auto pixels=std::make_shared<std::vector<uint8_t>>(size_t(width)*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
    {const auto i=(size_t(y)*width+x)*4;(*pixels)[i]=blue;(*pixels)[i+1]=uint8_t(y%256);(*pixels)[i+2]=123;(*pixels)[i+3]=255;}
    return std::make_shared<const room_image::Frame>(room_image::Frame{width,height,width*4,seq,pixels});
}
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    try
    {
        check(argc>=2,"Expected PORT [legacy]");
        const int port=std::stoi(argv[1]);check(port>0 && port<=65535,"Invalid port");
        const bool legacy=argc>=3 && std::string(argv[2])=="legacy";
        NetworkClient client;client.SetIP("127.0.0.1");client.SetPort(static_cast<unsigned short>(port));
        client.SetName("OpenRGB Room native image test");client.SetConnectTimeout(300);client.StartClient();
        wait_for([&]{return client.GetOnline();},12000,"Client enumeration timeout");
        check(client.GetRGBControllers().size()==2,"Expected two synthetic controllers");
        std::vector<room_image::RGBControllerImageInterface*> sinks;
        for(auto* controller:client.GetRGBControllers())
        {auto* sink=dynamic_cast<room_image::RGBControllerImageInterface*>(controller);check(sink,"Missing optional interface");sinks.push_back(sink);}
        if(legacy)
        {
            check(!client.GetSupportsImageAPI(),"Legacy peer advertised image support");
            for(auto* sink:sinks){room_image::Output out{};check(!sink->GetImageOutput(0,out),"Legacy peer leaked descriptor");check(sink->SubmitImage(0,frame(2,2,1),{},1000)==room_image::SubmitResult::Unsupported,"Legacy Submit was accepted");}
        }
        else
        {
            check(client.GetSupportsImageAPI(),"Missing SDK7 feature negotiation");
            for(auto* sink:sinks)
            {room_image::Output out{};wait_for([&]{return sink->GetImageOutput(0,out);},4000,"Image descriptor timeout");check(out.width && out.height,"Empty descriptor");}
            auto pixels=frame(800,600,777);
            check(sinks[0]->SubmitImage(0,nullptr,{},1000)==room_image::SubmitResult::Invalid,"Null frame accepted");
            check(sinks[0]->SubmitImage(999,pixels,{},1000)==room_image::SubmitResult::Unsupported,"Unknown zone accepted");
            check(sinks[0]->SubmitImage(0,pixels,{},99)==room_image::SubmitResult::Invalid,"Short lease accepted");
            for(auto* sink:sinks)
            {
                wait_for([&]{return sink->SubmitImage(0,pixels,{},150)==room_image::SubmitResult::Accepted;},2000,"Submit stayed busy");
                std::shared_ptr<const room_image::Frame> preview;room_image::Mapping mapping;
                wait_for([&]{return sink->GetImagePreview(0,preview,mapping);},100,"Preview absent");
                check(preview==pixels,"Preview copied the source frame");
            }
            std::this_thread::sleep_for(200ms);
            for(auto* sink:sinks){std::shared_ptr<const room_image::Frame> preview;room_image::Mapping mapping;check(!sink->GetImagePreview(0,preview,mapping),"Expired preview retained");}
            for(unsigned i=0;i<120;++i)for(auto* sink:sinks)
            {const auto result=sink->SubmitImage(0,pixels,{},4000);check(result==room_image::SubmitResult::Accepted || result==room_image::SubmitResult::Busy,"Coalescing submit rejected");}
            // Drain prior synthetic activity before waiting for a new ACK per output.
            std::this_thread::sleep_for(300ms);
            pixels=frame(800,600,777,173);
            for(auto* controller:client.GetRGBControllers())
            {
                auto* network=dynamic_cast<RGBController_Network*>(controller);
                uint64_t before=0,count=0;unsigned status=0;client.GetImageAck(network->GetID(),before,status);
                wait_for([&]{return network->SubmitImage(0,pixels,{},4000)==room_image::SubmitResult::Accepted;},2000,"Final submit failed");
                wait_for([&]{return client.GetImageAck(network->GetID(),count,status) && count>before;},2500,"No image ACK");
                check(status==NET_PACKET_STATUS_OK,"Server rejected native image");
            }
            std::this_thread::sleep_for(100ms);
        }
        const auto started=std::chrono::steady_clock::now();client.StopClient();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
        check(elapsed<2500,"Client stop exceeded its bound");
        // Old objects are deliberately still alive; their generation is retired.
        for(auto* sink:sinks){room_image::Output out{};check(!sink->GetImageOutput(0,out),"Retired controller still owns an image output");check(sink->SubmitImage(0,frame(2,2,2),{},1000)==room_image::SubmitResult::Unsupported,"Retired controller accepted image");}
        std::cout<<"{\"ok\":true,\"legacy\":"<<(legacy?"true":"false")<<",\"outputs\":2,\"sequence\":777,\"expectedBlue\":173,\"stop_ms\":"<<elapsed<<"}\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"native image client: "<<e.what()<<"\n";return 1;}
}
