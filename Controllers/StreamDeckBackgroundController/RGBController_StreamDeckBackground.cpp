/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "RGBController_StreamDeckBackground.h"
#include "LogManager.h"
#include <numeric>

/**------------------------------------------------------------------*\
    @name Stream Deck Background Bridge
    @category Accessory
    @type Network
    @save :x:
    @direct :white_check_mark:
    @effects :x:
    @detectors DetectStreamDeckBackground
    @comment Local compositor bridge; preserves Elgato actions and icons.
\*-------------------------------------------------------------------*/

RGBController_StreamDeckBackground::RGBController_StreamDeckBackground(const streamdeck_background::Options& options)
    : surface_input(!options.surface_channel.empty()),
      controller(options, [](const std::string& reason){ LOG_WARNING("[Stream Deck Background] %s", reason.c_str()); })
{
    name = "Stream Deck Background Canvas";
    vendor = options.transport=="native" ? "Elgato / native in-process compositor" : "Elgato / local compositor bridge";
    type = DEVICE_TYPE_ACCESSORY;
    description = surface_input ? "Native FrameSurface image input; compatibility LED updates ignored; Elgato local hook required"
                                : "80x50 background pixels; Elgato application and local hook required";
    location = options.transport=="native" ? "Native Frida Core: guarded Elgato compositor" : "Loopback: Stream Deck Background API";
    serial = "room-streamdeck-background-mk2";
    version = "1.0";
    mode direct;
    direct.name = "Direct";
    direct.value = 0;
    direct.flags = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(direct);
    SetupZones();
}

RGBController_StreamDeckBackground::~RGBController_StreamDeckBackground()
{
    Shutdown();
    controller.Stop();
}

void RGBController_StreamDeckBackground::SetupZones()
{
    leds.clear();
    zones.clear();
    std::vector<unsigned int> mapping(streamdeck_background::LED_COUNT);
    std::iota(mapping.begin(), mapping.end(), 0);
    zone canvas;
    canvas.name = "Background Canvas 80x50";
    canvas.type = ZONE_TYPE_MATRIX;
    canvas.leds_min = canvas.leds_max = canvas.leds_count = streamdeck_background::LED_COUNT;
    canvas.matrix_map.Set(streamdeck_background::HEIGHT, streamdeck_background::WIDTH, mapping.data());
    zones.push_back(canvas);
    for(unsigned int y = 0; y < streamdeck_background::HEIGHT; ++y)
        for(unsigned int x = 0; x < streamdeck_background::WIDTH; ++x)
        {
            led pixel;
            pixel.name = "Pixel " + std::to_string(x) + "," + std::to_string(y);
            leds.push_back(pixel);
        }
    SetupColors();
}

void RGBController_StreamDeckBackground::DeviceUpdateLEDs()
{
    if(surface_input) return;
    std::vector<unsigned char> rgb;
    rgb.reserve(streamdeck_background::LED_COUNT * 3);
    for(const auto color : colors)
    {
        rgb.push_back(RGBGetRValue(color));
        rgb.push_back(RGBGetGValue(color));
        rgb.push_back(RGBGetBValue(color));
    }
    controller.Submit(std::move(rgb));
}
void RGBController_StreamDeckBackground::DeviceUpdateZoneLEDs(int) { DeviceUpdateLEDs(); }
void RGBController_StreamDeckBackground::DeviceUpdateSingleLED(int) { DeviceUpdateLEDs(); }
void RGBController_StreamDeckBackground::DeviceUpdateMode() { DeviceUpdateLEDs(); }

bool RGBController_StreamDeckBackground::GetImageOutput(unsigned zone, room_image::Output& output) const
{
    return controller.GetImageOutput(zone,output);
}

room_image::SubmitResult RGBController_StreamDeckBackground::SubmitImage(unsigned zone,
    std::shared_ptr<const room_image::Frame> frame,const room_image::Mapping& mapping,unsigned lease_ms)
{
    return controller.SubmitImage(zone,std::move(frame),mapping,lease_ms);
}

bool RGBController_StreamDeckBackground::GetImagePreview(unsigned zone,std::shared_ptr<const room_image::Frame>& frame,
                                                        room_image::Mapping& mapping) const
{
    return controller.GetImagePreview(zone,frame,mapping);
}
