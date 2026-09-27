#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
enum { SETTINGSMANAGER_UPDATE_REASON_SETTINGS_UPDATED, SETTINGSMANAGER_UPDATE_REASON_SETTINGS_SCHEMA_UPDATED };
enum { PROFILEMANAGER_UPDATE_REASON_PROFILE_LIST_UPDATED };
class SettingsManager {
public:
 nlohmann::json schema,settings; unsigned saves=0;
 nlohmann::json GetSettingsSchema(std::string){return schema;}
 nlohmann::json GetSettings(std::string key){return settings[key];}
 void ModifySettings(std::string key,nlohmann::json value){settings[key].update(value,true);}
 void SaveSettings(){++saves;}
 void RegisterSettingsManagerCallback(void(*)(void*,unsigned),void*){}
 void UnregisterSettingsManagerCallback(void(*)(void*,unsigned),void*){}
};
class ProfileManager {
public:
 std::vector<std::string> profiles{"Alpha","Full Scale - Rainbow"};
 auto GetProfileList(){return profiles;}
 void RegisterProfileManagerCallback(void(*)(void*,unsigned),void*){}
 void UnregisterProfileManagerCallback(void(*)(void*,unsigned),void*){}
};
class ResourceManager {
public:
 SettingsManager settings; ProfileManager profiles;
 static ResourceManager* get(){static ResourceManager r;return &r;}
 SettingsManager* GetSettingsManager(){return &settings;}
 ProfileManager* GetProfileManager(){return &profiles;}
};
