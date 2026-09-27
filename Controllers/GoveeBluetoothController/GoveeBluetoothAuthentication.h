/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GoveeBluetoothProtocol.h"
#include <algorithm>
#include <functional>
#include <stdexcept>

namespace GoveeBluetooth
{
inline void EraseKey(Key& key) noexcept
{
    volatile uint8_t* bytes = key.data();
    for(size_t i = 0; i < key.size(); ++i) bytes[i] = 0;
}

struct SessionCredentials
{
    Key verified{}, pending{};
    bool verified_valid = false, pending_valid = false;
    ~SessionCredentials() { EraseKey(verified); EraseKey(pending); }
};

class AuthenticationReplyTimeout : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/* One allowlisted device, owned by its MTA transport. Windows can retain the
 * physical authenticated link after projected GATT objects have been closed.
 * A cached credential is only a candidate until a fresh AA14 verifies identity.
 * No lighting packet is sent here. Neither keys nor payloads are persisted. */
class H6008Authentication
{
public:
    enum class Result { Fresh, Resumed };
    using Exchange = std::function<Packet(uint8_t, uint8_t, const Key&)>;
    explicit H6008Authentication(uint64_t expected_identity_) : expected_identity(expected_identity_) {}
    ~H6008Authentication() { Clear(); }
    H6008Authentication(const H6008Authentication&) = delete;
    H6008Authentication& operator=(const H6008Authentication&) = delete;

    Result Connect(const Key& root_key, const Exchange& exchange)
    {
        // E702 may have reached the bulb even if its response was lost. Try that
        // candidate first, then the last verified session, before a new E7.
        if(pending_valid && Probe(pending, exchange))
        {
            CommitPending();
            return Result::Resumed;
        }
        if(verified_valid && Probe(verified, exchange))
        {
            Wipe(pending); pending_valid = false;
            return Result::Resumed;
        }

        Packet response{};
        for(unsigned attempt = 0; attempt < 2; ++attempt)
        {
            try { response = exchange(0xE7, 1, root_key); break; }
            catch(const AuthenticationReplyTimeout&) { if(attempt == 1) throw; }
        }
        CheckPacket(response, 0xE7, 1);
        std::copy_n(response.begin() + 2, pending.size(), pending.begin());
        pending_valid = true;
        CheckPacket(exchange(0xE7, 2, root_key), 0xE7, 2);
        CheckIdentity(exchange(0xAA, 0x14, pending));
        CommitPending();
        return Result::Fresh;
    }

    const Key& VerifiedKey() const
    {
        if(!verified_valid) throw std::runtime_error("No verified Govee BLE session credential");
        return verified;
    }
    SessionCredentials ExportCandidates() const
    {
        SessionCredentials result;
        result.verified = verified; result.pending = pending;
        result.verified_valid = verified_valid; result.pending_valid = pending_valid;
        return result;
    }
    void ImportCandidates(const SessionCredentials& value)
    {
        // This does not authenticate a transport: Connect must verify AA14.
        verified = value.verified; pending = value.pending;
        verified_valid = value.verified_valid; pending_valid = value.pending_valid;
    }
    void Clear() noexcept
    {
        Wipe(verified); Wipe(pending);
        verified_valid = pending_valid = false;
    }
private:
    static void Wipe(Key& key) noexcept
    {
        EraseKey(key);
    }
    static void CheckPacket(const Packet& packet, uint8_t prefix, uint8_t command)
    {
        if(!ValidPacket(packet) || packet[0] != prefix || packet[1] != command)
            throw std::runtime_error("Invalid Govee BLE authentication response");
    }
    void CheckIdentity(const Packet& packet)
    {
        CheckPacket(packet, 0xAA, 0x14);
        uint64_t reported = 0;
        for(unsigned index = 2; index < 8; ++index) reported = (reported << 8) | packet[index];
        if(reported != expected_identity)
        {
            Clear();
            throw std::runtime_error("Govee BLE authenticated AA14 identity mismatch");
        }
    }
    bool Probe(const Key& key, const Exchange& exchange)
    {
        try { CheckIdentity(exchange(0xAA, 0x14, key)); return true; }
        catch(const AuthenticationReplyTimeout&) { return false; }
        // A GATT error or a wrong identity is not an invalid-key timeout and
        // must not be concealed by proceeding with further authentication.
    }
    void CommitPending()
    {
        verified = pending;
        verified_valid = true;
        Wipe(pending); pending_valid = false;
    }
    const uint64_t expected_identity;
    Key verified{}, pending{};
    bool verified_valid = false, pending_valid = false;
};
}
