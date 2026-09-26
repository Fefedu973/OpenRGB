"""Copy an existing companion configuration into private native OpenRGB inputs.

Only creates new files in the requested directory; never starts a driver or edits
the old bridge. Treat this output directory as private, including its key file.
"""
import argparse
import json
import re
from pathlib import Path

def convert(source, destination):
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    key_path = destination / 'govee-communication.key'
    config_path = destination / 'govee-ble.local.json'
    if key_path.exists() or config_path.exists():
        raise ValueError('Destination already configured; refusing to overwrite')
    data = json.loads(source.read_text(encoding='utf-8-sig'))
    key = data['communication_key_hex'].strip()
    if not re.fullmatch('[0-9a-fA-F]{32}', key):
        raise ValueError('Invalid private communication key')
    devices = []
    for item in data['devices']:
        classic = item.get('model') == 'H6159' or 'h6159' in item.get('profile', '').lower()
        result = {'profile': 'h6159-classic-v1' if classic else 'h6008-realtime-v1',
                  'address': item['ble_address'], 'name': item.get('name', ''),
                  'enabled': True, 'power_on_acquire': False}
        if not classic:
            result['wifi_mac'] = item['wifi_mac']
        devices.append(result)
    # Exclusive creation also protects against accidental reuse between checks.
    with key_path.open('x', encoding='ascii') as output: output.write(key + '\n')
    with config_path.open('x', encoding='utf-8') as output:
        json.dump({'key_file': str(key_path), 'devices': devices}, output, indent=2, ensure_ascii=False)
    print(json.dumps({'created': str(config_path), 'devices': len(devices), 'hardwareStarted': False}))
    return config_path

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('destination', type=Path)
    a=p.parse_args(); convert(a.source,a.destination)
