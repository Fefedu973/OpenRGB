/*---------------------------------------------------------*\
| RGBController_Govee.h                                     |
|                                                           |
|   RGBController for Govee wireless lighting devices       |
|                                                           |
|   Adam Honse (calcprogrammer1@gmail.com)      01 Dec 2023 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include "RGBController.h"
#include "GoveeController.h"
#include "GoveeDirectControl.h"
#include <condition_variable>

class RGBController_Govee : public RGBController
{
public:
    RGBController_Govee(GoveeController* controller_ptr);
    ~RGBController_Govee();

    void        SetupZones();
    void        DeviceConfigureZone(int zone_idx);

    void        DeviceUpdateLEDs();
    void        DeviceUpdateZoneLEDs(int zone);
    void        DeviceUpdateSingleLED(int led);

    void        DeviceUpdateMode();

    void        KeepaliveThread();

private:
    void        UpdateStatic(bool force);
    void        UpdateLEDsLocked();

    GoveeController*                                    controller;
    std::thread*                                        keepalive_thread;
    std::atomic<bool>                                   keepalive_thread_run;
    std::chrono::time_point<std::chrono::steady_clock>  last_update_time;
    RGBColor                                            last_static_color;
    unsigned int                                        last_static_brightness;
    bool                                                razer_supported;
    bool                                                static_initialized;
    GoveeDirectControl                                  direct_control;
    std::mutex                                          send_mutex;
    std::atomic<bool>                                   updates_started{false};
    std::mutex                                          keepalive_wait_mutex;
    std::condition_variable                             keepalive_wake;
};
