// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {buildExport,validateTransform}=require('./export-canvas.cjs');

const transform={x:10,y:20,rotation:0,scale:{x:2,y:3},brightness:70};
function fixture() {
  return {schemaVersion:1,canvas:{width:320,height:200},devices:[
    {id:'segment-b',name:'Second segment',parentId:'parent',channel:'channel-1',order:4,enabled:true,active:true,
      baseSize:[4,2],leds:[{x:3,y:1,name:'B'},{x:0,y:0,name:'A'}]},
    {id:'parent',name:'Controller only',enabled:true,active:false,excludedReason:'root controller',baseSize:[1,1],leds:[]},
    {id:'disabled',enabled:false,active:false,baseSize:[1,1],leds:[{x:0,y:0}]}
  ],layouts:[{name:'Full Scale',brightness:90,entries:{'segment-b':transform,parent:transform,disabled:transform,unknown:transform}}]};
}

test('uses supplied LayoutCore, preserves source order, parent and component identity',()=>{
  const calls=[];
  const core={worldPoint:(d,t,p)=>{calls.push({id:d.id,t,p});return{x:p.x+100,y:p.y+200};}};
  const {exported,mapping}=buildExport(fixture(),core);
  const points=exported.layouts[0].points;
  assert.deepEqual(points.map(p=>[p.deviceId,p.ledIndex,p.name,p.local]),[
    ['segment-b',0,'B',{x:3,y:1}],['segment-b',1,'A',{x:0,y:0}]
  ]);
  assert.equal(calls.length,2);
  assert.equal(points[0].componentOrder,4);
  assert.equal(points[0].parentId,'parent');
  assert.equal(points[0].channel,'channel-1');
  assert.equal(mapping.devices[0].openRgb.controller.serial,null);
  assert.equal(mapping.devices[0].openRgb.zoneIndex,null);
  assert.deepEqual(mapping.devices[0].openRgb.ledMap,[{sourceLedIndex:0,targetLedIndex:null},{sourceLedIndex:1,targetLedIndex:null}]);
});

test('records exclusions and unknown geometry without invented points',()=>{
  const {exported}=buildExport(fixture(),{worldPoint:()=>({x:0,y:0})});
  const layout=exported.layouts[0];
  assert.equal(layout.points.length,2);
  assert.equal(layout.missingGeometry[0].deviceId,'unknown');
  assert(layout.exclusions.find(x=>x.deviceId==='parent').reasons.includes('root controller'));
  assert(layout.exclusions.find(x=>x.deviceId==='disabled').reasons.includes('disabled'));
  assert.equal(layout.brightness,90);
  assert.equal(layout.transforms['segment-b'].brightness,70);
});

test('missing placement never inherits Full Scale; out-of-canvas points remain flagged',()=>{
  const project=fixture();project.layouts.push({name:'music',entries:{}});
  const {exported}=buildExport(project,{worldPoint:()=>({x:-1,y:201})});
  assert.equal(exported.layouts[0].points.length,2);
  assert.equal(exported.layouts[0].statistics.outsideCanvasPoints,2);
  assert.equal(exported.layouts[1].points.length,0);
  assert(exported.layouts[1].exclusions[0].reasons.includes('placement-missing-in-layout'));
});

test('invalid or ambiguous inputs fail before export',()=>{
  const core={worldPoint:()=>({x:0,y:0})};
  const project=fixture();project.devices.push(project.devices[0]);
  assert.throws(()=>buildExport(project,core),/duplicate/);
  assert.throws(()=>buildExport(fixture(),core,{},['missing']),/Unknown layout/);
  assert.throws(()=>validateTransform({...transform,x:Infinity},'id'),/finite/);
  assert.throws(()=>validateTransform({...transform,flipped:'false'},'id'),/boolean/);
  assert.throws(()=>buildExport(fixture(),{worldPoint:()=>({x:NaN,y:0})}),/finite/);
});

const corePath=process.env.LAYOUT_STUDIO_CORE;
const core=corePath?require(require('node:path').resolve(corePath)):null;
const close=(a,b)=>assert(Math.abs(a-b)<1e-9,`${a} != ${b}`);

test('actual LayoutCore: anisotropic scale and top-left anchor',{skip:!core},()=>{
  const point=core.worldPoint({baseSize:[4,2]},transform,{x:3,y:1});
  close(point.x,16);close(point.y,23);
});
test('actual LayoutCore: ninety-degree rotation about scaled rectangle center',{skip:!core},()=>{
  const point=core.worldPoint({baseSize:[4,2]},{...transform,rotation:90},{x:0,y:0});
  close(point.x,17);close(point.y,19);
  const center=core.worldPoint({baseSize:[4,2]},{...transform,rotation:37},{x:2,y:1});
  close(center.x,14);close(center.y,23);
});
test('actual LayoutCore: both flips and scalar legacy scale',{skip:!core},()=>{
  const point=core.worldPoint({baseSize:[4,2]},{...transform,flipped:true,flippedV:true},{x:0,y:0});
  close(point.x,18);close(point.y,26);
  const scalar=core.worldPoint({baseSize:[4,2]},{...transform,scale:2},{x:3,y:1});
  close(scalar.x,16);close(scalar.y,22);
});
test('actual LayoutCore used end-to-end without reordering LED geometry',{skip:!core},()=>{
  const {exported}=buildExport(fixture(),core);
  assert.deepEqual(exported.layouts[0].points.map(p=>p.world),[{x:16,y:23},{x:10,y:20}]);
});
