/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace streamdeck_background {
/* Optional runtime dependency. The ordinary OpenRGB build does not link Frida.
 * Construction, requests and destruction belong to the one controller worker. */
class NativeClient {
public:
    NativeClient(const std::string& library,const std::string& lock_directory,unsigned fps);
    ~NativeClient();
    nlohmann::json Request(const char* path,const std::string& body);
    std::string Close(); // Empty iff restoration and detach completed successfully.
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
