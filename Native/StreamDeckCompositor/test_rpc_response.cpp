/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RpcResponse.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using Json=nlohmann::json;
static void Check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static std::string Reply(unsigned id,const char* status,const Json& value)
{return Json{{"type","send"},{"payload",Json::array({"frida:rpc",id,status,value})}}.dump();}
int main()
{
    try {
        room_sd::RpcResponse box;
        box.Begin(1);box.Message(Reply(2,"ok",{{"accepted",true}}).c_str());
        Check(!box.Read().completed,"Wrong request id completed active request");
        for(const auto* text:{"invalid","[]","{\"type\":null}","{\"type\":\"send\",\"payload\":{\"event\":null}}"})box.Message(text);
        Check(!box.Read().completed && box.Read().fault.empty(),"Unrelated envelope poisoned request");
        box.Message(Reply(1,"ok",{{"accepted",true}}).c_str());
        Check(box.Read().completed && box.Read().value.at("accepted")==true,"Object reply absent");
        box.Message(Reply(1,"ok",nullptr).c_str());
        Check(box.Read().value.is_object(),"Duplicate reply replaced completed result");
        box.Begin(2);box.Message(Reply(2,"ok",nullptr).c_str());
        Check(box.Read().completed && box.Read().value.is_null(),"Legitimate undiscovered layout null lost");
        box.Begin(3);box.Message(Reply(3,"error","expected error").c_str());
        Check(box.Read().completed && box.Read().error=="expected error","RPC error absent");
        box.Begin(4);Check(!box.Read().completed && box.Read().error.empty(),"Previous error leaked into next request");
        // A real producer thread delivers a nontrivial JSON copy while the
        // consumer polls. A ready snapshot must always contain the full value.
        std::atomic<unsigned> requested{0},finished{0};
        std::thread producer([&]{for(unsigned i=10;i<2010;++i){
            while(requested.load(std::memory_order_acquire)!=i)std::this_thread::yield();
            box.Message(Reply(i,"ok",{{"id",i},{"data",std::string(16000,'x')}}).c_str());
            finished.store(i,std::memory_order_release);
        }});
        bool valid=true;
        for(unsigned i=10;i<2010;++i){
            box.Begin(i);requested.store(i,std::memory_order_release);
            room_sd::RpcResponse::Result r;
            do {r=box.Read();if(!r.completed)std::this_thread::yield();}while(!r.completed);
            valid &= r.value.is_object() && r.value.value("id",0u)==i && r.value.value("data",std::string()).size()==16000;
            while(finished.load(std::memory_order_acquire)!=i)std::this_thread::yield();
        }
        producer.join();Check(valid,"Ready observed before complete payload publication");
        box.Message("{\"type\":\"send\",\"payload\":{\"event\":\"native-error\"}}");
        Check(!box.Read().fault.empty(),"Native fault missing");
        box.Begin(9999);Check(!box.Read().fault.empty(),"Persistent native fault cleared by retry");
        std::cout<<"PASS RPC envelope validation, duplicate/stale filtering, null layout, errors, 2000 concurrent full-payload replies, persistent fault\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
