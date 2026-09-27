"""Compile Windows production service; exercise only injected synthetic input."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
out = Path(__file__).parent / '.build'
out.mkdir(exist_ok=True)
exe = out / 'keyboard-input-tests.exe'
subprocess.run(['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/O2', '/W4',
                '/I'+str(root), str(root/'Input/KeyboardInputService.cpp'),
                str(Path(__file__).with_name('test_keyboard_input.cpp')),
                '/Fo'+str(out)+'\\', '/Fe'+str(exe), '/link', 'user32.lib'], check=True)
subprocess.run([str(exe)], check=True, timeout=10)
