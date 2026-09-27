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
        // B1 enters external-control mode and may interrupt an active stream.
        // Ordinary B0 frames (including idle keepalives) already maintain it.
        // Only reacquire after a real gap in frame output, never on a timer
        // measured from the last B1. Resuming does not restart the ON window.
        const bool resumed = sent && now - last_frame >= std::chrono::seconds(30);
        result.enable = !sent || result.power_on || resumed;
        brightness = std::min(brightness, 100u);
        result.brightness = !sent || result.power_on || resumed || brightness != last_brightness;
        if(result.brightness) last_brightness = brightness;
        last_frame = now;
        sent = true;
        return result;
    }

private:
    bool active = false, sent = false;
    unsigned int attempts = 0, last_brightness = 0;
    Clock::time_point started{}, next_power{}, last_frame{};
};
