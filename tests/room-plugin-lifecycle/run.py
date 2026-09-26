"""Build the actual coordinator with synthetic controller/registry boundaries.
Run from an MSVC developer prompt; no application, device, network or capture.
"""
import argparse
from pathlib import Path
import os
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--qt-root',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[2]
a.out.mkdir(parents=True,exist_ok=True)
source=(repo/'OpenRGBPluginAPI.cpp').read_text(encoding='utf-8')
coordinator=source.split('// BEGIN VIRTUAL_CONTROLLER_LIFECYCLE\n',1)[1].split('// END VIRTUAL_CONTROLLER_LIFECYCLE',1)[0]
(a.out/'lifecycle-under-test.inc').write_text(coordinator,encoding='utf-8')
env=os.environ.copy();env['PATH']=str(a.qt_root/'bin')+os.pathsep+env['PATH']
for gui in (False,True):
    name='lifecycle-qt' if gui else 'lifecycle-no-gui'
    exe=a.out/(name+'.exe')
    args=['cl','/nologo','/std:c++17','/EHsc','/MD','/utf-8','/permissive-','/Zc:__cplusplus',
          '/I'+str(a.out),str(Path(__file__).with_name('test_lifecycle.cpp')),
          '/Fo'+str(a.out/(name+'.obj')),'/Fe'+str(exe)]
    if gui:
        args+=['/I'+str(a.qt_root/'include'),'/I'+str(a.qt_root/'include/QtCore'),
               '/DQT_NO_DEBUG','/link','/LIBPATH:'+str(a.qt_root/'lib'),'Qt6Core.lib']
    else: args+=['/DNO_GUI']
    subprocess.run(args,check=True,env=env,cwd=a.out)
    subprocess.run([str(exe)],check=True,timeout=8,env=env,cwd=a.out)
