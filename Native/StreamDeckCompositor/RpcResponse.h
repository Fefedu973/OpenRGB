/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <mutex>
#include <string>

namespace room_sd {
// GLib may deliver callbacks independently of the request owner's polling.
// Publish readiness, payload and errors under one lock; never expose a ready
// flag while the result is still being copied or while another request resets it.
class RpcResponse {
public:
    struct Result {
        bool completed=false;
        nlohmann::json value;
        std::string error, fault;
    };
    void Begin(std::uint64_t id)
    {
        std::lock_guard<std::mutex> lock(mutex);
        waiting_id=id;result.completed=false;result.value=nullptr;result.error.clear();
    }
    Result Read() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return result;
    }
    void Message(const char* text) noexcept
    {
        try {
            const auto envelope=nlohmann::json::parse(text,nullptr,false);
            if(!envelope.is_object())return;
            const auto type=envelope.find("type");
            if(type==envelope.end() || !type->is_string())return;
            if(*type=="error") { Fault("Guarded compositor script failed");return; }
            if(*type!="send" || !envelope.contains("payload"))return;
            const auto& payload=envelope["payload"];
            if(payload.is_array() && payload.size()>=4 && payload[0]=="frida:rpc" && payload[1].is_number_unsigned())
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(payload[1].get<std::uint64_t>()!=waiting_id || result.completed)return;
                if(payload[2]=="ok")result.value=payload[3];
                else result.error=payload[3].is_string()?payload[3].get<std::string>():"RPC rejected";
                result.completed=true;
            }
            else if(payload.is_object())
            {
                const auto event=payload.find("event");
                if(event!=payload.end() && event->is_string() && *event=="native-error")
                    Fault("Native compositor reported a fault");
            }
        } catch(...) { Fault("Native compositor RPC envelope parsing failed"); }
    }
private:
    void Fault(const char* text)
    {
        std::lock_guard<std::mutex> lock(mutex);
        result.fault=text;
    }
    mutable std::mutex mutex;
    std::uint64_t waiting_id=0;
    Result result;
};
}
