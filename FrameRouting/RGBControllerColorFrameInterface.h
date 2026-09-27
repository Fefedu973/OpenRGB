// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Optional secondary interface: no change to the PluginAPI 5 controller vtable.
// A producer groups every segment it owns on a controller into ONE frame. A new
// accepted frame replaces that controller's pending frame; it is not a patch
// merged with an earlier frame from this or another producer.
namespace room_color
{
constexpr size_t MaxUpdates = 1024u * 1024u; // 8 MiB of index/color pairs.
struct ColorUpdate { uint32_t index = 0, color = 0; };
struct ColorFrame
{
    uint64_t topology = 0;
    std::vector<ColorUpdate> values;
};
enum class SubmitResult { Accepted, Busy, Unsupported, Invalid, Stale };

class RGBControllerColorFrameInterface
{
public:
    virtual ~RGBControllerColorFrameInterface() = default;
    // Capture when building/rebinding the route, not on every render. Any
    // resize invalidates queued frames even if their indexes remain in range.
    virtual uint64_t GetColorTopology() const = 0;
    // Nonblocking, no device I/O or callbacks. The producer relinquishes every
    // mutable alias. At most one pending allocation is retained per controller.
    // Indexed partial frames leave unlisted LEDs unchanged; duplicate indexes
    // are applied in order. All indexes are validated before any color changes.
    // Accepted acknowledges the mailbox, not hardware delivery. The worker may
    // discard an invalid/expired frame. Lease is 100..5000 milliseconds.
    // Busy/Stale/Invalid must not fall back to synchronous per-LED writes.
    virtual SubmitResult SubmitColorFrame(std::shared_ptr<const ColorFrame> frame,
                                         unsigned lease_ms = 1000) = 0;
};
}
