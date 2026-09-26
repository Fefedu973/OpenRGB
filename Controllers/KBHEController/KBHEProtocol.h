/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

/* The device owns the logical-key to physical-WS2812 permutation. */
namespace KBHEProtocol
{
constexpr std::size_t LED_COUNT    = 82;
constexpr std::size_t FRAME_BYTES  = LED_COUNT * 3;
constexpr std::size_t CHUNK_BYTES  = 60;
constexpr std::size_t CHUNK_COUNT  = (FRAME_BYTES + CHUNK_BYTES - 1) / CHUNK_BYTES;
constexpr unsigned char LIVE_MODE = 7;
using Report = std::array<unsigned char, 65>;
using Reply  = std::array<unsigned char, 64>;
using Frame  = std::array<unsigned char, FRAME_BYTES>;

inline Report Command(unsigned char command, std::initializer_list<unsigned char> payload = {})
{
    Report report{};
    report[1] = command; // report[0] is HIDAPI's unnumbered report ID.
    std::copy_n(payload.begin(), std::min<std::size_t>(payload.size(), 62), report.begin() + 3);
    return report;
}

inline std::array<Report, CHUNK_COUNT> FrameReports(const Frame& frame)
{
    std::array<Report, CHUNK_COUNT> reports{};
    for(std::size_t chunk = 0; chunk < CHUNK_COUNT; ++chunk)
    {
        const std::size_t offset = chunk * CHUNK_BYTES;
        const std::size_t size   = std::min(CHUNK_BYTES, FRAME_BYTES - offset);
        reports[chunk]    = Command(0x6A);
        reports[chunk][3] = static_cast<unsigned char>(chunk);
        reports[chunk][4] = static_cast<unsigned char>(size);
        std::copy_n(frame.begin() + offset, size, reports[chunk].begin() + 5);
    }
    return reports;
}

inline bool CompatibleCapabilities(const Reply& reply)
{
    const unsigned int flags = reply[8] | (static_cast<unsigned int>(reply[9]) << 8);
    return reply[0] == 0x7F && reply[1] == 0 && reply[2] == 1 &&
           reply[4] == LED_COUNT && reply[5] == 3 && reply[6] == CHUNK_BYTES &&
           reply[7] == LIVE_MODE && (flags & 0x69) == 0x69 && reply[10] == 0;
}
}
