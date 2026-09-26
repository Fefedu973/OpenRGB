/* SPDX-License-Identifier: GPL-2.0-or-later
 * Standalone offline tests. .cc deliberately excludes these from OpenRGB's
 * automatic Controllers/*.cpp enumeration. No WinRT/Bluetooth calls here. */
#include "GoveeBluetoothSession.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace GoveeBluetooth;

struct Fake : Transport
{
    bool connected = false;
    bool fail_color_readback = false;
    uint8_t power = 1, brightness = 151;
    Packet mode;
    unsigned int connects = 0;
    std::vector<Packet> writes;
    explicit Fake(Profile profile) : mode(MakePacket(0xAA, 5,
        {static_cast<uint8_t>(profile == Profile::H6008 ? 13 : 2), 12, 34, 56})) {}
    bool Connected() const override { return connected; }
    void Connect() override { connected = true; ++connects; }
    void Disconnect() noexcept override { connected = false; }
    void Send(const Packet& p) override
    {
        assert(connected && ValidPacket(p)); writes.push_back(p);
        if(p[1] == 1) power = p[2];
        else if(p[1] == 4) brightness = p[2];
        else if(p[1] == 5)
        {
            mode = p; mode[0] = 0xAA; mode[19] ^= 0x33 ^ 0xAA;
        }
    }
    Packet Query(uint8_t command) override
    {
        if(command == 1) return MakePacket(0xAA, 1, {power});
        if(command == 4) return MakePacket(0xAA, 4, {brightness});
        if(command == 5)
        {
            if(fail_color_readback) return MakePacket(0xAA, 5, {5});
            return mode;
        }
        throw std::runtime_error("Unexpected fake query");
    }
};

template<class F> void Reject(F body)
{
    bool threw = false;
    try { body(); } catch(const std::exception&) { threw = true; }
    assert(threw);
}

