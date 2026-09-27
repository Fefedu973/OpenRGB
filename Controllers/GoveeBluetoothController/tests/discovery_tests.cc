/* SPDX-License-Identifier: GPL-2.0-or-later
 * Offline passive-discovery tests. No Windows/Bluetooth access. */
#include "GoveeBluetoothDiscovery.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
using namespace GoveeBluetooth;
using namespace std::chrono_literals;

struct FakeRadio
{
    std::mutex mutex;
    std::condition_variable changed;
    PassiveDiscovery::Observation callback;
    std::atomic<unsigned int> starts{0}, stops{0};
    std::atomic<int> error{-1};
    struct Lease : PassiveDiscovery::Scan
    {
        FakeRadio& radio;
        explicit Lease(FakeRadio& radio) : radio(radio) {}
        ~Lease() override { ++radio.stops; }
        std::optional<int> Error() const override
        {
            const int code = radio.error.load();
            return code < 0 ? std::nullopt : std::optional<int>{code};
        }
    };
    std::unique_ptr<PassiveDiscovery::Scan> Start(PassiveDiscovery::Observation observe)
    {
        { std::lock_guard<std::mutex> lock(mutex); callback = std::move(observe); ++starts; }
        changed.notify_all();
        return std::make_unique<Lease>(*this);
    }
    PassiveDiscovery::Start Factory()
    {
        return [this](PassiveDiscovery::Observation observe) { return Start(std::move(observe)); };
    }
    void WaitStarted(unsigned int count = 1)
    {
        std::unique_lock<std::mutex> lock(mutex);
        assert(changed.wait_for(lock, 2s, [&] { return starts >= count; }));
    }
    void Emit(uint64_t address, uint8_t type)
    {
        PassiveDiscovery::Observation copy;
        { std::lock_guard<std::mutex> lock(mutex); copy = callback; }
        copy(address, type);
    }
};

