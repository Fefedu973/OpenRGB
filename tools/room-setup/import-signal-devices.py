"""Offline Signal layout/component inventory and native configuration compiler.

No registry, network, RGB SDK, credentials or hardware imports. Generated plans
contain private device identifiers: keep them outside version control.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import hashlib
import ipaddress
import json
import math
import re
from pathlib import Path

PROFILE_FILE = Path(__file__).with_name('device-profiles.json')
IDENTITY = ('type', 'name', 'description', 'version', 'serial', 'location')
SIZE, TYPE, SEGMENTS = 1 << 1, 1 << 3, 1 << 5
SET_SIZE, SET_TYPE, SET_SEGMENTS = 1 << 12, 1 << 14, 1 << 16
FIXED_DRIVERS = {
    '046d:c547:': ('logitech_hidpp', 'Controllers/LogitechController'),
    '0b05:879e:': ('asus_aio', 'Controllers/AsusAuraUSBController'),
    '187c:101d:': ('alienware_monitor', 'Controllers/AlienwareMonitorController'),
    '3402:0b01:': ('hyte_cnvs', 'Controllers/HYTEMousematController'),
    '9172:0002:': ('kbhe', 'Controllers/KBHEController'),
    'I2CBUS:': ('dram_native_enumeration_required', 'Controllers/ENESMBusController'),
    'govee-ble:': ('govee_bluetooth', 'Controllers/GoveeBluetoothController'),
    'nvidia-': ('nvidia_illumination', 'Controllers/NVIDIAIlluminationController'),
    'streamdeck-': ('streamdeck_image', 'Controllers/StreamDeckBackgroundController'),
    'signalrgb-desktop-wallpaper-': ('external_image_sink_required', 'Controllers/VirtualScreenController'),
}
MAPPING_EVIDENCE = {
    'contract': 'LedMapping[coordinate_index] is the component wire LED index',
    'signal_docs': 'https://docs.signalrgb.com/developer/plugins/component-structure/',
    'editor_source': 'https://github.com/qiangqiang101/Nollie-SignalRGB-Component-Editor/blob/733bd88860d36e1a30e926891be1f378eb215691/SignalRGB-CompGen/frmMain.vb',
    'visual_map_import': 'https://github.com/qiangqiang101/Nollie-SignalRGB-Component-Editor/blob/733bd88860d36e1a30e926891be1f378eb215691/SignalRGB-CompGen/frmImport.vb',
}


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_new(path, data):
    with Path(path).open('x', encoding='utf-8', newline='\n') as f:
        json.dump(data, f, ensure_ascii=False, indent=2, allow_nan=False)
        f.write('\n')


def eligible(device):
    return device.get('active') is True and device.get('enabled') is True


def component_record(device, snapshot):
    matches = [c for c in snapshot.get('components', {}).get(device['id'], [])
               if c.get('parent_device_id') == device.get('parentId')
               and c.get('channel') == device.get('channel')]
    if not matches:
        return None
    if any(c.get('LedMapping') != matches[0].get('LedMapping') or
           c.get('LedCoordinates') != matches[0].get('LedCoordinates') for c in matches[1:]):
        raise ValueError('Conflicting component geometry for ' + device['id'])
    return matches[0]


def geometry(device, snapshot, start=0):
    points = copy.deepcopy(device.get('leds', []))
    record = component_record(device, snapshot)
    mapping = record.get('LedMapping') if record else None
    if mapping is not None and sorted(mapping) != list(range(len(points))):
        raise ValueError('Component LedMapping is not a permutation: ' + device['id'])
    identity = mapping is None or mapping == list(range(len(points)))
    wire_points = [dict(p, wire_index=(mapping[i] if mapping is not None else i), coordinate_index=i)
                   for i, p in enumerate(points)]
    return {'source_id': device['id'], 'name': device.get('name', ''),
            'enabled': eligible(device), 'start_idx': start, 'leds_count': len(points),
            'base_size': copy.deepcopy(device.get('baseSize')), 'points': points,
            'signal_led_mapping': mapping,
            'coordinate_to_wire_status': 'identity' if identity else 'sourced_coordinate_to_wire',
            'wire_points': sorted(wire_points, key=lambda p: p['wire_index']),
            'mapping_evidence': None if identity else MAPPING_EVIDENCE,
            'notes': device.get('notes', [])}


def build_plan(project, metadata, snapshot, profiles=None):
    profiles = profiles or read_json(PROFILE_FILE)
    devices = project['devices']
    if len({d['id'] for d in devices}) != len(devices):
        raise ValueError('Duplicate source device IDs')
    active = [d for d in devices if eligible(d)]
    controllers = {}
    fragments = []
    excluded = [{'source_id': d['id'], 'name': d.get('name'),
                 'reason': d.get('excludedReason') or 'disabled_or_not_present'}
                for d in devices if not eligible(d)]

    def controller(source_id, family='unmatched'):
        if source_id not in controllers:
            m = metadata.get(source_id, {})
            controllers[source_id] = {
                'source_id': source_id, 'name': m.get('name', source_id), 'family': family,
                'status': 'requires_native_enumeration_and_binding', 'zones': [],
                'source_elements': [], 'notes': []}
        return controllers[source_id]

    # Current channel components have an explicit order. Disabled middle
    # components consume wire addresses even though they have no active placement.
    group_keys = {(d['parentId'], d['channel']) for d in active
                  if d.get('parentId') and d.get('channel') and d.get('order') is not None}
    for parent, channel in sorted(group_keys):
        members = sorted([d for d in devices if d.get('parentId') == parent
                          and d.get('channel') == channel and d.get('order') is not None],
                         key=lambda d: d['order'])
        if len({d['order'] for d in members}) != len(members):
            raise ValueError('Ambiguous component order: ' + parent + '/' + channel)
        usb = profiles['usb'].get(parent[:9].lower())
        model_match = re.search(r'\bH[0-9A-Z]{4}\b', str(metadata.get(parent, {}).get('name', '')))
        model = model_match.group(0) if model_match else None
        if channel.startswith('Govee [') and model in profiles['govee_lan']:
            family, zone_name, zone_idx = 'govee_lan', 'Govee Strip', 0
        elif usb:
            number = int(re.search(r'(\d+)$', channel).group(1))
            family, zone_name = usb['family'], usb['channel_prefix'] + str(number)
            zone_idx = number if family == 'asus_aura' else number - 1
            if family == 'corsair_core' and number > usb['physical_channels']:
                raise ValueError('Lighting Node CORE has only one physical output')
        else:
            family, zone_name, zone_idx = 'unmatched', None, None
        c = controller(parent, family)
        offset, components = 0, []
        for d in members:
            component = geometry(d, snapshot, offset)
            components.append(component)
            offset += component['leds_count']
            if eligible(d):
                c['source_elements'].append(d['id'])
        if usb and usb.get('max_leds') is not None and offset > usb['max_leds']:
            raise ValueError('Configured chain exceeds controller capacity: ' + channel)
        if family == 'govee_lan':
            if offset != profiles['govee_lan'][model]:
                raise ValueError('Govee segment count differs from native model table')
            m = metadata[parent]
            address = str(ipaddress.IPv4Address(m['currentIp']))
            mac = m['stableDeviceId']
            if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){7}[0-9a-fA-F]{2}', mac):
                raise ValueError('Govee device ID is not the stable 8-byte discovery identity')
            fragments.append({'ip': address, 'mac': mac})
            c['model'] = model
            c['stable_device_id'] = mac
            c['current_ip'] = address
        c['zones'].append({'source_channel': channel, 'native_name': zone_name,
                           'native_index_hint': zone_idx, 'leds_count': offset,
                           'action': 'configure_segments' if zone_name else 'manual_binding_required',
                           'components': components})

    handled = {s for c in controllers.values() for s in c['source_elements']}
    for d in active:
        if d['id'] in handled:
            continue
        source_id = d['id']
        suffix = source_id.rsplit(':', 1)[-1]
        strimer = profiles['nollie_strimer'].get(suffix) if source_id.startswith('3061:4714:') else None
        if strimer:
            parent = source_id.rsplit(':', 1)[0]
            c = controller(parent, 'nollie32')
            count = strimer['leds_per_row']
            g = geometry(d, snapshot)
            if len(g['points']) != count * strimer['rows']:
                raise ValueError('Unexpected Strimer geometry')
            for row in range(strimer['rows']):
                part = copy.deepcopy(g)
                part.update(start_idx=0, leds_count=count, source_led_start=row * count,
                            points=g['points'][row * count:(row + 1) * count])
                part['wire_points'] = [dict(p, wire_index=i) for i,p in enumerate(g['wire_points'][row*count:(row+1)*count])]
                c['zones'].append({'source_channel': suffix + '/' + str(row + 1),
                                  'native_name': strimer['name_prefix'] + str(row + 1),
                                  'native_index_hint': strimer['first_zone'] + row,
                                  'usb_channel': strimer['usb_channels'][row],
                                  'leds_count': count, 'action': 'configure_segments', 'components': [part]})
            c['source_elements'].append(source_id)
            continue
        parent = source_id.split(':12V Header')[0] if ':12V Header' in source_id else source_id
        c = controller(parent)
        for prefix, (family, driver) in FIXED_DRIVERS.items():
            if source_id.startswith(prefix):
                c.update(family=family, expected_driver=driver)
                break
        if re.search(r'\bH6008\b', str(metadata.get(source_id, {}).get('name', ''))):
            c.update(family='govee_bluetooth', expected_driver='Controllers/GoveeBluetoothController')
        c['source_elements'].append(source_id)
        g = geometry(d, snapshot)
        # Fixed-size devices need their real native layout; do not guess zones,
        # LED permutations, firmware version, SMBus identities or image routing.
        c['zones'].append({'source_channel': source_id, 'native_name': None,
                           'action': 'verify_fixed_or_image_mapping', 'leds_count': len(g['points']),
                           'components': [g]})
        if parent.startswith('0b05:19af:'):
            c['notes'].append('Onboard LEDs and 12V header share native Aura Mainboard fixed zone; confirm total and offsets after enumeration.')
        elif source_id.startswith('I2CBUS:'):
            c['notes'].append('RAM dimensions are inferred; native SMBus enumeration and LED count must be verified. Do not resize fixed RAM from this snapshot.')
        elif source_id.startswith('187c:101d:'):
            c['notes'].append('Signal uses one global sample. Native AW3426DW has two zones: explicit replication or independent placement is required.')
        elif source_id.startswith(('streamdeck-', 'signalrgb-desktop-wallpaper-')):
            c['notes'].append('Image surface: sample-marker count is not native pixel resolution. Bind through generic image routing, not a one-LED-per-pixel matrix.')
        elif source_id.startswith('nvidia-'):
            c['notes'].append('Native RGB/RGBW and monochrome zone capabilities differ; preserve native types.')
        elif c['family'] == 'govee_bluetooth':
            c['notes'].append('Use the separate private Govee Bluetooth config importer. Never add these devices to the LAN per-segment fallback.')

    # Aura's direct channel 4 concatenates onboard LEDs and then each 12 V
    # header. Keep their different placements as logical segments, never as
    # overlapping whole-zone members. Include disabled sources in wire offsets.
    for parent, c in controllers.items():
        profile = profiles['usb'].get(parent[:9].lower(), {}).get('fixed_zone')
        if not profile:
            continue
        if profile['order'] != 'onboard_then_12v_headers':
            raise ValueError('Unknown fixed zone ordering profile')
        onboard = [d for d in devices if d['id'] == parent]
        headers = [(int(match.group(1)), d) for d in devices
                   if (match := re.fullmatch(re.escape(parent) + r':12V Header (\d+)', d['id']))]
        if not onboard or not any(eligible(d) for d in onboard + [d for _, d in headers]):
            continue
        headers.sort(key=lambda p: p[0])
        if [n for n, _ in headers] != list(range(1, len(headers)+1)):
            raise ValueError('Fixed header numbering must be contiguous')
        offset, components = 0, []
        for d in onboard + [d for _, d in headers]:
            part = geometry(d, snapshot, offset)
            if part['leds_count'] <= 0:
                raise ValueError('Fixed zone component has no proven LED geometry')
            components.append(part)
            offset += part['leds_count']
        source_ids = {part['source_id'] for part in components}
        c['zones'] = [z for z in c['zones'] if not any(part['source_id'] in source_ids for part in z['components'])]
        c['zones'].append({'source_channel': 'Onboard + 12V', 'native_name': profile['name'],
                           'native_index_hint': profile['index'], 'leds_count': offset, 'fixed_size': True,
                           'action': 'configure_segments', 'components': components})
        c['notes'] = [n for n in c['notes'] if not n.startswith('Onboard LEDs and 12V header share')]
        c['notes'].append('Onboard then 12V ordering is shared by Signal SendMainBoardLeds and native AuraMainboard channel4. Native fixed count must match exactly; no resize is permitted.')

    layouts = [{'name': layout['name'], 'readOnly': layout.get('readOnly', False),
                'brightness': layout.get('brightness', 100),
                'entries': {d['id']: copy.deepcopy(layout.get('entries', {}).get(d['id'])) for d in active}}
               for layout in project.get('layouts', [])]
    result = {'schema_version': 1, 'private_identifiers': True,
              'scope': 'offline evidence and configuration proposal; not hardware validation',
              'canvas': project.get('canvas'), 'selected_layout': project.get('selected'),
              'summary': {'active_elements': len(active), 'sample_points': sum(len(d.get('leds', [])) for d in active),
                          'native_controller_groups': len(controllers), 'govee_lan_devices': len(fragments)},
              'controllers': list(controllers.values()), 'layouts': layouts, 'excluded': excluded,
              'expected_not_observed': [{'name': 'Logitech G915', 'source': 'user statement',
                                         'reason': 'No current enabled device identity; detect before binding. G502 X PLUS coexists.'}],
              'geometry_caution': 'Wire indices follow the sourced Signal component convention. Coincident coordinates must retain multiple LED routes; they cannot fit a one-index-per-cell native matrix.'}
    wallpaper = [d['id'] for d in active if d['id'].startswith('signalrgb-desktop-wallpaper-')]
    result['external_connection_proposals'] = []
    if wallpaper:
        result['external_connection_proposals'].append({
            'source_ids': wallpaper, 'transport':'openrgb_sdk_legacy', 'applied':False,
            'host':'127.0.0.1', 'port':6743, 'protocol_fixture_negotiated':4,
            'canonical_max_matrix_cells':16381,
            'settings_fragment_for_review_only':{'Client':{'clients':[{'ip':'127.0.0.1','port':6743}]}},
            'warning':'Saved Client entries auto-connect; do not merge this proposal until the wallpaper SDK server and selected screen source are enabled intentionally.',
            'identity_from_source_not_live_enumeration':{'vendor':'SignalRGB Wallpaper Bridge',
                'description':'Virtual wallpaper-glow device','version':'1.6.2-beta', 'serial':'','location':'bridge'},
            'name_and_dimensions_require_live_enumeration':True,
            'source':'https://github.com/Delido/signalrgb-wallpaper/blob/2d1099eeaf1f59c1693e22503ba95d7a50fc6603/wallpaper_bridge/openrgb_server.py'})
    return result, {'GoveeDevices': {'devices': fragments}}


def segment_geometry(component):
    """Return a lossless native matrix when representable, otherwise a reason.

    Coincident front/back LEDs are preserved as separate wire_points for Visual
    Map custom shapes; silently overwriting one matrix cell would lose LEDs.
    """
    points = component.get('wire_points')
    size = component.get('base_size')
    if not points or not size or len(size) != 2:
        return None, 'geometry_missing'
    w, h = size
    if any(isinstance(v, bool) or not isinstance(v, (int, float)) or int(v) != v or v < 1 for v in size):
        return None, 'noninteger_canvas_use_custom_shape'
    w, h = int(w), int(h)
    if w*h > 16381:
        return None, 'matrix_exceeds_legacy_sdk_limit_use_custom_shape'
    cells = [0xFFFFFFFF]*(w*h)
    if sorted(p['wire_index'] for p in points) != list(range(component['leds_count'])):
        raise ValueError('Wire geometry is not a complete permutation')
    for p in points:
        x, y = p['x'], p['y']
        if any(isinstance(v, bool) or not isinstance(v, (int, float)) or int(v) != v for v in (x,y)):
            return None, 'noninteger_coordinates_use_custom_shape'
        x, y = int(x), int(y)
        if not (0 <= x < w and 0 <= y < h):
            return None, 'out_of_canvas_coordinates_use_custom_shape'
        if cells[y*w+x] != 0xFFFFFFFF:
            return None, 'coincident_leds_require_custom_shape'
        cells[y*w+x] = p['wire_index']
    return {'width':w, 'height':h, 'map':cells}, None


def normalized(value):
    return re.sub('[^a-z0-9]', '', str(value).lower())


def propose_bindings(plan, native_export):
    """Suggestions only, never execute or install. Require unique strong identity.

    Names/model counts alone are insufficient. USB serial, USB instance path,
    stable Govee ID, explicit COM port or SMBus address are the available evidence.
    All resulting selectors are copied verbatim from the native export.
    """
    natives = native_export.get('controllers')
    if not isinstance(natives, list):
        raise ValueError('Native export must contain controllers')
    proposals, unresolved, identities = [], [], []
    for source in plan['controllers']:
        sid = source['source_id']
        usb = re.match(r'^([a-fA-F0-9]{4}):([a-fA-F0-9]{4}):(.+)$', sid)
        candidates = []
        for native in natives:
            reason = None
            if any(k not in native for k in IDENTITY):
                continue
            serial = normalized(native['serial'])
            location = str(native['location'])
            if source.get('stable_device_id') and serial == normalized(source['stable_device_id']):
                reason = 'exact_stable_govee_identity'
            elif usb:
                vid,pid,source_serial = usb.groups()
                if sid.startswith('3402:0b01:') and re.search(r':COM\d+$', sid):
                    if re.sub(r'^\\\\\.\\', '', location).upper() == sid.rsplit(':',1)[1].upper():
                        reason = 'same_observed_hyte_com_port'
                elif len(normalized(source_serial)) >= 6 and serial == normalized(source_serial):
                    reason = 'exact_usb_serial'
                else:
                    loc = location.lower()
                    instance = normalized(source_serial)
                    if ('vid_'+vid.lower()) in loc and ('pid_'+pid.lower()) in loc and len(instance)>=8 and instance in normalized(loc):
                        reason = 'same_usb_vid_pid_and_pnp_instance'
            elif sid.startswith('I2CBUS:') and source['family']=='dram_native_enumeration_required':
                address=int(sid.split(':',1)[1])
                if location.startswith('I2C: ') and re.search(r',\s*address\s+0x'+format(address,'02x')+r'\s*$', location, flags=re.I):
                    reason = 'same_smbus_address_requires_count_confirmation'
            elif source['family']=='streamdeck_image' and native['serial']=='room-streamdeck-background-mk2':
                reason = 'native_driver_explicit_mk2_canvas_identity'
            if reason:
                candidates.append((native,reason))
        if len(candidates) != 1:
            unresolved.append({'source_id':sid,'reason':'ambiguous_identity' if candidates else 'no_strong_identity_match',
                               'candidate_count':len(candidates)})
            continue
        native, reason = candidates[0]
        selector = {k:native[k] for k in IDENTITY}
        identities.append({'source_id':sid, 'selector':selector, 'evidence':reason,
                           'action':'review_only', 'family':source['family']})
        zones = []
        for z in source['zones']:
            if z['action'] != 'configure_segments':
                continue
            indexes = [i for i,nz in enumerate(native.get('zones') or []) if nz['name']==z['native_name']]
            if len(indexes)==1:
                zones.append({'source_channel':z['source_channel'], 'native_zone':indexes[0]})
            else:
                unresolved.append({'source_id':sid,'source_channel':z['source_channel'],
                                   'reason':'native_zone_missing_or_ambiguous'})
        if zones:
            proposals.append({'source_id':sid,'selector':selector,'zones':zones})
    return {'controllers':proposals}, {'identity_proposals':identities, 'unresolved':unresolved, 'applied':False}


def compile_configuration(plan, native_export, bindings):
    """Copy only descriptions from a real native export; never invent identities.

    Binding selectors must include all native identity fields. Only explicitly
    selected source controllers/zones are changed; all other export data survives.
    Plan geometries are separate and are not mistaken for verified native maps.
    """
    if native_export.get('profile_version') != 7 or not isinstance(native_export.get('controllers'), list):
        raise ValueError('Expected a native JSON description export with profile_version 7')
    result = copy.deepcopy(native_export)
    result['profile_name'] = 'Controller Configuration'
    seen_sources, edited = set(), set()
    plans = {c['source_id']: c for c in plan['controllers']}
    report = []
    for binding in bindings.get('controllers', []):
        sid = binding['source_id']
        if sid in seen_sources or sid not in plans:
            raise ValueError('Duplicate or unknown source controller binding')
        seen_sources.add(sid)
        selector = binding.get('selector')
        if not isinstance(selector, dict) or set(selector) != set(IDENTITY):
            raise ValueError('Binding requires complete native identity: ' + ', '.join(IDENTITY))
        candidates = [i for i, c in enumerate(result['controllers']) if all(c.get(k) == selector[k] for k in IDENTITY)]
        if len(candidates) != 1:
            raise ValueError('Native binding is missing or ambiguous')
        ci = candidates[0]
        controller = result['controllers'][ci]
        zones = controller.get('zones') or []
        pzones = {z['source_channel']: z for z in plans[sid]['zones']}
        selected = binding.get('zones', [])
        if not selected:
            raise ValueError('Binding must explicitly select at least one source channel')
        for zbind in selected:
            pzone = pzones[zbind['source_channel']]
            if pzone['action'] != 'configure_segments':
                raise ValueError('This fixed/image source needs a verified mapping; automatic resizing is forbidden')
            zi = zbind['native_zone']
            if isinstance(zi, bool) or not isinstance(zi, int) or zi < 0 or zi >= len(zones) or (ci, zi) in edited:
                raise ValueError('Invalid or duplicate target zone')
            zone = zones[zi]
            if zone['name'] != pzone['native_name']:
                raise ValueError('Native zone name does not match sourced profile')
            count = pzone['leds_count']
            if not isinstance(count, int) or count < 0 or not zone['leds_min'] <= count <= zone['leds_max']:
                raise ValueError('Requested LED count exceeds native zone bounds')
            flags = zone['flags']
            if pzone.get('fixed_size') and not count == zone['leds_count'] == zone['leds_min'] == zone['leds_max']:
                raise ValueError('Fixed zone source geometry must match the actual native fixed count')
            if count != zone['leds_count']:
                if not flags & SIZE:
                    raise ValueError('Native zone does not permit resizing')
                zone['leds_count'] = count
                flags |= SET_SIZE
            if not flags & SEGMENTS or not flags & TYPE:
                raise ValueError('Native zone does not permit segment/type configuration')
            components = pzone['components']
            expected = 0
            segments = []
            for component in components:
                if component['start_idx'] != expected or component['leds_count'] <= 0:
                    raise ValueError('Component wire offsets must be contiguous, including disabled components')
                matrix, geometry_note = segment_geometry(component)
                segments.append({'name': component['name'], 'type': 2 if matrix else 1,
                                 'start_idx': expected, 'leds_count': component['leds_count'],
                                 'matrix_map': matrix or {'width': 0, 'height': 0, 'map': []}, 'flags': 0})
                expected += component['leds_count']
            if expected != count:
                raise ValueError('Component lengths differ from configured zone size')
            zone.update(type=6, segments=segments, matrix_map={'width': 0, 'height': 0, 'map': []},
                        flags=(flags | SET_TYPE | SET_SEGMENTS) & ~(1 << 15))
            edited.add((ci, zi))
            report.append({'source_id': sid, 'source_channel': pzone['source_channel'],
                           'native_zone': zi, 'leds_count': count, 'segments': len(segments),
                           'custom_shapes_required': [{'source_id': c['source_id'], 'reason': segment_geometry(c)[1]}
                                                      for c in components if segment_geometry(c)[1]]})
    if not edited:
        raise ValueError('No explicitly bound native zones; no configuration produced')
    # ProfileManager builds temporary RGBController objects from JSON and calls
    # SetupColors. Keep the LED metadata large enough for every rebuilt zone;
    # copying an old shorter LEDs array would leave out-of-bounds zone pointers.
    for ci in {c for c, _ in edited}:
        original = native_export['controllers'][ci]
        target = result['controllers'][ci]
        previous_leds = original.get('leds') or []
        previous_colors = original.get('colors') or []
        previous_names = original.get('led_display_names') or []
        new_leds, new_colors, new_names, offset = [], [], [], 0
        for old_zone, zone in zip(original['zones'], target['zones']):
            for i in range(zone['leds_count']):
                in_old = i < old_zone['leds_count']
                new_leds.append(copy.deepcopy(previous_leds[offset+i]) if in_old and offset+i < len(previous_leds)
                                else {'name': zone['name'] + ', LED ' + str(i+1)})
                new_colors.append(previous_colors[offset+i] if in_old and offset+i < len(previous_colors) else 0)
                new_names.append(previous_names[offset+i] if in_old and offset+i < len(previous_names) else '')
            offset += old_zone['leds_count']
        target.update(leds=new_leds, colors=new_colors, led_display_names=new_names)
    return result, {'configured_zones': report, 'unbound_source_controllers': sorted(set(plans) - seen_sources),
                    'layout_imported': False, 'spatial_mapping': 'retained in plan; not yet applied to Visual Map'}


def export_visualmap(plan, native_export, bindings, layout_name='Full Scale'):
    """Prepare an inactive map for exact, explicitly bound component segments.

    Compile first so segment indexes/counts are the actual proposed native
    configuration, including disabled components that retain their wire offset.
    Fixed/image devices are reported as omissions, never guessed by name.
    """
    configured, _ = compile_configuration(plan, native_export, bindings)
    layouts = [layout for layout in plan['layouts'] if layout['name'] == layout_name]
    if len(layouts) != 1:
        raise ValueError('Requested source layout missing or ambiguous')
    layout = layouts[0]
    source_entries = layout['entries']
    planned = {controller['source_id']: controller for controller in plan['controllers']}
    entries, imported, per_source = [], set(), {}

    def finite(value, description, positive=False):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError('Invalid finite ' + description)
        if positive and value <= 0:
            raise ValueError('Invalid positive ' + description)
        return value

    layout_gain = finite(layout.get('brightness', 100), 'layout brightness') / 100
    if not 0 <= layout_gain <= 1:
        raise ValueError('Layout brightness must be 0..100')
    for binding in bindings['controllers']:
        controller = next(c for c in configured['controllers']
                          if all(c.get(key) == value for key, value in binding['selector'].items()))
        identity_keys = ('name', 'vendor', 'description', 'version', 'serial', 'location')
        if any(key not in controller for key in identity_keys):
            raise ValueError('Visual Map needs actual exported vendor and complete controller identity')
        identity = {key: controller[key] for key in identity_keys}
        pzones = {z['source_channel']: z for z in planned[binding['source_id']]['zones']}
        for zone_binding in binding['zones']:
            pzone = pzones[zone_binding['source_channel']]
            zi = zone_binding['native_zone']
            native_zone = controller['zones'][zi]
            for si, component in enumerate(pzone['components']):
                source_id = component['source_id']
                if not component['enabled'] or source_id not in source_entries:
                    continue
                if not isinstance(source_entries[source_id], dict):
                    continue # Active source with no saved placement remains explicitly unbound.
                # Do not rely on VisualMap's resizeCustomShape fallback: every
                # segment receives exactly one complete wire-index permutation.
                count = component['leds_count']
                segment = native_zone['segments'][si]
                if segment['leds_count'] != count or segment['start_idx'] != component['start_idx']:
                    raise ValueError('Native segment offset/count does not match the source component')
                points = component.get('wire_points', [])
                if len(points) != count or sorted(p['wire_index'] for p in points) != list(range(count)):
                    raise ValueError('Visual Map requires complete wire geometry')
                w, h = component['base_size']
                finite(w, 'shape width', True); finite(h, 'shape height', True)
                transform = source_entries[source_id]
                sx = finite(transform.get('scale', {}).get('x', 1), 'scale X', True)
                sy = finite(transform.get('scale', {}).get('y', 1), 'scale Y', True)
                x = finite(transform.get('x', 0), 'position X')
                y = finite(transform.get('y', 0), 'position Y')
                rotation = finite(transform.get('rotation', 0), 'rotation')
                gain = finite(transform.get('brightness', 100), 'member brightness') / 100
                if not 0 <= gain <= 1:
                    raise ValueError('Member brightness must be 0..100')
                flips = [transform.get('flipped', False), transform.get('flippedV', False)]
                if any(not isinstance(value, bool) for value in flips):
                    raise ValueError('Mirror flags must be boolean')
                settings = {
                    'shape': 2, 'x': x, 'y': y, 'scale': 1, 'led_spacing': 1, 'reverse': False,
                    'affine': {'scale_x': sx, 'scale_y': sy, 'rotation': rotation,
                               'flip_x': flips[0], 'flip_y': flips[1]},
                    'point_origin': 'cell', 'brightness': gain * layout_gain,
                    'custom_shape': {'w': w, 'h': h, 'led_positions': [
                        {'led_num': point['wire_index'], 'x': finite(point['x'], 'LED X'),
                         'y': finite(point['y'], 'LED Y')} for point in points]},
                }
                entries.append({'controller': copy.deepcopy(identity), 'zone_idx': zi,
                                'is_segment': True, 'segment_idx': si,
                                'custom_zone_name': component['name'], 'settings': settings})
                imported.add(source_id)
                per_source.setdefault(source_id, []).append({'zone_idx': zi, 'segment_idx': si, 'leds': count,
                                                           'zone_wire_start': component['start_idx'],
                                                           'source_led_start': component.get('source_led_start', 0)})

    w, h = plan['canvas']['width'], plan['canvas']['height']
    if any(isinstance(v, bool) or not isinstance(v, int) or v <= 0 for v in (w, h)):
        raise ValueError('Visual Map canvas dimensions must be positive integers')
    result = {'ctrl_zones': entries,
              'grid_settings': {'w': w, 'h': h, 'show_grid': False, 'show_bounds': True,
                                'grid_size': 1, 'snap_to_grid': False, 'auto_load': False,
                                'auto_register': False, 'hide_members': False}}
    report = {
        'source_layout': layout_name, 'source_read_only': layout.get('readOnly', False),
        'activation': 'none; auto_load and auto_register are false',
        'requires': 'OpenRGB Room compiled component Configuration.json and Visual Map affine + segment-identity extension',
        'point_origin': 'cell; equivalence to SignalRGB pixel-center convention remains unverified',
        'imported_source_elements': len(imported), 'members': len(entries),
        'led_routes': sum(len(entry['settings']['custom_shape']['led_positions']) for entry in entries),
        'source_members': per_source,
        'unmapped_layout_elements': sorted(set(source_entries) - imported),
        'complete': set(source_entries) == imported,
        'disabled_components': 'omitted from map but retained in native segment indexes/wire offsets',
        'source_hashes': plan.get('source_hashes', {}),
    }
    return result, report


def append_fixed_outputs(result, report, plan, native_export, fixed):
    """Explicit non-resizing whole-zone routes, never model/count auto-matching.

    LED source indices are given in native LED order. Image/matrix routes require
    an explicit source rectangle and an actual dense native matrix. Image routes
    use an optional native surface; matrix routes use the ordinary LED sampler
    (for example a legacy SDK wallpaper peer without the image extension).
    """
    result, report = copy.deepcopy(result), copy.deepcopy(report)
    layout = next(x for x in plan['layouts'] if x['name'] == report['source_layout'])
    geometries = {}
    for controller in plan['controllers']:
        for zone in controller['zones']:
            if zone['action'] == 'verify_fixed_or_image_mapping':
                for component in zone.get('components', []):
                    geometries.setdefault(component['source_id'], []).append(component)
    seen = set()
    image_count = 0
    image_cells = 0
    matrix_count = 0
    matrix_cells = 0
    fixed_report = []

    def number(value, positive=False):
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or (positive and value <= 0):
            raise ValueError('Invalid finite fixed-output geometry')
        return value

    for binding in fixed.get('outputs', []):
        source_id, selector = binding['source_id'], binding['selector']
        if set(selector) != set(IDENTITY) or not binding.get('evidence'):
            raise ValueError('Fixed output requires complete explicit identity and mapping evidence')
        candidates = [c for c in native_export['controllers'] if all(c.get(k) == v for k, v in selector.items())]
        if len(candidates) != 1:
            raise ValueError('Fixed output native identity missing or ambiguous')
        controller = candidates[0]
        if 'vendor' not in controller:
            raise ValueError('Fixed output requires exported vendor')
        zi = binding['zone_idx']
        if isinstance(zi, bool) or not isinstance(zi, int) or not 0 <= zi < len(controller['zones']):
            raise ValueError('Invalid fixed zone index')
        identity = {k: controller[k] for k in ('name', 'vendor', 'description', 'version', 'serial', 'location')}
        key = (tuple(selector[k] for k in IDENTITY), zi)
        if key in seen or any(e['controller'] == identity and e['zone_idx'] == zi for e in result['ctrl_zones']):
            raise ValueError('Fixed output overlaps another bound zone')
        seen.add(key)
        choices = geometries.get(source_id, [])
        if len(choices) != 1 or not isinstance(layout['entries'].get(source_id), dict):
            raise ValueError('Fixed output source geometry/placement missing or ambiguous')
        component, transform = choices[0], layout['entries'][source_id]
        count = controller['zones'][zi]['leds_count']
        if not isinstance(count, int) or count <= 0:
            raise ValueError('Fixed zone has no actual LEDs')
        base_w, base_h = (number(v, True) for v in component['base_size'])
        sx, sy = (number(transform.get('scale', {}).get(k, 1), True) for k in ('x', 'y'))
        x, y = (number(transform.get(k, 0)) for k in ('x', 'y'))
        rotation = number(transform.get('rotation', 0))
        fx, fy = transform.get('flipped', False), transform.get('flippedV', False)
        if not isinstance(fx, bool) or not isinstance(fy, bool):
            raise ValueError('Fixed mirror flags must be boolean')
        gain = number(transform.get('brightness', 100))/100 * number(layout.get('brightness', 100))/100
        if not 0 <= gain <= 1:
            raise ValueError('Invalid fixed-output brightness')
        kind = binding['kind']
        if kind == 'led':
            indices = binding['source_wire_indices']
            source_points = {p['wire_index']: p for p in component['wire_points']}
            if len(indices) != count or any(isinstance(i, bool) or not isinstance(i, int) or i not in source_points for i in indices):
                raise ValueError('Fixed LED mapping must cover exactly every native LED')
            points = [{'led_num': native_index, 'x': source_points[source_index]['x'], 'y': source_points[source_index]['y']}
                      for native_index, source_index in enumerate(indices)]
            shape_w, shape_h = base_w, base_h
            report['led_routes'] += count
        elif kind in ('image', 'matrix'):
            if kind == 'image' and binding.get('native_image_capability_verified') is not True:
                raise ValueError('An image route requires explicit native capability evidence')
            if kind == 'matrix' and binding.get('matrix_sampling_verified') is not True:
                raise ValueError('A matrix route requires explicit sampling capability evidence')
            matrix = controller['zones'][zi].get('matrix_map', {})
            shape_w, shape_h, mapping = matrix.get('width', 0), matrix.get('height', 0), matrix.get('map', [])
            if shape_w < 2 or shape_h < 2 or shape_w*shape_h != count or len(mapping) != count or sorted(mapping) != list(range(count)):
                raise ValueError('Image whole-zone route requires a dense native compatibility matrix')
            rectangle = binding['source_rect']
            rx, ry = number(rectangle['x']), number(rectangle['y'])
            rw, rh = number(rectangle['width'], True), number(rectangle['height'], True)
            if rx < 0 or ry < 0 or rx+rw > base_w or ry+rh > base_h:
                raise ValueError('Image source rectangle is outside its source component')
            # Preserve the original full-component pivot, including the extra
            # guard row/column of a source image plugin. Native grid dimensions
            # remain dense; only its affine maps to the actual capture rectangle.
            base_cx, base_cy = base_w*sx/2, base_h*sy/2
            local_cx, local_cy = rw*sx/2, rh*sy/2
            dx, dy = rx*sx+local_cx-base_cx, ry*sy+local_cy-base_cy
            if fx: dx = -dx
            if fy: dy = -dy
            angle = math.radians(rotation)
            x += base_cx-local_cx+math.cos(angle)*dx-math.sin(angle)*dy
            y += base_cy-local_cy+math.sin(angle)*dx+math.cos(angle)*dy
            sx, sy = sx*rw/shape_w, sy*rh/shape_h
            points = [{'led_num': index, 'x': i % shape_w, 'y': i // shape_w} for i, index in enumerate(mapping)]
            if kind == 'image':
                image_count += 1
                image_cells += count
            else:
                matrix_count += 1
                matrix_cells += count
        else:
            raise ValueError('Unknown fixed output kind')
        settings = {'shape': 2, 'x': x, 'y': y, 'scale': 1, 'led_spacing': 1, 'reverse': False,
                    'affine': {'scale_x': sx, 'scale_y': sy, 'rotation': rotation, 'flip_x': fx, 'flip_y': fy},
                    'point_origin': 'cell', 'brightness': gain,
                    'custom_shape': {'w': shape_w, 'h': shape_h, 'led_positions': points}}
        result['ctrl_zones'].append({'controller': identity, 'zone_idx': zi, 'is_segment': False, 'segment_idx': 0,
                                    'custom_zone_name': component['name'], 'settings': settings})
        report['source_members'].setdefault(source_id, []).append({'zone_idx': zi, 'is_segment': False, 'kind': kind,
                                                                 'leds': count if kind == 'led' else 0})
        fixed_report.append({'source_id': source_id, 'zone_idx': zi, 'kind': kind, 'evidence': binding['evidence']})
    report.update(members=len(result['ctrl_zones']), imported_source_elements=len(report['source_members']),
                  image_surfaces=image_count, image_compatibility_cells=image_cells, fixed_outputs=fixed_report,
                  matrix_surfaces=matrix_count, matrix_sample_cells=matrix_cells,
                  unmapped_layout_elements=sorted(set(layout['entries'])-set(report['source_members'])))
    report['complete'] = not report['unmapped_layout_elements']
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('plan')
    for name in ('project', 'metadata', 'snapshot', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    p = commands.add_parser('compile')
    for name in ('plan', 'native-export', 'bindings', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    p = commands.add_parser('propose')
    for name in ('plan', 'native-export', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    p = commands.add_parser('export-map')
    for name in ('plan', 'native-export', 'bindings', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    p.add_argument('--layout', default='Full Scale')
    p.add_argument('--fixed-bindings', type=Path)
    args = parser.parse_args()
    if args.out.exists():
        raise SystemExit('Output directory already exists; choose a fresh private directory')
    if args.command == 'plan':
        plan, fragment = build_plan(read_json(args.project), read_json(args.metadata), read_json(args.snapshot))
        plan['source_hashes'] = {name: hashlib.sha256(getattr(args, name).read_bytes()).hexdigest()
                                 for name in ('project', 'metadata', 'snapshot')}
        bindings = {'controllers': [{'source_id': c['source_id'], 'selector': None,
                                     'zones': [{'source_channel': z['source_channel'], 'native_zone': z.get('native_index_hint')}
                                               for z in c['zones'] if z['action'] == 'configure_segments']}
                                    for c in plan['controllers'] if any(z['action'] == 'configure_segments' for z in c['zones'])]}
        args.out.mkdir(parents=True)
        write_new(args.out / 'migration-plan.json', plan)
        write_new(args.out / 'bindings.template.json', bindings)
        write_new(args.out / 'OpenRGB.govee-lan.fragment.json', fragment)
        print(json.dumps(plan['summary']))
    elif args.command == 'propose':
        proposals, report = propose_bindings(read_json(args.plan), read_json(args.native_export))
        args.out.mkdir(parents=True)
        write_new(args.out / 'bindings.proposed.json', proposals)
        write_new(args.out / 'identity-proposals.json', report)
        print('Proposed', len(proposals['controllers']), 'uniquely identified controller bindings; none applied')
    elif args.command == 'export-map':
        result, report = export_visualmap(read_json(args.plan), read_json(args.native_export), read_json(args.bindings), args.layout)
        if args.fixed_bindings:
            result, report = append_fixed_outputs(result, report, read_json(args.plan), read_json(args.native_export), read_json(args.fixed_bindings))
        args.out.mkdir(parents=True)
        write_new(args.out / 'layout-components.vmap.json', result)
        write_new(args.out / 'map-import-report.json', report)
        print('Prepared', report['members'], 'inactive map members;', report['led_routes'], 'LED routes;', len(report['unmapped_layout_elements']), 'source elements remain unbound')
    else:
        config, report = compile_configuration(read_json(args.plan), read_json(args.native_export), read_json(args.bindings))
        args.out.mkdir(parents=True)
        write_new(args.out / 'Configuration.json', config)
        write_new(args.out / 'compile-report.json', report)
        print('Prepared', len(report['configured_zones']), 'zones; no application or hardware accessed')


if __name__ == '__main__':
    main()
