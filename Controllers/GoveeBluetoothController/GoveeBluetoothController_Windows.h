/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "GoveeBluetoothProtocol.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace GoveeBluetooth
{
struct Configuration
{
    Profile profile = Profile::H6008;
    uint64_t address = 0;
    uint64_t wifi_mac = 0;
    Key key{};
    std::string name;
    std::string serial;
    bool power_on_acquire = false;
};

class Controller
{
public:
    explicit Controller(Configuration configuration);
    ~Controller();
    void Submit(Frame frame);
    std::string Name() const { return configuration.name; }
    std::string Serial() const { return configuration.serial; }
    std::string Model() const { return configuration.profile == Profile::H6008 ? "H6008" : "H6159"; }
private:
    void Run();
    Configuration configuration;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::atomic<bool> stopping{false};
    Frame latest{};
    bool have_frame = false;
};
}
