/*---------------------------------------------------------*\
| OpenRGBPluginAPI.cpp                                      |
|                                                           |
|   Interface for OpenRGB plugins to call OpenRGB functions |
|                                                           |
|   Adam Honse (CalcProgrammer1)                08 Feb 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "OpenRGBPluginAPI.h"
#include "RGBController_Dummy.h"
#include "RGBController_Virtual.h"
#include <algorithm>
#include <functional>
#include <mutex>
#include <unordered_map>
#ifndef NO_GUI
#include <QCoreApplication>
#include <QThread>
#endif

// BEGIN VIRTUAL_CONTROLLER_LIFECYCLE
// Kept independent of the API object's lifetime: queued work owns this state,
// never a raw `this`. Entries are tombstoned before notifications can re-enter.
struct OpenRGBVirtualControllerState
{
    struct Entry
    {
        std::unique_ptr<RGBController_Virtual> controller;
        std::function<void()> mark_local;
        uint64_t revision = 0;
        bool registered = false;
    };
    std::recursive_mutex operations;
    std::mutex mutex;
    bool alive = true;
    std::unordered_map<RGBControllerInterface*, std::shared_ptr<Entry>> entries;
    std::vector<RGBController*> registered;
};

namespace
{
using VirtualState = OpenRGBVirtualControllerState;
using VirtualEntry = VirtualState::Entry;

// The historical InThread API now means deferred delivery on the application
// thread. It never spawns a detached worker, nor joins a worker waiting for UI.
// Without a Qt application (including NO_GUI), operations execute synchronously.
void DispatchVirtual(const std::shared_ptr<VirtualState>& state,
                     std::function<void()> operation, bool deferred)
{
    auto run = [state, operation = std::move(operation)] {
        std::lock_guard<std::recursive_mutex> serial(state->operations);
        operation();
    };
#ifndef NO_GUI
    if(auto* application = QCoreApplication::instance())
    {
        if(!QCoreApplication::closingDown())
        {
            if(deferred)
            {
                QMetaObject::invokeMethod(application, std::move(run), Qt::QueuedConnection);
                return;
            }
            if(QThread::currentThread() != application->thread())
            {
                QMetaObject::invokeMethod(application, std::move(run), Qt::BlockingQueuedConnection);
                return;
            }
        }
    }
#else
    (void)deferred;
#endif
    run();
}

std::shared_ptr<VirtualEntry> FindVirtual(const std::shared_ptr<VirtualState>& state,
                                        RGBControllerInterface* controller)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    const auto found = state->entries.find(controller);
    return state->alive && found != state->entries.end() ? found->second : nullptr;
}

void ChangeVirtualRegistration(const std::shared_ptr<VirtualState>& state,
                               const std::shared_ptr<VirtualEntry>& entry, bool registered)
{
    RGBController_Virtual* controller;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->alive || !entry->controller || entry->registered == registered) return;
        controller = entry->controller.get();
        entry->registered = registered;
        if(registered)
        {
            entry->mark_local();
            state->registered.push_back(controller);
        }
        else
            state->registered.erase(std::remove(state->registered.begin(), state->registered.end(), controller), state->registered.end());
    }
    if(!registered) controller->ClearCallbacks();
    // No state mutex across registry notifications: GetRegisteredVirtualControllers
    // takes it while ResourceManager already owns DeviceListChangeMutex.
    ResourceManager::get()->UpdateDeviceList();
}

void RequestVirtualRegistration(const std::shared_ptr<VirtualState>& state,
                                RGBControllerInterface* controller, bool registered, bool deferred)
{
    std::weak_ptr<VirtualEntry> weak;
    uint64_t revision;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = state->entries.find(controller);
        if(!state->alive || found == state->entries.end()) return;
        weak = found->second;
        revision = ++found->second->revision;
    }
    DispatchVirtual(state, [state, weak, revision, registered] {
        const auto entry = weak.lock();
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(!state->alive || !entry || !entry->controller || entry->revision != revision) return;
        }
        ChangeVirtualRegistration(state, entry, registered);
    }, deferred);
}

void DeleteVirtual(const std::shared_ptr<VirtualState>& state, RGBControllerInterface* controller)
{
    std::shared_ptr<VirtualEntry> entry;
    bool registered;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = state->entries.find(controller);
        if(found == state->entries.end()) return; // Duplicate delete/stale requests are inert.
        entry = found->second;
        ++entry->revision;
        registered = entry->registered;
        entry->registered = false;
        state->entries.erase(found);
        state->registered.erase(std::remove(state->registered.begin(), state->registered.end(), controller), state->registered.end());
    }
    // Revoke all borrowed plugin callbacks before its owner can be unloaded.
    entry->controller->AttachImageInterface(nullptr);
    entry->controller->ClearCallbacks();
    if(registered) ResourceManager::get()->UpdateDeviceList();
    entry->controller.reset();
}

void CloseVirtualState(const std::shared_ptr<VirtualState>& state)
{
    DispatchVirtual(state, [state] {
        std::vector<RGBControllerInterface*> controllers;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->alive = false;
            for(const auto& entry : state->entries) controllers.push_back(entry.first);
        }
        for(auto* controller : controllers) DeleteVirtual(state, controller);
    }, false);
}
} // namespace
// END VIRTUAL_CONTROLLER_LIFECYCLE

OpenRGBPluginAPI::OpenRGBPluginAPI()
    : virtual_controller_state(std::make_shared<OpenRGBVirtualControllerState>())
{
    log_manager         = ResourceManager::get()->GetLogManager();
    plugin_manager      = ResourceManager::get()->GetPluginManager();
    profile_manager     = ResourceManager::get()->GetProfileManager();
    resource_manager    = ResourceManager::get();
    settings_manager    = ResourceManager::get()->GetSettingsManager();
}

OpenRGBPluginAPI::~OpenRGBPluginAPI()
{
    CloseVirtualState(virtual_controller_state);
}

/*---------------------------------------------------------*\
| LogManager APIs                                           |
\*---------------------------------------------------------*/
void OpenRGBPluginAPI::LogEntry(const char* filename, int line, unsigned int level, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    log_manager->LogEntry_va(filename, line, level, fmt, va);

    va_end(va);
}

