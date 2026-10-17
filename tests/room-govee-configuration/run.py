"""Offline MSVC regression: real RGBController, extracted production Load/Save.

Run in an x64 MSVC developer shell. Only .build/Configuration.json is written;
no ResourceManager, detector, network, hardware or user configuration is opened.
"""
from pathlib import Path
import argparse
import subprocess


def method(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--without-fix', action='store_true', help='negative control: must fail restoration')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parents[1]
    build = here / '.build'
    build.mkdir(exist_ok=True)
    source = (repo / 'ProfileManager.cpp').read_text(encoding='utf-8')
    actual = '\n\n'.join(method(source, signature) for signature in (
        'bool ProfileManager::LoadControllerFromListWithOptions',
        'bool ProfileManager::SaveConfiguration()'))
    if args.without_fix:
        needle = 'govee_configuration::VersionsMatch(profile_controller, load_controller)'
        assert actual.count(needle) == 1
        actual = actual.replace(needle, '(profile_controller->GetVersion() == load_controller->GetVersion())')
    (build / 'profile-methods.inc').write_text(actual, encoding='utf-8')
    exe = build / ('baseline.exe' if args.without_fix else 'govee-configuration.exe')
    sources = [here / 'test_configuration.cpp', repo / 'RGBController/RGBController.cpp',
               repo / 'RGBController/RGBControllerKeyNames.cpp', repo / 'StringUtils.cpp', repo / 'JsonUtils.cpp']
    command = ['cl', '/nologo', '/std:c++17', '/EHsc', '/MD', '/utf-8', '/W3',
               '/I' + str(here / 'stubs'), '/I' + str(build), '/I' + str(repo / 'RGBController'),
               '/I' + str(repo / 'dependencies/json'), '/I' + str(repo)]
    subprocess.run(command + list(map(str, sources)) + ['/Fe' + str(exe)], cwd=build, check=True)
    subprocess.run([str(exe)], cwd=build, check=True, timeout=20)


if __name__ == '__main__':
    main()
