/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RGBController_GoveeBluetooth_Windows.h"
#include <algorithm>

RGBController_GoveeBluetooth::RGBController_GoveeBluetooth(GoveeBluetooth::Configuration configuration) :
    controller(std::move(configuration))
{
    name = controller.Name();
    vendor = "Govee";
    type = DEVICE_TYPE_LIGHT;
    description = "Native Bluetooth " + controller.Model() + " (one RGB zone)";
    location = "Bluetooth: " + controller.Serial();
    serial = controller.Serial();
    version = controller.Model() == "H6008" ? "h6008-realtime-v1" : "h6159-classic-v1";
    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    direct.color_mode = MODE_COLORS_PER_LED;
    direct.brightness_min = 0;
    direct.brightness_max = 100;
    direct.brightness = 100;
    modes.push_back(direct);
    mode stat;
    stat.name = "Static";
    stat.value = 1;
    stat.flags = MODE_FLAG_HAS_MODE_SPECIFIC_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    stat.color_mode = MODE_COLORS_MODE_SPECIFIC;
    stat.brightness_min = 0;
    stat.brightness_max = 100;
    stat.brightness = 100;
    stat.colors_min = 1;
    stat.colors_max = 1;
    stat.colors.resize(1);
    modes.push_back(stat);
    SetupZones();
}

RGBController_GoveeBluetooth::~RGBController_GoveeBluetooth()
{
    Shutdown();
    // Controller destructor stops its worker and performs a bounded restoration.
}

void RGBController_GoveeBluetooth::SetupZones()
{
    zone single;
    single.name = "Whole device";
    single.type = ZONE_TYPE_SINGLE;
    single.leds_min = single.leds_max = single.leds_count = 1;
    zones.push_back(single);
    led light;
    light.name = controller.Model() + " RGB";
    leds.push_back(light);
    SetupColors();
}

void RGBController_GoveeBluetooth::DeviceConfigureZone(int) {}
void RGBController_GoveeBluetooth::DeviceUpdateZoneLEDs(int) { DeviceUpdateLEDs(); }
void RGBController_GoveeBluetooth::DeviceUpdateSingleLED(int) { DeviceUpdateLEDs(); }
void RGBController_GoveeBluetooth::DeviceUpdateMode() { DeviceUpdateLEDs(); }

void RGBController_GoveeBluetooth::DeviceUpdateLEDs()
{
    if(colors.empty() || active_mode >= modes.size()) return;
    const mode& current = modes[active_mode];
    const RGBColor color = current.value == 1 && !current.colors.empty() ? current.colors[0] : colors[0];
    GoveeBluetooth::Frame frame;
    frame.rgb = {{RGBGetRValue(color), RGBGetGValue(color), RGBGetBValue(color)}};
    frame.brightness = static_cast<uint8_t>(std::min(current.brightness, 100u));
    controller.Submit(frame);
}
