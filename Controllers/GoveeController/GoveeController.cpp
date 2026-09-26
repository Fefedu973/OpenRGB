/*---------------------------------------------------------*\
| GoveeController.cpp                                       |
|                                                           |
|   Driver for Govee wireless lighting devices              |
|                                                           |
|   Adam Honse (calcprogrammer1@gmail.com)      01 Dec 2023 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <nlohmann/json.hpp>
#include "base64.hpp"
#include "GoveeController.h"
#include "JsonUtils.h"
#include "GoveeDiscovery.h"

using json = nlohmann::json;
using namespace std::chrono_literals;

base64::byte CalculateXorChecksum(std::vector<base64::byte> packet)
{
    base64::byte checksum = 0;

    for(unsigned int i = 0; i < packet.size(); i++)
    {
        checksum ^= packet[i];
    }

    return(checksum);
}

GoveeController::GoveeController(std::string ip, std::string mac)
{
    /*-----------------------------------------------------*\
    | Fill in location string with device's IP address      |
    \*-----------------------------------------------------*/
    ip_address  = ip;
    module_mac  = GoveeDiscovery::NormalizeMac(mac);

    /*-----------------------------------------------------*\
    | Register callback for receiving broadcasts            |
    \*-----------------------------------------------------*/
    RegisterReceiveBroadcastCallback(this);

    /*-----------------------------------------------------*\
    | Request device information                            |
    \*-----------------------------------------------------*/
    SendScan();

    /*-----------------------------------------------------*\
    | Wait up to 5s for device information to be received   |
    \*-----------------------------------------------------*/
    for(unsigned int wait_count = 0; wait_count < 500; wait_count++)
    {
        if(broadcast_received)
        {
            break;
        }

        std::this_thread::sleep_for(10ms);
    }

    /*-----------------------------------------------------*\
    | Open a UDP client sending to the Govee device IP,     |
    | port 4003                                             |
    \*-----------------------------------------------------*/
    port.udp_client(ip_address.c_str(), "4003");
}

GoveeController::~GoveeController()
{
    UnregisterReceiveBroadcastCallback(this);
}

std::string GoveeController::GetLocation()
{
    return("IP: " + ip_address);
}

std::string GoveeController::GetSku()
{
    return(sku);
}

std::string GoveeController::GetSerial()
{
    return module_mac;
}

bool GoveeController::IsDiscovered()
{
    return broadcast_received.load();
}

std::string GoveeController::GetVersion()
{
    return("BLE Hardware Version: "  + bleVersionHard  + "\r\n" +
           "BLE Software Version: "  + bleVersionSoft  + "\r\n" +
           "WiFi Hardware Version: " + wifiVersionHard + "\r\n" +
           "WiFI Software Version: " + wifiVersionSoft + "\r\n");
}

void GoveeController::ReceiveBroadcast(char* recv_buf, int size)
{
    if(broadcast_received.load() || size <= 0) return;
    const json response = json::parse(recv_buf, recv_buf + size, nullptr, false);
    if(response.is_discarded() || !response.is_object() || !response.contains("msg") || !response["msg"].is_object()) return;
    const auto& msg = response["msg"];
    if(!msg.contains("cmd") || msg["cmd"] != "scan" || !msg.contains("data")) return;
    const auto& data = msg["data"];
    if(!GoveeDiscovery::Matches(data, ip_address, module_mac)) return;
    auto field = [&data](const char* name) -> std::string {
        return data.contains(name) && data[name].is_string() ? data[name].get<std::string>() : "";
    };
    ip_address      = field("ip");
    sku             = field("sku");
    module_mac      = GoveeDiscovery::NormalizeMac(field("device"));
    bleVersionHard  = field("bleVersionHard");
    bleVersionSoft  = field("bleVersionSoft");
    wifiVersionHard = field("wifiVersionHard");
    wifiVersionSoft = field("wifiVersionSoft");
    // Release/acquire publication makes all metadata visible to the detector.
    broadcast_received.store(true);
}

