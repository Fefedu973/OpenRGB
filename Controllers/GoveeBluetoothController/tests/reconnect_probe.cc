/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit, read-only LIGHTING-STATE probe of the production Windows transport.
 * Authentication/CCCD writes and AA queries are necessary; no 0x33 command is
 * sent. This is not an offline test and must only run with other BLE owners off.
 */
#include "../GoveeBluetoothController_Windows.cpp"
#include <tlhelp32.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cwctype>
using json = nlohmann::json;
using namespace GoveeBluetooth;

static std::string ReadPrivate(const std::filesystem::path& path, size_t maximum)
{
    if(!path.is_absolute() || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > maximum)
        throw std::runtime_error("Private input rejected");
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Private input unavailable");
    return std::string(std::istreambuf_iterator<char>(input), {});
}

static void CheckOwners()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot verify RGB process ownership");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool conflict = false;
    if(!Process32FirstW(snapshot, &entry)) { CloseHandle(snapshot); throw std::runtime_error("Cannot enumerate RGB owners"); }
    do
    {
        std::wstring name(entry.szExeFile);
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
        if(name == L"openrgb.exe" || name == L"signalrgb.exe") conflict = true;
    } while(Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if(conflict) throw std::runtime_error("OpenRGB or SignalRGB is running; no BLE access attempted");
}

