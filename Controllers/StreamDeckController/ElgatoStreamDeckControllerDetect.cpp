/*---------------------------------------------------------*\
| ElgatoStreamDeckControllerDetect.cpp                      |
|                                                           |
|   Detector for Elgato Stream Deck MK.2                    |
|                                                           |
|   Ferréol DUBOIS COLI (Fefe_du_973)           23 Jan 2025 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "DetectionManager.h"
#include "ElgatoStreamDeckController.h"
#include "RGBController_ElgatoStreamDeck.h"
#include "ResourceManager.h"
#include "SettingsManager.h"

#define ELGATO_VID          0x0FD9
#define STREAMDECK_MK2_PID  0x0080

DetectedControllers DetectElgatoStreamDeckControllers(hid_device_info* info, const std::string&)
{
    DetectedControllers detected_controllers;

    // Elgato owns the USB handle when its compositor supplies backgrounds.
    // Do not replace the action icons through the legacy image controller.
    const json background = ResourceManager::get()->GetSettingsManager()->GetSettings("StreamDeckBackground");
    if(background.contains("enabled") && background["enabled"].is_boolean() && background["enabled"].get<bool>())
    {
        return detected_controllers;
    }

    if(info->interface_number == 0)
    {
        hid_device* dev = hid_open_path(info->path);

        if(dev)
        {
            ElgatoStreamDeckController*     controller     = new ElgatoStreamDeckController(dev, info->path);
            RGBController_ElgatoStreamDeck* rgb_controller = new RGBController_ElgatoStreamDeck(controller);

            detected_controllers.push_back(rgb_controller);
        }
    }

    return(detected_controllers);
}

REGISTER_HID_DETECTOR("Elgato Stream Deck MK.2", DetectElgatoStreamDeckControllers, ELGATO_VID, STREAMDECK_MK2_PID);
