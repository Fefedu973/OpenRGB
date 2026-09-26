/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RGBController_VirtualScreen.h"
#include <numeric>

RGBController_VirtualScreen::RGBController_VirtualScreen(const virtual_screen::Options& value)
    : options(value),controller(value)
{
    name = options.name; vendor = "OpenRGB Room"; type = DEVICE_TYPE_VIRTUAL;
    description = "Native image surface with an independent low-resolution compatibility matrix";
    version = "1"; serial = "virtual-screen:" + options.id; location = "FrameSurface:" + options.channel;
    mode direct;
    direct.name = "Direct"; direct.value = 0; direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED; modes.push_back(direct);
    SetupZones();
}
RGBController_VirtualScreen::~RGBController_VirtualScreen() { Shutdown(); controller.Stop(); }

void RGBController_VirtualScreen::SetupZones()
{
    leds.clear(); zones.clear();
    const unsigned count = options.compatibility_width*options.compatibility_height;
    std::vector<unsigned> mapping(count); std::iota(mapping.begin(),mapping.end(),0);
    zone output;
    output.name = "Image Surface"; output.type = ZONE_TYPE_MATRIX;
    output.leds_min = output.leds_max = output.leds_count = count;
    output.matrix_map.Set(options.compatibility_height,options.compatibility_width,mapping.data()); zones.push_back(output);
    for(unsigned y = 0; y < options.compatibility_height; ++y) for(unsigned x = 0; x < options.compatibility_width; ++x)
    {
        led cell; cell.name = "Compatibility " + std::to_string(x) + "," + std::to_string(y); leds.push_back(cell);
    }
    SetupColors();
}
void RGBController_VirtualScreen::DeviceUpdateLEDs() { controller.SubmitLEDs(colors); }
void RGBController_VirtualScreen::DeviceUpdateZoneLEDs(int) { DeviceUpdateLEDs(); }
void RGBController_VirtualScreen::DeviceUpdateSingleLED(int) { DeviceUpdateLEDs(); }
void RGBController_VirtualScreen::DeviceUpdateMode() { DeviceUpdateLEDs(); }
bool RGBController_VirtualScreen::GetImageOutput(unsigned zone,room_image::Output& output) const { return controller.GetImageOutput(zone,output); }
bool RGBController_VirtualScreen::GetImagePreview(unsigned zone,std::shared_ptr<const room_image::Frame>& frame,room_image::Mapping& mapping) const
{ return controller.GetImagePreview(zone,frame,mapping); }
room_image::SubmitResult RGBController_VirtualScreen::SubmitImage(unsigned zone,std::shared_ptr<const room_image::Frame> frame,
                                                                 const room_image::Mapping& mapping,unsigned lease_ms)
{ return controller.SubmitImage(zone,std::move(frame),mapping,lease_ms); }
