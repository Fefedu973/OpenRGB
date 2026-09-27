// SPDX-License-Identifier: GPL-2.0-or-later
#include "KeyboardInputService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace room_input
{
struct KeyboardInputService::State
{
    std::unique_ptr<KeyboardInputBackend> backend;
    std::function<double()> clock;
    mutable std::mutex lifecycle, events_mutex;
    std::string status = "Inactive";
    std::uint64_t next_token = 0, sequence = 0;
    std::unordered_map<std::uint64_t, std::uint64_t> listeners;
    std::unordered_map<std::string, std::unordered_set<std::uint16_t>> held;
    std::deque<KeyboardEvent> events;

    void Prune(double now)
    {
        while(!events.empty() && (now - events.front().time > .250 || events.size() > 64)) events.pop_front();
    }
    void Key(const std::string& path, std::uint16_t scan, bool up, double time)
    {
        if(path.empty() || path.size() > 4096 || !scan || !std::isfinite(time)) return;
        std::lock_guard<std::mutex> lock(events_mutex);
        if(listeners.empty()) return;
        auto device = held.find(path);
        if(device == held.end())
        {
            if(up || held.size() >= 64) return;
            device = held.emplace(path, std::unordered_set<std::uint16_t>{}).first;
        }
        if(up) { device->second.erase(scan); return; }
        if(device->second.size() >= 512 || !device->second.insert(scan).second) return;
        events.push_back({++sequence, time, path, scan});
        Prune(time);
    }
    void Removed(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        if(path.empty()) { held.clear(); events.clear(); return; }
        held.erase(path);
        events.erase(std::remove_if(events.begin(), events.end(), [&](const KeyboardEvent& event) { return event.device_path == path; }), events.end());
    }
};

namespace
{
#ifdef _WIN32
class WindowsKeyboardBackend final : public KeyboardInputBackend
{
    Key key;
    Removed removed;
    std::thread worker;
    HANDLE stop = nullptr;
    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    bool ready = false, success = false;
    std::string status;
    std::unordered_map<HANDLE, std::string> devices; // worker-thread only

    static bool Registrations(std::vector<RAWINPUTDEVICE>& devices)
    {
        UINT count = 0;
        if(GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) == UINT(-1) || count > 256) return false;
        devices.resize(count);
        if(count == 0) return true;
        const UINT read = GetRegisteredRawInputDevices(devices.data(), &count, sizeof(RAWINPUTDEVICE));
        if(read == UINT(-1)) return false;
        devices.resize(read);
        return true;
    }
    static bool Keyboard(const RAWINPUTDEVICE& device)
    {
        // A whole Generic Desktop page subscription also includes keyboards.
        return device.usUsagePage == 1 && (device.usUsage == 6 ||
            (device.usUsage == 0 && (device.dwFlags & RIDEV_PAGEONLY)));
    }

    void Complete(bool ok, const char* message)
    {
        std::lock_guard<std::mutex> lock(ready_mutex);
        success = ok; status = message; ready = true;
        ready_cv.notify_one();
    }
    std::string Path(HANDLE handle)
    {
        const auto cached = devices.find(handle);
        if(cached != devices.end()) return cached->second;
        if(!handle || devices.size() >= 64) return {};
        UINT size = 0;
        if(GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &size) == UINT(-1) || !size || size > 4096) return {};
        std::vector<wchar_t> path(size + 1, L'\0');
        const UINT read = GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, path.data(), &size);
        if(read == UINT(-1)) return {};
        const int length = static_cast<int>(wcsnlen(path.data(), path.size()));
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, nullptr, 0, nullptr, nullptr);
        if(bytes <= 0 || bytes > 4096) return {};
        std::string result(bytes, '\0');
        if(!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), length, &result[0], bytes, nullptr, nullptr)) return {};
        devices.emplace(handle, result);
        return result;
    }
    void Input(LPARAM parameter)
    {
        RAWINPUT input{};
        UINT size = sizeof(input);
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(parameter), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
        if(input.header.dwType != RIM_TYPEKEYBOARD || size < sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD)) return;
        const auto& keyboard = input.data.keyboard;
        // No VK-to-character/layout conversion: retain only actual physical scans.
        if(keyboard.MakeCode == 0xff || keyboard.VKey >= 0xff || !keyboard.MakeCode) return;
        const std::uint16_t prefix = keyboard.Flags & RI_KEY_E0 ? 0xe000 : keyboard.Flags & RI_KEY_E1 ? 0xe100 : 0;
        key(Path(input.header.hDevice), prefix | (keyboard.MakeCode & 0x7f), (keyboard.Flags & RI_KEY_BREAK) != 0, KeyboardInputService::SteadyTime());
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* self = reinterpret_cast<WindowsKeyboardBackend*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if(message == WM_NCCREATE)
        {
            self = static_cast<WindowsKeyboardBackend*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if(self)
        {
            if(message == WM_INPUT) self->Input(lparam);
            else if(message == WM_INPUT_DEVICE_CHANGE && wparam == GIDC_REMOVAL)
            {
                const auto device = self->devices.find(reinterpret_cast<HANDLE>(lparam));
                if(device != self->devices.end())
                { self->removed(device->second); self->devices.erase(device); }
            }
            else if(message == WM_POWERBROADCAST && (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMSUSPEND))
            { self->devices.clear(); self->removed({}); }
        }
        // Required cleanup for RIM_INPUT; also preserves all normal window behavior.
        return DefWindowProcW(window, message, wparam, lparam);
    }
    void Run()
    {
        const HINSTANCE module = GetModuleHandleW(nullptr);
        const wchar_t* class_name = L"OpenRGBRoomPhysicalKeyboardInput";
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.lpfnWndProc = WindowProc;
        cls.hInstance = module; cls.lpszClassName = class_name;
        if(!RegisterClassExW(&cls)) { Complete(false, "Keyboard input window registration failed"); return; }
        HWND window = CreateWindowExW(0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, module, this);
        // Message-only windows are not broadcast targets: explicitly subscribe.
        const HPOWERNOTIFY power = window ? RegisterSuspendResumeNotification(window, DEVICE_NOTIFY_WINDOW_HANDLE) : nullptr;
        bool registered = false;
        std::vector<RAWINPUTDEVICE> existing;
        if(!window) Complete(false, "Keyboard input window creation failed");
        else if(!Registrations(existing)) Complete(false, "Keyboard input registration could not be inspected");
        else if(std::any_of(existing.begin(), existing.end(), Keyboard)) Complete(false, "Keyboard input unavailable: another in-process registration owns keyboards");
        else
        {
            RAWINPUTDEVICE device{1, 6, RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, window};
            registered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
            Complete(registered, registered ? "Active: physical keyboard effects" : "Keyboard input registration failed");
        }
        if(registered)
        {
            while(WaitForSingleObject(stop, 0) != WAIT_OBJECT_0)
            {
                const DWORD wait = MsgWaitForMultipleObjects(1, &stop, FALSE, 50, QS_ALLINPUT);
                if(wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
                MSG message{};
                // Bound each drain so continuous input cannot starve shutdown.
                for(unsigned count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count)
                { DispatchMessageW(&message); }
            }
            // A later consumer may have replaced our registration. Never remove it.
            if(Registrations(existing) && std::any_of(existing.begin(), existing.end(), [window](const RAWINPUTDEVICE& item) { return Keyboard(item) && item.hwndTarget == window; }))
            {
                RAWINPUTDEVICE remove{1, 6, RIDEV_REMOVE, nullptr};
                RegisterRawInputDevices(&remove, 1, sizeof(remove));
            }
        }
        if(power) UnregisterSuspendResumeNotification(power);
        if(window) DestroyWindow(window);
        UnregisterClassW(class_name, module);
        devices.clear();
    }
public:
    ~WindowsKeyboardBackend() override { Stop(); }
    bool Start(Key callback, Removed removal, std::string& result) override
    {
        key = std::move(callback); removed = std::move(removal);
        ready = false; success = false;
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if(!stop) { result = "Keyboard input stop event creation failed"; return false; }
        try { worker = std::thread([this] { Run(); }); }
        catch(...) { CloseHandle(stop); stop = nullptr; result = "Keyboard input thread creation failed"; return false; }
        std::unique_lock<std::mutex> lock(ready_mutex);
        ready_cv.wait(lock, [this] { return ready; });
        result = status;
        return success;
    }
    void Stop() override
    {
        if(stop) SetEvent(stop);
        if(worker.joinable()) worker.join();
        if(stop) { CloseHandle(stop); stop = nullptr; }
        key = {}; removed = {};
    }
};
#else
class WindowsKeyboardBackend final : public KeyboardInputBackend
{
public:
    bool Start(Key, Removed, std::string& result) override { result = "Physical keyboard effects require Windows"; return false; }
    void Stop() override {}
};
#endif
}

