// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

// A checkpoint must not capture an intermediate plugin state, including a
// load that starts and finishes while a plugin's save callback is running.
class ProfileLoadState
{
public:
    class Scope
    {
    public:
        explicit Scope(ProfileLoadState& owner) : state(owner) { state.Begin(); }
        ~Scope() { state.End(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        ProfileLoadState& state;
    };

    bool Busy() const { return depth.load() != 0; }
    uint64_t Revision() const { return revision.load(); }
    void BeginRemote() { if(!remote.exchange(true)) Begin(); }
    void EndRemote() { if(remote.exchange(false)) End(); }

private:
    void Begin() { ++depth; ++revision; }
    void End() { ++revision; --depth; }
    std::atomic<unsigned> depth{0};
    std::atomic<uint64_t> revision{0};
    std::atomic<bool> remote{false};
};
