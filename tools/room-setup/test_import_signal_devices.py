# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('migration', Path(__file__).with_name('import-signal-devices.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def device(ident, n=2, active=True, **kwargs):
    return dict(id=ident, name=ident, active=active, enabled=active,
                baseSize=[n, 1], leds=[dict(x=i, y=0, name=str(i)) for i in range(n)], **kwargs)


def fixture():
    parent = '1b1c:0c1a:TEST'
    devices = [device('a', parentId=parent, channel='Channel 1', order=0),
               device('b', active=False, parentId=parent, channel='Channel 1', order=1),
               device('c', parentId=parent, channel='Channel 1', order=2)]
    project = {'devices': devices, 'canvas': {'width': 320, 'height': 200},
               'layouts': [{'name': 'Full Scale', 'readOnly': True,
                            'entries': {'a': {'x': 10, 'y': 20, 'rotation': 90}}}]}
    plan, _ = m.build_plan(project, {}, {})
    identity = dict(type=4, name='Real exported controller', description='Native description',
                    version='exported-1', serial='NATIVE-TEST', location='HID: TEST')
    zone = dict(name='Corsair RGB Header 1', type=1, flags=m.SIZE|m.TYPE|m.SEGMENTS,
                leds_count=0, leds_min=0, leds_max=204, matrix_map={'width': 0, 'height': 0, 'map': []},
                segments=[], modes=[])
    native = {'profile_version': 7, 'profile_name': 'export', 'controllers': [dict(identity, zones=[zone], leds=[])]}
    bindings = {'controllers': [{'source_id': parent, 'selector': identity,
                                 'zones': [{'source_channel': 'Channel 1', 'native_zone': 0}]}]}
    return project, plan, native, bindings


def fixed_fixture(image=False):
    _, plan, native, binding = fixture()
    native['controllers'][0]['vendor'] = 'Fixture'
    source = device('fixed', n=3)
    source['baseSize'] = [33, 21] if image else [3.25, 2.5]
    source['leds'] = [{'x': .25, 'y': .75}, {'x': .25, 'y': .75}, {'x': 2.5, 'y': 1.25}]
    component = m.geometry(source, {})
    plan['controllers'].append({'source_id': 'fixed', 'zones': [{'action': 'verify_fixed_or_image_mapping', 'components': [component]}]})
    plan['layouts'][0]['entries']['fixed'] = {'x': 7.25, 'y': -9.5, 'rotation': 37,
         'scale': {'x': 2.25, 'y': .75}, 'flipped': True, 'flippedV': False, 'brightness': 40}
    controller = copy.deepcopy(native['controllers'][0])
    controller.update(name='Explicit fixed controller', serial='FIXED')
    controller['zones'] = [{'name': 'Fixed zone', 'leds_count': 6 if image else 3,
        'matrix_map': {'width': 3, 'height': 2, 'map': [5, 4, 3, 2, 1, 0]} if image else {}}]
    native['controllers'].append(controller)
    fixed = {'outputs': [{'source_id': 'fixed', 'selector': {k: controller[k] for k in m.IDENTITY},
                         'zone_idx': 0, 'kind': 'image' if image else 'led',
                         'source_wire_indices': [2, 0, 1], 'evidence': ['explicit fixture wire correspondence']}]}
    if image:
        fixed['outputs'][0].update(native_image_capability_verified=True,
                                  source_rect={'x': 0, 'y': 0, 'width': 32, 'height': 20})
    result, report = m.export_visualmap(plan, native, binding)
    return plan, native, fixed, result, report


