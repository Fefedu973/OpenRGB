// SPDX-License-Identifier: GPL-2.0-or-later
#include "RGBController.h"
#include "GoveeVersionCompatibility.h"
#include "JsonUtils.h"
#include "filesystem.h"
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>

static unsigned checks = 0;
static void Check(bool ok, const std::string& label)
{
    ++checks;
    if(!ok) throw std::runtime_error(label);
}

// No application ResourceManager is linked. The production SaveConfiguration
// method receives only these inert controllers and a test-local directory.
class ResourceManager
{
public:
    static ResourceManager* get() { static ResourceManager manager; return &manager; }
    bool IsLocalClient() const { return false; }
    std::vector<RGBController*> GetRGBControllers() const { return controllers; }
    std::vector<RGBController*> controllers;
};

class ProfileManager
{
public:
    bool LoadControllerFromListWithOptions(std::vector<RGBController*>&, RGBController*, bool, bool);
    bool SaveConfiguration();
    std::vector<RGBController*> GetControllerListFromSavedConfiguration() { return {}; }
    filesystem::path configuration_directory = filesystem::current_path();
    std::mutex configuration_save_mutex;
    std::vector<RGBController*> manually_configured_rgb_controllers;
    bool active_base_color_enabled = false;
    RGBColor active_base_color = 0;
};
#define OPENRGB_PROFILE_VERSION 7
#include "profile-methods.inc"

static std::string Version(const std::string& software = "1.01.17")
{
    return "BLE Hardware Version: 3.01.01\r\nBLE Software Version: 3.00.60\r\n"
           "WiFi Hardware Version: 1.04.01\r\nWiFI Software Version: " + software + "\r\n";
}

class SyntheticController : public RGBController
{
public:
    using RGBController::name;
    using RGBController::description;
    using RGBController::vendor;
    using RGBController::version;
    using RGBController::serial;
    using RGBController::location;
    using RGBController::type;
    using RGBController::flags;
    using RGBController::zones;
    using RGBController::modes;
    unsigned device_configures = 0, zone_configures = 0;
    SyntheticController(bool saved = false)
    {
        name = "Govee H61E1"; vendor = "Govee"; description = "Govee Device";
        serial = "0123456789ABCDEF"; location = "IP: 192.0.2.1";
        version = Version(saved ? "1.01.17" : "1.06.24"); type = DEVICE_TYPE_LEDSTRIP;
        configuration = "{\"configuration\":{},\"zones\":[{\"configuration\":{}}]}";
        zone strip;
        strip.name = "Govee Strip"; strip.type = ZONE_TYPE_LINEAR;
        strip.leds_min = strip.leds_max = strip.leds_count = 30;
        strip.flags = ZONE_FLAG_MANUALLY_CONFIGURABLE_NAME | ZONE_FLAG_MANUALLY_CONFIGURABLE_TYPE
                    | ZONE_FLAG_MANUALLY_CONFIGURABLE_MATRIX_MAP | ZONE_FLAG_MANUALLY_CONFIGURABLE_SEGMENTS;
        if(saved)
        {
            strip.flags |= ZONE_FLAG_MANUALLY_CONFIGURED_SEGMENTS;
            unsigned start = 0;
            for(unsigned count : {4u, 11u, 15u})
            {
                segment part; part.name = "Segment " + std::to_string(start);
                part.type = ZONE_TYPE_LINEAR; part.start_idx = start; part.leds_count = count;
                strip.segments.push_back(part); start += count;
            }
        }
        zones.push_back(strip); leds.resize(30); SetupColors();
    }
    ~SyntheticController() override { Shutdown(); }
    void DeviceConfigureDevice() override { ++device_configures; }
    void DeviceConfigureZone(int) override { ++zone_configures; }
};

