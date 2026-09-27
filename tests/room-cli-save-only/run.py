"""Compile the real ApplyOptions body with in-memory controller boundaries.

MSVC developer shell: python run.py --out <temporary-directory>
--without-fix removes only the new guard and proves the old exit(0) cannot pass.
No OpenRGB process, SDK, registry or hardware is used.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
from pathlib import Path
import re
import subprocess

def extract(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--without-fix', action='store_true')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    source = extract((repo/'cli.cpp').read_text(encoding='utf-8'), 'void ApplyOptions(DeviceOptions&')
    if args.without_fix:
        source, count = re.subn(r'    if\(!options\.hasOption\)\s*\{\s*return;\s*\}', '', source, count=1)
        if count != 1:
            raise SystemExit('Expected production guard not found')
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output/'apply-options.inc').write_text(source, encoding='utf-8', newline='\n')
    exe = output/('cli-baseline.exe' if args.without_fix else 'cli-save-only.exe')
    subprocess.run(['cl','/nologo','/std:c++17','/EHsc','/MD','/utf-8','/I'+str(output),
                    str(Path(__file__).with_name('test_cli.cpp').resolve()),
                    '/Fo'+str(exe.with_suffix('.obj')),'/Fe'+str(exe)],cwd=output,check=True)
    result = subprocess.run([str(exe)],cwd=output,capture_output=True,text=True,timeout=5)
    print(result.stdout,end='')
    if result.stderr:
        print(result.stderr,end='')
    # The original bug exits successfully before saving: return code is not
    # evidence of success. The sentinel can only appear after all assertions.
    if result.returncode or 'CLI_SAVE_ONLY_ALL_CHECKS_PASSED' not in result.stdout:
        raise SystemExit(f'Regression detected: process exit={result.returncode}, completion sentinel absent or test failed')

if __name__ == '__main__':
    main()