int main()
{
    unsigned int tests = 0;
    auto pass = [&](const char* name) { ++tests; std::cout << "PASS " << name << '\n'; };
    const auto keep_running = [] { return false; };
    {
        PassiveDiscovery discovery(100ms, 250ms); discovery.Register(42);
        FakeRadio radio; unsigned int opens = 0;
        const auto device = OpenConfiguredDevice(discovery, 42, [&](std::optional<uint8_t> type)
        {
            ++opens; assert(!type); return std::make_shared<int>(7);
        }, radio.Factory(), keep_running);
        assert(device && opens == 1 && radio.starts == 0);
        pass("warm Windows cache bypasses all discovery");
    }
    {
        PassiveDiscovery discovery(200ms, 400ms); discovery.Register(42);
        FakeRadio radio; unsigned int opens = 0; std::shared_ptr<int> device;
        std::thread caller([&]
        {
            device = OpenConfiguredDevice(discovery, 42, [&](std::optional<uint8_t> type)
            {
                ++opens;
                if(!type) return std::shared_ptr<int>{}; // Cold FromBluetoothAddressAsync=null.
                assert(*type == 1); return std::make_shared<int>(42);
            }, radio.Factory(), keep_running);
        });
        radio.WaitStarted(); radio.Emit(42, 1); caller.join();
        assert(device && *device == 42 && opens == 2 && radio.starts == 1 && radio.stops == 1);
        pass("cold null plus exact random-address advertisement triggers one typed retry");
    }
    {
        PassiveDiscovery discovery(60ms, 200ms); discovery.Register(42);
        FakeRadio radio; unsigned int opens = 0; bool rejected = false;
        std::thread caller([&]
        {
            try { OpenConfiguredDevice(discovery, 42, [&](std::optional<uint8_t>) { ++opens; return std::shared_ptr<int>{}; }, radio.Factory(), keep_running); }
            catch(const std::runtime_error&) { rejected = true; }
        });
        radio.WaitStarted(); radio.Emit(999, 0); radio.Emit(42, 9); caller.join();
        assert(rejected && opens == 1 && radio.stops == 1);
        pass("unknown target and invalid address type never cause a typed open");
    }
    {
        PassiveDiscovery discovery(250ms, 500ms); discovery.Register(42); discovery.Register(43);
        FakeRadio radio;
        PassiveDiscovery::Result a{PassiveDiscovery::Status::Failed}, b{PassiveDiscovery::Status::Failed};
        std::thread first([&] { a = discovery.Discover(42, radio.Factory(), keep_running); });
        radio.WaitStarted();
        std::thread second([&] { b = discovery.Discover(43, radio.Factory(), keep_running); });
        radio.Emit(42, 0); radio.Emit(43, 1); first.join(); second.join();
        assert(a.status == PassiveDiscovery::Status::Found && a.type == 0);
        assert(b.status == PassiveDiscovery::Status::Found && b.type == 1);
        assert(radio.starts == 1 && radio.stops == 1);
        pass("two concurrent workers share one scan and retain each exact address type");
    }
    {
        PassiveDiscovery discovery(1s, 1200ms); discovery.Register(42);
        FakeRadio radio; std::atomic<bool> cancel{false};
        PassiveDiscovery::Result result{PassiveDiscovery::Status::Failed};
        std::thread caller([&] { result = discovery.Discover(42, radio.Factory(), [&] { return cancel.load(); }); });
        radio.WaitStarted(); const auto then = PassiveDiscovery::Clock::now(); cancel = true; caller.join();
        assert(result.status == PassiveDiscovery::Status::Cancelled && radio.stops == 1);
        assert(PassiveDiscovery::Clock::now() - then < 200ms); // Includes OS thread scheduling.
        pass("active watcher cancellation stops promptly without waiting for its scan deadline");
    }
    {
        PassiveDiscovery discovery(1s, 1200ms); discovery.Register(42); discovery.Register(43);
        FakeRadio radio; std::atomic<bool> cancel_owner{false}, cancel_waiter{false};
        std::thread owner([&] { discovery.Discover(42, radio.Factory(), [&] { return cancel_owner.load(); }); });
        radio.WaitStarted();
        PassiveDiscovery::Result result{PassiveDiscovery::Status::Failed};
        std::thread waiter([&] { result = discovery.Discover(43, radio.Factory(), [&] { return cancel_waiter.load(); }); });
        cancel_waiter = true; waiter.join();
        assert(result.status == PassiveDiscovery::Status::Cancelled && radio.starts == 1);
        assert(radio.stops == 0); // One caller cannot stop another caller's watcher.
        cancel_owner = true; owner.join(); assert(radio.stops == 1);
        pass("a waiting worker can cancel without cancelling the shared scan owner");
    }
    {
        PassiveDiscovery discovery(1s, 60ms); discovery.Register(42);
        FakeRadio radio; const auto then = PassiveDiscovery::Clock::now();
        const auto result = discovery.Discover(42, radio.Factory(), keep_running);
        const auto elapsed = PassiveDiscovery::Clock::now() - then;
        assert(result.status == PassiveDiscovery::Status::NotObserved && radio.stops == 1);
        assert(elapsed >= 50ms && elapsed < 400ms);
        pass("global request deadline bounds a longer scan and closes its watcher");
    }
    {
        PassiveDiscovery discovery(35ms, 100ms, 0ms); discovery.Register(42);
        FakeRadio radio;
        discovery.Discover(42, radio.Factory(), keep_running);
        const auto stale = radio.callback;
        PassiveDiscovery::Result result{PassiveDiscovery::Status::Failed};
        std::thread caller([&] { result = discovery.Discover(42, radio.Factory(), keep_running); });
        radio.WaitStarted(2); stale(42, 0); caller.join();
        assert(result.status == PassiveDiscovery::Status::NotObserved && radio.stops == 2);
        discovery.Unregister(42);
        radio.Emit(42, 0); // Late callback has shared state, never a destroyed transport.
        pass("late callbacks from stopped or replaced generations cannot populate a new scan");
    }
    {
        PassiveDiscovery discovery(100ms, 200ms); discovery.Register(42);
        FakeRadio radio; radio.error = 2;
        const auto result = discovery.Discover(42, radio.Factory(), keep_running);
        assert(result.status == PassiveDiscovery::Status::Failed && result.error == 2 && radio.stops == 1);
        pass("radio failure is reported explicitly and stops discovery");
    }
    {
        FakeRadio radio; PassiveDiscovery::Observation stale;
        {
            PassiveDiscovery discovery(1ms, 100ms); discovery.Register(42);
            discovery.Discover(42, radio.Factory(), keep_running); stale = radio.callback;
        }
        stale(42, 0); // Callback state outlives its coordinator safely and is inactive.
        assert(radio.stops == 1);
        pass("late callback after coordinator destruction is harmless");
    }
    std::cout << tests << " passive discovery tests passed\n";
}
