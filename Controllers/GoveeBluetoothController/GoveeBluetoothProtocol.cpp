/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "GoveeBluetoothProtocol.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace GoveeBluetooth
{
bool HasRequiredWriteProperty(Profile profile, bool write, bool write_without_response)
{
    // H6159 FW1.07.02 advertises only WriteWithoutResponse yet its validated
    // command/status path requires ATT WriteWithResponse. This is a profile
    // exception, not permission to use arbitrary non-writable characteristics.
    return profile == Profile::H6159 ? (write || write_without_response) : write_without_response;
}

Packet MakePacket(uint8_t prefix, uint8_t command, std::initializer_list<uint8_t> payload)
{
    if(payload.size() > 17) throw std::invalid_argument("Govee BLE payload exceeds 17 bytes");
    Packet packet{};
    packet[0] = prefix;
    packet[1] = command;
    std::copy(payload.begin(), payload.end(), packet.begin() + 2);
    for(unsigned int i = 0; i < 19; ++i) packet[19] ^= packet[i];
    return packet;
}

bool ValidPacket(const Packet& packet)
{
    uint8_t sum = 0;
    for(uint8_t byte : packet) sum ^= byte;
    return sum == 0;
}

Packet RestoreMode(const Packet& reply)
{
    if(!ValidPacket(reply) || reply[0] != 0xAA || reply[1] != 5)
        throw std::invalid_argument("Invalid Govee BLE mode snapshot");
    Packet packet = reply;
    packet[0] = 0x33;
    packet[19] ^= 0xAA ^ 0x33;
    return packet;
}

bool RestorableMode(Profile profile, const Packet& reply)
{
    if(!ValidPacket(reply) || reply[0] != 0xAA || reply[1] != 5) return false;
    if(profile == Profile::H6008) return reply[2] == 13; // Preserve white Kelvin and every payload field.
    return reply[2] == 2 && reply[6] <= 1 &&
           std::all_of(reply.begin() + 10, reply.begin() + 19, [](uint8_t b) { return b == 0; });
}

bool MatchesColor(Profile profile, const Packet& reply, RGB rgb)
{
    return RestorableMode(profile, reply) &&
           std::equal(rgb.begin(), rgb.end(), reply.begin() + 3) && reply[6] == 0 &&
           (profile != Profile::H6008 || reply[7] == 0);
}

Packet Color(Profile profile, RGB rgb)
{
    return MakePacket(0x33, 5, {static_cast<uint8_t>(profile == Profile::H6008 ? 13 : 2), rgb[0], rgb[1], rgb[2]});
}
Packet StartRealtime() { return MakePacket(0x33, 5, {5, 1, 0, 0, 0}); }
Packet Realtime(RGB rgb) { return MakePacket(0x33, 5, {5, 0, rgb[0], rgb[1], rgb[2]}); }
RGB ScaledRGB(const Frame& frame)
{
    RGB result{};
    for(unsigned int i = 0; i < 3; ++i)
        result[i] = static_cast<uint8_t>((static_cast<unsigned int>(frame.rgb[i]) * frame.brightness + 50) / 100);
    return result;
}
uint8_t BrightnessRaw(uint8_t percent)
{
    if(percent > 100) throw std::invalid_argument("Govee BLE brightness exceeds 100");
    return static_cast<uint8_t>((static_cast<unsigned int>(percent) * 255 + 50) / 100);
}

static uint8_t Nibble(char value)
{
    if(value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
    if(value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
    if(value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
    throw std::invalid_argument("Invalid hexadecimal Govee BLE configuration");
}
uint64_t ParseAddress(const std::string& address)
{
    if(address.size() != 17) throw std::invalid_argument("Govee BLE address must have six colon-separated octets");
    uint64_t result = 0;
    for(unsigned int i = 0; i < 6; ++i)
    {
        if(i && address[i * 3 - 1] != ':') throw std::invalid_argument("Invalid Govee BLE address separator");
        result = (result << 8) | (Nibble(address[i * 3]) << 4) | Nibble(address[i * 3 + 1]);
    }
    if(result == 0 || result == 0xFFFFFFFFFFFFULL) throw std::invalid_argument("Invalid Govee BLE address");
    return result;
}
Key ParseKey(const std::string& text)
{
    std::string hex;
    for(char c : text) if(!std::isspace(static_cast<unsigned char>(c))) hex.push_back(c);
    if(hex.size() != 32) throw std::invalid_argument("Govee BLE key file must contain exactly 16 hexadecimal bytes");
    Key key{};
    for(unsigned int i = 0; i < 16; ++i) key[i] = static_cast<uint8_t>((Nibble(hex[i * 2]) << 4) | Nibble(hex[i * 2 + 1]));
    return key;
}
}
