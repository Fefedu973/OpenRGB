/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#ifndef NO_GUI
#include <QCoreApplication>
#include <QThread>
#endif

// Fakes only at the controller/registry boundary. The coordinator below is
// extracted verbatim from OpenRGBPluginAPI.cpp by run.py on every invocation.
struct RGBControllerInterface { virtual ~RGBControllerInterface() = default; };
struct RGBController : RGBControllerInterface {};
static unsigned destroyed=0, detached=0, cleared=0, local=0, updates=0, assertions=0;
struct RGBController_Virtual : RGBController
{
    ~RGBController_Virtual() { ++destroyed; }
    void AttachImageInterface(void* p) { if(!p) ++detached; }
    void ClearCallbacks() { ++cleared; }
};
struct ResourceManager
{
    std::function<void()> callback;
    static ResourceManager* get() { static ResourceManager r; return &r; }
    void UpdateDeviceList() { ++updates; if(callback) callback(); }
};
#include "lifecycle-under-test.inc"

static void Check(bool ok,const char* message) { ++assertions; if(!ok) throw std::runtime_error(message); }
static RGBControllerInterface* Add(const std::shared_ptr<VirtualState>& state)
{
    auto entry=std::make_shared<VirtualEntry>();
    entry->controller=std::make_unique<RGBController_Virtual>();
    entry->mark_local=[] {++local;};
    auto* p=entry->controller.get();
    std::lock_guard<std::mutex> lock(state->mutex);state->entries.emplace(p,entry);return p;
}
static void Flush()
{
#ifndef NO_GUI
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
#endif
}
int main(int argc,char** argv)
{
#ifndef NO_GUI
    QCoreApplication app(argc,argv);
#else
    (void)argc;(void)argv;
#endif
    ResourceManager::get()->callback=[] {
#ifndef NO_GUI
        Check(QThread::currentThread()==QCoreApplication::instance()->thread(),"registry callback off GUI");
#endif
    };
    auto state=std::make_shared<VirtualState>();
    auto* a=Add(state);
    RequestVirtualRegistration(state,a,true,true);
#ifndef NO_GUI
    Check(state->registered.empty(),"deferred registration executed inline");
#endif
    DispatchVirtual(state,[=]{DeleteVirtual(state,a);},false);
    const auto after_delete=updates;
    Flush();
    Check(state->entries.empty()&&state->registered.empty(),"pending registration resurrected deleted wrapper");
    Check(destroyed==1 && detached==1 && cleared>=1,"delete did not revoke and destroy exactly once");
    Check(updates==after_delete,"cancelled registration notified registry");
    // A stale pointer is only used as an opaque key, never dereferenced.
    RequestVirtualRegistration(state,a,true,true);DeleteVirtual(state,a);Flush();
    Check(destroyed==1,"duplicate deletion touched freed wrapper");

    auto* b=Add(state);
    RequestVirtualRegistration(state,b,true,true);
    RequestVirtualRegistration(state,b,false,false);
    Flush();
    Check(state->registered.empty(),"unregister did not supersede pending register");
    RequestVirtualRegistration(state,b,true,true);
    RequestVirtualRegistration(state,b,false,true);
    RequestVirtualRegistration(state,b,true,true);
    Flush();
    Check(state->registered.size()==1&&state->registered[0]==b,"last requested state was not applied");
    const auto before_duplicate=updates;
    RequestVirtualRegistration(state,b,true,false);
    Check(updates==before_duplicate&&state->registered.size()==1,"duplicate register appended twice");
    // Registry readers may take the state mutex during callbacks, and a callback
    // can synchronously unregister another wrapper without recursive deadlock.
    auto* c=Add(state);
    ResourceManager::get()->callback=[=] {
        {std::lock_guard<std::mutex> lock(state->mutex);Check(!state->entries.empty(),"state vanished in notification");}
        RequestVirtualRegistration(state,b,false,false);
    };
    RequestVirtualRegistration(state,c,true,false);
    Check(state->registered.size()==1&&state->registered[0]==c,"nested unregister failed");
    ResourceManager::get()->callback={};

#ifndef NO_GUI
    // A background synchronous caller waits only for GUI delivery; Delete on
    // the GUI never joins it. Pumping the queue completes within a bounded time.
    std::atomic<bool> done=false;
    std::thread requester([&]{ RequestVirtualRegistration(state,b,true,false);done=true; });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(!done && std::chrono::steady_clock::now()<deadline) {Flush();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    if(!done) {std::cerr<<"GUI dispatch timeout\n";std::_Exit(2);}
    requester.join();
    Check(state->registered.size()==2,"background synchronous registration not delivered");
#endif

    // Queued callbacks retain state but never an API pointer; Close cancels all
    // deliveries and destroys both currently registered and pending wrappers.
    auto* d=Add(state);RequestVirtualRegistration(state,d,true,true);
    std::weak_ptr<VirtualState> weak=state;
    CloseVirtualState(state);
    Check(!state->alive&&state->entries.empty()&&state->registered.empty(),"API close retained controllers");
    Check(destroyed==4&&detached==4,"API close did not destroy every owned wrapper");
    const auto closed_updates=updates;
    state.reset();Flush();
    Check(weak.expired(),"queued events kept API state alive after delivery");
    Check(updates==closed_updates,"late event ran after API destruction");
    std::cout<<assertions<<" lifecycle assertions PASS ("
#ifdef NO_GUI
        <<"NO_GUI synchronous fallback"
#else
        <<"real Qt event dispatch"
#endif
        <<"); no device or app launch\n";
}
