/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace GoveeBluetooth
{
// Passive observations are only a way to populate the OS cache. They never
// authenticate a device or authorize a GATT write.
class PassiveDiscovery
{
public:
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;
    using Cancel = std::function<bool()>;
    using Observation = std::function<void(uint64_t, uint8_t)>; // 0=public, 1=random.
    struct Scan
    {
        virtual ~Scan() = default; // Stop and revoke callbacks, even on cancellation.
        virtual std::optional<int> Error() const = 0;
    };
    using Start = std::function<std::unique_ptr<Scan>(Observation)>;
    enum class Status { Found, NotObserved, Cancelled, Failed };
    struct Result { Status status; uint8_t type = 0; int error = 0; };

    explicit PassiveDiscovery(Milliseconds scan_time = Milliseconds(5000),
                              Milliseconds request_time = Milliseconds(6000),
                              Milliseconds reuse_time = Milliseconds(5000)) :
        state(std::make_shared<State>()), scan_time(scan_time), request_time(request_time), reuse_time(reuse_time) {}

    void Register(uint64_t address)
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->targets.count(address) && state->targets.size() >= 16)
            throw std::runtime_error("Too many configured BLE discovery targets");
        auto& target = state->targets[address];
        ++target.owners;
        if(state->active_generation) target.considered = state->active_generation;
    }
    void Unregister(uint64_t address)
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto found = state->targets.find(address);
        if(found != state->targets.end() && --found->second.owners == 0) state->targets.erase(found);
        state->changed.notify_all();
    }

    Result Discover(uint64_t address, const Start& start, const Cancel& cancelled)
    {
        const auto deadline = Clock::now() + request_time;
        // One calling worker owns the bounded watcher. Other workers share its
        // result; no detached thread or background scan survives this method.
        while(!scan_gate.try_lock_for(Milliseconds(25)))
        {
            if(cancelled()) return {Status::Cancelled};
            if(Clock::now() >= deadline) return {Status::NotObserved};
        }
        std::unique_lock<std::timed_mutex> scan_lock(scan_gate, std::adopt_lock);
        if(cancelled()) return {Status::Cancelled};
        if(Clock::now() >= deadline) return {Status::NotObserved};
        uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto target = state->targets.find(address);
            if(target == state->targets.end()) throw std::invalid_argument("BLE discovery target is not configured");
            if(state->completed_generation && target->second.considered == state->completed_generation &&
               Clock::now() - state->completed_at < reuse_time)
                return Lookup(*state, address, state->completed_generation, state->completed_error);
            generation = ++state->generation;
            state->active_generation = generation;
            for(auto& target_entry : state->targets)
            {
                target_entry.second.considered = generation;
                target_entry.second.observed = 0;
            }
        }
        auto shared = state;
        const Observation observe = [shared, generation](uint64_t observed_address, uint8_t type)
        {
            if(type > 1) return;
            std::lock_guard<std::mutex> lock(shared->mutex);
            if(shared->active_generation != generation) return; // Late callback after Stop/replacement.
            auto target = shared->targets.find(observed_address);
            if(target == shared->targets.end()) return; // Do not retain nearby unknown devices.
            target->second.type = type;
            target->second.observed = generation;
            shared->changed.notify_all();
        };
        std::unique_ptr<Scan> scan;
        bool was_cancelled = false;
        std::optional<int> error;
        try
        {
            scan = start(observe);
            if(!scan) throw std::runtime_error("BLE passive discovery did not start");
            const auto scan_deadline = std::min(deadline, Clock::now() + scan_time);
            while(true)
            {
                if(cancelled()) { was_cancelled = true; break; }
                error = scan->Error();
                if(error) break;
                std::unique_lock<std::mutex> lock(state->mutex);
                bool all_seen = !state->targets.empty();
                for(const auto& item : state->targets) all_seen &= item.second.observed == generation;
                if(all_seen || Clock::now() >= scan_deadline) break;
                state->changed.wait_until(lock, std::min(scan_deadline, Clock::now() + Milliseconds(25)));
            }
        }
        catch(...)
        {
            { std::lock_guard<std::mutex> lock(state->mutex); state->active_generation = 0; }
            scan.reset();
            throw;
        }
        Result result;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->active_generation = 0; // Revoke logical access before stopping the native watcher.
            if(was_cancelled) result = {Status::Cancelled};
            else
            {
                state->completed_generation = generation;
                state->completed_at = Clock::now();
                state->completed_error = error;
                result = Lookup(*state, address, generation, error);
            }
        }
        scan.reset();
        return result;
    }

private:
    struct Target { unsigned int owners = 0; uint64_t considered = 0, observed = 0; uint8_t type = 0; };
    struct State
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::map<uint64_t, Target> targets;
        uint64_t generation = 0, active_generation = 0, completed_generation = 0;
        Clock::time_point completed_at{};
        std::optional<int> completed_error;
    };
    static Result Lookup(const State& shared, uint64_t address, uint64_t generation, std::optional<int> error)
    {
        if(error) return {Status::Failed, 0, *error};
        const auto found = shared.targets.find(address);
        if(found != shared.targets.end() && found->second.observed == generation)
            return {Status::Found, found->second.type};
        return {Status::NotObserved};
    }
    std::shared_ptr<State> state;
    std::timed_mutex scan_gate;
    const Milliseconds scan_time, request_time, reuse_time;
};

// Production and tests use the same cold-cache flow. The factory is invoked
// first without an address type, then only for the configured address and the
// type from an exact matching advertisement. No guessed public/random retry.
template<class DeviceFactory>
auto OpenConfiguredDevice(PassiveDiscovery& discovery, uint64_t address, DeviceFactory&& open,
                          const PassiveDiscovery::Start& scan, const PassiveDiscovery::Cancel& cancelled)
{
    if(cancelled()) throw std::runtime_error("Govee BLE device discovery cancelled");
    auto device = open(std::optional<uint8_t>{});
    if(device) return device;
    const auto result = discovery.Discover(address, scan, cancelled);
    if(result.status == PassiveDiscovery::Status::Cancelled || cancelled())
        throw std::runtime_error("Govee BLE device discovery cancelled");
    if(result.status == PassiveDiscovery::Status::Failed)
        throw std::runtime_error("Govee BLE passive discovery stopped: error=" + std::to_string(result.error));
    if(result.status != PassiveDiscovery::Status::Found)
        throw std::runtime_error("Configured Govee BLE device was not observed during bounded passive discovery");
    device = open(std::optional<uint8_t>{result.type});
    if(!device) throw std::runtime_error("Configured Govee BLE device remains unavailable after passive discovery");
    return device;
}
}
