/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DetectionManager.h"
#include "LogManager.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "RGBController_StreamDeckBackground.h"

DetectedControllers DetectStreamDeckBackground()
{
    DetectedControllers found;
    try
    {
        streamdeck_background::Options options;
        const auto settings = ResourceManager::get()->GetSettingsManager()->GetSettings("StreamDeckBackground");
        if(streamdeck_background::ParseOptions(settings, options))
            found.push_back(new RGBController_StreamDeckBackground(options));
    }
    catch(const std::exception&)
    {
        LOG_WARNING("[Stream Deck Background] Invalid manual configuration; controller disabled");
    }
    return found;
}

REGISTER_DETECTOR("Stream Deck Background Bridge", DetectStreamDeckBackground);
