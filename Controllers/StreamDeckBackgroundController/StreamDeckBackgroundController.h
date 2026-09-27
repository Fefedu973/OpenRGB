/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

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

namespace streamdeck_background
{
constexpr unsigned int WIDTH = 80;
constexpr unsigned int HEIGHT = 50;
constexpr unsigned int LED_COUNT = WIDTH * HEIGHT;
constexpr unsigned int FRAME_BYTES = 15 * 72 * 72 * 4;

struct Options
{
    std::string session_file;
    unsigned int fps = 20;
    std::string surface_channel; // empty: compatibility LED matrix; nonempty: native image input
    unsigned int surface_stale_ms = 1000;
    std::string transport = "bridge"; // native: optional in-process Frida Core DLL, no Python/HTTP
    std::string native_library;
    std::string native_lock_directory; // same observer lock directory as an existing Python bridge
};

/* Disabled unless explicitly enabled; never embeds the session credential. */
bool ParseOptions(const nlohmann::json& settings, Options& options);

struct Layout
{
    std::vector<std::pair<int, int>> positions;
};

Layout ParseLayout(const nlohmann::json& value);
/* RGB row-major 80x50 -> full 480x272 canvas -> 15 opaque BGRA tiles. */
std::string EncodeTiles(const std::vector<unsigned char>& rgb, const Layout& layout);
std::string EncodeSurfaceTiles(const std::vector<unsigned char>& bgra, std::uint32_t width,
                              std::uint32_t height, std::uint32_t stride, const Layout& layout);
std::string EncodeNativeTiles(const room_image::Frame& frame, const room_image::Mapping& mapping, const Layout& layout);

struct Status
{
    std::string state = "idle";
    std::string error;
    std::uint64_t submitted = 0;
    std::uint64_t accepted = 0;
    std::uint64_t failures = 0;
    std::uint64_t surface_sequence = 0;
    std::uint32_t surface_width = 0, surface_height = 0;
    std::string input = "none";
    bool stop_requested = false;
    nlohmann::json compositor; // Bounded aggregate diagnostics, never frame pixels.
};

nlohmann::json AggregateCompositorStatus(const nlohmann::json& value);

class Controller : public room_image::RGBControllerImageInterface
{
public:
    using Reporter = std::function<void(const std::string&)>;
    using StatusReporter = std::function<void(const Status&)>;
    explicit Controller(const Options& options, Reporter reporter = {}, StatusReporter status_reporter = {});
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    void Submit(std::vector<unsigned char> rgb);
    bool GetImageOutput(unsigned zone, room_image::Output& output) const override;
    bool GetImagePreview(unsigned zone, std::shared_ptr<const room_image::Frame>& frame,
                         room_image::Mapping& mapping) const override;
    room_image::SubmitResult SubmitImage(unsigned zone, std::shared_ptr<const room_image::Frame> frame,
                                         const room_image::Mapping& mapping, unsigned lease_ms = 1000) override;
    void Stop();
    Status GetStatus() const;

private:
    void Run();
    void RunSurface();
    void Failure(const std::string& reason);
    void UpdateCompositorStatus(const nlohmann::json& value);

    Options options;
    Reporter reporter;
    StatusReporter status_reporter;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<unsigned char> latest;
    std::shared_ptr<const room_image::Frame> latest_image;
    room_image::Mapping latest_mapping;
    std::chrono::steady_clock::time_point image_deadline{};
    std::uint64_t led_generation = 0, image_generation = 0;
    Status status;
    bool stopping = false;
    std::thread worker;
};
}
