"""Prepare one control-toggle button without rebuilding existing Lights pages."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse,copy,hashlib,json,shutil,uuid
from pathlib import Path

def add_button(source,shortcut,slot='0,2'):
    page=copy.deepcopy(source)
    keypad=next(c for c in page['Controllers'] if c['Type']=='Keypad')
    if slot in keypad['Actions']:
        raise ValueError('Refusing to overwrite an existing button')
    keypad['Actions'][slot]={
        'ActionID':str(uuid.uuid4()),'LinkedTitle':True,'Name':'Ouvrir','Resources':None,
        'Settings':{'path':'"'+str(shortcut)+'"'},'State':0,
        'States':[{'FontSize':11,'Image':'Images/light-bulb.svg','ShowTitle':True,
                   'Title':'Principale\nOpenRGB','TitleAlignment':'bottom','TitleColor':'#ffffff','OutlineThickness':2}],
        'UUID':'com.elgato.streamdeck.system.open'}
    return page

def prepare(profile,page_id,shortcut,output):
    profile=profile.resolve();output=output.resolve();uuid.UUID(page_id)
    if output==profile or profile in output.parents or output.exists():
        raise ValueError('Proposal must be new and outside the live profile')
    page=profile/'Profiles'/page_id;manifest=page/'manifest.json'
    original=manifest.read_bytes();source=json.loads(original)
    if not (page/'Images'/'light-bulb.svg').is_file():raise FileNotFoundError('Existing light-bulb icon missing')
    changed=add_button(source,shortcut.resolve())
    output.mkdir(parents=True);shutil.copytree(page,output/'backup-original-lights')
    (output/'manifest.json').write_text(json.dumps(changed,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    report={'live_manifest':str(manifest),'original_sha256':hashlib.sha256(original).hexdigest(),
            'proposal_sha256':hashlib.sha256((output/'manifest.json').read_bytes()).hexdigest(),
            'shortcut':str(shortcut.resolve()),'slot':'0,2','live_files_changed':False}
    (output/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('profile','page','shortcut','output'):p.add_argument('--'+name,required=True)
    a=p.parse_args();print(json.dumps(prepare(Path(a.profile),a.page,Path(a.shortcut),Path(a.output)),indent=2))
