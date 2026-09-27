/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DetectionManager.h"
#include "KBHEController.h"
#include "RGBController_KBHE.h"
#include "StringUtils.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"

DetectedControllers DetectKBHEControllers(hid_device_info* info, const std::string&)
{
    DetectedControllers result;
    /* Reject the boot/NKRO keyboard and XInput virtual gamepad even if a future
     * registration accidentally broadens the detector. */
    if(info->vendor_id != 0x9172 || info->product_id != 0x0002 ||
       info->interface_number != 1 || info->usage_page != 0xFF00 || info->usage != 1)
    {
        return result;
    }
    hid_device* handle = hid_open_path(info->path);
    if(!handle)
    {
        return result;
    }
    const bool legacy_75he = info->product_string &&
        std::wstring(info->product_string).find(L"75HE") != std::wstring::npos;
    const std::string serial = info->serial_number ? StringUtils::wstring_to_string(info->serial_number) : "";
    KBHEController* controller = new KBHEController(handle, info->path, serial, legacy_75he);
    if(controller->Probe())
    {
        auto* settings_manager = ResourceManager::get()->GetSettingsManager();
        json schema;
        schema["keep_black_on_exit"] = {{"type", "bool"}, {"default", false},
            {"title", "Keep black when closing"},
            {"description", "When all final Direct-mode keys are black, confirm the black live frame instead of restoring the previous hardware effect. Other colors keep normal restoration."}};
        settings_manager->RegisterSettingsSchemaLocalOnly("KBHE", "KBHE", schema);
        const json settings = settings_manager->GetSettings("KBHE");
        result.push_back(new RGBController_KBHE(controller,
            settings.is_object() && settings.value("keep_black_on_exit", false)));
    }
    else
    {
        LOG_WARNING("[KBHE] Device skipped: %s", controller->GetLastError().c_str());
        delete controller;
    }
    return result;
}

REGISTER_HID_DETECTOR_IPU("KBHE 75HE RAW HID", DetectKBHEControllers, 0x9172, 0x0002, 1, 0xFF00, 0x0001);
