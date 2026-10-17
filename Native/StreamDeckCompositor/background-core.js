'use strict';
// Exact-build, in-memory compositor bridge. All paint work stays in the native
// queued notification. No Storage calls, profile writes, or direct USB writes.
const exe=Process.getModuleByName('StreamDeck.exe'),gui=Process.getModuleByName('Qt6Gui.dll'),core=Process.getModuleByName('Qt6Core.dll');
// Frida process enumeration may omit this elevated process. Check the actual
// main module before any interceptor or native call is created instead.
const expectedPath='c:\\program files\\elgato\\streamdeck\\streamdeck.exe';
if(Process.mainModule.name.toLowerCase()!=='streamdeck.exe'||exe.path.replace(/\//g,'\\').toLowerCase()!==expectedPath)throw Error('Unexpected target main module/path');
const meta=exe.base.add(0x1578290),start=Date.now();
let target=null,armed=false,deadline=0,dirty=false,restore=false,colors=null,tiles=null;
let pending=null,sequence=0,lastQueued=0,errors=0,compositions=0,paints=0,acknowledged=0,restores=0,rejectedCandidates=0;
let stalled=false,queuedStalls=0,queueRecoveries=0,abandonedFrames=0;
let leaseRevision=0;
const bootstrap={phase:'searching',attempts:0,threadQueries:0,candidates:0,wakeups:0,detail:null};
let bootstrapCandidate=null,bootstrapAwaiting=false,bootstrapProbeRequested=false,bootstrapCancelled=false,bootstrapLastSearch=-1000;
// BEGIN FRAME PACING
class FramePacing {
 constructor(maxFps=20,now=Date.now()){this.maxFps=maxFps;this.nextAt=0;this.reset(now);}
 reset(now){this.since=now;this.received=0;this.coalesced=0;this.queueTimes=[];this.ackTimes=[];this.latencies=[];this.inputAges=[];this.lastInputAt=null;}
 configure(options,now=Date.now()){
  if(!options||typeof options!=='object'||!Number.isInteger(options.maxFps)||options.maxFps<1||options.maxFps>30)throw Error('maxFps must be an integer from 1 to 30');
  if(options.resetMetrics!==undefined&&typeof options.resetMetrics!=='boolean')throw Error('resetMetrics must be boolean');
  this.maxFps=options.maxFps;this.nextAt=now;
  if(options.resetMetrics)this.reset(now);
  return {maxFps:this.maxFps,dispatchIntervalMs:5};
 }
 input(wasDirty,now){this.received++;if(wasDirty)this.coalesced++;this.lastInputAt=now;}
 canQueue(hasTarget,hasPending,fault,now){return hasTarget&&!hasPending&&!fault&&now>=this.nextAt;}
 push(array,value){array.push(value);if(array.length>256)array.shift();}
 queued(now,isRestore){
  const period=1000/this.maxFps;
  // Retain phase within one interval; after a stall resume from now without
  // building or replaying a catch-up queue.
  this.nextAt=this.nextAt===0||now-this.nextAt>=period?now+period:this.nextAt+period;
  if(!isRestore)this.push(this.queueTimes,now);
 }
 ack(frame,now){
  if(frame.restore)return;
  this.push(this.ackTimes,now);this.push(this.latencies,Math.max(0,now-frame.at));
  if(frame.inputAt!==null)this.push(this.inputAges,Math.max(0,now-frame.inputAt));
 }
 summary(values){
  if(!values.length)return {samples:0,mean:null,p50:null,p95:null,max:null};
  const sorted=values.slice().sort((a,b)=>a-b),pick=q=>sorted[Math.max(0,Math.ceil(q*sorted.length)-1)];
  return {samples:sorted.length,mean:Math.round(100*sorted.reduce((a,b)=>a+b,0)/sorted.length)/100,p50:pick(.5),p95:pick(.95),max:sorted[sorted.length-1]};
 }
 metrics(now=Date.now()){
  const seconds=Math.max(.001,Math.min(5,(now-this.since)/1000));
  const rate=times=>Math.round(100*times.filter(t=>t>now-5000).length/seconds)/100;
  return {maxFps:this.maxFps,dispatchIntervalMs:5,windowSeconds:seconds,received:this.received,coalesced:this.coalesced,
   queuedFps5s:rate(this.queueTimes),ackFps5s:rate(this.ackTimes),ackLatencyMs:this.summary(this.latencies),inputToAckMs:this.summary(this.inputAges)};
 }
}
// END FRAME PACING
const pacing=new FramePacing();
// BEGIN FRAME LEASE
// Pure acceptance gate, also exercised through the real Frida child fixture.
// expiresAt and now use GetTickCount64 in both processes, never wall time.
class FrameLease {
 constructor(){this.lastSequence=0;}
 check(ms,expiresAt,sequence,now){
  if(!Number.isInteger(ms)||ms<500||ms>10000)throw Error('lease_ms must be500..10000');
  if(!Number.isSafeInteger(expiresAt)||expiresAt<=0||!Number.isSafeInteger(now)||now<0||expiresAt>now+ms)
   throw Error('Invalid monotonic frame deadline');
  if(!Number.isSafeInteger(sequence)||sequence<=0)throw Error('Invalid frame sequence');
  if(sequence<=this.lastSequence)return 'native-frame-stale';
  // Even an expired newer request supersedes older requests still in flight.
  this.lastSequence=sequence;
  return expiresAt<=now?'native-frame-expired':null;
 }
}
// END FRAME LEASE
const frameLease=new FrameLease();
const paintedKeys=new Set(),nativeThreads=new Set(),activeFrames=new Map(),factoryCalls=new Map();
let lastFrameCoverage=null,lastFault=null;
function emit(event,data={}){send({event,...data});}
function check(m,rva,hex){const b=Array.from(new Uint8Array(m.base.add(rva).readByteArray(hex.length/2))).map(x=>x.toString(16).padStart(2,'0')).join('');if(b!==hex)throw Error('Unsupported code '+m.name+'+'+rva.toString(16));}
function describe(p){try{const a=ptr(p),m=Process.findModuleByAddress(a);return m?m.name+'+'+a.sub(m.base):a.toString();}catch(_){return null;}}
function fail(e){armed=false;dirty=false;restore=false;errors++;const fault={message:String(e),type:e.type||null,address:e.address?describe(e.address):null,pc:e.context?describe(e.context.pc):null,stack:e.stack||null,lastFrameCoverage};if(!lastFault)lastFault=fault;emit('native-error',fault);}
if(Process.arch!=='x64'||exe.size!==0x1aab000)throw Error('Unsupported Stream Deck build');
check(exe,0x5c8550,'488bc444894018555356574154415541');
check(exe,0x419a80,'48895c24185556574154415541564157');
check(exe,0x41551b,'c744243002000000');
check(exe,0x177c80,'48895c24084889742410574883ec20');
check(exe,0x4164a0,'48895c2410488974241848897c242055');
check(exe,0x5c52b0,'48895c242055565741564157');
check(exe,0x5c544e,'0f57c00f11070f114710488d9680000000');
check(exe,0x5c8a80,'48895c242055565741564157');
check(exe,0x5c8b95,'488d5f10488bcbff15de527800');
// Image_t +0x18 is the encoded-image cache selector. These two instructions
// prove the forced action path clears it and the upload task tests it.
check(exe,0x5c8b51,'4c897dbf488d7710');
check(exe,0x60ed0a,'4d3966480f8ea0000000');
// These pinned Qt instructions are the provenance of the read-only TLS walk:
// currentThread() -> current QThreadData -> QThread*, and QObject::thread().
check(core,0x26cab0,'8b0dbaea750065488b042558000000ba68000000488b04c8488b0402c3');
check(core,0x1a54e0,'4883ec28e8c7750c004885c07505e88d740c00488b4050904883c428c3');
check(core,0x100e80,'488b4108488b4058488b405090c3');
check(core,0x25e7f0,'48895c2410488974241848897c242055');
check(gui,0x43970,'405355564883ec50');
check(gui,0x3ab50,'40534883ec204883791000');
const activate=new NativeFunction(core.getExportByName('?activate@QMetaObject@@SAXPEAVQObject@@PEBU1@HPEAPEAX@Z'),'void',['pointer','pointer','int','pointer'],'win64');
const fill=new NativeFunction(gui.getExportByName('?fill@QImage@@QEAAXI@Z'),'void',['pointer','uint'],'win64');
const bits=new NativeFunction(gui.getExportByName('?bits@QImage@@QEAAPEAEXZ'),'pointer',['pointer'],'win64');
const originalAnimate=new NativeFunction(exe.base.add(0x419a80),'void',['pointer','pointer'],'win64');
const renderGrid=new NativeFunction(exe.base.add(0x4164a0),'void',['pointer','uint'],'win64');
const objectThread=new NativeFunction(core.getExportByName('?thread@QObject@@QEBAPEAVQThread@@XZ'),'pointer',['pointer'],'win64');
const currentQtThread=new NativeFunction(core.getExportByName('?currentThread@QThread@@SAPEAV1@XZ'),'pointer',[],'win64');
const kernel=Process.getModuleByName('kernel32.dll'),ntdll=Process.getModuleByName('ntdll.dll'),user=Process.getModuleByName('user32.dll');
const getTickCount64=new NativeFunction(kernel.getExportByName('GetTickCount64'),'uint64',[],'win64');
function monotonicMs(){return getTickCount64().toNumber();}
const openThread=new NativeFunction(kernel.getExportByName('OpenThread'),'pointer',['uint','int','uint'],'win64');
const closeHandle=new NativeFunction(kernel.getExportByName('CloseHandle'),'int',['pointer'],'win64');
const queryThread=new NativeFunction(ntdll.getExportByName('NtQueryInformationThread'),'int',['pointer','uint','pointer','uint','pointer'],'win64');
const postThreadMessage=new NativeFunction(user.getExportByName('PostThreadMessageW'),'int',['uint','uint','pointer','pointer'],'win64');
function validTarget(t){return t&&t.composer.readPointer().equals(exe.base.add(0x166f090))&&t.owner.readPointer().equals(exe.base.add(0x16555e8))&&t.owner.add(0x18).readU32()===5&&t.owner.add(0x1c).readU32()===3;}
function ownerOf(bundle){const composer=bundle.add(0x28).readPointer().sub(0x10),owner=composer.sub(0x4f0),t={composer,owner};return validTarget(t)?t:null;}
function validTileImage(image){
 const d=image.add(16).readPointer();
 return !d.isNull()&&d.add(4).readS32()===72&&d.add(8).readS32()===72&&[4,5,6].includes(d.add(64).readS32())&&d.add(72).readS64().toNumber()===288;
}
function readLayout(t){
 if(!validTarget(t))throw Error('Target expired');
 const c=t.composer,b=c.add(0xe8).readPointer(),e=c.add(0xf0).readPointer();
 const vectorBytes=e.sub(b).toInt32();
 const tileWidth=c.add(0x28).readS32(),tileHeight=c.add(0x2c).readS32();
 // With no page background, native assign() legitimately leaves this vector
 // empty. Only a verified 72px composition of the pinned 5x3 device may use
 // the MK.2 geometry measured in the successful native-background session.
 if(vectorBytes===0&&t.tileVerified&&tileWidth===72&&tileHeight===72){
  return {width:480,height:272,tileWidth,tileHeight,positions:Array.from({length:15},(_,i)=>[11+(i%5)*97,5+Math.floor(i/5)*97]),columns:5,rows:3,source:'verified-mk2-fallback'};
 }
 if(vectorBytes!==120)throw Error('Unsupported native layout vector: '+vectorBytes+' bytes');
 const d=c.add(0xe0).readPointer();
 if(d.isNull())return null;
 const width=d.add(4).readS32(),height=d.add(8).readS32();
 const positions=Array.from({length:15},(_,i)=>[b.add(i*8).readS32(),b.add(i*8+4).readS32()]);
 if(tileWidth!==72||tileHeight!==72||width<72||width>4096||height<72||height>4096||positions.some(p=>p[0]<0||p[1]<0||p[0]+72>width||p[1]+72>height))throw Error('Unsupported background geometry');
 return {width,height,tileWidth,tileHeight,positions,columns:5,rows:3,source:'native-background'};
}
function layout(){return target?readLayout(target):null;}
function selectTarget(t,image){
 // Thumbnails, unloaded backgrounds and other renderers share this vtable.
 // An unselected candidate must never fault the bridge or retain its pointer.
 try{
  if(!validTileImage(image))return false;
  t.tileVerified=true;
  const candidate=readLayout(t);if(!candidate)return false;
  target=t;if(!bootstrapAwaiting){bootstrap.phase='natural';bootstrapCandidate=null;}emit(bootstrapAwaiting?'bootstrap-target-validated':'target-ready',{layout:candidate});return true;
 }catch(e){
  rejectedCandidates++;
  if(rejectedCandidates<=5)emit('target-skipped',{reason:String(e)});
  return false;
 }
}
// Existing, sleeping CKeysComposer workers are QThreads. Enumerate only this
// process's OS threads, read their TEB/TLS using the exact guarded Qt layout,
// and require the complete physical MK.2 identity. No heap search/write or
// explicit thread suspension is requested here. The OS handle is closed on
// every path (Frida controls its own enumeration internals).
function discoverBootstrap(now){
 if(bootstrapCandidate&&now-bootstrapCandidate.discoveredAt>3000){bootstrapCandidate=null;bootstrap.phase='searching';bootstrap.detail='owner-dispatch-timeout';}
 if(target||bootstrapCandidate||bootstrapCancelled||errors||now-bootstrapLastSearch<1000)return;
 if(bootstrap.attempts>=60){bootstrap.phase='unavailable';bootstrap.detail='search-limit';return;}
 bootstrapLastSearch=now;bootstrap.attempts++;
 const index=core.base.add(0x9cb570).readU32();
 if(index>4095){bootstrap.detail='invalid-tls-index';return;}
 const threads=Process.enumerateThreads();
 if(threads.length>512){bootstrap.detail='thread-limit';return;}
 const info=Memory.alloc(48),returned=Memory.alloc(4),found=[],qtThreads=[];
 for(const thread of threads){
  let handle=null;
  try{
   handle=openThread(0x40,0,thread.id);if(handle.isNull())continue;
   bootstrap.threadQueries++;
   if(queryThread(handle,0,info,48,returned)<0||returned.readU32()!==48)continue;
   const teb=info.add(8).readPointer();if(teb.isNull())continue;
   const tls=teb.add(0x58).readPointer();if(tls.isNull())continue;
   const block=tls.add(index*8).readPointer();if(block.isNull())continue;
   const data=block.add(0x68).readPointer();if(data.isNull())continue;
   const qthread=data.add(0x50).readPointer();if(qthread.isNull())continue;
   qtThreads.push({id:thread.id,qthread});
   if(!qthread.readPointer().equals(exe.base.add(0x166f090)))continue;
   const t={composer:qthread,owner:qthread.sub(0x4f0)};
   if(validTarget(t)&&validTileImage(qthread.add(0x80)))found.push(t);
  }catch(_){/* An exiting/non-Qt thread is not a candidate. */}
  finally{if(handle&&!handle.isNull())closeHandle(handle);}
 }
 if(target||bootstrapCancelled||errors)return;
 bootstrap.candidates=found.length;
 if(found.length!==1){bootstrap.detail=found.length?'ambiguous-device':'waiting-existing-composer';return;}
 // QObject affinity is read here, then checked AGAIN from its owning thread
 // before any signal emission. A mere pointer discovery never reports ready.
 const t=found[0];let affinity;
 try{affinity=objectThread(t.owner);}catch(_){bootstrap.detail='owner-changed';return;}
 if(target||bootstrapCancelled||errors)return; // affinity lookup may yield
 const owners=qtThreads.filter(x=>x.qthread.equals(affinity));
 if(owners.length!==1){bootstrap.detail='owner-thread-unavailable';return;}
 bootstrapCandidate={...t,threadId:owners[0].id,affinity,discoveredAt:now};bootstrap.phase='waiting-owner-thread';bootstrap.detail=null;
 // WM_NULL only wakes the existing message loop. It is not a key, page change,
 // button action, application command or call to a guessed internal method.
 if(postThreadMessage(owners[0].id,0,ptr(0),ptr(0)))bootstrap.wakeups++;
 else{bootstrapCandidate=null;bootstrap.phase='searching';bootstrap.detail='owner-wakeup-unavailable';}
}
Interceptor.attach(core.base.add(0x25e7f0),{onEnter(){
 const t=bootstrapCandidate;
 if(!t||target||bootstrapCancelled||errors||Process.getCurrentThreadId()!==t.threadId)return;
 bootstrapCandidate=null;
 try{
  if(!validTarget(t)||!currentQtThread().equals(t.affinity)||!objectThread(t.owner).equals(t.affinity)){
   bootstrap.phase='searching';bootstrap.detail='owner-changed';return;
  }
  if(bootstrapCancelled||target||errors)return; // exported calls may yield
  bootstrapAwaiting=true;
  if(!selectTarget(t,t.composer.add(0x80))){bootstrapAwaiting=false;bootstrap.phase='searching';bootstrap.detail='layout-not-ready';return;}
  bootstrap.phase='queued-native-probe';
  // Interceptor listeners suppress nested listeners on this thread. Calling
  // activate here would hide the Qt vector-copy hook, losing our notification
  // identity. Only validate on the owner thread; the normal timer emits it.
  bootstrapProbeRequested=true;
 }catch(e){
  // Losing a not-yet-selected candidate during device teardown is a discovery
  // miss, not a failure of a native render. Never signal through that pointer.
  if(!target){bootstrapAwaiting=false;bootstrap.phase='searching';bootstrap.detail='owner-changed';}
  else fail(e);
 }
}});
// Tag the actual Qt-owned vector, not unrelated native animation notifications.
Interceptor.attach(exe.base.add(0x177c80),{onEnter(a){
 if(pending&&a[1].equals(pending.input)){pending.copies.add(a[0].toString());}
}});
// The usual animate slot filters out keys absent from the action map. Our
// Qt-owned notification instead renders the existing full grid on THAT thread.
// 0x20010 selects device output and skips layer reassignment; no actions are
// created and the UI-preview branch is not requested. Untagged calls are intact.
Interceptor.replace(exe.base.add(0x419a80),new NativeCallback((owner,vector)=>{
 if(!target||!owner.equals(target.owner)||!pending||!pending.copies.has(vector.toString())){
  originalAnimate(owner,vector);return;
 }
 const frame=pending,tid=Process.getCurrentThreadId();
 try{
  if(errors||!validTarget(target))throw Error('Tagged target expired or faulted');
  // The queued callback may win the race against the 5ms timer after Qt wakes.
  // A renewed global lease must never make this old notification paint again.
  const enteredAt=Date.now();
  if(enteredAt-frame.at>3000)stallQueuedFrame(frame,enteredAt);
  // A queued image must not repaint after stop/lease expiry. Render the native
  // page instead, through the same complete route used by explicit restoration.
  const leaseNow=monotonicMs();
  if(frame.stalled||!armed||leaseNow>=deadline||leaseNow>=frame.leaseDeadline){frame.restore=true;restore=false;}
  // From this point the render owns an immutable choice of pixels/restoration.
  // A timer, stop RPC or lease expiry must not change half of a 15-key frame.
  frame.phase='rendering';frame.renderAt=enteredAt;
  frame.rendered=new Set();frame.injected=new Set();frame.empty=new Set();
  nativeThreads.add(tid);activeFrames.set(tid,frame);
  renderGrid(owner,0x20010);
  lastFrameCoverage={sequence:frame.sequence,restore:frame.restore,rendered:frame.rendered.size,
   injected:frame.injected.size,empty:frame.empty.size,actions:frame.rendered.size-frame.empty.size};
  if(frame.renderOverdue||Date.now()-frame.renderAt>3000)throw Error('Native grid render timeout; bridge stopped');
  if(errors||!validTarget(target)||frame.rendered.size!==15||(!frame.restore&&frame.injected.size!==15))
   throw Error('Incomplete native grid: '+JSON.stringify(lastFrameCoverage));
  acknowledged++;if(frame.restore)restores++;
  if(frame.bootstrap){bootstrapAwaiting=false;bootstrap.phase='complete';bootstrap.detail=null;emit('bootstrap-complete',{rendered:frame.rendered.size,latencyMs:Date.now()-frame.at});}
  pacing.ack(frame,Date.now());
  if(frame.stalled){
   stalled=false;queueRecoveries++;
   emit('queue-recovered',{sequence:frame.sequence,waitMs:Math.max(0,frame.renderAt-frame.at),restored:true});
  }
  if(acknowledged<=3||acknowledged%100===0)emit('frame-ack',{...lastFrameCoverage,latencyMs:Date.now()-frame.at});
 }catch(e){fail(e);}
 finally{activeFrames.delete(tid);if(pending===frame)pending=null;}
},'void',['pointer','pointer'],'win64'));
function queueFrame(isRestore,isBootstrap=false){
 const now=Date.now();
 if(!pacing.canQueue(!!target,!!pending,errors,now))return false;
 if(!validTarget(target))throw Error('Target lifetime changed');
 const entries=Memory.alloc(240),v=Memory.alloc(24),argv=Memory.alloc(16);
 for(let i=0;i<15;i++){entries.add(i*16).writeU64(i);entries.add(i*16+8).writeU64(0);}
 v.writePointer(entries);v.add(8).writePointer(entries.add(240));v.add(16).writePointer(entries.add(240));argv.writePointer(ptr(0));argv.add(8).writePointer(v);
 pending={input:v,entries,argv,copies:new Set(),sequence:++sequence,restore:isRestore,bootstrap:isBootstrap,at:now,leaseDeadline:deadline,phase:'queued',stalled:false,
  inputAt:isRestore?null:pacing.lastInputAt,colors,tiles};
 lastQueued=now;pacing.queued(now,isRestore);activate(target.composer,meta,0,argv);
 if(pending&&pending.copies.size===0)throw Error('Qt vector copy not observed; refusing further notifications');
 return true;
}
// Only our exact Qt notification uses synchronous composition. Preserve
// bundle render options and set the non-filtering immediate-render bit0.
Interceptor.attach(exe.base.add(0x5c8a80),{onEnter(a){
 if(errors||!activeFrames.has(Process.getCurrentThreadId()))return;
 try{
  const t=ownerOf(a[0]);if(!t||!target||!t.composer.equals(target.composer))return;
  const stack=factoryCalls.get(Process.getCurrentThreadId()),call=stack&&stack[stack.length-1];
  if(call&&call.composer.equals(t.composer))call.bundle=a[0];
  a[3]=ptr((a[3].toInt32()||a[0].add(0x48).readU32())|1);
 }catch(e){fail(e);}
}});
function paintTile(image,key,frame){
 if(!validTileImage(image))throw Error('Unexpected native tile image');
 const frameTiles=frame?frame.tiles:tiles,frameColors=frame?frame.colors:colors;
 if(frameTiles){
  // Public bits() detaches the copy, including the shared native blank image.
  const pixels=bits(image);
  if(pixels.isNull()||!validTileImage(image))throw Error('Unexpected detached image stride');
  pixels.writeByteArray(frameTiles[key]);
 }else if(frameColors){const c=frameColors[key];fill(image,(0xff000000|(c[0]<<16)|(c[1]<<8)|c[2])>>>0);}
 else throw Error('Missing frame pixels');
 paints++;paintedKeys.add(key);if(frame)frame.injected.add(key);
}
// Observe the native factory while it owns the composer lock. The exact blank
// branch is positive evidence of a copied empty tile: never infer emptiness from
// a missed compose callback or overwrite an already composed action image.
Interceptor.attach(exe.base.add(0x5c52b0),{onEnter(a){
 this.tid=Process.getCurrentThreadId();
 this.call={composer:a[0],out:a[1],key:a[2].toInt32(),keyPointer:a[2],blank:false};
 const stack=factoryCalls.get(this.tid)||[];stack.push(this.call);factoryCalls.set(this.tid,stack);
},onLeave(result){
 const call=this.call,stack=factoryCalls.get(this.tid);
 if(stack){stack.pop();if(!stack.length)factoryCalls.delete(this.tid);}
 try{
  if(errors||!result.equals(call.out)||call.key<0||call.key>=15||!call.keyPointer.equals(ptr(call.key)))return;
  const t={composer:call.composer,owner:call.composer.sub(0x4f0)},image=call.out.add(0x20);
  if(!validTarget(t)||(target&&!t.composer.equals(target.composer)))return;
  if(!target&&!selectTarget(t,image))return;
  const frame=activeFrames.get(this.tid);if(!frame)return;
  if(!validTileImage(image))throw Error('Invalid factory tile');
  if(call.blank){
   frame.empty.add(call.key);
   if(!frame.restore){
    paintTile(image,call.key,frame);
    // The native no-layer path retains Image_t's encoded-image cache selector.
    // UploadXIconTask copies it to +0x48 and sends the cached JPEG before reading
    // QImage. Match the forced action path: bypass that cache on THIS temporary
    // descriptor only. Never alter a bundle or the persistent encoded cache.
    call.out.add(0x18).writeU64(0);
    // The positively identified no-bundle branch zeroes the entire descriptor,
    // including its destination key. Supply the known key for this painted copy.
    if(!call.bundle)call.out.writeU64(call.key);
   }
  }
  frame.rendered.add(call.key);
 }catch(e){
  // Other compositor classes also use this factory. An unrelated natural
  // candidate must not fault our selected device; tagged render failures must.
  if(activeFrames.has(this.tid))fail(e);
 }
}});
Interceptor.attach(exe.base.add(0x5c544e),{onEnter(){
 const stack=factoryCalls.get(Process.getCurrentThreadId()),call=stack&&stack[stack.length-1];
 // RSI/RDI are the guarded function's composer/output locals, not arguments
 // guessed at a mid-function address. This hook only records the branch.
 if(call&&this.context.rsi.equals(call.composer)&&this.context.rdi.equals(call.out))call.blank=true;
}});
// A second native empty case keeps a bundle but has no renderable foreground
// layers (the state calculation returned bit0=0). This branch copies only the
// bundle's base image, bypassing the pre-overlay compositor altogether. Mark it
// positively; the factory onLeave still validates and detaches the returned copy.
Interceptor.attach(exe.base.add(0x5c8b95),{onEnter(){
 const stack=factoryCalls.get(Process.getCurrentThreadId()),call=stack&&stack[stack.length-1];
 if(call&&call.bundle&&this.context.rdi.equals(call.bundle))call.blank=true;
}});
Interceptor.attach(exe.base.add(0x5c8550),{onEnter(a){
 compositions++;
 try{
  const bundle=a[0],image=a[1],t=ownerOf(bundle);if(!t)return;
  if(target&&!t.composer.equals(target.composer))return;
  if(!target&&!selectTarget(t,image))return;
  const frame=activeFrames.get(Process.getCurrentThreadId());
  if(errors)return;
  if(frame){if(frame.restore)return;}
  else if(!armed||monotonicMs()>=deadline||stalled)return;
  const key=bundle.readU64().toNumber();if(key<0||key>=15)return;
  paintTile(image,key,frame);
 }catch(e){fail(e);}
}});
function lease(ms,expiresAt){if(!Number.isInteger(ms)||ms<500||ms>10000)throw Error('lease_ms must be500..10000');const now=Date.now();pacing.input(dirty,now);deadline=expiresAt===undefined?monotonicMs()+ms:expiresAt;armed=true;dirty=true;restore=false;}
function stop(){
 leaseRevision++;
 bootstrapCancelled=true;bootstrapCandidate=null;
 // No render/image has been sent for this candidate yet, so there is nothing
 // to restore. Do not enqueue behind a stop in the owner-validation/timer gap.
 if(bootstrapProbeRequested){bootstrapProbeRequested=false;bootstrapAwaiting=false;target=null;}
 if(!target)bootstrap.phase='cancelled';
 armed=false;dirty=false;restore=target!==null&&!errors;colors=null;tiles=null;
 // Cancellation belongs to this notification, not to the replaceable global
 // lease. A new input may arm the NEXT image, never revive the stopped one.
 if(pending&&pending.phase==='queued'){pending.restore=true;pending.colors=null;pending.tiles=null;}
 return {stopped:true,restorationPending:restore};
}
function stallQueuedFrame(frame,now){
 // Qt owns a copy of this notification. Keep its identity and backing memory;
 // never enqueue a replacement while it might still arrive. Discard the image,
 // and let that SAME notification restore the native page when Qt resumes.
 if(frame.phase!=='queued'||frame.stalled)return;
 frame.stalled=true;stalled=true;queuedStalls++;
 if(!frame.restore)abandonedFrames++;
 armed=false;dirty=false;restore=false;colors=null;tiles=null;frame.colors=null;frame.tiles=null;
 emit('queue-stalled',{sequence:frame.sequence,pendingAgeMs:Math.max(0,now-frame.at),phase:frame.phase});
}
rpc.exports={
 layout,
 status(){return {ready:!!target&&!bootstrapAwaiting&&!errors&&!stalled&&!(pending&&pending.renderOverdue),bootstrap:{...bootstrap},armed,restorationPending:restore||stalled,errors,compositions,paints,paintedKeys:[...paintedKeys],lastFrameCoverage,lastFault,queued:sequence,acknowledged,restores,rejectedCandidates,pending:pending?pending.sequence:null,pendingPhase:pending?pending.phase:null,pendingEntered:!!pending&&pending.phase==='rendering',pendingAgeMs:pending?Math.max(0,Date.now()-pending.at):null,stalled,queuedStalls,queueRecoveries,abandonedFrames,nativeThreads:[...nativeThreads],uptimeMs:Date.now()-start,pacing:pacing.metrics()};},
 configure(options){return pacing.configure(options);},
 setcolors(value,ms){if(!Array.isArray(value)||value.length!==15||value.some(c=>!Array.isArray(c)||c.length!==3||c.some(v=>!Number.isInteger(v)||v<0||v>255)))throw Error('Expected15 RGB triplets');if(errors)throw Error('Native bridge faulted');if(bootstrapAwaiting)return {accepted:false,reason:'native-bootstrap-pending'};if(stalled)return {accepted:false,reason:'native-queue-stalled'};colors=value.map(c=>c.slice());tiles=null;lease(ms);return {accepted:true};},
 setframe(ms,expiresAt,sequence,data){
  if(!(data instanceof ArrayBuffer)||data.byteLength!==311040)throw Error('Expected311040 BGRA bytes');
  const revision=leaseRevision,now=monotonicMs();
  if(revision!==leaseRevision)return {accepted:false,reason:'native-frame-cancelled'};
  if(errors)throw Error('Native bridge faulted');
  const reason=frameLease.check(ms,expiresAt,sequence,now);
  if(reason)return {accepted:false,reason};
  if(bootstrapAwaiting)return {accepted:false,reason:'native-bootstrap-pending'};
  if(stalled)return {accepted:false,reason:'native-queue-stalled'};
  const bytes=new Uint8Array(data);for(let i=3;i<bytes.length;i+=4)if(bytes[i]!==255)throw Error('Only opaque BGRA accepted');
  tiles=Array.from({length:15},(_,i)=>data.slice(i*20736,(i+1)*20736));colors=null;lease(ms,expiresAt);return {accepted:true};
 },
 stop
};
setInterval(()=>{
 if(errors)return;
 try{
  const now=Date.now();
  discoverBootstrap(now);
  if(pending&&pending.phase==='rendering'){
   // Do not mutate a render in progress. Report it, then fail after the native
   // call returns; a genuine slow/hung renderer is NOT a recovered queue stall.
   if(now-pending.renderAt>3000&&!pending.renderOverdue){pending.renderOverdue=true;emit('render-overdue',{sequence:pending.sequence,renderAgeMs:now-pending.renderAt});}
   return;
  }
  if(pending&&now-pending.at>3000)stallQueuedFrame(pending,now);
  if(stalled)return;
  if(armed&&monotonicMs()>=deadline){stop();emit('lease-expired');}
  if(bootstrapProbeRequested){
   // The owner was checked on its Qt thread. Recheck its lifetime and affinity
   // after yielding before using the same sender/queued path as normal frames.
   const t=target;let valid=false;
   try{valid=!!t&&validTarget(t)&&objectThread(t.owner).equals(t.affinity);}catch(_){}
   if(!bootstrapProbeRequested||bootstrapCancelled||target!==t)return;
   if(!valid){bootstrapProbeRequested=false;bootstrapAwaiting=false;target=null;bootstrap.phase='searching';bootstrap.detail='owner-changed';return;}
   // Publish this state before activate: its cooperative native call may yield
   // to stop/ACK. Once pending exists, cancellation must retain its identity.
   bootstrapProbeRequested=false;
   if(!queueFrame(true,true)&&!bootstrapCancelled&&bootstrapAwaiting&&target===t)bootstrapProbeRequested=true;
   return;
  }
  if(restore){if(queueFrame(true))restore=false;}
  else if(dirty&&armed){if(queueFrame(false))dirty=false;}
 }catch(e){fail(e);}
},5);
emit('ready',{stage:'background-api-core-fps-candidate',pid:Process.id,maxFps:pacing.maxFps});
