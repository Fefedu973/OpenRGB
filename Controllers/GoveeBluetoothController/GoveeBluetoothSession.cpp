/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "GoveeBluetoothSession.h"
#include <stdexcept>

namespace GoveeBluetooth
{
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
    transport.Connect(); // GATT validation, E7 + AA14 occur before all mode/color writes.
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

    bool black = frame.rgb == RGB{{0, 0, 0}} || frame.brightness == 0;
    if(!acquired && profile == Profile::H6159 && power_on_acquire && black && power == 0)
        blackout_owned = true; // Explicit first-acquisition ON is deferred until RGB becomes nonzero.
    if(!acquired && profile == Profile::H6159 && power_on_acquire && !black && power == 0)
    {
        SendClassicColor(frame, true);
        SetPower(true);
    }
    acquired = true;
    realtime_started = false;
    sent = false;
    state = "ready";
}

void Session::SendClassicColor(const Frame& frame, bool force)
{
    if(force || !sent || frame.brightness != last_frame.brightness)
    {
        modified = true;
        transport.Send(MakePacket(0x33, 4, {BrightnessRaw(frame.brightness)}));
    }
    if(force || !sent || frame.rgb != last_frame.rgb)
    {
        modified = true;
        transport.Send(Color(profile, frame.rgb));
        if(!MatchesColor(profile, transport.Query(5), frame.rgb))
            throw std::runtime_error("H6159 RGB readback mismatch");
    }
    last_frame = frame;
    sent = true;
}

void Session::Step(const Frame& frame, uint64_t now_ms)
{
    if(frame.brightness > 100) throw std::invalid_argument("Govee BLE brightness exceeds 100");
    if(!transport.Connected()) Initialize(frame, now_ms);
    if(now_ms - last_power_ms >= (profile == Profile::H6008 ? 500u : 1500u))
    {
        ObservePower(transport.Query(1)[2]);
        last_power_ms = now_ms;
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
            SendClassicColor(frame, true); // Preload color/level while OFF, avoid stale white flash.
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
        if(!realtime_started)
        {
            modified = true;
            transport.Send(StartRealtime());
            realtime_started = true;
        }
        if(!sent || !(frame == last_frame))
        {
            transport.Send(Realtime(ScaledRGB(frame)));
            last_frame = frame;
            sent = true;
        }
    }
    else SendClassicColor(frame, false);
    state = "streaming";
}

void Session::Release()
{
    state = "releasing";
    if(snapshot_valid && modified && transport.Connected())
    {
        // H6008 never claims power or brightness; preserve external controls.
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
