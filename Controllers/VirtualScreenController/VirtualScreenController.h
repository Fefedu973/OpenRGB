/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "../../FrameRouting/RGBControllerImageInterface.h"

class RGBController;
// Safe detector: reads only the explicitly enabled local VirtualScreens settings.
std::vector<RGBController*> DetectVirtualScreenControllers();

namespace virtual_screen
{
struct Options
{
    std::string id, name, channel;
    unsigned width = 800, height = 600, fps = 30;
    unsigned compatibility_width = 32, compatibility_height = 18;
};
std::vector<Options> ParseOptions(const nlohmann::json& settings);
bool ValidOptions(const Options& options);

struct Status
{
    std::string state = "idle", error;
    std::uint64_t submitted = 0, published = 0, replaced = 0;
    bool stopped = false;
};

// Image-capable reference output. No USB, Bluetooth, desktop capture or networking.
// Windows publishes an optional user-local FrameSurface. Other platforms retain
// the same in-process image/preview interface without claiming shared-memory output.
class Controller : public room_image::RGBControllerImageInterface
{
public:
    explicit Controller(const Options& options);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    bool GetImageOutput(unsigned zone, room_image::Output& output) const override;
    bool GetImagePreview(unsigned zone, std::shared_ptr<const room_image::Frame>& frame,
                         room_image::Mapping& mapping) const override;
    room_image::SubmitResult SubmitImage(unsigned zone, std::shared_ptr<const room_image::Frame> frame,
                                         const room_image::Mapping& mapping, unsigned lease_ms = 1000) override;
    void SubmitLEDs(const std::vector<std::uint32_t>& colors);
    void Stop();
    Status GetStatus() const;
private:
    using Clock = std::chrono::steady_clock;
    void Run();
    Options options;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::atomic<bool> stopping{false};
    std::thread worker;
    std::shared_ptr<const room_image::Frame> native, compatibility;
    room_image::Mapping native_mapping;
    Clock::time_point lease_deadline{};
    std::uint64_t revision = 0;
    Status status;
};
}
