// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the actual compiled C++ packets against the installed SignalRGB JS.
// The VM has no hardware API, filesystem, network, or require capability.
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const [dump, pluginDirectory] = process.argv.slice(2);
if (!dump || !pluginDirectory) throw new Error('Usage: node signal-parity.cjs native-parity.json SIGNAL_PLUGIN_DIRECTORY');
const native = JSON.parse(fs.readFileSync(dump, 'utf8'));
const profiles = new Map();
const contexts = [];
for (let generation = 1; generation <= 3; generation++) {
    const file = path.join(pluginDirectory, `Alienware_Monitor_Gen${generation}_Controller.js`);
    const source = fs.readFileSync(file, 'utf8').replace(/^export /gm, '');
    const context = vm.createContext({});
    vm.runInContext(source, context, {filename: path.basename(file), timeout: 1000});
    const modelData = JSON.parse(vm.runInContext('JSON.stringify({vid:PLUGIN_VID,profiles:PROFILES})', context));
    for (const [pid, model] of Object.entries(modelData.profiles))
        profiles.set(`${modelData.vid}:${pid}`, {model, context});
    contexts.push(context);
}
assert.equal(native.profiles.length, profiles.size, 'Native/SignalRGB model count');
let packets = 0;
for (const p of native.profiles) {
    const reference = profiles.get(`${p.vid}:${p.pid}`);
    assert(reference, `Missing SignalRGB model ${p.name}`);
    const {model, context} = reference;
    for (const key of ['protocol', 'auth', 'interval', 'all'])
        assert.equal(p[key], model[key], `${p.name}: ${key}`);
    assert.deepEqual(p.zones.map(z=>z.mask), model.zones.map(z=>z.mask), `${p.name}: zone masks`);
    for (const report of p.reports) {
        const js = JSON.parse(vm.runInContext(`JSON.stringify(colorPacket(PROFILES[${p.pid}],${report.mask},${JSON.stringify(report.rgb)}))`, context));
        assert.deepEqual(report.packet, js, `${p.name}: mask ${report.mask}, RGB ${report.rgb}`);
        packets++;
    }
}
for (const vector of native.auth) {
    const js = JSON.parse(vm.runInContext(`JSON.stringify(authenticationResponse(${JSON.stringify(vector.token)},OEM_KEYS[${vector.key}]))`, contexts[2]));
    assert.deepEqual(vector.answer, js, `OEM response ${vector.key}`);
}
console.log(`Installed SignalRGB parity: ${profiles.size} profiles, ${packets} packets, ${native.auth.length} OEM responses: PASS`);
