// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
const base=process.argv[2];if(!base)throw Error('Usage: node tools/check-settings.mjs http://device:80');
const headers={Authorization:'Basic '+Buffer.from(`${process.env.TPMS_HTTP_USER??'1'}:${process.env.TPMS_HTTP_PASSWORD??'1'}`).toString('base64')};
async function get(path){let r=await fetch(base+path,{headers,signal:AbortSignal.timeout(10000)});assert.equal(r.status,200,path);return r.json();}
async function command(op,expected){let s=await get('/api/settings');let r=await fetch(base+'/api/settings',{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify({revision:s.revision,...op}),signal:AbortSignal.timeout(10000)});assert.equal(r.status,202);let q=await r.json();assert(q.id);for(let i=0;i<80;i++){await new Promise(r=>setTimeout(r,100));let result=await get('/api/operation?id='+q.id);if(result.done){if(expected){assert.equal(result.ok,false);assert.equal(result.error,expected);}else assert.equal(result.ok,true,result.error);return result;}}throw Error('operation_timeout');}
async function read(name){const chunks=[];let offset=0,total=1;while(offset<total){let r=await get('/api/file?name='+encodeURIComponent(name)+'&offset='+offset);total=r.total;let b=Buffer.from(r.hex,'hex');assert(b.length||offset===total);chunks.push(b);offset+=b.length;}return Buffer.concat(chunks);}
const before=await get('/api/settings'),files=await get('/api/files');assert(files.mounted);assert(files.total>3*1024*1024);const foreign=new Map();for(const f of files.files.filter(f=>f.owner!=='TPMS'))foreign.set(f.name,await read(f.name));
const id=before.known[0]?.id, carid='__verification_vehicle';let checks=[];
assert(id,'requires at least one real registered sensor');
assert(!before.garage.some(c=>c.id===carid),'verification vehicle already exists; refusing to overwrite');
try{
 assert.equal(before.version,2);for(const k of before.known){assert(!('name' in k));assert(k.id&&k.protocolId&&k.driver);}checks.push('immutable registry has system and protocol identities, no sensor names');
 await command({op:'known.put',id,name:'forged',transport:'rf433',protocol:'invented',code:1},'immutable_sensor_identity');
 let forged=await fetch(base+'/api/settings',{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify({revision:before.revision,op:'known.bind',discoveryId:'__not_observed',transport:'ble',protocol:'tomtom',code:1})});assert.equal(forged.status,422);assert.equal((await forged.json()).error,'discovery_not_observed');assert.equal((await get('/api/settings')).revision,before.revision);checks.push('manual identity edits and invented discoveries rejected without mutation');
 let catalog=await get('/api/tpms');assert(catalog.drivers.some(d=>d.id==='tomtom-ble'&&d.transport==='ble'&&d.protocol==='tomtom'));assert(!catalog.drivers.some(d=>d.transport==='rf433'));assert.equal(catalog.search.thresholdPa,10000);for(const k of before.known)assert(catalog.devices.some(d=>d.id===k.id&&d.known));
 for(const active of [true,false]){const r=await fetch(base+'/api/tpms/search',{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify({active})});assert.equal(r.status,202);for(let i=0;i<60;i++){await new Promise(r=>setTimeout(r,100));catalog=await get('/api/tpms');if(catalog.search.active===active)break;if(i===59)throw Error('search did not apply');}}checks.push('compiled driver catalog and owner-task search start/stop');
 const freeTags=['verification tag','Свободный тег'];await command({op:'known.tags',id,tags:freeTags});let tagged=await get('/api/settings');assert.deepEqual(tagged.known.find(k=>k.id===id).tags,freeTags);assert.deepEqual(tagged.garage,before.garage);for(const tags of ['invalid',[42],['a','a'],['x'.repeat(65)]])await command({op:'known.tags',id,tags},'sensor_tags_invalid');checks.push('free-form Unicode tags persisted independently; malformed tags rejected');
 const vehicle={id:carid,name:'Verification only',axles:[{name:'Mono',wheels:[{name:'Wheel',sensor:id}]}]};
 await command({op:'vehicle.put',vehicle,makeDefault:true});const committedDefault=await get('/api/settings');assert.equal(committedDefault.active,carid);assert.deepEqual(committedDefault.garage.find(c=>c.id===carid).axles,vehicle.axles);checks.push('vehicle layout and startup default committed together');
 const revision=(await get('/api/settings')).revision;
 await command({op:'known.remove',id},'sensor_assigned');
 await command({op:'vehicle.put',vehicle:{...vehicle,axles:[{name:'A',wheels:[{name:'W1',sensor:id},{name:'W2',sensor:id}]}]}},'wheel_assignment_invalid');
 await command({op:'vehicle.select',id:'__missing'},'active_vehicle_missing');
 await command({op:'vehicle.select',id:carid,revision:revision-1},'revision_conflict');
 assert.equal((await get('/api/settings')).revision,revision);checks.push('assignment integrity, missing references and optimistic conflicts');
 const truck={...vehicle,axles:[2,4,1].map((n,a)=>({name:'Axle '+a,wheels:Array.from({length:n},(_,w)=>({name:'Wheel '+w,sensor:a===0&&w===0?id:''}))}))};
 await command({op:'vehicle.put',vehicle:truck});let saved=(await get('/api/settings')).garage.find(c=>c.id===carid);assert.deepEqual(saved.axles.map(a=>a.wheels.length),[2,4,1]);checks.push('variable axle and wheel geometry');
 const slots=(await get('/api/files')).files.filter(f=>/^tpms\.cfg\.[01]$/.test(f.name));assert.equal(slots.length,2);for(const slot of slots){const bytes=await read(slot.name);assert.equal(bytes.subarray(0,8).toString(),'TPMSCFG1');assert.equal(Number(bytes.readBigUInt64LE(16)),bytes.length-32);let crc=0xffffffff;for(const b of bytes.subarray(32)){crc^=b;for(let i=0;i<8;i++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}assert.equal(Number(bytes.readBigUInt64LE(24)),(~crc)>>>0);const payload=JSON.parse(bytes.subarray(32).toString());assert.equal(Number(bytes.readBigUInt64LE(8)),payload.revision+1);}checks.push('two complete, readable persistent generations');
 let denied=await fetch(base+'/api/file?name='+encodeURIComponent('../anything'),{headers});assert.equal(denied.status,404);checks.push('file browser traversal rejected');
 for(const mode of ['active','passive']){let r=await fetch(base+'/api/scan',{method:'POST',headers:{...headers,'Content-Type':'application/json'},body:JSON.stringify({mode})});assert.equal(r.status,202);for(let i=0;i<60;i++){await new Promise(r=>setTimeout(r,100));if((await get('/api/state')).scanMode===mode)break;if(i===59)throw Error('scan mode did not apply');}}checks.push('passive/active applied by BT owner task');
}finally{
 let now=await get('/api/settings');if(now.garage.some(c=>c.id===carid))await command({op:'vehicle.remove',id:carid});now=await get('/api/settings');if(before.active&&now.active!==before.active)await command({op:'vehicle.select',id:before.active});now=await get('/api/settings');const originalTags=before.known.find(k=>k.id===id).tags||[];if(JSON.stringify(now.known.find(k=>k.id===id).tags)!==JSON.stringify(originalTags))await command({op:'known.tags',id,tags:originalTags});
}
// Leave both recovery generations free of temporary verification entries.
await command({op:'vehicle.select',id:before.active});
const after=await get('/api/settings');assert.deepEqual(after.known,before.known);assert.deepEqual(after.garage,before.garage);assert.equal(after.active,before.active);
for(const [name,bytes]of foreign)assert.deepEqual(await read(name),bytes,'foreign file changed: '+name);checks.push('original garage restored; every foreign FS file unchanged');
assert((await get('/api/state')).dropped===0);await writeFile('local/reboot-expected.json',JSON.stringify(after));console.log(JSON.stringify({passed:checks.length,checks,revision:after.revision,foreignFiles:foreign.size},null,2));
