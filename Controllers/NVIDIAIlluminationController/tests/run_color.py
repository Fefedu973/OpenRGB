"""Test color mathematics only; does not link or initialize NVAPI."""
import os
import pathlib
import shutil
import subprocess
import tempfile

here = pathlib.Path(__file__).resolve().parent
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("Set CXX to a C++17 compiler.")
with tempfile.TemporaryDirectory(prefix="openrgb-nvidia-color-") as temporary:
    binary = pathlib.Path(temporary) / ("test-color.exe" if os.name == "nt" else "test-color")
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I" + str(here.parent),
                    str(here / "test_color.cc"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
