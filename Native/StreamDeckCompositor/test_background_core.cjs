// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the production Frida script against an isolated native/Qt memory model.
// No Frida module, process attachment, device, network or GUI is used here.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync(require('node:path').join(__dirname,'background-core.js'),'utf8');

function fixture(options={}) {
 const memory=[],hooks=new Map(),replacements=new Map(),functions=new Map(),events=[],queue=[];
 let address=0x200000000,now=1000,timer,originalCalls=0,gridCalls=0,output=[],descriptors=[],uploaded=[];
 class Pointer {
  constructor(n){this.n=n instanceof Pointer?n.n:Number(n);}
  add(n){return ptr(this.n+Number(n));}sub(n){return ptr(this.n-Number(n instanceof Pointer?n.n:n));}
  equals(n){return this.n===ptr(n).n;}isNull(){return this.n===0;}toInt32(){return this.n|0;}toString(){return '0x'+this.n.toString(16);}
  block(n){const b=memory.find(b=>this.n>=b.at&&this.n+n<=b.at+b.data.length);if(!b)throw Error('Invalid mock memory '+this);return [b.data,this.n-b.at];}
  readByteArray(n){const [b,o]=this.block(n);return b.slice(o,o+n);}
  writeByteArray(v){const bytes=Buffer.from(v);const [b,o]=this.block(bytes.length);bytes.copy(b,o);}
  readPointer(){return ptr(Number(this.readU64().value));}writePointer(v){this.writeU64(ptr(v).n);}
  readU64(){const [b,o]=this.block(8);const value=b.readBigUInt64LE(o);return {value,toNumber:()=>Number(value)};}
  readS64(){const [b,o]=this.block(8);const value=b.readBigInt64LE(o);return {toNumber:()=>Number(value)};}
  writeU64(v){const [b,o]=this.block(8);b.writeBigUInt64LE(BigInt(v),o);}
  readU32(){const [b,o]=this.block(4);return b.readUInt32LE(o);}readS32(){const [b,o]=this.block(4);return b.readInt32LE(o);}
  writeU32(v){const [b,o]=this.block(4);b.writeUInt32LE(v>>>0,o);}
 }
 function ptr(n){return new Pointer(n);}
 function alloc(size){const p=ptr(address);address+=size+64;memory.push({at:p.n,data:Buffer.alloc(size)});return p;}
 const exe={name:'StreamDeck.exe',path:'C:\\Program Files\\Elgato\\StreamDeck\\StreamDeck.exe',base:ptr(0x100000000),size:0x1aab000};
 const gui={name:'Qt6Gui.dll',base:ptr(0x110000000)},core={name:'Qt6Core.dll',base:ptr(0x120000000)};
 const modules={exe,gui,core},exports=new Map();
 for(const m of Object.values(modules))m.getExportByName=name=>{
  if(!exports.has(name))exports.set(name,alloc(8));return exports.get(name);
 };
 for(const match of source.matchAll(/check\((exe|gui|core),(0x[0-9a-f]+),'([0-9a-f]+)'\)/g)) {
  const bytes=Buffer.from(match[3],'hex');
  if(options.badGuard===Number(match[2]))bytes[0]^=1;
  memory.push({at:modules[match[1]].base.n+Number(match[2]),data:bytes});
 }
 function invoke(p,args=[],context={},bypass=false){
  const key=ptr(p).toString();if(!bypass&&replacements.has(key))return replacements.get(key)(...args);
  const calls=(hooks.get(key)||[]).map(h=>({h,self:{context}}));
  for(const c of calls)c.h.onEnter?.call(c.self,args);
  const result=functions.get(key)?.(...args)||ptr(0);
  for(const c of calls.reverse())c.h.onLeave?.call(c.self,result);
  return result;
 }
 function point(rva,context){invoke(exe.base.add(rva),[],context);}
 function imageData(value=17){
  const d=alloc(96),pixels=alloc(20736);d.add(4).writeU32(72);d.add(8).writeU32(72);d.add(64).writeU32(6);d.add(72).writeU64(288);
  const b=Buffer.alloc(20736,value);for(let i=3;i<b.length;i+=4)b[i]=255;pixels.writeByteArray(b);return {d,pixels};
 }
 // The model keeps the pixel pointer separately: real QImageData's first bytes
 // hold a refcount, not this test convenience field.
 const pixelStorage=new Map();
 function data(value=17){const {d,pixels}=imageData(value);pixelStorage.set(d.n,pixels);return d;}
 function pixels(image){return pixelStorage.get(image.add(16).readPointer().n);}
 function detach(image){const old=image.add(16).readPointer(),d=data();pixelStorage.get(d.n).writeByteArray(pixelStorage.get(old.n).readByteArray(20736));image.add(16).writePointer(d);return pixelStorage.get(d.n);}
 const owner=alloc(0x800),composer=owner.add(0x4f0),blank=data(),bundles=Array.from({length:15},(_,key)=>{
  const b=alloc(0x60);b.writeU64(key);b.add(0x28).writePointer(composer.add(0x10));b.add(0x48).writeU32(4);return b;
 });
 owner.writePointer(exe.base.add(0x16555e8));owner.add(0x18).writeU32(5);owner.add(0x1c).writeU32(3);
 composer.writePointer(exe.base.add(0x166f090));composer.add(0x28).writeU32(72);composer.add(0x2c).writeU32(72);
 composer.add(0x80+16).writePointer(blank); // native blank is shared across factory copies
 let actions=new Set(options.actions??[0,2,4,6,8,10,12,14]);
 const api={};
 functions.set(core.getExportByName('?activate@QMetaObject@@SAXPEAVQObject@@PEBU1@HPEAPEAX@Z').toString(),(c,m,index,argv)=>{
  assert(c.equals(composer));const input=argv.add(8).readPointer(),copy=alloc(24);
  invoke(exe.base.add(0x177c80),[copy,input]);queue.push(()=>invoke(exe.base.add(0x419a80),[owner,copy]));
 });
 functions.set(gui.getExportByName('?bits@QImage@@QEAAPEAEXZ').toString(),image=>detach(image));
 functions.set(gui.getExportByName('?fill@QImage@@QEAAXI@Z').toString(),(image,rgb)=>{
  const p=detach(image),bytes=Buffer.alloc(20736);for(let i=0;i<bytes.length;i+=4)bytes.writeUInt32LE(rgb>>>0,i);p.writeByteArray(bytes);
 });
 functions.set(exe.base.add(0x419a80).toString(),()=>{originalCalls++;});
 functions.set(exe.base.add(0x5c8a80).toString(),(bundle,out,unused,flags)=>{
  assert(flags.toInt32()&1,'tagged action rendering must be synchronous');
  const image=out.add(0x20);image.add(16).writePointer(blank);
  out.writeU64(bundle.readU64().toNumber());out.add(0x18).writeU64(123456);
  if(options.inactiveBundles?.includes(bundle.readU64().toNumber())){
   point(0x5c8b95,{rdi:bundle});return out;
  }
  // Native forced foreground rendering clears Image_t's encoded cache selector.
  out.add(0x18).writeU64(0);
  if(options.missedAction!==bundle.readU64().toNumber())invoke(exe.base.add(0x5c8550),[bundle,image]);
  // Native icon/title overlay comes AFTER our pre-overlay background hook.
  const p=detach(image);p.writeByteArray(Buffer.from([251,252,253,255]));return out;
 });
 functions.set(exe.base.add(0x5c52b0).toString(),(c,out,keyPointer)=>{
  const key=keyPointer.toInt32();
  if(actions.has(key))invoke(exe.base.add(0x5c8a80),[bundles[key],out,ptr(0),ptr(0)]);
  else{point(0x5c544e,{rsi:c,rdi:out});out.add(0x20+16).writePointer(blank);}
  if(options.badImage===key)out.add(0x20+16).writePointer(ptr(0));
  if(options.badStride===key){const image=out.add(0x20);detach(image);image.add(16).readPointer().add(72).writeU64(300);}
  return out;
 });
 functions.set(exe.base.add(0x4164a0).toString(),(o,flags)=>{
  assert(o.equals(owner));assert.equal(flags,0x20010);gridCalls++;output=[];descriptors=[];uploaded=[];
  for(let key=0;key<15;key++){
   if(options.skip===key)continue;
   options.duringRender?.({key,rpc:sandbox.rpc.exports,tick(ms){now+=ms;timer();}});
   const out=alloc(128);invoke(exe.base.add(0x5c52b0),[composer,out,ptr(key),ptr(0)]);
   const image=out.add(0x20),cache=out.add(0x18).readU64().toNumber(),destination=out.readU64().toNumber();
   // Model UploadXIconTask, not just the modified QImage: a positive selector
   // uses a pre-existing encoded blank and skips the painted pixels entirely.
   const rendered=image.add(16).readPointer().isNull()?null:Buffer.from(pixels(image).readByteArray(20736));
   output[key]=cache>0?Buffer.from(pixelStorage.get(blank.n).readByteArray(20736)):rendered;
   descriptors[key]={cache,destination};uploaded[destination]=output[key];
  }
 });
 const sandbox={Process:{mainModule:exe,arch:'x64',id:1,getModuleByName:n=>Object.values(modules).find(m=>m.name===n),getCurrentThreadId:()=>77},
  Memory:{alloc},ptr,NativeFunction:function(p){return (...args)=>invoke(p,args,{},true);},NativeCallback:function(f){return f;},
  Interceptor:{attach(p,h){const key=p.toString();hooks.set(key,[...(hooks.get(key)||[]),h]);},replace(p,f){replacements.set(p.toString(),f);}},
  rpc:{exports:api},send:e=>events.push(e),setInterval:f=>{timer=f;},Date:{now:()=>now},ArrayBuffer,Uint8Array};
 vm.runInNewContext(source,sandbox,{filename:'background-core.js'});
 function naturalEmpty(){const old=actions;actions=new Set();const out=alloc(128);invoke(exe.base.add(0x5c52b0),[composer,out,ptr(0),ptr(0)]);actions=old;}
 return {rpc:sandbox.rpc.exports,events,naturalEmpty,queue,
  tick(ms=60){now+=ms;timer();},advance(ms){now+=ms;},drain(){assert(queue.length);queue.shift()();},
  colors(rgb=[100,50,25]){return sandbox.rpc.exports.setcolors(Array.from({length:15},()=>rgb),2000);},
  untagged(){invoke(exe.base.add(0x419a80),[owner,alloc(24)]);},
  setActions(keys){actions=new Set(keys);},expireTarget(){owner.writePointer(ptr(0));},
  get output(){return output;},get descriptors(){return descriptors;},get uploaded(){return uploaded;},get originalCalls(){return originalCalls;},get gridCalls(){return gridCalls;},
  get blank(){return Buffer.from(pixelStorage.get(blank.n).readByteArray(20736));}
 };
}

