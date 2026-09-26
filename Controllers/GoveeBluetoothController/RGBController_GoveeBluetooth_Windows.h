/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBController.h"
#include "GoveeBluetoothController_Windows.h"

class RGBController_GoveeBluetooth : public RGBController
{
public:
    explicit RGBController_GoveeBluetooth(GoveeBluetooth::Configuration configuration);
    ~RGBController_GoveeBluetooth();
    void SetupZones();
    void DeviceConfigureZone(int zone_idx) override;
    void DeviceUpdateLEDs() override;
    void DeviceUpdateZoneLEDs(int zone_idx) override;
    void DeviceUpdateSingleLED(int led_idx) override;
    void DeviceUpdateMode() override;
private:
    GoveeBluetooth::Controller controller;
};
