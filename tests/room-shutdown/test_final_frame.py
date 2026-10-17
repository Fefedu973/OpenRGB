"""Compile production shutdown bodies with inert sinks; no hardware or network."""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def body(relative, signature):
    source = (ROOT / relative).read_text(encoding="utf-8")
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


ble = body("Controllers/GoveeBluetoothController/RGBController_GoveeBluetooth_Windows.cpp",
           "RGBController_GoveeBluetooth::~RGBController_GoveeBluetooth()")
keyboard = body("Controllers/KBHEController/RGBController_KBHE.cpp",
                "RGBController_KBHE::~RGBController_KBHE()")
stop = body("Controllers/GoveeBluetoothController/GoveeBluetoothController_Windows.cpp",
            "void Controller::Stop(bool requested_black)")
toggle = body("Controllers/GoveeBluetoothController/GoveeBluetoothController_Windows.cpp",
              "void Controller::SetControlEnabled(bool enabled)")
submit = body("Controllers/GoveeBluetoothController/GoveeBluetoothController_Windows.cpp",
              "void Controller::Submit(Frame frame)")
configuration_methods = "\n".join(
    "void " + name + "() " + body(
        "Controllers/GoveeBluetoothController/RGBController_GoveeBluetooth_Windows.cpp",
        "void RGBController_GoveeBluetooth::" + name + "()")
    for name in ("InitDeviceSpecificConfiguration", "DeviceConfigureDevice",
                 "DeviceUpdateDeviceSpecificConfiguration"))
source = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <mutex>
#include <nlohmann/json.hpp>
#include <vector>
#include <iostream>
#include <string>
#define LOG_INFO(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
using RGBColor=unsigned;
unsigned RGBGetRValue(unsigned c){return c&255;}
unsigned RGBGetGValue(unsigned c){return (c>>8)&255;}
unsigned RGBGetBValue(unsigned c){return (c>>16)&255;}
struct mode {unsigned value=0,brightness=100;std::vector<unsigned> colors;};
struct Outcome {bool joined=false,black=false,restored=false;};
constexpr unsigned CONTROLLER_FLAG_MANUALLY_CONFIGURABLE_DEVICE_SPECIFIC=1;
constexpr unsigned CONTROLLER_FLAG_MANUALLY_CONFIGURED_DEVICE_SPECIFIC=2;
struct ColorSink {
 Outcome& out;
 bool enabled=true;
 bool ControlEnabled()const{return enabled;}
 void SetControlEnabled(bool value){enabled=value;}
 void Stop(bool black){assert(out.joined);out.black=black;}
};
struct RGBController_GoveeBluetooth {
 Outcome& out;ColorSink controller;std::vector<unsigned> colors;unsigned active_mode=0;
 std::string configuration;unsigned flags=0;
 std::vector<mode> modes{{}};
 RGBController_GoveeBluetooth(Outcome& o):out(o),controller{o}{InitDeviceSpecificConfiguration();}
 CONFIGURATION_METHODS
 void Shutdown(){out.joined=true;}
 ~RGBController_GoveeBluetooth() BLE_BODY
};
struct KeySink {
 Outcome& out;bool retained=false;
 bool KeepBlackOnExit(){assert(out.joined);out.black=retained=true;return true;}
 std::string GetRestorationResult()const{return "black_exit_confirmed";}
 ~KeySink(){out.restored=!retained;}
};
struct RGBController_KBHE {
 Outcome& out;KeySink* controller;bool keep_black_on_exit=false;
 unsigned active_mode=0;std::vector<unsigned> colors;
 RGBController_KBHE(Outcome& o):out(o),controller(new KeySink{o}){}
 void Shutdown(){out.joined=true;}
 void ReportError(){assert(false);}
 ~RGBController_KBHE() KEY_BODY
};
struct Controller {
 struct {bool keep_black_on_exit=false;} configuration;
 bool final_black=false,stopping=false;
 std::atomic<bool> control_enabled{true},operation_cancelled{false};
 std::mutex mutex;uint64_t control_generation=0;bool have_frame=false;
 struct Frame {unsigned brightness=100,color=0;} latest;
 struct {void notify_all(){} void notify_one(){}} wake;
 struct Worker {
  Controller* owner;bool running=true;
  bool joinable()const{return running;}
  void join(){assert(owner->stopping);running=false;}
 } worker{this};
 void Stop(bool requested_black=false) STOP_BODY
 void SetControlEnabled(bool enabled) TOGGLE_BODY
 void Submit(Frame frame) SUBMIT_BODY
};
int main(){
 unsigned cases=0;
 for(unsigned color:{0u,0x123456u}){
  Outcome result;
  {RGBController_GoveeBluetooth c(result);c.colors={color};}
  assert(result.joined&&result.black==(color==0));++cases;
 }
 for(unsigned color:{0u,0x123456u}){
  Outcome result;
  {RGBController_GoveeBluetooth c(result);c.colors={0xff};c.modes[0]={1,100,{color}};}
  assert(result.black==(color==0));++cases;
 }
 {Outcome result;{RGBController_GoveeBluetooth c(result);c.colors={0xff};c.modes[0].brightness=0;}
  assert(result.black);++cases;}
 {Outcome result;{RGBController_GoveeBluetooth c(result);}
  assert(!result.black);++cases;}
 for(bool policy:{false,true})for(bool black:{false,true}){
  Outcome result;
  {RGBController_KBHE c(result);c.keep_black_on_exit=policy;c.colors.assign(82,0);if(!black)c.colors[81]=0xff;}
  assert(result.joined&&result.black==(policy&&black)&&result.restored==!(policy&&black));++cases;
 }
 {Outcome result;{RGBController_KBHE c(result);c.keep_black_on_exit=true;c.active_mode=1;c.colors.assign(82,0);}
  assert(!result.black&&result.restored);++cases;}
 for(bool policy:{false,true})for(bool black:{false,true})for(bool enabled:{false,true}){
  Controller c;c.configuration.keep_black_on_exit=policy;c.control_enabled=enabled;c.Stop(black);
  assert(c.final_black==(policy&&black&&enabled)&&c.stopping&&!c.worker.joinable());
  c.Stop();assert(c.final_black==(policy&&black&&enabled));++cases;
 }
 {Outcome result;{RGBController_GoveeBluetooth c(result);c.colors={0};c.controller.enabled=false;}
  assert(result.joined&&!result.black);++cases;}
 {Outcome result;{RGBController_GoveeBluetooth c(result);
  auto config=nlohmann::json::parse(c.configuration);
  assert(config["schema"]["control_enabled"]["type"]=="bool");
  assert(config["configuration"]["control_enabled"]==true);
  assert(c.flags&CONTROLLER_FLAG_MANUALLY_CONFIGURABLE_DEVICE_SPECIFIC);++cases;
  for(bool enabled:{false,true,false}){
   config["configuration"]["control_enabled"]=enabled;c.configuration=config.dump();
   c.DeviceUpdateDeviceSpecificConfiguration();assert(c.controller.enabled==enabled);++cases;
  }
  for(auto bad:nlohmann::json::array({0,1,"false",nullptr,nlohmann::json::array(),nlohmann::json::object()})){
   config["configuration"]["control_enabled"]=bad;c.configuration=config.dump();
   c.DeviceUpdateDeviceSpecificConfiguration();assert(!c.controller.enabled);
   assert(nlohmann::json::parse(c.configuration)["configuration"]["control_enabled"]==false);
  }++cases;
  config["configuration"].erase("control_enabled");c.configuration=config.dump();
  c.DeviceUpdateDeviceSpecificConfiguration();assert(!c.controller.enabled);++cases;
  c.flags|=CONTROLLER_FLAG_MANUALLY_CONFIGURED_DEVICE_SPECIFIC;
  c.DeviceConfigureDevice();assert(!c.controller.enabled);++cases;
  c.flags&=~CONTROLLER_FLAG_MANUALLY_CONFIGURED_DEVICE_SPECIFIC;
  c.DeviceConfigureDevice();assert(c.controller.enabled);
  assert(nlohmann::json::parse(c.configuration)["configuration"]["control_enabled"]==true);++cases;
 }}
 {Controller c;c.Submit({80,0x123456});assert(c.have_frame);
  c.SetControlEnabled(false);assert(!c.have_frame&&!c.control_enabled&&c.operation_cancelled&&c.control_generation==1);
  c.Submit({100,0xffffff});assert(!c.have_frame&&c.latest.color==0x123456);
  c.SetControlEnabled(true);assert(!c.have_frame&&c.control_enabled&&c.control_generation==2);
  c.Submit({120,0xabcdef});assert(c.have_frame&&c.latest.color==0xabcdef&&c.latest.brightness==100);
  c.SetControlEnabled(true);assert(c.have_frame&&c.control_generation==2);
  c.Stop();c.SetControlEnabled(false);assert(c.control_enabled&&c.control_generation==2);++cases;
 }
 std::cout<<cases<<" production final-buffer/configuration/suspension cases PASS\n";
}
'''.replace("BLE_BODY", ble).replace("KEY_BODY", keyboard).replace("STOP_BODY", stop) \
    .replace("CONFIGURATION_METHODS", configuration_methods).replace("TOGGLE_BODY", toggle).replace("SUBMIT_BODY", submit)

compiler = os.environ.get("CXX") or shutil.which("cl.exe")
if not compiler:
    raise SystemExit("Run in an x64 MSVC developer environment (or set CXX).")
with tempfile.TemporaryDirectory(prefix="room-shutdown-") as directory:
    directory = Path(directory)
    cpp = directory / "test.cpp"
    cpp.write_text(source, encoding="utf-8")
    exe = directory / "test.exe"
    subprocess.run([compiler, "/nologo", "/EHsc", "/std:c++17", "/I"+str(ROOT / "dependencies/json"), str(cpp), "/Fe:"+str(exe)],
                   cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
