/*---------------------------------------------------------*\
| GoveeControllerDetect.cpp                                 |
|                                                           |
|   Detector for Govee wireless lighting devices            |
|                                                           |
|   Adam Honse (calcprogrammer1@gmail.com)      01 Dec 2023 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "DetectionManager.h"
#include "GoveeController.h"
#include "RGBController.h"
#include "RGBController_Govee.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "GoveeDiscovery.h"
#include "LogManager.h"

DetectedControllers DetectGoveeControllers()
{
    DetectedControllers detected_controllers;
    json                govee_settings;

    /*-----------------------------------------------------*\
    | Get Govee settings from settings manager              |
    \*-----------------------------------------------------*/
    govee_settings = ResourceManager::get()->GetSettingsManager()->GetSettings("GoveeDevices");

    /*-----------------------------------------------------*\
    | If the Govee settings contains devices, process       |
    \*-----------------------------------------------------*/
    if(govee_settings.contains("devices") && govee_settings["devices"].is_array())
    {
        GoveeController::ReceiveThreadRun = false;

        if(govee_settings["devices"].size() > 0)
        {
            /*---------------------------------------------*\
            | Open a UDP client sending to and receiving    |
            | from the Govee Multicast IP, send port 4001   |
            | and receive port 4002                         |
            \*---------------------------------------------*/
            if(!GoveeController::OpenDiscoverySocket())
            {
                LOG_WARNING("[Govee] Cannot open discovery socket on UDP 4002; discovery skipped");
                return detected_controllers;
            }

            /*---------------------------------------------*\
            | Start a thread to handle responses received   |
            | from the Govee device                         |
            \*---------------------------------------------*/
            GoveeController::ReceiveThreadRun = true;
            GoveeController::ReceiveThread = new std::thread(&GoveeController::ReceiveBroadcastThreadFunction);
        }

        for(unsigned int device_idx = 0; device_idx < govee_settings["devices"].size(); device_idx++)
        {
            const auto& entry = govee_settings["devices"][device_idx];
            if(entry.is_object() && entry.contains("ip") && entry["ip"].is_string())
            {
                const std::string govee_ip = entry["ip"];
                if(!GoveeDiscovery::ValidIPv4(govee_ip)) continue;
                std::string govee_mac;
                if(entry.contains("mac"))
                {
                    if(!entry["mac"].is_string()) continue;
                    govee_mac = GoveeDiscovery::NormalizeMac(entry["mac"].get<std::string>());
                    if(govee_mac.empty()) continue;
                }

                GoveeController*     controller     = new GoveeController(govee_ip, govee_mac);
                if(!controller->IsDiscovered())
                {
                    LOG_WARNING("[Govee] Configured device %u did not answer discovery; no phantom controller created", device_idx);
                    delete controller;
                    continue;
                }
                RGBController_Govee* rgb_controller = new RGBController_Govee(controller);

                detected_controllers.push_back(rgb_controller);
            }
        }

        /*-------------------------------------------------*\
        | All controllers have been created, the broadcast  |
        | receiver thread is no longer needed and can be    |
        | shut down                                         |
        \*-------------------------------------------------*/
        if(GoveeController::ReceiveThreadRun)
        {
            GoveeController::ReceiveThreadRun = false;
            GoveeController::ReceiveThread->join();
            delete GoveeController::ReceiveThread;
            GoveeController::ReceiveThread = nullptr;
            GoveeController::broadcast_port.tcp_close();
            GoveeController::broadcast_port.sock = INVALID_SOCKET;
        }
    }

    return(detected_controllers);
}

REGISTER_DETECTOR("Govee", DetectGoveeControllers);
