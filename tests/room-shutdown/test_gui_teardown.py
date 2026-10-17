"""Exercise the real dialog teardown bodies with Qt widgets and inert controllers.

No plugins, device detection, hardware, or existing OpenRGB process are opened.
Run in an MSVC developer shell with --qt PATH_TO_QT_MSVC.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "qt/OpenRGBDialog/OpenRGBDialog.cpp"


def body(signature):
    source = SOURCE.read_text(encoding="utf-8")
    start = source.index("{", source.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt", default=os.environ.get("QT_ROOT", os.environ.get("QTDIR")))
    args = parser.parse_args()
    if not args.qt or not shutil.which("cl"):
        parser.error("Use an MSVC developer shell and --qt (or QT_ROOT/QTDIR).")
    qt = Path(args.qt).resolve()
    close = body("void OpenRGBDialog::closeEvent(QCloseEvent *event)")
    clear = body("void OpenRGBDialog::ClearDevicesList()")
    update = body("void OpenRGBDialog::UpdateDevicesList()")
    # Compile the production early-exit before its first controller-list read.
    update_guard = update[:update.index("    std::vector<RGBController *>")]
    source = r'''
#include <QApplication>
#include <QCloseEvent>
#include <QPointer>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

struct Metrics {
    int unregistered=0, invalid_calls=0, pages=0, infos=0, containers=0;
    int captures=0, unloads=0, exit_profiles=0, callback_removals=0, list_reads=0;
} m;
struct FakeController {
    bool alive=true;
    virtual ~FakeController()=default;
    virtual void UnregisterUpdateCallback(void*) {
        if(!alive) ++m.invalid_calls;
        ++m.unregistered;
    }
};
struct OpenRGBDevicePage : QWidget {
    FakeController* controller;
    explicit OpenRGBDevicePage(FakeController* c):controller(c){}
    ~OpenRGBDevicePage() override {
        controller->UnregisterUpdateCallback(this);
        ++m.pages;
    }
};
struct InfoPage : QWidget { ~InfoPage() override {++m.infos;} };
struct OpenRGBPluginContainer : QWidget { ~OpenRGBPluginContainer() override {++m.containers;} };
void OpenRGBDialogResourceManagerCallback(){}
struct Profiles {
    bool LoadAutoProfileExit(){++m.exit_profiles; return false;}
};
struct ResourceManager {
    Profiles profiles;
    static ResourceManager* get(){static ResourceManager r; return &r;}
    void WaitForDetection(){}
    void UnregisterResourceManagerCallback(void(*)(),void*){++m.callback_removals;}
    Profiles* GetProfileManager(){return &profiles;}
};
struct Plugins {
    std::function<void()> unload;
    void UnloadPlugins(){++m.unloads; unload();}
};
struct Ui {
    QTabWidget* DevicesTabBar;
    QTabWidget* InformationTabBar;
};
struct OpenRGBDialog : QWidget {
    Ui owned_ui{new QTabWidget(this),new QTabWidget(this)};
    Ui* ui=&owned_ui;
    QTimer timer;
    QTimer* session_timer=&timer;
    bool session_closing=false;
    Plugins* plugin_manager;
    std::vector<OpenRGBDevicePage*> hidden_pages;
    explicit OpenRGBDialog(Plugins* p):plugin_manager(p){}
    bool IsMinimizeOnClose(){return false;}
    void CaptureLastSession(){++m.captures;}
    void ClearDevicesList() CLEAR_BODY
    void UpdateDevicesList() UPDATE_GUARD
        ++m.list_reads;
    }
    void closeEvent(QCloseEvent* event) override CLOSE_BODY
};

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    std::vector<FakeController> controllers(5);
    Plugins plugins;
    QPointer<QWidget> p0,p1,p2,h0,h1,device_plugin,info_plugin;
    {
        OpenRGBDialog dialog(&plugins);
        auto add=[&](QTabWidget* tabs,QWidget* page){tabs->addTab(page,"test");return page;};
        p0=add(dialog.ui->DevicesTabBar,new OpenRGBDevicePage(&controllers[0]));
        device_plugin=add(dialog.ui->DevicesTabBar,new OpenRGBPluginContainer);
        p1=add(dialog.ui->DevicesTabBar,new OpenRGBDevicePage(&controllers[1]));
        p2=add(dialog.ui->DevicesTabBar,new OpenRGBDevicePage(&controllers[2]));
        add(dialog.ui->InformationTabBar,new InfoPage);
        info_plugin=add(dialog.ui->InformationTabBar,new OpenRGBPluginContainer);
        add(dialog.ui->InformationTabBar,new InfoPage);
        add(dialog.ui->InformationTabBar,new InfoPage);
        h0=new OpenRGBDevicePage(&controllers[3]);
        h1=new OpenRGBDevicePage(&controllers[4]);
        dialog.hidden_pages={static_cast<OpenRGBDevicePage*>(h0.data()),static_cast<OpenRGBDevicePage*>(h1.data())};
        dialog.UpdateDevicesList();
        assert(m.list_reads==1);
        plugins.unload=[&]{
            assert(m.callback_removals==1 && m.captures==1);
            // A plugin may destroy its controllers only after ALL device pages.
            assert(m.unregistered==5 && m.pages==5 && m.infos==3);
            assert(p0.isNull() && p1.isNull() && p2.isNull() && h0.isNull() && h1.isNull());
            assert(dialog.hidden_pages.empty());
            assert(dialog.ui->DevicesTabBar->count()==1 && dialog.ui->InformationTabBar->count()==1);
            assert(device_plugin && info_plugin && m.containers==0);
            for(auto& controller:controllers) controller.alive=false;
            // An already queued list notification must not recreate stale pages.
            dialog.UpdateDevicesList();
            assert(m.list_reads==1);
        };
        QCloseEvent event;
        dialog.closeEvent(&event);
        assert(event.isAccepted() && m.unloads==1 && m.exit_profiles==1);
        dialog.ClearDevicesList(); // Idempotent; plugin containers still retained.
        dialog.closeEvent(&event); // aboutToQuit may request close a second time.
        assert(m.pages==5 && m.infos==3 && m.containers==0);
        assert(m.unloads==1 && m.exit_profiles==1 && m.captures==1);
    }
    assert(m.invalid_calls==0 && m.unregistered==5);
    assert(m.containers==2 && device_plugin.isNull() && info_plugin.isNull());
    std::cout << "PASS: visible/hidden/info teardown, plugin ownership, unload ordering, queued notification and repeated close\n";
}
'''
    source = source.replace("CLEAR_BODY", clear).replace("CLOSE_BODY", close).replace("UPDATE_GUARD", update_guard)
    with tempfile.TemporaryDirectory(prefix="openrgb-gui-teardown-") as temp:
        folder = Path(temp)
        cpp = folder / "test.cpp"
        exe = folder / "test.exe"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run([
            "cl", "/nologo", "/EHsc", "/std:c++17", "/Zc:__cplusplus", "/permissive-", "/MD", "/W4", "/DWIN32", "/D_WINDOWS", "/DNOMINMAX",
            f"/I{qt / 'include'}", f"/I{qt / 'include/QtCore'}", f"/I{qt / 'include/QtGui'}", f"/I{qt / 'include/QtWidgets'}",
            str(cpp), f"/Fe:{exe}", "/link", f"/LIBPATH:{qt / 'lib'}", "Qt6Core.lib", "Qt6Gui.lib", "Qt6Widgets.lib"
        ], cwd=folder, check=True)
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_PLUGIN_PATH=str(qt / "plugins"))
        env["PATH"] = str(qt / "bin") + os.pathsep + env["PATH"]
        subprocess.run([str(exe)], env=env, cwd=folder, check=True, timeout=20)


if __name__ == "__main__":
    main()
