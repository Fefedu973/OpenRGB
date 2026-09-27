// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QPointer>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "ResourceManager.h"
#include "OpenRGBSettingsPage.h"
#include "OpenRGBDynamicSettingsWidget.h"
#include "qt/LastSessionCheckpoint.h"

using nlohmann::json;
static unsigned assertions = 0;
static void Check(bool value, const char* reason)
{
    ++assertions;
    if(!value) throw std::runtime_error(reason);
}
static QByteArray ReadBytes(const QString& path)
{
    QFile file(path);
    Check(file.open(QIODevice::ReadOnly), "read fixture");
    return file.readAll();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try
    {
        auto& settings = ResourceManager::get()->settings;
        json properties = json::object();
        for(const char* key : {"open_profile", "exit_profile", "resume_profile", "suspend_profile", "service_startup_profile", "service_shutdown_profile"})
        {
            properties[key] = {{"title", key}, {"type", "profile"}};
            settings.settings["ProfileManager"][key] = {{"enabled", false}, {"name", ""}};
        }
        properties["remember_last_session"] = {{"title", "Remember Last Session"}, {"type", "bool"}};
        settings.schema = {{"ProfileManager", {{"title", "Profile Manager"}, {"type", "object"}, {"properties", properties}}}};
        settings.settings["ProfileManager"]["open_profile"] = {{"enabled", true}, {"name", "Full Scale - Rainbow"}};
        QTabWidget tabs;
        tabs.resize(900, 700);
        auto* page = new OpenRGBSettingsPage;
        tabs.addTab(page, "General Settings");
        tabs.show();
        app.processEvents();
        Check(page->findChildren<QGroupBox*>().size() == 1, "initial group exists");
        Check(page->findChildren<QGroupBox*>().front()->isVisible(), "initial group visible");
        const auto original = settings.settings;

        // The real schema callback used to destroy the GUI from the worker,
        // leaving zero groups. Production UpdateInterface is called here.
        std::thread worker([page] { page->UpdateInterface(); });
        worker.join();
        app.processEvents();
        QEventLoop settle;
        QTimer::singleShot(20, &settle, &QEventLoop::quit);
        settle.exec();
        Check(page->findChildren<QGroupBox*>().size() == 1, "worker refresh keeps group");
        Check(page->findChildren<QGroupBox*>().front()->isVisible(), "worker refresh keeps visible content");
        for(auto* child : page->findChildren<QWidget*>()) Check(child->thread() == app.thread(), "GUI ownership after refresh");
        settings.saves = 0;
        auto widgets = page->findChildren<OpenRGBDynamicSettingsWidget*>();
        for(auto* widget : widgets) widget->ProfileListUpdated();
        Check(settings.saves == 0, "profile refresh must not save settings");
        Check(settings.settings == original, "profile refresh preserves settings");
        std::thread profile_worker([widgets] { for(auto* widget : widgets) widget->ProfileListUpdated(); });
        profile_worker.join();
        app.processEvents();
        Check(settings.saves == 0, "worker profile refresh must not save");
        unsigned unselected = 0;
        QComboBox* selected = nullptr;
        for(auto* combo : page->findChildren<QComboBox*>())
        {
            if(combo->currentIndex() == -1) ++unselected;
            if(combo->currentText() == "Full Scale - Rainbow") selected = combo;
        }
        Check(unselected == 5, "empty profiles stay unselected");
        Check(selected != nullptr, "selected profile retained");
        selected->setCurrentIndex(selected->findText("Alpha"));
        Check(settings.saves == 1, "real user edit saves once");
        Check(settings.settings["ProfileManager"]["open_profile"]["name"] == "Alpha", "real edit persisted");
        settings.settings["ProfileManager"]["open_profile"]["name"] = "Missing preset";
        page->UpdateInterface();
        for(auto* combo : page->findChildren<QComboBox*>()) Check(combo->currentIndex() == -1, "missing preset not silently replaced");
        QPointer<OpenRGBSettingsPage> deleted = new OpenRGBSettingsPage;
        std::thread late([deleted] { deleted->UpdateInterface(); });
        late.join();
        delete deleted;
        app.processEvents();
        Check(deleted.isNull(), "queued refresh cancelled by receiver destruction");

        QTemporaryDir directory;
        Check(directory.isValid(), "temporary directory");
        const QString path = directory.filePath("last-session.json");
        LastSessionCheckpoint checkpoint(path);
        ProfileLoadState loading;
        json plugins = {{"Synthetic effects", {{"speed", 17}, {"running", true}}},
                        {"Synthetic map", {{"version", 1}, {"active_map", "FullScale.json"}}}};
        auto collect = [&] { return plugins; };
        Check(!checkpoint.Capture(false, loading, collect, "Alpha"), "startup/teardown gate");
        Check(!QFile::exists(path), "gate creates no file");
        Check(checkpoint.Capture(true, loading, collect, "Alpha"), "first checkpoint");
        const QByteArray good = ReadBytes(path);
        Check(checkpoint.WriteCount() == 1, "one write");
        Check(checkpoint.Capture(true, loading, collect, "Alpha"), "unchanged capture accepted");
        Check(checkpoint.WriteCount() == 1, "unchanged JSON not rewritten");
        {
            ProfileLoadState::Scope load(loading);
            Check(!checkpoint.Capture(true, loading, collect, "Alpha"), "in-flight load suppressed");
        }
        Check(!checkpoint.Capture(true, loading, [&] {
            ProfileLoadState::Scope transient(loading);
            return json{{"Synthetic effects", {{"speed", 999}}}};
        }, "Alpha"), "load during collection suppressed even after completion");
        Check(ReadBytes(path) == good, "transient state preserves last good file");
        loading.BeginRemote();
        loading.BeginRemote();
        Check(!checkpoint.Capture(true, loading, collect, "Alpha"), "remote load suppressed");
        loading.EndRemote();
        Check(!loading.Busy(), "duplicate remote begin balanced");
        Check(!checkpoint.Capture(true, loading, [] { return json::object(); }, "Alpha"), "empty unloaded plugins rejected");
        Check(ReadBytes(path) == good, "empty snapshot preserves previous state");
        plugins["Synthetic effects"]["speed"] = 21;
        Check(checkpoint.Capture(true, loading, collect, "Alpha"), "changed checkpoint");
        Check(checkpoint.WriteCount() == 2, "changed JSON writes once");
        LastSessionCheckpoint reopened(path);
        json restored;
        Check(reopened.Read(restored), "checkpoint survives new instance");
        Check(restored["plugins"] == plugins, "effect and canonical map payload round-trip");
        Check(restored.size() == 3 && !restored.contains("controllers") && !restored.contains("configuration"), "plugins-only boundary");
        Check(!LastSessionCheckpoint::Valid(json{{"version", 2}, {"plugins", plugins}, {"source_profile", "Alpha"}}), "unknown version rejected");
        Check(!LastSessionCheckpoint::Valid(json{{"version", 1}, {"plugins", plugins}, {"source_profile", "Alpha"}, {"controllers", json::array()}}), "controller snapshot rejected");
        QFile broken(path);
        Check(broken.open(QIODevice::WriteOnly | QIODevice::Truncate), "corrupt test fixture");
        broken.write("{broken"); broken.close();
        Check(!reopened.Read(restored), "invalid file requests startup-profile fallback");
        LastSessionCheckpoint unavailable(directory.filePath("absent/last-session.json"));
        Check(!unavailable.Capture(true, loading, collect, "Alpha"), "failed atomic write reported");
        Check(unavailable.WriteCount() == 0, "failed write not committed");
        std::cout << "PASS " << assertions << " assertions; real Qt settings widgets, no hardware\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
