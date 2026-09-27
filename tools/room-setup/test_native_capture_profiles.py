"""Profile isolation and route-preservation regressions; no live configuration."""
# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import unittest
from pathlib import Path

module_spec=importlib.util.spec_from_file_location('profiles',Path(__file__).with_name('create-native-effect-profiles.py'))
profiles=importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(profiles)


class Profiles(unittest.TestCase):
    def setUp(self):
        self.template={'profile_name':'Original','controllers':[], 'plugins':{
            'OpenRGB Effects Plugin':{'version':2,'Effects':[{'ControllerZones':[{'name':'Full Scale.json','vendor':'OpenRGB Visual Map Plugin','self_brightness':73}], 'CustomSettings':{'zone_regions':[{'id':'unchanged'}],'audio_settings':{'audio_device':7}}}]},
            'OpenRGB Visual Map Plugin':{'active_map':'Full Scale.json'},'Other plugin':{'preserved':True}}}
        self.original=copy.deepcopy(self.template)
        self.spec={'id':'ScreenAmbience','title':'Screen Ambience','screenReactive':True,'controls':[{'key':'color_boost','default':1}]}

    def test_independent_named_scenes_and_routes(self):
        ids=['f717b624-8acb-48e7-8db3-30bd99d1c07a','144f21b7-d9dd-4dd4-8ac6-441bf041ee66']
        a,b=[profiles.make_profile(self.template,self.spec,'Scene '+str(i),scene=value,follow=True) for i,value in enumerate(ids)]
        effect=lambda p:p['plugins']['OpenRGB Effects Plugin']['Effects'][0]
        self.assertEqual([effect(p)['CustomSettings']['screen_source']['scene'] for p in (a,b)],ids)
        self.assertEqual(effect(a)['ControllerZones'],effect(self.original)['ControllerZones'])
        self.assertEqual(a['plugins']['Other plugin'],self.original['plugins']['Other plugin'])
        effect(a)['CustomSettings']['zone_regions'][0]['id']='modified'
        self.assertEqual(effect(b)['CustomSettings']['zone_regions'][0]['id'],'unchanged')
        self.assertEqual(self.template,self.original)

    def test_raw_effect_and_music_route(self):
        raw=profiles.make_profile(self.template,self.spec,'Raw')
        self.assertFalse(raw['plugins']['OpenRGB Effects Plugin']['Effects'][0]['CustomSettings']['screen_source']['follow_better_appearance'])
        music_spec=dict(self.spec,id='PumpUpBeats',screenReactive=False,audioReactive=True)
        result=profiles.make_profile(self.template,music_spec,'Music',music_map='Music.json')
        effect=result['plugins']['OpenRGB Effects Plugin']['Effects'][0]
        self.assertEqual(effect['ControllerZones'][0]['self_brightness'],73)
        self.assertEqual(effect['ControllerZones'][0]['name'],'Music.json')
        self.assertEqual(result['plugins']['OpenRGB Visual Map Plugin']['active_map'],'Music.json')
        self.assertEqual(effect['CustomSettings']['audio_settings'],{'audio_device':7})
        self.assertNotIn('screen_source',effect['CustomSettings'])
        self.assertEqual(self.template,self.original)

    def test_invalid_scene_does_not_change_template(self):
        with self.assertRaises(ValueError):profiles.make_profile(self.template,self.spec,'Bad',scene='invalid')
        self.assertEqual(self.template,self.original)


if __name__=='__main__':unittest.main()
