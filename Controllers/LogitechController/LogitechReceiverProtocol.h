/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace LogitechReceiverProtocol
{
using Report = std::array<uint8_t, 64>;
struct Pairing { uint8_t slot; uint16_t wpid; bool online; bool online_known; };

inline bool Connection(const uint8_t* data, int size, Pairing& pair)
{
    if(size < 7 || (data[0] != 0x10 && data[0] != 0x11) ||
       (data[0] == 0x11 && size < 20) || data[1] < 1 || data[1] > 6 || data[2] != 0x41)
        return false;
    const uint16_t wpid = static_cast<uint16_t>((data[6] << 8) | data[5]);
    if(wpid == 0 || wpid == 0xFFFF) return false;
    pair = {data[1], wpid, (data[4] & 0x40) == 0, true};
    return true;
}

inline void Add(std::vector<Pairing>& pairs, const Pairing& pair)
{
    for(auto& existing : pairs)
    {
        if(existing.slot == pair.slot)
        {
            if(existing.online_known && !pair.online_known && existing.wpid == pair.wpid) return;
            existing = pair;
            return;
        }
    }
    pairs.push_back(pair);
}

/* RAP requests are short reports on usage1. GET_LONG_REGISTER responses are
 * long reports on usage2 on Windows; its errors are still short on usage1.
 * The caller maps usages to the same fd where the platform exposes both.
 * Every reply is correlated; no timeout/ACK can become a paired device. */
template<class Write, class Read>
bool Register(Write write, Read read, uint8_t operation, uint8_t reg,
              const std::array<uint8_t, 3>& payload, int subregister,
              Report& response, std::vector<Pairing>* notifications = nullptr)
{
    if(operation != 0x80 && operation != 0x81 && operation != 0x83) return false;
    const uint8_t request[7] = {0x10, 0xFF, operation, reg, payload[0], payload[1], payload[2]};
    if(write(request, sizeof(request)) != static_cast<int>(sizeof(request))) return false;
    const uint8_t expected_id = operation == 0x83 ? 0x11 : 0x10;
    const int expected_size = operation == 0x83 ? 20 : 7;
    auto inspect = [&](const Report& candidate, int size) -> int
    {
        Pairing pair{};
        if(notifications && Connection(candidate.data(), size, pair)) Add(*notifications, pair);
        if(size >= 7 && candidate[0] == 0x10 && candidate[1] == 0xFF &&
           candidate[2] == 0x8F && candidate[3] == operation && candidate[4] == reg)
            return -1;
        if(size >= expected_size && candidate[0] == expected_id && candidate[1] == 0xFF &&
           candidate[2] == operation && candidate[3] == reg &&
           (subregister < 0 || candidate[4] == subregister))
        {
            response = candidate;
            return 1;
        }
        return 0;
    };
    for(unsigned attempt = 0; attempt < 8; ++attempt)
    {
        Report candidate{};
        int result = inspect(candidate, read(operation == 0x83 ? 2 : 1, candidate.data(), candidate.size(), 50));
        if(result) return result > 0;
        if(operation == 0x83)
        {
            candidate = {};
            result = inspect(candidate, read(1, candidate.data(), candidate.size(), 0));
            if(result) return result > 0;
        }
    }
    return false;
}

template<class Write, class Read>
std::vector<Pairing> Discover(Write write, Read read)
{
    std::vector<Pairing> pairs;
    Report response{};
    if(Register(write, read, 0x81, 0, {}, -1, response, &pairs) && !(response[5] & 1))
    {
        // Preserve all other notification flags instead of overwriting them.
        Register(write, read, 0x80, 0, {response[4], static_cast<uint8_t>(response[5] | 1), response[6]}, -1, response, &pairs);
    }
    if(!Register(write, read, 0x81, 2, {}, -1, response, &pairs)) return pairs;
    const unsigned count = response[5];
    if(count == 0 || count > 6) return pairs;
    Register(write, read, 0x80, 2, {2, 0, 0}, -1, response, &pairs);
    for(unsigned attempt = 0; attempt < 24 && pairs.size() < count; ++attempt)
    {
        Report event{}; Pairing pair{};
        const int size = read(1, event.data(), event.size(), 25);
        if(Connection(event.data(), size, pair)) Add(pairs, pair);
    }
    // Missing connection events are not evidence that a paired slot is empty.
    // Read stored pairing info (RAP B5/20+n-1) as Solaar does, also while asleep.
    for(uint8_t slot = 1; slot <= 6 && pairs.size() < count; ++slot)
    {
        if(std::any_of(pairs.begin(), pairs.end(), [slot](const Pairing& p) { return p.slot == slot; })) continue;
        const uint8_t sub = static_cast<uint8_t>(0x20 + slot - 1);
        if(Register(write, read, 0x83, 0xB5, {sub, 0, 0}, sub, response, &pairs))
        {
            const uint16_t wpid = static_cast<uint16_t>((response[7] << 8) | response[8]);
            if(wpid != 0 && wpid != 0xFFFF) Add(pairs, {slot, wpid, false, false});
        }
    }
    return pairs;
}
}
