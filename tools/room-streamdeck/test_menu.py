# SPDX-License-Identifier: GPL-2.0-or-later
import copy
from pathlib import Path
import unittest
from prepare_menu import build_pages,MAIN,OTHER

class MenuTest(unittest.TestCase):
    def test_page_preserves_parent_and_only_has_reviewed_actions(self):
        back={'UUID':'com.elgato.streamdeck.profile.backtoparent','ActionID':'original-back','States':[{}],'Settings':{},'AutoExit':{'Enabled':False}}
        source={'Controllers':[{'Type':'Keypad','Background':{'preserve':True},'Actions':{'0,0':back,'1,1':{'UUID':'old-cloud-action'}}}],'Icon':'keep','Name':'old'}
        original=copy.deepcopy(source)
        root,child,shortcuts=build_pages(source,Path('C:/example/shortcuts'),'AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA')
        self.assertEqual(source,original)
        self.assertEqual(root['Controllers'][0]['Actions']['0,0'],back)
        self.assertEqual(root['Controllers'][0]['Background'],{'preserve':True})
        self.assertEqual(root['Icon'],'keep')
        self.assertEqual(len(shortcuts),11)
        self.assertEqual([v['profile'] for v in shortcuts],[v[1] for v in MAIN+OTHER])
        self.assertEqual(root['Controllers'][0]['Actions']['1,1']['Settings']['ProfileUUID'],'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa')
        all_actions=list(root['Controllers'][0]['Actions'].values())+list(child['Controllers'][0]['Actions'].values())
        self.assertEqual(len({a['ActionID'] for a in all_actions}),len(all_actions))
        self.assertTrue(all(a['UUID'].startswith('com.elgato.streamdeck.') for a in all_actions))
        self.assertNotIn('Music - Tri Band',[v['profile'] for v in shortcuts])

    def test_refuses_unverified_navigation(self):
        with self.assertRaises(ValueError):build_pages({'Controllers':[{'Type':'Keypad','Actions':{'0,0':{'UUID':'different'}}}]},Path('C:/example'),'new')

if __name__=='__main__':unittest.main()
