/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DetectionManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "LogManager.h"
#include "RGBController_GoveeBluetooth_Windows.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

static std::string ReadGoveePrivateFile(const std::filesystem::path& path, std::size_t limit)
{
    if(!path.is_absolute()) throw std::invalid_argument("Govee BLE private file path must be absolute");
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Govee BLE private file could not be opened");
    std::string result;
    char buffer[4096];
    while(input.read(buffer, sizeof(buffer)) || input.gcount())
    {
        if(result.size() + static_cast<std::size_t>(input.gcount()) > limit)
            throw std::invalid_argument("Govee BLE private file exceeds its size limit");
        result.append(buffer, static_cast<std::size_t>(input.gcount()));
    }
    return result;
}

DetectedControllers DetectGoveeBluetoothControllers()
{
    DetectedControllers controllers;
    auto* manager = ResourceManager::get()->GetSettingsManager();
    json schema;
    schema["config_file"] = {{"type", "string"}, {"default", ""},
        {"title", "Govee Bluetooth private configuration"},
        {"description", "Absolute path to the manually allowlisted BLE devices JSON; never include keys in OpenRGB settings."}};
    schema["keep_black_on_exit"] = {{"type", "bool"}, {"default", false},
        {"title", "Keep black when closing"},
        {"description", "When the final requested color is black, confirm power OFF instead of restoring the startup snapshot. Other colors retain normal restoration. Never reconnect during shutdown."}};
    manager->RegisterSettingsSchemaLocalOnly("GoveeBluetooth", "Govee Bluetooth", schema);
    const json settings = manager->GetSettings("GoveeBluetooth");
    if(!settings.is_object() || !settings.contains("config_file") || !settings["config_file"].is_string() || settings["config_file"] == "")
        return controllers;
    try
    {
        const std::filesystem::path config_path = std::filesystem::u8path(settings["config_file"].get<std::string>());
        const json config = json::parse(ReadGoveePrivateFile(config_path, 65536));
        if(!config.is_object() || !config.contains("devices") || !config["devices"].is_array() || config["devices"].size() > 16)
            throw std::invalid_argument("Govee BLE config requires at most 16 manually allowlisted devices");
        std::set<uint64_t> addresses;
        unsigned int index = 0;
        for(const auto& item : config["devices"])
        {
            ++index;
            try
            {
                if(!item.is_object()) throw std::invalid_argument("Device configuration is not an object");
                if(!item.value("enabled", true)) continue;
                GoveeBluetooth::Configuration device;
                device.keep_black_on_exit = settings.value("keep_black_on_exit", false);
                const auto profile = item.at("profile").get<std::string>();
                if(profile == "h6008-realtime-v1") device.profile = GoveeBluetooth::Profile::H6008;
                else if(profile == "h6159-classic-v1") device.profile = GoveeBluetooth::Profile::H6159;
                else throw std::invalid_argument("Unsupported Govee BLE profile");
                device.serial = item.contains("address") ? item.at("address").get<std::string>() : item.at("ble_address").get<std::string>();
                device.address = GoveeBluetooth::ParseAddress(device.serial);
                if(addresses.count(device.address)) throw std::invalid_argument("Duplicate Govee BLE address");
                if(item.contains("power_on_acquire"))
                    device.power_on_acquire = item.at("power_on_acquire").get<bool>();
                const std::string friendly = item.value("name", std::string());
                if(friendly.size() > 80 || friendly.find_first_of("\r\n\t") != std::string::npos)
                    throw std::invalid_argument("Invalid Govee BLE friendly name");
                device.name = std::string("Govee ") + (device.profile == GoveeBluetooth::Profile::H6008 ? "H6008" : "H6159") +
                    (friendly.empty() ? "" : " - " + friendly);
                if(device.profile == GoveeBluetooth::Profile::H6008)
                {
                    device.wifi_mac = GoveeBluetooth::ParseAddress(item.at("wifi_mac").get<std::string>());
                    const std::string key_path = item.contains("key_file") ? item.at("key_file").get<std::string>() : config.at("key_file").get<std::string>();
                    device.key = GoveeBluetooth::ParseKey(ReadGoveePrivateFile(std::filesystem::u8path(key_path), 256));
                }
                auto* controller = new RGBController_GoveeBluetooth(std::move(device));
                addresses.insert(GoveeBluetooth::ParseAddress(controller->GetSerial()));
                controllers.push_back(controller);
            }
            catch(const std::exception&)
            {
                // JSON exceptions can embed raw values. Never log those values,
                // communication keys, private file contents or private paths.
                LOG_WARNING("[Govee BLE] Configuration entry %u rejected (profile/address/name/key validation)", index);
            }
        }
    }
    catch(const std::exception&)
    {
        LOG_WARNING("[Govee BLE] Private configuration rejected; no unlisted devices are scanned");
    }
    return controllers;
}

REGISTER_DETECTOR("Govee Bluetooth (manual, Windows)", DetectGoveeBluetoothControllers);
