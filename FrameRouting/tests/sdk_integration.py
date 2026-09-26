"""Exercise the real SDK server with only synthetic virtual image outputs.

Never uses the user's OpenRGB configuration or starts hardware detection.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

repo=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('image_client',repo/'tools/room-sdk/image_client.py')
sdk=importlib.util.module_from_spec(spec); spec.loader.exec_module(sdk)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--openrgb',type=Path,required=True)
    p.add_argument('--reader',type=Path,required=True)
    p.add_argument('--qt-bin',type=Path)
    p.add_argument('--native-client',type=Path)
    p.add_argument('--report',type=Path)
    a=p.parse_args()
    env=os.environ.copy()
    if a.qt_bin: env['PATH']=str(a.qt_bin)+os.pathsep+env['PATH']
    env['QT_QPA_PLATFORM']='offscreen'
    with socket.socket() as reserved:
        reserved.bind(('127.0.0.1',0)); port=reserved.getsockname()[1]
    report={'mode':'virtual-only','hardwareDetection':False,'checks':[]}
    channel='room-sdk-'+str(os.getpid())
    flags=getattr(subprocess,'CREATE_NO_WINDOW',0)
    with tempfile.TemporaryDirectory(prefix='OpenRGB-Room-SDK-test-') as directory:
        config=Path(directory)
        outputs=[{'id':'sdk-a','name':'SDK Screen A','width':800,'height':600,'channel':channel+'-a',
                  'fps':30,'compatibility_width':32,'compatibility_height':18},
                 {'id':'sdk-b','name':'SDK Screen B','width':321,'height':197,'channel':channel+'-b',
                  'fps':30,'compatibility_width':17,'compatibility_height':11}]
        saved_client_trap=socket.socket()
        saved_client_trap.bind(('127.0.0.1',0));saved_client_trap.listen(1);saved_client_trap.settimeout(.1)
        settings={'VirtualScreens':{'enabled':True,'outputs':outputs},
                  'Client':{'clients':[{'ip':'127.0.0.1','port':saved_client_trap.getsockname()[1]}]}}
        (config/'OpenRGB.json').write_text(json.dumps(settings),encoding='utf-8')
        with (config/'server.log').open('w',encoding='utf-8') as log:
            server=subprocess.Popen([str(a.openrgb.resolve()),'--virtual-only','--noautoconnect','--server',
                '--server-host','127.0.0.1','--server-port',str(port),'--config',str(config)],
                stdout=log,stderr=subprocess.STDOUT,env=env,creationflags=flags)
            client=None
            try:
                deadline=time.monotonic()+25
                while time.monotonic()<deadline:
                    if server.poll() is not None: raise RuntimeError('Virtual-only server exited before SDK startup')
                    try:
                        client=sdk.Client(port=port)
                        if len(client.controllers())==2: break
                        client.close(); client=None
                    except (OSError,ConnectionError): pass
                    time.sleep(.1)
                if client is None: raise TimeoutError('Virtual-only server did not enumerate two screens')
                try:
                    unexpected,_=saved_client_trap.accept();unexpected.close()
                    raise AssertionError('Virtual-only connected a saved network client')
                except socket.timeout: pass
                report['checks'].append('Virtual-only ignores saved remote-client connections')
                ids=client.controllers()
                descriptors=[client.request(1,device,reply_id=1) for device in ids]
                surfaces=[client.outputs(device) for device in ids]
                assert len(surfaces[0])==len(surfaces[1])==1
                by_size={(s[0]['width'],s[0]['height']):device for device,s in zip(ids,surfaces)}
                assert (800,600) in by_size and (321,197) in by_size
                report['checks'].append('SDK7 capability negotiation and two arbitrary image outputs')
                # Same controller description at SDK6/7 proves no implicit widening
                # of the old LED format. Each peer uses its negotiated version.
                legacy=sdk.Client(port=port,version=6)
                try:
                    assert not legacy.flags & sdk.IMAGE_FLAG
                    assert legacy.outputs(ids[0])==[]
                    old=legacy.request(1,ids[0],reply_id=1)
                    new=client.request(1,ids[0],reply_id=1)
                    assert old==new
                    try:
                        legacy.request(sdk.REQUEST_OUTPUTS,ids[0],reply_id=sdk.REQUEST_OUTPUTS)
                        raise AssertionError('SDK6 image request was accepted')
                    except RuntimeError as e: assert 'status 2' in str(e)
                finally: legacy.close()
                v4=sdk.Client(port=port,version=4)
                try:
                    assert v4.controllers()==[0,1]
                    desc=v4.request(1,0,struct.pack('<I',4),reply_id=1)
                    assert struct.unpack_from('<I',desc)[0]==len(desc)
                finally: v4.close()
                report['checks'].append('SDK4 enumeration and SDK6 LED descriptor compatibility')
                pixels=sdk.gradient(800,600)
                for dims,suffix,mapping in [((800,600),'-a',(0.,0.,1.,0.,0.,1.,1.)),
                                            ((321,197),'-b',(1.,0.,-1.,0.,0.,1.,.5))]:
                    reader=subprocess.Popen([str(a.reader.resolve()),channel+suffix,'5000'],stdout=subprocess.PIPE,
                                            stderr=subprocess.PIPE,text=True,env=env,creationflags=flags)
                    try:
                        client.send_image(by_size[dims],0,800,600,pixels,sequence=42,lease_ms=4000,mapping=mapping)
                        stdout,stderr=reader.communicate(timeout=6)
                        assert reader.returncode==0,(stdout,stderr)
                        observed=json.loads(stdout)
                        assert (observed['width'],observed['height'])==dims, observed
                        expected=([[0,0,64,255],[255,0,64,255],[0,255,64,255],[255,255,64,255]] if suffix=='-a'
                                  else [[128,0,32,255],[0,0,32,255],[128,128,32,255],[0,128,32,255]])
                        for sample,color in zip(observed['samples'],expected):
                            assert all(abs(actual-wanted)<=1 for actual,wanted in zip(sample['rgba'],color)),observed
                        report.setdefault('surfaces',[]).append(observed)
                    finally:
                        if reader.poll() is None: reader.terminate(); reader.wait(timeout=3)
                report['checks'].append('800x600 frames traverse real TCP server and native output to shared memory')
                malformed=struct.pack('<8IQ7dI',sdk.MAGIC,1,0,800,600,3200,1,1000,55,0,0,1,0,0,1,1,800*600*4)
                try:
                    client.request(sdk.UPDATE_IMAGE,ids[0],malformed)
                    raise AssertionError('Truncated image accepted')
                except RuntimeError as e: assert 'status 5' in str(e)
                report['checks'].append('Malformed image returns INVALID_DATA without disconnecting SDK')
                assert len(client.controllers())==2
                # Every abandoned 64 MiB body must release the shared receive
                # reservation; otherwise later clients exhaust its bounded pool.
                for _ in range(4):
                    interrupted=sdk.Client(port=port)
                    try:
                        interrupted.sock.sendall(struct.pack('<4sIII',b'ORGB',ids[0],sdk.UPDATE_IMAGE,sdk.MAX_BYTES+100)+b'x')
                        interrupted.sock.shutdown(socket.SHUT_WR)
                        interrupted.sock.settimeout(2)
                        while interrupted.sock.recv(4096): pass
                    finally: interrupted.close()
                client.send_image(ids[0],0,800,600,pixels,lease_ms=1000)
                report['checks'].append('Interrupted large image bodies release receive budget; subsequent frames work')
                if a.native_client:
                    result=subprocess.run([str(a.native_client.resolve()),str(port)],env=env,creationflags=flags,
                                          text=True,capture_output=True,timeout=35)
                    assert result.returncode==0,(result.stdout,result.stderr)
                    report['nativeClient']=json.loads(next(line for line in reversed(result.stdout.splitlines()) if line.startswith('{"ok":')))
                    # The native adapter's final frame uses blue=173. Verify it
                    # reaches both native outputs, not just the TCP ACK path.
                    for suffix in ('-a','-b'):
                        deadline=time.monotonic()+2
                        while True:
                            read=subprocess.run([str(a.reader.resolve()),channel+suffix,'1000'],env=env,
                                creationflags=flags,text=True,capture_output=True,timeout=2)
                            observed=json.loads(read.stdout) if read.returncode==0 else {}
                            samples=observed.get('samples',[])
                            if len(samples)==5 and all(s['rgba'][2]==173 for s in samples): break
                            if time.monotonic()>deadline: raise AssertionError(('Native frame not displayed',observed))
                            time.sleep(.02)
                    report['checks'].append('Real NetworkClient/RGBController_Network image adapter')
                    fixture_spec=importlib.util.spec_from_file_location('capability_fixture',repo/'tests/room-network-images/capability_fixture.py')
                    fixture=importlib.util.module_from_spec(fixture_spec);fixture_spec.loader.exec_module(fixture)
                    report['legacyNativeClients']=fixture.run_capability_fixtures(a.native_client.resolve(),env,descriptors)
            except Exception:
                log.flush()
                print((config/'server.log').read_text(encoding='utf-8',errors='replace')[-6000:])
                raise
            finally:
                saved_client_trap.close()
                if client: client.close()
                if server.poll() is None: server.terminate()
                server.wait(timeout=5)
    report['passed']=True
    if a.report:
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()
