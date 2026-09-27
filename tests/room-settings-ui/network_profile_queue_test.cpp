// SPDX-License-Identifier: GPL-2.0-or-later
// Compile the real worker body, extracted at build time; only plugin callbacks
// are fakes. No sockets, application configuration, or device I/O are used.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include "NetworkProtocol.h"
#include "ProfileLoadState.h"

namespace network_test
{
constexpr unsigned PROFILE_QUEUE_CANCEL = 0xFFFFFFFFu;
struct NetworkClientListenerThreadQueueEntry
{
    NetPacketHeader header{};
    unsigned char* data = nullptr;
    unsigned generation = 0;
};
struct NetworkClientListenerThread
{
    std::atomic<bool> online{true};
    std::mutex queue_mutex;
    std::condition_variable start_cv;
    std::queue<NetworkClientListenerThreadQueueEntry> queue;
};
struct ProfileManager
{
    ProfileLoadState loading;
    std::atomic<unsigned> begins{0}, loaded{0}, completed{0}, cancelled{0};
    void OnRemoteProfileLoadCancelled() { loading.EndRemote(); ++cancelled; }
};
struct ResourceManager
{
    static ResourceManager* get() { static ResourceManager r; return &r; }
    ProfileManager* GetProfileManager() { return &profiles; }
    ProfileManager profiles;
};
struct NetworkClient
{
    std::atomic<bool> remote_profile_load_pending{false};
    void ProfileManagerListenThread(NetworkClientListenerThread*);
    void ProcessRequest_ProfileManager_ProfileAboutToLoad()
    { auto* p=ResourceManager::get()->GetProfileManager(); p->loading.BeginRemote(); ++p->begins; }
    void ProcessRequest_ProfileManager_ProfileLoaded(unsigned, unsigned char*)
    { ++ResourceManager::get()->GetProfileManager()->loaded; }
    void ProcessRequest_ProfileManager_ActiveProfileChanged(unsigned, unsigned char*)
    { auto* p=ResourceManager::get()->GetProfileManager(); p->loading.EndRemote(); ++p->completed; }
    void ProcessRequest_ProfileManager_ProfileListUpdated(unsigned, unsigned char*) {}
    void ProcessRequest_RGBController_SignalUpdate(unsigned, unsigned char*, unsigned) {}
};
#include "network-profile-worker.inc"

void Push(NetworkClientListenerThread& queue, unsigned packet)
{
    NetworkClientListenerThreadQueueEntry entry;
    entry.header.pkt_id=packet;
    if(packet!=PROFILE_QUEUE_CANCEL) { entry.header.pkt_size=1; entry.data=new unsigned char[1]{0}; }
    { std::lock_guard<std::mutex> lock(queue.queue_mutex); queue.queue.push(entry); }
    queue.start_cv.notify_all();
}
}

unsigned TestNetworkProfileQueue()
{
    using namespace network_test;
    using namespace std::chrono_literals;
    auto* p=ResourceManager::get()->GetProfileManager();
    NetworkClient client;
    NetworkClientListenerThread queue;
    // Lost connection while ABOUT was still queued: cancellation follows it.
    Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_ABOUT_TO_LOAD);
    Push(queue,PROFILE_QUEUE_CANCEL);
    std::thread worker([&] { client.ProfileManagerListenThread(&queue); });
    unsigned checks=0;
    const auto wait=[&](const auto& predicate, const char* message)
    {
        const auto deadline=std::chrono::steady_clock::now()+2s;
        while(!predicate() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(1ms);
        ++checks;
        if(!predicate()) throw std::runtime_error(message);
    };
    const auto stop=[&]
    {
        { std::lock_guard<std::mutex> lock(queue.queue_mutex); queue.online=false; }
        queue.start_cv.notify_all(); worker.join();
        // Same ordering as production StopClient: cancellation after join.
        if(client.remote_profile_load_pending.exchange(false)) p->OnRemoteProfileLoadCancelled();
    };
    try
    {
        wait([&]{return p->cancelled==1 && !p->loading.Busy();},"queued ABOUT cancelled on disconnect");
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_ABOUT_TO_LOAD);
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_LOADED);
        Push(queue,PROFILE_QUEUE_CANCEL);
        wait([&]{return p->cancelled==2 && p->loaded==1;},"disconnect after LOADED still completes plugin transaction");
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_ABOUT_TO_LOAD);
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_LOADED);
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_ACTIVE_PROFILE_CHANGED);
        Push(queue,PROFILE_QUEUE_CANCEL);
        wait([&]{return p->completed==1 && !client.remote_profile_load_pending;},"reconnected profile completes normally");
        ++checks;
        if(p->cancelled!=2) throw std::runtime_error("completed transaction was cancelled twice");
        Push(queue,NET_PACKET_ID_PROFILEMANAGER_PROFILE_ABOUT_TO_LOAD);
        wait([&]{return p->begins==4 && p->loading.Busy();},"new transaction started before intentional stop");
        stop();
        ++checks;
        if(p->loading.Busy() || p->cancelled!=3) throw std::runtime_error("intentional stop did not clear load guard");
    }
    catch(...) { if(worker.joinable()) stop(); throw; }
    return checks;
}
