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
static volatile LONG synthetic_marks=0;
extern "C" __declspec(dllexport) __declspec(noinline) int RoomSyntheticMark()
{return InterlockedIncrement(&synthetic_marks);}
extern "C" __declspec(dllexport) __declspec(noinline) int RoomSyntheticMarks()
{return InterlockedCompareExchange(&synthetic_marks,0,0);}
extern "C" __declspec(dllexport) __declspec(noinline) int RoomSyntheticProbe(int value)
{
    volatile int result=value;
    for(int i=0;i<9;++i)result=result*11+i;
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
        Child child;
        {room_sd::Engine initialize_frida;} // GLib's embedded runtime before creating GError fixtures.
        // Deterministic error classification and cleanup-policy matrix. Error
        // text alone must never authorize a second attachment.
        unsigned lifecycle_checks=0;
        for(const char* phase:{"get-device","attach","create-script","load-script"}) {
            for(bool cancelled:{false,true}) {
                try {
                    auto* error=g_error_new_literal(G_IO_ERROR,cancelled?G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED,"Operation was cancelled");
                    room_sd::CheckOpenError(error,phase,child.process.dwProcessId,room_sd::Clock::now(),5000);
                    Check(false,"Injected open error not thrown");
                }catch(const room_sd::OpenFailure& failure) {
                    Check(failure.cancelled==cancelled,"Typed error classification ignores text");++lifecycle_checks;
                    Check(std::string(failure.what()).find(std::string("phase=")+phase)!=std::string::npos,"Open phase preserved");++lifecycle_checks;
                    Check(std::string(failure.what()).find("deadline_ms=5000")!=std::string::npos,"Open deadline preserved");++lifecycle_checks;
                }
            }
        }
        const room_sd::OpenFailure cancellation("load-script",1,500,500,true,"g-io-error-quark",G_IO_ERROR_CANCELLED,"cancelled");
        room_sd::CleanupResult clean;clean.had_session=true;clean.had_script=true;clean.load_attempted=true;clean.session_detached=true;clean.manager_closed=true;
        const auto policy=[&](room_sd::CleanupResult result,bool expected,const char* why){Check(result.AllowsRetry(cancellation)==expected,why);++lifecycle_checks;};
        policy(clean,true,"Partially loaded script explicitly detached can retry");
        auto cleanup=clean;cleanup.ok=false;policy(cleanup,false,"Unload/detach error forbids retry even when local detached state is true");
        cleanup=clean;cleanup.session_detached=false;policy(cleanup,false,"Missing detach acknowledgement forbids retry");
        cleanup=clean;cleanup.manager_closed=false;policy(cleanup,false,"Manager cleanup error forbids retry");
        cleanup=clean;cleanup.had_script=false;cleanup.load_attempted=false;policy(cleanup,true,"Create-script cancellation after confirmed detach can retry");
        room_sd::CleanupResult before_script;before_script.manager_closed=true;
        policy(before_script,true,"Cancelled attach without session or script can retry after manager cleanup");
        cleanup=before_script;cleanup.manager_closed=false;policy(cleanup,false,"No-session path still requires manager cleanup");
        cleanup=before_script;cleanup.had_script=true;policy(cleanup,false,"Unexpected script without session fails closed");
        cleanup=before_script;cleanup.load_attempted=true;policy(cleanup,false,"Unaccounted attempted load fails closed");
        const room_sd::OpenFailure actual_fault("load-script",1,500,500,false,"frida-error-quark",1,"Operation was cancelled");
        Check(!clean.AllowsRetry(actual_fault),"Real initialization error stays fatal after successful cleanup");++lifecycle_checks;
        try {room_sd::CheckOpenError(g_error_new_literal(g_quark_from_static_string("synthetic-other-domain"),G_IO_ERROR_CANCELLED,"Operation was cancelled"),"attach",1,room_sd::Clock::now(),5000);}
        catch(const room_sd::OpenFailure& failure){Check(!failure.cancelled,"Matching error code in another domain is not cancellation");++lifecycle_checks;}
        // Real cancellation after the child's script has already installed a
        // hook, but before load_sync returns success. Unload must not depend on
        // loaded=true. Cleanup and reopening use non-overlapping Engine scopes.
        {
            room_sd::Engine cancelled_engine;room_sd::OpenTimeouts limits;limits.load_script=500;
            bool cancelled=false;
            try {
                cancelled_engine.Open(child.process.dwProcessId,R"JS(
const mark=new NativeFunction(Process.mainModule.getExportByName('RoomSyntheticMark'),'int',[]);
const probe=new NativeFunction(Process.mainModule.getExportByName('RoomSyntheticProbe'),'int',['int']);
Interceptor.attach(Process.mainModule.getExportByName('RoomSyntheticProbe'),{onEnter(){mark();}});
probe(1);
new NativeFunction(Process.getModuleByName('kernel32.dll').getExportByName('Sleep'),'void',['uint'])(1500);
rpc.exports={status(){return {ready:true};}};
)JS",limits);
            }catch(const room_sd::OpenFailure& failure) {
                cancelled=true;room_sd::CleanupResult cleanup;std::string detail;
                Check(failure.cancelled,"Real load failure is typed cancellation");
                Check(std::string(failure.what()).find("phase=load-script")!=std::string::npos,"Real cancellation phase");
                Check(cancelled_engine.Close(&detail,&cleanup),"Cancelled load cleanup succeeded");
                Check(cleanup.load_attempted&&cleanup.had_script&&cleanup.had_session&&cleanup.session_detached,"Cancelled loaded hook detached explicitly");
                Check(cleanup.AllowsRetry(failure),"Verified cancelled load may reopen");
                std::cout<<failure.what()<<cleanup.Summary()<<"\n";
            }
            Check(cancelled,"Synthetic load must outlast its initialization budget");
        }
        {
            room_sd::Engine reopened;
            reopened.Open(child.process.dwProcessId,R"JS(
const mark=new NativeFunction(Process.mainModule.getExportByName('RoomSyntheticMark'),'int',[]);
const count=new NativeFunction(Process.mainModule.getExportByName('RoomSyntheticMarks'),'int',[]);
const address=Process.mainModule.getExportByName('RoomSyntheticProbe');
const probe=new NativeFunction(address,'int',['int']);
rpc.exports={verify(){const initial=count();for(let i=0;i<10;i++)probe(i);const afterCleanup=count();
 const hook=Interceptor.attach(address,{onEnter(){mark();}});for(let i=0;i<10;i++)probe(i);
 const afterReopen=count();hook.detach();return {initial,afterCleanup,afterReopen};}};
)JS");
            const auto counts=reopened.Call("verify");
            Check(counts["initial"].get<int>()>=1,"Cancelled load had really installed and executed its hook");
            Check(counts["afterCleanup"]==counts["initial"],"Cancelled script has no surviving hook");
            Check(counts["afterReopen"].get<int>()-counts["afterCleanup"].get<int>()==10,"Reopened session has exactly one hook");
            std::string detail;Check(reopened.Close(&detail),"Reopened test session detached");
            std::cout<<"PASS open lifecycle "<<lifecycle_checks<<" deterministic checks; real cancel/reopen hook counts "<<counts.dump()<<"\n";
        }
        std::cerr<<"test: engine init"<<std::endl;room_sd::Engine engine;
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
