/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBController.h"
#include "StreamDeckBackgroundController.h"

class RGBController_StreamDeckBackground : public RGBController, public room_image::RGBControllerImageInterface
{
public:
    explicit RGBController_StreamDeckBackground(const streamdeck_background::Options& options);
    ~RGBController_StreamDeckBackground();
    void SetupZones();
    void DeviceUpdateLEDs();
    void DeviceUpdateZoneLEDs(int zone);
    void DeviceUpdateSingleLED(int led);
    void DeviceUpdateMode();
    bool GetImageOutput(unsigned zone, room_image::Output& output) const override;
    bool GetImagePreview(unsigned zone, std::shared_ptr<const room_image::Frame>& frame,
                         room_image::Mapping& mapping) const override;
    room_image::SubmitResult SubmitImage(unsigned zone, std::shared_ptr<const room_image::Frame> frame,
                                         const room_image::Mapping& mapping, unsigned lease_ms = 1000) override;

private:
    bool surface_input;
    streamdeck_background::Controller controller;
};
