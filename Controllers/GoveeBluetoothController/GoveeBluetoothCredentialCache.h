/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GoveeBluetoothAuthentication.h"
#include <list>
#include <mutex>
#include <optional>

namespace GoveeBluetooth
{
/* Exact configuration identity. A different root credential must never reuse
 * another configuration's authenticated session, even on the same BLE address. */
struct CredentialScope
{
    uint64_t address = 0, wifi_identity = 0;
    Profile profile = Profile::H6008;
    Key root{};
    ~CredentialScope() { EraseKey(root); }
    bool operator==(const CredentialScope& other) const
    {
        return address == other.address && wifi_identity == other.wifi_identity &&
            profile == other.profile && root == other.root;
    }
};

/* Small process-memory handoff across controller destruction/rescan. Entry
 * credentials are candidates, never authorization: the receiving transport
 * must verify AA14. Expiry is checked/pruned on every access. Time is injected
 * in milliseconds from a monotonic clock so offline tests need no sleeps. */
class CredentialCache
{
public:
    static constexpr uint64_t TTL_MS = 5 * 60 * 1000;
    static constexpr size_t CAPACITY = 16;
    std::optional<SessionCredentials> Take(const CredentialScope& scope, uint64_t now_ms)
    {
        std::lock_guard<std::mutex> guard(mutex);
        Prune(now_ms);
        for(auto it = entries.begin(); it != entries.end(); ++it)
        {
            if(it->scope == scope)
            {
                SessionCredentials value = it->credentials;
                entries.erase(it); // Consume: a failed verification leaves no stale shared entry.
                return value;
            }
        }
        return std::nullopt;
    }
    void Store(const CredentialScope& scope, const SessionCredentials& credentials, uint64_t now_ms)
    {
        std::lock_guard<std::mutex> guard(mutex);
        Prune(now_ms);
        EraseUnlocked(scope);
        if(!credentials.verified_valid && !credentials.pending_valid) return;
        if(entries.size() == CAPACITY) entries.pop_front(); // Secure destructors wipe the oldest entry.
        entries.push_back({scope, credentials, now_ms});
    }
    void Erase(const CredentialScope& scope)
    {
        std::lock_guard<std::mutex> guard(mutex);
        EraseUnlocked(scope);
    }
    size_t Size(uint64_t now_ms)
    {
        std::lock_guard<std::mutex> guard(mutex);
        Prune(now_ms);
        return entries.size();
    }
private:
    struct Entry
    {
        CredentialScope scope;
        SessionCredentials credentials;
        uint64_t saved_ms;
    };
    void Prune(uint64_t now_ms)
    {
        for(auto it = entries.begin(); it != entries.end();)
        {
            if(now_ms < it->saved_ms || now_ms - it->saved_ms >= TTL_MS) it = entries.erase(it);
            else ++it;
        }
    }
    void EraseUnlocked(const CredentialScope& scope)
    {
        for(auto it = entries.begin(); it != entries.end();)
            if(it->scope == scope) it = entries.erase(it); else ++it;
    }
    std::mutex mutex;
    std::list<Entry> entries;
};
}
