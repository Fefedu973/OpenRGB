// SPDX-License-Identifier: GPL-2.0-or-later
#include "GoveeDirectControl.h"
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using RGBColor = unsigned;
static unsigned RGBGetRValue(unsigned v) { return v & 255; }
static unsigned RGBGetGValue(unsigned v) { return (v >> 8) & 255; }
static unsigned RGBGetBValue(unsigned v) { return (v >> 16) & 255; }
enum { GOVEE_MODE_STATIC, GOVEE_MODE_DIRECT };
static GoveeDirectControl::Clock::time_point current;
static auto ClockNow() { return current; }
static void Time(unsigned ms) { current = GoveeDirectControl::Clock::time_point{} + std::chrono::milliseconds(ms); }
struct Radio
{
    std::vector<std::string> calls;
    void SendRazerEnable() { calls.push_back("enable"); }
    void SendRazerData(RGBColor*, unsigned n) { if(n != 42) throw std::runtime_error("wrong H61A2 segment count"); calls.push_back("frame"); }
    void SetBrightness(unsigned) { calls.push_back("brightness"); }
    void SetPower(bool b) { calls.push_back(b ? "on" : "off"); }
    void SetColor(unsigned, unsigned, unsigned) { calls.push_back("color"); }
    unsigned Count(const std::string& s) { return unsigned(std::count(calls.begin(), calls.end(), s)); }
};
struct Mode { unsigned value, brightness; std::vector<RGBColor> colors; };
struct RGBController_Govee
{
    Radio radio;
    Radio* controller = &radio;
    std::vector<RGBColor> colors = std::vector<RGBColor>(42, 0x123456);
    std::vector<Mode> modes = {{GOVEE_MODE_STATIC, 100, {0xabcdef}}, {GOVEE_MODE_DIRECT, 100, {}}};
    unsigned active_mode = 1, last_static_brightness = 0;
    RGBColor last_static_color = 0;
    bool razer_supported = true, static_initialized = false;
    std::atomic<bool> updates_started{false};
    std::mutex send_mutex;
    GoveeDirectControl direct_control;
    GoveeDirectControl::Clock::time_point last_update_time{};
    void DeviceUpdateLEDs(); void UpdateLEDsLocked(); void DeviceUpdateMode(); void UpdateStatic(bool);
};
#include "direct-methods.inc"
static unsigned checks = 0;
static void Check(bool b, const char* text) { ++checks; if(!b) throw std::runtime_error(text); }
int main()
{
    try
    {
        RGBController_Govee c;
        Time(0); c.DeviceUpdateMode();
        Check(c.radio.calls == std::vector<std::string>{"enable", "frame", "brightness", "on"}, "acquisition programs frame and brightness before ON");
        // Continuous output must not re-enter external-control mode or resend
        // unchanged brightness: both commands may disturb the active rendering.
        for(unsigned t=20; t<=120000; t+=20) { Time(t); c.DeviceUpdateLEDs(); }
        Check(c.radio.Count("on") == 3, "ON retries bounded to three during acquisition");
        Check(c.radio.Count("enable") == 3, "120 seconds at 50 FPS never re-enters mode after acquisition");
        Check(c.radio.Count("brightness") == 3, "120 seconds at 50 FPS never resends unchanged brightness");
        Check(c.radio.Count("frame") == 6001, "all RGB frames are still transmitted");
        Time(120020); c.modes[1].brightness=50; c.DeviceUpdateLEDs();
        Check(c.radio.Count("brightness") == 4, "brightness change sent immediately");
        Check(c.radio.Count("enable") == 3, "brightness change does not re-enter mode");
        Time(120040); c.DeviceUpdateLEDs();
        Check(c.radio.Count("brightness") == 4, "new unchanged brightness coalesced");

        Time(151040); c.DeviceUpdateLEDs();
        Check(c.radio.Count("enable") == 4 && c.radio.Count("brightness") == 5, "real 31-second output gap reacquires mode and brightness once");
        Check(c.radio.Count("on") == 3, "refresh does not rearm acquisition after manual OFF");
        for(unsigned t=151060; t<=191040; t+=20) { Time(t); c.DeviceUpdateLEDs(); }
        Check(c.radio.Count("enable") == 4 && c.radio.Count("brightness") == 5, "recovered continuous stream is not periodically reactivated");
        Time(191060); c.DeviceUpdateMode();
        Check(c.radio.Count("on") == 4, "explicit Direct selection starts a new acquisition");

        RGBController_Govee idle;
        Time(0); idle.DeviceUpdateLEDs(); // An LED update may arrive before mode callback.
        for(unsigned t=1000; t<=120000; t+=1000) { Time(t); idle.DeviceUpdateLEDs(); }
        Check(idle.radio.Count("on") == 3, "one-second worker keepalive services bounded acquisition");
        Check(idle.radio.Count("enable") == 3 && idle.radio.Count("brightness") == 3, "120 seconds of 1 FPS keepalive never counts as idle");
        Check(idle.radio.Count("frame") == 121, "one-second keepalive sends only normal frames after acquisition");
        Time(149999); idle.DeviceUpdateLEDs();
        Check(idle.radio.Count("enable") == 3, "gap shorter than thirty seconds does not reactivate");
        Time(179999); idle.DeviceUpdateLEDs();
        Check(idle.radio.Count("enable") == 4 && idle.radio.Count("brightness") == 4 && idle.radio.Count("on") == 3,
              "exact thirty-second gap reacquires without ON");
        RGBController_Govee delayed;
        Time(0); delayed.DeviceUpdateMode(); Time(4000); delayed.DeviceUpdateLEDs();
        Check(delayed.radio.Count("on") == 1, "late scheduling does not extend acquisition window");

        RGBController_Govee empty; empty.colors.clear();
        Time(0); empty.DeviceUpdateMode();
        Check(empty.radio.calls.empty(), "no acquisition or power for an empty frame");
        RGBController_Govee solid; solid.active_mode=0;
        Time(0); solid.DeviceUpdateMode();
        for(unsigned t=1000; t<30000; t+=1000) { Time(t); solid.DeviceUpdateLEDs(); }
        Check(solid.radio.Count("color")==1 && solid.radio.Count("brightness")==1, "queued keepalive does not flood static mode");
        Time(30000); solid.DeviceUpdateLEDs();
        Check(solid.radio.Count("color")==2 && solid.radio.Count("brightness")==2, "existing thirty-second static refresh preserved");
        std::cout << "PASS " << checks << " Govee Direct lifecycle assertions (production methods, fake clock/radio)\n";
        return 0;
    }
    catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
