// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only preflight against real PE files. This never loads or attaches to
// Elgato; matching bytes do not establish runtime or semantic compatibility.
'use strict';
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const vm=require('node:vm');

const FILES={exe:'StreamDeck.exe',core:'Qt6Core.dll',gui:'Qt6Gui.dll'};
const sha256=bytes=>crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
const hex=value=>'0x'+value.toString(16);
function requireValue(condition,message){if(!condition)throw Error(message);}

function parsePE(bytes,name='PE') {
 const range=(offset,length)=>Number.isSafeInteger(offset)&&Number.isSafeInteger(length)&&offset>=0&&length>=0&&offset+length<=bytes.length;
 requireValue(Buffer.isBuffer(bytes)&&range(0,64)&&bytes.toString('ascii',0,2)==='MZ',name+': missing DOS header');
 const pe=bytes.readUInt32LE(0x3c);
 requireValue(range(pe,24)&&bytes.readUInt32LE(pe)===0x4550,name+': missing PE signature');
 requireValue(bytes.readUInt16LE(pe+4)===0x8664,name+': expected AMD64');
 const count=bytes.readUInt16LE(pe+6),optionalSize=bytes.readUInt16LE(pe+20),optional=pe+24;
 requireValue(count>0&&count<=96&&optionalSize>=112&&range(optional,optionalSize),name+': invalid PE header size');
 requireValue(bytes.readUInt16LE(optional)===0x20b,name+': expected PE32+');
 const imageSize=bytes.readUInt32LE(optional+56),imageBaseBig=bytes.readBigUInt64LE(optional+24);
 requireValue(imageSize>0&&imageBaseBig<=BigInt(Number.MAX_SAFE_INTEGER),name+': unsupported image range');
 const imageBase=Number(imageBaseBig),table=optional+optionalSize;
 requireValue(range(table,count*40),name+': truncated section table');
 const sections=[];
 for(let index=0;index<count;index++) {
  const offset=table+index*40;
  const section={name:bytes.toString('ascii',offset,offset+8).replace(/\0.*$/,''),
   virtualSize:bytes.readUInt32LE(offset+8),rva:bytes.readUInt32LE(offset+12),
   rawSize:bytes.readUInt32LE(offset+16),rawOffset:bytes.readUInt32LE(offset+20),flags:bytes.readUInt32LE(offset+36)};
  section.span=Math.max(section.virtualSize,section.rawSize);
  requireValue(section.rva+section.span<=imageSize&&(!section.rawSize||range(section.rawOffset,section.rawSize)),name+': invalid section '+section.name);
  sections.push(section);
 }
 function locate(rva,length) {
  requireValue(Number.isSafeInteger(rva)&&Number.isSafeInteger(length)&&rva>=0&&length>0&&rva+length<=imageSize,name+': invalid RVA range');
  const matches=sections.filter(section=>rva>=section.rva&&rva+length<=section.rva+section.span);
  requireValue(matches.length===1,name+': RVA '+hex(rva)+' has no unique section');
  const section=matches[0],delta=rva-section.rva;
  requireValue(delta+length<=section.rawSize&&range(section.rawOffset+delta,length),name+': RVA '+hex(rva)+' is not fully file-backed');
  return {section,offset:section.rawOffset+delta};
 }
 function read(rva,length){const {offset}=locate(rva,length);return bytes.subarray(offset,offset+length);}
 return {name,imageSize,imageBase,sections,locate,read};
}

