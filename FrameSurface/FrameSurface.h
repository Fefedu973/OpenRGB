/* SPDX-License-Identifier: GPL-2.0-or-later
 * Versioned, bounded CPU image transport. Independent of OpenRGB's LED/SDK ABI.
 * One producer, latest frame only; consumers choose their own polling cadence.
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#endif

namespace room_surface
{
constexpr std::uint32_t VERSION = 1;
constexpr std::uint32_t BGRA8_OPAQUE_SRGB = 1;
constexpr std::uint64_t MAX_CAPACITY = 64ULL * 1024 * 1024;
constexpr std::uint32_t HEADER_BYTES = 128;
constexpr std::uint32_t OWNER_HAS_LIFETIME_EVENT = 1;

struct alignas(8) Header
{
    char magic[8];                         // ORGBFRM1, no terminator
    std::uint32_t version;
    std::uint32_t header_bytes;
    std::uint64_t capacity;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride;
    std::uint32_t format;
    std::uint64_t payload_bytes;
    std::uint64_t sequence;
    std::uint64_t timestamp_ms;            // Windows GetTickCount64 uptime
    std::uint64_t generation;
    std::uint32_t owner_pid;
    std::uint32_t owner_flags;
    std::uint64_t owner_start_ticks;       // process creation FILETIME
    std::uint8_t reserved[40];
};
static_assert(sizeof(Header) == HEADER_BYTES, "FrameSurface header ABI mismatch");
static_assert(offsetof(Header, sequence) == 48, "FrameSurface field alignment mismatch");

enum class FrameStatus { NewFrame, Unchanged, Stale, Unavailable, Invalid, Busy };

struct Frame
{
    std::uint32_t width = 0, height = 0, stride = 0;
    std::uint64_t sequence = 0, timestamp_ms = 0, generation = 0;
    std::vector<std::uint8_t> bgra;
};

inline bool ValidChannel(const std::string& channel)
{
    return !channel.empty() && channel.size() <= 64 && std::all_of(channel.begin(), channel.end(), [](unsigned char c)
    { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

inline bool ValidImage(std::uint32_t width, std::uint32_t height, std::uint32_t stride,
                       std::uint32_t channels, std::uint64_t bytes, std::uint64_t capacity)
{
    if(width == 0 || height == 0 || (channels != 3 && channels != 4) || capacity == 0 || capacity > MAX_CAPACITY) return false;
    const std::uint64_t row = std::uint64_t(width) * channels;
    const std::uint64_t total = std::uint64_t(stride) * height;
    return row <= std::numeric_limits<std::uint32_t>::max() && stride >= row
           && total <= capacity && total <= bytes;
}

inline bool ValidHeader(const Header& header)
{
    return std::memcmp(header.magic, "ORGBFRM1", 8) == 0 && header.version == VERSION
        && header.header_bytes == HEADER_BYTES && header.format == BGRA8_OPAQUE_SRGB
        && header.generation != 0 && header.sequence != 0
        && ValidImage(header.width, header.height, header.stride, 4, header.payload_bytes, header.capacity)
        && header.payload_bytes == std::uint64_t(header.stride) * header.height;
}

#ifdef _WIN32
namespace detail
{
inline std::wstring Name(const std::string& channel, bool mutex)
{
    return std::wstring(L"Local\\OpenRGB-Room.Surface.") + std::wstring(channel.begin(),channel.end()) + (mutex ? L".Mutex" : L"");
}

inline std::wstring OwnerName(const std::string& channel, std::uint64_t generation)
{
    return Name(channel,false) + L".Owner." + std::to_wstring(generation);
}

inline bool LifetimeExists(const Header& header, const std::string& channel)
{
    if(!(header.owner_flags & OWNER_HAS_LIFETIME_EVENT)) return true; // Earlier v1 process-only owner.
    HANDLE lifetime=OpenEventW(SYNCHRONIZE,FALSE,OwnerName(channel,header.generation).c_str());
    if(lifetime) { CloseHandle(lifetime);return true; }
    // Only confirmed absence permits takeover; access failures remain conservative.
    return GetLastError()!=ERROR_FILE_NOT_FOUND;
}

/* Explicit user-only DACL, no global namespace, privilege or administrator requirement. */
class Security
{
public:
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,FALSE};
    Security()
    {
        HANDLE token = nullptr;
        if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
        DWORD length = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &length);
        if(length == 0 || length > 65536) { CloseHandle(token);return; }
        std::vector<std::uint8_t> buffer(length);
        const bool queried = GetTokenInformation(token,TokenUser,buffer.data(),length,&length) != FALSE;
        CloseHandle(token);
        if(!queried) return;
        LPWSTR sid = nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&sid)) return;
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
        LocalFree(sid);
        ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&attributes.lpSecurityDescriptor,nullptr);
    }
    ~Security() { if(attributes.lpSecurityDescriptor) LocalFree(attributes.lpSecurityDescriptor); }
    bool Valid() const { return attributes.lpSecurityDescriptor != nullptr; }
};

