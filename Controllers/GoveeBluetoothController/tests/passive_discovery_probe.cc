/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit native passive-advertisement probe. No device object, GATT session,
 * authentication, subscription, color write or power change is requested.
 * Reuses the production watcher and forced-cold-cache resolver path.
 */
#include "../GoveeBluetoothController_Windows.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <nlohmann/json.hpp>

using namespace GoveeBluetooth;
using json = nlohmann::json;

int main(int argc, char** argv)
{
    if(argc != 3 || std::string(argv[1]) != "--passive-scan")
    {
        std::cerr << "Usage: passive_discovery_probe --passive-scan ABS_PRIVATE_DEVICE_CONFIG\n"
                     "Requires operator coordination; performs a bounded real passive scan, never GATT.\n";
        return 2;
    }
    try
    {
        struct Target { unsigned index; std::string profile; uint64_t address; };
        std::vector<Target> targets;
        std::set<uint64_t> unique;
        try
        {
            const auto path = std::filesystem::u8path(argv[2]);
            if(!path.is_absolute() || !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 65536)
                throw std::runtime_error("Input bounds");
            std::ifstream file(path, std::ios::binary);
            if(!file) throw std::runtime_error("Input missing");
            const auto data = json::parse(file);
            unsigned index = 0;
            for(const auto& item : data.at("devices"))
            {
                const unsigned current = index++;
                if(!item.value("enabled", true)) continue;
                const auto profile = item.at("profile").get<std::string>();
                if(profile != "h6008-realtime-v1" && profile != "h6159-classic-v1")
                    throw std::runtime_error("Unsupported profile");
                const auto address = ParseAddress(item.contains("address") ? item.at("address").get<std::string>() : item.at("ble_address").get<std::string>());
                if(!unique.insert(address).second || targets.size() >= 16) throw std::runtime_error("Duplicate or excessive targets");
                targets.push_back({current, profile, address});
            }
            if(targets.empty()) throw std::runtime_error("No enabled targets");
        }
        catch(...) { throw std::runtime_error("Private device configuration rejected (values redacted)"); }

        struct Apartment
        {
            Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;
        PassiveDiscovery discovery;
        for(const auto& target : targets) discovery.Register(target.address);
        unsigned starts = 0;
        const auto started = Clock::now();
        bool success = true;
        json result{{"variant", "forced-cache-miss-marker-only"}, {"passive", true},
                    {"deviceObjectsOpened", 0}, {"gattOperations", 0}, {"lightingWrites", 0},
                    {"targets", json::array()}};
        for(const auto& target : targets)
        {
            unsigned opens = 0;
            json row{{"configIndex", target.index}, {"profile", target.profile}};
            try
            {
                const auto marker = OpenConfiguredDevice(discovery, target.address,
                    [&](std::optional<uint8_t> type)
                    {
                        ++opens;
                        // This deliberately does NOT call FromBluetoothAddressAsync.
                        // It tests observation and strict typed-retry dispatch only.
                        return type ? unsigned(*type) + 1u : 0u;
                    }, [&](PassiveDiscovery::Observation observe)
                    {
                        ++starts;
                        return std::make_unique<WindowsPassiveScan>(std::move(observe));
                    }, [] { return false; });
                row["observed"] = true;
                row["addressType"] = marker == 1 ? "public" : "random";
                row["factoryCalls"] = opens;
            }
            catch(const std::exception& error)
            {
                row["observed"] = false;
                row["error"] = error.what();
                success = false;
            }
            result["targets"].push_back(std::move(row));
        }
        result["watchersStarted"] = starts;
        result["elapsedMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count();
        result["ok"] = success;
        result["limitation"] = "Validates production passive watcher and typed-retry dispatch; not GATT availability, authentication or a real reboot.";
        std::cout << result.dump(2) << '\n';
        return success ? 0 : 1;
    }
    catch(const std::exception& error)
    {
        std::cout << json{{"ok", false}, {"error", error.what()}, {"gattOperations", 0}, {"lightingWrites", 0}}.dump() << '\n';
        return 1;
    }
}
