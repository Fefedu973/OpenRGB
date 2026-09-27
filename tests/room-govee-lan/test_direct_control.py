"""Compile real RGBController_Govee methods with fake clock and radio; no network."""
import argparse
from pathlib import Path
import subprocess


def extract(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'Controllers/GoveeController/RGBController_Govee.cpp').read_text()
    body = '\n'.join(extract(source, 'void RGBController_Govee::' + signature) for signature in
                     ('DeviceUpdateLEDs()', 'UpdateLEDsLocked()', 'DeviceUpdateMode()', 'UpdateStatic(bool force)'))
    # Only the clock and network/controller boundaries are replaced.
    body = body.replace('std::chrono::steady_clock::now()', 'ClockNow()')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'direct-methods.inc').write_text(body)
    exe = args.out / 'direct-control.exe'
    subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++17', '/MD', '/W3', '/utf-8',
                    '/I' + str(root / 'Controllers/GoveeController'), '/I' + str(args.out),
                    str(Path(__file__).with_suffix('.cpp')), '/Fo' + str(exe.with_suffix('.obj')),
                    '/Fe' + str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=5)


if __name__ == '__main__':
    main()
