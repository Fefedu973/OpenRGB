"""Read cached Stream Deck diagnostics over Room SDK7; never change a device.

Only protocol negotiation, controller enumeration and descriptor reads are sent.
No profile save/load, frame, color, mode or configuration write is performed.
After the first lookup, only this controller is read (cached metadata, no Frida
RPC). Samples are >=2 seconds apart because the native worker caches status at
that cadence. LCD output still requires visual confirmation.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
from datetime import datetime, timezone
import json
import struct
import time
from image_client import Client

TARGET = 'Stream Deck Background Canvas'
MAX_CONFIG = 32768


def name_of(body):
    if len(body) < 11 or struct.unpack_from('<I', body)[0] != len(body):
        raise ValueError('Invalid controller descriptor size')
    length, = struct.unpack_from('<H', body, 8)
    if not 1 <= length <= len(body)-10 or body[9+length] != 0:
        raise ValueError('Invalid controller name')
    return body[10:9+length].decode('utf-8')


def runtime_of(body):
    # SDK6/7 GetDeviceDescriptionData ends with uint32 configuration length and
    # the NUL-terminated JSON string. Validate that exact suffix boundary rather
    # than interpreting unrelated LED/matrix structures or searching raw text.
    if not body or body[-1] != 0:
        raise ValueError('Missing final configuration terminator')
    start = max(12, len(body)-MAX_CONFIG)
    while True:
        start = body.find(b'{', start, len(body)-1)
        if start < 0:
            return None
        size, = struct.unpack_from('<I', body, start-4)
        if size == len(body)-start:
            try:
                configuration = json.loads(body[start:-1])
                runtime = configuration.get('configuration', {}).get('runtime')
                if isinstance(runtime, dict):
                    # This controller publishes aggregates only. Do not include
                    # names, serial numbers, locations, pixels or other settings.
                    return {k: runtime[k] for k in
                            ('state', 'input', 'error', 'submitted', 'accepted', 'failures', 'compositor')
                            if k in runtime}
            except (ValueError, AttributeError):
                pass
        start += 1


class DiagnosticFreshness:
    """Age repeated SDK snapshots locally; never label an old RPC sample new.

    ageMs is the age when the worker published the metadata, not a live SDK
    clock. observedAgeMs adds elapsed observation time for that same sample.
    No cross-process assumption about the C++ steady clock epoch is required.
    """
    def __init__(self):
        self.key = None
        self.first_seen = None
        self.initial_age = None

    def annotate(self, runtime, now):
        if not isinstance(runtime, dict) or not isinstance(runtime.get('compositor'), dict):
            return runtime
        result = dict(runtime)
        compositor = dict(runtime['compositor'])
        result['compositor'] = compositor
        sample = compositor.get('sampledSteadyMs')
        age = compositor.get('ageMs')
        if not isinstance(sample, (int, float)) or isinstance(sample, bool):
            compositor['observedAgeMs'] = None
            compositor['stale'] = True
            return result
        key = (sample, compositor.get('uptimeMs'))
        repeated = key == self.key and self.first_seen is not None
        valid_age = isinstance(age, (int, float)) and not isinstance(age, bool) and age >= 0
        if not repeated:
            self.key, self.first_seen = key, now
            self.initial_age = age if valid_age else None
        observed = None if self.initial_age is None else max(0, self.initial_age + (now-self.first_seen)*1000)
        if observed is not None and valid_age:
            observed = max(observed, age)
        compositor['observedAgeMs'] = None if observed is None else round(observed)
        compositor['cached'] = bool(compositor.get('cached')) or repeated
        compositor['stale'] = bool(compositor.get('stale')) or (observed is not None and observed > 5000) or compositor.get('transportState') in ('recovering', 'faulted', 'closed', 'disconnected')
        return result


def self_test():
    name = TARGET.encode()+b'\0'
    configuration = json.dumps({'schema': {}, 'configuration': {
        'runtime': {'state': 'streaming', 'accepted': 12,
                    'compositor': {'errors': 0, 'lastFrameCoverage': {'empty': 7}}}}}).encode()+b'\0'
    body = (b'\0'*8 + struct.pack('<H',len(name))+name + b'arbitrary zone bytes' +
            struct.pack('<I',len(configuration))+configuration)
    body = struct.pack('<I',len(body))+body[4:]
    assert name_of(body) == TARGET
    assert runtime_of(body)['compositor']['lastFrameCoverage']['empty'] == 7
    assert runtime_of(body[:-len(configuration)-4]+b'not-json\0') is None
    corrupted = body[:-len(configuration)-4]+struct.pack('<I',2)+configuration
    assert runtime_of(corrupted) is None
    freshness = DiagnosticFreshness()
    runtime = {'error': 'bounded transport error', 'compositor': {
        'sampledSteadyMs': 123, 'ageMs': 0, 'uptimeMs': 900,
        'ready': True, 'cached': False, 'transportState': 'active'}}
    assert freshness.annotate(runtime, 1)['compositor']['observedAgeMs'] == 0
    older = freshness.annotate(runtime, 8)['compositor']
    assert older['observedAgeMs'] == 7000 and older['cached'] and older['stale']
    assert runtime['compositor']['ageMs'] == 0 and 'observedAgeMs' not in runtime['compositor']
    recovering = {'compositor': dict(runtime['compositor'], transportState='recovering', ageMs=8000)}
    assert freshness.annotate(recovering, 9)['compositor']['observedAgeMs'] == 8000
    new = {'compositor': dict(runtime['compositor'], sampledSteadyMs=124, uptimeMs=1900)}
    assert freshness.annotate(new, 10)['compositor']['observedAgeMs'] == 0
    assert freshness.annotate({'compositor': {'sampledSteadyMs': None}}, 11)['compositor']['stale']
    print('Offline descriptor parser: PASS (no socket opened)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=6742)
    parser.add_argument('--device',type=int,help='Known SDK7 controller ID; skips initial enumeration')
    parser.add_argument('--samples',type=int,default=3)
    parser.add_argument('--interval',type=float,default=2.1)
    parser.add_argument('--self-test',action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not 1 <= args.samples <= 20 or not 2 <= args.interval <= 60:
        parser.error('Use 1..20 samples and an interval of 2..60 seconds')
    client = Client('127.0.0.1',args.port,version=7)
    try:
        if client.version != 7:
            raise RuntimeError('This reader requires Room SDK7')
        selected = args.device
        first = None
        for device in [selected] if selected is not None else client.controllers():
            body = client.request(1,device,struct.pack('<I',7),reply_id=1)
            if name_of(body) == TARGET:
                selected, first = device, body
                break
        if first is None:
            raise RuntimeError('Stream Deck background controller not found')
        freshness = DiagnosticFreshness()
        for sample in range(args.samples):
            if sample:
                time.sleep(args.interval)
                first = client.request(1,selected,struct.pack('<I',7),reply_id=1)
                if name_of(first) != TARGET:
                    raise RuntimeError('Controller changed during observation; rerun discovery')
            print(json.dumps({'utc': datetime.now(timezone.utc).isoformat(),
                              'sdk_device': selected, 'runtime': freshness.annotate(runtime_of(first), time.monotonic())},ensure_ascii=False),flush=True)
    finally:
        client.close()


if __name__ == '__main__':
    main()
