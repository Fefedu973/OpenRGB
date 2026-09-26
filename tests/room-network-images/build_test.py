"""Build the real native SDK client test against a completed MSVC OpenRGB build.
No hardware detection is executed. No external Python packages are required.
"""
from pathlib import Path
import argparse,re,subprocess,os,shutil
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,default=Path(__file__).resolve().parents[2]/'build');p.add_argument('--link-only',action='store_true');p.add_argument('--vcvars',type=Path,default=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');a=p.parse_args()
build=a.build.resolve();repo=Path(__file__).resolve().parents[2];out=Path(__file__).resolve().parent/'.build';out.mkdir(exist_ok=True)
# Single-configuration qmake writes Makefile; debug_and_release uses Makefile.Release.
make=''
for name in ('Makefile','Makefile.Release'):
    candidate=build/name
    if candidate.is_file() and re.search(r'^OBJECTS\s*=',candidate.read_text(),re.M):
        make=candidate.read_text();break
if not make:raise RuntimeError('No qmake makefile with OBJECTS found')
def variable(key):
    logical=make.replace('\\\n',' ')
    result=re.search('^'+key+r'\s*=\s*(.*)$',logical,re.M)
    if not result: raise RuntimeError('Missing Makefile variable '+key)
    return result.group(1)
flags=variable('CXXFLAGS').replace('$(DEFINES)',variable('DEFINES'))
compile_rsp=out/'compile.rsp'
compile_rsp.write_text(flags+'\n'+variable('INCPATH')+'\n/c\n/Fo"'+str(out)+'\\\\"\n/Fd"'+str(out/'compile.pdb')+'"\n'+ '\n'.join('"'+str(x)+'"' for x in [repo/'NetworkClient.cpp',repo/'RGBController/RGBController_Network.cpp',Path(__file__).with_name('native_image_client.cpp')]))
objects=variable('OBJECTS').split()
objects=[x for x in objects if Path(x.replace('\\','/')).name not in ('main_Windows.obj','NetworkClient.obj','RGBController_Network.obj')]
# A native screen TU can have been built separately while the main qmake
# makefile is intentionally frozen during a parallel integration build.
for name in ('VirtualScreenController.obj','VirtualScreenDetect.obj','RGBController_VirtualScreen.obj'):
    candidate=str(Path('_intermediate_release')/'.obj'/name)
    if candidate not in objects and (build/candidate).is_file():objects.append(candidate)
missing=[x for x in objects if not (build/x).exists()]
if missing: raise RuntimeError('Build OpenRGB first; missing '+str(len(missing))+' objects, first: '+missing[0])
link_rsp=out/'link.rsp'
linkflags=variable('LFLAGS').replace('/SUBSYSTEM:WINDOWS','/SUBSYSTEM:CONSOLE')
link_rsp.write_text(linkflags+'\n/OUT:"'+str(out/'native_image_client.exe')+'"\n'+variable('LIBS')+'\n'+'\n'.join(objects)+'\n'+'\n'.join('"'+str(out/name)+'"' for name in ['NetworkClient.obj','RGBController_Network.obj','native_image_client.obj']))
cmd=out/'build.cmd'
compile_step='' if a.link_only else 'cl @"'+str(compile_rsp)+'"\nif errorlevel 1 exit /b 1\n'
cmd.write_text('@echo off\ncall "'+str(a.vcvars)+'" >nul\nif errorlevel 1 exit /b 1\ncd /d "'+str(build)+'"\n'+compile_step+'link @"'+str(link_rsp)+'"\nexit /b %errorlevel%\n')
subprocess.run(['cmd.exe','/d','/c',str(cmd)],check=True)
# Reuse the built application's already-provisioned runtime dependencies.
for name in ('hidapi-hotplug.dll','libusb-1.0.dll','PawnIOLib.dll'):
    for location in (build/'release'/name,build/name):
        if location.is_file():shutil.copy2(location,out/name);break
print(out/'native_image_client.exe')
