"""DeviceControl real executable / fake SDK integration tests. No live hardware."""
import argparse,json,socket,struct,subprocess,threading
from test_profile_select import exact,HEADER
U=lambda x:struct.pack('<I',x)
H=lambda x:struct.pack('<H',x)
S=lambda x:H(len(x.encode())+1)+x.encode()+b'\0'
SERIAL='AA:BB:CC:DD:EE:FF'
FLAGS=1|(1<<16)|(1<<17)|(1<<24)
NAME='Lumière principale'
def descriptor(serial=SERIAL,enabled=True,flags=FLAGS,malformed=False):
    # Exercise modes, zones, segments, colors, LED names and UTF-8 display name.
    mode=S('Direct')+U(0)*11+H(1)+U(0xffffff)
    segment=S('Segment')+U(0)*3+H(0)+U(0)
    zone=S('Whole device')+U(0)*4+H(0)+H(1)+segment+U(0)*2+H(1)+mode+S('Zone')
    data=U(0)+S('Govee H6008')+S('Govee')+S('Native Bluetooth H6008')+S('h6008-realtime-v1')+S(serial)+S('Bluetooth')
    data+=H(1)+U(0)+mode+H(1)+zone+H(1)+S('RGB')+H(1)+U(0xffffff)+H(1)+S('LED')+U(flags)+S(NAME)
    config=json.dumps({'schema':{'control_enabled':{'type':'bool'}},'configuration':{'control_enabled':enabled,'future_option':19}}).encode()+b'\0'
    data+=U(len(config))+config
    result=U(len(data)+4)+data
    return result[:-7] if malformed else result

def case(exe,scenario,action='toggle'):
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(3)
    port=listener.getsockname()[1];errors=[];writes=[];state={'enabled':scenario!='enable','flags':FLAGS}
    def server():
        try:
            with listener.accept()[0] as peer:
                peer.settimeout(3)
                while True:
                    magic,device,kind,size=HEADER.unpack(exact(peer,16));body=exact(peer,size)
                    assert magic==b'ORGB'
                    def emit(k,b):
                        raw=HEADER.pack(b'ORGB',device,k,len(b))+b;peer.sendall(raw[:5]);peer.sendall(raw[5:])
                    def ack(status=0):emit(10,U(kind)+U(status))
                    if kind==40:emit(40,U(6 if scenario=='old' else 7));ack()
                    elif kind==52:
                        assert body==U(1<<16);emit(53,U(1<<16));ack()
                    elif kind==0:
                        ids=[41,98] if scenario=='duplicate' else [41]
                        emit(0,U(len(ids))+b''.join(U(x) for x in ids));ack()
                    elif kind==1:
                        assert body==U(7) and device in (41,98)
                        emit(1,descriptor('different' if scenario=='absent' else SERIAL,state['enabled'],state['flags'],scenario=='malformed'));ack()
                    elif kind==1130:
                        writes.append(kind);assert device==41
                        value=json.loads(body[:-1]);assert body[-1:]==b'\0' and value['future_option']==19
                        if scenario=='reject':ack(1);return
                        if scenario!='ignored':state['enabled']=value['control_enabled']
                        ack()
                    elif kind==1004:
                        writes.append(kind);assert device==41
                        assert body==U(11+len(NAME.encode()))+U(FLAGS|(1<<25))+S(NAME)
                        state['flags']=FLAGS|(1<<25);ack()
                    else:raise AssertionError(kind)
        except (EOFError,ConnectionResetError,BrokenPipeError):pass
        except BaseException as error:errors.append(error)
        finally:listener.close()
    t=threading.Thread(target=server);t.start()
    result=subprocess.run([exe,'--serial',SERIAL,'--control',action,'--port',str(port),'--timeout-ms','2000'],timeout=4)
    t.join(3)
    assert not t.is_alive() and not errors, errors
    success=scenario in ('toggle','enable','off','on')
    assert (result.returncode==0)==success,(scenario,result.returncode)
    if success:
        assert writes==[1130,1004]
        assert state['enabled']==(scenario in ('enable','on'))
    elif scenario in ('old','absent','duplicate','malformed'):assert writes==[],writes
    print('PASS',scenario)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--exe',required=True);a=p.parse_args()
    for scenario in ('toggle','enable','off','on','old','absent','duplicate','malformed','reject','ignored'):
        case(a.exe,scenario,scenario if scenario in ('on','off') else 'toggle')
