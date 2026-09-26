// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const SCHEMA = 'openrgb-room/canvas-led-export/v1';
function clone(value) { return JSON.parse(JSON.stringify(value)); }
function fail(message) { throw new Error(message); }
function finite(value, label) { if (!Number.isFinite(value)) fail(`${label}: finite number required`); return value; }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }

function eligibility(device) {
  const reasons = [];
  if (device.enabled !== true) reasons.push(device.enabled === false ? 'disabled' : 'enabled-state-unknown');
  if (device.active !== true) reasons.push(device.active === false ? 'inactive' : 'active-state-unknown');
  if (device.excludedReason) reasons.push(device.excludedReason);
  return reasons;
}

function validateTransform(transform, id) {
  if (!transform || typeof transform !== 'object' || Array.isArray(transform)) fail(`${id}: transform missing`);
  finite(transform.x, `${id}.x`); finite(transform.y, `${id}.y`);
  finite(transform.rotation ?? 0, `${id}.rotation`);
  const scale = typeof transform.scale === 'number' ? {x:transform.scale,y:transform.scale} : (transform.scale || {x:1,y:1});
  if (finite(scale.x, `${id}.scale.x`) <= 0 || finite(scale.y, `${id}.scale.y`) <= 0) fail(`${id}: scale must be positive`);
  for (const key of ['flipped', 'flippedV']) if (key in transform && typeof transform[key] !== 'boolean') fail(`${id}.${key}: boolean required`);
  if (transform.brightness !== undefined && (finite(transform.brightness, `${id}.brightness`) < 0 || transform.brightness > 100)) fail(`${id}: brightness outside 0..100`);
}

function buildExport(project, core, source = {}, names = null) {
  if (!core || typeof core.worldPoint !== 'function') fail('LayoutCore.worldPoint is required');
  if (!project || !Array.isArray(project.devices) || !Array.isArray(project.layouts)) fail('Unsupported Layout Studio project');
  const canvas = clone(project.canvas);
  if (finite(canvas?.width, 'canvas.width') <= 0 || finite(canvas?.height, 'canvas.height') <= 0) fail('Invalid canvas dimensions');
  const ids = new Set();
  const devices = project.devices.map((device, deviceOrder) => {
    if (!device || typeof device.id !== 'string' || !device.id || ids.has(device.id)) fail('Missing/duplicate device identity');
    ids.add(device.id);
    if (!Array.isArray(device.baseSize) || device.baseSize.length !== 2 || !device.baseSize.every(v => Number.isFinite(v) && v > 0)) fail(`${device.id}: base dimensions unavailable`);
    if (!Array.isArray(device.leds)) fail(`${device.id}: LED geometry unavailable`);
    return {
      id:device.id, name:device.name ?? device.id, kind:device.kind ?? null,
      parentId:device.parentId ?? null, channel:device.channel ?? null,
      componentOrder:device.order ?? null, sourceDeviceOrder:deviceOrder,
      enabled:typeof device.enabled === 'boolean' ? device.enabled : null,
      active:typeof device.active === 'boolean' ? device.active : null,
      excludedReason:device.excludedReason || '', geometryUncertain:device.uncertain === true,
      notes:device.notes || '', role:device.role || '', baseSize:clone(device.baseSize),
      leds:device.leds.map((led, ledIndex) => ({
        ledIndex, name:led.name ?? null,
        local:{x:finite(led.x,`${device.id}.led[${ledIndex}].x`),y:finite(led.y,`${device.id}.led[${ledIndex}].y`)}
      }))
    };
  });
  const requested = names ? new Set(names) : null;
  const seenNames = new Set();
  const layouts = [];
  for (const layout of project.layouts) {
    if (!layout || typeof layout.name !== 'string' || seenNames.has(layout.name)) fail('Missing/duplicate layout name');
    seenNames.add(layout.name);
    if (requested && !requested.has(layout.name)) continue;
    if (!layout.entries || typeof layout.entries !== 'object' || Array.isArray(layout.entries)) fail(`${layout.name}: entries required`);
    const brightness = layout.brightness ?? 100;
    if (finite(brightness,`${layout.name}.brightness`) < 0 || brightness > 100) fail('Layout brightness outside 0..100');
    const result = {name:layout.name, brightness, readOnly:layout.readOnly === true,
      transforms:clone(layout.entries), points:[], exclusions:[], missingGeometry:[], statistics:{}};
    for (const [id, transform] of Object.entries(layout.entries)) {
      validateTransform(transform,id);
      if (!ids.has(id)) result.missingGeometry.push({deviceId:id,reason:'No device geometry in source project',transform:clone(transform)});
    }
    for (const device of devices) {
      const transform = layout.entries[device.id];
      const reasons = eligibility(device);
      if (!transform) reasons.push('placement-missing-in-layout');
      if (device.leds.length === 0) reasons.push('no-led-geometry');
      if (reasons.length) {
        result.exclusions.push({deviceId:device.id,parentId:device.parentId,ledCount:device.leds.length,reasons});
        continue;
      }
      // Keep source device/LED order; never derive an OpenRGB offset from it.
      for (const led of device.leds) {
        const world = core.worldPoint(device,transform,led.local);
        finite(world?.x,`${device.id}.world.x`); finite(world?.y,`${device.id}.world.y`);
        result.points.push({
          deviceId:device.id,parentId:device.parentId,channel:device.channel,componentOrder:device.componentOrder,
          ledIndex:led.ledIndex,name:led.name,local:clone(led.local),world:{x:world.x,y:world.y},
          insideCanvas:world.x >= 0 && world.x < canvas.width && world.y >= 0 && world.y < canvas.height,
          geometryUncertain:device.geometryUncertain
        });
      }
    }
    result.statistics = {
      eligiblePoints:result.points.length,
      eligibleDevices:new Set(result.points.map(point=>point.deviceId)).size,
      excludedDevices:result.exclusions.length,missingGeometryDevices:result.missingGeometry.length,
      outsideCanvasPoints:result.points.filter(point=>!point.insideCanvas).length,
      uncertainPoints:result.points.filter(point=>point.geometryUncertain).length
    };
    layouts.push(result);
  }
  if (requested) for (const name of requested) if (!seenNames.has(name)) fail(`Unknown layout: ${name}`);
  const exported = {
    schema:SCHEMA, source:{...source,projectSchemaVersion:project.schemaVersion ?? null,
      capturedAt:project.capturedAt ?? null,sourceHash:project.sourceHash ?? null},
    canvas, geometry:{function:'LayoutCore.worldPoint',note:project.geometryNote || '',
      outsideCanvasPolicy:'preserve and flag; do not silently clamp or remove',
      brightnessPolicy:'preserve layout and transform brightness separately; no invented global brightness'},
    devices,layouts,
    limitations:[
      'Active/enabled states are inherited from the saved project, not re-detected hardware.',
      'LED indices preserve source array order, not a proven OpenRGB order or parent-channel offset.',
      'Missing geometry/placements are retained as unresolved records; no fallback placement is invented.',
      'This is a canonical migration dataset, not a Visual Map import file.'
    ]
  };
  const mapping = {
    schema:'openrgb-room/identity-mapping-template/v1',canvasSourceSchema:SCHEMA,
    instructions:'Fill exact stable controller identity, explicit zone index and target LED index for each source LED after verification. Never match solely by current enumeration index or IP address.',
    devices:devices.map(device=>({
      signalRgb:{id:device.id,parentId:device.parentId,channel:device.channel,componentOrder:device.componentOrder,name:device.name},
      sourceExcludedReasons:eligibility(device),
      openRgb:{status:'unmapped',controller:{serial:null,name:null,vendor:null,location:null},zoneIndex:null,
        ledMap:device.leds.map(led=>({sourceLedIndex:led.ledIndex,targetLedIndex:null})),verifiedBy:null}
    }))
  };
  return {exported,mapping};
}

