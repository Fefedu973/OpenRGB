/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "VirtualScreenController.h"
#include "RGBController_VirtualScreen.h"
#include "DetectionManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "LogManager.h"

DetectedControllers DetectVirtualScreenControllers()
{
    DetectedControllers found;
    auto* settings = ResourceManager::get()->GetSettingsManager();
    nlohmann::json schema;
    schema["enabled"] = {{"type","bool"},{"default",false},{"title","Enable virtual image screens"}};
    schema["outputs"] = {{"type","array"},{"default",nlohmann::json::array()},
        {"title","Virtual image outputs"},{"description","Up to16 local image outputs, independent native dimensions and compatibility matrices."}};
    settings->RegisterSettingsSchemaLocalOnly("VirtualScreens","Virtual Screens",schema);
    try
    {
        for(const auto& options : virtual_screen::ParseOptions(settings->GetSettings("VirtualScreens")))
            found.push_back(new RGBController_VirtualScreen(options));
    }
    catch(const std::exception&)
    {
        for(auto* controller : found) delete controller;
        found.clear();
        LOG_WARNING("[Virtual Screens] Invalid local configuration; no image outputs registered");
    }
    return found;
}
REGISTER_DETECTOR("Virtual Screens (local image outputs)",DetectVirtualScreenControllers);
