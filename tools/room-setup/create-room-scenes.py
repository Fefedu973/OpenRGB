"""Prepare room scene files offline, preserving routes and named source maps.

Inputs are machine-local native maps/configuration, never public device IDs.
The output directory must not exist. No process, SDK or registry is accessed.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
from pathlib import Path

IDENTITY = ('name', 'vendor', 'description', 'version', 'serial', 'location')
FULL = 'Full Scale.json'
MUSIC = 'Music - Room Pulse.json'


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


def music_map(source, configuration, reference=None):
    """Room Pulse regions, optionally reusing an existing music arrangement."""
    result = copy.deepcopy(source)
    result['grid_settings'].update(auto_load=False, auto_register=False, w=320, h=200)
    controllers = {identity(c): c for c in configuration['controllers']}
    placements = {route(m): m['settings'] for m in (reference or {}).get('ctrl_zones', [])}
    manifest = []
    lamp = 0
    for m in result['ctrl_zones']:
        name, z = m['controller']['name'], m['zone_idx']
        if route(m) in placements:
            m['settings'] = copy.deepcopy(placements[route(m)])
            kind = 'reference music arrangement'
        elif name.startswith('Govee H61') and not name.startswith('Govee H6159') or name == 'Govee H6062' or (name.startswith('ASUS ROG STRIX Z790') and z in (1, 3)) or (name == 'Nollie 32CH' and z >= 16):
            c = controllers[identity(m['controller'])]
            zone = c['zones'][z]
            total = zone['leds_count']
            seg = zone['segments'][m['segment_idx']] if m['is_segment'] else dict(start_idx=0, leds_count=total)
            count, start = seg['leds_count'], seg['start_idx']
            old = m['settings']['custom_shape']['led_positions']
            if sorted(p['led_num'] for p in old) != list(range(count)):
                raise ValueError('A strip must describe every local LED exactly once')
            m['settings']['custom_shape'] = dict(w=count, h=1, led_positions=[dict(led_num=i, x=i, y=0) for i in range(count)])
            place(m, 52 + start * 264 / total, 188, count * 264 / total, 3)
            kind = 'frequency strip'
        elif name == 'ENE DRAM':
            place(m, 10, 32, 5, 151)
            kind = 'volume meter'
        elif 'Fan' in m['custom_zone_name'] or 'Corsair Lighting' in name or 'Strix LC' in name:
            place(m, 263, 3, 24, 24)
            kind = 'circular bass sector'
        elif name in ('KBHE 75HE', 'HYTE CNVS', 'Stream Deck Background Canvas', 'Wallpaper Screen 1'):
            place(m, 52, 33, 264, 148)
            kind = 'mirrored spectrum'
        else:
            x = 196 if lamp % 3 != 2 else 236
            place(m, x, 10, 8, 8)
            lamp += 1
            kind = 'bass or volume pulse'
        # The wallpaper is a screen: give it the complete main visualization.
        if name == 'Wallpaper Screen 1':
            place(m, 52, 33, 264, 148)
            kind = 'mirrored spectrum'
        manifest.append(dict(label=m['custom_zone_name'], kind=kind))
    if [route(m) for m in result['ctrl_zones']] != [route(m) for m in source['ctrl_zones']]:
        raise AssertionError('Routing changed')
    for old, new in zip(source['ctrl_zones'], result['ctrl_zones']):
        if sorted(p['led_num'] for p in old['settings']['custom_shape']['led_positions']) != sorted(p['led_num'] for p in new['settings']['custom_shape']['led_positions']):
            raise AssertionError('LED indices changed')
        s = new['settings']
        affine = s['affine']
        if affine.get('rotation', 0) or affine.get('flip_x') or affine.get('flip_y'):
            raise ValueError('Music reference must use axis-aligned positive placements')
        # Older maps placed their last cell's origin on the right boundary.
        # Fit its sample center inside the canvas without removing that LED.
        points = s['custom_shape']['led_positions']
        for axis, limit in (('x', 320), ('y', 200)):
            local_max = max(p[axis] + 0.5 for p in points)
            if s[axis] + local_max * affine['scale_' + axis] > limit:
                affine['scale_' + axis] = (limit - 0.5 - s[axis]) / local_max
        for point in s['custom_shape']['led_positions']:
            x = s['x'] + (point['x'] + 0.5) * affine['scale_x']
            y = s['y'] + (point['y'] + 0.5) * affine['scale_y']
            if not (0 <= x <= 320 and 0 <= y <= 200):
                raise ValueError('Music reference contains LED samples outside its canvas')
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
    p['profile_name'] = 'Music - Room Pulse'
    e = map_reference(p, MUSIC)
    e['CustomSettings']['audio_settings']['audio_device'] = 2147483647
    profiles[p['profile_name']] = p
    return profiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('map', 'configuration', 'rainbow', 'music', 'out'):
        parser.add_argument('--' + flag, required=True, type=Path)
    parser.add_argument('--reference-music-map', type=Path)
    args = parser.parse_args()
    read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
    source, configuration, rainbow, music = map(read, (args.map, args.configuration, args.rainbow, args.music))
    layout, manifest = music_map(source, configuration, read(args.reference_music_map) if args.reference_music_map else None)
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
