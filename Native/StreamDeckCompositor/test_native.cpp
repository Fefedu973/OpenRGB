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

// Natural native-thread entry points for testing Interceptor callback context.
// Distinct nontrivial bodies prevent linker folding and leave hookable prologues.
extern "C" __declspec(dllexport) __declspec(noinline) int RoomSyntheticOuter(int value)
{
    volatile int result=value;
    for(int i=0;i<8;++i)result=result*3+i;
    return result;
}
extern "C" __declspec(dllexport) __declspec(noinline) int RoomSyntheticInner(int value)
{
    volatile int result=value;
    for(int i=0;i<8;++i)result=result*7+i;
    return result;
}

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
        for(int i=0;i<300 && WaitForSingleObject(event,100)==WAIT_TIMEOUT;++i)RoomSyntheticOuter(i);
        CloseHandle(event);return 0;
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
        // Exercise the production acceptance gate, not a duplicate test-only
        // implementation, with the same Windows clock in both processes.
        std::ifstream source_file(std::filesystem::path(__FILE__).parent_path()/"background-core.js",std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(source_file)),std::istreambuf_iterator<char>());
        const auto lease_begin=source.find("// BEGIN FRAME LEASE"),lease_end=source.find("// END FRAME LEASE");
        Check(lease_begin!=std::string::npos && lease_end>lease_begin,"Production frame-lease source markers");
        const std::string synthetic=source.substr(lease_begin,lease_end-lease_begin)+R"JS(
