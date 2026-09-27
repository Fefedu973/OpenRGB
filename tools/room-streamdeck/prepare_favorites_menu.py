"""Prepare an additive Favorites folder; preserve every existing Lights action."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import uuid


def pages(source, profiles, shortcuts, child_id):
    parent=copy.deepcopy(source)
    keypad=next(c for c in parent['Controllers'] if c['Type']=='Keypad')
    back=keypad['Actions']['0,0']
    if back['UUID']!='com.elgato.streamdeck.profile.backtoparent':
        raise ValueError('Expected existing parent navigation')
    if len(profiles)>14 or not profiles:
        raise ValueError('One favorites page requires 1 to 14 profiles')
    free=next((f'{i%5},{i//5}' for i in range(1,15) if f'{i%5},{i//5}' not in keypad['Actions']),None)
    if free is None: raise ValueError('Lights has no free key; no existing action will be replaced')
    def state(title,icon):
        return [{'FontSize':10,'Image':'Images/'+icon,'ShowTitle':True,'Title':title,
                 'TitleAlignment':'bottom','TitleColor':'#ffffff','OutlineThickness':2}]
    keypad['Actions'][free]={'ActionID':str(uuid.uuid4()),'LinkedTitle':True,'Name':'Créer un dossier',
        'Resources':None,'Settings':{'ProfileUUID':child_id.lower()},'State':0,
        'States':state('Favoris','star.svg'),'UUID':'com.elgato.streamdeck.profile.openchild'}
    child_back=copy.deepcopy(back);child_back['ActionID']=str(uuid.uuid4())
    child={'Controllers':[{'Type':'Keypad','Actions':{'0,0':child_back}}],'Icon':'','Name':'Favoris SignalRGB'}
    entries=[]
    for index,profile in enumerate(profiles,1):
        name=profile['profile_name']
        effect=profile['plugins']['OpenRGB Effects Plugin']['Effects'][0]
        title=effect['CustomName']
        if not effect['EffectClassName'].startswith('SignalFavorite.'):
            raise ValueError('Favorite is not a native effect: '+name)
        shortcut=shortcuts/(name+'.lnk');entries.append({'profile':name,'path':str(shortcut)})
        icon=('star.svg' if any(word in title for word in ('Space','Galax','Aurora')) else
              'water.svg' if title=='Underwater' else 'rainbow.svg' if 'Rainbow' in title else 'colours.svg')
        display=title.replace(' ','\n',1) if len(title)>11 else title
        child['Controllers'][0]['Actions'][f'{index%5},{index//5}']={
            'ActionID':str(uuid.uuid4()),'LinkedTitle':True,'Name':'Ouvrir','Resources':None,
            'Settings':{'path':'"'+str(shortcut)+'"'},'State':0,'States':state(display,icon),
            'UUID':'com.elgato.streamdeck.system.open'}
    return parent,child,entries,free


def prepare(profile_dir, lights_page, profiles_dir, icons, helper, shortcuts, output):
    profile_dir=profile_dir.resolve();output=output.resolve()
    if output==profile_dir or profile_dir in output.parents: raise ValueError('Output must be outside live profile')
    if output.exists(): raise FileExistsError(output)
    page=profile_dir/'Profiles'/lights_page
    raw=(page/'manifest.json').read_bytes();source=json.loads(raw)
    profiles=[json.loads(p.read_text(encoding='utf-8-sig')) for p in sorted(profiles_dir.glob('Favori - *.json'))]
    child_id=str(uuid.uuid4()).upper()
    parent,child,entries,slot=pages(source,profiles,shortcuts.resolve(),child_id)
    output.mkdir(parents=True)
    shutil.copytree(page,output/'backup-original-lights')
    for ident,content in ((lights_page,parent),(child_id,child)):
        destination=output/'pages'/ident;destination.mkdir(parents=True)
        (destination/'manifest.json').write_text(json.dumps(content,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (destination/'Images').mkdir()
        for name in ('star.svg','water.svg','rainbow.svg','colours.svg'):
            shutil.copy2(icons/name,destination/'Images'/name)
    report={'profile_directory':str(profile_dir),'lights_page':lights_page,'favorites_page':child_id,
        'source_manifest_sha256':hashlib.sha256(raw).hexdigest(),'added_slot':slot,
        'helper':str(helper.resolve()),'helper_sha256':hashlib.sha256(helper.read_bytes()).hexdigest(),
        'shortcuts':entries,'requires_profiles':[e['profile'] for e in entries],
        'all_previous_actions_preserved':True,'live_files_changed':False}
    (output/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('profile-dir','lights-page','profiles-dir','icons','helper','shortcuts','output'):p.add_argument('--'+key,required=True)
    a=p.parse_args()
    r=prepare(Path(a.profile_dir),a.lights_page,Path(a.profiles_dir),Path(a.icons),Path(a.helper),Path(a.shortcuts),Path(a.output))
    print(f'Prepared {len(r["shortcuts"])} favorites; existing Lights actions preserved; no live files changed')
