// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Optional secondary interface; the PluginAPI 5 controller vtable is unchanged.
// Geometry metadata only. No captured keys, raw handles or controller pointers.
namespace room_input
{
constexpr std::size_t MaxInputPoints=16384;
struct InputPoint
{
    std::string location,serial,name,keyname;
    unsigned global_led=0;
    double u=0,v=0; // normalized scene coordinates, top-left origin
    std::uint64_t generation=0; // mapping generation; do not mix different snapshots
};

class RGBControllerInputMappingInterface
{
public:
    virtual ~RGBControllerInputMappingInterface()=default;
    // Copy cached value-only keyboard geometry. No device I/O. False means
    // unavailable/disabled, not an instruction to guess geometry from LED names.
    // Caller replaces its previous snapshot rather than appending across calls.
    virtual bool GetInputPoints(unsigned zone,std::vector<InputPoint>& points) const=0;
};
}