class Lock
{
public:
    bool acquired = false;
    explicit Lock(HANDLE value, DWORD timeout) : handle(value)
    {
        const DWORD result = value ? WaitForSingleObject(value,timeout) : WAIT_FAILED;
        acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    ~Lock() { if(acquired) ReleaseMutex(handle); }
private:
    HANDLE handle;
};

inline std::uint64_t ProcessStart(HANDLE process)
{
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(process,&creation,&exit,&kernel,&user)) return 0;
    return (std::uint64_t(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}

inline bool OwnerAlive(const Header& header, const std::string& channel)
{
    if(!header.owner_pid || !LifetimeExists(header,channel)) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,FALSE,header.owner_pid);
    if(!process) return GetLastError() != ERROR_INVALID_PARAMETER; // refuse takeover if access cannot establish death
    const bool alive = WaitForSingleObject(process,0) == WAIT_TIMEOUT;
    const auto start = ProcessStart(process);
    CloseHandle(process);
    return alive && (start == 0 || start == header.owner_start_ticks);
}

inline bool ValidCapacityHeader(const Header& header)
{
    return std::memcmp(header.magic,"ORGBFRM1",8) == 0 && header.version == VERSION
        && header.header_bytes == HEADER_BYTES && header.capacity > 0 && header.capacity <= MAX_CAPACITY;
}
}
#endif

class Publisher
{
public:
    explicit Publisher(const std::string& channel, std::uint64_t requested_capacity = MAX_CAPACITY)
    {
        if(!ValidChannel(channel) || requested_capacity == 0 || requested_capacity > MAX_CAPACITY)
        { error = "Invalid channel or capacity";return; }
#ifdef _WIN32
        detail::Security security;
        if(!security.Valid()) { error = "Cannot build user-only security descriptor";return; }
        mutex_handle = CreateMutexW(&security.attributes,FALSE,detail::Name(channel,true).c_str());
        if(!mutex_handle) { error = "Cannot open surface mutex";return; }
        detail::Lock lock(mutex_handle,100);
        if(!lock.acquired) { error = "Surface mutex busy";return; }
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,&security.attributes,PAGE_READWRITE,0,
                                     static_cast<DWORD>(HEADER_BYTES + requested_capacity),detail::Name(channel,false).c_str());
        const DWORD create_error = GetLastError();
        if(!mapping) { error = "Cannot create surface mapping";return; }
        const bool existed = create_error == ERROR_ALREADY_EXISTS;
        void* prefix = MapViewOfFile(mapping,FILE_MAP_READ,0,0,HEADER_BYTES);
        if(!prefix) { error = "Cannot map surface header";return; }
        Header previous{};std::memcpy(&previous,prefix,HEADER_BYTES);UnmapViewOfFile(prefix);
        if(existed)
        {
            if(!detail::ValidCapacityHeader(previous)) { error = "Existing surface header is invalid";return; }
            if(detail::OwnerAlive(previous,channel)) { error = "Surface already has a live publisher";return; }
            if(previous.capacity < requested_capacity) { error = "Existing reader mapping is smaller; close readers before resizing";return; }
            capacity = previous.capacity;
        }
        else capacity = requested_capacity;
        view = static_cast<std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,static_cast<SIZE_T>(HEADER_BYTES+capacity)));
        if(!view) { error = "Cannot map bounded surface storage";return; }
        static std::atomic<std::uint64_t> nonce{0};
        for(unsigned int attempt=0;attempt<4 && !lifetime_event;++attempt)
        {
            LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);
            generation=std::uint64_t(counter.QuadPart) ^ (std::uint64_t(GetCurrentProcessId()) << 32) ^ ++nonce;
            if(!generation) generation=1;
            lifetime_event=CreateEventW(&security.attributes,TRUE,FALSE,detail::OwnerName(channel,generation).c_str());
            if(lifetime_event && GetLastError()==ERROR_ALREADY_EXISTS)
            { CloseHandle(lifetime_event);lifetime_event=nullptr; }
        }
        if(!lifetime_event) { error="Cannot create publisher lifetime marker";return; }
        Header header{};std::memcpy(header.magic,"ORGBFRM1",8);
        header.version=VERSION;header.header_bytes=HEADER_BYTES;header.capacity=capacity;
        header.format=BGRA8_OPAQUE_SRGB;header.generation=generation;header.owner_pid=GetCurrentProcessId();
        header.owner_flags=OWNER_HAS_LIFETIME_EVENT;
        header.owner_start_ticks=detail::ProcessStart(GetCurrentProcess());
        if(!header.owner_start_ticks) { error = "Cannot identify publisher process";return; }
        std::memcpy(view,&header,HEADER_BYTES);
        open=true;
