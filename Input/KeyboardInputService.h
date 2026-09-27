// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../FrameRouting/OpenRGBInputPluginAPI.h"
#include <functional>
#include <memory>

namespace room_input
{
// Backend boundary is injectable for tests: no real input registration needed.
class KeyboardInputBackend
{
public:
    using Key = std::function<void(const std::string&, std::uint16_t, bool, double)>;
    using Removed = std::function<void(const std::string&)>;
    virtual ~KeyboardInputBackend() = default;
    virtual bool Start(Key key, Removed removed, std::string& status) = 0;
    virtual void Stop() = 0; // joins: no callbacks after return
};

class KeyboardInputService
{
public:
    explicit KeyboardInputService(std::unique_ptr<KeyboardInputBackend> backend,
                                  std::function<double()> clock = SteadyTime);
    ~KeyboardInputService();
    KeyboardInputService(const KeyboardInputService&) = delete;
    KeyboardInputService& operator=(const KeyboardInputService&) = delete;
    static std::shared_ptr<KeyboardInputService> SharedInstance();
    static double SteadyTime();
    std::uint64_t Acquire();
    void Release(std::uint64_t token);
    std::vector<KeyboardEvent> Read(std::uint64_t token);
    std::string Status() const;
private:
    struct State;
    std::unique_ptr<State> state;
};
}
