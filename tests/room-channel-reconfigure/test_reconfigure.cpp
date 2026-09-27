// SPDX-License-Identifier: GPL-2.0-or-later
// USB/controller base are boundaries; the included production method bodies are unchanged.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

enum { ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE=2, ZONE_FLAG_MANUALLY_CONFIGURABLE_NAME=4,
       ZONE_FLAG_MANUALLY_CONFIGURABLE_TYPE=8, ZONE_FLAG_MANUALLY_CONFIGURABLE_MATRIX_MAP=16,
       ZONE_FLAG_MANUALLY_CONFIGURABLE_SEGMENTS=32, ZONE_FLAG_MANUALLY_CONFIGURED_SIZE=4096,
       ZONE_FLAG_MANUALLY_CONFIGURED_NAME=8192, ZONE_FLAG_MANUALLY_CONFIGURED_TYPE=16384,
       ZONE_FLAG_MANUALLY_CONFIGURED_MATRIX_MAP=32768, ZONE_TYPE_LINEAR=1, ZONE_TYPE_MATRIX=2,
       CORSAIR_LIGHTING_NODE_NUM_CHANNELS=2, CORSAIR_LIGHTING_NODE_MODE_DIRECT=0 };
struct Matrix {
    unsigned width=0,height=0; std::vector<unsigned> map;
    void Set(unsigned h,unsigned w,const unsigned* values) {
        height=h;width=w;map.assign(w*h,0);
        if(values) std::copy_n(values,w*h,map.begin());
    }
};
struct Zone { unsigned flags=0, leds_count=0, leds_min=0, leds_max=0;
              int type=0; std::string name; Matrix matrix_map; unsigned* colors=nullptr; };
struct led { std::string name; };
struct Mode { int value=0; };
struct FakeUSB {
    int last_channel=-1; unsigned last_count=0; const unsigned* last_colors=nullptr;
    unsigned GetNumChannels() { return 2; }
    unsigned GetLEDsPerChannel() { return 256; }
    std::string GetChannelName(unsigned char n) { return "Channel " + std::to_string(n+1); }
    const int* GetChannelIndex() { static int channels[]={5,4}; return channels; }
    void SetChannelLEDs(int c, const unsigned* p, unsigned n) {
        last_channel=c; last_count=n; last_colors=p;
    }
};
struct Boundary {
    FakeUSB usb;
    FakeUSB* controller=&usb;
    std::vector<Zone> zones;
    std::vector<led> leds;
    std::vector<unsigned> colors, leds_channel;
    std::vector<Mode> modes={{0}};
    unsigned active_mode=0;
    void SetupColors() {
        size_t count=0; for(const auto& z:zones) count+=z.leds_count;
        colors.resize(count);
        size_t start=0; for(auto& z:zones) { z.colors=count ? colors.data()+start : nullptr; start+=z.leds_count; }
    }
};
struct RGBController_Nollie: Boundary {
    void SetupZones(); void DeviceUpdateZoneLEDs(int zone); void DeviceUpdateSingleLED(int led);
};
struct RGBController_CorsairLightingNode: Boundary {
    void SetupZones(); void DeviceUpdateZoneLEDs(int zone); void DeviceUpdateSingleLED(int led);
};
#include "methods.inc"
using zone=Zone;
struct FakeGovee { std::string sku; std::string GetSku() { return sku; } };
struct RGBController_Govee: Boundary {
    FakeGovee radio;
    FakeGovee* controller=&radio;
    bool razer_supported=false;
    unsigned updates=0;
    void SetupZones(); void DeviceConfigureZone(int zone_idx);
    void DeviceUpdateLEDs() { ++updates; }
};
#include "govee-methods.inc"
static int assertions=0;
static void check(bool condition, const char* why) {
    ++assertions; if(!condition) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
template<class T> void exercise(bool remapped) {
    T c; c.SetupZones();
    for(auto& z:c.zones) z.flags |= ZONE_FLAG_MANUALLY_CONFIGURED_SIZE;
    // Increasing the first zone must move flat LED 3 from channel 1 to channel 0.
    for(const auto lengths: {std::vector<unsigned>{2,3}, {4,1}, {0,5}, {3,0}, {0,0}, {1,4}}) {
        c.zones[0].leds_count=lengths[0]; c.zones[1].leds_count=lengths[1]; c.SetupZones();
        for(unsigned i=0; i<lengths[0]+lengths[1]; ++i) {
            const unsigned zone=i<lengths[0] ? 0 : 1;
            c.DeviceUpdateSingleLED(i);
            check(c.usb.last_channel == (remapped ? 5-(int)zone : (int)zone), "single LED reaches its CURRENT USB channel after resize");
            check(c.usb.last_count == lengths[zone], "USB update length follows rebuilt zone");
            check(c.usb.last_colors == c.zones[zone].colors, "USB pointer belongs to current zone colors");
        }
        check(c.leds_channel.size() == lengths[0]+lengths[1], "routing table does not accumulate stale entries");
        check(c.leds.size() == c.colors.size(), "LED/color counts stay aligned");
    }
}
int main() {
    exercise<RGBController_Nollie>(true);
    exercise<RGBController_CorsairLightingNode>(false);
    for(const auto& item:std::vector<std::pair<std::string,unsigned>>{{"H61E1",30},{"H61A0",25},{"H61A2",42},{"H6062",57}}) {
        RGBController_Govee g; g.radio.sku=item.first; g.SetupZones();
        check(g.zones.size()==1 && g.colors.size()==item.second, "Govee native model segment count unchanged");
        auto& z=g.zones[0];
        check(z.leds_min==item.second && z.leds_max==item.second, "Govee cannot arbitrarily resize physical segments");
        check(!(z.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SIZE), "no Govee resize capability advertised");
        check((z.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_SEGMENTS)!=0, "Govee exposes logical segments");
        check((z.flags & ZONE_FLAG_MANUALLY_CONFIGURABLE_MATRIX_MAP)!=0, "Govee exposes sample geometry");
        check(g.razer_supported, "known Govee addressable models retain complete-frame transport capability");
        g.DeviceConfigureZone(-1); g.DeviceConfigureZone(1);
        check(g.updates==0, "invalid Govee zone does not trigger update");
        z.type=6; g.DeviceConfigureZone(0);
        check(g.leds.size()==item.second && g.colors.size()==item.second, "segmented geometry preserves physical frame size");
    }
    std::cout << assertions << " assertions passed (production methods; no hardware)\n";
}