struct Owner
{
    HANDLE handle = nullptr;
    explicit Owner(uint64_t address)
    {
        std::ostringstream name; name << "Local\\OpenRGB-Govee-BLE-" << std::hex << address;
        handle = CreateMutexA(nullptr, FALSE, name.str().c_str());
        const DWORD result = handle ? WaitForSingleObject(handle, 0) : WAIT_FAILED;
        if(result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        {
            if(handle) CloseHandle(handle);
            handle = nullptr;
            throw std::runtime_error("Configured device already has a native BLE owner");
        }
    }
    ~Owner() { if(handle) { ReleaseMutex(handle); CloseHandle(handle); } }
};

static json Metadata(const TransportDiagnostics& d)
{
    return {{"connectionStatus", d.connection_status}, {"sessionStatus", d.session_status},
        {"authenticated", d.authenticated}, {"cccdSubscribeStatus", d.subscribe_status},
        {"cccdSubscribeMs", d.subscribe_ms}, {"cccdUnsubscribeStatus", d.unsubscribe_status},
        {"cccdUnsubscribeMs", d.unsubscribe_ms}, {"disconnectConnectionStatus", d.disconnect_connection_status},
        {"disconnectSessionStatus", d.disconnect_session_status}, {"notificationEvents", d.notification_events},
        {"wrongLengthEvents", d.wrong_length}, {"callbackErrors", d.callback_errors}};
}

static json RunCycle(WindowsTransport& transport, unsigned cycle)
{
    json row{{"cycle", cycle + 1}};
    const auto start = Clock::now();
    try
    {
        transport.Connect();
        row["connectMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        row["afterConnect"] = Metadata(transport.Diagnostics());
        const auto cccd = transport.ReadNotificationConfiguration();
        row["cccdReadStatus"] = cccd.first;
        row["cccdReadValue"] = cccd.second;
        for(uint8_t command : {1, 4, 5}) transport.Query(command);
        row["stateQueries"] = 3;
        row["beforeDisconnect"] = Metadata(transport.Diagnostics());
        row["ok"] = true;
    }
    catch(const winrt::hresult_error& e)
    {
        row["windowsError"] = static_cast<uint32_t>(e.code().value);
        row["ok"] = false;
    }
    catch(const std::exception& e)
    {
        row["error"] = e.what(); // Production errors contain no key/address/payload.
        row["ok"] = false;
    }
    const auto close_start = Clock::now();
    transport.Disconnect();
    row["disconnectMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - close_start).count();
    row["afterDisconnect"] = Metadata(transport.Diagnostics());
    return row;
}

int main(int argc, char** argv)
{
    try
    {
        if(argc < 4 || argc > 7 || std::string(argv[1]) != "--read-only")
        {
            std::cerr << "Usage: reconnect_probe --read-only ABS_CONFIG INDEX [CYCLES=2] [GAP_MS=0] [current|full-services|auth-write-response|new-object|new-mta|clear-factory-cache]\n"
                         "Stop all BLE owners first. This only authenticates, subscribes and reads state.\n";
            return 2;
        }
        const unsigned index = std::stoul(argv[3]);
        const unsigned cycles = argc >= 5 ? std::stoul(argv[4]) : 2;
        const unsigned gap_ms = argc >= 6 ? std::stoul(argv[5]) : 0;
        const std::string variant = argc >= 7 ? argv[6] : "current";
        if(variant != "current" && variant != "full-services" && variant != "auth-write-response" &&
           variant != "new-object" && variant != "new-mta" && variant != "clear-factory-cache")
            throw std::runtime_error("Unknown probe variant");
        if(cycles < 1 || cycles > 3 || gap_ms > 10000) throw std::runtime_error("Probe bounds exceeded");
        CheckOwners();
        Configuration config;
        struct KeyCleanup { Configuration& config; ~KeyCleanup() { SecureZeroMemory(config.key.data(), config.key.size()); } } key_cleanup{config};
        try
        {
            const auto data = json::parse(ReadPrivate(std::filesystem::u8path(argv[2]), 65536));
            const auto& item = data.at("devices").at(index);
            if(!item.value("enabled", true) || item.at("profile") != "h6008-realtime-v1") throw std::runtime_error("Profile");
            config.address = ParseAddress(item.at("address").get<std::string>());
            config.wifi_mac = ParseAddress(item.at("wifi_mac").get<std::string>());
            config.key = ParseKey(ReadPrivate(std::filesystem::u8path(item.value("key_file", data.value("key_file", std::string()))), 256));
        }
        catch(...) { throw std::runtime_error("H6008 private configuration rejected (values redacted)"); }
        Owner owner(config.address);
        struct Apartment
        {
            bool initialized;
            explicit Apartment(bool enable = true) : initialized(enable)
            { if(enable) winrt::init_apartment(winrt::apartment_type::multi_threaded); }
            ~Apartment() { if(initialized) winrt::uninit_apartment(); }
        } apartment(variant != "new-mta" && variant != "clear-factory-cache");
        std::atomic<bool> stop{false};
        TransportProbeOptions options;
        options.full_services = variant == "full-services";
        options.auth_write_response = variant == "auth-write-response";
        std::unique_ptr<WindowsTransport> transport;
        if(variant != "new-object" && variant != "new-mta" && variant != "clear-factory-cache")
            transport = std::make_unique<WindowsTransport>(config, stop, options);
        json result{{"model", "H6008"}, {"variant", variant}, {"lightingWrites", 0}, {"gapMs", gap_ms}, {"cycles", json::array()}};
        bool all_ok = true;
        for(unsigned cycle = 0; cycle < cycles; ++cycle)
        {
            if(cycle && gap_ms) std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms));
            json row;
            if(variant == "new-mta" || variant == "clear-factory-cache")
            {
                // No main-thread WinRT apartment: after join, this cycle's
                // transport, all its projected objects and MTA are gone.
                std::exception_ptr failure;
                std::thread thread([&]
                {
                    try
                    {
                        Apartment cycle_apartment;
                        {
                            WindowsTransport cycle_transport(config, stop, options);
                            row = RunCycle(cycle_transport, cycle);
                        }
                        if(variant == "clear-factory-cache")
                        {
                            // Standalone comparison only, after every projected
                            // transport object is destroyed and before MTA exit.
                            winrt::clear_factory_cache();
                            row["factoryCacheCleared"] = true;
                        }
                    }
                    catch(...) { failure = std::current_exception(); }
                });
                thread.join();
                if(failure) std::rethrow_exception(failure);
            }
            else if(variant == "new-object")
            {
                WindowsTransport cycle_transport(config, stop, options);
                row = RunCycle(cycle_transport, cycle);
            }
            else row = RunCycle(*transport, cycle);
            all_ok = all_ok && row["ok"].get<bool>();
            result["cycles"].push_back(row);
            std::cerr << "Read-only cycle " << cycle + 1 << ": " << (row["ok"].get<bool>() ? "PASS" : "FAIL") << '\n';
        }
        result["ok"] = all_ok;
        std::cout << result.dump(2) << '\n';
        return all_ok ? 0 : 1;
    }
    catch(const winrt::hresult_error& e) { std::cerr << "Windows error 0x" << std::hex << static_cast<uint32_t>(e.code().value) << '\n'; return 1; }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
