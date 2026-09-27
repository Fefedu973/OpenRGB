/* SPDX-License-Identifier: GPL-2.0-or-later */
// Explicit opt-in diagnostic. Only IRoot/GetName/GetInfo, never lighting control.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <hidapi.h>
#include "LogitechReceiverIdentity.h"
#include <array>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

static bool HasOwnerProcess()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE) return true;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool owner = false;
    if(Process32FirstW(snapshot, &entry))
    {
        do
        {
            if(!_wcsicmp(entry.szExeFile, L"SignalRgb.exe")
               || !_wcsicmp(entry.szExeFile, L"OpenRGB.exe")
               || !_wcsicmp(entry.szExeFile, L"lghub.exe")
               || !_wcsicmp(entry.szExeFile, L"lghub_agent.exe"))
            {
                owner = true;
                break;
            }
        } while(Process32NextW(snapshot, &entry));
    }
    else owner = true;
    CloseHandle(snapshot);
    return owner;
}

static bool Query(hid_device* device, unsigned char feature, unsigned char function,
                  unsigned char p0, unsigned char p1, std::array<unsigned char, 64>& response,
                  unsigned char slot = 1)
{
    // This diagnostic only emits read-only functions selected below.
    unsigned char request[20] = {0x11, slot, feature, function, p0, p1};
    const int written = hid_write(device, request, sizeof(request));
    if(written != sizeof(request))
    {
        std::printf("write_failed bytes=%d\n", written);
        if(hid_error(device)) std::wcerr << hid_error(device) << L"\n";
        return false;
    }
    for(unsigned int i = 0; i < 8; ++i)
    {
        int count = hid_read_timeout(device, response.data(), response.size(), 100);
        if(count <= 0)
        {
            if(count < 0 && hid_error(device)) std::wcerr << hid_error(device) << L"\n";
            return false;
        }
        std::printf("reply_size=%d header=%02X:%02X:%02X:%02X\n", count,
                    response[0], response[1], response[2], response[3]);
        if(count >= 7 && response[1] == slot && response[2] == 0xFF
           && response[3] == feature && response[4] == function) return false;
        const int minimum_response_size = feature == 0 ? 7 : 20;
        if(count >= minimum_response_size && (response[0] == 0x11 || response[0] == 0x10) && response[1] == slot
           && response[2] == feature && response[3] == function) return true;
    }
    return false;
}

int main(int argc, char** argv)
{
    const bool slot_scan = argc == 2 && std::string(argv[1]) == "--root-slot-scan";
    if(argc != 2 || (std::string(argv[1]) != "--probe-read-only" && !slot_scan))
    {
        std::cerr << "Explicit --probe-read-only required; stop RGB owners first.\n";
        return 2;
    }
    if(HasOwnerProcess())
    {
        std::cerr << "Refused: SignalRGB/OpenRGB/G HUB present, or process list unavailable.\n";
        return 3;
    }
    if(hid_init()) return 4;
    hid_device_info* devices = hid_enumerate(0x046D, 0xC547);
    unsigned int matched = 0;
    unsigned int successful = 0;
    for(hid_device_info* info = devices; info; info = info->next)
    {
        if(info->interface_number != 2 || info->usage_page != 0xFF00 || info->usage != 2)
            continue;
        ++matched;
        hid_device* device = hid_open_path(info->path);
        if(!device) { std::cerr << "Could not open matching receiver " << matched << "\n"; continue; }
        if(slot_scan)
        {
            std::array<unsigned char, 64> reply{};
            for(unsigned int slot : {1u, 2u, 3u, 4u, 5u, 6u, 255u})
            {
                const bool found = Query(device, 0, 0x0E, 0, 5, reply, static_cast<unsigned char>(slot));
                std::printf("slot=%u root_0005=%s feature=%02X\n", slot, found ? "reply" : "none", found ? reply[4] : 0);
                if(found) ++successful;
            }
            hid_close(device);
            continue;
        }
        std::string name;
        bool named = LogitechProbeReceiverName(
            [device](const unsigned char* d, size_t n) { return hid_write(device, d, n); },
            [device](unsigned char* d, size_t n, int t) { return hid_read_timeout(device, d, n, t); }, name);
        std::cout << "receiver=" << matched << " vid=046D pid=C547 interface=2 usage_page=FF00 usage=2"
                  << " name=" << (named ? name : "<no correlated reply>") << "\n";
        std::array<unsigned char, 64> response{};
        for(unsigned int feature_id : {0x0001u, 0x0003u, 0x0005u, 0x4522u, 0x8070u, 0x8071u, 0x8080u, 0x8081u})
        {
            if(!Query(device, 0, 0x0E, static_cast<unsigned char>(feature_id >> 8),
                      static_cast<unsigned char>(feature_id), response))
            {
                std::printf("feature=%04X query=failed\n", feature_id);
                continue;
            }
            const unsigned char index = response[4];
            std::printf("feature=%04X index=%02X flags=%02X version=%u\n",
                        feature_id, index, response[5], response[6]);
            if(feature_id == 0x8081 && index)
            {
                std::vector<unsigned int> zones;
                for(unsigned char page = 0; page < 3; ++page)
                {
                    if(!Query(device, index, 0x0E, 0, page, response))
                    {
                        std::printf("zone_page=%u query=failed\n", page);
                        continue;
                    }
                    for(unsigned int bit = page == 0 ? 1 : 0; bit < 112; ++bit)
                    {
                        const unsigned int id = page * 112u + bit;
                        if(id <= 255 && (response[6 + bit / 8] & (1u << (bit % 8)))) zones.push_back(id);
                    }
                }
                std::cout << "per_key_zones=";
                for(unsigned int zone : zones) std::cout << zone << ',';
                std::cout << " count=" << zones.size() << "\n";
            }
        }
        if(named) ++successful;
        hid_close(device);
    }
    hid_free_enumeration(devices);
    hid_exit();
    std::cout << "matched=" << matched << " named=" << successful << "\n";
    return successful == matched && matched != 0 ? 0 : 1;
}
