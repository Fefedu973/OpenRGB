/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBController.h"
#include "VirtualScreenController.h"

class RGBController_VirtualScreen : public RGBController, public room_image::RGBControllerImageInterface
{
public:
    explicit RGBController_VirtualScreen(const virtual_screen::Options& options);
    ~RGBController_VirtualScreen();
    void SetupZones();
    void DeviceUpdateLEDs() override;
    void DeviceUpdateZoneLEDs(int) override;
    void DeviceUpdateSingleLED(int) override;
    void DeviceUpdateMode() override;
    bool GetImageOutput(unsigned zone,room_image::Output& output) const override;
    bool GetImagePreview(unsigned zone,std::shared_ptr<const room_image::Frame>& frame,room_image::Mapping& mapping) const override;
    room_image::SubmitResult SubmitImage(unsigned zone,std::shared_ptr<const room_image::Frame> frame,
                                         const room_image::Mapping& mapping,unsigned lease_ms = 1000) override;
private:
    virtual_screen::Options options;
    virtual_screen::Controller controller;
};
