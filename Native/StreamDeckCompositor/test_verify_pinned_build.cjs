// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {createHash}=require('node:crypto');
const {parsePE,loadProfiles,inspectBuffers}=require('./verify_pinned_build.cjs');
const hash=value=>createHash('sha256').update(value).digest('hex').toUpperCase();
function pe() {
 const bytes=Buffer.alloc(0x600),header=0x80,optional=header+24,table=optional+0xf0;
 bytes.write('MZ');bytes.writeUInt32LE(header,0x3c);bytes.writeUInt32LE(0x4550,header);
 bytes.writeUInt16LE(0x8664,header+4);bytes.writeUInt16LE(2,header+6);bytes.writeUInt16LE(0xf0,header+20);
 bytes.writeUInt16LE(0x20b,optional);bytes.writeBigUInt64LE(0x140000000n,optional+24);
 bytes.writeUInt32LE(0x3000,optional+56);bytes.writeUInt32LE(0x200,optional+60);
 for(const [i,name,rva,raw,flags] of [[0,'.text',0x1000,0x200,0x60000020],[1,'.rdata',0x2000,0x400,0x40000040]]) {
  const section=table+i*40;bytes.write(name,section);bytes.writeUInt32LE(0x200,section+8);
  bytes.writeUInt32LE(rva,section+12);bytes.writeUInt32LE(0x200,section+16);bytes.writeUInt32LE(raw,section+20);bytes.writeUInt32LE(flags,section+36);
 }
 bytes[0x300]=0x40;bytes[0x301]=0x53;
 bytes.writeBigUInt64LE(0x140001100n,0x400);bytes.writeBigUInt64LE(0x140001110n,0x408);
 return bytes;
}
function fixture() {
 const buffers={'StreamDeck.exe':pe(),'Qt6Core.dll':pe(),'Qt6Gui.dll':pe()};
 const source=`// BEGIN BUILD PROFILES
 const BUILD_PROFILES=[{version:'test.1',imageSize:0x3000,meta:0x2010,ownerVtable:0x2000,composerVtable:0x2008,rva:{entry:0x1100},signatures:{entry:'4053'}}];
 // END BUILD PROFILES
 check(core,0x1100,'4053');check(gui,0x1100,'4053');`;
 const pins=()=>`inline constexpr SupportedBuild supported_builds[]={ {"test.1","${hash(buffers['StreamDeck.exe'])}"} }; const pairs files[]={ {L"Qt6Core.dll","${hash(buffers['Qt6Core.dll'])}"},{L"Qt6Gui.dll","${hash(buffers['Qt6Gui.dll'])}"} };`;
 return {buffers,source,pins};
}
test('real file-offset mapping is used, and valid guards/data are reported separately',()=>{
 const f=fixture(),result=inspectBuffers(f.buffers,f.source,f.pins());
 assert.equal(result.ok,true);assert.equal(result.signatureCount,3);assert.equal(result.matchedSignatures,3);
 assert.equal(result.data.length,3);assert(result.data.every(item=>item.valid));
 assert.equal(result.data.find(item=>item.name==='ownerVtable').firstEntryRva,'0x1100');
 assert(result.limitations.some(text=>text.includes('runtime rendering')));
});
test('same image size with an unpinned executable never identifies a supported version',()=>{
 const f=fixture(),pins=f.pins();f.buffers['StreamDeck.exe'][0x250]^=1;
 const result=inspectBuffers(f.buffers,f.source,pins);
 assert.equal(result.ok,false);assert.equal(result.version,null);assert.equal(result.files['StreamDeck.exe'].hashMatches,false);
 assert.equal(result.checks.some(check=>check.module==='StreamDeck.exe'),false);
});
test('a pinned executable with a stale instruction guard fails',()=>{
 const f=fixture();f.buffers['StreamDeck.exe'][0x301]^=1;
 const result=inspectBuffers(f.buffers,f.source,f.pins());
 assert.equal(result.ok,false);assert.equal(result.files['StreamDeck.exe'].hashMatches,true);
 assert.equal(result.checks.find(check=>check.module==='StreamDeck.exe').actual,'4052');
});
test('a Qt hash mismatch fails even when its checked instruction is unchanged',()=>{
 const f=fixture(),pins=f.pins();f.buffers['Qt6Core.dll'][0x250]^=1;
 const result=inspectBuffers(f.buffers,f.source,pins);
 assert.equal(result.ok,false);assert.equal(result.matchedSignatures,3);assert.equal(result.files['Qt6Core.dll'].hashMatches,false);
});
test('non AMD64 and truncated sections are rejected before inspection',()=>{
 const bytes=pe();bytes.writeUInt16LE(0x14c,0x84);assert.throws(()=>parsePE(bytes),/expected AMD64/);
 assert.throws(()=>parsePE(pe().subarray(0,0x500)),/invalid section/);
});
test('virtual zero-fill is not treated as bytes present on disk',()=>{
 const bytes=pe();bytes.writeUInt32LE(0x40,0x188+40+16);
 const image=parsePE(bytes);assert.throws(()=>image.read(0x20a0,8),/not fully file-backed/);
});
test('overlapping section ranges cannot resolve a guard ambiguously',()=>{
 const bytes=pe();bytes.writeUInt32LE(0x1000,0x188+40+12);
 assert.throws(()=>parsePE(bytes).read(0x1100,2),/no unique section/);
});
test('metadata cannot silently point to a code section',()=>{
 const f=fixture(),source=f.source.replace('meta:0x2010','meta:0x1100');
 const result=inspectBuffers(f.buffers,source,f.pins());assert.equal(result.ok,false);
 assert.match(result.data.find(item=>item.name==='meta').error,/non-executable data/);
});
test('native and script build sets must agree',()=>{
 const f=fixture();assert.throws(()=>inspectBuffers(f.buffers,f.source,f.pins().replace('test.1','test.2')),/versions differ/);
});
test('unpaired RVA/signature keys and duplicate profile blocks fail closed',()=>{
 const f=fixture();assert.throws(()=>loadProfiles(f.source.replace("signatures:{entry:","signatures:{other:")),/keys differ/);
 assert.throws(()=>loadProfiles(f.source+'\n// BEGIN BUILD PROFILES'),/one BUILD PROFILES/);
});
