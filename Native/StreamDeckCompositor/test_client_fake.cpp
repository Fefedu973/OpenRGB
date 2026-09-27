// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic C ABI only. No Frida, process lookup, USB, network or target app.
#define ROOM_SD_NATIVE_BUILD
#include "RoomStreamDeckNative.h"
#include <algorithm>
#include <cstring>
namespace {
int opens=0,closes=0,live=0,frames=0,accepted=0,failure=0,remaining=0,health=0;
bool stalled=false;
unsigned pid=10;
void Copy(const char* text,char* out,unsigned cap)
{ if(out&&cap){auto n=std::min<std::size_t>(std::strlen(text),cap-1);std::memcpy(out,text,n);out[n]=0;} }
}
extern "C" __declspec(dllexport) void fixture_reset()
{ opens=closes=live=frames=accepted=failure=remaining=health=0;pid=10;stalled=false; }
extern "C" __declspec(dllexport) void fixture_failure(int type,int count,int status)
{ failure=type;remaining=count;health=status; }
extern "C" __declspec(dllexport) int fixture_count(int which)
{ return which==0?opens:which==1?closes:which==2?live:which==4?accepted:frames; }
extern "C" __declspec(dllexport) void fixture_pid(unsigned value){pid=value;}
extern "C" __declspec(dllexport) void fixture_stalled(int value){stalled=value!=0;}
uint32_t room_sd_abi(){return ROOM_SD_ABI;}
uint32_t room_sd_find_process(){return pid;}
int room_sd_open(uint32_t,const char*,uint32_t,void** out,char*,uint32_t)
{ if(live)return ROOM_SD_BUSY;++opens;++live;*out=&live;return ROOM_SD_OK; }
int room_sd_close(void*,char* error,uint32_t cap)
{ ++closes;--live;Copy("",error,cap);return ROOM_SD_OK; }
int room_sd_request(void*,uint32_t op,const uint8_t*,uint32_t,char* out,uint32_t cap,char* error,uint32_t error_cap)
{
    if(op==ROOM_SD_STATUS)
    {
        if(health==1){Copy("Native compositor RPC timed out or detached",error,error_cap);return ROOM_SD_RPC;}
        Copy(health==2?"{\"errors\":1,\"ready\":true}":health==3?"{\"ready\":true}":health==4?"broken-json":
             stalled?"{\"errors\":0,\"ready\":false,\"stalled\":true,\"pending\":42}":
             "{\"errors\":0,\"ready\":true,\"stalled\":false,\"pending\":null}",out,cap);return ROOM_SD_OK;
    }
    if(op==ROOM_SD_FRAME)
    {
        ++frames;
        if(remaining>0)
        {
            --remaining;
            const char* text=failure==1?"Native compositor RPC timed out or detached":
                failure==2?"Native compositor RPC method=setframe pid=10 elapsed_ms=1501 deadline_ms=1500 detached=false: Reply deadline expired":
                failure==3?"Native compositor RPC method=setframe pid=10 elapsed_ms=12 deadline_ms=1500 detached=true: Session detached while awaiting RPC":
                "Native compositor reported a fault: invalid native image";
            Copy(text,error,error_cap);return ROOM_SD_RPC;
        }
        if(stalled){Copy("{\"accepted\":false,\"reason\":\"native-queue-stalled\"}",out,cap);return ROOM_SD_OK;}
        ++accepted;Copy("{\"accepted\":true}",out,cap);return ROOM_SD_OK;
    }
    Copy("{}",out,cap);return ROOM_SD_OK;
}