void GoveeController::SetColor(unsigned char red, unsigned char green, unsigned char blue)
{
    json command;

    command["msg"]["cmd"]                       = "colorwc";
    command["msg"]["data"]["color"]["r"]        = red;
    command["msg"]["data"]["color"]["g"]        = green;
    command["msg"]["data"]["color"]["b"]        = blue;
    command["msg"]["data"]["colorTemInKelvin"]  = "0";

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SendRazerData(RGBColor* colors, unsigned int size)
{
    /*-----------------------------------------------------*\
    | Do not send an empty frame (this was producing        |
    | length=2, count=0)                                    |
    \*-----------------------------------------------------*/
    if(size == 0)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | PT payload: BB [len_hi] [len_lo] B0 [gradient_off=1]  |
    | [led_count] (RGB * N) [xor]                           |
    | length = 2 + 3*N  (bytes after 0xB0: gradient_off +   |
    | led_count + RGB*count)                                |
    \*-----------------------------------------------------*/
    const unsigned int count = std::min(size, 255u);
    const unsigned int payload_len = 2 + (3 * count);

    /*-----------------------------------------------------*\
    | Create buffer with fixed size and fill sequentially   |
    \*-----------------------------------------------------*/

    std::vector<base64::byte> pkt;
    pkt.reserve(7 + (3 * count));

    pkt.push_back(0xBB);
    pkt.push_back(static_cast<base64::byte>((payload_len >> 8) & 0xFF)); /* len_hi */
    pkt.push_back(static_cast<base64::byte>(payload_len & 0xFF));        /* len_lo */
    pkt.push_back(0xB0);                                                 /* subcommand */
    pkt.push_back(0x01);                                                 /* gradient_off = 1 */
    pkt.push_back(static_cast<base64::byte>(count));                     /* led_count */

    for(std::size_t led_idx = 0; led_idx < count; led_idx++)
    {
        pkt.push_back(RGBGetRValue(colors[led_idx]));
        pkt.push_back(RGBGetGValue(colors[led_idx]));
        pkt.push_back(RGBGetBValue(colors[led_idx]));
    }

    pkt.push_back(CalculateXorChecksum(pkt));

    json command;
    command["msg"]["cmd"]                       = "razer";
    command["msg"]["data"]["pt"]                = base64::encode(pkt);

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SendRazerDisable()
{
    const std::vector<base64::byte> pkt = { 0xBB, 0x00, 0x01, 0xB1, 0x00, 0x0B };
    json command;

    command["msg"]["cmd"]                       = "razer";
    command["msg"]["data"]["pt"]                = base64::encode(pkt);

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SendRazerEnable()
{
    const std::vector<base64::byte> pkt = { 0xBB, 0x00, 0x01, 0xB1, 0x01, 0x0A };
    json command;

    command["msg"]["cmd"]                       = "razer";
    command["msg"]["data"]["pt"]                = base64::encode(pkt);

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SetBrightness(unsigned int brightness)
{
    json command;

    command["msg"]["cmd"]                       = "brightness";
    command["msg"]["data"]["value"]             = std::min(brightness, 100u);

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SetPower(bool enabled)
{
    json command;

    command["msg"]["cmd"]                       = "turn";
    command["msg"]["data"]["value"]             = enabled ? 1 : 0;

    std::string command_str                     = command.dump();

    port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

void GoveeController::SendScan()
{
    json command;

    command["msg"]["cmd"]                       = "scan";
    /*-----------------------------------------------------*\
    | Matches what Govee devices commonly accept for LAN    |
    | scan                                                  |
    \*-----------------------------------------------------*/
    command["msg"]["data"]["account_topic"]     = "reserve";

    /*-----------------------------------------------------*\
    | Convert the JSON object to a string and write it      |
    \*-----------------------------------------------------*/
    std::string command_str                     = command.dump();

    broadcast_port.udp_write((char *)command_str.c_str(), (int)command_str.length() + 1);
}

/*---------------------------------------------------------*\
| Static class members for shared broadcast receiver        |
\*---------------------------------------------------------*/
net_port                        GoveeController::broadcast_port;
std::vector<GoveeController*>   GoveeController::callbacks;
std::mutex                     GoveeController::callbacks_mutex;
std::thread*                    GoveeController::ReceiveThread;
std::atomic<bool>               GoveeController::ReceiveThreadRun;

void GoveeController::ReceiveBroadcastThreadFunction()
{
    char recv_buf[1024];

    broadcast_port.set_receive_timeout(1, 0);

    while(ReceiveThreadRun.load())
    {
        /*-------------------------------------------------*\
        | Receive up to 1024 bytes from the device with a   |
        | 1s timeout                                        |
        \*-------------------------------------------------*/
        int size = broadcast_port.udp_listen(recv_buf, 1024);

        /*-------------------------------------------------*\
        | If data was received, loop through registered     |
        | callback controllers and call the                 |
        | ReceiveBroadcast function for the controller      |
        | matching the received data                        |
        |                                                   |
        | NOTE: As implemented, it doesn't actually match   |
        | the intended controller and just calls all        |
        | registered controllers.  As they are all called   |
        | sequence, this should work, but if parallel calls |
        | are ever needed, receives should be filtered by   |
        | IP address                                        |
        \*-------------------------------------------------*/
        if(size > 0)
        {
            std::lock_guard<std::mutex> lock(callbacks_mutex);
            for(std::size_t callback_idx = 0; callback_idx < callbacks.size(); callback_idx++)
            {
                GoveeController* controller = callbacks[callback_idx];

                controller->ReceiveBroadcast(recv_buf, size);
            }
        }
    }
}

void GoveeController::RegisterReceiveBroadcastCallback(GoveeController* controller_ptr)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex);
    callbacks.push_back(controller_ptr);
}

void GoveeController::UnregisterReceiveBroadcastCallback(GoveeController* controller_ptr)
{
    std::lock_guard<std::mutex> lock(callbacks_mutex);
    for(std::size_t callback_idx = 0; callback_idx < callbacks.size(); callback_idx++)
    {
        if(callbacks[callback_idx] == controller_ptr)
        {
            callbacks.erase(callbacks.begin() + callback_idx);
            break;
        }
    }
}
