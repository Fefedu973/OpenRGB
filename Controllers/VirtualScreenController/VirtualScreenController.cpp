/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "VirtualScreenController.h"
#include "../../FrameSurface/FrameSurface.h"
#include <set>
#include <stdexcept>

namespace virtual_screen
{
namespace
{
bool Admit(const std::shared_ptr<const std::vector<std::uint8_t>>& pixels)
{
    // Shared scene frames count once even when many outputs retain them. Distinct
    // incoming allocations retained by these outputs are bounded to64MiB in total.
    static std::mutex budget_mutex;
    static std::vector<std::weak_ptr<const std::vector<std::uint8_t>>> retained;
    std::unique_lock<std::mutex> guard(budget_mutex,std::try_to_lock);
    if(!guard.owns_lock()) return false;
    std::size_t bytes = 0;
    for(auto item = retained.begin(); item != retained.end();)
    {
        if(auto current = item->lock())
        {
            if(current.get() == pixels.get()) return true;
            bytes += current->size(); ++item;
        }
        else item = retained.erase(item);
    }
    if(retained.size() >= 256 || pixels->size() > room_image::MaxFrameBytes - std::min(bytes,room_image::MaxFrameBytes)) return false;
    retained.push_back(pixels);
    return true;
}
}

bool ValidOptions(const Options& o)
{
    return room_surface::ValidChannel(o.id) && room_surface::ValidChannel(o.channel)
        && !o.name.empty() && o.name.size() <= 80
        && std::none_of(o.name.begin(),o.name.end(),[](unsigned char c){return c < 32 || c == 127;})
        && o.width && o.height && o.width <= 4096 && o.height <= 4096
        && std::uint64_t(o.width)*o.height*4 <= room_image::MaxFrameBytes
        && o.fps >= 1 && o.fps <= 60
        && o.compatibility_width >= 2 && o.compatibility_width <= 127
        && o.compatibility_height >= 2 && o.compatibility_height <= 127
        && std::uint64_t(o.compatibility_width)*o.compatibility_height <= 16381;
}

std::vector<Options> ParseOptions(const nlohmann::json& settings)
{
    std::vector<Options> result;
    if(!settings.is_object() || !settings.value("enabled",false)) return result;
    const auto& outputs = settings.at("outputs");
    if(!outputs.is_array() || outputs.size() > 16) throw std::invalid_argument("At most16 virtual screens are allowed");
    std::set<std::string> ids, channels;
    std::uint64_t bytes = 0;
    for(const auto& item : outputs)
    {
        if(!item.value("enabled",true)) continue;
        Options o;
        o.id = item.at("id").get<std::string>();
        o.name = item.value("name",std::string("Virtual Screen"));
        o.channel = item.at("channel").get<std::string>();
        auto number = [&](const char* key, unsigned fallback)
        {
            if(!item.contains(key)) return fallback;
            if(!item[key].is_number_integer()) throw std::invalid_argument("Virtual screen dimensions/FPS must be integers");
            const auto value = item[key].get<std::int64_t>();
            if(value < 1 || value > 4096) throw std::invalid_argument("Virtual screen dimension/FPS outside bounds");
            return unsigned(value);
        };
        o.width = number("width",800); o.height = number("height",600); o.fps = number("fps",30);
        o.compatibility_width = number("compatibility_width",32); o.compatibility_height = number("compatibility_height",18);
        bytes += std::uint64_t(o.width)*o.height*4;
        if(!ValidOptions(o) || bytes > room_image::MaxFrameBytes || !ids.insert(o.id).second || !channels.insert(o.channel).second)
            throw std::invalid_argument("Invalid/duplicate virtual screen or aggregate64MiB frame budget exceeded");
        result.push_back(std::move(o));
    }
    return result;
}

Controller::Controller(const Options& value) : options(value)
{
    if(!ValidOptions(options)) throw std::invalid_argument("Invalid virtual screen options");
    worker = std::thread(&Controller::Run,this);
}
Controller::~Controller() { Stop(); }

void Controller::Stop()
{
    {
        std::lock_guard<std::mutex> guard(mutex);
        stopping.store(true,std::memory_order_relaxed);
    }
    changed.notify_all();
    if(worker.joinable()) worker.join();
    std::lock_guard<std::mutex> guard(mutex);
    native.reset(); compatibility.reset(); status.stopped = true; status.state = "stopped";
}

bool Controller::GetImageOutput(unsigned zone,room_image::Output& output) const
{
    if(zone != 0) return false;
    output = {0,options.width,options.height,options.fps}; return true;
}

room_image::SubmitResult Controller::SubmitImage(unsigned zone,std::shared_ptr<const room_image::Frame> frame,
                                                const room_image::Mapping& mapping,unsigned lease_ms)
{
    if(zone != 0) return room_image::SubmitResult::Unsupported;
    if(!frame || !frame->Valid() || !mapping.Valid() || lease_ms < 100 || lease_ms > 5000)
        return room_image::SubmitResult::Invalid;
    std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);
    if(!guard.owns_lock() || stopping.load(std::memory_order_relaxed) || !Admit(frame->pixels)) return room_image::SubmitResult::Busy;
    const bool same = native == frame && native_mapping.origin_x == mapping.origin_x && native_mapping.origin_y == mapping.origin_y
        && native_mapping.u_x == mapping.u_x && native_mapping.u_y == mapping.u_y
        && native_mapping.v_x == mapping.v_x && native_mapping.v_y == mapping.v_y && native_mapping.brightness == mapping.brightness;
    if(native && native != frame) ++status.replaced;
    native = std::move(frame); native_mapping = mapping;
    lease_deadline = Clock::now() + std::chrono::milliseconds(lease_ms);
    if(!same) ++revision;
    ++status.submitted;
    changed.notify_one();
    return room_image::SubmitResult::Accepted;
}

