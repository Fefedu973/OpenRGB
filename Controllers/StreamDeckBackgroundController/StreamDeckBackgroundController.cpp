/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StreamDeckBackgroundController.h"
#include "StreamDeckNativeClient.h"
#include "httplib.h"
#include "../../FrameSurface/FrameSurface.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace streamdeck_background
{
namespace
{
using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

struct Session
{
    int port = 0;
    std::string token;
    std::shared_ptr<NativeClient> native;
    bool operator==(const Session& other) const { return port == other.port && token == other.token && native == other.native; }
};

Session ReadSession(const std::string& path)
{
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if(size < 2 || size > 8192) throw std::runtime_error("Session file unavailable or invalid");
    std::string content(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if(!input.read(&content[0], content.size())) throw std::runtime_error("Session file unavailable or invalid");
    json value = json::parse(content, nullptr, false);
    if(!value.is_object() || !value.contains("http_port") || !value["http_port"].is_number_integer()
       || !value.contains("token") || !value["token"].is_string())
        throw std::runtime_error("Session file unavailable or invalid");
    const auto port = value["http_port"].get<std::int64_t>();
    const std::string token = value["token"].get<std::string>();
    if(port < 1024 || port > 65535 || token.size() < 32 || token.size() > 128
       || !std::all_of(token.begin(), token.end(), [](unsigned char c)
          { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }))
        throw std::runtime_error("Session file unavailable or invalid");
    return {static_cast<int>(port), token, {}};
}

Session ReadSession(const Options& options,std::shared_ptr<NativeClient>& native)
{
    if(options.transport!="native")return ReadSession(options.session_file);
    if(!native)native=std::make_shared<NativeClient>(options.native_library,options.native_lock_directory,options.fps);
    return {0,{},native};
}

json Request(const Session& session, const char* method, const char* path, const std::string& body = {})
{
    if(session.native)return session.native->Request(path,body);
    /* Literal loopback, no environment proxy, redirects, cookies or remote URL. */
    httplib::Client client("127.0.0.1", session.port);
    client.set_connection_timeout(0, 400000);
    client.set_read_timeout(0, 400000);
    client.set_write_timeout(0, 400000);
    client.set_follow_location(false);
    client.set_keep_alive(false);
    const auto deadline = Clock::now() + std::chrono::milliseconds(1200);
    std::string reply;
    httplib::Request request;
    request.method = method;
    request.path = path;
    request.headers.emplace("Authorization", "Bearer " + session.token);
    request.headers.emplace("X-Lease-Ms", "2000");
    request.headers.emplace("Content-Type", "application/octet-stream");
    request.body = body;
    request.content_receiver = [&](const char* data, std::size_t length, std::uint64_t, std::uint64_t)
    {
        if(reply.size() + length > 16384 || Clock::now() > deadline) return false;
        reply.append(data, length);
        return true;
    };
    const auto result = client.send(request);
    if(!result || Clock::now() > deadline) throw std::runtime_error("Local bridge request failed or timed out");
    if(result->status != 200) throw std::runtime_error("Local bridge HTTP " + std::to_string(result->status));
    json value = json::parse(reply, nullptr, false);
    if(!value.is_object()) throw std::runtime_error("Invalid local bridge response");
    return value;
}
}

bool ParseOptions(const nlohmann::json& settings, Options& options)
{
    if(settings.is_null() || (settings.is_object() && !settings.contains("enabled"))) return false;
    if(!settings.is_object() || !settings["enabled"].is_boolean()) throw std::runtime_error("enabled must be a boolean");
    if(!settings["enabled"].get<bool>()) return false;
    options.transport="bridge";
    if(settings.contains("transport")) {
        if(!settings["transport"].is_string())throw std::runtime_error("transport must be bridge or native");
        options.transport=settings["transport"].get<std::string>();
    }
    if(options.transport!="bridge" && options.transport!="native")throw std::runtime_error("transport must be bridge or native");
    const auto absolute=[&](const char* key) {
        if(!settings.contains(key) || !settings[key].is_string())throw std::runtime_error(std::string("An absolute ")+key+" is required");
        const auto value=settings[key].get<std::string>();
        if(value.empty() || value.find_first_of("\r\n")!=std::string::npos || !std::filesystem::u8path(value).is_absolute())
            throw std::runtime_error(std::string("An absolute ")+key+" is required");
        return value;
    };
    if(options.transport=="native") {
        options.native_library=absolute("native_library");
        options.native_lock_directory=absolute("native_lock_directory");
    }else options.session_file=absolute("session_file");
    if(settings.contains("fps"))
    {
        if(!settings["fps"].is_number_integer()) throw std::runtime_error("fps must be an integer from 1 to 20");
        const auto fps = settings["fps"].get<std::int64_t>();
        if(fps < 1 || fps > 20) throw std::runtime_error("fps must be an integer from 1 to 20");
        options.fps = static_cast<unsigned int>(fps);
    }
    options.surface_channel.clear();
    options.surface_stale_ms = 1000;
    if(settings.contains("frame_surface"))
    {
        const auto& surface = settings["frame_surface"];
        if(!surface.is_object() || !surface.contains("channel") || !surface["channel"].is_string()
           || !room_surface::ValidChannel(surface["channel"].get<std::string>()))
            throw std::runtime_error("frame_surface.channel must be 1-64 letters, digits, underscores or hyphens");
        options.surface_channel = surface["channel"].get<std::string>();
        if(surface.contains("stale_ms"))
        {
            if(!surface["stale_ms"].is_number_integer()) throw std::runtime_error("frame_surface.stale_ms must be 100-2000");
            const auto ttl = surface["stale_ms"].get<std::int64_t>();
            if(ttl < 100 || ttl > 2000) throw std::runtime_error("frame_surface.stale_ms must be 100-2000");
            options.surface_stale_ms = static_cast<unsigned int>(ttl);
        }
    }
    return true;
}

Layout ParseLayout(const nlohmann::json& value)
{
    for(const auto& item : std::vector<std::pair<const char*, int>>{{"width",480},{"height",272},{"tileWidth",72},{"tileHeight",72}})
        if(!value.contains(item.first) || !value[item.first].is_number_integer() || value[item.first] != item.second)
            throw std::runtime_error("Unsupported native Stream Deck layout");
    if(!value.contains("positions") || !value["positions"].is_array() || value["positions"].size() != 15)
        throw std::runtime_error("Unsupported native Stream Deck layout");
    Layout layout;
    std::set<std::pair<int,int>> unique;
    for(const auto& point : value["positions"])
    {
        if(!point.is_array() || point.size() != 2 || !point[0].is_number_integer() || !point[1].is_number_integer())
            throw std::runtime_error("Invalid native tile positions");
        const auto x = point[0].get<std::int64_t>(), y = point[1].get<std::int64_t>();
        if(x < 0 || x > 408 || y < 0 || y > 200 || !unique.emplace(static_cast<int>(x),static_cast<int>(y)).second)
            throw std::runtime_error("Invalid native tile positions");
        layout.positions.emplace_back(static_cast<int>(x),static_cast<int>(y));
    }
    return layout;
}

static std::string EncodeImage(const std::vector<unsigned char>& pixels, std::uint32_t width,
                               std::uint32_t height, std::uint32_t stride, unsigned int channels,
                               const Layout& layout)
{
    if(!room_surface::ValidImage(width,height,stride,channels,pixels.size(),room_surface::MAX_CAPACITY)
       || layout.positions.size() != 15) throw std::runtime_error("Invalid canvas size");
    std::string tiles(FRAME_BYTES, '\0');
    std::size_t output = 0;
    for(const auto& origin : layout.positions)
    {
        if(origin.first < 0 || origin.first > 408 || origin.second < 0 || origin.second > 200)
            throw std::runtime_error("Invalid native tile positions");
        for(int y = 0; y < 72; ++y)
        {
            const double sy = std::max(0.0, std::min(height - 1.0, (origin.second + y + 0.5) * height / 272.0 - 0.5));
            const auto y0 = static_cast<unsigned int>(sy), y1 = std::min(y0 + 1, height - 1);
            const double fy = sy - y0;
            for(int x = 0; x < 72; ++x)
            {
                const double sx = std::max(0.0, std::min(width - 1.0, (origin.first + x + 0.5) * width / 480.0 - 0.5));
                const auto x0 = static_cast<unsigned int>(sx), x1 = std::min(x0 + 1, width - 1);
                const double fx = sx - x0;
                for(unsigned int output_channel = 0; output_channel < 3; ++output_channel)
                {
                    const auto channel = channels == 3 ? 2-output_channel : output_channel;
                    const double top = pixels[std::size_t(y0)*stride+std::size_t(x0)*channels+channel] * (1-fx)
                                     + pixels[std::size_t(y0)*stride+std::size_t(x1)*channels+channel] * fx;
                    const double bottom = pixels[std::size_t(y1)*stride+std::size_t(x0)*channels+channel] * (1-fx)
                                        + pixels[std::size_t(y1)*stride+std::size_t(x1)*channels+channel] * fx;
                    tiles[output++] = static_cast<char>(std::lround(top * (1-fy) + bottom * fy));
                }
                tiles[output++] = static_cast<char>(255);
            }
        }
    }
    return tiles;
}

std::string EncodeTiles(const std::vector<unsigned char>& rgb, const Layout& layout)
{
    if(rgb.size() != LED_COUNT*3) throw std::runtime_error("Invalid canvas size");
    return EncodeImage(rgb,WIDTH,HEIGHT,WIDTH*3,3,layout);
}

std::string EncodeSurfaceTiles(const std::vector<unsigned char>& bgra, std::uint32_t width,
                              std::uint32_t height, std::uint32_t stride, const Layout& layout)
{
    room_image::Frame frame;frame.width=width;frame.height=height;frame.stride=stride;
    // Synchronous encoder only: this borrowed wrapper never enters SubmitImage or a queue.
    frame.pixels=std::shared_ptr<const std::vector<unsigned char>>(&bgra,[](const std::vector<unsigned char>*){});
    return EncodeNativeTiles(frame,{},layout);
}

std::string EncodeNativeTiles(const room_image::Frame& frame,const room_image::Mapping& mapping,const Layout& layout)
{
    if(!frame.Valid() || !mapping.Valid() || layout.positions.size()!=15)
        throw std::runtime_error("Invalid native image or mapping");
    std::string tiles(FRAME_BYTES,'\0');std::size_t offset=0;
    for(const auto& origin:layout.positions)
    {
        if(origin.first<0 || origin.first>408 || origin.second<0 || origin.second>200)
            throw std::runtime_error("Invalid native tile positions");
        for(unsigned y=0;y<72;++y) for(unsigned x=0;x<72;++x)
        {
            const auto pixel=room_image::SampleBGRA(frame,mapping,(origin.first+x+0.5)/480.0,(origin.second+y+0.5)/272.0);
            for(unsigned channel=0;channel<4;++channel)tiles[offset++]=static_cast<char>((pixel>>(channel*8))&255);
        }
    }
    return tiles;
}

nlohmann::json AggregateCompositorStatus(const nlohmann::json& value)
{
    json result=json::object();
    if(!value.is_object()) return result;
    const auto copy_scalar=[](const json& source,json& destination,const char* key)
    {
        const auto item=source.find(key);
        if(item!=source.end() && (item->is_number() || item->is_boolean() || item->is_null())) destination[key]=*item;
    };
    for(const char* key:{"ready","armed","restorationPending","errors","compositions","paints","queued","acknowledged","restores","rejectedCandidates","pending","uptimeMs"}) copy_scalar(value,result,key);
    if(value.contains("lastFrameCoverage") && value["lastFrameCoverage"].is_object())
        for(const char* key:{"sequence","restore","rendered","injected","empty","actions"}) copy_scalar(value["lastFrameCoverage"],result["lastFrameCoverage"],key);
    if(value.contains("pacing") && value["pacing"].is_object())
    {
        for(const char* key:{"maxFps","dispatchIntervalMs","windowSeconds","received","coalesced","queuedFps5s","ackFps5s"}) copy_scalar(value["pacing"],result["pacing"],key);
        for(const char* name:{"ackLatencyMs","inputToAckMs"})
            if(value["pacing"].contains(name) && value["pacing"][name].is_object())
                for(const char* key:{"samples","mean","p50","p95","max"}) copy_scalar(value["pacing"][name],result["pacing"][name],key);
    }
    if(value.contains("lastFault") && value["lastFault"].is_object() && value["lastFault"].contains("message") && value["lastFault"]["message"].is_string())
        result["fault"]=value["lastFault"]["message"].get<std::string>().substr(0,512);
    return result;
}

void Controller::UpdateCompositorStatus(const nlohmann::json& value)
{
    auto aggregate=AggregateCompositorStatus(value);
    if(aggregate.empty()) return;
    aggregate["sampledSteadyMs"]=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
    Status snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex);
        status.compositor=std::move(aggregate);
        snapshot=status;
    }
    // No transport mutex across the controller's metadata/AccessMutex callback.
    if(status_reporter) status_reporter(snapshot);
}

