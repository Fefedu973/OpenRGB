/* SPDX-License-Identifier: GPL-2.0-or-later */
// API5 test double adapted from the Visual Map image-routing harness.
/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "OpenRGBPluginInterface.h"
#include "RGBController_Virtual.h"
#include <FrameRouting/OpenRGBImagePluginAPI.h>
class FakeAPI : public OpenRGBPluginAPIInterface, public room_image::PluginAPI
{
public:
void LogEntry(const char* filename, int line, unsigned int level, const char* fmt, ...) override {  }
void UnregisterVirtualRGBControllerInThread(RGBControllerInterface* rgb_controller) override {  }
void ClearActiveProfile() override {  }
std::vector<std::string> GetProfileList() override { return {}; }
bool LoadProfile(std::string profile_name) override { return {}; }
bool SaveProfileFromPlugin(std::string profile_name, std::string plugin_name, nlohmann::json plugin_data) override { return {}; }
filesystem::path GetConfigurationDirectory() override { return {}; }
bool GetDetectionEnabled() override { return {}; }
unsigned int GetDetectionPercent() override { return {}; }
std::string GetDetectionString() override { return {}; }
void RescanDevices() override {  }
void WaitForDetection() override {  }
nlohmann::json GetDeviceDescriptionJSON(RGBControllerInterface* controller) override { return {}; }
nlohmann::json GetLEDDescriptionJSON(led led) override { return {}; }
nlohmann::json GetMatrixMapDescriptionJSON(matrix_map_type matrix_map) override { return {}; }
nlohmann::json GetModeDescriptionJSON(mode mode) override { return {}; }
nlohmann::json GetSegmentDescriptionJSON(segment segment) override { return {}; }
nlohmann::json GetZoneDescriptionJSON(zone zone) override { return {}; }
RGBControllerInterface* SetDeviceDescriptionJSON(nlohmann::json controller_json) override { return {}; }
led SetLEDDescriptionJSON(nlohmann::json led_json) override { return {}; }
matrix_map_type SetMatrixMapDescriptionJSON(nlohmann::json matrix_map_json) override { return {}; }
mode SetModeDescriptionJSON(nlohmann::json mode_json) override { return {}; }
segment SetSegmentDescriptionJSON(nlohmann::json segment_json) override { return {}; }
zone SetZoneDescriptionJSON(nlohmann::json zone_json) override { return {}; }
bool CompareControllers(RGBControllerInterface* controller_1, RGBControllerInterface* controller_2) override { return {}; }
std::string DeviceTypeToString(device_type type) override { return {}; }
bool SetModeValuesFromMode(mode& destination, mode& source) override { return {}; }
nlohmann::json GetSettings(std::string settings_key) override { return {}; }
void SaveSettings() override {  }
void SetSettings(std::string settings_key, nlohmann::json new_settings) override {  }

 std::vector<RGBControllerInterface*> physical, created;
 unsigned attachments=0, detachments=0, removed=0;
 unsigned image_version=1;
 ~FakeAPI(){ for(auto* p:created) delete dynamic_cast<RGBController_Virtual*>(p); }
 unsigned ImageAPIVersion() const override {return image_version;}
 bool AttachImageInterface(RGBControllerInterface* c, room_image::RGBControllerImageInterface* sink) override {
   auto* v=dynamic_cast<RGBController_Virtual*>(c); if(!v) return false;
   v->AttachImageInterface(sink); if(sink) ++attachments; else ++detachments; return true;
 }
 RGBControllerInterface* CreateVirtualRGBController(RGBController_Setup* s) override {
   auto* v=new RGBController_Virtual(s); created.push_back(v); return v;
 }
 void DeleteVirtualRGBController(RGBControllerInterface* c) override {
   created.erase(std::remove(created.begin(),created.end(),c),created.end());
   ++removed; delete dynamic_cast<RGBController_Virtual*>(c);
 }
 void UpdateVirtualRGBController(RGBControllerInterface* c, RGBController_Setup* s) override { dynamic_cast<RGBController_Virtual*>(c)->UpdateVirtualController(s); }
 void RegisterVirtualRGBController(RGBControllerInterface*) override {}
 void RegisterVirtualRGBControllerInThread(RGBControllerInterface*) override {}
 void UnregisterVirtualRGBController(RGBControllerInterface*) override {}
 std::vector<RGBControllerInterface*> GetRGBControllers() override {return physical;}
};