test('mixed grid paints 15 current cells, leaves icons and native shared blank intact',()=>{
 const f=fixture();f.naturalEmpty();const blank=Buffer.from(f.blank);f.colors();f.tick();
 assert.equal(f.rpc.status().acknowledged,0,'queue acceptance is not render completion');f.drain();
 const s=f.rpc.status();assert.equal(s.errors,0);assert.equal(s.acknowledged,1);
 assert.deepEqual({...s.lastFrameCoverage},{sequence:1,restore:false,rendered:15,injected:15,empty:7,actions:8});
 for(let i=0;i<15;i++){
  assert.deepEqual([...f.output[i].subarray(4,8)],[25,50,100,255]);
  assert.deepEqual([...f.output[i].subarray(0,4)],i%2===0?[251,252,253,255]:[25,50,100,255]);
 }
 assert.deepEqual(f.blank,blank);assert.equal(f.originalCalls,0);assert.equal(f.gridCalls,1);
});
test('empty page is naturally discoverable and full image uses each distinct tile',()=>{
 const f=fixture({actions:[]});assert.equal(f.rpc.status().ready,false);f.naturalEmpty();assert.equal(f.rpc.status().ready,true);
 const bytes=new Uint8Array(311040);for(let i=0;i<bytes.length;i+=4){bytes[i]=Math.floor(i/20736);bytes[i+3]=255;}
 f.rpc.setframe(2000,bytes.buffer);f.tick();f.drain();assert.equal(f.rpc.status().lastFrameCoverage.empty,15);
 for(let i=0;i<15;i++){
  assert.deepEqual([...f.uploaded[i].subarray(0,4)],[i,0,0,255]);
  assert.deepEqual(f.descriptors[i],{cache:0,destination:i});
 }
});
test('page changes re-evaluate empty/action coverage, native restore renders all 15',()=>{
 const f=fixture();f.naturalEmpty();f.colors();f.tick();f.drain();f.setActions([1,3]);f.colors();f.tick();f.drain();
 assert.equal(f.rpc.status().lastFrameCoverage.empty,13);
 f.rpc.stop();f.tick();f.drain();const s=f.rpc.status();assert.equal(s.restores,1);assert.equal(s.lastFrameCoverage.rendered,15);assert.equal(s.lastFrameCoverage.injected,0);
 for(let i=0;i<15;i++)assert.deepEqual([...f.output[i].subarray(0,4)],[1,3].includes(i)?[251,252,253,255]:[17,17,17,255]);
});
test('untagged native calls retain original handler',()=>{
 const f=fixture();f.naturalEmpty();f.colors();f.untagged();assert.equal(f.originalCalls,1);assert.equal(f.gridCalls,0);assert.equal(f.rpc.status().acknowledged,0);
});
test('empty cells retaining inactive bundles use the positive no-layer branch',()=>{
 const f=fixture({actions:Array.from({length:15},(_,i)=>i),inactiveBundles:[8,9,10,11,12,13,14]});
 f.naturalEmpty();f.colors();f.tick();f.drain();const s=f.rpc.status();
 assert.equal(s.errors,0);assert.equal(s.acknowledged,1);assert.equal(s.lastFrameCoverage.injected,15);assert.equal(s.lastFrameCoverage.empty,7);
 for(let i=8;i<15;i++){
  assert.deepEqual([...f.output[i].subarray(0,4)],[25,50,100,255]);
  assert.deepEqual(f.descriptors[i],{cache:0,destination:i},'painted temporary descriptor bypasses encoded cache');
 }
 f.rpc.stop();f.tick();f.drain();assert.equal(f.rpc.status().restores,1);
 for(let i=8;i<15;i++){
  assert.deepEqual(f.output[i],f.blank);
  assert.deepEqual(f.descriptors[i],{cache:123456,destination:i},'restore retains native encoded-cache identity');
 }
});
for(const [label,options] of [['missing cell',{skip:14}],['missing action callback',{missedAction:0}],['null image',{badImage:1}],['invalid stride',{badStride:1}]])
 test(label+' cannot acknowledge a complete frame',()=>{
  const f=fixture(options);f.naturalEmpty();f.colors();f.tick();f.drain();assert(f.rpc.status().errors>0);assert.equal(f.rpc.status().acknowledged,0);
  if(options.missedAction===0)assert.deepEqual([...f.output[0].subarray(0,4)],[251,252,253,255],'no post-overlay fallback');
 });
