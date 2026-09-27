// SPDX-License-Identifier: GPL-2.0-or-later
#include "Input/KeyboardInputService.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace room_input;
unsigned checks = 0;
void Check(bool value) { ++checks; if(!value) { std::cerr << "Assertion " << checks << " failed\n"; std::abort(); } }
struct Mock final : KeyboardInputBackend
{
    Key key; Removed remove;
    unsigned starts = 0, stops = 0;
    bool available = true;
    bool Start(Key k, Removed r, std::string& status) override
    { ++starts; key = k; remove = r; status = available ? "Active mock" : "Registration conflict"; return available; }
    void Stop() override { ++stops; key = {}; remove = {}; }
    void Down(const std::string& path, std::uint16_t scan, double time) { key(path, scan, false, time); }
    void Up(const std::string& path, std::uint16_t scan, double time) { key(path, scan, true, time); }
};
int main()
{
    double now = 10;
    auto backend = std::make_unique<Mock>(); auto* mock = backend.get();
    KeyboardInputService service(std::move(backend), [&] { return now; });
    Check(mock->starts == 0);
    auto a = service.Acquire(); Check(a != 0 && mock->starts == 1);
    mock->Down("synthetic-A", 30, now);
    mock->Down("synthetic-A", 30, now); // held key is not an onset
    auto b = service.Acquire(); Check(b != a && mock->starts == 1);
    Check(service.Read(b).empty()); // no pre-acquisition backlog
    auto events = service.Read(a); Check(events.size() == 1 && events[0].scan == 30);
    Check(service.Read(a).empty());
    mock->Down("synthetic-B", 30, now); // independent keyboard, same physical scan
    mock->Down("synthetic-A", 0xe01c, now);
    events = service.Read(a); Check(events.size() == 2 && events[1].scan == 0xe01c);
    Check(service.Read(b).size() == 2); // independent cursors
    mock->Up("synthetic-A", 30, now); mock->Down("synthetic-A", 30, now);
    Check(service.Read(a).size() == 1);
    mock->remove("synthetic-A");
    Check(service.Read(b).empty()); // removal also drops queued events for that device
    mock->Down("synthetic-A", 30, now); Check(service.Read(a).size() == 1);
    mock->Down("synthetic-A", 31, now);
    now += .251; Check(service.Read(a).empty());
    for(unsigned i = 0; i < 100; ++i) mock->Down("synthetic-C", static_cast<std::uint16_t>(i + 1), now);
    events = service.Read(a); Check(events.size() == 64 && events.front().scan == 37 && events.back().scan == 100);
    mock->remove(""); // resume/reset
    mock->Down("synthetic-A", 30, now); Check(service.Read(a).size() == 1);
    mock->Down("", 30, now); mock->Down("synthetic-A", 0, now);
    Check(service.Read(a).empty());
    service.Release(a); Check(mock->stops == 0);
    service.Release(a); Check(mock->stops == 0);
    service.Release(b); Check(mock->stops == 1 && service.Status() == "Inactive");
    Check(service.Read(a).empty());
    mock->available = false;
    Check(service.Acquire() == 0 && service.Status() == "Registration conflict");
    mock->available = true;
    a = service.Acquire(); Check(a != 0 && service.Read(a).empty());
    service.Release(a);
    // Concurrent effects share exactly one backend lifecycle.
    const unsigned initial_starts = mock->starts, initial_stops = mock->stops;
    std::uint64_t tokens[16]{}; std::vector<std::thread> workers;
    for(unsigned i = 0; i < 16; ++i) workers.emplace_back([&, i] { tokens[i] = service.Acquire(); });
    for(auto& worker : workers) worker.join(); workers.clear();
    Check(mock->starts == initial_starts + 1);
    for(auto token : tokens) Check(token != 0);
    for(auto token : tokens) workers.emplace_back([&, token] { service.Release(token); });
    for(auto& worker : workers) worker.join();
    Check(mock->stops == initial_stops + 1);
    // Buffer is safe when producer and independent readers overlap.
    a = service.Acquire(); b = service.Acquire();
    std::thread producer([&] { for(unsigned i=0;i<10000;++i) { mock->Down("synthetic-thread", 32, now); mock->Up("synthetic-thread", 32, now); } });
    std::thread reader([&] { for(unsigned i=0;i<10000;++i) { const auto batch=service.Read(a); if(batch.size()>64) std::abort(); } });
    producer.join(); reader.join();
    Check(service.Read(b).size() <= 64);
    service.Release(a); service.Release(b);
    std::cout << checks << " keyboard input assertions passed (synthetic backend only)\n";
}
