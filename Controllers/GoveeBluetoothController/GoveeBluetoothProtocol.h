/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace GoveeBluetooth
{
using Packet = std::array<uint8_t, 20>;
using RGB = std::array<uint8_t, 3>;
using Key = std::array<uint8_t, 16>;
enum class Profile { H6008, H6159 };

struct Frame
{
    RGB rgb = {{0, 0, 0}};
    uint8_t brightness = 100;
    bool operator==(const Frame& other) const { return rgb == other.rgb && brightness == other.brightness; }
};

Packet MakePacket(uint8_t prefix, uint8_t command, std::initializer_list<uint8_t> payload = {});
Packet RestoreMode(const Packet& reply);
bool ValidPacket(const Packet& packet);
bool RestorableMode(Profile profile, const Packet& reply);
bool MatchesColor(Profile profile, const Packet& reply, RGB rgb);
Packet Color(Profile profile, RGB rgb);
Packet StartRealtime();
Packet Realtime(RGB rgb);
RGB ScaledRGB(const Frame& frame);
uint8_t BrightnessRaw(uint8_t percent);
uint64_t ParseAddress(const std::string& address);
Key ParseKey(const std::string& text);

/* State machine and tests share this narrow interface. No Windows objects or
 * asynchronous operation may be accessed from an OpenRGB rendering thread. */
class Transport
{
public:
    virtual ~Transport() = default;
    virtual bool Connected() const = 0;
    virtual void Connect() = 0;
    virtual void Disconnect() noexcept = 0;
    virtual void Send(const Packet& packet) = 0;
    virtual Packet Query(uint8_t command) = 0;
};
}