Controller::Controller(const Options& config, Reporter logger, StatusReporter status_callback)
    : options(config), reporter(std::move(logger)), status_reporter(std::move(status_callback))
{
    if(options.fps < 1 || options.fps > 20) throw std::runtime_error("Invalid Stream Deck frame rate");
    if(!options.surface_channel.empty() && (!room_surface::ValidChannel(options.surface_channel)
       || options.surface_stale_ms < 100 || options.surface_stale_ms > 2000))
        throw std::runtime_error("Invalid frame surface configuration");
    worker = std::thread(&Controller::Run, this);
}

Controller::~Controller() { Stop(); }

void Controller::Submit(std::vector<unsigned char> rgb)
{
    if(!options.surface_channel.empty()) return; // Native images never fall back to LED colours.
    if(rgb.size() != LED_COUNT * 3) throw std::runtime_error("Invalid Stream Deck RGB frame");
    std::lock_guard<std::mutex> lock(mutex);
    if(stopping) return;
    latest = std::move(rgb);
    ++led_generation;
    ++status.submitted;
    changed.notify_one();
}

bool Controller::GetImageOutput(unsigned zone,room_image::Output& output) const
{
    if(zone!=0 || !options.surface_channel.empty()) return false;
    output={0,480,272,options.fps};return true;
}

