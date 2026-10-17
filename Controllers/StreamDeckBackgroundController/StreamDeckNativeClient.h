/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <chrono>
#include <functional>

namespace streamdeck_background {
/* Optional runtime dependency. The ordinary OpenRGB build does not link Frida.
 * Construction, requests and destruction belong to the one controller worker. */
class NativeClient {
public:
    using Clock = std::chrono::steady_clock;
    using Now = std::function<Clock::time_point()>;
    NativeClient(const std::string& library,const std::string& lock_directory,unsigned fps,Now now = {});
    ~NativeClient();
    nlohmann::json Request(const char* path,const std::string& body);
    std::string Close(); // Empty iff restoration and detach completed successfully.
    nlohmann::json LastDiagnostics() const; // Cached status; never reconnects or writes.
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
