"""Bend a custom LED ribbon while preserving identities and endpoint positions.

Only prepares JSON. Coordinates stay in Visual Map's existing CUSTOM format;
there is no device-specific renderer, firmware change or runtime connection.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import json
import math
from pathlib import Path


def validate_transform(settings):
    """Reject invalid geometry before changing any caller-owned point."""
    def number(value, positive=False):
        return (isinstance(value,(int,float)) and not isinstance(value,bool)
                and math.isfinite(value) and (not positive or value>0))
    shape=settings.get('custom_shape'); transform=settings.get('affine',{})
    if not isinstance(shape,dict) or not isinstance(transform,dict):
        raise ValueError('Custom shape and affine transform must be objects')
    if (not all(number(settings.get(k)) for k in ('x','y'))
            or not number(settings.get('scale',1),True)
            or not all(number(shape.get(k),True) for k in ('w','h'))
            or not all(number(transform.get(k,1),True) for k in ('scale_x','scale_y'))
            or not number(transform.get('rotation',0))):
        raise ValueError('Transform values must be finite, with positive dimensions and scales')
    if settings.get('point_origin','cell') not in ('cell','center'):
        raise ValueError('Point origin must be cell or center')
    if not all(isinstance(transform.get(k,False),bool) for k in ('flip_x','flip_y')):
        raise ValueError('Flip values must be boolean')
    matrix,origin=affine(settings)
    if not all(math.isfinite(value) for value in (*matrix,*origin)):
        raise ValueError('Transform arithmetic overflow')


def affine(settings, width=None, height=None):
    shape=settings['custom_shape']; a=settings.get('affine',{})
    width=shape['w'] if width is None else width
    height=shape['h'] if height is None else height
    sx=settings.get('scale',1)*a.get('scale_x',1)
    sy=settings.get('scale',1)*a.get('scale_y',1)
    angle=math.radians(a.get('rotation',0)); c,s=math.cos(angle),math.sin(angle)
    fx=-1 if a.get('flip_x',False) else 1
    fy=-1 if a.get('flip_y',False) else 1
    m=(c*sx*fx,-s*sy*fy,s*sx*fx,c*sy*fy)
    origin=(width*sx/2-(m[0]*width+m[1]*height)/2,
            height*sy/2-(m[2]*width+m[3]*height)/2)
    return m,origin


def world(settings, point):
    m,o=affine(settings)
    offset=0 if settings.get('point_origin','cell')=='center' else .5
    x,y=point['x']+offset,point['y']+offset
    return (settings['x']+o[0]+m[0]*x+m[1]*y,
            settings['y']+o[1]+m[2]*x+m[3]*y)


def bend(data, member_name, direction, strength=.25):
    if direction not in ('up','down') or not math.isfinite(strength) or not 0<strength<=.75:
        raise ValueError('Direction up/down and strength in (0,.75] required')
    members=[z for z in data['ctrl_zones'] if z.get('custom_zone_name')==member_name]
    if not members:raise ValueError('No matching ribbon members')
    reference=members[0]['settings']; original=copy.deepcopy(members)
    geometry=('x','y','scale','affine','point_origin')
    for z in members:
        settings=z['settings']
        if settings.get('shape')!=2:raise ValueError('CUSTOM geometry required')
        validate_transform(settings)
        if any(settings.get(k)!=reference.get(k) for k in geometry):
            raise ValueError('All ribbon strands must share their transform')
        if any(settings['custom_shape'][k]!=reference['custom_shape'][k] for k in ('w','h')):
            raise ValueError('All strands must share their canvas')
    positions=[p for z in members for p in z['settings']['custom_shape']['led_positions']]
    if not positions:raise ValueError('Empty ribbon')
    if not all(math.isfinite(p[k]) for p in positions for k in ('x','y')):
        raise ValueError('Nonfinite coordinates')
    low,high=min(p['x'] for p in positions),max(p['x'] for p in positions)
    if high<=low:raise ValueError('Ribbon must extend along local x')
    matrix,old_origin=affine(reference)
    transverse_y=matrix[3]
    if abs(transverse_y)<1e-8:raise ValueError('Rotated ribbon cannot bow vertically along its transverse axis')
    chord=(high-low)*math.hypot(matrix[0],matrix[2])
    transverse_scale=math.hypot(matrix[1],matrix[3])
    sign=(-1 if direction=='up' else 1)*(1 if transverse_y>0 else -1)
    amplitude=sign*strength*chord/transverse_scale
    if not math.isfinite(amplitude):raise ValueError('Ribbon curvature arithmetic overflow')
    for p in positions:
        t=(p['x']-low)/(high-low)
        p['y']+=amplitude*4*t*(1-t)
    dx=math.floor(min(p['x'] for p in positions));dy=math.floor(min(p['y'] for p in positions))
    width=max(1,math.ceil(max(p['x'] for p in positions)-dx+1))
    height=max(1,math.ceil(max(p['y'] for p in positions)-dy+1))
    _,new_origin=affine(reference,width,height)
    ox=reference['x']+old_origin[0]-new_origin[0]+matrix[0]*dx+matrix[1]*dy
    oy=reference['y']+old_origin[1]-new_origin[1]+matrix[2]*dx+matrix[3]*dy
    for member in members:
        settings=member['settings'];settings.update(x=ox,y=oy)
        shape=settings['custom_shape'];shape.update(w=width,h=height)
        for p in shape['led_positions']:p['x']-=dx;p['y']-=dy
    endpoint_error=0
    for before,after in zip(original,members):
        old=before['settings'];new=after['settings']
        old_points=old['custom_shape']['led_positions'];new_points=new['custom_shape']['led_positions']
        if [p['led_num'] for p in old_points]!=[p['led_num'] for p in new_points]:
            raise AssertionError('LED order changed')
        for p,q in zip(old_points,new_points):
            if p['x'] in (low,high):
                a,b=world(old,p),world(new,q)
                endpoint_error=max(endpoint_error,math.dist(a,b))
    if endpoint_error>1e-8:raise AssertionError('Ribbon endpoints moved')
    return {'name':member_name,'strands':len(members),'leds':len(positions),
            'direction':direction,'sag_canvas_units':strength*chord,'endpoint_error':endpoint_error}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--member-name',required=True);parser.add_argument('--direction',choices=['up','down'],required=True)
    parser.add_argument('--strength',type=float,default=.25)
    args=parser.parse_args()
    if args.output.exists() or args.output.resolve()==args.source.resolve():
        raise ValueError('Choose a new output file; source is preserved')
    data=json.loads(args.source.read_text(encoding='utf-8-sig'))
    report=bend(data,args.member_name,args.direction,args.strength)
    args.output.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))
