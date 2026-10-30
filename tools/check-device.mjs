import assert from 'node:assert/strict';
import {decode} from '../../NaCBORUIforLVGL/node_modules/cborg/cborg.js';

if(!process.argv[2]) throw Error('Usage: node tools/check-device.mjs http://device:80');
const base = new URL(process.argv[2]);
const checks = [];
const authorization = 'Basic ' + Buffer.from(`${process.env.TPMS_HTTP_USER ?? '1'}:${process.env.TPMS_HTTP_PASSWORD ?? '1'}`).toString('base64');
const anonymous = await fetch(new URL('/api/state',base),{signal:AbortSignal.timeout(10000)});
assert.equal(anonymous.status,401); await anonymous.text(); checks.push('HTTP requires the existing login');
async function get(path) {
  const response = await fetch(new URL(path,base),{headers:{Authorization:authorization},signal:AbortSignal.timeout(10000)});
  assert.equal(response.status,200,path);
  return response;
}
const state = await (await get('/api/state')).json();
assert.equal(state.ready,true); assert.equal(state.msgType,'snapshot');
assert.equal(typeof state.reports,'number'); assert.equal(state.diagnostic.capture_ok,true);
assert(state.diagnostic.tasks.length > 0); checks.push('live task/receiver snapshot');
const cbor = decode(new Uint8Array(await (await get('/api/state.cbor')).arrayBuffer()));
assert.equal(cbor.msgType,'snapshot'); assert.equal(cbor.ready,true);
assert(Array.isArray(cbor.sensors)); checks.push('equivalent CBOR contract');
for(const path of ['/','/ble','/index','/index2','/css.css']) {
  const response = await get(path); const text = await response.text();
  assert(text.length > 100,path);
}
checks.push('all retained page routes and stylesheet');

function socketClient() {
  return new Promise((resolve,reject)=>{
    const url = new URL('/ws',base); url.protocol = base.protocol==='https:'?'wss:':'ws:';
    const ws = new WebSocket(url); ws.binaryType='arraybuffer';
    const queued = [], waiting = [];
    const timer = setTimeout(()=>{ws.close();reject(Error('WS open timeout'));},10000);
    ws.onmessage = event => waiting.length ? waiting.shift()(event.data) : queued.push(event.data);
    ws.onopen = ()=>{
      clearTimeout(timer);
      resolve({ws,next:()=>new Promise((done,fail)=>{
        const timeout=setTimeout(()=>fail(Error('WS message timeout')),10000);
        const complete=value=>{clearTimeout(timeout);done(value);};
        queued.length ? complete(queued.shift()) : waiting.push(complete);
      })});
    };
    ws.onerror = ()=>{clearTimeout(timer);reject(Error('WS transport error'));};
  });
}
const clients = await Promise.all([socketClient(),socketClient()]);
try {
  for(const [index,client] of clients.entries()) {
    let snapshot;
    do { const message=await client.next(); if(typeof message==='string') snapshot=JSON.parse(message); }
    while(snapshot?.msgType!=='snapshot');
    assert.equal(snapshot.ready,true);
    const marker=JSON.stringify({type:'echo',content:`device check ${index} / проверка`});
    client.ws.send(marker);
    let echoed;
    do { echoed=await client.next(); } while(echoed!==marker);
    const bytes=new Uint8Array([0,1,2,127,128,255,index]); client.ws.send(bytes);
    do { echoed=await client.next(); } while(!(echoed instanceof ArrayBuffer));
    assert.deepEqual(new Uint8Array(echoed),bytes);
  }
  checks.push('two simultaneous WebSocket clients, Unicode text and binary echo');
  const controller = new AbortController();
  const timer=setTimeout(()=>controller.abort(),12000);
  try {
    const response=await fetch(new URL('/events',base),{headers:{Authorization:authorization},signal:controller.signal});
    assert.equal(response.status,200); assert.match(response.headers.get('content-type'),/text\/event-stream/);
    const reader=response.body.getReader(); let text='';
    while(!text.includes('data:')) { const part=await reader.read(); assert(!part.done); text+=new TextDecoder().decode(part.value); }
    assert.match(text,/webTick/); checks.push('SSE event alongside two WS sessions');
    await reader.cancel();
  } finally { clearTimeout(timer); controller.abort(); }
} finally { for(const client of clients) client.ws.close(); }
const later=await (await get('/api/state')).json();
assert(later.uptimeMs > state.uptimeMs); assert(later.webTick > state.webTick);
assert.equal(later.scan,'scanning'); checks.push('continuous BLE/main loop during mixed traffic');
console.log(JSON.stringify({passed:checks.length,checks},null,2));