class MigrationTests(unittest.TestCase):
    def test_asus_onboard_then_12v_preserves_distinct_placements_and_fixed_size(self):
        parent = '0b05:19af:FIXTURE'
        board = device(parent, n=3)
        header = device(parent+':12V Header 1', n=1)
        header.update(baseSize=[3,3], leds=[{'x':1,'y':1}])
        project = {'devices':[header,board], 'canvas':{'width':320,'height':200}, 'layouts':[
            {'name':'Full Scale','entries':{parent:{'x':10,'y':20}, header['id']:{'x':70,'y':80,'rotation':90}}}]}
        plan,_ = m.build_plan(project,{}, {})
        zone = plan['controllers'][0]['zones'][0]
        self.assertTrue(zone['fixed_size'])
        self.assertEqual([(x['source_id'],x['start_idx']) for x in zone['components']],[(parent,0),(header['id'],3)])
        _,_,native,_ = fixture()
        c=native['controllers'][0];c['vendor']='ASUS'
        c['zones'][0].update(name='Aura Mainboard',leds_count=4,leds_min=4,leds_max=4,flags=m.TYPE|m.SEGMENTS)
        bindings={'controllers':[{'source_id':parent,'selector':{k:c[k] for k in m.IDENTITY},
                                 'zones':[{'source_channel':'Onboard + 12V','native_zone':0}]}]}
        config,_=m.compile_configuration(plan,native,bindings)
        self.assertEqual([(s['start_idx'],s['leds_count']) for s in config['controllers'][0]['zones'][0]['segments']],[(0,3),(3,1)])
        self.assertFalse(config['controllers'][0]['zones'][0]['flags'] & m.SET_SIZE)
        mapped,_=m.export_visualmap(plan,native,bindings)
        self.assertEqual([x['segment_idx'] for x in mapped['ctrl_zones']],[0,1])
        self.assertEqual([x['settings']['x'] for x in mapped['ctrl_zones']],[10,70])
        self.assertEqual(mapped['ctrl_zones'][1]['settings']['custom_shape']['led_positions'],[{'led_num':0,'x':1,'y':1}])
        c['zones'][0].update(leds_count=5,leds_min=4,leds_max=5,flags=m.TYPE|m.SEGMENTS|m.SIZE)
        with self.assertRaisesRegex(ValueError,'actual native fixed count'):
            m.compile_configuration(plan,native,bindings)

    def test_asus_disabled_header_keeps_its_physical_offset(self):
        parent='0b05:19af:FIXTURE'
        plan,_=m.build_plan({'devices':[device(parent,n=3),device(parent+':12V Header 1',n=1,active=False)]},{},{})
        z=plan['controllers'][0]['zones'][0]
        self.assertEqual(z['leds_count'],4)
        self.assertEqual([p['enabled'] for p in z['components']],[True,False])
        self.assertEqual(z['components'][1]['start_idx'],3)

    def test_fixed_routes_explicit_native_order_and_preserves_source(self):
        plan, native, fixed, result, report = fixed_fixture()
        before = copy.deepcopy((plan, native, fixed, result, report))
        mapped, details = m.append_fixed_outputs(result, report, plan, native, fixed)
        self.assertEqual((plan, native, fixed, result, report), before)
        entry = mapped['ctrl_zones'][-1]
        self.assertFalse(entry['is_segment'])
        self.assertEqual(entry['settings']['custom_shape'], {'w': 3.25, 'h': 2.5, 'led_positions': [
            {'led_num': 0, 'x': 2.5, 'y': 1.25}, {'led_num': 1, 'x': .25, 'y': .75},
            {'led_num': 2, 'x': .25, 'y': .75}]})
        self.assertEqual(details['led_routes'], report['led_routes'] + 3)
        self.assertEqual(entry['settings']['brightness'], .4)

    def test_fixed_route_rejects_incomplete_mapping_and_evidence(self):
        for change, expected in ((lambda b: b.update(source_wire_indices=[0]), 'every native LED'),
             (lambda b: b.update(source_wire_indices=[0,1,99]), 'every native LED'),
             (lambda b: b.update(evidence=[]), 'evidence'),
             (lambda b: b['selector'].pop('version'), 'identity')):
            with self.subTest(expected=expected):
                plan, native, fixed, result, report = fixed_fixture()
                change(fixed['outputs'][0])
                with self.assertRaisesRegex(ValueError, expected):
                    m.append_fixed_outputs(result, report, plan, native, fixed)

    def test_fixed_route_rejects_ambiguous_identity_and_overlap(self):
        plan, native, fixed, result, report = fixed_fixture()
        native['controllers'].append(copy.deepcopy(native['controllers'][-1]))
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            m.append_fixed_outputs(result, report, plan, native, fixed)
        native['controllers'].pop()
        fixed['outputs'] *= 2
        with self.assertRaisesRegex(ValueError, 'overlaps'):
            m.append_fixed_outputs(result, report, plan, native, fixed)

    def test_image_route_is_native_dense_surface_not_sparse_source_markers(self):
        plan, native, fixed, result, report = fixed_fixture(image=True)
        mapped, details = m.append_fixed_outputs(result, report, plan, native, fixed)
        shape = mapped['ctrl_zones'][-1]['settings']['custom_shape']
        self.assertEqual((shape['w'], shape['h']), (3, 2))
        self.assertEqual([p['led_num'] for p in shape['led_positions']], [5,4,3,2,1,0])
        self.assertEqual(details['led_routes'], report['led_routes'])
        self.assertEqual((details['image_surfaces'], details['image_compatibility_cells']), (1, 6))

    def test_image_route_rejects_missing_capability_holes_and_outside_crop(self):
        for case in ('capability', 'hole', 'crop'):
            plan, native, fixed, result, report = fixed_fixture(image=True)
            if case == 'capability': fixed['outputs'][0]['native_image_capability_verified'] = False
            elif case == 'hole': native['controllers'][-1]['zones'][0]['matrix_map']['map'][0] = 0
            else: fixed['outputs'][0]['source_rect']['width'] = 34
            with self.subTest(case=case), self.assertRaises(ValueError):
                m.append_fixed_outputs(result, report, plan, native, fixed)

    def test_legacy_matrix_route_does_not_claim_native_image_capability(self):
        plan, native, fixed, result, report = fixed_fixture(image=True)
        fixed['outputs'][0].update(kind='matrix', native_image_capability_verified=False)
        with self.assertRaisesRegex(ValueError,'sampling capability'):
            m.append_fixed_outputs(result, report, plan, native, fixed)
        fixed['outputs'][0]['matrix_sampling_verified']=True
        mapped, details=m.append_fixed_outputs(result, report, plan, native, fixed)
        self.assertEqual(details['image_surfaces'],0)
        self.assertEqual(details['image_compatibility_cells'],0)
        self.assertEqual((details['matrix_surfaces'],details['matrix_sample_cells']),(1,6))
        self.assertEqual(details['led_routes'],report['led_routes'])
        self.assertEqual(len(mapped['ctrl_zones'][-1]['settings']['custom_shape']['led_positions']),6)

    def test_image_capture_rectangle_keeps_full_source_pivot_rotation_and_flips(self):
        def projected(x, y, width, height, sx, sy, rotation, flip_x, flip_y, u, v):
            dx, dy = (u-width/2)*sx, (v-height/2)*sy
            if flip_x: dx = -dx
            if flip_y: dy = -dy
            a = math.radians(rotation)
            return x+width*sx/2+dx*math.cos(a)-dy*math.sin(a), y+height*sy/2+dx*math.sin(a)+dy*math.cos(a)
        for rotation in (0, 37, 90, 180):
            for flip_x in (False, True):
                for flip_y in (False, True):
                    plan, native, fixed, result, report = fixed_fixture(image=True)
                    t = plan['layouts'][0]['entries']['fixed']
                    t.update(rotation=rotation, flipped=flip_x, flippedV=flip_y)
                    mapped, _ = m.append_fixed_outputs(result, report, plan, native, fixed)
                    s = mapped['ctrl_zones'][-1]['settings']; a = s['affine']
                    for u, v in ((0,0), (.5,.5), (1,1), (.13,.71)):
                        expected = projected(t['x'],t['y'],33,21,2.25,.75,rotation,flip_x,flip_y,u*32,v*20)
                        actual = projected(s['x'],s['y'],3,2,a['scale_x'],a['scale_y'],rotation,flip_x,flip_y,u*3,v*2)
                        for one, two in zip(actual, expected): self.assertAlmostEqual(one, two, places=11)

    def test_visualmap_preserves_segment_indices_disabled_gaps_and_native_identity(self):
        _, plan, native, binding = fixture()
        native['controllers'][0]['vendor'] = 'Actual exported vendor'
        plan['layouts'][0]['entries']['c'] = {'x': 1.25, 'y': -3.5, 'scale': {'x': 2.25, 'y': 3},
                                               'rotation': 37, 'flipped': True, 'flippedV': False, 'brightness': 42}
        plan['layouts'][0]['brightness'] = 50
        before = copy.deepcopy(plan)
        result, report = m.export_visualmap(plan, native, binding)
        self.assertEqual(plan, before)
        members = result['ctrl_zones']
        self.assertEqual([entry['segment_idx'] for entry in members], [0, 2])
        self.assertTrue(all(entry['is_segment'] for entry in members))
        self.assertEqual(members[1]['controller']['vendor'], 'Actual exported vendor')
        self.assertEqual(members[1]['settings']['x'], 1.25)
        self.assertEqual(members[1]['settings']['affine'], {'scale_x': 2.25, 'scale_y': 3, 'rotation': 37,
                                                          'flip_x': True, 'flip_y': False})
        self.assertAlmostEqual(members[1]['settings']['brightness'], .21)
        self.assertEqual(members[1]['settings']['point_origin'], 'cell')
        self.assertEqual(report['led_routes'], 4)
        self.assertTrue(report['complete'])
        self.assertFalse(result['grid_settings']['auto_register'])
        self.assertFalse(result['grid_settings']['auto_load'])

    def test_visualmap_keeps_coincident_leds_and_wire_order(self):
        _, plan, native, binding = fixture()
        native['controllers'][0]['vendor'] = 'Fixture'
        points = plan['controllers'][0]['zones'][0]['components'][0]['wire_points']
        points[0].update(x=.25, y=.75)
        points[1].update(x=.25, y=.75)
        result, _ = m.export_visualmap(plan, native, binding)
        self.assertEqual(result['ctrl_zones'][0]['settings']['custom_shape']['led_positions'],
                         [{'led_num': 0, 'x': .25, 'y': .75}, {'led_num': 1, 'x': .25, 'y': .75}])

    def test_visualmap_reports_unbound_elements_without_inventing_mapping(self):
        _, plan, native, binding = fixture()
        native['controllers'][0]['vendor'] = 'Fixture'
        plan['layouts'][0]['entries']['unbound-wallpaper'] = {'x': 0, 'y': 0}
        _, report = m.export_visualmap(plan, native, binding)
        self.assertFalse(report['complete'])
        self.assertEqual(report['unmapped_layout_elements'], ['c', 'unbound-wallpaper'])

    def test_visualmap_rejects_missing_vendor_and_invalid_transform(self):
        _, plan, native, binding = fixture()
        with self.assertRaisesRegex(ValueError, 'exported vendor'):
            m.export_visualmap(plan, native, binding)
        native['controllers'][0]['vendor'] = 'Fixture'
        plan['layouts'][0]['entries']['a']['scale'] = {'x': 0, 'y': 1}
        with self.assertRaisesRegex(ValueError, 'positive scale'):
            m.export_visualmap(plan, native, binding)
        plan['layouts'][0]['entries']['a']['scale']['x'] = float('nan')
        with self.assertRaisesRegex(ValueError, 'finite scale'):
            m.export_visualmap(plan, native, binding)

    def test_visualmap_refuses_partial_wire_geometry(self):
        _, plan, native, binding = fixture()
        native['controllers'][0]['vendor'] = 'Fixture'
        plan['controllers'][0]['zones'][0]['components'][0]['wire_points'].pop()
        with self.assertRaisesRegex(ValueError, 'complete (wire geometry|permutation)'):
            m.export_visualmap(plan, native, binding)

    def test_visualmap_strimer_rows_keep_full_shape_pivot_and_row_coordinates(self):
        strip = device('3061:4714:TEST:24PinStrimer', n=120)
        strip['baseSize'] = [20, 6]
        strip['leds'] = [{'x': i % 20, 'y': i // 20, 'name': str(i)} for i in range(120)]
        project = {'devices': [strip], 'canvas': {'width': 320, 'height': 200}, 'layouts': [
            {'name': 'Full Scale', 'entries': {strip['id']: {'x': 11, 'y': 17, 'rotation': 90,
                                                          'scale': {'x': 2, 'y': 3}}}}]}
        plan, _ = m.build_plan(project, {}, {})
        _, _, base_native, _ = fixture()
        controller = base_native['controllers'][0]
        controller['vendor'] = 'Nollie'
        controller['zones'] = [copy.deepcopy(controller['zones'][0]) for _ in range(22)]
        bindings = {'controllers': [{'source_id': '3061:4714:TEST',
                    'selector': {k: controller[k] for k in m.IDENTITY}, 'zones': []}]}
        for zone in plan['controllers'][0]['zones']:
            index = zone['native_index_hint']
            controller['zones'][index]['name'] = zone['native_name']
            bindings['controllers'][0]['zones'].append({'source_channel': zone['source_channel'], 'native_zone': index})
        result, report = m.export_visualmap(plan, base_native, bindings)
        self.assertEqual(len(result['ctrl_zones']), 6)
        for row, entry in enumerate(result['ctrl_zones']):
            shape = entry['settings']['custom_shape']
            self.assertEqual((shape['w'], shape['h']), (20, 6))
            self.assertEqual({p['y'] for p in shape['led_positions']}, {row})
            self.assertEqual([p['led_num'] for p in shape['led_positions']], list(range(20)))
            self.assertEqual(entry['settings']['x'], 11)
        self.assertEqual(report['led_routes'], 120)

    def test_disabled_middle_preserves_wire_addresses(self):
        project, plan, native, binding = fixture()
        components = plan['controllers'][0]['zones'][0]['components']
        self.assertEqual([c['start_idx'] for c in components], [0, 2, 4])
        self.assertEqual([c['enabled'] for c in components], [True, False, True])
        self.assertEqual(plan['summary']['active_elements'], 2)
        self.assertEqual(plan['summary']['sample_points'], 4)
        config, _ = m.compile_configuration(plan, native, binding)
        self.assertEqual(config['controllers'][0]['zones'][0]['leds_count'], 6)

    def test_preserves_full_scale_and_source_inputs(self):
        project, _, _, _ = fixture()
        before = copy.deepcopy(project)
        plan, _ = m.build_plan(project, {}, {})
        self.assertEqual(project, before)
        self.assertTrue(plan['layouts'][0]['readOnly'])
        self.assertEqual(plan['layouts'][0]['entries']['a']['rotation'], 90)
        self.assertNotIn('b', plan['layouts'][0]['entries'])

    def test_nonidentity_mapping_retained_without_guessing(self):
        project, _, _, _ = fixture()
        snapshot = {'components': {'a': [{'parent_device_id': '1b1c:0c1a:TEST', 'channel': 'Channel 1',
                                         'LedMapping': [1, 0], 'LedCoordinates': [[0, 0], [1, 0]]}]}}
        plan, _ = m.build_plan(project, {}, snapshot)
        c = plan['controllers'][0]['zones'][0]['components'][0]
        self.assertEqual(c['signal_led_mapping'], [1, 0])
        self.assertEqual(c['coordinate_to_wire_status'], 'sourced_coordinate_to_wire')
        self.assertEqual(c['wire_points'][0]['x'], 1)

    def test_bad_mapping_is_rejected(self):
        project, _, _, _ = fixture()
        snapshot = {'components': {'a': [{'parent_device_id': '1b1c:0c1a:TEST', 'channel': 'Channel 1', 'LedMapping': [0, 0]}]}}
        with self.assertRaisesRegex(ValueError, 'permutation'):
            m.build_plan(project, {}, snapshot)

    def test_strimer_rows_bind_exact_native_channels(self):
        d = device('3061:4714:TEST:24PinStrimer', n=120)
        plan, _ = m.build_plan({'devices': [d]}, {}, {})
        c = plan['controllers'][0]
        self.assertEqual(c['source_id'], '3061:4714:TEST')
        self.assertEqual([z['native_index_hint'] for z in c['zones']], list(range(16, 22)))
        self.assertEqual([z['usb_channel'] for z in c['zones']], [19, 18, 17, 16, 7, 6])
        self.assertEqual([z['components'][0]['source_led_start'] for z in c['zones']], [0,20,40,60,80,100])
        self.assertEqual([len(z['components'][0]['points']) for z in c['zones']], [20]*6)

    def test_govee_fixed_model_length_and_disabled_parent(self):
        d = device('strip', n=25, parentId='192.0.2.10', channel='Govee [test]', order=0)
        meta = {'192.0.2.10': {'name': 'Govee H61A0', 'currentIp': '192.0.2.11', 'stableDeviceId': '00:11:22:33:44:55:66:77'}}
        plan, fragment = m.build_plan({'devices': [d]}, meta, {})
        self.assertEqual(fragment['GoveeDevices']['devices'][0]['ip'], '192.0.2.11')
        self.assertEqual(plan['controllers'][0]['zones'][0]['leds_count'], 25)
        d['leds'].pop()
        with self.assertRaisesRegex(ValueError, 'segment count'):
            m.build_plan({'devices': [d]}, meta, {})
        d.update(active=False, enabled=False)
        plan, fragment = m.build_plan({'devices': [d]}, meta, {})
        self.assertEqual(fragment['GoveeDevices']['devices'], [])

    def test_complete_native_identity_and_capabilities_preserved(self):
        _, plan, native, bindings = fixture()
        original = copy.deepcopy(native)
        result, report = m.compile_configuration(plan, native, bindings)
        self.assertEqual(native, original)
        for key in m.IDENTITY:
            self.assertEqual(result['controllers'][0][key], native['controllers'][0][key])
        zone = result['controllers'][0]['zones'][0]
        self.assertEqual(zone['type'], 6)
        self.assertEqual([s['start_idx'] for s in zone['segments']], [0,2,4])
        self.assertEqual(zone['flags'] & m.SET_SIZE, m.SET_SIZE)
        self.assertEqual(len(result['controllers'][0]['leds']), 6)
        self.assertFalse(report['layout_imported'])

    def test_ambiguous_native_identity_rejected(self):
        _, plan, native, binding = fixture()
        native['controllers'] *= 2
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            m.compile_configuration(plan, native, binding)

    def test_missing_native_version_rejected(self):
        _, plan, native, binding = fixture()
        del binding['controllers'][0]['selector']['version']
        with self.assertRaisesRegex(ValueError, 'complete native identity'):
            m.compile_configuration(plan, native, binding)

    def test_capacity_and_resize_capability_rejected(self):
        _, plan, native, binding = fixture()
        native['controllers'][0]['zones'][0]['leds_max'] = 5
        with self.assertRaisesRegex(ValueError, 'bounds'):
            m.compile_configuration(plan, native, binding)
        native['controllers'][0]['zones'][0]['leds_max'] = 204
        native['controllers'][0]['zones'][0]['flags'] &= ~m.SIZE
        with self.assertRaisesRegex(ValueError, 'resizing'):
            m.compile_configuration(plan, native, binding)

    def test_fixed_count_allows_segments_without_resize_flag(self):
        _, plan, native, binding = fixture()
        z = native['controllers'][0]['zones'][0]
        z.update(leds_count=6, leds_min=6, leds_max=6, flags=m.TYPE|m.SEGMENTS)
        result, _ = m.compile_configuration(plan, native, binding)
        self.assertEqual(result['controllers'][0]['zones'][0]['flags'] & m.SET_SIZE, 0)
        z['flags'] &= ~m.SEGMENTS
        with self.assertRaisesRegex(ValueError, 'segment/type'):
            m.compile_configuration(plan, native, binding)

    def test_duplicate_zone_or_wrong_native_name_rejected(self):
        _, plan, native, binding = fixture()
        binding['controllers'][0]['zones'] *= 2
        with self.assertRaisesRegex(ValueError, 'duplicate target zone'):
            m.compile_configuration(plan, native, binding)
        binding['controllers'][0]['zones'] = binding['controllers'][0]['zones'][:1]
        native['controllers'][0]['zones'][0]['name'] = 'different connector'
        with self.assertRaisesRegex(ValueError, 'zone name'):
            m.compile_configuration(plan, native, binding)

    def test_fixed_or_image_sources_never_auto_resize(self):
        _, plan, native, binding = fixture()
        plan['controllers'][0]['zones'][0]['action'] = 'verify_fixed_or_image_mapping'
        with self.assertRaisesRegex(ValueError, 'automatic resizing'):
            m.compile_configuration(plan, native, binding)

    def test_never_overwrites_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / 'Configuration.json'
            m.write_new(p, {'existing': True})
            with self.assertRaises(FileExistsError):
                m.write_new(p, {'existing': False})
            self.assertEqual(json.loads(p.read_text()), {'existing': True})

    def test_asymmetric_component_permutation_is_not_inverted(self):
        d = device('a', n=3, parentId='parent', channel='Channel 1', order=0)
        snap = {'components': {'a': [{'parent_device_id':'parent', 'channel':'Channel 1', 'LedMapping':[2,0,1]}]}}
        c = m.geometry(d, snap)
        matrix, reason = m.segment_geometry(c)
        self.assertIsNone(reason)
        self.assertEqual(matrix['map'], [2,0,1])
        self.assertEqual([(p['wire_index'], p['x']) for p in c['wire_points']], [(0,1),(1,2),(2,0)])

    def test_coincident_leds_are_not_silently_lost_in_matrix(self):
        d = device('fan', n=3)
        d['leds'][2]['x']=0
        c = m.geometry(d, {})
        matrix, reason = m.segment_geometry(c)
        self.assertIsNone(matrix)
        self.assertEqual(reason, 'coincident_leds_require_custom_shape')
        self.assertEqual(len(c['wire_points']), 3)

    def test_unique_usb_proposal_copies_native_identity_only(self):
        _, plan, native, _ = fixture()
        # Source ID and native serial are independent representations of the
        # same device; proposal still copies the exact native identity fields.
        plan['controllers'][0]['source_id']='1b1c:0c1a:ABC12345'
        native['controllers'][0]['serial']='ABC12345'
        proposals, report = m.propose_bindings(plan, native)
        self.assertEqual(len(proposals['controllers']), 1)
        self.assertEqual(proposals['controllers'][0]['selector']['version'], 'exported-1')
        self.assertFalse(report['applied'])
        native['controllers'].append(copy.deepcopy(native['controllers'][0]))
        proposals, report = m.propose_bindings(plan, native)
        self.assertEqual(proposals['controllers'], [])
        self.assertEqual(report['unresolved'][0]['reason'], 'ambiguous_identity')

    def test_model_name_alone_is_not_a_binding(self):
        _, plan, native, _ = fixture()
        plan['controllers'][0]['name']=native['controllers'][0]['name']
        proposals, report = m.propose_bindings(plan, native)
        self.assertEqual(proposals['controllers'], [])
        self.assertEqual(report['unresolved'][0]['reason'], 'no_strong_identity_match')

    def test_plan_profile_paths_exist(self):
        repo=Path(__file__).resolve().parents[2]
        for _, driver in m.FIXED_DRIVERS.values():
            self.assertTrue((repo / driver).is_dir(), driver)

    def test_stable_govee_id_matches_after_ip_change(self):
        _, plan, native, _ = fixture()
        source=plan['controllers'][0]
        source.update(source_id='192.0.2.1', family='govee_lan', stable_device_id='00:11:22:33:44:55:66:77')
        native['controllers'][0].update(serial='0011223344556677', location='IP: 192.0.2.99')
        _, report=m.propose_bindings(plan,native)
        self.assertEqual(report['identity_proposals'][0]['evidence'],'exact_stable_govee_identity')

    def test_pnp_instance_match_requires_both_vid_and_pid(self):
        _, plan, native, _ = fixture()
        plan['controllers'][0]['source_id']='1b1c:0c1a:7-123456-1-2'
        native['controllers'][0].update(serial='', location=r'HID: \\?\hid#vid_1b1c&pid_0c1a#7&123456&1&2#{class}')
        proposals, report=m.propose_bindings(plan,native)
        self.assertEqual(len(proposals['controllers']),1)
        native['controllers'][0]['location']=native['controllers'][0]['location'].replace('0c1a','0000')
        proposals, report=m.propose_bindings(plan,native)
        self.assertEqual(proposals['controllers'],[])

    def test_smbus_address_proposal_still_requires_count_verification(self):
        _, plan, native, _ = fixture()
        plan['controllers'][0].update(source_id='I2CBUS:112',family='dram_native_enumeration_required')
        native['controllers'][0]['location']='I2C: controller, address 0x70'
        _,report=m.propose_bindings(plan,native)
        self.assertIn('requires_count_confirmation',report['identity_proposals'][0]['evidence'])
        native['controllers'].append(copy.deepcopy(native['controllers'][0]))
        _,report=m.propose_bindings(plan,native)
        self.assertEqual(report['identity_proposals'],[])


if __name__ == '__main__':
    unittest.main()
