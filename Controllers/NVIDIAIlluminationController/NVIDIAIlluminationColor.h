/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cstdint>

namespace NVIDIAIlluminationColor
{
/* Monochrome hardware still needs the per-LED intensity of a direct RGB frame.
 * This matches the verified RTX 3080 Ti FE bridge: round(max(R,G,B)*pct/255).
 * Division by the odd denominator 255 cannot have an exact half-integer tie. */
inline uint8_t MonochromeBrightness(uint8_t red, uint8_t green, uint8_t blue, uint8_t percent)
{
    const unsigned int peak = std::max({red, green, blue});
    const unsigned int brightness = std::min<unsigned int>(percent, 100);
    return static_cast<uint8_t>((peak * brightness + 127) / 255);
}
}
