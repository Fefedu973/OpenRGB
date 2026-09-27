"""Prepare native effect and named Better scene profiles offline; preserve routing.

The destination must be new. No application, SDK, registry or device is touched.
Scene IDs and user routing remain in the requested private output, not this tool.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
import re
import uuid
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def make_profile(template, spec, name, scene='', follow=False, music_map=None):
    profile = copy.deepcopy(template)
    effects = profile['plugins']['OpenRGB Effects Plugin']['Effects']
    if len(effects) != 1:
        raise ValueError('A template must contain exactly one effect')
    effect = effects[0]
    previous = effect['CustomSettings']
    settings = dict(width=800, height=600, publish_frame=False,
                    frame_channel='room-shaders', zone_regions=previous.get('zone_regions', []),
                    show_rendering=False, invert_time=False,
                    use_audio=spec.get('audioReactive', False), preset=spec['id'],
                    schema_version=1,
                    parameters={control['key']: control['default'] for control in spec['controls']})
    if settings['use_audio'] and 'audio_settings' in previous:
        settings['audio_settings'] = copy.deepcopy(previous['audio_settings'])
    if spec.get('screenReactive'):
        if scene:
            scene = str(uuid.UUID(scene))
        settings['screen_source'] = dict(kind='better', connection_file='', channel='better-screen-capture',
                                         scene=scene, follow_better_appearance=follow,
                                         display=0, display_name='')
    effect.update(EffectClassName='SignalFavorite.' + spec['id'], CustomName=name,
                  FPS=60, Speed=1000, AutoStart=True, CustomSettings=settings)
    profile['profile_name'] = name
    if music_map:
        routes = effect['ControllerZones']
        if len(routes) != 1 or routes[0]['vendor'] != 'OpenRGB Visual Map Plugin':
            raise ValueError('A music map requires one existing Visual Map route')
        routes[0]['name'] = music_map
        profile['plugins']['OpenRGB Visual Map Plugin']['active_map'] = music_map
    return profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('template', 'presets', 'out'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--id', action='append', default=[])
    parser.add_argument('--scene', action='append', default=[], metavar='NAME=UUID')
    parser.add_argument('--music-template', type=Path)
    parser.add_argument('--music-map')
    args = parser.parse_args()
    if args.out.exists():
        raise FileExistsError('Refusing to overwrite prepared or live profiles')
    specs = {spec['id']: spec for spec in map(read, args.presets.glob('*.json'))}
    template = read(args.template)
    music = read(args.music_template) if args.music_template else template
    profiles = []
    for ident in args.id:
        spec = specs[ident]
        audio = spec.get('audioReactive', False)
        profiles.append(make_profile(music if audio else template, spec,
                                     'SignalRGB - ' + spec['title'],
                                     follow=ident == 'ScreenAmbience',
                                     music_map=args.music_map if audio else None))
    for requested in args.scene:
        name, ident = requested.rsplit('=', 1)
        profiles.append(make_profile(template, specs['ScreenAmbience'], 'Ambilight - ' + name,
                                     scene=ident, follow=True))
    filenames = set()
    for profile in profiles:
        name = profile['profile_name']
        if re.search(r'[<>:"/\\|?*\x00-\x1f]', name) or name.endswith(('.', ' ')) or len(name) > 120:
            raise ValueError('Invalid Windows profile filename')
        if name.casefold() in filenames:
            raise ValueError('Duplicate profile name')
        filenames.add(name.casefold())
    args.out.mkdir(parents=True, exist_ok=False)
    for profile in profiles:
        (args.out / (profile['profile_name'] + '.json')).write_text(
            json.dumps(profile, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Prepared {len(profiles)} native profiles; original templates and maps unchanged')


if __name__ == '__main__':
    main()
