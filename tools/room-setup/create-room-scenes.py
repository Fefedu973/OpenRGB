"""Prepare room scene files offline, preserving routes and named source maps.

Inputs are machine-local native maps/configuration, never public device IDs.
The output directory must not exist. No process, SDK or registry is accessed.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
from collections import defaultdict
from pathlib import Path

IDENTITY = ('name', 'vendor', 'description', 'version', 'serial', 'location')
FULL = 'Full Scale.json'
MUSIC = 'Music - Tri Band.json'


def identity(controller):
    return tuple(controller[k] for k in IDENTITY)


def route(member):
    return identity(member['controller']) + (member['zone_idx'], member['is_segment'], member['segment_idx'])


def place(member, x, y, width, height):
    """Keep local LED wire geometry; replace only its placement transform."""
    s = member['settings']
    shape = s['custom_shape']
    s.update(x=x, y=y, scale=1, led_spacing=1, reverse=False, point_origin='cell')
    s['affine'] = dict(scale_x=width / shape['w'], scale_y=height / shape['h'],
                       rotation=0, flip_x=False, flip_y=False)


def music_map(source, configuration):
    result = copy.deepcopy(source)
    result['grid_settings'].update(auto_load=False, auto_register=False, w=320, h=200)
    controllers = {identity(c): c for c in configuration['controllers']}
    lanes = [defaultdict(list) for _ in range(3)]
    pulses = []
    surfaces = []
    for m in result['ctrl_zones']:
        name, z = m['controller']['name'], m['zone_idx']
        lane = None
        if name in ('Govee H61A2', 'Govee H6062'):
            lane = 0
        elif name == 'Govee H61E1':
            lane = 1
        elif name in ('Govee H61A0', 'ENE DRAM'):
            lane = 2
        elif name.startswith('ASUS ROG STRIX Z790') and z in (1, 3):
            lane = 1 if z == 1 else 2
        elif name == 'Nollie 32CH' and z >= 16:
            lane = (z - 16) % 3
        if lane is not None:
            lanes[lane][identity(m['controller']) + (z,)].append(m)
        elif name in ('KBHE 75HE', 'HYTE CNVS', 'Stream Deck Background Canvas', 'Wallpaper Screen 1'):
            surfaces.append(m)
        else:
            pulses.append(m)
    manifest = []
    for lane, groups in enumerate(lanes):
        for column, members in enumerate(groups.values()):
            x = lane * 320 / 3 + 8 + column * 90 / max(1, len(groups) - 1)
            c = controllers[identity(members[0]['controller'])]
            zone = c['zones'][members[0]['zone_idx']]
            total = zone['leds_count']
            for m in members:
                seg = zone['segments'][m['segment_idx']] if m['is_segment'] else dict(start_idx=0, leds_count=total)
                count, start = seg['leds_count'], seg['start_idx']
                old = m['settings']['custom_shape']['led_positions']
                if sorted(p['led_num'] for p in old) != list(range(count)):
                    raise ValueError('A VU member must describe every local LED exactly once')
                m['settings']['custom_shape'] = dict(w=1, h=count, led_positions=[
                    dict(led_num=i, x=0, y=count - 1 - i) for i in range(count)])
                place(m, x, 106 + (total - start - count) * 90 / total, 2, count * 90 / total)
                manifest.append(dict(label=m['custom_zone_name'], band=['bass','mid','treble'][lane], kind='VU'))
    # Each physical pulse object gets a stable cell in the bass upper region.
    # Broad surfaces span all three bands, providing their combined visualization.
    for i, m in enumerate(pulses):
        place(m, 5 + i % 5 * 19, 5 + i // 5 * 12, 15, 9)
        manifest.append(dict(label=m['custom_zone_name'], band='bass', kind='pulse'))
    for i, m in enumerate(surfaces):
        place(m, 4, 4 + i * 23, 312, 20)
        manifest.append(dict(label=m['custom_zone_name'], band='all', kind='surface'))
    if [route(m) for m in result['ctrl_zones']] != [route(m) for m in source['ctrl_zones']]:
        raise AssertionError('Routing changed')
    for old, new in zip(source['ctrl_zones'], result['ctrl_zones']):
        if sorted(p['led_num'] for p in old['settings']['custom_shape']['led_positions']) != sorted(p['led_num'] for p in new['settings']['custom_shape']['led_positions']):
            raise AssertionError('LED indices changed')
        s = new['settings']
        shape, affine = s['custom_shape'], s['affine']
        if not (0 <= s['x'] and 0 <= s['y'] and
                s['x'] + shape['w'] * affine['scale_x'] <= 320 and
                s['y'] + shape['h'] * affine['scale_y'] <= 200):
            raise ValueError('Inventory exceeds this music canvas; revise its placement plan')
    if len(surfaces) > 4 or len(pulses) > 40:
        raise ValueError('Inventory exceeds the upper music area; revise its placement plan')
    return result, manifest


def map_reference(profile, filename):
    profile['plugins']['OpenRGB Visual Map Plugin'] = dict(version=1, active_map=filename)
    effect = profile['plugins']['OpenRGB Effects Plugin']['Effects'][0]
    effect['ControllerZones'][0]['name'] = filename
    effect['AutoStart'] = True
    return effect


def make_scenes(rainbow, music):
    profiles = {}
    rainbow = copy.deepcopy(rainbow)
    map_reference(rainbow, FULL)
    profiles[rainbow['profile_name']] = rainbow
    for name, level in (('Full Blanc', '1.0'), ('Full Noir', '0.0')):
        p = copy.deepcopy(rainbow)
        p['profile_name'] = name
        e = map_reference(p, FULL)
        e.update(CustomName=name, FPS=10)
        settings = e['CustomSettings']
        settings.update(width=320, height=200)
        settings['shader_program'].update(width=320, height=200)
        settings['shader_program']['main_pass']['fragment_shader'] = (
            'void mainImage(out vec4 color, in vec2 pixel)\n{\n'
            f'    color = vec4(vec3({level}), 1.0);\n}}\n')
        profiles[name] = p
    p = copy.deepcopy(rainbow)
    p['profile_name'] = 'Ambilight - Web Page'
    e = map_reference(p, FULL)
    e.update(EffectClassName='WebPage', CustomName='Ambilight localhost 8443', FPS=20)
    e['CustomSettings'] = dict(url='https://localhost:8443', width=800, height=500, fps=20,
                               publish_frame=False, frame_channel='room-webpage', zone_regions=[])
    profiles[p['profile_name']] = p
    p = copy.deepcopy(music)
    p['profile_name'] = 'Music - Tri Band'
    e = map_reference(p, MUSIC)
    e['CustomSettings']['audio_settings']['audio_device'] = 2147483647
    profiles[p['profile_name']] = p
    return profiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('map', 'configuration', 'rainbow', 'music', 'out'):
        parser.add_argument('--' + flag, required=True, type=Path)
    args = parser.parse_args()
    read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
    source, configuration, rainbow, music = map(read, (args.map, args.configuration, args.rainbow, args.music))
    layout, manifest = music_map(source, configuration)
    profiles = make_scenes(rainbow, music)
    args.out.mkdir(parents=True, exist_ok=False)
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    write(args.out / 'maps' / MUSIC, layout)
    write(args.out / 'music-routing.json', manifest)
    for name, value in profiles.items():
        write(args.out / 'profiles' / (name + '.json'), value)
    print(f'Prepared {len(profiles)} profiles and {len(layout["ctrl_zones"])} music members; no live settings changed')


if __name__ == '__main__':
    main()
