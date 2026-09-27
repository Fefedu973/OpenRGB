/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "Guard.h"
#define ROOM_SD_ENGINE_TRACE
#include "FridaEngine.h"
#include "RoomStreamDeckNative.h"
#include <fcntl.h>
#include <io.h>
#include <sys/locking.h>
#include <sys/stat.h>
#include <iostream>

static void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F>static void Reject(F fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}Check(failed,"Expected rejection");}
struct Child {
    HANDLE stop=nullptr;PROCESS_INFORMATION process{};
    Child() {
        const auto name=L"Local\\RoomFridaSynthetic-"+std::to_wstring(GetCurrentProcessId());
        stop=CreateEventW(nullptr,TRUE,FALSE,name.c_str());Check(stop!=nullptr,"CreateEvent");
        std::wstring exe(32768,L'\0');exe.resize(GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size())));
        std::wstring command=L"\""+exe+L"\" --child \""+name+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);
        Check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Create synthetic child");
    }
    ~Child(){if(stop)SetEvent(stop);if(process.hProcess){WaitForSingleObject(process.hProcess,5000);CloseHandle(process.hProcess);CloseHandle(process.hThread);}if(stop)CloseHandle(stop);}
};
int wmain(int argc,wchar_t** argv)
{
    if(argc==3 && std::wstring(argv[1])==L"--child") {
        HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!event)return 2;
        WaitForSingleObject(event,30000);CloseHandle(event);return 0;
    }
    try {
        std::cerr<<"test: guards"<<std::endl;
        const auto directory=std::filesystem::temp_directory_path()/(L"room-native-compositor-test-"+std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(directory);
        {std::ofstream file(directory/L"abc",std::ios::binary);file<<"abc";}
        Check(room_sd::Sha256(directory/L"abc")=="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD","SHA256 exact");
        Reject([&]{room_sd::CheckBuild(GetCurrentProcessId());}); // never targets Elgato
        const auto lock_file=directory/(L"observer-"+std::to_wstring(GetCurrentProcessId())+L".lock");
        const int fd=_wopen(lock_file.c_str(),_O_CREAT|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE);
        Check(fd>=0,"CRT lock file");Check(_write(fd,"0",1)==1,"CRT lock byte");_lseek(fd,0,SEEK_SET);
        Check(_locking(fd,_LK_NBLCK,1)==0,"CRT observer lock");
        Reject([&]{room_sd::ObserverLock lock;lock.Acquire(directory,GetCurrentProcessId());});
        _lseek(fd,0,SEEK_SET);_locking(fd,_LK_UNLCK,1);_close(fd);
        {room_sd::ObserverLock first;first.Acquire(directory,GetCurrentProcessId());Reject([&]{room_sd::ObserverLock second;second.Acquire(directory,GetCurrentProcessId());});}
        {room_sd::ObserverLock after;after.Acquire(directory,GetCurrentProcessId());}
        std::cout<<"PASS pinned-path rejection, SHA256 and CRT-compatible observer lock\n";
        std::cerr<<"test: create child"<<std::endl;
        Child child;std::cerr<<"test: engine init"<<std::endl;room_sd::Engine engine;
        const char* synthetic=R"JS(
let armed=false,deadline=0,frames=0;
rpc.exports={configure(o){return o;},layout(){return {width:480,height:272};},
status(){return {ready:true,armed:armed&&Date.now()<deadline,pending:null,restorationPending:false,errors:0,frames};},
setframe(ms,data){if(!(data instanceof ArrayBuffer)||data.byteLength!==311040)throw Error('frame');let b=new Uint8Array(data);if(b[0]!==173||b[b.length-1]!==255)throw Error('bytes');frames++;armed=true;deadline=Date.now()+ms;return {accepted:true,bytes:b.length,blue:b[0]};},
stall(){return new Promise(()=>{});},stop(){armed=false;return {stopped:true,restorationPending:false};}};
)JS";
        std::cerr<<"test: attach synthetic pid "<<child.process.dwProcessId<<std::endl;
        engine.Open(child.process.dwProcessId,synthetic);
        std::cerr<<"test: RPC"<<std::endl;
        Check(engine.Call("configure",room_sd::Json::array({{{"maxFps",20}}}))["maxFps"]==20,"RPC configure");
        Check(engine.Call("layout")["width"]==480,"RPC layout");
        std::vector<unsigned char> frame(311040,0);frame[0]=173;for(std::size_t i=3;i<frame.size();i+=4)frame[i]=255;
        const auto start=room_sd::Clock::now();
        for(int i=0;i<20;++i)Check(engine.Call("setframe",room_sd::Json::array({100}),frame.data(),frame.size())["accepted"]==true,"Binary frame RPC");
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(room_sd::Clock::now()-start).count();
        Check(engine.Call("status")["frames"]==20,"All RPCs completed");
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        Check(engine.Call("status")["armed"]==false,"Image lease expired");
        const auto timeout_start=room_sd::Clock::now();
        Reject([&]{engine.Call("stall",room_sd::Json::array(),nullptr,0,100);});
        Check(room_sd::Clock::now()-timeout_start<std::chrono::seconds(1),"Bounded RPC timeout");
        std::string detail;Check(engine.Restore(detail),"Synthetic restoration acknowledged");
        const auto close_start=room_sd::Clock::now();engine.Close();
        const auto closed=std::chrono::duration_cast<std::chrono::milliseconds>(room_sd::Clock::now()-close_start).count();
        Check(closed<7000,"Bounded close");
        std::filesystem::remove(directory/L"abc");std::filesystem::remove(lock_file);std::filesystem::remove(directory);
        std::cout<<"{\"ok\":true,\"syntheticOnly\":true,\"binaryFrames\":20,\"frameBytes\":311040,\"rpcTotalMs\":"<<elapsed<<",\"closeMs\":"<<closed<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