static nlohmann::json SavedJson(ProfileManager& manager, SyntheticController& old, SyntheticController& current)
{
    manager.manually_configured_rgb_controllers = {&old};
    ResourceManager::get()->controllers = {&current};
    Check(manager.SaveConfiguration(), "production SaveConfiguration succeeds");
    std::ifstream input(manager.configuration_directory / "Configuration.json");
    nlohmann::json result; input >> result;
    return result;
}

int main()
{
    try
    {
        ProfileManager manager;
        SyntheticController old(true), current;
        std::vector<RGBController*> profiles = {&old};
        Check(current.zones[0].segments.empty(), "fresh discovery has no segments");
        Check(manager.LoadControllerFromListWithOptions(profiles, &current, true, false),
              "observed WiFi software update restores controller");
        Check(current.zones[0].segments.size() == 3 && current.zones[0].segments[0].leds_count == 4
              && current.zones[0].segments[1].leds_count == 11 && current.zones[0].segments[2].leds_count == 15
              && current.zones[0].segments[1].start_idx == 4 && current.zones[0].segments[2].start_idx == 15,
              "real ConfigureZone restores all three segment offsets and lengths");
        Check(current.device_configures == 1 && current.zone_configures == 1, "configuration applied once");
        Check(RGBController::CompareControllers(&old, &current), "save matcher recognizes firmware-updated device");
        const auto saved = SavedJson(manager, old, current);
        Check(saved["controllers"].size() == 1, "save does not duplicate old firmware entry");
        Check(saved["controllers"][0]["version"] == Version("1.06.24"), "save records actual new firmware");
        Check(saved["controllers"][0]["zones"][0]["segments"].size() == 3, "save preserves restored segments");
        Check(old.version == Version() && old.zones[0].segments.size() == 3, "source profile remains unchanged");

        using Mutation = std::function<void(SyntheticController&, SyntheticController&)>;
        const std::vector<std::pair<std::string, Mutation>> rejected = {
            {"other detected vendor", [](auto&, auto& n){ n.vendor = "Other"; }},
            {"other saved vendor", [](auto& o, auto&){ o.vendor = "Other"; }},
            {"both other vendor", [](auto& o, auto& n){ o.vendor = n.vendor = "Other"; }},
            {"missing serial", [](auto& o, auto& n){ o.serial = n.serial = ""; }},
            {"zero serial", [](auto& o, auto& n){ o.serial = n.serial = "0000000000000000"; }},
            {"different serial", [](auto&, auto& n){ n.serial = "FEDCBA9876543210"; }},
            {"invalid serial length", [](auto& o, auto& n){ o.serial = n.serial = "1234"; }},
            {"nonhex serial", [](auto& o, auto& n){ o.serial = n.serial = "G123456789ABCDEF"; }},
            {"changed name", [](auto&, auto& n){ n.name += " v2"; }},
            {"changed description", [](auto&, auto& n){ n.description += " v2"; }},
            {"changed controller type", [](auto&, auto& n){ n.type = DEVICE_TYPE_LIGHT; }},
            {"changed location", [](auto&, auto& n){ n.location = "IP: 192.0.2.2"; }},
            {"BLE hardware changed", [](auto&, auto& n){ n.version.replace(n.version.find("3.01.01"), 7, "3.01.02"); }},
            {"WiFi hardware changed", [](auto&, auto& n){ n.version.replace(n.version.find("1.04.01"), 7, "2.04.01"); }},
            {"unknown version field", [](auto&, auto& n){ n.version += "Other Version: 1.00.00\r\n"; }},
            {"duplicate version field", [](auto&, auto& n){ n.version += "BLE Hardware Version: 3.01.01\r\n"; }},
            {"missing version field", [](auto&, auto& n){ n.version.erase(0, n.version.find('\n') + 1); }},
            {"malformed version value", [](auto&, auto& n){ n.version.replace(n.version.find("1.06.24"), 7, "unknown"); }},
            {"empty version value", [](auto&, auto& n){ n.version.erase(n.version.find("1.06.24"), 7); }},
            {"unrecognized old version", [](auto& o, auto&){ o.version = "legacy"; }},
            {"zone count changed", [](auto&, auto& n){ n.zones.push_back(n.zones[0]); }},
            {"zone minimum changed", [](auto&, auto& n){ n.zones[0].leds_min = 20; }},
            {"zone maximum changed", [](auto&, auto& n){ n.zones[0].leds_max = 60; }},
            {"zone geometry flag cannot bypass limits", [](auto&, auto& n){ n.zones[0].leds_max = 60; n.zones[0].flags |= ZONE_FLAG_ZONE_GEOMETRY_MAY_CHANGE; }},
            {"unconfigurable zone renamed", [](auto&, auto& n){ n.zones[0].flags = 0; n.zones[0].name = "Different"; }},
            {"unconfigurable zone type changed", [](auto&, auto& n){ n.zones[0].flags = 0; n.zones[0].type = ZONE_TYPE_MATRIX; }},
        };
        for(const auto& test : rejected)
        {
            SyntheticController previous(true), next;
            test.second(previous, next);
            std::vector<RGBController*> list = {&previous};
            Check(!manager.LoadControllerFromListWithOptions(list, &next, true, false), test.first + ": restore rejects");
            Check(next.device_configures == 0 && next.zone_configures == 0 && next.zones[0].segments.empty(),
                  test.first + ": rejection precedes all configuration mutations");
            Check(!RGBController::CompareControllers(&previous, &next), test.first + ": save does not discard old entry");
        }
        SyntheticController changed_hardware;
        changed_hardware.version.replace(changed_hardware.version.find("3.01.01"), 7, "4.01.01");
        const auto retained = SavedJson(manager, old, changed_hardware);
        Check(retained["controllers"].size() == 1 && retained["controllers"][0]["version"] == old.version
              && retained["controllers"][0]["zones"][0]["segments"].size() == 3,
              "incompatible detected hardware retains offline saved configuration and segments");

        SyntheticController custom_old(true), custom_new;
        custom_old.zones[0].flags |= ZONE_FLAG_MANUALLY_CONFIGURED_TYPE | ZONE_FLAG_MANUALLY_CONFIGURED_MATRIX_MAP;
        custom_old.zones[0].type = ZONE_TYPE_MATRIX;
        custom_old.zones[0].matrix_map.Set(2, 15, nullptr);
        for(unsigned i = 0; i < 30; ++i) custom_old.zones[0].matrix_map.map[i] = 29 - i;
        std::vector<RGBController*> custom = {&custom_old};
        Check(manager.LoadControllerFromListWithOptions(custom, &custom_new, true, false), "configured matrix accepted over detected linear default");
        Check(custom_new.zones[0].type == ZONE_TYPE_MATRIX && custom_new.zones[0].matrix_map.map[0] == 29
              && custom_new.zones[0].segments.size() == 3, "configured matrix and segments both restored");
        Check(SavedJson(manager, custom_old, custom_new)["controllers"].size() == 1, "custom geometry has one saved identity");

        SyntheticController other_old(true), other_new;
        other_old.vendor = other_new.vendor = "Other"; other_new.version = other_old.version;
        other_old.serial = other_new.serial = "";
        std::vector<RGBController*> same = {&other_old};
        Check(manager.LoadControllerFromListWithOptions(same, &other_new, true, false), "exact-version legacy matching unchanged");
        SyntheticController short_old(true), short_new;
        short_old.serial = short_new.serial = "012345ABCDEF";
        short_new.version.replace(short_new.version.find("3.00.60"), 7, "3.00.61");
        std::vector<RGBController*> short_list = {&short_old};
        Check(manager.LoadControllerFromListWithOptions(short_list, &short_new, true, false), "12-digit identity and BLE software-only update accepted");
        std::cout << "PASS " << checks << " assertions: production firmware matching, restoration and save\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
