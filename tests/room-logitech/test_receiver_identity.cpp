/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "LogitechReceiverIdentity.h"
#include "LogitechReceiverProtocol.h"
#include <array>
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>

struct FakeReceiver
{
    std::string name;
    uint8_t feature;
    bool noise = false;
    bool missing = false;
    bool truncate = false;
    bool short_write = false;
    unsigned int writes = 0;
    std::deque<std::vector<unsigned char>> replies;

    int Write(const unsigned char* data, size_t size)
    {
        ++writes;
        // Model Windows long-message collection: the old 7-byte probe fails.
        if(size != 20 || data[0] != 0x11 || data[1] != 1 || short_write)
        {
            return -1;
        }
        if(missing) return static_cast<int>(size);
        std::vector<unsigned char> reply(20);
        std::copy(data, data + 4, reply.begin());
        if(data[2] == 0)
        {
            assert(data[3] == 0x0E && data[4] == 0 && data[5] == 5);
            reply[4] = feature;
        }
        else
        {
            assert(data[2] == feature); // Reject another receiver's cached index.
            if(data[3] == 0x0E) reply[4] = static_cast<unsigned char>(name.size());
            else
            {
                assert(data[3] == 0x1E);
                const size_t offset = data[4];
                for(size_t i = 0; i < 16 && offset + i < name.size(); ++i)
                    reply[4 + i] = static_cast<unsigned char>(name[offset + i]);
                if(truncate) reply.resize(5);
            }
        }
        if(noise)
        {
            auto unrelated = reply;
            unrelated[1] = 2; // Other paired slot.
            replies.push_back(unrelated);
            unrelated = reply;
            unrelated[3] ^= 1; // Other application's software ID.
            replies.push_back(unrelated);
            replies.push_back({0x10, 1, 0x41, 0, 0, 0x99, 0x40});
        }
        replies.push_back(reply);
        return static_cast<int>(size);
    }
    int Read(unsigned char* data, size_t size, int timeout)
    {
        assert(size == 64 && timeout > 0 && timeout <= 200);
        if(replies.empty()) return 0;
        auto result = replies.front();
        replies.pop_front();
        std::copy(result.begin(), result.end(), data);
        return static_cast<int>(result.size());
    }
    bool Probe(std::string& output)
    {
        return LogitechProbeReceiverName(
            [this](const unsigned char* d, size_t n) { return Write(d, n); },
            [this](unsigned char* d, size_t n, int t) { return Read(d, n, t); }, output);
    }
};

struct FakeRAP
{
    uint16_t wpid;
    bool notifications = false, pairing_registers = true, connected_ack_only = false;
    unsigned reads = 0, writes = 0, long_reads = 0;
    std::deque<std::vector<uint8_t>> short_queue, long_queue;
    int Write(const uint8_t* request, size_t size)
    {
        assert(size == 7 && request[0] == 0x10 && request[1] == 0xFF);
        ++writes;
        const auto op = request[2], reg = request[3], sub = request[4];
        std::vector<uint8_t> result(op == 0x83 ? 20 : 7);
        std::copy(request, request + 7, result.begin());
        if(op == 0x81 && reg == 0)
        {
            result[4] = 0x80; result[5] = 0x08; result[6] = 0x02;
        }
        if(op == 0x80 && reg == 0)
            assert(request[4] == 0x80 && request[5] == 0x09 && request[6] == 0x02);
        if(op == 0x81 && reg == 2) result[5] = 1;
        if(op == 0x83)
        {
            if(!pairing_registers || (sub != 0x20 && sub != 0x30 && sub != 0x40))
            {
                short_queue.push_back({0x10, 0xFF, 0x8F, op, reg, 2, 0});
                return static_cast<int>(size);
            }
            result[0] = 0x11;
            if(sub == 0x20) { result[7] = uint8_t(wpid >> 8); result[8] = uint8_t(wpid); }
            if(sub == 0x30) { result[5] = 1; result[6] = 2; result[7] = 3; result[8] = 4; }
            if(sub == 0x40) { result[5] = 4; result[6] = 'T'; result[7] = 'e'; result[8] = 's'; result[9] = 't'; }
            auto unrelated = result; unrelated[4] ^= 0x01; long_queue.push_back(unrelated);
            long_queue.push_back(result);
        }
        else short_queue.push_back(result);
        if(op == 0x80 && reg == 2 && notifications && !connected_ack_only)
            short_queue.push_back({0x10, 1, 0x41, 4, 0x12, uint8_t(wpid), uint8_t(wpid >> 8)});
        return static_cast<int>(size);
    }
    int Read(unsigned usage, uint8_t* buffer, size_t capacity, int timeout)
    {
        assert(capacity == 64 && timeout >= 0 && timeout <= 50);
        ++reads; if(usage == 2) ++long_reads;
        auto& queue = usage == 2 ? long_queue : short_queue;
        if(queue.empty()) return 0;
        const auto value = queue.front(); queue.pop_front();
        std::copy(value.begin(), value.end(), buffer);
        return static_cast<int>(value.size());
    }
    auto Discover()
    {
        return LogitechReceiverProtocol::Discover(
            [this](const uint8_t* d, size_t n) { return Write(d, n); },
            [this](unsigned u, uint8_t* d, size_t n, int t) { return Read(u, d, n, t); });
    }
};

