let tpmsData={devices:[],drivers:[],search:{}},tpmsAt=performance.now(),tpmsLoading=false,selectedSensor='',tagSensorId='';
const instrument=id=>tpmsData.devices.find(d=>d.id===id)||(()=>{const k=settings.known.find(k=>k.id===id);return k?{...k,known:true,protocolId:k.protocolId||k.identity,observed:false}:null;})();
function sensorModel(d){const ageSeconds=d.observed?Math.max(0,Math.floor((Number(tpmsData.uptimeMs||0)+performance.now()-tpmsAt-d.lastSeenMs)/1000)):null;const number=d.protocolId||d.id;const pressure=d.pressureBar==null?'— bar':fmt(d.pressureBar,2)+' bar';const tooltip=`${T('Системный ID')}: ${d.id}\n${T('ID протокола')}: ${number}\n${d.address||'—'}\nΔ ${d.deltaPa==null?'—':fmt(d.deltaPa/100000,2)} bar / max ${d.peakDeltaPa==null?'—':fmt(d.peakDeltaPa/100000,2)} bar`;
 const tags=settings.known.find(k=>k.id===d.id)?.tags||d.tags||[];
 return {id:d.id,protocol:d.protocol==='tomtom'?'TomTom':d.protocol,transport:d.transport==='ble'?'BLE':d.transport,number,pressure,tags:tags.join(', '),age:ageSeconds==null?'— s':ageSeconds+' s',changed:!!d.changed,selected:selectedSensor===d.id,tooltip:tooltip+'\n'+T('Теги')+': '+tags.join(', '),accessible:`${d.protocol} / ${d.transport} / ${number}, ${pressure}, ${tags.join(', ')}, ${d.id}`};}
function tagButton(id){const b=btn('',()=>editTags(id),T('Теги'));b.className='icon-action tag-editor-button';delete b.dataset.tooltip;b.append(icon('tag'));return b;}
function editTags(id){const k=settings.known.find(k=>k.id===id);if(!k)return;tagSensorId=id;$('tagIdentity').textContent=(k.protocolId||k.identity)+' · '+k.id;$('tagValues').value=(k.tags||[]).join(', ');$('tagEditor').showModal();$('tagValues').focus();}
$('tagForm').onsubmit=async e=>{e.preventDefault();const tags=[...new Set($('tagValues').value.split(/[,;\n]/).map(s=>s.trim()).filter(Boolean))];$('saveTags').disabled=true;try{if(await perform({op:'known.tags',id:tagSensorId,tags})){$('tagEditor').close();await loadTpms();}}finally{$('saveTags').disabled=false;}};
function renderKnownDevices(){tableRows($('knownRows'),settings.known,k=>k.id,(tr,k)=>{const d=instrument(k.id)||k;cell(tr,0,k.id,'mono');cell(tr,1,k.protocolId||k.identity||'—','mono');cell(tr,2,k.protocol);cell(tr,3,k.transport);const tagCell=cell(tr,4,(k.tags||[]).join(', ')||'—');tagCell.dataset.tooltip=(k.tags||[]).join(', ');cell(tr,5,fmt(d.pressureBar,2),'number');cell(tr,6,d.observed?sensorModel(d).age:'—','number');const td=cell(tr,7);if(!td.children.length)td.append(tagButton(k.id),btn('ⓘ',()=>{},T('Подробности')),btn('×',()=>{},T('Забыть')));td.children[1].onclick=()=>details(k.id,JSON.stringify({...k,observation:d},null,2));td.children[2].onclick=async()=>{if(await confirmAction('Удалить известный датчик?'))perform({op:'known.remove',id:k.id});};});}
function renderPalette(id){const host=$(id);const old=new Map([...host.children].map(e=>[e.dataset.key,e]));const devices=[...tpmsData.devices];for(const k of settings.known)if(!devices.some(d=>d.id===k.id))devices.push(instrument(k.id));
 for(let i=0;i<devices.length;i++){const d=devices[i];let wrap=old.get(d.id);if(!wrap){wrap=el('div',null,'sensor-palette-item');wrap.dataset.key=d.id;const actions=el('div',null,'sensor-palette-actions');actions.append(btn(T('Наш'),()=>{}),tagButton(d.id));wrap.append(document.createElement('tpms-sensor'),actions);}old.delete(d.id);wrap.firstChild.model=sensorModel(d);const actions=wrap.lastChild,add=actions.firstChild;add.hidden=!!d.known;add.disabled=!d.observed||!d.available;add.textContent=T('Наш');add.onclick=()=>bindInstrument(d.id);actions.lastChild.hidden=!d.known;if(host.children[i]!==wrap)host.insertBefore(wrap,host.children[i]||null);}for(const e of old.values())e.remove();}
