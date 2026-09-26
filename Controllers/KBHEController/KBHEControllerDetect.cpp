/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DetectionManager.h"
#include "KBHEController.h"
#include "RGBController_KBHE.h"
#include "StringUtils.h"
#include "LogManager.h"

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
        result.push_back(new RGBController_KBHE(controller));
    }
    else
    {
        LOG_WARNING("[KBHE] Device skipped: %s", controller->GetLastError().c_str());
        delete controller;
    }
    return result;
}

REGISTER_HID_DETECTOR_IPU("KBHE 75HE RAW HID", DetectKBHEControllers, 0x9172, 0x0002, 1, 0xFF00, 0x0001);