int main()
{
    // Two same-VID/PID receivers, each split into short/long Windows collections.
    const char* a1 = R"(\\?\hid#vid_046d&pid_c547&mi_02&col01#8&aaaaaaa&0&0000#{hid-guid})";
    const char* a2 = R"(\\?\HID#VID_046D&PID_C547&MI_02&COL02#8&AAAAAAA&0&0001#{HID-GUID})";
    const char* b1 = R"(\\?\hid#vid_046d&pid_c547&mi_02&col01#8&bbbbbbb&0&0000#{hid-guid})";
    const char* b2 = R"(\\?\hid#vid_046d&pid_c547&mi_02&col02#8&bbbbbbb&0&0001#{hid-guid})";
    assert(LogitechSameReceiverCollection(a2, a1, 2, 2, 0xFF00, 1));
    assert(LogitechSameReceiverCollection(a1, a2, 2, 2, 0xFF00, 2));
    assert(LogitechSameReceiverCollection(b2, b1, 2, 2, 0xFF00, 1));
    assert(!LogitechSameReceiverCollection(a2, b1, 2, 2, 0xFF00, 1));
    assert(!LogitechSameReceiverCollection(b2, a1, 2, 2, 0xFF00, 1));
    assert(!LogitechSameReceiverCollection(a1, a2, 1, 2, 0xFF00, 2));
    assert(!LogitechSameReceiverCollection(a1, a2, 2, 2, 0x59, 1)); // LampArray
    assert(!LogitechSameReceiverCollection(a1, a2, 2, 2, 0xFF00, 0x102));
    assert(!LogitechSameReceiverCollection(nullptr, nullptr, 2, 2, 0xFF00, 2));
    assert(LogitechDevicePathKey("/dev/hidraw3") == "/dev/hidraw3");
    assert(LogitechDevicePathKey("CaseSensitiveIOService") == "CaseSensitiveIOService");

    FakeReceiver keyboard{"G915 TKL LIGHTSPEED", 3};
    FakeReceiver mouse{"G502 X PLUS", 7};
    keyboard.noise = mouse.noise = true;
    std::string keyboard_name, mouse_name;
    // Both enumeration orders must preserve separate handles and feature maps.
    for(unsigned int i = 0; i < 2; ++i)
    {
        if(i) { assert(mouse.Probe(mouse_name)); assert(keyboard.Probe(keyboard_name)); }
        else  { assert(keyboard.Probe(keyboard_name)); assert(mouse.Probe(mouse_name)); }
        assert(keyboard_name == keyboard.name && mouse_name == mouse.name);
    }
    assert(keyboard.writes == 8 && mouse.writes == 6); // Paginated keyboard name.
    std::string result = "stale G915";
    FakeReceiver absent{"G915", 3}; absent.missing = true;
    assert(!absent.Probe(result) && result.empty() && absent.writes == 1);
    FakeReceiver short_io{"G915", 3}; short_io.short_write = true;
    assert(!short_io.Probe(result));
    FakeReceiver unsupported{"G915", 0}; assert(!unsupported.Probe(result));
    FakeReceiver broken{"G915 TKL", 3}; broken.truncate = true;
    assert(!broken.Probe(result) && result.empty());
    FakeReceiver invalid_name{std::string("G915\0oops", 9), 3};
    assert(!invalid_name.Probe(result));
    FakeReceiver huge_name{std::string(129, 'X'), 3}; assert(!huge_name.Probe(result));
    FakeReceiver x_keyboard{"G915 X LIGHTSPEED", 9};
    assert(x_keyboard.Probe(result) && result == "G915 X LIGHTSPEED");

    unsigned int reads = 0;
    assert(!LogitechProbeReceiverName(
        [](const unsigned char*, size_t n) { return static_cast<int>(n); },
        [&reads](unsigned char* data, size_t, int)
        {
            ++reads;
            const unsigned char unrelated[] = {0x11, 2, 0, 0x0E, 3};
            std::copy(std::begin(unrelated), std::end(unrelated), data);
            return 5;
        }, result));
    {
        LogitechReceiverProtocol::Pairing pair{};
        const uint8_t timeout[20]{};
        const uint8_t ack[]{0x10, 0xFF, 0x80, 2, 0, 0, 0};
        const uint8_t bogus_slot[]{0x10, 0, 0x41, 4, 0x12, 0x99, 0x40};
        assert(!LogitechReceiverProtocol::Connection(timeout, 0, pair));
        assert(!LogitechReceiverProtocol::Connection(timeout, -1, pair));
        assert(!LogitechReceiverProtocol::Connection(timeout, 20, pair));
        assert(!LogitechReceiverProtocol::Connection(ack, sizeof(ack), pair));
        assert(!LogitechReceiverProtocol::Connection(bogus_slot, sizeof(bogus_slot), pair));
        FakeRAP missing{0x4099}; missing.pairing_registers=false;
        const auto none=missing.Discover(); assert(none.empty() && missing.reads<150);
        FakeRAP event{0x4099}; event.notifications=true;
        const auto connected=event.Discover();
        assert(connected.size()==1 && connected[0].slot==1 && connected[0].wpid==0x4099);
        assert(connected[0].online_known && connected[0].online);
        // Same-PID receivers remain independent, including when notifications
        // never arrive and only the long pairing register gives the slot.
        FakeRAP keyboard_rap{0x408E}, mouse_rap{0x4099};
        for(unsigned order=0;order<2;++order)
        {
            const auto first=order?mouse_rap.Discover():keyboard_rap.Discover();
            const auto second=order?keyboard_rap.Discover():mouse_rap.Discover();
            assert(first.size()==1 && second.size()==1 && first[0].slot==1 && second[0].slot==1);
            assert(first[0].wpid==(order?0x4099:0x408E));
            assert(second[0].wpid==(order?0x408E:0x4099));
            assert(!first[0].online_known && !second[0].online_known);
        }
        assert(keyboard_rap.long_reads>0 && mouse_rap.long_reads>0);
        LogitechReceiverProtocol::Report report{};
        for(uint8_t sub:std::array<uint8_t,2>{{0x30,0x40}})
        {
            assert(LogitechReceiverProtocol::Register(
                [&mouse_rap](const uint8_t* d,size_t n){return mouse_rap.Write(d,n);},
                [&mouse_rap](unsigned u,uint8_t* d,size_t n,int t){return mouse_rap.Read(u,d,n,t);},
                0x83,0xB5,{sub,0,0},sub,report));
            assert(report[0]==0x11 && report[4]==sub);
        }
        std::cout << "PASS: RAP ACK/timeout rejection, real pairing fallback, usage2 long replies, two receiver isolation\n";
    }
    assert(reads == 8 && result.empty());
    assert(!LogitechProbeReceiverName(
        [](const unsigned char*, size_t n) { return static_cast<int>(n); },
        [](unsigned char* data, size_t, int)
        {
            const unsigned char error[] = {0x10, 1, 0xFF, 0, 0x0E, 2, 0};
            std::copy(std::begin(error), std::end(error), data);
            return 7;
        }, result));
    std::cout << "PASS: receiver isolation, long HID++ feature discovery, correlated replies, pagination, failure bounds\n";
}