function loadProfiles(source) {
 const start='// BEGIN BUILD PROFILES',end='// END BUILD PROFILES';
 const begin=source.indexOf(start),finish=source.indexOf(end);
 requireValue(begin>=0&&finish>begin&&source.indexOf(start,begin+start.length)<0&&source.indexOf(end,finish+end.length)<0,'Expected one BUILD PROFILES block');
 // Evaluate only the repository-owned data block, never the Frida script or
 // target content. A fresh context has no require, process or filesystem API.
 const block=source.slice(begin+start.length,finish);
 const json=new vm.Script(block+'\nJSON.stringify(BUILD_PROFILES)').runInNewContext(Object.create(null),{timeout:1000,contextCodeGeneration:{strings:false,wasm:false}});
 const profiles=JSON.parse(json);
 requireValue(Array.isArray(profiles)&&profiles.length>0,'No build profiles');
 const versions=new Set(),sizes=new Set();
 for(const profile of profiles) {
  requireValue(typeof profile.version==='string'&&profile.version.length>0&&!versions.has(profile.version),'Missing/duplicate profile version');
  requireValue(Number.isSafeInteger(profile.imageSize)&&profile.imageSize>0&&!sizes.has(profile.imageSize),'Missing/ambiguous profile image size');
  versions.add(profile.version);sizes.add(profile.imageSize);
  requireValue(profile.rva&&profile.signatures&&typeof profile.rva==='object'&&typeof profile.signatures==='object','Missing profile guards');
  const addresses=Object.keys(profile.rva).sort(),signatures=Object.keys(profile.signatures).sort();
  requireValue(addresses.length>0&&JSON.stringify(addresses)===JSON.stringify(signatures),'RVA/signature keys differ');
  for(const key of signatures)requireValue(Number.isSafeInteger(profile.rva[key])&&profile.rva[key]>0&&typeof profile.signatures[key]==='string'&&/^(?:[0-9a-f]{2})+$/i.test(profile.signatures[key]),'Invalid guard '+key);
  for(const key of ['meta','ownerVtable','composerVtable'])requireValue(Number.isSafeInteger(profile[key])&&profile[key]>0,'Missing data RVA '+key);
 }
 const qt=[];
 for(const match of source.matchAll(/check\((core|gui),\s*(0x[0-9a-f]+),\s*'([0-9a-f]+)'\)/gi)) {
  requireValue(match[3].length%2===0,'Invalid Qt guard');
  qt.push({module:match[1],rva:Number(match[2]),signature:match[3].toLowerCase()});
 }
 requireValue(qt.some(guard=>guard.module==='core')&&qt.some(guard=>guard.module==='gui'),'Qt signature guards missing');
 return {profiles,qt};
}

