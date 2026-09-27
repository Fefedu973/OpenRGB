"""Compile production zone rebuild/routing methods with a fake USB boundary.

MSVC developer prompt: python run.py --out <temporary-directory>
--without-fix demonstrates the pre-fix failure; it must exit nonzero.
No hardware, OpenRGB instance or shared settings are accessed.
"""
import argparse
from pathlib import Path
import subprocess


def method(source, signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--without-fix', action='store_true')
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[2]
    a.out.mkdir(parents=True, exist_ok=True)
    methods = []
    for folder, name in [('NollieController', 'Nollie'),
                         ('CorsairLightingNodeController', 'CorsairLightingNode')]:
        source = (repo / 'Controllers' / folder / ('RGBController_' + name + '.cpp')).read_text(encoding='utf-8')
        for fn in ('SetupZones()', 'DeviceUpdateZoneLEDs(int zone)', 'DeviceUpdateSingleLED(int led)'):
            methods.append(method(source, 'void RGBController_' + name + '::' + fn))
    actual = '\n\n'.join(methods)
    if a.without_fix:
        assert actual.count('leds_channel.clear();') == 2
        actual = actual.replace('leds_channel.clear();', '')
    (a.out / 'methods.inc').write_text(actual, encoding='utf-8', newline='\n')
    govee = (repo / 'Controllers/GoveeController/RGBController_Govee.cpp').read_text(encoding='utf-8')
    table = govee[govee.index('enum GoveeZoneLayout'):govee.index('\nenum\n', govee.index('enum GoveeZoneLayout'))]
    table += '\n' + method(govee, 'void RGBController_Govee::SetupZones()')
    table += '\n' + method(govee, 'void RGBController_Govee::DeviceConfigureZone(int zone_idx)')
    (a.out / 'govee-methods.inc').write_text(table, encoding='utf-8', newline='\n')
    aura = (repo / 'Controllers/AsusAuraUSBController/AsusAuraUSBController/RGBController_AsusAuraUSB.cpp').read_text(encoding='utf-8')
    aura = '\n'.join(method(aura, 'void RGBController_AuraUSB::' + fn) for fn in
                     ('SetupZones()', 'DeviceConfigureZone(int zone_idx)', 'DeviceUpdateZoneLEDs(int zone)', 'DeviceUpdateSingleLED(int led)'))
    (a.out / 'aura-methods.inc').write_text(aura, encoding='utf-8', newline='\n')
    exe = a.out / ('channel-baseline.exe' if a.without_fix else 'channel-reconfigure.exe')
    subprocess.run(['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/utf-8',
                    '/I' + str(a.out), str(Path(__file__).with_name('test_reconfigure.cpp')),
                    '/Fo' + str(exe.with_suffix('.obj')), '/Fe' + str(exe)], check=True, cwd=a.out)
    subprocess.run([str(exe)], check=True, timeout=10, cwd=a.out)


if __name__ == '__main__':
    main()
