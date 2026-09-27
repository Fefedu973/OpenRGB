/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
/* Internal engine; production exports never accept caller-supplied JavaScript.
 * The synthetic-process test uses this class directly with an inert RPC script. */
#include "frida-core.h"
#pragma comment(lib,"setupapi.lib")
#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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
class Engine {
    static std::mutex& Ownership(){static std::mutex value;return value;}
    std::unique_lock<std::mutex> ownership;
    GMainContext* context=nullptr;
    FridaDeviceManager* manager=nullptr;FridaDevice* device=nullptr;
    FridaSession* session=nullptr;FridaScript* script=nullptr;
    gulong message_handler=0,detach_handler=0;
    std::thread::id owner=std::this_thread::get_id();
    std::uint64_t next_id=0,waiting_id=0;
    bool replied=false,loaded=false,detached=false;Json reply;
    std::string failure,rpc_error;
    static void Message(FridaScript*,const gchar* text,GBytes*,gpointer self)
    {
        auto& engine=*static_cast<Engine*>(self);
        try {
            const auto value=Json::parse(text,nullptr,false);
            if(!value.is_object())return;
            if(value.value("type","")=="error") {engine.failure="Guarded compositor script failed";return;}
            if(value.value("type","")!="send" || !value.contains("payload"))return;
            const auto& payload=value["payload"];
            if(payload.is_array() && payload.size()>=4 && payload[0]=="frida:rpc"
               && payload[1].is_number_unsigned() && payload[1].get<std::uint64_t>()==engine.waiting_id)
            {
                engine.replied=true;
                if(payload[2]=="ok")engine.reply=payload[3];
                else engine.rpc_error=payload[3].is_string()?payload[3].get<std::string>():"RPC rejected";
            }
            else if(payload.is_object() && payload.value("event","")=="native-error")
                engine.failure="Native compositor reported a fault";
        }catch(...){engine.failure="Invalid native compositor response";}
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
    void Open(unsigned pid,const std::string& source)
    {
        CheckThread();GError* error=nullptr;
        ROOM_SD_TRACE("manager");
        manager=frida_device_manager_new();
        ROOM_SD_TRACE("get device");
        {Cancellation timeout;device=frida_device_manager_get_device_by_type_sync(manager,FRIDA_DEVICE_TYPE_LOCAL,1000,timeout,&error);}
        ThrowError(error,"Local Frida device unavailable");if(!device)throw std::runtime_error("Local Frida device unavailable");
        ROOM_SD_TRACE("attach");
        {Cancellation timeout;session=frida_device_attach_sync(device,pid,nullptr,timeout,&error);}
        ThrowError(error,"Attachment failed");if(!session)throw std::runtime_error("Attachment failed");
        ROOM_SD_TRACE("create script");
        detach_handler=g_signal_connect(session,"detached",G_CALLBACK(Detached),this);
        auto* options=frida_script_options_new();
        frida_script_options_set_name(options,"openrgb-room-guarded-background");
        frida_script_options_set_runtime(options,FRIDA_SCRIPT_RUNTIME_QJS);
        {Cancellation timeout;script=frida_session_create_script_sync(session,source.c_str(),options,timeout,&error);}
        g_object_unref(options);ThrowError(error,"Script creation failed");
        if(!script)throw std::runtime_error("Script creation failed");
        ROOM_SD_TRACE("load script");
        message_handler=g_signal_connect(script,"message",G_CALLBACK(Message),this);
        {Cancellation timeout;frida_script_load_sync(script,timeout,&error);}
        ThrowError(error,"Script loading failed");loaded=true;Pump();
        ROOM_SD_TRACE("loaded");
        if(!failure.empty())throw std::runtime_error(failure);
    }
    Json Call(const char* method,const Json& arguments=Json::array(),const void* data=nullptr,std::size_t size=0,unsigned timeout_ms=1500)
    {
        CheckThread();Pump();
        if(!script || !loaded || detached)throw std::runtime_error("Compositor session is disconnected");
        if(!failure.empty())throw std::runtime_error(failure);
        waiting_id=++next_id;replied=false;rpc_error.clear();reply=nullptr;
        const auto command=Json::array({"frida:rpc",waiting_id,"call",method,arguments}).dump();
        GBytes* bytes=data?g_bytes_new(data,size):nullptr;
        frida_script_post(script,command.c_str(),bytes);if(bytes)g_bytes_unref(bytes);
        const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
        while(!replied && !detached && failure.empty() && Clock::now()<deadline)
        {Pump();if(!replied)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(!failure.empty())throw std::runtime_error(failure);
        if(!replied)throw std::runtime_error("Native compositor RPC timed out or detached");
        if(!rpc_error.empty())throw std::runtime_error(rpc_error);
        return reply;
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
    bool Close(std::string* detail=nullptr)noexcept
    {
        ROOM_SD_TRACE("close");
        bool ok=true;
        const auto note=[&](GError* error) {
            if(error) {ok=false;if(detail && detail->empty())*detail=error->message?error->message:"Frida cleanup failed";g_error_free(error);}
        };
        // Bound each Frida operation; callbacks are disconnected before userdata dies.
        if(script) {
            if(message_handler)g_signal_handler_disconnect(script,message_handler);
            message_handler=0;
            if(loaded && !detached){GError* error=nullptr;Cancellation timeout;frida_script_unload_sync(script,timeout,&error);note(error);}
            g_object_unref(script);script=nullptr;loaded=false;
        }
        if(session) {
            if(detach_handler)g_signal_handler_disconnect(session,detach_handler);
            detach_handler=0;
            if(!detached){GError* error=nullptr;Cancellation timeout;frida_session_detach_sync(session,timeout,&error);note(error);}
            g_object_unref(session);session=nullptr;
        }
        if(device){g_object_unref(device);device=nullptr;}
        if(manager){GError* error=nullptr;Cancellation timeout;frida_device_manager_close_sync(manager,timeout,&error);note(error);g_object_unref(manager);manager=nullptr;}
        return ok;
    }
};
}
