/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../GoveeBluetoothCredentialCache.h"
#include <cassert>
#include <iostream>
#include <type_traits>
#include <vector>
using namespace GoveeBluetooth;

struct Radio
{
    Key root{}, active{}, proposed{};
    bool authenticated = false, lose_confirm = false, io_error = false;
    unsigned dropped_identity = 0, generation = 0;
    uint64_t identity = 0x010203040506;
    std::vector<uint16_t> requests;
    Radio() { root.fill(0xA1); }
    Packet Exchange(uint8_t prefix, uint8_t command, const Key& key)
    {
        requests.push_back(static_cast<uint16_t>((prefix << 8) | command));
        if(io_error) throw std::runtime_error("simulated GATT error");
        if(prefix == 0xE7)
        {
            assert(key == root);
            if(authenticated) throw AuthenticationReplyTimeout("already in session");
            if(command == 1)
            {
                proposed.fill(static_cast<uint8_t>(++generation));
                auto packet = MakePacket(prefix, command);
                std::copy(proposed.begin(), proposed.end(), packet.begin() + 2);
                packet[19] = 0;
                for(unsigned i = 0; i < 19; ++i) packet[19] ^= packet[i];
                return packet;
            }
            assert(command == 2);
            active = proposed; authenticated = true;
            if(lose_confirm) { lose_confirm = false; throw AuthenticationReplyTimeout("lost E702 reply"); }
            return MakePacket(prefix, command);
        }
        assert(prefix == 0xAA && command == 0x14); // Never a lighting command.
        if(!authenticated || key != active) throw AuthenticationReplyTimeout("invalid session");
        if(dropped_identity) { --dropped_identity; throw AuthenticationReplyTimeout("lost AA14 reply"); }
        auto packet = MakePacket(prefix, command);
        for(unsigned i = 0; i < 6; ++i) packet[2 + i] = static_cast<uint8_t>(identity >> ((5 - i) * 8));
        packet[19] = 0;
        for(unsigned i = 0; i < 19; ++i) packet[19] ^= packet[i];
        return packet;
    }
    H6008Authentication::Result Connect(H6008Authentication& state)
    {
        return state.Connect(root, [this](auto prefix, auto command, const auto& key) { return Exchange(prefix, command, key); });
    }
};
template<class F> void Reject(F call)
{
    bool rejected = false;
    try { call(); } catch(const std::runtime_error&) { rejected = true; }
    assert(rejected);
}
int main()
{
    static_assert(!std::is_copy_constructible<H6008Authentication>::value, "Credentials must stay per transport");
    unsigned tests = 0;
    auto check = [&](const char* name) { ++tests; std::cout << "PASS " << name << '\n'; };
    {
        Radio radio; H6008Authentication state(radio.identity);
        assert(radio.Connect(state) == H6008Authentication::Result::Fresh);
        assert((radio.requests == std::vector<uint16_t>{0xE701,0xE702,0xAA14}));
        assert(state.VerifiedKey() == radio.active);
        radio.requests.clear();
        assert(radio.Connect(state) == H6008Authentication::Result::Resumed);
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        check("fresh handshake and identity-verified retained-session reconnect");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.Connect(state);
        const auto previous = radio.active;
        radio.authenticated = false; radio.requests.clear();
        assert(radio.Connect(state) == H6008Authentication::Result::Fresh);
        assert((radio.requests == std::vector<uint16_t>{0xAA14,0xE701,0xE702,0xAA14}));
        assert(state.VerifiedKey() != previous);
        check("physical reset invalidates prior credential and permits a fresh handshake");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.Connect(state);
        radio.dropped_identity = 1; radio.requests.clear();
        Reject([&] { radio.Connect(state); });
        assert((radio.requests == std::vector<uint16_t>{0xAA14,0xE701,0xE701}));
        radio.requests.clear();
        assert(radio.Connect(state) == H6008Authentication::Result::Resumed);
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        check("one lost status and failed new E7 do not destroy the last verified credential");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.lose_confirm = true;
        Reject([&] { radio.Connect(state); });
        Reject([&] { state.VerifiedKey(); });
        radio.requests.clear();
        assert(radio.Connect(state) == H6008Authentication::Result::Resumed);
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        check("lost E702 reply retains only a candidate until exact AA14 verifies it");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.dropped_identity = 1;
        Reject([&] { radio.Connect(state); });
        radio.requests.clear();
        assert(radio.Connect(state) == H6008Authentication::Result::Resumed);
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        check("lost post-handshake identity reply recovers without repeating E7");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.Connect(state);
        radio.identity ^= 1; radio.requests.clear();
        Reject([&] { radio.Connect(state); });
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        Reject([&] { state.VerifiedKey(); });
        check("wrong AA14 identity clears credentials and aborts without fallback");
    }
    {
        Radio radio; H6008Authentication state(radio.identity); radio.Connect(state);
        radio.io_error = true; radio.requests.clear();
        Reject([&] { radio.Connect(state); });
        assert((radio.requests == std::vector<uint16_t>{0xAA14}));
        check("GATT error is not misclassified as an expired session key");
    }
    {
        Radio first, second; second.identity += 1;
        H6008Authentication a(first.identity), b(second.identity);
        first.Connect(a); second.Connect(b);
        first.requests.clear(); second.requests.clear();
        assert(first.Connect(a) == H6008Authentication::Result::Resumed);
        assert(second.Connect(b) == H6008Authentication::Result::Resumed);
        assert(first.requests.size() == 1 && second.requests.size() == 1);
        a.Clear(); Reject([&] { a.VerifiedKey(); });
        assert(b.VerifiedKey() == second.active);
        check("independent transports retain no shared credential cache");
    }
    {
        Radio radio; H6008Authentication state(radio.identity);
        Reject([&] { state.Connect(radio.root, [](auto, auto, const auto&) { return Packet{}; }); });
        Reject([&] { state.VerifiedKey(); });
        check("malformed handshake cannot become a verified credential");
    }
    {
        CredentialCache cache; Radio radio;
        CredentialScope scope; scope.address = 1; scope.wifi_identity = radio.identity; scope.root = radio.root;
        for(unsigned cycle = 0; cycle < 3; ++cycle)
        {
            H6008Authentication recreated(radio.identity);
            if(auto candidates = cache.Take(scope, cycle * 10)) recreated.ImportCandidates(*candidates);
            assert(cache.Size(cycle * 10) == 0); // Consumed before any validation.
            assert(radio.Connect(recreated) == (cycle ? H6008Authentication::Result::Resumed : H6008Authentication::Result::Fresh));
            cache.Store(scope, recreated.ExportCandidates(), cycle * 10);
        }
        check("three destroyed/recreated authentication owners use process-memory handoff plus AA14");
    }
    {
        CredentialCache cache; Radio radio; H6008Authentication state(radio.identity); radio.Connect(state);
        CredentialScope scope; scope.address = 1; scope.wifi_identity = radio.identity; scope.root = radio.root;
        cache.Store(scope, state.ExportCandidates(), 0);
        auto other = scope; other.address = 2; assert(!cache.Take(other, 1));
        other = scope; other.profile = Profile::H6159; assert(!cache.Take(other, 1));
        other = scope; other.wifi_identity ^= 1; assert(!cache.Take(other, 1));
        other = scope; other.root[15] ^= 1; assert(!cache.Take(other, 1));
        assert(cache.Take(scope, 1));
        assert(!cache.Take(scope, 1));
        check("scope requires exact BLE/profile/AA14/root credential and entries are single-use");
    }
    {
        CredentialCache cache; CredentialScope scope; SessionCredentials credentials;
        credentials.verified_valid = true; credentials.verified.fill(7);
        cache.Store(scope, credentials, 10);
        assert(cache.Size(CredentialCache::TTL_MS + 9) == 1);
        assert(!cache.Take(scope, CredentialCache::TTL_MS + 10));
        cache.Store(scope, credentials, 10);
        assert(cache.Size(9) == 0); // Never extend validity across a backwards clock input.
        check("cache expires exactly at the five-minute monotonic boundary and prunes rollback");
    }
    {
        CredentialCache cache; SessionCredentials credentials; credentials.verified_valid = true;
        CredentialScope scope;
        for(unsigned i = 0; i < CredentialCache::CAPACITY + 1; ++i)
        {
            scope.address = i;
            cache.Store(scope, credentials, i);
        }
        assert(cache.Size(20) == CredentialCache::CAPACITY);
        scope.address = 0; assert(!cache.Take(scope, 20));
        scope.address = 1; assert(cache.Take(scope, 20));
        scope.address = 2; cache.Erase(scope); assert(!cache.Take(scope, 20));
        check("bounded sixteen-entry cache evicts oldest and supports explicit invalidation");
    }
    {
        CredentialCache cache; Radio radio; H6008Authentication original(radio.identity); radio.Connect(original);
        CredentialScope scope; scope.address = 1; scope.wifi_identity = radio.identity; scope.root = radio.root;
        cache.Store(scope, original.ExportCandidates(), 0);
        H6008Authentication recreated(radio.identity);
        recreated.ImportCandidates(*cache.Take(scope, 1));
        radio.identity ^= 1;
        Reject([&] { radio.Connect(recreated); });
        assert(cache.Size(1) == 0);
        Reject([&] { recreated.VerifiedKey(); });
        check("wrong-identity handoff clears local candidates and cannot remain in shared cache");
    }
    std::cout << tests << " authentication recovery tests passed\n";
}
