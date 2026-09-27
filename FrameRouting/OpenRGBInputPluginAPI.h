// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace room_input
{
struct KeyboardEvent
{
    std::uint64_t sequence = 0;
    double time = 0; // steady_clock seconds, not wall time
    std::string device_path;
    std::uint16_t scan = 0; // physical Set 1; E0/E1 encoded as 0xe000/0xe100
};

// Optional secondary API. Query with dynamic_cast; API5's vtable is unchanged.
// Physical make events only: no characters, layout translation or OS injection.
class PluginAPI
{
public:
    virtual ~PluginAPI() = default;
    virtual unsigned InputAPIVersion() const = 0;
    // First listener starts the core-owned service. 0 means unavailable/conflict.
    virtual std::uint64_t AcquireKeyboardInput() = 0;
    virtual void ReleaseKeyboardInput(std::uint64_t token) = 0;
    // Consumes this listener's cursor. At most 64 events younger than 250 ms;
    // acquiring a listener never replays events that occurred before acquisition.
    virtual std::vector<KeyboardEvent> ReadKeyboardInput(std::uint64_t token) = 0;
    // Aggregate service state only; never keys, device paths or captured text.
    virtual std::string KeyboardInputStatus() const = 0;
};
}
