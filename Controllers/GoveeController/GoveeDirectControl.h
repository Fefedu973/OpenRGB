// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <chrono>

// The caller serializes access. No socket, clock reads or background work here.
class GoveeDirectControl
{
public:
    using Clock = std::chrono::steady_clock;
    struct Actions { bool power_on = false, enable = false, brightness = false; };

    void Begin(Clock::time_point now)
    {
        active = true;
        started = next_power = now;
        attempts = 0;
        sent = false;
    }
    void Reset() { active = false; }
    bool Active() const { return active; }

    Actions Next(Clock::time_point now, unsigned int brightness)
    {
        if(!active) Begin(now);
        Actions result;
        // UDP has no delivery guarantee. Retry acquisition at most three times,
        // within three seconds; never continually override a later manual OFF.
        if(attempts < 3 && now - started < std::chrono::seconds(3) && now >= next_power)
        {
            result.power_on = true;
            ++attempts;
            next_power = now + std::chrono::seconds(1);
        }
        result.enable = !sent || result.power_on || now - last_enable >= std::chrono::seconds(10);
        brightness = std::min(brightness, 100u);
        result.brightness = !sent || result.power_on || brightness != last_brightness ||
                            now - last_brightness_time >= std::chrono::seconds(10);
        if(result.enable) last_enable = now;
        if(result.brightness) { last_brightness = brightness; last_brightness_time = now; }
        sent = true;
        return result;
    }

private:
    bool active = false, sent = false;
    unsigned int attempts = 0, last_brightness = 0;
    Clock::time_point started{}, next_power{}, last_enable{}, last_brightness_time{};
};
