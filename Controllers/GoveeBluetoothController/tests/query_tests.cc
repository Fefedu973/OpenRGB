/* SPDX-License-Identifier: GPL-2.0-or-later
 * Offline request/response failures. No Windows or Bluetooth access. */
#include "GoveeBluetoothQuery.h"
#include "GoveeBluetoothSession.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace GoveeBluetooth;

struct Timeout : std::runtime_error { Timeout() : std::runtime_error("fake missing reply") {} };

struct Radio : Transport
{
    bool connected = false;
    unsigned int connects = 0, disconnects = 0, missing = 0, reads = 0;
    std::vector<Packet> writes;
    bool Connected() const override { return connected; }
    void Connect() override { connected = true; ++connects; }
    void Disconnect() noexcept override { connected = false; ++disconnects; }
    void Send(const Packet& p) override { assert(connected); writes.push_back(p); }
    Packet Query(uint8_t command) override
    {
        return QueryWithRetry<Timeout>(Profile::H6008, command, [&](unsigned int)
        {
            assert(connected);
            ++reads;
            if(command == 1 && missing) { --missing; throw Timeout(); }
            if(command == 1) return MakePacket(0xAA, 1, {1});
            if(command == 4) return MakePacket(0xAA, 4, {180});
            assert(command == 5);
            return MakePacket(0xAA, 5, {13, 10, 20, 30});
        }).reply;
    }
};

int main()
{
    unsigned int tests = 0;
    auto pass = [&](const char* name) { ++tests; std::cout << "PASS " << name << '\n'; };
    {
        unsigned int calls = 0;
        const auto expected = MakePacket(0xAA, 1, {0});
        const auto result = QueryWithRetry<Timeout>(Profile::H6008, 1, [&](unsigned int attempt)
        {
            assert(attempt == ++calls);
            if(attempt == 1) throw Timeout();
            return expected;
        });
        assert(result.reply == expected && result.attempts == 2 && calls == 2);
        pass("AA01 missing reply retries once and preserves the fresh OFF response");
    }
    {
        unsigned int calls = 0;
        bool timed_out = false;
        try
        {
            QueryWithRetry<Timeout>(Profile::H6008, 1, [&](unsigned int) -> Packet { ++calls; throw Timeout(); });
        }
        catch(const Timeout&) { timed_out = true; }
        assert(timed_out && calls == 2);
        pass("two missing AA01 replies propagate the timeout without a third attempt");
    }
    {
        Radio radio;
        Session session(Profile::H6008, radio);
        const Frame frame{{{45,67,89}},100};
        session.Step(frame, 0);
        const auto writes = radio.writes.size();
        const auto reads = radio.reads;
        radio.missing = 1;
        session.Step(frame, 500);
        assert(session.State() == "streaming" && radio.connects == 1 && radio.disconnects == 0);
        assert(radio.reads == reads + 2 && radio.writes.size() == writes);
        pass("periodic AA01 recovery keeps the session and does not replay mode or color");
    }
    {
        Radio radio;
        Session session(Profile::H6008, radio);
        const Frame frame{{{45,67,89}},100};
        session.Step(frame, 0);
        const auto writes = radio.writes.size();
        const auto reads = radio.reads;
        radio.missing = 2;
        bool timed_out = false;
        try { session.Step(frame, 500); } catch(const Timeout&) { timed_out = true; }
        assert(timed_out && radio.reads == reads + 2 && radio.writes.size() == writes);
        // Match the owning worker's recovery policy, with no retry inside Session.
        radio.Disconnect();
        session.Step(frame, 1500);
        assert(session.State() == "streaming" && radio.connects == 2 && radio.disconnects == 1);
        assert(radio.writes.size() == writes + 2);
        pass("exhaustion leaves reconnection to the worker and resumes realtime afterwards");
    }
    {
        for(uint8_t command : {uint8_t(4), uint8_t(5), uint8_t(0x14)})
        {
            unsigned int calls = 0;
            try { QueryWithRetry<Timeout>(Profile::H6008, command, [&](unsigned int) -> Packet { ++calls; throw Timeout(); }); }
            catch(const Timeout&) {}
            assert(calls == 1);
        }
        pass("other H6008 reads remain single-attempt");
    }
    {
        for(uint8_t command : {uint8_t(1), uint8_t(4), uint8_t(5)})
        {
            unsigned int calls = 0;
            const auto expected = MakePacket(0xAA, command);
            const auto result = QueryWithRetry<Timeout>(Profile::H6159, command, [&](unsigned int) -> Packet
            {
                if(++calls == 1) throw Timeout();
                return expected;
            });
            assert(calls == 2 && result.reply == expected);
        }
        pass("H6159 existing one-retry policy is preserved");
    }
    {
        unsigned int calls = 0;
        bool cancelled = false;
        try { QueryWithRetry<Timeout>(Profile::H6008, 1, [&](unsigned int) -> Packet { ++calls; throw std::runtime_error("cancelled/GATT failure"); }); }
        catch(const std::runtime_error&) { cancelled = true; }
        assert(cancelled && calls == 1);
        pass("cancellation and non-timeout GATT failures never start another attempt");
    }
    {
        unsigned int calls = 0;
        const auto result = QueryWithRetry<Timeout>(Profile::H6008, 1, [&](unsigned int) { ++calls; return MakePacket(0xAA, 1, {1}); });
        assert(calls == 1 && result.attempts == 1);
        pass("normal reply adds no request or delay");
    }
    std::cout << tests << " query tests passed\n";
}
