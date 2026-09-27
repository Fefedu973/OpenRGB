/* SPDX-License-Identifier: GPL-2.0-or-later */
#define ROOM_SD_NATIVE_BUILD
#include "RoomStreamDeckNative.h"
#include "Guard.h"
#include "FridaEngine.h"
#include "GuardedScript.inc"
#include <cstring>
#include <memory>

namespace {
struct Handle {room_sd::ObserverLock lock;room_sd::Engine engine;};
void Copy(const std::string& value,char* buffer,std::uint32_t capacity)
{ if(buffer && capacity){const auto n=std::min<std::size_t>(value.size(),capacity-1);memcpy(buffer,value.data(),n);buffer[n]=0;} }
}
uint32_t room_sd_abi(void){return ROOM_SD_ABI;}
uint32_t room_sd_find_process(void){try{return room_sd::FindProcess();}catch(...){return 0;}}
int room_sd_open(uint32_t pid,const char* directory,uint32_t fps,void** output,char* error,uint32_t capacity)
{
    if(output)*output=nullptr;Copy("",error,capacity);
    if(!output || !directory || fps<1 || fps>20)return ROOM_SD_INVALID;
    int stage=ROOM_SD_GUARD;
    try {
        const auto path=std::filesystem::u8path(directory);
        if(!path.is_absolute())throw std::runtime_error("Observer lock directory must be absolute");
        if(!pid)pid=room_sd::FindProcess();room_sd::CheckBuild(pid);
        // The process cannot unload a library still executing Frida's worker threads.
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&room_sd_open),&module))
            throw std::runtime_error("Unable to pin native compositor module");
        auto handle=std::make_unique<Handle>();stage=ROOM_SD_BUSY;handle->lock.Acquire(path,pid);
        stage=ROOM_SD_ATTACH;handle->engine.Open(pid,ROOM_GUARDED_SCRIPT);
        handle->engine.Call("configure",room_sd::Json::array({{{"maxFps",fps}}}));
        *output=handle.release();return ROOM_SD_OK;
    }catch(const std::exception& e){Copy(e.what(),error,capacity);return stage;}
    catch(...){Copy("Unknown native compositor failure",error,capacity);return stage;}
}
int room_sd_request(void* pointer,uint32_t operation,const uint8_t* data,uint32_t size,
                    char* result,uint32_t result_capacity,char* error,uint32_t error_capacity)
{
    Copy("",error,error_capacity);
    if(!pointer || !result || result_capacity<2)return ROOM_SD_INVALID;
    try {
        auto& engine=static_cast<Handle*>(pointer)->engine;room_sd::Json value;
        switch(operation) {
        case ROOM_SD_STATUS:value=engine.Call("status");break;
        case ROOM_SD_LAYOUT:value=engine.Call("layout");break;
        case ROOM_SD_FRAME:
            if(!data || size!=311040)throw std::runtime_error("Expected 311040 opaque BGRA tile bytes");
            for(std::size_t i=3;i<size;i+=4)if(data[i]!=255)throw std::runtime_error("Only opaque BGRA accepted");
            value=engine.Call("setframe",room_sd::Json::array({2000}),data,size);break;
        case ROOM_SD_STOP:value=engine.Call("stop");break;
        default:return ROOM_SD_INVALID;
        }
        const auto encoded=value.dump();if(encoded.size()>=result_capacity)throw std::runtime_error("Native result buffer too small");
        Copy(encoded,result,result_capacity);return ROOM_SD_OK;
    }catch(const std::exception& e){Copy(e.what(),error,error_capacity);return ROOM_SD_RPC;}
    catch(...){Copy("Unknown native RPC failure",error,error_capacity);return ROOM_SD_RPC;}
}
int room_sd_close(void* pointer,char* error,uint32_t capacity)
{
    Copy("",error,capacity);if(!pointer)return ROOM_SD_OK;
    auto* handle=static_cast<Handle*>(pointer);
    try {
        handle->engine.CheckThread();std::string detail;const bool restored=handle->engine.Restore(detail);
        const bool closed=handle->engine.Close(&detail);
        delete handle;Copy(detail,error,capacity);return restored&&closed?ROOM_SD_OK:ROOM_SD_RPC;
    }catch(const std::exception& e){Copy(e.what(),error,capacity);return ROOM_SD_RPC;}
}
