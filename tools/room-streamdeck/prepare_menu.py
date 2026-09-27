"""Prepare reviewed Elgato page replacements outside the live profile tree."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import uuid

MAIN=[('Blanc','Full Blanc','light-bulb.svg'),('Éteindre','Full Noir','circle-with-cross.svg'),
      ('Musique','Music - Room Pulse','music.svg'),('Ambilight','Ambilight - Web Page','tv.svg'),
      ('Rainbow','Full Scale - Rainbow','rainbow.svg')]
OTHER=[('Plasma','Effet - Plasma','colours.svg'),('Feu','Effet - Feu','flash.svg'),
       ('Aurore','Effet - Aurore','air.svg'),('Ondes','Effet - Ondes','water.svg'),
       ('Étoiles','Effet - Etoiles','star.svg'),('Bulles','Effet - Bulles','circle.svg')]

def build_pages(source, shortcut_directory, other_id):
    page=copy.deepcopy(source)
    keypad=next(c for c in page['Controllers'] if c['Type']=='Keypad')
    back=keypad['Actions']['0,0']
    if back['UUID']!='com.elgato.streamdeck.profile.backtoparent':
        raise ValueError('Expected the verified parent-navigation action at 0,0')
    shortcuts=[]
    def action(title,profile,icon):
        shortcut=shortcut_directory/(profile+'.lnk')
        shortcuts.append({'profile':profile,'path':str(shortcut)})
        return {'ActionID':str(uuid.uuid4()),'LinkedTitle':True,'Name':'Ouvrir','Resources':None,
                'Settings':{'path':'"'+str(shortcut)+'"'},'State':0,
                'States':[{'FontSize':11,'Image':'Images/'+icon,'ShowTitle':True,'Title':title,
                           'TitleAlignment':'bottom','TitleColor':'#ffffff','OutlineThickness':2}],
                'UUID':'com.elgato.streamdeck.system.open'}
    actions={'0,0':copy.deepcopy(back)}
    for index,entry in enumerate(MAIN,1):actions[f'{index%5},{index//5}']=action(*entry)
    actions['1,1']={'ActionID':str(uuid.uuid4()),'LinkedTitle':True,'Name':'Créer un dossier',
        'Resources':None,'Settings':{'ProfileUUID':other_id.lower()},'State':0,
        'States':[{'FontSize':10,'Image':'Images/folder.svg','ShowTitle':True,'Title':'Autres\neffets',
                   'TitleAlignment':'bottom','TitleColor':'#ffffff','OutlineThickness':2}],
        'UUID':'com.elgato.streamdeck.profile.openchild'}
    keypad['Actions']=actions
    child_back=copy.deepcopy(back);child_back['ActionID']=str(uuid.uuid4())
    child={'Controllers':[{'Type':'Keypad','Actions':{'0,0':child_back}}],'Icon':'','Name':'Autres effets'}
    for index,entry in enumerate(OTHER,1):child['Controllers'][0]['Actions'][f'{index%5},{index//5}']=action(*entry)
    return page,child,shortcuts

def prepare(profile_dir,lights_page,icon_dir,helper,output,shortcut_directory):
    profile_dir=profile_dir.resolve();output=output.resolve();helper=helper.resolve()
    if output==profile_dir or profile_dir in output.parents:
        raise ValueError('Output must be outside the live profile')
    if output.exists():raise FileExistsError('Refusing to overwrite an existing proposal')
    if not helper.is_file():raise ValueError('Compiled helper is required')
    uuid.UUID(lights_page)
    page_dir=next(p for p in (profile_dir/'Profiles').iterdir() if p.name.lower()==lights_page.lower())
    manifest=page_dir/'manifest.json';original=manifest.read_bytes();source=json.loads(original)
    icons={entry[2] for entry in MAIN+OTHER}|{'folder.svg'}
    for icon in icons:
        if not (icon_dir/icon).is_file():raise FileNotFoundError(icon)
    other_id=str(uuid.uuid4()).upper()
    page,child,shortcuts=build_pages(source,shortcut_directory.resolve(),other_id)
    output.mkdir(parents=True)
    shutil.copytree(page_dir,output/'backup-original-lights')
    for ident,content in ((page_dir.name,page),(other_id,child)):
        destination=output/'pages'/ident;destination.mkdir(parents=True)
        (destination/'manifest.json').write_text(json.dumps(content,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (destination/'Images').mkdir()
        for icon in icons:shutil.copyfile(icon_dir/icon,destination/'Images'/icon)
    report={'profile_directory':str(profile_dir),'lights_page':page_dir.name,'other_page':other_id,
            'source_manifest_sha256':hashlib.sha256(original).hexdigest(),
            'helper':str(helper),'helper_sha256':hashlib.sha256(helper.read_bytes()).hexdigest(),
            'shortcuts':shortcuts,'requires_profiles':[x['profile'] for x in shortcuts],
            'navigation_preserved':page['Controllers'][0]['Actions']['0,0']==source['Controllers'][0]['Actions']['0,0'],
            'live_files_changed':False,'icon_source':str(icon_dir),
            'install_note':'Close Elgato normally; backup full sdProfile; verify source hash; copy reviewed page and new child; create shortcuts; restart Elgato.'}
    (output/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('profile-dir','lights-page','icon-dir','helper','output','shortcut-directory'):p.add_argument('--'+name,required=True)
    a=p.parse_args()
    result=prepare(Path(a.profile_dir),a.lights_page,Path(a.icon_dir),Path(a.helper),Path(a.output),Path(a.shortcut_directory))
    print(f'Prepared {len(result["shortcuts"])} profile buttons in two pages. No live files changed.')