int main()
{
    unsigned int tests = 0;
    auto check = [&tests](const char* name) { ++tests; std::cout << "PASS " << name << '\n'; };
    assert(StartRealtime() == MakePacket(0x33, 5, {5, 1, 0, 0, 0}));
    assert(Realtime({{1, 2, 3}}) == MakePacket(0x33, 5, {5, 0, 1, 2, 3}));
    assert(Realtime({{1, 2, 3}})[19] == 0x33);
    check("exact 20-byte realtime vectors and XOR checksum");
    Reject([] { MakePacket(1, 2, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}); });
    Reject([] { ParseAddress("02:00:00:00:00:GG"); });
    Reject([] { ParseKey("001122"); });
    assert(ParseAddress("02:00:00:00:00:01") == 0x020000000001ULL);
    assert(ParseKey("000102030405060708090a0b0c0d0e0f\n")[15] == 15);
    check("strict configuration and packet domains");
    assert(ScaledRGB({{{255, 50, 1}}, 0}) == (RGB{{0,0,0}}));
    assert(ScaledRGB({{{200, 100, 50}}, 50}) == (RGB{{100,50,25}}));
    assert(BrightnessRaw(100) == 255 && BrightnessRaw(50) == 128);
    check("brightness scaling and real zero");
    {
        Fake radio(Profile::H6008);
        radio.mode = MakePacket(0xAA, 5, {13,12,34,56,0x11,0xF8,0xAA,0x55});
        const Packet before = radio.mode;
        Session session(Profile::H6008, radio);
        session.Step({{{9,8,7}},100},0);
        assert(radio.writes.size() == 2 && radio.writes[0] == StartRealtime());
        assert(radio.writes[1] == Realtime({{9,8,7}}));
        session.Release();
        assert(radio.mode == before && radio.brightness == 151 && radio.power == 1);
        assert(std::none_of(radio.writes.begin(),radio.writes.end(),[](const Packet& p){return p[1]==1||p[1]==4;}));
        check("H6008 exact white/RGB snapshot restoration; no power or brightness writes");
    }
    {
        Fake radio(Profile::H6008); radio.mode = MakePacket(0xAA,5,{5,0,9,9,9});
        Session session(Profile::H6008,radio);
        session.Step({{{40,20,10}},50},0);
        assert(session.RecoveredBaseline());
        assert(radio.writes[0] == Color(Profile::H6008,{{20,10,5}}));
        session.Release();
        assert(MatchesColor(Profile::H6008,radio.mode,{{20,10,5}}));
        check("orphan realtime05 establishes explicitly requested new baseline");
    }
    {
        Fake radio(Profile::H6008); radio.mode=MakePacket(0xAA,5,{5});radio.fail_color_readback=true;
        Session session(Profile::H6008,radio);
        Reject([&]{ session.Step({{{3,4,5}},100},0); });
        radio.Disconnect();
        Reject([&]{ session.Step({{{8,9,10}},100},1000); });
        assert(std::none_of(radio.writes.begin(),radio.writes.end(),[](const Packet& p){return p[2]==5;}));
        radio.fail_color_readback=false;radio.Disconnect();
        session.Step({{{8,9,10}},100},2000);session.Release();
        assert(MatchesColor(Profile::H6008,radio.mode,{{3,4,5}}));
        check("failed recovery readback never enters realtime; provisional baseline survives reconnect");
    }
    {
        Fake radio(Profile::H6008);radio.mode=MakePacket(0xAA,5,{7});Session session(Profile::H6008,radio);
        Reject([&]{session.Step({{{1,2,3}},100},0);});assert(radio.writes.empty());
        check("unknown H6008 scene rejected before mode or color write");
    }
    {
        Fake radio(Profile::H6008);radio.power=0;Session session(Profile::H6008,radio);
        session.Step({{{1,2,3}},100},0);assert(session.State()=="paused_off" && radio.writes.empty());
        radio.power=1;session.Step({{{1,2,3}},100},501);assert(session.State()=="streaming");
        radio.power=0;session.Step({{{4,5,6}},100},1002);const auto count=radio.writes.size();
        session.Step({{{7,8,9}},100},2000);assert(radio.writes.size()==count);
        session.Release();assert(radio.power==0);
        check("H6008 external OFF respected across subsequent frames and release");
    }
    {
        Fake radio(Profile::H6008);Session session(Profile::H6008,radio);Frame frame{{{4,5,6}},100};
        session.Step(frame,0);const auto count=radio.writes.size();
        session.Step(frame,100);session.Step(frame,200);assert(radio.writes.size()==count);
        radio.Disconnect();session.Step(frame,1000);assert(radio.connects==2);
        assert(radio.writes[radio.writes.size()-2]==StartRealtime());
        check("unchanged colors coalesced and realtime reinitialized after reconnect");
    }
    {
        Fake radio(Profile::H6159);radio.mode=MakePacket(0xAA,5,{2,12,34,56,1,78,90,123});
        const auto before=radio.mode;Session session(Profile::H6159,radio);
        session.Step({{{7,8,9}},75},0);session.Release();
        assert(radio.mode==before && radio.brightness==151 && radio.power==1);
        check("H6159 secondary-color flag and raw brightness restored exactly");
    }
    {
        Fake radio(Profile::H6159);Session session(Profile::H6159,radio);
        session.Step({{{0,0,0}},100},0);assert(radio.power==0 && session.State()=="blackout");
        auto begin=radio.writes.size();session.Step({{{20,30,40}},60},100);
        assert(radio.writes[begin][1]==4 && radio.writes[begin+1][1]==5 && radio.writes[begin+2][1]==1);
        assert(radio.power==1 && MatchesColor(Profile::H6159,radio.mode,{{20,30,40}}));
        check("H6159 zero OFF ownership; newest color and brightness preloaded before ON");
    }
    {
        Fake radio(Profile::H6159);Session session(Profile::H6159,radio);
        session.Step({{{200,100,50}},0},0);assert(radio.power==0);
        session.Release();assert(radio.power==1 && radio.brightness==151);
        check("zero brightness also owns blackout and shutdown restores initial power");
    }
    {
        Fake radio(Profile::H6159);Session session(Profile::H6159,radio,true);
        session.Step({{{1,2,3}},100},0);radio.power=0;
        session.Step({{{0,0,0}},100},2000);
        const auto count=radio.writes.size();session.Step({{{8,9,10}},100},4000);
        assert(radio.power==0 && radio.writes.size()==count && session.State()=="paused_off");
        session.Release();assert(radio.power==0);
        check("externally switched OFF is not mistaken for our black frame, even with acquisition ON opt-in");
    }
    {
        Fake radio(Profile::H6159);radio.power=0;Session session(Profile::H6159,radio,true);
        session.Step({{{6,7,8}},100},0);
        assert(radio.writes[0][1]==4 && radio.writes[1][1]==5 && radio.writes[2][1]==1);
        session.Release();assert(radio.power==0);
        check("optional first-acquisition ON preloads RGB and restores initial OFF");
    }
    {
        Fake radio(Profile::H6159);radio.power=0;Session session(Profile::H6159,radio,true);
        session.Step({{{0,0,0}},100},0);assert(radio.power==0 && radio.writes.empty());
        session.Step({{{20,30,40}},100},100);assert(radio.power==1);
        session.Release();assert(radio.power==0);
        check("opt-in initial ON deferred through black until first nonzero RGB, retaining OFF snapshot");
    }
    {
        Fake radio(Profile::H6159);radio.mode=MakePacket(0xAA,5,{2,12,34,56,3});Session session(Profile::H6159,radio);
        Reject([&]{session.Step({{{1,2,3}},100},0);});assert(radio.writes.empty());
        check("unknown H6159 mode fields rejected before writes");
    }
    std::cout << tests << " offline protocol/session tests passed\n";
}