test('stale target fails before any full-grid native call',()=>{
 const f=fixture();f.naturalEmpty();f.colors();f.tick();f.expireTarget();f.drain();assert.equal(f.gridCalls,0);assert.equal(f.rpc.status().acknowledged,0);assert(f.rpc.status().errors>0);
});
test('stop while queued and lease expiry render only native restore',()=>{
 for(const expiry of [false,true]){const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();if(expiry)f.tick(2001);else f.rpc.stop();f.drain();
  assert.equal(f.rpc.status().paints,0);assert.equal(f.rpc.status().restores,1);assert.deepEqual(f.output[0],f.blank);}
});
test('exact new native entry/branch guards reject before any interceptor',()=>{
 for(const rva of [0x4164a0,0x5c52b0,0x5c544e,0x5c8b95,0x5c8b51,0x60ed0a])assert.throws(()=>fixture({badGuard:rva}),/Unsupported code/);
});

test('queued delay is nonfatal and keeps exactly one Qt notification until native restoration',()=>{
 const f=fixture();f.naturalEmpty();f.colors();f.tick();const queued=f.queue[0];
 f.tick(3001);let s=f.rpc.status();
 assert.equal(s.errors,0);assert.equal(s.ready,false);assert.equal(s.stalled,true);assert.equal(s.armed,false);
 assert.equal(s.pending,1);assert.equal(s.pendingPhase,'queued');assert.equal(s.pendingEntered,false);assert.equal(s.pendingAgeMs,3001);
 assert.equal(s.queuedStalls,1);assert.equal(s.abandonedFrames,1);assert.equal(s.restorationPending,true);
 assert.equal(f.colors().accepted,false);
 const bytes=new Uint8Array(311040);for(let i=3;i<bytes.length;i+=4)bytes[i]=255;
 assert.equal(f.rpc.setframe(2000,bytes.buffer).accepted,false);
 for(let i=0;i<5;i++)f.tick(5000);
 assert.equal(f.queue.length,1);assert.equal(f.queue[0],queued);assert.equal(f.rpc.status().queuedStalls,1);
 // A page may have changed while Qt was busy. Restore its CURRENT icons/page.
 f.setActions([1,3]);f.drain();s=f.rpc.status();
 assert.equal(s.errors,0);assert.equal(s.ready,true);assert.equal(s.stalled,false);assert.equal(s.armed,false);
 assert.equal(s.paints,0);assert.equal(s.restores,1);assert.equal(s.queueRecoveries,1);assert.equal(s.pending,null);
 assert.equal(s.pendingPhase,null);assert.equal(s.pendingAgeMs,null);assert.equal(s.pendingEntered,false);
 assert.equal(s.lastFrameCoverage.actions,2);assert.equal(s.lastFrameCoverage.injected,0);
 assert.equal(f.events.filter(e=>e.event==='queue-recovered').length,1);
 assert.equal(f.events.find(e=>e.event==='queue-recovered').sequence,1);
 f.tick(5000);assert.equal(f.queue.length,0,'discarded input must never resume by itself');
 assert.equal(f.colors([10,20,30]).accepted,true);f.tick();f.drain();
 assert.equal(f.rpc.status().lastFrameCoverage.restore,false);
 assert.deepEqual([...f.output[0].subarray(0,4)],[30,20,10,255]);
});

