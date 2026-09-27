/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Explicit opt-in hardware harness. Never executed by Build-Native -Tests. */
#include "../../Controllers/StreamDeckBackgroundController/StreamDeckNativeClient.h"
#include "../../Controllers/StreamDeckBackgroundController/StreamDeckBackgroundController.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
int main(int argc,char** argv)
{
    if(argc!=4 || std::string(argv[1])!="--authorized-elgato-test") {
        std::cerr<<"Usage: test_elgato --authorized-elgato-test ABSOLUTE_DLL ABSOLUTE_SHARED_LOCK_DIRECTORY\n";return 2;
    }
    using Clock=std::chrono::steady_clock;using namespace streamdeck_background;
    NativeClient native(argv[2],argv[3],20);nlohmann::json evidence;
    try {
        const auto deadline=Clock::now()+std::chrono::seconds(75);nlohmann::json before;
        std::cerr<<"WAITING: guarded natural composition, at most 75 seconds; no pixel writes while waiting\n";
        do {
            before=native.Request("/health",{});
            evidence["waitingStatus"]=before;
            if(before.value("ready",false))break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }while(Clock::now()<deadline);
        if(!before.value("ready",false))throw std::runtime_error("No guarded 5x3 compositor discovered within 75 seconds");
        const auto layout=ParseLayout(native.Request("/layout",{}));
        evidence["before"]=before;std::cerr<<"ANIMATION START: native in-process 800x600 gradient, 8 seconds, icons preserved by compositor\n";
        const auto start=Clock::now();unsigned frames=0;
        while(Clock::now()-start<std::chrono::seconds(8)) {
            const auto tick=Clock::now();
            auto bytes=std::make_shared<std::vector<unsigned char>>(800*600*4);
            const double phase=std::chrono::duration<double>(tick-start).count();
            for(unsigned y=0;y<600;++y)for(unsigned x=0;x<800;++x){const auto i=(y*800+x)*4;
                (*bytes)[i]=static_cast<unsigned char>(40+35*(1+std::sin(x/150.0+phase)));
                (*bytes)[i+1]=static_cast<unsigned char>(40+35*(1+std::sin(y/150.0+phase+2)));
                (*bytes)[i+2]=static_cast<unsigned char>(40+35*(1+std::sin((x+y)/150.0+phase+4)));(*bytes)[i+3]=255;}
            room_image::Frame frame;frame.width=800;frame.height=600;frame.stride=3200;frame.sequence=++frames;frame.pixels=bytes;
            if(native.Request("/frame",EncodeNativeTiles(frame,{},layout)).value("accepted",false)!=true)
                throw std::runtime_error("Native frame refused");
            std::this_thread::sleep_until(tick+std::chrono::milliseconds(50));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        evidence["after"]=native.Request("/health",{});evidence["submitted"]=frames;
        if(evidence["after"].value("errors",0)!=0 || evidence["after"].value("paints",0)<=before.value("paints",0))
            throw std::runtime_error("Frames accepted but no native painting confirmed");
        native.Request("/stop",{});const auto error=native.Close();
        evidence["restored"]=error.empty();evidence["closeError"]=error;evidence["ok"]=error.empty();
        std::cout<<evidence.dump()<<std::endl;return error.empty()?0:1;
    }catch(const std::exception& e){const auto restore=native.Close();evidence["ok"]=false;evidence["error"]=e.what();evidence["closeError"]=restore;std::cout<<evidence.dump()<<std::endl;return 1;}
}
