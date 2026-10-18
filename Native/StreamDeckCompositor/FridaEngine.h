/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* Internal engine; production exports never accept caller-supplied JavaScript.
 * The synthetic-process test uses this class directly with an inert RPC script. */
#include "frida-core.h"
#include "RpcResponse.h"
#pragma comment(lib,"setupapi.lib")
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>
#ifdef ROOM_SD_ENGINE_TRACE
#include <cstdio>
#define ROOM_SD_TRACE(value) std::fprintf(stderr,"engine: %s\n",value)
#else
#define ROOM_SD_TRACE(value) ((void)0)
#endif

namespace room_sd {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
class Cancellation {
    GCancellable* value=g_cancellable_new();std::mutex mutex;std::condition_variable cv;
    bool done=false;std::thread timer;
public:
    explicit Cancellation(unsigned ms=2000):timer([this,ms] {
        std::unique_lock<std::mutex> lock(mutex);
        if(!cv.wait_for(lock,std::chrono::milliseconds(ms),[this]{return done;}))g_cancellable_cancel(value);
    }){}
    ~Cancellation(){{std::lock_guard<std::mutex> lock(mutex);done=true;}cv.notify_one();timer.join();g_object_unref(value);}
    operator GCancellable*()const{return value;}
};
inline void ThrowError(GError* error,const char* fallback)
{
    if(!error)return;const std::string message=error->message?error->message:fallback;
    g_error_free(error);throw std::runtime_error(message);
}
struct OpenFailure:std::runtime_error {
    bool cancelled;
    OpenFailure(const std::string& phase,unsigned pid,long long elapsed,unsigned deadline,
                bool was_cancelled,const std::string& domain,int code,const std::string& message)
        :std::runtime_error("Native compositor open phase="+phase+" pid="+std::to_string(pid)
            +" elapsed_ms="+std::to_string(elapsed)+" deadline_ms="+std::to_string(deadline)
            +" cancelled="+(was_cancelled?"true":"false")+" domain="+domain
            +" code="+std::to_string(code)+": "+message),cancelled(was_cancelled){}
};
inline void CheckOpenError(GError* error,const char* phase,unsigned pid,Clock::time_point started,unsigned deadline)
{
    if(!error)return;
    const bool cancelled=g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)!=FALSE;
    const char* domain=g_quark_to_string(error->domain);
    OpenFailure failure(phase,pid,std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count(),
                        deadline,cancelled,domain?domain:"unknown",error->code,error->message?error->message:"Initialization failed");
    g_error_free(error);throw failure;
}
struct OpenTimeouts {
    // Loading a newly started Elgato process can require more than the normal
    // RPC budget. This changes initialization only, not frame/cleanup latency.
    unsigned get_device=2000,attach=5000,create_script=2000,load_script=5000;
};
struct CleanupResult {
    bool ok=true,had_session=false,had_script=false,load_attempted=false;
    bool session_detached=false,manager_closed=false;
    bool AllowsRetry(const OpenFailure& failure)const {
        // A cancelled attach may return no session, but our code has not yet
        // created any script. Once a session exists, detachment must be proven.
        return failure.cancelled && ok && manager_closed
            && (had_session?session_detached:(!had_script&&!load_attempted));
    }
    std::string Summary()const {
        return std::string(" cleanup_ok=")+(ok?"true":"false")
            +" load_attempted="+(load_attempted?"true":"false")
            +" detach_confirmed="+(session_detached?"true":"false")
            +" manager_closed="+(manager_closed?"true":"false");
    }
};
class Engine {
    static std::mutex& Ownership(){static std::mutex value;return value;}
    std::unique_lock<std::mutex> ownership;
    GMainContext* context=nullptr;
    FridaDeviceManager* manager=nullptr;FridaDevice* device=nullptr;
    FridaSession* session=nullptr;FridaScript* script=nullptr;
    gulong message_handler=0,detach_handler=0;
    std::thread::id owner=std::this_thread::get_id();
    std::uint64_t next_id=0;
    unsigned target_pid=0;
    bool loaded=false,load_attempted=false;
    std::atomic<bool> detached{false};
    RpcResponse response;
    static void Message(FridaScript*,const gchar* text,GBytes*,gpointer self)
    {
        static_cast<Engine*>(self)->response.Message(text);
    }
    static void Detached(FridaSession*,FridaSessionDetachReason,FridaCrash*,gpointer self)
    { static_cast<Engine*>(self)->detached=true; }
    void Pump()
    { for(unsigned i=0;i<256 && g_main_context_pending(context);++i)g_main_context_iteration(context,FALSE); }
public:
    Engine():ownership(Ownership(),std::try_to_lock)
    {
        if(!ownership.owns_lock())throw std::runtime_error("A native compositor session already owns the GLib dispatcher");
        if(std::string(frida_version_string())!="17.18.0")throw std::runtime_error("This compositor requires Frida Core 17.18.0");
        static std::once_flag initialized;std::call_once(initialized,[]{frida_init();});
        context=g_main_context_ref(g_main_context_default());
    }
    Engine(const Engine&)=delete;
    ~Engine(){Close();g_main_context_unref(context);}
    void CheckThread()const
    {if(std::this_thread::get_id()!=owner)throw std::runtime_error("Native compositor handle used from another thread");}
    void Open(unsigned pid,const std::string& source,const OpenTimeouts& timeouts={})
    {
        CheckThread();target_pid=pid;GError* error=nullptr;
        ROOM_SD_TRACE("manager");
        manager=frida_device_manager_new();
        ROOM_SD_TRACE("get device");
        auto started=Clock::now();
        {Cancellation timeout(timeouts.get_device);device=frida_device_manager_get_device_by_type_sync(manager,FRIDA_DEVICE_TYPE_LOCAL,1000,timeout,&error);}
        CheckOpenError(error,"get-device",pid,started,timeouts.get_device);if(!device)throw std::runtime_error("Native compositor open phase=get-device: Local Frida device unavailable");
        ROOM_SD_TRACE("attach");
        started=Clock::now();
        auto* session_options=frida_session_options_new();
        // A broken connection must not preserve an orphan script remotely.
        frida_session_options_set_persist_timeout(session_options,0);
        {Cancellation timeout(timeouts.attach);session=frida_device_attach_sync(device,pid,session_options,timeout,&error);}
        g_object_unref(session_options);
        CheckOpenError(error,"attach",pid,started,timeouts.attach);if(!session)throw std::runtime_error("Native compositor open phase=attach: Attachment failed");
        ROOM_SD_TRACE("create script");
        detach_handler=g_signal_connect(session,"detached",G_CALLBACK(Detached),this);
        auto* options=frida_script_options_new();
        frida_script_options_set_name(options,"openrgb-room-guarded-background");
        frida_script_options_set_runtime(options,FRIDA_SCRIPT_RUNTIME_QJS);
        started=Clock::now();
        {Cancellation timeout(timeouts.create_script);script=frida_session_create_script_sync(session,source.c_str(),options,timeout,&error);}
        g_object_unref(options);CheckOpenError(error,"create-script",pid,started,timeouts.create_script);
        if(!script)throw std::runtime_error("Script creation failed");
        ROOM_SD_TRACE("load script");
        message_handler=g_signal_connect(script,"message",G_CALLBACK(Message),this);
        started=Clock::now();load_attempted=true;
        {Cancellation timeout(timeouts.load_script);frida_script_load_sync(script,timeout,&error);}
        CheckOpenError(error,"load-script",pid,started,timeouts.load_script);loaded=true;Pump();
        ROOM_SD_TRACE("loaded");
        const auto fault=response.Read().fault;
        if(!fault.empty())throw std::runtime_error(fault);
    }
    Json Call(const char* method,const Json& arguments=Json::array(),const void* data=nullptr,std::size_t size=0,unsigned timeout_ms=1500)
    {
        CheckThread();
        const auto started=Clock::now();
        try {
        Pump();
        if(!script || !loaded || detached)throw std::runtime_error("Compositor session is disconnected");
        const bool diagnostics=std::string(method)=="status";
        const auto fault=response.Read().fault;
        if(!diagnostics && !fault.empty())throw std::runtime_error(fault);
        const auto waiting_id=++next_id;response.Begin(waiting_id);
        const auto command=Json::array({"frida:rpc",waiting_id,"call",method,arguments}).dump();
        GBytes* bytes=data?g_bytes_new(data,size):nullptr;
        frida_script_post(script,command.c_str(),bytes);if(bytes)g_bytes_unref(bytes);
        const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
        do {
            Pump();
            const auto result=response.Read();
            if(!diagnostics && !result.fault.empty())throw std::runtime_error(result.fault);
            if(result.completed) {
                if(!result.error.empty())throw std::runtime_error(result.error);
                return result.value;
            }
            if(detached || Clock::now()>=deadline)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }while(true);
        throw std::runtime_error(detached ? "Session detached while awaiting RPC" : "Reply deadline expired");
        }catch(const std::exception& error) {
            const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count();
            // Method names are internal constants. Never include RPC arguments,
            // frame bytes, a screen image or a user profile in this diagnostic.
            throw std::runtime_error("Native compositor RPC method="+std::string(method)
                +" pid="+std::to_string(target_pid)+" elapsed_ms="+std::to_string(elapsed)
                +" deadline_ms="+std::to_string(timeout_ms)+" detached="+(detached ? "true" : "false")
                +": "+error.what());
        }
    }
    bool Restore(std::string& detail)
    {
        if(!loaded)return true;
        if(detached){detail="Target detached before restoration could be acknowledged";return false;}
        try {
            Call("stop",Json::array(),nullptr,0,500);
            const auto deadline=Clock::now()+std::chrono::milliseconds(3000);
            do {
                const auto status=Call("status",Json::array(),nullptr,0,500);
                if(status.value("errors",0)!=0)throw std::runtime_error("Native compositor faulted before restoration");
                if(status.contains("pending") && status["pending"].is_null() && !status.value("restorationPending",true))return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }while(Clock::now()<deadline);
            detail="Native background restoration did not acknowledge within 3 seconds";
        }catch(const std::exception& e){detail=e.what();}
        return false;
    }
    bool Close(std::string* detail=nullptr,CleanupResult* outcome=nullptr)noexcept
    {
        ROOM_SD_TRACE("close");
        CleanupResult result;result.had_session=session!=nullptr;result.had_script=script!=nullptr;result.load_attempted=load_attempted;
        const auto note=[&](GError* error,const char* phase) {
            if(error) {result.ok=false;if(detail && detail->empty())*detail=std::string(phase)+": "+(error->message?error->message:"Frida cleanup failed");g_error_free(error);}
        };
        // Bound each Frida operation; callbacks are disconnected before userdata dies.
        if(script) {
            if(message_handler)g_signal_handler_disconnect(script,message_handler);
            message_handler=0;
            if(load_attempted && !detached && !frida_script_is_destroyed(script)){GError* error=nullptr;Cancellation timeout;frida_script_unload_sync(script,timeout,&error);note(error,"unload");}
            g_object_unref(script);script=nullptr;loaded=false;load_attempted=false;
        }
        if(session) {
            if(detach_handler)g_signal_handler_disconnect(session,detach_handler);
            detach_handler=0;
            // Frida sets its local detached flag before remote release finishes.
            // An already-set flag is not sufficient evidence for retrying open.
            if(!frida_session_is_detached(session)){
                GError* error=nullptr;Cancellation timeout;frida_session_detach_sync(session,timeout,&error);
                result.session_detached=error==nullptr && frida_session_is_detached(session)!=FALSE;
                note(error,"detach");
                if(!result.session_detached){result.ok=false;if(detail && detail->empty())*detail="Session detachment not confirmed";}
            }
            g_object_unref(session);session=nullptr;
        }
        if(device){g_object_unref(device);device=nullptr;}
        if(manager){GError* error=nullptr;Cancellation timeout;frida_device_manager_close_sync(manager,timeout,&error);result.manager_closed=error==nullptr;note(error,"manager-close");g_object_unref(manager);manager=nullptr;}
        if(outcome)*outcome=result;
        return result.ok;
    }
};
}
