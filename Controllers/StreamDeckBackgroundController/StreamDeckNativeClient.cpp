/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StreamDeckNativeClient.h"
#include "../../Native/StreamDeckCompositor/RoomStreamDeckNative.h"
#include <array>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace streamdeck_background {
struct NativeClient::Impl {
    std::string library,lock_directory;unsigned fps;void* handle=nullptr;
    std::uint32_t failed_pid=0,active_pid=0;
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
        if(!pid)throw std::runtime_error("Waiting for the pinned Elgato application");
        if(pid==failed_pid)throw std::runtime_error("Native compositor faulted; waiting for a new Elgato process");
        std::array<char,1024> error{};
        const auto code=open(pid,lock_directory.c_str(),fps,&handle,error.data(),static_cast<uint32_t>(error.size()));
        if(code!=ROOM_SD_OK) {
            // A busy observer can be released normally. Unsupported guards and
            // attach faults stay suppressed for this process, not retried forever.
            if(code==ROOM_SD_GUARD || code==ROOM_SD_ATTACH || code==ROOM_SD_RPC)failed_pid=pid;
            throw std::runtime_error(error[0]?error.data():"Native compositor could not open");
        }
        active_pid=pid;
    }
    std::string Close()
    {
        if(!handle)return {};
        std::array<char,1024> error{};const auto code=close(handle,error.data(),static_cast<uint32_t>(error.size()));handle=nullptr;active_pid=0;
        return code==ROOM_SD_OK?std::string():std::string(error[0]?error.data():"Native restoration incomplete");
    }
#endif
};
NativeClient::NativeClient(const std::string& library,const std::string& lock_directory,unsigned fps):impl(new Impl)
{impl->library=library;impl->lock_directory=lock_directory;impl->fps=fps;}
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
    std::array<char,16384> result{};std::array<char,1024> error{};
    const auto code=impl->request(impl->handle,operation,reinterpret_cast<const uint8_t*>(body.data()),
                                 static_cast<uint32_t>(body.size()),result.data(),static_cast<uint32_t>(result.size()),
                                 error.data(),static_cast<uint32_t>(error.size()));
    if(code!=ROOM_SD_OK) {
        impl->failed_pid=impl->active_pid;impl->Close();
        throw std::runtime_error(error[0]?error.data():"Native compositor request failed");
    }
    const auto value=nlohmann::json::parse(result.data(),nullptr,false);
    // layout is null until the guarded native composer is discovered.
    if(value.is_null() && operation==ROOM_SD_LAYOUT)throw std::runtime_error("Waiting for native Stream Deck layout");
    if(!value.is_object())throw std::runtime_error("Invalid native compositor response (operation="+route+", type="+value.type_name()+")");
    if(operation==ROOM_SD_STATUS && value.value("errors",0)!=0)
        throw std::runtime_error("Native compositor reported a fault");
    return value;
#else
    (void)path;(void)body;throw std::runtime_error("The optional native Elgato compositor requires Windows");
#endif
}
}