bool Controller::GetImagePreview(unsigned zone,std::shared_ptr<const room_image::Frame>& frame,
                                 room_image::Mapping& mapping) const
{
    if(zone!=0 || !options.surface_channel.empty())return false;
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || stopping || !latest_image || Clock::now()>=image_deadline)return false;
    frame=latest_image;mapping=latest_mapping;return true;
}

room_image::SubmitResult Controller::SubmitImage(unsigned zone,std::shared_ptr<const room_image::Frame> frame,
                                                const room_image::Mapping& mapping,unsigned lease_ms)
{
    if(zone!=0 || !options.surface_channel.empty())return room_image::SubmitResult::Unsupported;
    if(!frame || !frame->Valid() || !mapping.Valid() || lease_ms<100 || lease_ms>5000)
        return room_image::SubmitResult::Invalid;
    std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);
    if(!lock.owns_lock() || stopping)return room_image::SubmitResult::Busy;
    latest_image=std::move(frame);latest_mapping=mapping;
    image_deadline=Clock::now()+std::chrono::milliseconds(lease_ms);
    ++image_generation;++status.submitted;changed.notify_one();
    return room_image::SubmitResult::Accepted;
}

Status Controller::GetStatus() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}

void Controller::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        changed.notify_all();
    }
    if(worker.joinable()) worker.join();
}