#else
        error = "FrameSurface shared memory is currently Windows-only";
#endif
    }
    ~Publisher() { Close(); }
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;
    bool IsOpen() const { return open; }
    std::string LastError() const { return error; }

    bool PublishBGRA(const std::uint8_t* data, std::size_t bytes, std::uint32_t width,
                     std::uint32_t height, std::uint32_t stride)
    {
        std::lock_guard<std::mutex> local(local_mutex);
        if(!open || !data || !ValidImage(width,height,stride,4,bytes,capacity))
        { error = "Invalid or oversized BGRA frame";return false; }
        // Explicitly opaque, unpremultiplied BGRA8; never reinterpret arbitrary Qt formats.
        for(std::uint32_t y=0;y<height;++y) for(std::uint32_t x=0;x<width;++x)
            if(data[std::size_t(y)*stride + std::size_t(x)*4 + 3] != 255)
            { error = "BGRA8 surface requires alpha 255";return false; }
#ifdef _WIN32
        detail::Lock lock(mutex_handle,5);
        if(!lock.acquired) { error = "Surface mutex busy; frame dropped";return false; }
        Header header{};std::memcpy(&header,view,HEADER_BYTES);
        if(header.generation != generation || header.owner_pid != GetCurrentProcessId())
        { error = "Publisher ownership changed";return false; }
        const auto payload = std::uint64_t(stride)*height;
        auto* pixels = view + HEADER_BYTES;
        std::memset(pixels,0,static_cast<std::size_t>(payload));
        for(std::uint32_t y=0;y<height;++y)
            std::memcpy(pixels+std::size_t(y)*stride,data+std::size_t(y)*stride,std::size_t(width)*4);
        header.width=width;header.height=height;header.stride=stride;header.payload_bytes=payload;
        header.sequence=++sequence;header.timestamp_ms=GetTickCount64();
        std::memcpy(view,&header,HEADER_BYTES);
        error.clear();return true;
#else
        return false;
#endif
    }

    bool PublishRGB(const std::uint8_t* data, std::size_t bytes, std::uint32_t width,
                    std::uint32_t height, std::uint32_t stride)
    {
        const std::uint64_t packed_stride=std::uint64_t(width)*4;
        const std::uint64_t payload=packed_stride*height;
        if(!data || !ValidImage(width,height,stride,3,bytes,MAX_CAPACITY)
           || packed_stride>std::numeric_limits<std::uint32_t>::max() || payload>capacity)
        { error="Invalid or oversized RGB frame";return false; }
        std::vector<std::uint8_t> bgra(static_cast<std::size_t>(payload));
        for(std::uint32_t y=0;y<height;++y) for(std::uint32_t x=0;x<width;++x)
        {
            const auto from=std::size_t(y)*stride+std::size_t(x)*3,to=(std::size_t(y)*width+x)*4;
            bgra[to]=data[from+2];bgra[to+1]=data[from+1];bgra[to+2]=data[from];bgra[to+3]=255;
        }
        return PublishBGRA(bgra.data(),bgra.size(),width,height,static_cast<std::uint32_t>(packed_stride));
    }

    void Close()
    {
        std::lock_guard<std::mutex> local(local_mutex);
#ifdef _WIN32
        // Retire the instance even when a consumer holds the shared mutex past
        // the bounded close timeout. A live process is not a live Publisher.
        if(lifetime_event) { CloseHandle(lifetime_event);lifetime_event=nullptr; }
        if(open && view)
        {
            detail::Lock lock(mutex_handle,100);
            if(lock.acquired)
            {
                Header header{};std::memcpy(&header,view,HEADER_BYTES);
                if(header.generation==generation) { header.owner_pid=0;std::memcpy(view,&header,HEADER_BYTES); }
            }
        }
        if(view) UnmapViewOfFile(view);
        if(mapping) CloseHandle(mapping);
        if(mutex_handle) CloseHandle(mutex_handle);
        view=nullptr;mapping=nullptr;mutex_handle=nullptr;
#endif
        open=false;
    }