function loadNativePins(source) {
 const table=source.match(/supported_builds\s*\[\s*\]\s*=\s*\{([\s\S]*?)\};/);
 requireValue(table,'Native supported_builds table missing');
 const builds=[];
 for(const match of table[1].matchAll(/\{\s*"([^"]+)"\s*,\s*"([0-9a-f]{64})"\s*\}/gi))builds.push({version:match[1],sha256:match[2].toUpperCase()});
 requireValue(builds.length>0&&new Set(builds.map(build=>build.version)).size===builds.length&&new Set(builds.map(build=>build.sha256)).size===builds.length,'Native executable pins missing or ambiguous');
 const qt={};
 for(const match of source.matchAll(/\{\s*L"(Qt6Core\.dll|Qt6Gui\.dll)"\s*,\s*"([0-9a-f]{64})"\s*\}/gi)) {
  requireValue(!qt[match[1]],'Duplicate native Qt pin');qt[match[1]]=match[2].toUpperCase();
 }
 requireValue(qt[FILES.core]&&qt[FILES.gui],'Native Qt pins missing');
 return {builds,qt};
}

function inspectBuffers(buffers,scriptSource,guardSource) {
 const {profiles,qt}=loadProfiles(scriptSource),pins=loadNativePins(guardSource);
 requireValue(JSON.stringify(profiles.map(profile=>profile.version).sort())===JSON.stringify(pins.builds.map(build=>build.version).sort()),'Native and script profile versions differ');
 const files={},images={},errors=[],checks=[],data=[];
 for(const [module,name] of Object.entries(FILES)) {
  requireValue(Buffer.isBuffer(buffers[name]),'Missing '+name);
  images[module]=parsePE(buffers[name],name);
  files[name]={sha256:sha256(buffers[name]),bytes:buffers[name].length,imageSize:hex(images[module].imageSize)};
 }
 const native=pins.builds.find(build=>build.sha256===files[FILES.exe].sha256);
 if(!native)errors.push('StreamDeck.exe SHA256 is not pinned');
 for(const name of [FILES.core,FILES.gui]) {
  files[name].expectedSha256=pins.qt[name];files[name].hashMatches=files[name].sha256===pins.qt[name];
  if(!files[name].hashMatches)errors.push(name+' SHA256 differs');
 }
 files[FILES.exe].hashMatches=!!native;
 const profile=native?profiles.find(candidate=>candidate.version===native.version):null;
 if(profile&&profile.imageSize!==images.exe.imageSize)errors.push('Native hash and script image size do not agree');
 const signatures=profile?[...Object.keys(profile.signatures).map(name=>({name,module:'exe',rva:profile.rva[name],signature:profile.signatures[name]})),...qt]:qt;
 for(const guard of signatures) {
  const result={module:FILES[guard.module],name:guard.name||hex(guard.rva),rva:hex(guard.rva),expected:guard.signature};
  try {
   const image=images[guard.module],length=guard.signature.length/2,{section}=image.locate(guard.rva,length);
   requireValue((section.flags&0x20000000)!==0,'Signature is not in an executable section');
   result.section=section.name;result.actual=image.read(guard.rva,length).toString('hex');result.matches=result.actual===guard.signature.toLowerCase();
   if(!result.matches)errors.push(result.module+' '+result.name+': signature differs');
  }catch(error){result.matches=false;result.error=error.message;errors.push(result.module+' '+result.name+': '+error.message);}
  checks.push(result);
 }
 if(profile)for(const name of ['meta','ownerVtable','composerVtable']) {
  const result={name,rva:hex(profile[name])};
  try {
   const image=images.exe,{section}=image.locate(profile[name],8);
   requireValue((section.flags&0x40000000)!==0&&(section.flags&0x20000000)===0,'Data RVA is not in readable, non-executable data');
   result.section=section.name;result.fileBacked=true;
   if(name.endsWith('Vtable')) {
    const pointer=image.read(profile[name],8).readBigUInt64LE();
    requireValue(pointer>=BigInt(image.imageBase)&&pointer<BigInt(image.imageBase+image.imageSize),'First vtable entry is outside this image');
    const target=Number(pointer-BigInt(image.imageBase)),destination=image.locate(target,1);
    requireValue((destination.section.flags&0x20000000)!==0,'First vtable entry does not point to executable data');
    result.firstEntryRva=hex(target);
   }
   result.valid=true;
  }catch(error){result.valid=false;result.error=error.message;errors.push(name+': '+error.message);}
  data.push(result);
 }
 return {ok:errors.length===0,scope:'static-pinned-PE-preflight',version:profile?.version??null,
  sourceHashes:{'background-core.js':sha256(scriptSource),'Guard.h':sha256(guardSource)},files,
  signatureCount:checks.length,matchedSignatures:checks.filter(check=>check.matches).length,checks,data,errors,
  limitations:['No process was launched, attached to or modified.','Matching file hashes, instruction guards and data ranges does not validate full function semantics, object layout or runtime rendering.','Runtime verification remains required after adapting a profile.']};
}

function verifyPinnedBuild(directory) {
 requireValue(path.isAbsolute(directory),'Binary directory must be absolute');
 const buffers={};for(const name of Object.values(FILES))buffers[name]=fs.readFileSync(path.join(directory,name));
 return {directory:path.resolve(directory),...inspectBuffers(buffers,fs.readFileSync(path.join(__dirname,'background-core.js'),'utf8'),fs.readFileSync(path.join(__dirname,'Guard.h'),'utf8'))};
}
if(require.main===module) {
 const args=process.argv.slice(2);
 if(args.length===1&&args[0]==='--help'){console.log('Usage: node verify_pinned_build.cjs ABSOLUTE_STREAMDECK_DIRECTORY');}
 else if(args.length!==1){console.error('Usage: node verify_pinned_build.cjs ABSOLUTE_STREAMDECK_DIRECTORY');process.exitCode=2;}
 else try{const result=verifyPinnedBuild(args[0]);console.log(JSON.stringify(result,null,2));if(!result.ok)process.exitCode=1;}
 catch(error){console.log(JSON.stringify({ok:false,scope:'static-pinned-PE-preflight',error:error.message},null,2));process.exitCode=1;}
}
module.exports={parsePE,loadProfiles,loadNativePins,inspectBuffers,verifyPinnedBuild};