let armed=false,deadline=0,frames=0,expired=0,nativeSleeps=0;
const clock=new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('GetTickCount64'),'uint64',[],'win64');
const sleep=new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('Sleep'),'void',['uint'],'win64');
const now=()=>clock().toNumber(),gate=new FrameLease();
rpc.exports={configure(o){return o;},layout(){return {width:480,height:272};},
reentrancy(){return new Promise(resolve=>{
 const outer=Process.mainModule.getExportByName('RoomSyntheticOuter');
 const inner=Process.mainModule.getExportByName('RoomSyntheticInner');
 const callInner=new NativeFunction(inner,'int',['int']);
 let innerCalls=0,entered=false;
 const innerHook=Interceptor.attach(inner,{onEnter(){innerCalls++;}});
 const outerHook=Interceptor.attach(outer,{onEnter(){
  if(entered)return;entered=true;
  callInner(7);const nested=innerCalls;
  setTimeout(()=>{
   const before=innerCalls;callInner(7);const deferred=innerCalls-before;
   outerHook.detach();innerHook.detach();
   resolve({nested,deferred});
  },0);
 }});
});},
status(){return {ready:true,armed:armed&&now()<deadline,pending:null,restorationPending:false,errors:0,frames,expired,nativeSleeps};},
setframe(ms,expiresAt,sequence,data){if(!(data instanceof ArrayBuffer)||data.byteLength!==311040)throw Error('frame');const reason=gate.check(ms,expiresAt,sequence,now());if(reason){if(reason==='native-frame-expired')expired++;return {accepted:false,reason};}let b=new Uint8Array(data);if(b[0]!==173||b[b.length-1]!==255)throw Error('bytes');frames++;armed=true;deadline=expiresAt;return {accepted:true,bytes:b.length,blue:b[0]};},
block(ms){const until=Date.now()+ms;while(Date.now()<until){}return {completed:true};},
nativeblock(ms){setTimeout(()=>{sleep(ms);nativeSleeps++;},10);return {scheduled:true};},
fault(){send({event:'native-error',message:'synthetic coverage failure',stack:'fixture:1'});return {};},
stall(){return new Promise(()=>{});},stop(){armed=false;return {stopped:true,restorationPending:false};}};
)JS";
        std::cerr<<"test: attach synthetic pid "<<child.process.dwProcessId<<std::endl;
        engine.Open(child.process.dwProcessId,synthetic);
        std::cerr<<"test: RPC"<<std::endl;
        Check(engine.Call("configure",room_sd::Json::array({{{"maxFps",20}}}))["maxFps"]==20,"RPC configure");
        Check(engine.Call("layout")["width"]==480,"RPC layout");
        const auto reentrancy=engine.Call("reentrancy");
        std::cout<<"Interceptor reentrancy: "<<reentrancy.dump()<<"\n";
        Check(reentrancy["nested"]==0,"NativeFunction inside Interceptor suppresses nested listener");
        Check(reentrancy["deferred"]==1,"Timer outside Interceptor observes native listener");
        std::vector<unsigned char> frame(311040,0);frame[0]=173;for(std::size_t i=3;i<frame.size();i+=4)frame[i]=255;
        std::uint64_t input_sequence=0;
        const auto arguments=[&](unsigned ms=500){return room_sd::Json::array({ms,GetTickCount64()+ms,++input_sequence});};
        const auto start=room_sd::Clock::now();
        for(int i=0;i<20;++i)Check(engine.Call("setframe",arguments(),frame.data(),frame.size())["accepted"]==true,"Binary frame RPC");
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(room_sd::Clock::now()-start).count();
        Check(engine.Call("status")["frames"]==20,"All RPCs completed");
        std::this_thread::sleep_for(std::chrono::milliseconds(520));
        Check(engine.Call("status")["armed"]==false,"Image lease expired");
        // A temporarily blocked target dispatches this RPC after its deadline.
        // Its late reply is ignored by Engine, but the exact production gate
        // must still reject its pixels and let this same session recover.
        Reject([&]{engine.Call("block",room_sd::Json::array({1200}),nullptr,0,100);});
        Reject([&]{engine.Call("setframe",arguments(),frame.data(),frame.size(),100);});
        std::this_thread::sleep_for(std::chrono::milliseconds(1250));
        const auto delayed=engine.Call("status");
        Check(delayed["expired"]==1 && delayed["frames"]==20 && delayed["armed"]==false,"Late timed-out frame cannot restart a lease");
        Check(engine.Call("setframe",arguments(),frame.data(),frame.size())["accepted"]==true,"Same session accepts fresh frame after blocked dispatcher");
        std::string detail;Check(engine.Restore(detail),"Restore after delayed frame");
        // Model a native call made by the timer, without attributing the live
        // incident to this mechanism. Frida's cooperative JS lock alone does
        // not promise that its RPC event dispatcher remains available.
        Check(engine.Call("nativeblock",room_sd::Json::array({1500}))["scheduled"]==true,"Native timer block scheduled");
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        bool native_status_timed_out=false;
        try {engine.Call("status",room_sd::Json::array(),nullptr,0,100);}
        catch(const std::exception&) {native_status_timed_out=true;}
        bool native_frame_timed_out=false;
        try {engine.Call("setframe",arguments(),frame.data(),frame.size(),100);}
        catch(const std::exception&) {native_frame_timed_out=true;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1550));
        const auto after_native=engine.Call("status");
        Check(after_native["nativeSleeps"]==1,"Native timer call completed");
        Check(after_native["armed"]==false,"Native timer cannot extend original image deadline");
        if(native_frame_timed_out)Check(after_native["expired"]==2 && after_native["frames"]==21,"Frame delayed by native timer rejected");
        std::cout<<"Native timer dispatch: statusTimeout="<<native_status_timed_out<<", frameTimeout="<<native_frame_timed_out<<", status="<<after_native.dump()<<"\n";
        Check(engine.Call("setframe",arguments(),frame.data(),frame.size())["accepted"]==true,"Same session accepts fresh frame after native timer");
        const auto accepted_frames=engine.Call("status")["frames"];
        const auto timeout_start=room_sd::Clock::now();
        std::string timeout_detail;
        try { engine.Call("stall",room_sd::Json::array(),nullptr,0,100); }
        catch(const std::exception& error) { timeout_detail=error.what(); }
        Check(timeout_detail.find("method=stall")!=std::string::npos,"Timeout names the RPC");
        Check(timeout_detail.find("pid="+std::to_string(child.process.dwProcessId))!=std::string::npos,"Timeout identifies target PID");
        Check(timeout_detail.find("elapsed_ms=")!=std::string::npos && timeout_detail.find("deadline_ms=100")!=std::string::npos,"Timeout records actual elapsed time and configured bound");
        Check(timeout_detail.find("detached=false")!=std::string::npos && timeout_detail.find("Reply deadline expired")!=std::string::npos,"Timeout distinguishes detach from no reply");
        Check(room_sd::Clock::now()-timeout_start<std::chrono::seconds(1),"Bounded RPC timeout");
        Check(engine.Restore(detail),"Synthetic restoration acknowledged");
        Reject([&]{engine.Call("fault");});
        Check(engine.Call("status")["frames"]==accepted_frames,"Read-only status unavailable after fault");
        Reject([&]{engine.Call("setframe",arguments(),frame.data(),frame.size());});
        const auto close_start=room_sd::Clock::now();engine.Close();
        const auto closed=std::chrono::duration_cast<std::chrono::milliseconds>(room_sd::Clock::now()-close_start).count();
        Check(closed<7000,"Bounded close");
        std::filesystem::remove(directory/L"abc");std::filesystem::remove(lock_file);std::filesystem::remove(directory);
        std::cout<<"{\"ok\":true,\"syntheticOnly\":true,\"binaryFrames\":20,\"frameBytes\":311040,\"rpcTotalMs\":"<<elapsed<<",\"closeMs\":"<<closed<<"}\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
