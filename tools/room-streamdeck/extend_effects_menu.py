"""Prepare one additive effect submenu without editing a running Elgato profile."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import uuid


def prepare(profile_dir, parent_page, profiles_dir, shortcuts, title, output):
    profile_dir, output = profile_dir.resolve(), output.resolve()
    if output == profile_dir or profile_dir in output.parents:
        raise ValueError('Preparation must be outside the live profile')
    if output.exists():
        raise FileExistsError(output)
    page = profile_dir/'Profiles'/parent_page
    original = (page/'manifest.json').read_bytes()
    parent = json.loads(original)
    actions = next(c for c in parent['Controllers'] if c['Type'] == 'Keypad')['Actions']
    back = actions.get('0,0', {})
    if back.get('UUID') != 'com.elgato.streamdeck.profile.backtoparent':
        raise ValueError('Parent must have an existing Back button')
    slot = next((f'{i % 5},{i // 5}' for i in range(1, 15) if f'{i % 5},{i // 5}' not in actions), None)
    if slot is None:
        raise ValueError('No free parent key; no action will be replaced')
    profiles = [json.loads(p.read_text(encoding='utf-8-sig')) for p in sorted(profiles_dir.glob('*.json'))]
    if not 1 <= len(profiles) <= 14:
        raise ValueError('One submenu requires 1 to 14 prepared profiles')
    child_id = str(uuid.uuid4()).upper()
    state = lambda label: [{'FontSize': 10, 'Image': 'Images/star.svg', 'ShowTitle': True,
                            'Title': label, 'TitleAlignment': 'bottom', 'TitleColor': '#ffffff', 'OutlineThickness': 2}]
    actions[slot] = {'ActionID': str(uuid.uuid4()), 'LinkedTitle': True, 'Name': 'Créer un dossier',
        'Resources': None, 'Settings': {'ProfileUUID': child_id.lower()}, 'State': 0,
        'States': state(title), 'UUID': 'com.elgato.streamdeck.profile.openchild'}
    child_back = copy.deepcopy(back)
    child_back['ActionID'] = str(uuid.uuid4())
    child = {'Controllers': [{'Type': 'Keypad', 'Actions': {'0,0': child_back}}], 'Icon': '', 'Name': title}
    entries = []
    for i, profile in enumerate(profiles, 1):
        effects = profile['plugins']['OpenRGB Effects Plugin']['Effects']
        if len(effects) != 1 or not effects[0]['EffectClassName'].startswith('SignalFavorite.'):
            raise ValueError('Expected one native effect per profile')
        name = profile['profile_name']
        shortcut = shortcuts.resolve()/(name+'.lnk')
        entries.append({'profile': name, 'path': str(shortcut)})
        child['Controllers'][0]['Actions'][f'{i % 5},{i // 5}'] = {
            'ActionID': str(uuid.uuid4()), 'LinkedTitle': True, 'Name': 'Ouvrir', 'Resources': None,
            'Settings': {'path': '"'+str(shortcut)+'"'}, 'State': 0,
            'States': state(effects[0]['CustomName']), 'UUID': 'com.elgato.streamdeck.system.open'}
    output.mkdir(parents=True)
    shutil.copytree(page, output/'backup-parent')
    for identifier, content in ((parent_page, parent), (child_id, child)):
        destination = output/'pages'/identifier
        destination.mkdir(parents=True)
        (destination/'manifest.json').write_text(json.dumps(content, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if (page/'Images').exists():
            shutil.copytree(page/'Images', destination/'Images')
    proposal = {'profile_directory': str(profile_dir), 'parent_page': parent_page, 'child_page': child_id,
                'added_slot': slot, 'source_manifest_sha256': hashlib.sha256(original).hexdigest(),
                'shortcuts': entries, 'live_files_changed': False, 'all_previous_actions_preserved': True}
    (output/'proposal.json').write_text(json.dumps(proposal, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return proposal


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('profile-dir', 'parent-page', 'profiles-dir', 'shortcuts', 'title', 'output'):
        parser.add_argument('--'+key, required=True)
    args = parser.parse_args()
    result = prepare(Path(args.profile_dir), args.parent_page, Path(args.profiles_dir), Path(args.shortcuts), args.title, Path(args.output))
    print(f'Prepared {len(result["shortcuts"])} native effect shortcuts; existing buttons preserved')