double KeyboardInputService::SteadyTime()
{ return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::shared_ptr<KeyboardInputService> KeyboardInputService::SharedInstance()
{
    static std::mutex mutex;
    static std::weak_ptr<KeyboardInputService> instance;
    std::lock_guard<std::mutex> lock(mutex);
    auto service = instance.lock();
    if(!service)
    {
        service = std::make_shared<KeyboardInputService>(std::make_unique<WindowsKeyboardBackend>());
        instance = service;
    }
    return service;
}
KeyboardInputService::KeyboardInputService(std::unique_ptr<KeyboardInputBackend> backend, std::function<double()> clock)
    : state(std::make_unique<State>())
{ state->backend = std::move(backend); state->clock = std::move(clock); }
KeyboardInputService::~KeyboardInputService()
{ state->backend->Stop(); }
std::uint64_t KeyboardInputService::Acquire()
{
    std::lock_guard<std::mutex> operation(state->lifecycle);
    bool first;
    std::uint64_t token;
    {
        std::lock_guard<std::mutex> lock(state->events_mutex);
        if(state->listeners.size() >= 256) return 0;
        first = state->listeners.empty();
        token = ++state->next_token;
        state->listeners.emplace(token, state->sequence);
    }
    if(first && !state->backend->Start([this](const std::string& path, std::uint16_t scan, bool up, double time) { state->Key(path, scan, up, time); },
                                      [this](const std::string& path) { state->Removed(path); }, state->status))
    {
        state->backend->Stop();
        std::lock_guard<std::mutex> lock(state->events_mutex);
        state->listeners.erase(token); state->events.clear(); state->held.clear();
        return 0;
    }
    return token;
}
void KeyboardInputService::Release(std::uint64_t token)
{
    std::lock_guard<std::mutex> operation(state->lifecycle);
    bool last;
    {
        std::lock_guard<std::mutex> lock(state->events_mutex);
        if(!state->listeners.erase(token)) return;
        last = state->listeners.empty();
    }
    if(last)
    {
        state->backend->Stop();
        std::lock_guard<std::mutex> lock(state->events_mutex);
        state->events.clear(); state->held.clear(); state->status = "Inactive";
    }
}
std::vector<KeyboardEvent> KeyboardInputService::Read(std::uint64_t token)
{
    std::lock_guard<std::mutex> lock(state->events_mutex);
    const auto listener = state->listeners.find(token);
    if(listener == state->listeners.end()) return {};
    const double now = state->clock();
    state->Prune(now);
    std::vector<KeyboardEvent> result;
    for(const auto& event : state->events)
        if(event.sequence > listener->second && event.time <= now) result.push_back(event);
    listener->second = state->sequence;
    return result;
}
std::string KeyboardInputService::Status() const
{ std::lock_guard<std::mutex> lock(state->lifecycle); return state->status; }
}