/*---------------------------------------------------------*\
| PluginManager APIs                                        |
\*---------------------------------------------------------*/
RGBControllerInterface* OpenRGBPluginAPI::CreateVirtualRGBController(RGBController_Setup* setup)
{
    RGBControllerInterface* result = nullptr;
    const auto state = virtual_controller_state;
    DispatchVirtual(state, [state, setup, &result] {
        auto entry = std::make_shared<VirtualEntry>();
        entry->controller = std::make_unique<RGBController_Virtual>(setup);
        entry->mark_local = [controller = entry->controller.get()] {
            controller->flags &= ~CONTROLLER_FLAG_REMOTE;
            controller->flags |= CONTROLLER_FLAG_LOCAL;
        };
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->alive) return;
        result = entry->controller.get();
        state->entries.emplace(result, std::move(entry));
    }, false);
    return result;
}

bool OpenRGBPluginAPI::AttachImageInterface(RGBControllerInterface* controller, room_image::RGBControllerImageInterface* sink)
{
    bool attached = false;
    const auto state = virtual_controller_state;
    const std::weak_ptr<VirtualEntry> weak = FindVirtual(state, controller);
    DispatchVirtual(state, [state, weak, sink, &attached] {
        const auto entry = weak.lock();
        if(!entry || !entry->controller || sink == entry->controller.get()) return;
        entry->controller->AttachImageInterface(sink);
        attached = true;
    }, false);
    return attached;
}

void OpenRGBPluginAPI::RegisterVirtualRGBControllerInThread(RGBControllerInterface* controller)
{
    RequestVirtualRegistration(virtual_controller_state, controller, true, true);
}
void OpenRGBPluginAPI::RegisterVirtualRGBController(RGBControllerInterface* controller)
{
    RequestVirtualRegistration(virtual_controller_state, controller, true, false);
}
void OpenRGBPluginAPI::UnregisterVirtualRGBControllerInThread(RGBControllerInterface* controller)
{
    RequestVirtualRegistration(virtual_controller_state, controller, false, true);
}
void OpenRGBPluginAPI::UnregisterVirtualRGBController(RGBControllerInterface* controller)
{
    RequestVirtualRegistration(virtual_controller_state, controller, false, false);
}

void OpenRGBPluginAPI::UpdateVirtualRGBController(RGBControllerInterface* controller, RGBController_Setup* setup)
{
    const auto state = virtual_controller_state;
    const std::weak_ptr<VirtualEntry> weak = FindVirtual(state, controller);
    DispatchVirtual(state, [weak, setup] {
        const auto entry = weak.lock();
        if(entry && entry->controller) entry->controller->UpdateVirtualController(setup);
    }, false);
}

void OpenRGBPluginAPI::DeleteVirtualRGBController(RGBControllerInterface* controller)
{
    const auto state = virtual_controller_state;
    std::weak_ptr<VirtualEntry> weak;
    // Invalidate queued registrations immediately, even before a non-GUI caller
    // reaches the synchronous application-thread dispatch.
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = state->entries.find(controller);
        if(found == state->entries.end()) return;
        weak = found->second;
        ++found->second->revision;
    }
    DispatchVirtual(state, [state, weak] {
        const auto entry = weak.lock();
        if(entry && entry->controller) DeleteVirtual(state, entry->controller.get());
    }, false);
}

std::vector<RGBController*> OpenRGBPluginAPI::GetRegisteredVirtualControllers() const
{
    const auto state = virtual_controller_state;
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->registered;
}

/*---------------------------------------------------------*\
| ProfileManager APIs                                       |
\*---------------------------------------------------------*/
void OpenRGBPluginAPI::ClearActiveProfile()
{
    profile_manager->ClearActiveProfile();
}

std::vector<std::string> OpenRGBPluginAPI::GetProfileList()
{
    return(profile_manager->GetProfileList());
}

