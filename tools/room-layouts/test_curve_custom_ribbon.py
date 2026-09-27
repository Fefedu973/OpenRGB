"""Pure JSON geometry checks; no live layout or process access."""
# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import json
import math
import unittest
from curve_custom_ribbon import bend,world

def fixture():
    return {'ctrl_zones':[{'custom_zone_name':'Synthetic ribbon','zone_idx':0,
      'is_segment':True,'segment_idx':lane,'settings':{
      'shape':2,'x':15.25,'y':9.75,'scale':1.5,'point_origin':'cell',
      'affine':{'scale_x':1.2,'scale_y':.8,'rotation':37,'flip_x':True},
      'custom_shape':{'w':5,'h':2,'led_positions':[
      {'led_num':(4-column if lane else column),'x':column,'y':lane+.125}
      for column in range(5)]}}}for lane in range(2)]}

class Geometry(unittest.TestCase):
    def test_valid_endpoints_and_order(self):
        data=fixture();old=copy.deepcopy(data);report=bend(data,'Synthetic ribbon','up')
        self.assertEqual(report['leds'],10);self.assertLess(report['endpoint_error'],1e-10)
        for a,b in zip(old['ctrl_zones'],data['ctrl_zones']):
            for p,q in zip(a['settings']['custom_shape']['led_positions'],b['settings']['custom_shape']['led_positions']):
                self.assertEqual(p['led_num'],q['led_num'])
                if p['x'] in (0,4):self.assertLess(math.dist(world(a['settings'],p),world(b['settings'],q)),1e-10)

    def test_rejects_invalid_before_mutation(self):
        changes=[('x',float('nan')),('y',float('inf')),('scale',0),('scale',-1),
                 ('scale',True),('point_origin','corner'),('affine',None)]
        for key,value in changes:
            with self.subTest(key=key,value=value):
                data=fixture();data['ctrl_zones'][1]['settings'][key]=value;before=json.dumps(data)
                with self.assertRaises(ValueError):bend(data,'Synthetic ribbon','up')
                self.assertEqual(before,json.dumps(data))
        for key,value in [('scale_x',0),('scale_y',float('nan')),('rotation',float('inf')),('flip_x',1)]:
            with self.subTest(key=key):
                data=fixture();data['ctrl_zones'][1]['settings']['affine'][key]=value;before=json.dumps(data)
                with self.assertRaises(ValueError):bend(data,'Synthetic ribbon','up')
                self.assertEqual(before,json.dumps(data))
        for key,value in [('w',-1),('h',float('nan'))]:
            data=fixture();data['ctrl_zones'][1]['settings']['custom_shape'][key]=value;before=json.dumps(data)
            with self.assertRaises(ValueError):bend(data,'Synthetic ribbon','up')
            self.assertEqual(before,json.dumps(data))

    def test_rejects_finite_input_overflow(self):
        data=fixture()
        for member in data['ctrl_zones']:
            member['settings']['scale']=1e308;member['settings']['affine']['scale_x']=1e308
        before=json.dumps(data)
        with self.assertRaises(ValueError):bend(data,'Synthetic ribbon','up')
        self.assertEqual(before,json.dumps(data))

if __name__=='__main__':unittest.main()