void Controller::Failure(const std::string& reason)
{
    bool report;
    {
        std::lock_guard<std::mutex> lock(mutex);
        report = status.error != reason;
        status.state = "waiting";
        status.error = reason;
        ++status.failures;
    }
    /* The reason is generated locally; never forward a server body or credential. */
    if(report && reporter) reporter(reason);
}

void Controller::Run()
{
    if(!options.surface_channel.empty()) { RunSurface();return; }
    std::shared_ptr<NativeClient> native;
    Session ready_session, attempted_session;
    Layout layout;
    bool ready = false, attempted_any = false, sent_native = false;
    std::uint64_t sent_generation = 0;
    auto last_sent = Clock::time_point::min();
    auto next_attempt = Clock::now();
    auto next_diagnostics = Clock::time_point::min();
    const auto interval = std::chrono::milliseconds((1000 + options.fps - 1) / options.fps);
    struct Pending
    {
        std::vector<unsigned char> rgb;
        std::shared_ptr<const room_image::Frame> image;
        room_image::Mapping mapping;
        Clock::time_point deadline{};
        std::uint64_t generation=0;
    };
    const auto select=[&](Pending& pending) // Caller holds mutex; at most one frame reference, no image copy.
    {
        if(latest_image && Clock::now()>=image_deadline)latest_image.reset();
        if(latest_image)
        {
            pending.image=latest_image;pending.mapping=latest_mapping;
            pending.deadline=image_deadline;pending.generation=image_generation;
        }
        else if(!latest.empty()) { pending.rgb=latest;pending.generation=led_generation; }
        else return false;
        return true;
    };
    const auto release=[&]()
    {
        if(attempted_any)
        {
            attempted_any=false;
            try
            {
                Request(attempted_session,"POST","/stop");
                std::lock_guard<std::mutex> lock(mutex);status.stop_requested=true;
            }
            catch(const std::exception&)
            { Failure("Normal-background request failed; the 2-second lease remains the fallback"); }
        }
        ready=false;sent_generation=0;last_sent=Clock::time_point::min();
    };
    while(true)
    {
        Pending pending;
        bool have_input=false;
        {
            std::unique_lock<std::mutex> lock(mutex);
            while(!stopping)
            {
                pending=Pending{};
                have_input=select(pending);
                if(!have_input)
                {
                    if(attempted_any)break; // Release expired native ownership before waiting for input.
                    status.state="idle";status.input="none";
                    changed.wait(lock,[&]{return stopping || latest_image || !latest.empty();});
                    continue;
                }
                auto wake_at=next_attempt;
                if(sent_native==bool(pending.image) && sent_generation==pending.generation && last_sent!=Clock::time_point::min())
                    wake_at=std::max(wake_at,last_sent+std::chrono::milliseconds(500));
                if(pending.image)wake_at=std::min(wake_at,pending.deadline);
                if(Clock::now()>=wake_at)break;
                changed.wait_until(lock,wake_at);
            }
            if(stopping)break;
            status.state = ready ? "streaming" : "connecting";
        }
        if(!have_input) { release();continue; }
        try
        {
            const Session session = ReadSession(options,native);
            if(!ready || !(session == ready_session))
            {
                ready = false;
                const auto health = Request(session, "GET", "/health");
                if(session.native) { UpdateCompositorStatus(health); next_diagnostics=Clock::now()+std::chrono::seconds(2); }
                if(!health.contains("ready") || health["ready"] != true) throw std::runtime_error("Waiting for native Stream Deck target");
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if(stopping) break;
                }
                layout = ParseLayout(Request(session, "GET", "/layout"));
                ready_session = session;
                ready = true;
            }
            if(session.native && Clock::now()>=next_diagnostics)
            {
                next_diagnostics=Clock::now()+std::chrono::seconds(2);
                UpdateCompositorStatus(Request(session,"GET","/health"));
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(stopping) break;
                /* A slow reconnect must not replay the frame captured before it. */
                pending=Pending{};have_input=select(pending);
            }
            if(!have_input) { release();continue; }
            const auto tiles = pending.image ? EncodeNativeTiles(*pending.image,pending.mapping,layout) : EncodeTiles(pending.rgb,layout);
            {
                std::lock_guard<std::mutex> lock(mutex);
                if(stopping)break;
                // Discard a matrix prepared before a native takeover, or an image
                // that expired while being encoded. An HTTP request already in
                // flight remains bounded and completes before the next frame.
                const bool native_active=latest_image && Clock::now()<image_deadline;
                if((!pending.image && native_active) || (pending.image && Clock::now()>=pending.deadline))continue;
            }
            /* The server may apply a frame before its HTTP reply is lost. */
            attempted_session = session;
            attempted_any = true;
            const auto response = Request(session, "POST", "/frame", tiles);
            if(!response.contains("accepted") || response["accepted"] != true) throw std::runtime_error("Local bridge did not accept the frame");
            last_sent = Clock::now();
            next_attempt = last_sent + interval;
            sent_generation = pending.generation;sent_native=bool(pending.image);
            std::lock_guard<std::mutex> lock(mutex);
            ++status.accepted;
            status.state = "streaming";
            status.input = pending.image ? "native" : "led";
            if(pending.image)
            {
                status.surface_sequence=pending.image->sequence;
                status.surface_width=pending.image->width;status.surface_height=pending.image->height;
            }
            status.error.clear();
            status.stop_requested=false;
        }
        catch(const std::exception& error)
        {
            ready = false;
            Failure(error.what());
            if(native) UpdateCompositorStatus(native->LastDiagnostics());
            next_attempt = Clock::now() + std::chrono::seconds(1);
        }
    }
    release();
    if(native) {const auto error=native->Close();if(!error.empty())Failure(error);}
    std::lock_guard<std::mutex> lock(mutex);
    status.state = "stopped";
}