test('explicit stop during a queued stall does not enqueue a second restoration',()=>{
 const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();f.tick(3001);
 assert.equal(f.rpc.stop().restorationPending,true);f.tick(5000);assert.equal(f.queue.length,1);
 f.drain();f.tick();const s=f.rpc.status();
 assert.equal(s.errors,0);assert.equal(s.restores,1);assert.equal(s.queueRecoveries,1);assert.equal(s.restorationPending,false);
 assert.equal(f.queue.length,0);assert.deepEqual(f.output[0],f.blank);
});

test('late callback detects a stalled notification before the timer despite a renewed image lease',()=>{
 const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();
 f.advance(2900);assert.equal(f.colors([9,8,7]).accepted,true);f.advance(101);
 assert.equal(f.rpc.status().queuedStalls,0,'timer has not run');f.drain();const s=f.rpc.status();
 assert.equal(s.errors,0);assert.equal(s.queuedStalls,1);assert.equal(s.queueRecoveries,1);
 assert.equal(s.lastFrameCoverage.restore,true);assert.equal(s.paints,0);assert.equal(s.armed,false);
 f.tick();assert.equal(f.queue.length,0);assert.deepEqual(f.output[0],f.blank);
 f.colors([1,2,3]);f.tick();f.drain();assert.deepEqual([...f.output[0].subarray(0,4)],[3,2,1,255]);
});

