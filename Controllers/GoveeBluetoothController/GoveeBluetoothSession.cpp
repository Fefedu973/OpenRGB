/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "GoveeBluetoothSession.h"
#include <stdexcept>

namespace GoveeBluetooth
{
Session::Session(Profile profile_, Transport& transport_) :
    Session(profile_, transport_, profile_ == Profile::H6008) {}

Session::Session(Profile profile_, Transport& transport_, bool power_on_acquire_) :
    profile(profile_), transport(transport_), power_on_acquire(power_on_acquire_) {}

void Session::ObservePower(uint8_t current)
{
    if(current > 1) throw std::runtime_error("Unknown Govee BLE power state");
    if(acquired && power == 1 && current == 0 && !blackout_owned) external_off = true;
    if(current == 1) external_off = false;
    power = current;
}

void Session::SetPower(bool on)
{
    modified = true;
    transport.Send(MakePacket(0x33, 1, {static_cast<uint8_t>(on)}));
    power = transport.Query(1)[2];
    if(power != static_cast<uint8_t>(on)) throw std::runtime_error("Govee BLE power write was not confirmed");
}

void Session::Initialize(const Frame& frame, uint64_t now_ms)
{
    state = "connecting";
    // A diagnostic or caller may already own an authenticated transport. Reuse
    // it while still taking this Session's snapshot before all state writes.
    if(!transport.Connected()) transport.Connect();
    state = "initializing";
    ObservePower(transport.Query(1)[2]);
    last_power_ms = now_ms;
    Packet mode = transport.Query(5);
    if(profile == Profile::H6008 && mode[2] != 5 && mode[2] != 13)
        throw std::runtime_error("H6008 current scene is not a supported static/realtime mode");
    if(!snapshot_valid)
    {
        original_power = power;
        original_brightness = transport.Query(4)[2];
        if(profile == Profile::H6008 && mode[2] == 5)
        {
            // A prior process lost its snapshot. Explicit RGB establishes a NEW
            // baseline; never pretend to restore an unknown pre-reboot color.
            original_mode = Color(profile, ScaledRGB(frame));
            original_mode[0] = 0xAA;
            original_mode[19] ^= 0x33 ^ 0xAA;
            snapshot_valid = true; // Retain this baseline even if readback fails.
            recovered_baseline = true;
            recovery_pending = true;
            modified = true;
            transport.Send(RestoreMode(original_mode));
            mode = transport.Query(5);
            if(!MatchesColor(profile, mode, ScaledRGB(frame)))
                throw std::runtime_error("H6008 recovered RGB baseline was not confirmed");
            recovery_pending = false;
        }
        if(!RestorableMode(profile, mode)) throw std::runtime_error("Initial Govee BLE mode cannot be safely restored");
        original_mode = mode;
        snapshot_valid = true;
    }
    else if(profile == Profile::H6008 && recovery_pending)
    {
        transport.Send(RestoreMode(original_mode));
        mode = transport.Query(5);
        RGB baseline{{original_mode[3], original_mode[4], original_mode[5]}};
        if(!MatchesColor(profile, mode, baseline))
            throw std::runtime_error("H6008 pending recovered baseline was not confirmed");
        recovery_pending = false;
    }
    else if(profile == Profile::H6159 && !RestorableMode(profile, mode))
        throw std::runtime_error("H6159 was switched to an unsupported scene during reconnect");

    realtime_started = false;
    sent = false;
    if(!acquired && profile == Profile::H6008 && power_on_acquire && power == 0)
        acquire_on_pending = true;
    bool black = frame.rgb == RGB{{0, 0, 0}} || frame.brightness == 0;
    if(!acquired && profile == Profile::H6159 && power_on_acquire && black && power == 0)
        blackout_owned = true; // Explicit first-acquisition ON is deferred until RGB becomes nonzero.
    if(!acquired && profile == Profile::H6159 && power_on_acquire && !black && power == 0)
    {
        SendClassicColor(frame, true, now_ms);
        SetPower(true);
    }
    acquired = true;
    state = "ready";
}

void Session::SendRealtimeColor(const Frame& frame, bool force)
{
    if(!realtime_started)
    {
        modified = true;
        transport.Send(StartRealtime());
        realtime_started = true;
    }
    if(force || !sent || !(frame == last_frame))
    {
        modified = true;
        transport.Send(Realtime(ScaledRGB(frame)));
        last_frame = frame;
        sent = true;
    }
}

void Session::SendClassicColor(const Frame& frame, bool force, uint64_t now_ms)
{
    const bool confirm_current = force || !sent;
    if(!confirm_current && now_ms - last_color_check_ms >= 1500u)
    {
        // Check the last delivered color before overwriting it. This also runs
        // for a static frame, detecting an externally changed/ignored mode.
        if(!MatchesColor(profile, transport.Query(5), last_frame.rgb))
            throw std::runtime_error("H6159 periodic RGB readback mismatch");
        last_color_check_ms = now_ms;
    }
    if(force || !sent || frame.brightness != last_frame.brightness)
    {
        modified = true;
        transport.Send(MakePacket(0x33, 4, {BrightnessRaw(frame.brightness)}));
    }
    if(force || !sent || frame.rgb != last_frame.rgb)
    {
        modified = true;
        transport.Send(Color(profile, frame.rgb));
    }
    // ATT WriteWithResponse acknowledges each write. Keep an application-mode
    // readback on the first color of every connection and before any forced
    // power-on preload, rather than serializing a status exchange per frame.
    if(confirm_current)
    {
        if(!MatchesColor(profile, transport.Query(5), frame.rgb))
            throw std::runtime_error("H6159 RGB readback mismatch");
        last_color_check_ms = now_ms;
    }
    last_frame = frame;
    sent = true;
}

void Session::Step(const Frame& frame, uint64_t now_ms)
{
    if(frame.brightness > 100) throw std::invalid_argument("Govee BLE brightness exceeds 100");
    if(!acquired || !transport.Connected()) Initialize(frame, now_ms);
    if(now_ms - last_power_ms >= (profile == Profile::H6008 ? 500u : 1500u))
    {
        ObservePower(transport.Query(1)[2]);
        last_power_ms = now_ms;
    }
    if(profile == Profile::H6008 && acquire_on_pending)
    {
        if(power != 0 || external_off)
            acquire_on_pending = false; // An external ON takes precedence; we did not own it.
        else if(ScaledRGB(frame) != RGB{{0, 0, 0}})
        {
            SendRealtimeColor(frame, true); // Stage mode and newest RGB while OFF.
            acquire_power_owned = true; // Retain ownership if ON succeeds but its ACK is lost.
            SetPower(true);
            acquire_on_pending = false;
        }
    }
    if(profile == Profile::H6159)
    {
        bool black = frame.rgb == RGB{{0, 0, 0}} || frame.brightness == 0;
        if(black)
        {
            if(!blackout_owned)
            {
                ObservePower(transport.Query(1)[2]);
                if(power == 0 || external_off) { state = "paused_off"; return; }
                blackout_owned = true;
            }
            if(power != 0) SetPower(false);
            state = "blackout";
            return;
        }
        if(blackout_owned)
        {
            SendClassicColor(frame, true, now_ms); // Preload color/level while OFF, avoid stale white flash.
            SetPower(true);
            blackout_owned = false;
            external_off = false;
            state = "streaming";
            return;
        }
    }
    if(power == 0 || external_off)
    {
        sent = false;
        realtime_started = false;
        state = "paused_off";
        return;
    }
    if(profile == Profile::H6008)
    {
        SendRealtimeColor(frame, false);
    }
    else SendClassicColor(frame, false, now_ms);
    state = "streaming";
}

void Session::Release(bool keep_black)
{
    state = "releasing";
    if(keep_black)
    {
        // Explicit exit policy supersedes the startup snapshot. Do not acquire
        // or reconnect a light while shutting down, or restore it ON on failure.
        state = "black_exit_unconfirmed";
        if(acquired && transport.Connected())
        {
            SetPower(false); // ATT acknowledgement plus AA01 application readback.
            state = "black_exit_confirmed";
        }
        transport.Disconnect();
        return;
    }
    if(snapshot_valid && modified && transport.Connected())
    {
        // Only an initially OFF H6008 explicitly turned ON by this acquisition
        // is ours to switch back OFF. Pre-existing ON/external OFF remain alone.
        // Restore OFF before the old mode to avoid briefly showing a stale color.
        if(profile == Profile::H6008 && acquire_power_owned)
        {
            const uint8_t current = transport.Query(1)[2];
            if(current > 1) throw std::runtime_error("Unknown Govee BLE power state at release");
            if(current != original_power) SetPower(original_power != 0);
        }
        transport.Send(RestoreMode(original_mode));
        if(profile == Profile::H6159)
        {
            transport.Send(MakePacket(0x33, 4, {original_brightness}));
            const uint8_t current = transport.Query(1)[2];
            // An independently switched-off light must remain off on exit.
            const bool external_now_off = current == 0 && !blackout_owned;
            transport.Send(MakePacket(0x33, 1, {static_cast<uint8_t>(external_off || external_now_off ? 0 : original_power)}));
        }
    }
    transport.Disconnect();
    state = "idle";
}
}