void Controller::RunSurface()
{
    std::shared_ptr<NativeClient> native;
    room_surface::Reader reader(options.surface_channel);
    room_surface::Frame frame;
    Session ready_session, attempted_session;
    Layout layout;
    bool ready=false, attempted_any=false;
    std::uint64_t sent_sequence=0, sent_generation=0;
    auto last_sent=Clock::time_point::min();
    auto next_attempt=Clock::now();
    auto next_diagnostics=Clock::time_point::min();
    const auto interval=std::chrono::milliseconds((1000+options.fps-1)/options.fps);
    const auto stopped=[&]() { std::lock_guard<std::mutex> lock(mutex);return stopping; };
    const auto release=[&]()
    {
        if(attempted_any)
        {
            // At most one bounded release attempt; expiry remains the fallback on failure.
            attempted_any=false;
            try
            {
                Request(attempted_session,"POST","/stop");
                std::lock_guard<std::mutex> lock(mutex);status.stop_requested=true;
            }
            catch(const std::exception&)
            { Failure("Normal-background request failed; the 2-second lease remains the fallback"); }
        }
        ready=false;sent_sequence=sent_generation=0;last_sent=Clock::time_point::min();
    };
    const auto read=[&]()
    {
        const auto result=reader.ReadLatest(frame,options.surface_stale_ms);
        if(result==room_surface::FrameStatus::NewFrame)
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++status.submitted;status.surface_sequence=frame.sequence;
            status.surface_width=frame.width;status.surface_height=frame.height;
        }
        return result;
    };
    while(true)
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_until(lock,next_attempt,[&]{return stopping;});
            if(stopping) break;
        }
        next_attempt=Clock::now()+interval;
        const auto result=read();
        if(result==room_surface::FrameStatus::Busy) continue; // No stale frame replay during contention.
        if(result!=room_surface::FrameStatus::NewFrame && result!=room_surface::FrameStatus::Unchanged)
        {
            release();
            std::lock_guard<std::mutex> lock(mutex);
            status.state="waiting_surface";status.error=reader.LastError();
            continue;
        }
        if(frame.sequence==sent_sequence && frame.generation==sent_generation
           && Clock::now()<last_sent+std::chrono::milliseconds(500)) continue;
        try
        {
            const Session session=ReadSession(options,native);
            if(!ready || !(session==ready_session))
            {
                ready=false;
                { std::lock_guard<std::mutex> lock(mutex);status.state="connecting"; }
                const auto health=Request(session,"GET","/health");
                if(session.native) { UpdateCompositorStatus(health); next_diagnostics=Clock::now()+std::chrono::seconds(2); }
                if(!health.contains("ready") || health["ready"]!=true)
                    throw std::runtime_error("Waiting for native Stream Deck target");
                if(stopped()) break;
                layout=ParseLayout(Request(session,"GET","/layout"));
                ready_session=session;ready=true;
            }
            if(session.native && Clock::now()>=next_diagnostics)
            {
                next_diagnostics=Clock::now()+std::chrono::seconds(2);
                UpdateCompositorStatus(Request(session,"GET","/health"));
            }
            if(stopped()) break;
            // Refresh after HTTP handshake: it may have consumed the frame's whole TTL.
            const auto refreshed=read();
            if(refreshed!=room_surface::FrameStatus::NewFrame && refreshed!=room_surface::FrameStatus::Unchanged)
            { if(refreshed!=room_surface::FrameStatus::Busy) release();continue; }
            const auto tiles=EncodeSurfaceTiles(frame.bgra,frame.width,frame.height,frame.stride,layout);
            if(stopped()) break;
            attempted_session=session;attempted_any=true;
            const auto response=Request(session,"POST","/frame",tiles);
            if(!response.contains("accepted") || response["accepted"]!=true)
                throw std::runtime_error("Local bridge did not accept the frame");
            last_sent=Clock::now();next_attempt=last_sent+interval;
            sent_sequence=frame.sequence;sent_generation=frame.generation;
            std::lock_guard<std::mutex> lock(mutex);
            ++status.accepted;status.state="streaming";status.error.clear();status.stop_requested=false;
        }
        catch(const std::exception& error)
        {
            ready=false;Failure(error.what());next_attempt=Clock::now()+std::chrono::seconds(1);
            if(native) UpdateCompositorStatus(native->LastDiagnostics());
        }
    }
    release();
    if(native) {const auto error=native->Close();if(!error.empty())Failure(error);}
    std::lock_guard<std::mutex> lock(mutex);status.state="stopped";
}
}