bool OpenRGBPluginAPI::LoadProfile(std::string profile_name)
{
    return(profile_manager->LoadProfile(profile_name));
}

bool OpenRGBPluginAPI::SaveProfileFromPlugin(std::string profile_name, std::string plugin_name, nlohmann::json plugin_data)
{
    return(profile_manager->SaveProfileFromPlugin(profile_name, plugin_name, plugin_data));
}

/*---------------------------------------------------------*\
| ResourceManager APIs                                      |
\*---------------------------------------------------------*/
filesystem::path OpenRGBPluginAPI::GetConfigurationDirectory()
{
    return(resource_manager->GetConfigurationDirectory());
}

bool OpenRGBPluginAPI::GetDetectionEnabled()
{
    return(resource_manager->GetDetectionEnabled());
}

unsigned int OpenRGBPluginAPI::GetDetectionPercent()
{
    return(resource_manager->GetDetectionPercent());
}

std::string OpenRGBPluginAPI::GetDetectionString()
{
    return(resource_manager->GetDetectionString());
}

void OpenRGBPluginAPI::RescanDevices()
{
    resource_manager->RescanDevices();
}

void OpenRGBPluginAPI::WaitForDetection()
{
    resource_manager->WaitForDetection();
}

std::vector<RGBControllerInterface*> OpenRGBPluginAPI::GetRGBControllers()
{
    return(resource_manager->GetRGBControllerInterfaces());
}

/*---------------------------------------------------------*\
| RGBController APIs                                        |
\*---------------------------------------------------------*/
nlohmann::json OpenRGBPluginAPI::GetDeviceDescriptionJSON(RGBControllerInterface* controller)
{
    return(RGBController::GetDeviceDescriptionJSON((RGBController*)controller));
}

nlohmann::json OpenRGBPluginAPI::GetLEDDescriptionJSON(led led)
{
    return(RGBController::GetLEDDescriptionJSON(led));
}

nlohmann::json OpenRGBPluginAPI::GetMatrixMapDescriptionJSON(matrix_map_type matrix_map)
{
    return(RGBController::GetMatrixMapDescriptionJSON(matrix_map));
}

nlohmann::json OpenRGBPluginAPI::GetModeDescriptionJSON(mode mode)
{
    return(RGBController::GetModeDescriptionJSON(mode));
}

nlohmann::json OpenRGBPluginAPI::GetSegmentDescriptionJSON(segment segment)
{
    return(RGBController::GetSegmentDescriptionJSON(segment));
}

nlohmann::json OpenRGBPluginAPI::GetZoneDescriptionJSON(zone zone)
{
    return(RGBController::GetZoneDescriptionJSON(zone));
}

RGBControllerInterface* OpenRGBPluginAPI::SetDeviceDescriptionJSON(nlohmann::json controller_json)
{
    RGBController_Dummy* new_controller = new RGBController_Dummy();
    RGBController::SetDeviceDescriptionJSON(controller_json, (RGBController*)new_controller);

    return(new_controller);
}

led OpenRGBPluginAPI::SetLEDDescriptionJSON(nlohmann::json led_json)
{
    return(RGBController::SetLEDDescriptionJSON(led_json));
}

matrix_map_type OpenRGBPluginAPI::SetMatrixMapDescriptionJSON(nlohmann::json matrix_map_json)
{
    return(RGBController::SetMatrixMapDescriptionJSON(matrix_map_json));
}

mode OpenRGBPluginAPI::SetModeDescriptionJSON(nlohmann::json mode_json)
{
    return(RGBController::SetModeDescriptionJSON(mode_json));
}

segment OpenRGBPluginAPI::SetSegmentDescriptionJSON(nlohmann::json segment_json)
{
    return(RGBController::SetSegmentDescriptionJSON(segment_json));
}

zone OpenRGBPluginAPI::SetZoneDescriptionJSON(nlohmann::json zone_json)
{
    return(RGBController::SetZoneDescriptionJSON(zone_json));
}

bool OpenRGBPluginAPI::CompareControllers(RGBControllerInterface* controller_1, RGBControllerInterface* controller_2)
{
    return(RGBController::CompareControllers((RGBController*)controller_1, (RGBController*)controller_2));
}

std::string OpenRGBPluginAPI::DeviceTypeToString(device_type type)
{
    return(RGBController::DeviceTypeToString(type));
}

bool OpenRGBPluginAPI::SetModeValuesFromMode(mode& destination, mode& source)
{
    return(RGBController::SetModeValuesFromMode(destination, source));
}

/*---------------------------------------------------------*\
| SettingsManager APIs                                      |
\*---------------------------------------------------------*/
nlohmann::json OpenRGBPluginAPI::GetSettings(std::string settings_key)
{
    return(settings_manager->GetSettings(settings_key));
}

void OpenRGBPluginAPI::SaveSettings()
{
    settings_manager->SaveSettings();
}

void OpenRGBPluginAPI::SetSettings(std::string settings_key, nlohmann::json new_settings)
{
    settings_manager->SetSettings(settings_key, new_settings);
}
