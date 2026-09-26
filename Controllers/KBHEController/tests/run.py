"""Compile actual KBHE code against link-time HID mocks; never accesses hardware."""
import os
import pathlib
import shutil
import subprocess
import tempfile

here = pathlib.Path(__file__).resolve().parent
controller = here.parent
root = controller.parents[1]
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("Set CXX to a C++17 compiler.")
includes = [root, controller, root / "dependencies/hidapi-win/include", root / "dependencies/json",
            root / "RGBController", root / "hidapi_wrapper", root / "i2c_smbus", root / "SPDAccessor"]
flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread"] + ["-I" + str(p) for p in includes]
with tempfile.TemporaryDirectory(prefix="openrgb-kbhe-tests-") as build:
    output = pathlib.Path(build) / ("kbhe-tests.exe" if os.name == "nt" else "kbhe-tests")
    subprocess.run([compiler, *flags, str(here / "test_kbhe.cc"), str(controller / "KBHEController.cpp"),
                    str(root / "RGBController/RGBControllerKeyNames.cpp"), "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
subprocess.run([compiler, *flags, "-fsyntax-only", str(controller / "RGBController_KBHE.cpp"),
                str(controller / "KBHEControllerDetect.cpp")], check=True)
print("Native RGB wrapper and detector compile against current OpenRGB headers: PASS")
