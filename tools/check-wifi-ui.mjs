// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const root=new URL('../',import.meta.url);
const read=p=>readFile(new URL(p,root),'utf8');
const project=JSON.parse(await read('src/ui/project.json'));
for(const id of ['radio','radio-small']){
 const screen=project.screens[id],nodes=screen.root.children;
 const button=nodes.find(n=>n.id==='wifi-ap-open');
 assert(button,`${id}: AP start is available`);
 assert.equal(button.events[0].action,'wifi.ap.open');
 assert.equal(button.events[0].args.literal,300);
 assert(button.style.x>=0 && button.style.x+button.style.width<=screen.size.width,`${id}: AP button fits width`);
 for(const node of nodes.filter(n=>n!==button)){
  const a=button.style,b=node.style;
  assert(!(a.x<b.x+b.width && a.x+a.width>b.x && a.y<b.y+b.height && a.y+a.height>b.y),`${id}: AP button overlaps ${node.id}`);
 }
 assert(nodes.find(n=>n.id==='wifi-ap-info')?.bindings.some(b=>b.value.source==='wifi.ap'),`${id}: local credentials visible`);
 if(id==='radio-small')assert(screen.root.flags.SCROLLABLE,'compact settings retain scroll access to AP details');
}
const web=await read('src/modules/web/web.cpp');
for(const route of ['wifi_config_read','wifi_config_write','wifi_result','setup_page']){
 const start=web.indexOf(`Route ${route}(`);
 const definition=web.slice(start,web.indexOf('authenticated_http);',start)+20);
 assert(definition.includes('private_http')&&definition.includes('authenticated_http'),`${route}: authentication required`);
}
const dashboard=await read('src/modules/lcd/dashboard.cpp');
assert(dashboard.includes('"registry", public_registry'),'HTTP registry uses redacted snapshot');
assert(dashboard.includes('== "wifi.ap"'),'local credential source excluded from HTTP registry');
const markup=await read('src/www/shell-head.html');
assert.match(markup, /id="wifiPassword" type="password"/);
const js=await read('src/www/app.js');
assert(!js.includes('console.log(command)'),'no credential command logging');
assert(js.includes("$('wifiEditor').addEventListener('close',()=>{$('wifiPassword').value='';})"),'password cleared after closing editor');
console.log('Wi-Fi setup UI contracts pass (native bounds, actions, authentication, credential redaction).');
