"""Adversarial capability peers for the real C++ NetworkClient.

Input descriptors must come from a real, synthetic-only SDK6 server. Only TCP
loopback is opened; this module never starts OpenRGB or hardware detection.
"""
import json
import socket
import struct
import subprocess
import threading
import time

IMAGE_FLAG=1<<29
REQUEST_OUTPUTS=0x524D0001
UPDATE_IMAGE=0x524D0002

def run_capability_fixtures(executable,env,descriptors):
    if len(descriptors)!=2: raise ValueError('Two real synthetic descriptors required')
    results=[]
    for version,flags,label in [(6,1|IMAGE_FLAG,'SDK6 even with image bit'),(7,1,'SDK7 without image bit')]:
        errors=[];packets=[]
        listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(15)
        port=listener.getsockname()[1]
        def serve():
            try:
                connection,_=listener.accept()
                with connection:
                    connection.settimeout(15)
                    def exact(n):
                        value=bytearray()
                        while len(value)<n:
                            data=connection.recv(n-len(value))
                            if not data: raise EOFError()
                            value.extend(data)
                        return bytes(value)
                    def reply(kind,device,body):
                        header=struct.pack('<4sIII',b'ORGB',device,kind,len(body))
                        # Deliberately split every header and body across sends.
                        for byte in header:
                            connection.sendall(bytes([byte]));time.sleep(.001)
                        for offset in range(0,len(body),31):connection.sendall(body[offset:offset+31])
                    while True:
                        try: magic,device,kind,length=struct.unpack('<4sIII',exact(16))
                        except EOFError: break
                        if magic!=b'ORGB' or length>1024*1024: raise AssertionError('Invalid client envelope')
                        body=exact(length);packets.append(kind)
                        if kind in (REQUEST_OUTPUTS,UPDATE_IMAGE):raise AssertionError('Client sent image extension without both gates')
                        if kind==40:reply(40,device,struct.pack('<I',version))
                        elif kind==52:reply(53,device,struct.pack('<I',flags))
                        elif kind==0:reply(0,device,struct.pack('<III',2,1001,1002))
                        elif kind==1:
                            if device not in (1001,1002):raise AssertionError('Wrong synthetic device ID')
                            reply(1,device,descriptors[device-1001])
            except Exception as error:errors.append(repr(error))
            finally:listener.close()
        thread=threading.Thread(target=serve,daemon=True);thread.start()
        try:
            result=subprocess.run([str(executable),str(port),'legacy'],env=env,text=True,capture_output=True,
                timeout=20,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        finally:
            thread.join(timeout=2)
            if thread.is_alive():listener.close()
        if thread.is_alive():raise AssertionError('Fixture did not stop')
        if errors:raise AssertionError(errors)
        if result.returncode:raise AssertionError((label,result.stdout,result.stderr))
        observation=json.loads(next(line for line in reversed(result.stdout.splitlines()) if line.startswith('{\"ok\":')))
        if not observation['ok'] or not observation['legacy']:raise AssertionError(observation)
        results.append({'case':label,'passed':True,'fragmentedReplies':True,'imageRequests':0,'stop_ms':observation['stop_ms'],'packets':len(packets)})
    return results