function renderSensorPalettes(){renderPalette('knownSensors');renderPalette('garageSensors');for(const e of document.querySelectorAll('[data-search-status]'))e.textContent=tpmsData.search?.active?T('Поиск')+' · Δ ≥ 0.10 bar':T('Поиск остановлен');const chosen=instrument(selectedSensor);for(const e of document.querySelectorAll('[data-selected-sensor]'))e.textContent=chosen?T('Выбран')+': '+(chosen.protocolId||chosen.id):T('Выберите датчик');$('driverCapabilities').textContent=(tpmsData.drivers||[]).map(d=>d.label).join(' · ')||T('Нет доступных драйверов');for(const b of document.querySelectorAll('[data-search="start"]'))b.textContent=T(tpmsData.search?.active?'Сбросить поиск':'Начать поиск');for(const b of document.querySelectorAll('[data-search="stop"]'))b.disabled=!tpmsData.search?.active;}
function renderVehicleCanvas(){renderVehicleDiagram($('vehicleCanvas'),draft,true);}
function renderHomeVehicle(){const vehicle=settings.garage.find(c=>c.id===settings.active);$('homeVehicleTitle').textContent=vehicle?.name||T('Нет машин');renderVehicleDiagram($('homeVehicleCanvas'),vehicle,false);}
function renderVehicleDiagram(root,vehicle,editable){
 if(!vehicle){root.replaceChildren();delete root.dataset.signature;return;}
 const signature=vehicle.id+'|'+vehicle.axles.map(a=>a.wheels.length).join(',')+'|'+editable;
 if(signature!==root.dataset.signature){
  root.dataset.signature=signature;root.replaceChildren();const frame=el('section',null,'vehicle-frame');
  const widest=Math.max(...vehicle.axles.map(a=>a.wheels.length));
  frame.style.setProperty('--vehicle-width',(widest===1?112:84*widest+120)+'px');
  root.append(frame);
  const front=el('div','↑','vehicle-front');front.setAttribute('role','img');front.setAttribute('aria-label',T('Перед'));frame.append(front);
  for(const [ai,a]of vehicle.axles.entries()){
   const row=el('section',null,'vehicle-axle'),slots=el('div',null,'axle-wheels');
   row.dataset.axleRow=ai;slots.dataset.count=a.wheels.length;
   const left=Math.ceil(a.wheels.length/2),right=a.wheels.length-left;
   slots.style.gridTemplateColumns=a.wheels.length===1?'1fr':`repeat(${left},80px) minmax(20px,1fr) repeat(${right},80px)`;
   for(const [wi]of a.wheels.entries()){
    const slot=el('div',null,'wheel-drop');slot.dataset.axle=ai;slot.dataset.wheel=wi;
    slot.style.gridColumn=String(a.wheels.length===1?1:wi<left?wi+1:wi+2);
    const content=el('div',null,'wheel-instrument');
    let footer;
    if(editable){footer=btn('×',()=>{if(!draft||draft.id!==vehicle.id)return;draft.axles[ai].wheels[wi].sensor='';markDirty();renderAssignments();},T('Снять датчик с колеса'));delete footer.dataset.tooltip;}
    else{footer=el('div',null,'wheel-telemetry');footer.append(el('span',null,'wheel-temperature'),el('span',null,'wheel-age'));}
    slot.append(content,footer);slots.append(slot);
   }
   row.append(slots);frame.append(row);
  }
 }
 for(const [ai,a]of vehicle.axles.entries()){
  const slots=root.firstChild.children[ai+1].firstChild;
  for(const [wi,w]of a.wheels.entries()){
   const slot=slots.children[wi];slot.dataset.sensor=w.sensor;slot.setAttribute('aria-label',a.name+' / '+w.name);if(editable)slot.lastChild.hidden=!w.sensor;
   const content=slot.firstChild,d=instrument(w.sensor);
   if(d){let tile=content.querySelector('tpms-sensor');if(!tile){tile=document.createElement('tpms-sensor');content.replaceChildren(tile);}tile.model={...sensorModel(d),readOnly:!editable,hideAge:!editable,selected:editable&&selectedSensor===d.id};}
   else{let empty=content.querySelector(':scope > .empty-wheel');if(!empty){empty=editable?btn('+',()=>assignInstrument(selectedSensor,ai,wi),T('Назначить датчик колесу')):el('div','—');delete empty.dataset.tooltip;empty.classList.remove('icon-action');empty.classList.add('empty-wheel');content.replaceChildren(empty);}empty.setAttribute('aria-label',T(editable?'Назначить датчик колесу':'Не назначен')+' '+a.name+' / '+w.name);}
   if(!editable){slot.lastChild.firstChild.textContent=fmt(d?.temperatureC,0)+'°C';slot.lastChild.lastChild.textContent=d?sensorModel(d).age.replace(' s','s'):'—s';}
  }
 }
}
async function bindInstrument(id){const d=instrument(id);if(!d?.observed){opState(T('Ожидается пакет'),'error');return false;}if(d.known)return true;const ok=await perform({op:'known.bind',discoveryId:id});if(ok)await loadTpms();return ok;}
async function assignInstrument(id,ai,wi){if(!id||!draft){opState(T('Выберите датчик'),'error');return;}const vehicleId=draft.id;const d=instrument(id);if(!d)return;if(!d.known&&!await bindInstrument(id))return;if(!draft||draft.id!==vehicleId||!draft.axles[ai]?.wheels[wi])return;if(!assignSensorToWheel(draft,id,ai,wi))return;selectedSensor='';markDirty();renderAssignments();renderSensorPalettes();}
async function recognizeResearch(row){await loadTpms();const d=tpmsData.devices.find(d=>d.address===row.address&&d.code===row.tpms?.code);if(d)await bindInstrument(d.id);else opState(T('Ожидается пакет'),'error');}
async function loadTpms(){if(tpmsLoading)return;tpmsLoading=true;try{tpmsData=await api('/api/tpms');tpmsAt=performance.now();renderSensorPalettes();renderKnownDevices();renderVehicleCanvas();renderHomeVehicle();}catch(e){opState(e.message,'error');}finally{tpmsLoading=false;}}
for(const b of document.querySelectorAll('[data-search]'))b.onclick=async()=>{try{await api('/api/tpms/search',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({active:b.dataset.search==='start'})});await loadTpms();}catch(e){opState(e.message,'error');}};
for(const b of document.querySelectorAll('[data-clear-sensor]'))b.onclick=()=>{selectedSensor='';renderSensorPalettes();renderVehicleCanvas();};
document.addEventListener('tpms-select',e=>{if(!e.detail.id||e.target.closest('#homeVehicleCanvas'))return;const slot=e.target.closest('.wheel-drop');if(slot&&selectedSensor&&selectedSensor!==e.detail.id){assignInstrument(selectedSensor,Number(slot.dataset.axle),Number(slot.dataset.wheel));return;}selectedSensor=selectedSensor===e.detail.id?'':e.detail.id;renderSensorPalettes();renderVehicleCanvas();});
let dragId='',touchDrag=null,ignoreSensorClickUntil=0;
const dropSlot=target=>target?.closest?.('.wheel-drop');
function highlightSlot(slot){for(const e of document.querySelectorAll('.wheel-drop.drop-hover'))if(e!==slot)e.classList.remove('drop-hover');slot?.classList.add('drop-hover');}
document.addEventListener('dragstart',e=>{const tile=e.target.closest('tpms-sensor');if(!tile||tile.model.readOnly)return;dragId=tile.model.id;e.dataTransfer.setData('application/x-tpms-id',dragId);e.dataTransfer.effectAllowed='move';});
$('vehicleCanvas').addEventListener('dragover',e=>{if(dragId){e.preventDefault();e.dataTransfer.dropEffect='move';highlightSlot(dropSlot(e.target));}});
$('vehicleCanvas').addEventListener('drop',e=>{const slot=dropSlot(e.target);if(slot&&dragId){e.preventDefault();assignInstrument(dragId,Number(slot.dataset.axle),Number(slot.dataset.wheel));}highlightSlot(null);dragId='';});
document.addEventListener('dragend',()=>{dragId='';highlightSlot(null);});
function clearTouchDrag(){if(!touchDrag)return;clearTimeout(touchDrag.timer);touchDrag.ghost?.remove();touchDrag=null;highlightSlot(null);}
document.addEventListener('touchstart',e=>{const tile=e.target.closest('tpms-sensor');if(!tile||tile.model.readOnly||e.touches.length!==1)return;const t=e.touches[0];touchDrag={tile,id:tile.model.id,x:t.clientX,y:t.clientY,active:false,ghost:null,timer:null};touchDrag.timer=setTimeout(()=>{if(!touchDrag)return;touchDrag.active=true;const ghost=document.createElement('tpms-sensor');ghost.className='sensor-drag-ghost';ghost.setAttribute('aria-hidden','true');ghost.inert=true;document.body.append(ghost);ghost.model={...tile.model,selected:true};ghost.style.left=(touchDrag.x-38)+'px';ghost.style.top=(touchDrag.y-75)+'px';touchDrag.ghost=ghost;},350);},{passive:true});
document.addEventListener('touchmove',e=>{if(!touchDrag)return;if(e.touches.length!==1){clearTouchDrag();return;}const t=e.touches[0];if(!touchDrag.active){if(Math.hypot(t.clientX-touchDrag.x,t.clientY-touchDrag.y)>8)clearTouchDrag();return;}e.preventDefault();touchDrag.ghost.style.left=(t.clientX-38)+'px';touchDrag.ghost.style.top=(t.clientY-75)+'px';highlightSlot(dropSlot(document.elementFromPoint(t.clientX,t.clientY)));},{passive:false});
document.addEventListener('touchend',e=>{if(!touchDrag)return;const d=touchDrag;if(d.active){e.preventDefault();ignoreSensorClickUntil=performance.now()+500;const t=e.changedTouches[0],slot=dropSlot(document.elementFromPoint(t.clientX,t.clientY));if(slot)assignInstrument(d.id,Number(slot.dataset.axle),Number(slot.dataset.wheel));}clearTouchDrag();},{passive:false});
document.addEventListener('touchcancel',clearTouchDrag);
document.addEventListener('click',e=>{if(performance.now()<ignoreSensorClickUntil&&e.target.closest('tpms-sensor')){e.preventDefault();e.stopImmediatePropagation();}},true);
document.addEventListener('contextmenu',e=>{if(e.target.closest('tpms-sensor'))e.preventDefault();});
setInterval(()=>{for(const tile of document.querySelectorAll('tpms-sensor:not(.sensor-drag-ghost)')){const d=instrument(tile.model?.id);if(d){const readOnly=!!tile.model.readOnly;tile.model={...sensorModel(d),readOnly,hideAge:readOnly,selected:!readOnly&&selectedSensor===d.id};}}renderHomeVehicle();},1000);
help.identify=['Начните поиск до изменения давления. База — последнее измерение в начале сеанса, либо первый пришедший пакет. При суммарном изменении от базы не менее 0,10 bar компонент меняет цвет до сброса поиска. Это признак изменения, положение колеса выбирает пользователь. Перетащите компонент на колесо и сохраните машину. На телефоне можно выбрать датчик касанием, затем коснуться колеса; для перетаскивания удерживайте компонент. Давность показывает, когда реально приходил пакет.','Start before changing pressure. The baseline is the last reading at session start or the first received packet. A cumulative change of at least 0.10 bar marks the component until reset. This marks a change; the user chooses wheel position. Drag onto a wheel and save the vehicle. On touch screens select a sensor then a wheel, or hold to drag. Age indicates the actual last packet.'];
help.garage=['Идентичность датчика неизменна и приходит из обнаружения. Названия осей и колёс принадлежат машине. Выберите или перетащите датчик на колесо, затем сохраните машину. Перемещение между занятыми местами меняет датчики местами; при переносе на пустое место прежнее освобождается. Изменить число осей и колёс можно в «Оси и колёса».','Sensor identity comes from discovery and is immutable. Axle and wheel names belong to the vehicle. Select or drag a sensor to a wheel, then save the vehicle. Moving between occupied positions swaps their sensors; moving to an empty position clears the source. Edit geometry under Axles and wheels.'];
