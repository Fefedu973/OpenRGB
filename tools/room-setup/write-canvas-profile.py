"""Write a plugin-only spatial shader profile from one actual native descriptor.

Offline only: no SDK, registry, process or hardware calls. Existing files are
never overwritten. The selected controller identity must be unique.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
from pathlib import Path

IDENTITY = ('name', 'vendor', 'description', 'version', 'serial', 'location')
RAINBOW = """// One continuous spatial field for the entire selected canvas.
void mainImage(out vec4 color, in vec2 pixel)
{
    vec2 uv = pixel / iResolution.xy;
    float hue = fract(uv.x + 0.30 * uv.y - iTime * 0.04);
    color = vec4(HSVToRGB(vec3(hue, 0.90, 0.82)), 1.0);
}
"""


def make_profile(export, controller_name, profile_name='Full Scale - Rainbow',
                 width=800, height=500, fps=30, zone_idx=0, serial=None):
    if export.get('profile_version') != 7:
        raise ValueError('An actual native profile_version 7 descriptor export is required')
    candidates = [c for c in export.get('controllers', []) if c.get('name') == controller_name
                  and (serial is None or c.get('serial') == serial)]
    if len(candidates) != 1:
        raise ValueError('Controller identity is missing or ambiguous')
    controller = candidates[0]
    if any(not isinstance(controller.get(k), str) for k in IDENTITY):
        raise ValueError('Incomplete native identity')
    for value in (width, height, fps, zone_idx):
        if isinstance(value, bool) or not isinstance(value, int):
            raise ValueError('Dimensions, FPS and zone must be integers')
    if not (16 <= width <= 4096 and 16 <= height <= 4096 and width*height <= 16777216 and 1 <= fps <= 60):
        raise ValueError('Canvas dimensions or FPS exceed the bounded preset limits')
    zones = controller.get('zones', [])
    if not 0 <= zone_idx < len(zones) or zones[zone_idx].get('leds_count', 0) <= 0:
        raise ValueError('Selected native zone is missing or empty')
    if not isinstance(profile_name, str) or not profile_name.strip():
        raise ValueError('Profile name is empty')
    target = {k: controller[k] for k in IDENTITY}
    target.update(zone_idx=zone_idx, is_segment=False, segment_idx=-1, reverse=False, self_brightness=100)
    program = {'main_pass': {'type': 2, 'fragment_shader': RAINBOW, 'texture_path': ''},
               'passes': [], 'version': '110', 'width': width, 'height': height}
    effect = {'EffectClassName': 'Shaders', 'CustomName': 'Spatial rainbow canvas',
              'FPS': fps, 'Speed': 1000, 'Slider2Val': 1, 'RandomColors': False,
              'AllowOnlyFirst': False, 'Brightness': 100, 'Temperature': 0, 'Tint': 0,
              'UserColors': [16777215], 'AutoStart': True, 'SelectAll': False,
              'ControllerZones': [target], 'CustomSettings': {
                  'width': width, 'height': height, 'shader_program': program,
                  'publish_frame': False, 'frame_channel': 'room-shaders', 'zone_regions': [],
                  'show_rendering': False, 'invert_time': False, 'use_audio': False}}
    # Do not copy saved device colors/modes or set base_color. Only Effects owns
    # the selected canvas; its members receive the shared spatial frame.
    return {'profile_version': 7, 'profile_name': profile_name, 'controllers': [],
            'plugins': {'OpenRGB Effects Plugin': {'version': 2, 'Effects': [effect]}}}


def autoload_settings(settings, profile_name):
    """Return a copy preserving clients, credentials and unrelated preferences."""
    result = copy.deepcopy(settings)
    manager = result.setdefault('ProfileManager', {})
    for key in ('open_profile', 'resume_profile'):
        manager[key] = {'enabled': True, 'name': profile_name}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native-export', required=True, type=Path)
    p.add_argument('--controller-name', required=True)
    p.add_argument('--serial')
    p.add_argument('--profile-name', default='Full Scale - Rainbow')
    p.add_argument('--width', type=int, default=800)
    p.add_argument('--height', type=int, default=500)
    p.add_argument('--fps', type=int, default=30)
    p.add_argument('--zone', type=int, default=0)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    result = make_profile(json.loads(args.native_export.read_bytes()), args.controller_name,
                          args.profile_name, args.width, args.height, args.fps, args.zone, args.serial)
    with args.out.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(result, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print('Prepared one spatial shader on one explicit controller zone; no hardware accessed')


if __name__ == '__main__':
    main()
