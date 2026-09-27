"""Create six native shader presets from a local rainbow profile, offline."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
from pathlib import Path

PRESETS = {'Plasma': 'plasma', 'Feu': 'fire', 'Aurore': 'aurora',
           'Ondes': 'waves', 'Etoiles': 'stars', 'Bulles': 'bubbles'}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rainbow', required=True, type=Path)
    p.add_argument('--shaders', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    a = p.parse_args()
    source = json.loads(a.rainbow.read_text(encoding='utf-8-sig'))
    results = []
    for title, asset in PRESETS.items():
        profile = copy.deepcopy(source)
        profile['profile_name'] = 'Effet - ' + title
        profile['controllers'] = []
        effect = profile['plugins']['OpenRGB Effects Plugin']['Effects'][0]
        effect.update(CustomName=title, AutoStart=True, FPS=30, Brightness=100)
        settings = effect['CustomSettings']
        settings.update(use_audio=False, width=800, height=500)
        shader = settings['shader_program']
        shader.update(width=800, height=500, passes=[])
        shader['main_pass']['fragment_shader'] = (a.shaders / ('room-' + asset + '.fs')).read_text(encoding='utf-8')
        results.append(profile)
    a.out.mkdir(parents=True, exist_ok=False)
    for profile in results:
        (a.out / (profile['profile_name'] + '.json')).write_text(json.dumps(profile, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'Prepared {len(results)} profiles; no live settings changed')

if __name__ == '__main__':
    main()
