import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
const assign=vm.runInNewContext((await readFile(new URL('../src/www/wheel-assignment.js',import.meta.url),'utf8'))+'\nassignSensorToWheel');
const vehicle=()=>({id:'test',axles:[{wheels:[{name:'L',sensor:'A'},{name:'R',sensor:'B'}]},{wheels:[{name:'Only',sensor:'C'},{name:'Empty',sensor:''}]}]});
const ids=v=>v.axles.flatMap(a=>a.wheels.map(w=>w.sensor));
const v=vehicle();
assert(assign(v,'A',0,1));assert.deepEqual(ids(v),['B','A','C','']);
assert(assign(v,'A',0,0));assert.deepEqual(ids(v),['A','B','C','']);
assert(assign(v,'A',1,0));assert.deepEqual(ids(v),['C','B','A','']);
assert(assign(v,'A',1,1));assert.deepEqual(ids(v),['C','B','','A']);
assert(!assign(v,'A',1,1));assert.deepEqual(ids(v),['C','B','','A']);
assert(assign(v,'D',0,1));assert.deepEqual(ids(v),['C','D','','A']);
assert(!assign(v,'D',9,9));assert(!assign(v,'',0,1));
const garage={cars:[vehicle(),vehicle()]};assign(garage.cars[0],'A',0,1);assert.deepEqual(ids(garage.cars[1]),['A','B','C','']);
for(const car of [v,...garage.cars]){const assigned=ids(car).filter(Boolean);assert.equal(new Set(assigned).size,assigned.length);}
assert.equal(v.axles[0].wheels[0].name,'L');assert.equal(v.axles[1].wheels[1].name,'Empty');
console.log('Wheel swap, cross-axle move, empty target, same target and registry assignment passed');
