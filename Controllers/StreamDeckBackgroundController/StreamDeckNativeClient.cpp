/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StreamDeckNativeClient.h"
#include "../../Native/StreamDeckCompositor/RoomStreamDeckNative.h"
#include <array>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace streamdeck_background {
namespace {
bool IsReplyTimeout(const std::string& error)
{
    // Old ABI1 conflates timeout and detach. Conservatively quarantine that
    // same handle; a later explicit detach/fault is fatal, never a reattach.
    if(error=="Native compositor RPC timed out or detached")return true;
    return error.find("Native compositor RPC method=")==0
        && error.find(" detached=false: Reply deadline expired")!=std::string::npos;
}
bool ValidDiagnostics(const nlohmann::json& value)
{
    return value.is_object() && value.contains("errors") && value["errors"].is_number_integer()
        && value["errors"]>=0 && value.contains("ready") && value["ready"].is_boolean();
}
}
struct NativeClient::Impl {
    std::string library,lock_directory;unsigned fps;void* handle=nullptr;
    std::uint32_t failed_pid=0,active_pid=0,observed_pid=0;
    Now now;
    bool recovering=false,opening_retry=false,has_sample=false;
    unsigned retry_seconds=1,recovery_attempts=0,open_retry_seconds=1,open_attempts=0;
    Clock::time_point next_probe{},next_open{},sampled_at{};
    std::string transport_state="disconnected",transport_error;
    nlohmann::json diagnostics;
    std::string close_error;
    nlohmann::json Snapshot(bool cached) const
    {
        auto value=diagnostics.is_object()?diagnostics:nlohmann::json::object();
        value["lastKnownReady"]=value.value("ready",false);
        if(transport_state!="active")value["ready"]=false;
        value["transportState"]=transport_state;
        value["transportError"]=transport_error.substr(0,512);
        value["cached"]=cached;
        value["sampledSteadyMs"]=has_sample?nlohmann::json(std::chrono::duration_cast<std::chrono::milliseconds>(sampled_at.time_since_epoch()).count()):nlohmann::json(nullptr);
        value["ageMs"]=has_sample?nlohmann::json(std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(now()-sampled_at).count())):nlohmann::json(nullptr);
        value["stale"]=!has_sample || transport_state!="active" || now()-sampled_at>std::chrono::seconds(5);
        value["recoveryAttempts"]=recovery_attempts;
        value["openAttempts"]=open_attempts;
        value["retryKind"]=opening_retry?"open":recovering?"rpc":"none";
        const auto deadline=opening_retry?next_open:next_probe;
        value["retryAfterMs"]=(opening_retry||recovering)?std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now()).count()):0;
        return value;
    }
