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
source = r'''
#include <algorithm>
#include <cassert>
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
struct ColorSink {
 Outcome& out;
 void Stop(bool black){assert(out.joined);out.black=black;}
};
struct RGBController_GoveeBluetooth {
 Outcome& out;ColorSink controller;std::vector<unsigned> colors;unsigned active_mode=0;
 std::vector<mode> modes{{}};
 RGBController_GoveeBluetooth(Outcome& o):out(o),controller{o}{}
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
 struct {void notify_all(){}} wake;
 struct Worker {
  Controller* owner;bool running=true;
  bool joinable()const{return running;}
  void join(){assert(owner->stopping);running=false;}
 } worker{this};
 void Stop(bool requested_black=false) STOP_BODY
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
 for(bool policy:{false,true})for(bool black:{false,true}){
  Controller c;c.configuration.keep_black_on_exit=policy;c.Stop(black);
  assert(c.final_black==(policy&&black)&&c.stopping&&!c.worker.joinable());
  c.Stop();assert(c.final_black==(policy&&black));++cases;
 }
 std::cout<<cases<<" production final-buffer/stop-policy cases PASS\n";
}
'''.replace("BLE_BODY", ble).replace("KEY_BODY", keyboard).replace("STOP_BODY", stop)

compiler = os.environ.get("CXX") or shutil.which("cl.exe")
if not compiler:
    raise SystemExit("Run in an x64 MSVC developer environment (or set CXX).")
with tempfile.TemporaryDirectory(prefix="room-shutdown-") as directory:
    directory = Path(directory)
    cpp = directory / "test.cpp"
    cpp.write_text(source, encoding="utf-8")
    exe = directory / "test.exe"
    subprocess.run([compiler, "/nologo", "/EHsc", "/std:c++17", str(cpp), "/Fe:"+str(exe)],
                   cwd=directory, check=True)
    subprocess.run([str(exe)], check=True)
