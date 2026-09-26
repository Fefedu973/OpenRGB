/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Optional secondary interface. RGBControllerInterface's vtable and the ordinary
// LED SDK do not change. An in-process plugin can dynamic_cast a controller to
// this interface without knowing its model, driver, transport or zone count.
namespace room_image
{
constexpr size_t MaxFrameBytes = 64u * 1024u * 1024u;

struct Frame
{
    uint32_t width = 0, height = 0, stride = 0;
    uint64_t sequence = 0;
    // Top-left, BGRA8, opaque sRGB. Producers must relinquish mutable aliases.
    // All sinks can share one immutable allocation; Submit never borrows pixels.
    std::shared_ptr<const std::vector<uint8_t>> pixels;

    bool Valid() const
    {
        return pixels && width && height && width <= 16384 && height <= 16384
            && uint64_t(width)*4 <= stride
            && uint64_t(stride)*height <= MaxFrameBytes
            && uint64_t(stride)*height <= pixels->size();
    }
};

// Affine mapping: normalized coordinates of any output zone to the scene image.
// Handles crops, rotation and mirroring, independent of resolution and hardware.
// Samples outside the scene are black, not clamped to an unrelated edge.
struct Mapping
{
    double origin_x = 0, origin_y = 0;
    double u_x = 1, u_y = 0, v_x = 0, v_y = 1;
    double brightness = 1;
    bool Valid() const
    {
        return std::isfinite(origin_x) && std::isfinite(origin_y) &&
            std::isfinite(u_x) && std::isfinite(u_y) &&
            std::isfinite(v_x) && std::isfinite(v_y) &&
            std::isfinite(brightness) && brightness >= 0 && brightness <= 1;
    }
    bool Point(double u, double v, double& x, double& y) const
    {
        x = origin_x + u*u_x + v*v_x;
        y = origin_y + u*u_y + v*v_y;
        return std::isfinite(x) && std::isfinite(y);
    }
    static Mapping Rectangle(double x, double y, double w, double h,
                             double degrees = 0, bool flip_x = false, bool flip_y = false)
    {
        const double radians = degrees * 0.017453292519943295;
        const double c = std::cos(radians), s = std::sin(radians);
        Mapping m;
        m.u_x = w*c*(flip_x ? -1 : 1); m.u_y = w*s*(flip_x ? -1 : 1);
        m.v_x = -h*s*(flip_y ? -1 : 1); m.v_y = h*c*(flip_y ? -1 : 1);
        m.origin_x = x+w/2-(m.u_x+m.v_x)/2;
        m.origin_y = y+h/2-(m.u_y+m.v_y)/2;
        return m;
    }
};

struct Output
{
    unsigned zone = 0;
    // Preferred native size, informational. Input need not have these dimensions.
    uint32_t width = 0, height = 0;
    unsigned max_fps = 0;
};

enum class SubmitResult { Accepted, Busy, Unsupported, Invalid };

class RGBControllerImageInterface
{
public:
    virtual ~RGBControllerImageInterface() = default;
    // False means use the ordinary per-LED route for this zone. Zone indexes are
    // the same indexes used by RGBControllerInterface, not a fixed device list.
    virtual bool GetImageOutput(unsigned zone, Output& output) const = 0;
    // Nonblocking, latest-only. Busy/Invalid must NOT trigger a LED fallback.
    // Frame ownership can be retained. Mapping is copied by the sink. A native
    // image lease suppresses redundant LED writes from the same effect engine;
    // after expiry the sink may resume ordinary LED updates. 100..5000 ms.
    virtual SubmitResult SubmitImage(unsigned zone, std::shared_ptr<const Frame> frame,
                                     const Mapping& mapping, unsigned lease_ms = 1000) = 0;
    // Optional current image for a bounded GUI preview. Returns an immutable
    // snapshot and mapping without I/O; false when unavailable/expired.
    virtual bool GetImagePreview(unsigned, std::shared_ptr<const Frame>&, Mapping&) const { return false; }
};

// General bilinear sampler for output adapters. Returns BGRA with opaque alpha;
// the caller validates frame and mapping once before iterating output pixels.
inline uint32_t SampleBGRA(const Frame& frame, const Mapping& mapping, double u, double v)
{
    double x, y;
    if(!mapping.Point(u,v,x,y) || x<0 || y<0 || x>1 || y>1) return 0xff000000u;
    const double px = std::clamp(x*frame.width-0.5, 0.0, double(frame.width-1));
    const double py = std::clamp(y*frame.height-0.5, 0.0, double(frame.height-1));
    const auto x0 = uint32_t(px), y0 = uint32_t(py);
    const auto x1 = (std::min)(x0+1,frame.width-1), y1 = (std::min)(y0+1,frame.height-1);
    const auto* data = frame.pixels->data();
    const auto a = data + size_t(y0)*frame.stride+size_t(x0)*4;
    const auto b = data + size_t(y0)*frame.stride+size_t(x1)*4;
    const auto c = data + size_t(y1)*frame.stride+size_t(x0)*4;
    const auto d = data + size_t(y1)*frame.stride+size_t(x1)*4;
    const double dx = px-x0, dy = py-y0;
    uint32_t color = 0xff000000u;
    for(unsigned channel=0; channel<3; ++channel)
    {
        const double top = a[channel]*(1-dx)+b[channel]*dx;
        const double bottom = c[channel]*(1-dx)+d[channel]*dx;
        color |= uint32_t(std::clamp(std::lround((top*(1-dy)+bottom*dy)*mapping.brightness),0l,255l)) << (8*channel);
    }
    return color;
}
}