void Controller::SubmitLEDs(const std::vector<std::uint32_t>& colors)
{
    if(colors.size() != std::size_t(options.compatibility_width)*options.compatibility_height) return;
    std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);
    if(!guard.owns_lock() || stopping.load(std::memory_order_relaxed) || (native && Clock::now() < lease_deadline)) return;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(colors.size()*4);
    for(std::size_t i = 0; i < colors.size(); ++i)
    {
        // OpenRGB's RGBColor packs R in its low byte.
        (*pixels)[4*i] = (colors[i] >> 16) & 255; (*pixels)[4*i+1] = (colors[i] >> 8) & 255;
        (*pixels)[4*i+2] = colors[i] & 255; (*pixels)[4*i+3] = 255;
    }
    if(!Admit(pixels)) return;
    auto frame = std::make_shared<room_image::Frame>();
    frame->width = options.compatibility_width; frame->height = options.compatibility_height; frame->stride = frame->width*4;
    frame->sequence = ++revision; frame->pixels = std::move(pixels); compatibility = std::move(frame);
    ++status.submitted; changed.notify_one();
}

bool Controller::GetImagePreview(unsigned zone,std::shared_ptr<const room_image::Frame>& frame,room_image::Mapping& mapping) const
{
    if(zone != 0) return false;
    std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);
    if(!guard.owns_lock() || stopping.load(std::memory_order_relaxed)) return false;
    if(native && Clock::now() < lease_deadline) { frame = native; mapping = native_mapping; return true; }
    if(compatibility) { frame = compatibility; mapping = {}; return true; }
    return false;
}
Status Controller::GetStatus() const { std::lock_guard<std::mutex> guard(mutex); return status; }

void Controller::Run()
{
    try
    {
        std::vector<std::uint8_t> output(std::size_t(options.width)*options.height*4);
        std::unique_ptr<room_surface::Publisher> publisher;
        auto next = Clock::now(), retry = Clock::now(), heartbeat = Clock::now();
        std::uint64_t rendered = 0;
        bool clear = false;
        while(!stopping.load(std::memory_order_relaxed))
        {
            std::shared_ptr<const room_image::Frame> input;
            room_image::Mapping mapping;
            std::uint64_t generation;
            bool selected_native;
            Clock::time_point deadline;
            {
                std::unique_lock<std::mutex> guard(mutex);
                if(stopping.load(std::memory_order_relaxed)) break;
                const auto now = Clock::now();
                if(native && now >= lease_deadline) { native.reset(); ++revision; clear = true; }
                input = native ? native : compatibility;
                selected_native = bool(native); deadline = lease_deadline;
                mapping = native ? native_mapping : room_image::Mapping{};
                generation = revision;
                if(!input && !clear) { status.state = "idle"; changed.wait(guard); continue; }
                const auto due = generation == rendered ? heartbeat : next;
                if(now < due)
                {
                    changed.wait_until(guard,selected_native ? std::min(due,deadline) : due);
                    continue;
                }
            }
            for(unsigned y = 0; y < options.height && !stopping.load(std::memory_order_relaxed); ++y)
                for(unsigned x = 0; x < options.width; ++x)
                {
                    const auto pixel = input ? room_image::SampleBGRA(*input,mapping,(x+0.5)/options.width,(y+0.5)/options.height) : 0xff000000u;
                    const auto offset = (std::size_t(y)*options.width+x)*4;
                    for(unsigned c = 0; c < 4; ++c) output[offset+c] = (pixel >> (c*8)) & 255;
                }
            if(stopping.load(std::memory_order_relaxed)) break;
            {
                std::lock_guard<std::mutex> guard(mutex);
                if((selected_native && Clock::now() >= deadline) || (!selected_native && native && Clock::now() < lease_deadline)) continue;
            }
            const auto now = Clock::now();
            bool published = false;
            std::string error;
#ifdef _WIN32
            if(!publisher && now >= retry)
            {
                publisher.reset(new room_surface::Publisher(options.channel,std::uint64_t(options.width)*options.height*4));
                if(!publisher->IsOpen()) { error = publisher->LastError(); publisher.reset(); retry = now + std::chrono::seconds(1); }
            }
            if(publisher)
            {
                published = publisher->PublishBGRA(output.data(),output.size(),options.width,options.height,options.width*4);
                if(!published) error = publisher->LastError();
            }
#else
            error = "In-process preview only; FrameSurface output requires Windows";
#endif
            {
                std::lock_guard<std::mutex> guard(mutex);
                status.state = published ? "publishing" : "unavailable";
                if(!error.empty() || published) status.error = error;
                if(published) ++status.published;
            }
            rendered = generation; clear = false;
            next = now + std::chrono::microseconds(1000000/options.fps);
            heartbeat = now + std::chrono::milliseconds(500);
        }
    }
    catch(const std::exception&)
    {
        std::lock_guard<std::mutex> guard(mutex); status.state = "error"; status.error = "Virtual screen worker failed";
        stopping.store(true,std::memory_order_relaxed);
    }
}
}
