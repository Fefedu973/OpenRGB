/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBController.h"
#include "KBHEController.h"

class RGBController_KBHE : public RGBController
{
public:
    explicit RGBController_KBHE(KBHEController* controller);
    ~RGBController_KBHE();
    void SetupZones();
    void DeviceUpdateLEDs();
    void DeviceUpdateZoneLEDs(int zone);
    void DeviceUpdateSingleLED(int led);
    void DeviceUpdateMode();

private:
    void ReportError();
    KBHEController* controller;
    bool direct_selected = false;
    std::string reported_error;
};
