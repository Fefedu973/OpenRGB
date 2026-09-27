"""Exercise the one-shot EXE against fake loopback SDK servers, never OpenRGB."""
# SPDX-License-Identifier: GPL-2.0-or-later
import argparse
import socket
import struct
import subprocess
import threading
import time

HEADER=struct.Struct('<4sIII')
def exact(sock,n):
    data=b''
    while len(data)<n:
        part=sock.recv(n-len(data))
        if not part: raise EOFError()
        data+=part
    return data
def run_case(exe,mode):
    commands=[];failures=[]
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(3)
    port=listener.getsockname()[1]
    profile='Musique été test'
    def server():
        try:
            with listener.accept()[0] as peer:
                peer.settimeout(2)
                def emit(kind,body=b'',magic=b'ORGB'):
                    frame=HEADER.pack(magic,0,kind,len(body))+body
                    # Real partial reads and interleaved asynchronous packet.
                    peer.sendall(frame[:7]);peer.sendall(frame[7:])
                def ack(kind,status=0):emit(10,struct.pack('<II',kind,status))
                while True:
                    magic,device,kind,size=HEADER.unpack(exact(peer,16));body=exact(peer,size);commands.append((kind,body))
                    assert magic==b'ORGB' and device==0
                    if mode=='silent':time.sleep(.4);return
                    if mode=='malformed':emit(40,struct.pack('<I',7),b'NOPE');return
                    if kind==40:
                        emit(40,struct.pack('<I',4 if mode=='old' else 7))
                        if mode=='old':return
                        ack(40)
                    elif kind==52:
                        assert body==struct.pack('<I',0), 'helper must not subscribe or request local client'
                        ack(52);emit(53,struct.pack('<I',4))
                    elif kind==152:
                        assert body==profile.encode()+b'\0'
                        emit(100)
                        ack(152,1 if mode=='reject' else 0)
                        if mode=='reject':return
                    elif kind==156:
                        emit(156,(profile if mode!='mismatch' else 'Different').encode()+b'\0');ack(156);return
                    else:raise AssertionError(kind)
        except (EOFError,ConnectionResetError,BrokenPipeError):pass
        except BaseException as error:failures.append(error)
        finally:listener.close()
    thread=threading.Thread(target=server);thread.start()
    started=time.monotonic()
    result=subprocess.run([str(exe),'--port',str(port),'--profile',profile,'--timeout-ms','250' if mode=='silent' else '2000'],timeout=4)
    thread.join(3)
    assert not thread.is_alive() and not failures, failures
    assert (result.returncode==0)==(mode=='ok'), (mode,result.returncode)
    assert sum(kind==152 for kind,_ in commands)<=1, 'profile load must never retry implicitly'
    if mode in ('silent','malformed','old'):assert not any(kind==152 for kind,_ in commands)
    if mode=='silent':assert time.monotonic()-started<1.5
    return commands

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--exe',required=True);args=parser.parse_args()
    for mode in ('ok','reject','mismatch','old','malformed','silent'):
        run_case(args.exe,mode);print('PASS',mode)
    print('6 fake-server scenarios passed; no live OpenRGB connection opened.')
