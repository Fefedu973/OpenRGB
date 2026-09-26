/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "RGBControllerImageInterface.h"
class RGBControllerInterface;

namespace room_image
{
// Optional secondary plugin API: query with dynamic_cast on the existing API
// pointer. Old API5 plugins keep their exact vtable. New plugins must check this
// capability, not infer it from a version number or cast an object_ptr blindly.
class PluginAPI
{
public:
    virtual ~PluginAPI() = default;
    virtual unsigned ImageAPIVersion() const = 0;
    // The controller must be a virtual controller created by the plugin API.
    // The image sink is borrowed and must outlive its attachment. Detach nullptr
    // before unloading/destroying the plugin object; detach waits for active
    // image calls before returning. Passing nullptr does not remove the LEDs.
    // Image callbacks must not reenter attach/detach or UpdateDeviceList, and
    // routing graphs must reject cycles before calling another image sink.
    virtual bool AttachImageInterface(RGBControllerInterface* controller,
                                      RGBControllerImageInterface* sink) = 0;
};
}