#ifdef _WIN32
    HMODULE module=nullptr;
    decltype(&room_sd_abi) abi=nullptr;
    decltype(&room_sd_find_process) find_process=nullptr;
    decltype(&room_sd_open) open=nullptr;
    decltype(&room_sd_request) request=nullptr;
    decltype(&room_sd_close) close=nullptr;
    void Load()
    {
        if(module)return;
        const auto path=std::filesystem::u8path(library);
        if(!path.is_absolute())throw std::runtime_error("Native compositor DLL path must be absolute");
        module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)throw std::runtime_error("Optional RoomStreamDeckNative.dll unavailable");
        abi=reinterpret_cast<decltype(abi)>(GetProcAddress(module,"room_sd_abi"));
        find_process=reinterpret_cast<decltype(find_process)>(GetProcAddress(module,"room_sd_find_process"));
        open=reinterpret_cast<decltype(open)>(GetProcAddress(module,"room_sd_open"));
        request=reinterpret_cast<decltype(request)>(GetProcAddress(module,"room_sd_request"));
        close=reinterpret_cast<decltype(close)>(GetProcAddress(module,"room_sd_close"));
        if(!abi || !find_process || !open || !request || !close || abi()!=ROOM_SD_ABI)
        {FreeLibrary(module);module=nullptr;throw std::runtime_error("Unsupported native compositor C ABI");}
    }
    void Connect()
    {
        Load();if(handle)return;const auto pid=find_process();
        if(pid!=observed_pid)
        {
            // Never present a previous process's status as this target's health.
            observed_pid=pid;diagnostics=nullptr;has_sample=false;
            recovering=opening_retry=false;retry_seconds=open_retry_seconds=1;
            recovery_attempts=open_attempts=0;next_open={};close_error.clear();transport_error.clear();
        }
        if(!pid){transport_state="disconnected";transport_error="Waiting for the pinned Elgato application";throw std::runtime_error(transport_error);}
        if(pid==failed_pid){transport_state="faulted";throw std::runtime_error("Native compositor faulted; waiting for a new Elgato process");}
        if(opening_retry && now()<next_open)throw std::runtime_error(transport_error);
        std::array<char,16384> error{};
        ++open_attempts;
        const auto code=open(pid,lock_directory.c_str(),fps,&handle,error.data(),static_cast<uint32_t>(error.size()));
        if(code!=ROOM_SD_OK) {
            if(handle){active_pid=pid;Fatal("Native compositor open returned a handle on failure");}
            if(code==ROOM_SD_RETRY_OPEN && !handle)
            {
                // The DLL alone certifies typed initialization cancellation and
                // completed cleanup. Never infer retry safety from error text.
                open_retry_seconds=opening_retry?std::min(10u,open_retry_seconds*2):1;
                opening_retry=true;transport_state="recovering";
                transport_error="Native compositor opening retry after confirmed cleanup: "+std::string(error[0]?error.data():"Initialization cancelled");
                next_open=now()+std::chrono::seconds(open_retry_seconds);
                throw std::runtime_error(transport_error);
            }
            opening_retry=false;
            // A busy observer can be released normally. Unsupported guards and
            // attach faults stay suppressed for this process, not retried forever.
            if(code==ROOM_SD_GUARD || code==ROOM_SD_ATTACH || code==ROOM_SD_RPC || code==ROOM_SD_RETRY_OPEN)failed_pid=pid;
            transport_state=failed_pid==pid?"faulted":"disconnected";
            transport_error=error[0]?error.data():"Native compositor could not open";
            throw std::runtime_error(transport_error);
        }
        active_pid=pid;close_error.clear();diagnostics=nullptr;has_sample=false;recovering=opening_retry=false;
        recovery_attempts=0;retry_seconds=1;transport_state="active";transport_error.clear();
    }
    std::string Close()
    {
        if(!handle)return close_error;
        const auto closing_pid=active_pid;
        std::array<char,16384> error{};const auto code=close(handle,error.data(),static_cast<uint32_t>(error.size()));handle=nullptr;active_pid=0;recovering=false;
        close_error=code==ROOM_SD_OK?std::string():std::string(error[0]?error.data():"Native restoration incomplete");
        if(!close_error.empty()){failed_pid=closing_pid;transport_error=close_error;}
        transport_state=failed_pid==closing_pid?"faulted":"closed";
        return close_error;
    }
    void Recover(const std::string& reason)
    {
        opening_retry=false;
        retry_seconds=recovering?std::min(10u,retry_seconds*2):1;
        recovering=true;transport_state="recovering";transport_error=reason.substr(0,512);
        next_probe=now()+std::chrono::seconds(retry_seconds);
    }
    [[noreturn]] void Fatal(const std::string& reason)
    {
        failed_pid=active_pid;
        const auto cleanup=Close();
        transport_state="faulted";transport_error=reason+(cleanup.empty()?std::string():"; cleanup: "+cleanup);
        throw std::runtime_error(transport_error);
    }
    nlohmann::json Call(uint32_t operation,const std::string& body)
    {
        std::array<char,16384> result{},error{};
        const auto code=request(handle,operation,reinterpret_cast<const uint8_t*>(body.data()),
            static_cast<uint32_t>(body.size()),result.data(),static_cast<uint32_t>(result.size()),error.data(),static_cast<uint32_t>(error.size()));
        if(code!=ROOM_SD_OK)
        {
            const std::string reason=error[0]?error.data():"Native compositor request failed";
            if(code==ROOM_SD_RPC && IsReplyTimeout(reason))
            {
                Recover(reason);
                throw std::runtime_error(reason+"; recovering on existing session");
            }
            Fatal(reason);
        }
        auto value=nlohmann::json::parse(result.data(),nullptr,false);
        if(value.is_null() && operation==ROOM_SD_LAYOUT)throw std::runtime_error("Waiting for native Stream Deck layout");
        if(!value.is_object())Fatal("Invalid native compositor response");
        if(operation==ROOM_SD_STATUS)
        {
            if(!ValidDiagnostics(value))Fatal("Invalid native compositor health response");
            diagnostics=value;sampled_at=now();has_sample=true;
            if(value["errors"]!=0)
            {
                std::string detail="Native compositor reported a fault";
                const auto fault=value.find("lastFault");
                if(fault!=value.end() && fault->is_object())
                {
                    const auto message=fault->find("message");
                    if(message!=fault->end() && message->is_string())detail+=": "+message->get<std::string>().substr(0,512);
                }
                Fatal(detail); // Never export a native stack or arbitrary fault object.
            }
            if(recovering && !value["ready"].get<bool>())Recover("Native compositor is responding but its target is not ready");
            else {recovering=false;retry_seconds=1;transport_state="active";transport_error.clear();}
            return Snapshot(false);
        }
        return value;
    }
#endif
};
NativeClient::NativeClient(const std::string& library,const std::string& lock_directory,unsigned fps,Now now):impl(new Impl)
{impl->library=library;impl->lock_directory=lock_directory;impl->fps=fps;impl->now=now?std::move(now):Now([]{return Clock::now();});}
NativeClient::~NativeClient()
{
#ifdef _WIN32
    try{impl->Close();}catch(...){}if(impl->module)FreeLibrary(impl->module); // DLL pins itself before initializing Frida.
#endif
}
std::string NativeClient::Close()
{
#ifdef _WIN32
    return impl->Close();
#else
    return {};
#endif
}
nlohmann::json NativeClient::LastDiagnostics() const {return impl->Snapshot(true);}
nlohmann::json NativeClient::Request(const char* path,const std::string& body)
{
#ifdef _WIN32
    impl->Connect();std::uint32_t operation;
    const std::string route=path;
    if(route=="/health")operation=ROOM_SD_STATUS;
    else if(route=="/layout")operation=ROOM_SD_LAYOUT;
    else if(route=="/frame")operation=ROOM_SD_FRAME;
    else if(route=="/stop")operation=ROOM_SD_STOP;
    else throw std::runtime_error("Unknown native compositor operation");
    // Only a fresh health reply can end quarantine. Never send the old frame,
    // a layout request, or another mutating RPC while the dispatcher is busy.
    // Close() uses the separately bounded native restoration/cleanup path.
    if(impl->recovering)
    {
        if(operation!=ROOM_SD_STATUS || impl->now()<impl->next_probe)
            throw std::runtime_error("Native compositor recovering on existing session; waiting for scheduled health probe");
        ++impl->recovery_attempts;
    }
    return impl->Call(operation,body);
#else
    (void)path;(void)body;throw std::runtime_error("The optional native Elgato compositor requires Windows");
#endif
}
}