test('a stalled restoration is retained and recovered without counting an abandoned image',()=>{
 const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();f.drain();
 f.rpc.stop();f.tick();f.tick(3001);assert.equal(f.rpc.status().abandonedFrames,0);
 f.drain();const s=f.rpc.status();assert.equal(s.errors,0);assert.equal(s.queuedStalls,1);assert.equal(s.queueRecoveries,1);
 assert.equal(s.restores,1);assert.equal(s.lastFrameCoverage.injected,0);
});

test('lease expiry or stop during rendering never changes half of the current frame',()=>{
 for(const stop of [false,true]){
  let entered=false;
  const f=fixture({duringRender({key,rpc,tick}){
   if(key!==7||entered)return;entered=true;
   assert.equal(rpc.status().pendingPhase,'rendering');assert.equal(rpc.status().pendingEntered,true);
   if(stop)rpc.stop();else tick(2001);
  }});
  f.naturalEmpty();f.colors();f.tick();f.drain();let s=f.rpc.status();
  assert.equal(s.errors,0);assert.equal(s.queuedStalls,0);assert.equal(s.lastFrameCoverage.injected,15);
  assert.equal(s.lastFrameCoverage.restore,false);assert.equal(s.acknowledged,1);
  f.tick();f.drain();s=f.rpc.status();assert.equal(s.restores,1);assert.equal(s.lastFrameCoverage.injected,0);
 }
});

