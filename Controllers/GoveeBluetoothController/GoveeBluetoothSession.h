/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GoveeBluetoothProtocol.h"

namespace GoveeBluetooth
{
/* Single-thread owned. Step takes the newest frame, not a historical FIFO.
 * Connection failures preserve the first snapshot and black-out ownership. */
class Session
{
public:
    Session(Profile profile, Transport& transport);
    Session(Profile profile, Transport& transport, bool power_on_acquire);
    void Step(const Frame& frame, uint64_t now_ms);
    void Release();
    std::string State() const { return state; }
    bool RecoveredBaseline() const { return recovered_baseline; }
private:
    void Initialize(const Frame& frame, uint64_t now_ms);
    void ObservePower(uint8_t current);
    void SetPower(bool on);
    void SendClassicColor(const Frame& frame, bool force, uint64_t now_ms);
    void SendRealtimeColor(const Frame& frame, bool force);
    Profile profile;
    Transport& transport;
    bool power_on_acquire;
    bool acquired = false;
    bool snapshot_valid = false;
    bool modified = false;
    bool recovered_baseline = false;
    bool recovery_pending = false;
    bool realtime_started = false;
    bool blackout_owned = false;
    bool acquire_on_pending = false;
    bool acquire_power_owned = false;
    bool external_off = false;
    bool sent = false;
    uint8_t power = 0;
    uint8_t original_power = 0;
    uint8_t original_brightness = 0;
    Packet original_mode{};
    Frame last_frame{};
    uint64_t last_power_ms = 0;
    uint64_t last_color_check_ms = 0;
    std::string state = "idle";
};
}
