/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RGBController_KBHE.h"
#include "KBHELayout.h"
#include "LogManager.h"

/**------------------------------------------------------------------*\
    @name KBHE 75HE
    @category Keyboard
    @type USB
    @save :x:
    @direct :white_check_mark:
    @effects :x:
    @detectors DetectKBHEControllers
    @comment 82-key ISO layout. Hardware mode restores the prior firmware effect.
\*-------------------------------------------------------------------*/
RGBController_KBHE::RGBController_KBHE(KBHEController* device_controller) : controller(device_controller)
{
    name        = "KBHE 75HE";
    vendor      = "KBHE";
    description = "KBHE 82-key keyboard (RAW HID live RGB)";
    type        = DEVICE_TYPE_KEYBOARD;
    version     = controller->GetVersion();
    serial      = controller->GetSerial();
    location    = controller->GetLocation();

    mode direct;
    direct.name       = "Direct";
    direct.value      = KBHEProtocol::LIVE_MODE;
    direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct);
    mode hardware;
    hardware.name       = "Hardware (restore previous effect)";
    hardware.value      = 0x76;
    hardware.flags      = 0;
    hardware.color_mode = MODE_COLORS_NONE;
    modes.push_back(hardware);
    SetupZones();
}

RGBController_KBHE::~RGBController_KBHE()
{
    Shutdown();
    delete controller;
}

void RGBController_KBHE::SetupZones()
{
    zone keyboard;
    keyboard.name       = "Keyboard";
    keyboard.type       = ZONE_TYPE_MATRIX;
    keyboard.leds_min   = KBHEProtocol::LED_COUNT;
    keyboard.leds_max   = KBHEProtocol::LED_COUNT;
    keyboard.leds_count = KBHEProtocol::LED_COUNT;
    keyboard.matrix_map.Set(KBHELayout::HEIGHT, KBHELayout::WIDTH, nullptr);
    std::fill(keyboard.matrix_map.map.begin(), keyboard.matrix_map.map.end(), 0xFFFFFFFF);
    for(unsigned int i = 0; i < KBHEProtocol::LED_COUNT; ++i)
    {
        const auto& key = KBHELayout::KEYS[i];
        keyboard.matrix_map.map[key.row * KBHELayout::WIDTH + key.column] = i;
        led light;
        light.name  = key.name;
        light.value = i;
        leds.push_back(light);
    }
    zones.push_back(keyboard);
    SetupColors();
}

void RGBController_KBHE::ReportError()
{
    const std::string& error = controller->GetLastError();
    if(!error.empty() && error != reported_error)
    {
        LOG_WARNING("[KBHE] %s", error.c_str());
        reported_error = error;
    }
}

void RGBController_KBHE::DeviceUpdateLEDs()
{
    if(active_mode != 0)
    {
        return;
    }
    if(!direct_selected)
    {
        direct_selected = controller->EnterDirectMode();
        if(!direct_selected)
        {
            ReportError();
            return;
        }
    }
    KBHEProtocol::Frame frame{};
    for(std::size_t i = 0; i < std::min(colors.size(), KBHEProtocol::LED_COUNT); ++i)
    {
        frame[i * 3]     = RGBGetRValue(colors[i]);
        frame[i * 3 + 1] = RGBGetGValue(colors[i]);
        frame[i * 3 + 2] = RGBGetBValue(colors[i]);
    }
    if(!controller->SendFrame(frame))
    {
        ReportError();
    }
    else
    {
        reported_error.clear();
    }
}

void RGBController_KBHE::DeviceUpdateZoneLEDs(int) { DeviceUpdateLEDs(); }
void RGBController_KBHE::DeviceUpdateSingleLED(int) { DeviceUpdateLEDs(); }

void RGBController_KBHE::DeviceUpdateMode()
{
    if(active_mode == 0)
    {
        direct_selected = controller->EnterDirectMode();
        if(direct_selected)
        {
            DeviceUpdateLEDs();
        }
        else
        {
            ReportError();
        }
    }
    else
    {
        direct_selected = false;
        if(!controller->RestoreHardware())
        {
            ReportError();
        }
    }
}