private:
    bool open=false;
    std::uint64_t capacity=0,generation=0,sequence=0;
    std::string error;
    std::mutex local_mutex;
#ifdef _WIN32
    HANDLE mapping=nullptr,mutex_handle=nullptr,lifetime_event=nullptr;
    std::uint8_t* view=nullptr;
#endif
};

class Reader
{
public:
    explicit Reader(const std::string& value) : channel(value) {}
    ~Reader() { Close(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    std::string LastError() const { return error; }

    FrameStatus ReadLatest(Frame& frame, std::uint32_t ttl_ms=2000, std::uint32_t lock_timeout_ms=5)
    {
        if(!ValidChannel(channel) || ttl_ms==0 || ttl_ms>60000 || lock_timeout_ms>100)
        { error="Invalid channel, TTL or lock timeout";return FrameStatus::Invalid; }
#ifdef _WIN32
        if(!mutex_handle) mutex_handle=OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE,FALSE,detail::Name(channel,true).c_str());
        if(!mutex_handle) { error="Publisher unavailable";return FrameStatus::Unavailable; }
        detail::Lock lock(mutex_handle,lock_timeout_ms);
        if(!lock.acquired) { error="Surface mutex busy";return FrameStatus::Busy; }
        if(!mapping) mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,detail::Name(channel,false).c_str());
        if(!mapping) { error="Publisher unavailable";return FrameStatus::Unavailable; }
        if(!view)
        {
            void* prefix=MapViewOfFile(mapping,FILE_MAP_READ,0,0,HEADER_BYTES);
            if(!prefix) { error="Cannot map surface header";return FrameStatus::Invalid; }
            Header first{};std::memcpy(&first,prefix,HEADER_BYTES);UnmapViewOfFile(prefix);
            if(!detail::ValidCapacityHeader(first)) { error="Invalid surface capacity header";return FrameStatus::Invalid; }
            mapped_capacity=first.capacity;
            view=static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,static_cast<SIZE_T>(HEADER_BYTES+mapped_capacity)));
            if(!view) { error="Mapping is shorter than declared capacity";return FrameStatus::Invalid; }
        }
        Header header{};std::memcpy(&header,view,HEADER_BYTES);
        if(header.capacity!=mapped_capacity || !detail::ValidCapacityHeader(header))
        { error="Surface capacity changed or corrupted";return FrameStatus::Invalid; }
        if(!header.owner_pid || !header.sequence || !detail::LifetimeExists(header,channel))
        { error="No live frame published";return FrameStatus::Unavailable; }
        if(!ValidHeader(header)) { error="Invalid frame header";return FrameStatus::Invalid; }
        const auto now=GetTickCount64();
        if(header.timestamp_ms>now) { error="Invalid future frame timestamp";return FrameStatus::Invalid; }
        if(now-header.timestamp_ms>ttl_ms) { error="Frame TTL expired";return FrameStatus::Stale; }
        if(header.generation==last_generation && header.sequence==last_sequence)
        {
            // A healthy static-image producer may refresh only its timestamp.
            // Expose that validated heartbeat without allocating/copying pixels
            // or changing the generation/sequence used by renderer upload caches.
            frame.timestamp_ms=header.timestamp_ms;
            error.clear();return FrameStatus::Unchanged;
        }
        // Allocate only after validating format, arithmetic and actual mapped capacity.
        frame.bgra.resize(static_cast<std::size_t>(header.payload_bytes));
        std::memcpy(frame.bgra.data(),view+HEADER_BYTES,frame.bgra.size());
        frame.width=header.width;frame.height=header.height;frame.stride=header.stride;
        frame.sequence=header.sequence;frame.timestamp_ms=header.timestamp_ms;frame.generation=header.generation;
        last_generation=header.generation;last_sequence=header.sequence;
        error.clear();return FrameStatus::NewFrame;
#else
        (void)frame;
        error="FrameSurface shared memory is currently Windows-only";return FrameStatus::Unavailable;
#endif
    }

    void Close()
    {
#ifdef _WIN32
        if(view) UnmapViewOfFile(view);
        if(mapping) CloseHandle(mapping);
        if(mutex_handle) CloseHandle(mutex_handle);
        view=nullptr;mapping=nullptr;mutex_handle=nullptr;mapped_capacity=0;
#endif
        last_sequence=last_generation=0;
    }
private:
    std::string channel,error;
    std::uint64_t last_sequence=0,last_generation=0;
#ifdef _WIN32
    HANDLE mapping=nullptr,mutex_handle=nullptr;
    const std::uint8_t* view=nullptr;
    std::uint64_t mapped_capacity=0;
#endif
};
}