function argumentsFrom(argv) {
  const args={layouts:[]};
  for (let i=0;i<argv.length;i++) {
    const key=argv[i];
    if (key==='--help') return {help:true};
    if (!['--project','--core','--out','--layout'].includes(key) || !argv[i+1]) fail(`Unknown or incomplete argument: ${key}`);
    const value=argv[++i];
    if(key==='--layout') args.layouts.push(value); else args[key.slice(2)]=value;
  }
  if(!args.project) fail('--project is required');
  return args;
}

function main(argv) {
  const args=argumentsFrom(argv);
  if(args.help) {
    console.log('node export-canvas.cjs --project <project.json> [--core <layout-core.js>] [--out <private-directory>] [--layout <exact-name> ...]');
    return;
  }
  const projectPath=path.resolve(args.project);
  const corePath=path.resolve(args.core || path.join(path.dirname(projectPath),'layout-core.js'));
  const projectBytes=fs.readFileSync(projectPath),coreBytes=fs.readFileSync(corePath);
  const project=JSON.parse(projectBytes.toString('utf8').replace(/^\uFEFF/,''));
  const core=require(corePath);
  const result=buildExport(project,core,{projectSha256:sha256(projectBytes),layoutCoreSha256:sha256(coreBytes)},args.layouts.length?args.layouts:null);
  const output=path.resolve(args.out || path.join(__dirname,'local-output'));
  fs.mkdirSync(output,{recursive:true});
  fs.writeFileSync(path.join(output,'canvas.json'),JSON.stringify(result.exported,null,2)+'\n','utf8');
  // Never overwrite a user's mapping work when refreshing the geometry snapshot.
  const mappingPath=path.join(output,'identity-mapping.template.json');
  const mappingCreated=!fs.existsSync(mappingPath);
  if(mappingCreated) fs.writeFileSync(mappingPath,JSON.stringify(result.mapping,null,2)+'\n',{encoding:'utf8',flag:'wx'});
  console.log(JSON.stringify({output,mappingTemplateCreated:mappingCreated,layouts:result.exported.layouts.map(l=>({name:l.name,...l.statistics}))},null,2));
}

module.exports={buildExport,eligibility,validateTransform,argumentsFrom,sha256};
if(require.main===module) {
  try { main(process.argv.slice(2)); } catch(error) { console.error(error.message);process.exitCode=1; }
}
