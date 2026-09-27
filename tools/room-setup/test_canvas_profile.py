# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('canvas_profile',Path(__file__).with_name('write-canvas-profile.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class CanvasProfileTests(unittest.TestCase):
    def fixture(self):
        return {'profile_version':7,'controllers':[{'name':'Canvas','vendor':'Plugin','description':'Virtual canvas',
            'version':'actual-build','serial':'unique','location':'here','zones':[{'leds_count':64}]}]}

    def test_single_spatial_shader_preserves_actual_identity_and_has_no_device_state(self):
        data=self.fixture();before=copy.deepcopy(data);p=m.make_profile(data,'Canvas')
        self.assertEqual(data,before);self.assertEqual(p['controllers'],[]);self.assertNotIn('base_color',p)
        effects=p['plugins']['OpenRGB Effects Plugin']['Effects'];self.assertEqual(len(effects),1)
        e=effects[0];self.assertTrue(e['AutoStart']);self.assertFalse(e['SelectAll']);self.assertEqual(e['FPS'],30)
        self.assertEqual(len(e['ControllerZones']),1)
        for key in m.IDENTITY:self.assertEqual(e['ControllerZones'][0][key],data['controllers'][0][key])
        c=e['CustomSettings'];self.assertEqual((c['width'],c['height']),(800,500))
        self.assertEqual(c['shader_program']['main_pass']['type'],2)
        self.assertEqual(c['shader_program']['version'],'110');self.assertEqual(c['shader_program']['passes'],[])

    def test_ambiguous_missing_and_empty_zone_rejected(self):
        for mode in ('missing','duplicate','empty'):
            d=self.fixture()
            if mode=='missing':d['controllers']=[]
            elif mode=='duplicate':d['controllers']*=2
            else:d['controllers'][0]['zones'][0]['leds_count']=0
            with self.subTest(mode=mode),self.assertRaises(ValueError):m.make_profile(d,'Canvas')

    def test_size_and_fps_are_bounded(self):
        for options in ({'width':4097},{'width':True},{'height':0},{'fps':61},{'zone_idx':-1}):
            with self.subTest(options=options),self.assertRaises(ValueError):m.make_profile(self.fixture(),'Canvas',**options)

    def test_autoload_merge_preserves_other_settings_and_input(self):
        data={'Client':{'clients':[{'host':'127.0.0.1','port':6743}]},'GoveeBluetooth':{'key_file':'private'},
              'ProfileManager':{'exit_profile':{'enabled':False,'name':'existing'}}}
        before=copy.deepcopy(data);out=m.autoload_settings(data,'Rainbow')
        self.assertEqual(data,before)
        for key in ('Client','GoveeBluetooth'):self.assertEqual(out[key],data[key])
        self.assertEqual(out['ProfileManager']['exit_profile'],data['ProfileManager']['exit_profile'])
        self.assertEqual(out['ProfileManager']['open_profile'],{'enabled':True,'name':'Rainbow'})
        self.assertEqual(out['ProfileManager']['resume_profile'],out['ProfileManager']['open_profile'])


if __name__=='__main__':unittest.main()