test('a real render lasting over three seconds remains a fault after completing the native call',()=>{
 let advanced=false;
 const f=fixture({duringRender({key,rpc,tick}){
  if(key!==7||advanced)return;advanced=true;tick(3001);
  assert.equal(rpc.status().pendingPhase,'rendering');assert.equal(rpc.status().stalled,false);
  assert.equal(rpc.status().errors,0,'do not alter an in-flight render');
 }});
 f.naturalEmpty();f.colors();f.tick();f.drain();const s=f.rpc.status();
 assert.equal(s.queuedStalls,0);assert.equal(s.queueRecoveries,0);assert.equal(s.lastFrameCoverage.injected,15);
 assert.equal(s.acknowledged,0);assert.equal(s.ready,false);assert(s.errors>0);
 assert.match(s.lastFault.message,/Native grid render timeout/);
 assert.equal(f.events.filter(e=>e.event==='render-overdue').length,1);
});

test('a target that expires during a queued stall still fails closed',()=>{
 const f=fixture();f.naturalEmpty();f.colors();f.tick();f.tick(3001);f.expireTarget();f.drain();
 const s=f.rpc.status();assert(s.errors>0);assert.equal(s.queueRecoveries,0);assert.equal(s.acknowledged,0);assert.equal(f.gridCalls,0);
});

test('stop then fresh input cannot revive the queued old image',()=>{
 const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();
 f.rpc.stop();assert.equal(f.colors([10,20,30]).accepted,true);f.drain();
 let s=f.rpc.status();assert.equal(s.errors,0);assert.equal(s.paints,0);assert.equal(s.restores,1);
 assert.deepEqual(f.output[0],f.blank);assert.equal(f.queue.length,0);
 f.tick();f.drain();s=f.rpc.status();assert.equal(s.lastFrameCoverage.restore,false);assert.equal(s.paints,15);
 assert.deepEqual([...f.output[0].subarray(0,4)],[30,20,10,255]);
});

test('expired image lease cannot be renewed retroactively while queued',()=>{
 for(const timerRan of [false,true]){
  const f=fixture({actions:[]});f.naturalEmpty();f.colors();f.tick();
  if(timerRan)f.tick(2001);else f.advance(2001);
  assert.equal(f.colors([11,22,33]).accepted,true);f.drain();let s=f.rpc.status();
  assert.equal(s.errors,0);assert.equal(s.paints,0);assert.equal(s.restores,1);assert.equal(s.queuedStalls,0);
  assert.deepEqual(f.output[0],f.blank);
  f.tick();f.drain();s=f.rpc.status();assert.equal(s.lastFrameCoverage.restore,false);assert.equal(s.paints,15);
  assert.deepEqual([...f.output[0].subarray(0,4)],[33,22,11,255]);
 }
});
