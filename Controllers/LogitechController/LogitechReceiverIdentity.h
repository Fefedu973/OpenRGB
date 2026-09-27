/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

/* Windows exposes each HID collection as a separate path. Keep the physical
 * instance, remove only the collection number in its two locations. */
inline std::string LogitechDevicePathKey(const char* path)
{
    std::string key = path ? path : "";
    std::string folded = key;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t collection = folded.find("&col");
    if(collection == std::string::npos)
    {
        return key; // Linux/macOS paths are case-sensitive and already identify a node.
    }
    size_t end = collection + 4;
    while(end < folded.size() && std::isxdigit(static_cast<unsigned char>(folded[end])))
    {
        ++end;
    }
    if(end == collection + 4)
    {
        return key;
    }
    key = folded;
    key.erase(collection, end - collection);
    const size_t guid = key.rfind('#');
    const size_t last = guid == std::string::npos ? std::string::npos : key.rfind('&', guid);
    if(last != std::string::npos)
    {
        key.erase(last, guid - last);
    }
    return key;
}

inline bool LogitechSameReceiverCollection(const char* a, const char* b,
                                           int interface_a, int interface_b,
                                           uint16_t page, uint16_t usage)
{
    const std::string key = LogitechDevicePathKey(a);
    return !key.empty() && interface_a == interface_b && page == 0xFF00
        && (usage == 1 || usage == 2 || usage == 4)
        && key == LogitechDevicePathKey(b);
}

/* A usage-2 Windows handle accepts long reports, including requests whose
 * payload would fit a short report. Discover 0x0005 rather than assuming its
 * feature index. All reads are correlated and bounded; this changes no state.
 * The callbacks are the only I/O, allowing protocol tests without a HID handle. */
template<class Write, class Read>
bool LogitechProbeReceiverName(Write write, Read read, std::string& name)
{
    name.clear();
    auto request = [&](uint8_t feature, uint8_t function, uint8_t p0, uint8_t p1,
                       unsigned char* response) -> int
    {
        const unsigned char message[20] = {0x11, 0x01, feature, function, p0, p1};
        if(write(message, sizeof(message)) != static_cast<int>(sizeof(message)))
        {
            return 0;
        }
        for(unsigned int attempt = 0; attempt < 8; ++attempt)
        {
            const int size = read(response, 64, 100);
            if(size <= 0)
            {
                return 0;
            }
            if(size >= 6 && response[1] == 1 && response[2] == 0xFF
               && response[3] == feature && response[4] == function)
            {
                return 0; // Matching HID++ 2.0 error, not a name.
            }
            if(size >= 5 && response[0] == 0x11 && response[1] == 1
               && response[2] == feature && response[3] == function)
            {
                return size;
            }
        }
        return 0;
    };

    unsigned char response[64] = {};
    if(!request(0, 0x0E, 0x00, 0x05, response) || response[4] == 0)
    {
        return false;
    }
    const uint8_t feature = response[4];
    if(!request(feature, 0x0E, 0, 0, response))
    {
        return false;
    }
    const unsigned int length = response[4];
    if(length == 0 || length > 128)
    {
        return false;
    }
    std::string result;
    for(unsigned int offset = 0; offset < length; offset += 16)
    {
        const unsigned int count = std::min(16u, length - offset);
        const int size = request(feature, 0x1E, static_cast<uint8_t>(offset), 0, response);
        if(size < static_cast<int>(4 + count))
        {
            return false;
        }
        for(unsigned int i = 0; i < count; ++i)
        {
            const unsigned char c = response[4 + i];
            if(c < 0x20 || c > 0x7E)
            {
                return false;
            }
            result.push_back(static_cast<char>(c));
        }
    }
    name = result;
    return true;
}
