'use strict';
const $=id=>document.getElementById(id), app=$('app');
const translations={
'Главная':'Home','Перед':'Front','Показывать при включении':'Show at startup','Теги':'Tags','Теги датчика':'Sensor tags','Теги через запятую':'Tags separated by commas','Задача':'Task','Состояние':'State','Приоритет':'Priority','Стек, байт':'Stack, bytes','Выберите датчик':'Select a sensor','Нет доступных драйверов':'No available drivers','Снять датчик с колеса':'Unassign sensor','Назначить датчик колесу':'Assign sensor to wheel','Сбросить поиск':'Reset search','Начать поиск':'Start search','Остановить':'Stop','Снять выбор':'Clear selection','Оси и колёса':'Axles and wheels','Обнаружение и поиск':'Discovery and identification','Системный ID':'System ID','ID протокола':'Protocol ID','Колёса машины':'Vehicle wheels','Датчики для назначения':'Sensors to assign','Обнаруженные датчики':'Discovered sensors','Выбран':'Selected','Поиск':'Search','Поиск остановлен':'Search stopped','Ожидается пакет':'Waiting for a packet','Наш':'Ours','Не назначен':'Unassigned','Забыть':'Forget','Закрыть меню':'Close menu','Машина':'Vehicle','Раздел':'Section','Разделы':'Sections','Меню':'Menu','Название машины':'Vehicle name','Удалить машину':'Delete vehicle','Закрыть':'Close','Задачи':'Tasks','Вытеснено':'Evicted','Давление в шинах':'Tire pressure','Колесо':'Wheel','Датчик':'Sensor','Давность, с':'Age, s','Батарея, %':'Battery, %','Тревога':'Alarm','Приёмник':'Receiver','Реклама':'Advertising','Все устройства BT':'All Bluetooth devices','Пакеты':'Packets','Разбор':'Parsing','Известный':'Known','Действия':'Actions','Имя':'Name','Тип':'Type','Данные':'Data','Код':'Code','Файлы':'Files','Обновить':'Refresh','Байты':'Bytes','Владелец':'Owner','Гараж':'Garage','Добавить машину':'Add vehicle','Сохранить':'Save','Машины':'Vehicles','Название':'Name','Оси / колёса':'Axles / wheels','Добавить ось':'Add axle','Ось':'Axle','Известный датчик':'Known sensor','Отменить правки':'Discard edits','Известные датчики':'Known sensors','Добавить':'Add','Транспорт':'Transport','Протокол':'Protocol','Подтверждение':'Confirmation','Продолжить':'Continue','Отмена':'Cancel','Цветовая схема':'Color scheme','Плотность':'Density','Светлая':'Light','Кремовая':'Cream','Изумрудная':'Emerald','Тёмная':'Dark','Ультракомпактная':'Ultra compact','Компактная':'Compact','Обычная':'Normal','Подсказки':'Tooltips','Показывать подсказки':'Show tooltips','Подробности':'Details','Скачать':'Download','Наш':'Ours','Нет':'No','Да':'Yes','Ожидание':'Waiting','Подключено':'Connected','Нет связи':'Disconnected','Не назначен':'Unassigned','Сохранено':'Saved','Есть правки':'Unsaved changes','Редактировать':'Edit','Удалить':'Delete','Использовать':'Use','Добавить колесо':'Add wheel','Удалить ось':'Remove axle','Удалить колесо':'Remove wheel','Новая машина':'New vehicle','Нет машин':'No vehicles','Активна':'Active','Ошибка':'Error','Сохранение':'Saving','Сканирование':'Scanning','Пассивный':'Passive','Активный':'Active','Отменить несохранённые правки?':'Discard unsaved edits?','Удалить машину?':'Delete vehicle?','Удалить известный датчик?':'Delete known sensor?','Результат операции':'Operation result','Устройства':'Devices','Резерв':'Reserve','Чужие файлы':'Other files','Версия':'Revision','Журнал':'Journal','Секунды с последнего пакета; измерения сохраняются без срока давности.':'Seconds since the last packet; readings are retained without expiry.'};
let lang=localStorage.getItem('tpms.language')||'ru',theme=localStorage.getItem('tpms.theme')||'cream',density=localStorage.getItem('tpms.density')||'nano',tooltips=localStorage.getItem('tpms.tooltips')==='true';
let page='home',state={},settings={revision:0,active:'',known:[],garage:[]},stateAt=performance.now(),draft=null,dirty=false,busy=false,operation='—',socket,connected=false,researchRows=[],selectedReport=null,requestBusy=false,unique=0;
const T=s=>lang==='ru'?s:(translations[s]||s),fmt=(v,d=0)=>v==null?'—':Number(v).toFixed(d),code=v=>Number(v||0).toString(16).toUpperCase().padStart(6,'0');
const icon=name=>{const i=['car','home'].includes(name)?$('icon-'+name).content.firstElementChild.cloneNode(true):document.createElement('i');i.setAttribute('class','ti ti-'+name+(['car','home'].includes(name)?' icon-svg':''));i.setAttribute('aria-hidden','true');return i;};
function el(tag,text,cls){const e=document.createElement(tag);if(text!=null)e.textContent=text;if(cls)e.className=cls;return e;}
function btn(text,fn,tip){const b=el('button',text);if(['ⓘ','×','✕','+','↓','↑','<','>'].includes(text))b.classList.add('icon-action');b.type='button';b.onclick=fn;if(tip){b.dataset.tooltip=tip;b.setAttribute('aria-label',tip);}return b;}
function opState(text,kind=''){operation=text;for(const b of document.querySelectorAll('[data-operation]')){b.className='operation icon-action '+kind;b.dataset.tooltip=text;b.setAttribute('aria-label',T('Результат операции')+': '+text);}}
function details(title,text){$('detailTitle').textContent=title;$('detailText').textContent=text;if(!$('details').open)$('details').showModal();}
$('closeDetails').onclick=()=>{$('details').close();selectedReport=null;};$('details').addEventListener('close',()=>selectedReport=null);
for(const b of document.querySelectorAll('[data-operation]'))b.onclick=()=>details(T('Результат операции'),operation);
for(const b of document.querySelectorAll('[data-close]'))b.onclick=()=>$(b.dataset.close).close();
function confirmAction(text){return new Promise(resolve=>{$('confirmText').textContent=T(text);$('confirm').showModal();const done=x=>{$('confirm').close();resolve(x);};$('confirmYes').onclick=()=>done(true);$('confirmNo').onclick=()=>done(false);$('confirm').oncancel=()=>resolve(false);});}
const pages=[['home','Главная','home'],['tpms','Давление в шинах','chart-bar'],['tom','TomTom','antenna-bars-5'],['bt','Все устройства BT','broadcast'],['files','Файлы','folder'],['garage','Гараж','car'],['known','Известные датчики','list'],['radio','Настройки','settings']];
function appearance(){
 app.dataset.theme=theme;app.dataset.density=density;app.dataset.tooltips=String(tooltips);
 const themes=[['light','Светлая','sun'],['cream','Кремовая','ice-cream'],['emerald','Изумрудная','trees'],['dark','Тёмная','moon']],densities=[['nano','Ультракомпактная','baseline-density-small'],['mini','Компактная','baseline-density-medium'],['norm','Обычная','baseline-density-large']];
 if(!themes.some(x=>x[0]===theme))theme='cream';if(!densities.some(x=>x[0]===density))density='nano';
 $('appearanceText').textContent=T(themes.find(x=>x[0]===theme)[1])+' · '+density.toUpperCase();const m=$('appearanceMenu');m.replaceChildren();
 for(const [title,items,selected,set] of [['Цветовая схема',themes,theme,v=>theme=v],['Плотность',densities,density,v=>density=v]]){m.append(el('strong',T(title)));for(const [id,name,image] of items){const b=btn('',()=>{set(id);localStorage.setItem('tpms.theme',theme);localStorage.setItem('tpms.density',density);appearance();});b.append(icon(image),document.createTextNode(' '+T(name)+(id===selected?' ✓':'')));b.classList.toggle('selected',id===selected);b.setAttribute('aria-pressed',String(id===selected));m.append(b);}}
 m.append(el('strong',T('Подсказки')));const b=btn('',()=>{tooltips=!tooltips;localStorage.setItem('tpms.tooltips',tooltips);appearance();});b.append(icon(tooltips?'eye':'eye-off'),document.createTextNode(' '+T('Показывать подсказки')));b.setAttribute('role','switch');b.setAttribute('aria-checked',String(tooltips));m.append(b);
}
function localize(){document.documentElement.lang=lang;for(const e of document.querySelectorAll('[aria-label]')){if(!e.dataset.originalAria)e.dataset.originalAria=e.getAttribute('aria-label');e.setAttribute('aria-label',T(e.dataset.originalAria));}for(const o of $('scanMode').options)o.textContent=T(o.value==='active'?'Активный':'Пассивный');for(const e of document.querySelectorAll('[data-i]'))e.textContent=T(e.dataset.i);$('language').textContent=lang.toUpperCase();const nav=$('navigation');nav.replaceChildren();for(const [id,name,image]of pages){const a=el('a');a.href='#'+id;a.append(icon(image),el('span',T(name)));a.onclick=e=>{e.preventDefault();if(performance.now()<suppressMenuClickUntil)return;switchPage(id);};if(id==='garage')a.className='settings-menu';a.setAttribute('aria-label',T(name));a.dataset.tooltip=T(name);nav.append(a);}setPageVisible();appearance();renderSchemeButtons();renderSettings(false);renderState();syncMenu();}
$('language').onclick=()=>{lang=lang==='ru'?'en':'ru';localStorage.setItem('tpms.language',lang);localize();if(page==='tom'||page==='bt')renderResearch();};
const narrow=matchMedia('(max-width:900px)');
let mobileMenuOpen=false,desktopMenuCollapsed=localStorage.getItem('tpms.menu')==='collapsed',gesture=null,suppressMenuClickUntil=0;
function syncMenu(focus=false){
 app.classList.toggle('mobile-menu-open',narrow.matches&&mobileMenuOpen);
 app.classList.toggle('menu-collapsed',!narrow.matches&&desktopMenuCollapsed);
 $('menuToggle').setAttribute('aria-expanded',String(narrow.matches?mobileMenuOpen:!desktopMenuCollapsed));
 $('menuBackdrop').hidden=!narrow.matches||!mobileMenuOpen;
 document.querySelector('.shell-sidebar').inert=narrow.matches&&!mobileMenuOpen;
 document.querySelector('main').inert=narrow.matches&&mobileMenuOpen;
 if(focus&&narrow.matches&&mobileMenuOpen)($('navigation').querySelector('a.active')||$('navigation').querySelector('a'))?.focus();
}
function closeMenu(restore=false){mobileMenuOpen=false;syncMenu();if(restore)$('menuToggle').focus();}
$('menuToggle').onclick=()=>{if(narrow.matches){mobileMenuOpen=!mobileMenuOpen;syncMenu(true);}else{desktopMenuCollapsed=!desktopMenuCollapsed;localStorage.setItem('tpms.menu',desktopMenuCollapsed?'collapsed':'expanded');syncMenu();}};
$('menuBackdrop').onclick=()=>closeMenu(true);
narrow.addEventListener('change',()=>{mobileMenuOpen=false;gesture=null;app.classList.remove('menu-dragging');syncMenu();});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&narrow.matches&&mobileMenuOpen){closeMenu(true);e.preventDefault();}});
document.addEventListener('pointerdown',e=>{
 if(!narrow.matches||e.button!==0)return;
 const aside=document.querySelector('.shell-sidebar'),rect=document.querySelector('.shell-body').getBoundingClientRect();
 if(e.clientY<rect.top)return;
 if(!mobileMenuOpen&&e.clientX>14)return;
 if(mobileMenuOpen&&!aside.contains(e.target))return;
 gesture={id:e.pointerId,x:e.clientX,y:e.clientY,dx:0,open:mobileMenuOpen,active:false};
},{passive:true});
document.addEventListener('pointermove',e=>{
 if(!gesture||e.pointerId!==gesture.id)return;
 const dx=e.clientX-gesture.x,dy=e.clientY-gesture.y;
 if(!gesture.active){if(Math.abs(dy)>16&&Math.abs(dy)>Math.abs(dx)){gesture=null;return;}if(Math.abs(dx)<12||Math.abs(dx)<Math.abs(dy)*1.3)return;gesture.active=true;app.classList.add('menu-dragging');(gesture.open?document.querySelector('.shell-sidebar'):$('menuEdge')).setPointerCapture(e.pointerId);}
 gesture.dx=dx;e.preventDefault();const width=document.querySelector('.shell-sidebar').getBoundingClientRect().width;
 app.style.setProperty('--drawer-x',Math.max(-width,Math.min(0,(gesture.open?0:-width)+dx))+'px');
},{passive:false});
function endMenuGesture(e){if(!gesture||e.pointerId!==gesture.id)return;const g=gesture;gesture=null;app.classList.remove('menu-dragging');app.style.removeProperty('--drawer-x');if(g.active){suppressMenuClickUntil=performance.now()+400;mobileMenuOpen=g.open?g.dx>-50:g.dx>45;syncMenu();e.preventDefault();}}
document.addEventListener('pointerup',endMenuGesture,{passive:false});
document.addEventListener('pointercancel',()=>{gesture=null;app.classList.remove('menu-dragging');app.style.removeProperty('--drawer-x');});
syncMenu();
function setPageVisible(){for(const [id]of pages)$('page-'+id).hidden=id!==page;for(const a of $('navigation').querySelectorAll('a'))a.classList.toggle('active',a.hash==='#'+page);}
async function switchPage(id){if(!pages.some(p=>p[0]===id))id='home';if(page==='garage'&&dirty&&id!=='garage'&&!await confirmAction('Отменить несохранённые правки?'))return;if(page==='garage'&&id!=='garage')dirty=false;page=id;location.hash=id;setPageVisible();if(narrow.matches)closeMenu(false);if(id==='radio')loadWifi();if(id==='files')loadFiles();if(id==='bt'||id==='tom')loadResearch();if(id==='garage')renderSettings(false);if(['home','known','garage','tom'].includes(id))loadTpms();}
window.addEventListener('hashchange',()=>{const id=location.hash.slice(1);if(id!==page)switchPage(id);});
function tableRows(body,items,key,update){const existing=new Map([...body.children].map(r=>[r.dataset.key,r]));for(let i=0;i<items.length;i++){const item=items[i],k=String(key(item,i));let tr=existing.get(k);if(!tr){tr=document.createElement('tr');tr.dataset.key=k;}existing.delete(k);update(tr,item,i);if(body.children[i]!==tr)body.insertBefore(tr,body.children[i]||null);}for(const r of existing.values())r.remove();}
function cell(tr,i,text,cls){while(tr.children.length<=i)tr.append(el('td'));const td=tr.children[i];if(text!=null&&td.textContent!==String(text))td.textContent=text;if(cls)td.className=cls;return td;}
function age(last){return Math.max(0,Math.floor((Number(state.uptimeMs||0)+performance.now()-stateAt-Number(last||0))/1000));}
let wifiConfig={profiles:[]},wifiEditing=0;
Object.assign(translations,{'Сети Wi-Fi':'Wi-Fi networks','Сеть Wi-Fi':'Wi-Fi network','Пароль':'Password','Заменить пароль':'Replace password','Включён':'Enabled','Подключить':'Connect','Сохранено в NVS':'Saved to NVS','Операция принята':'Operation accepted','Слишком длинное имя сети':'SSID exceeds 32 bytes'});
async function loadWifi(report=false){if(state.wifi?.webEnabled===false)return;try{wifiConfig=await api('/api/wifi/config');renderWifi();}catch(e){if(report)opState(e.message,'error');}}
function renderWifi(){
 tableRows($('wifiRows'),wifiConfig.profiles||[],p=>p.id,(tr,p)=>{
  cell(tr,0,p.ssid);cell(tr,1,p.priority,'number');cell(tr,2,p.enabled?'✓':'—');
  const edit=btn('✎',()=>editWifi(p),T('Редактировать'));edit.className='icon-action';
  const connect=btn(T('Подключить'),()=>wifiPerform({op:'connect',id:p.id}));
  const remove=btn('×',async()=>{if(await confirmAction(T('Удалить')+' '+p.ssid+'?'))wifiPerform({op:'remove',id:p.id});},T('Удалить'));remove.className='icon-action';
  cell(tr,3).replaceChildren(edit,connect,remove);
 });
 if(!radioDirty){$('wifiBoot').checked=wifiConfig.startupEnabled??true;$('wifiWindow').value=wifiConfig.startupWindowSeconds??0;}
}
async function wifiPerform(command){
 try{
  const accepted=await api('/api/wifi/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(command)});
  if(!accepted.accepted)throw Error(accepted.error||'wifi_command_failed');
  opState(T('Операция принята'),'pending');
  for(let i=0;i<40;i++){
   await new Promise(r=>setTimeout(r,150));
   const result=await api('/api/wifi/operation?id='+accepted.id);
   if(result.state==='failed')throw Error(result.error||'wifi_command_failed');
   if(result.state==='done'){opState('✓','ok');await loadWifi();loadState();return true;}
  }
  throw Error('wifi_operation_timeout');
 }catch(e){opState(e.message,'error');return false;}
}
function editWifi(profile){
 wifiEditing=profile?.id||0;$('wifiSsid').value=profile?.ssid||'';$('wifiPassword').value='';$('wifiPriority').value=profile?.priority??0;$('wifiProfileEnabled').checked=profile?.enabled??true;
 $('wifiReplacePassword').checked=!profile;$('wifiReplacePassword').disabled=!profile;$('wifiPassword').disabled=!!profile;
 $('wifiEditor').showModal();
}
$('wifiReplacePassword').onchange=()=>{$('wifiPassword').disabled=!$('wifiReplacePassword').checked;if($('wifiPassword').disabled)$('wifiPassword').value='';};
$('addWifi').onclick=()=>editWifi(null);$('refreshWifi').onclick=()=>loadWifi(true);
$('startAp').onclick=()=>wifiPerform({op:'ap.start',seconds:300});$('stopAp').onclick=()=>wifiPerform({op:'ap.stop'});
$('wifiForm').onsubmit=async e=>{
 e.preventDefault();if(!$('wifiForm').reportValidity())return;
 if(new TextEncoder().encode($('wifiSsid').value).length>32){$('wifiSsid').classList.add('invalid');opState(T('Слишком длинное имя сети'),'error');return;}
 const command={op:'put',id:wifiEditing,ssid:$('wifiSsid').value,priority:Number($('wifiPriority').value),enabled:$('wifiProfileEnabled').checked};
 if($('wifiReplacePassword').checked)command.password=$('wifiPassword').value;
 $('saveWifi').disabled=true;
 try{if(await wifiPerform(command)){$('wifiEditor').close();$('wifiPassword').value='';}}finally{$('saveWifi').disabled=false;}
};
$('wifiEditor').addEventListener('close',()=>{$('wifiPassword').value='';});

let radioDirty=false, radioPending=false;
Object.assign(translations,{'Сеанс, с (0=∞)':'Session, s (0=∞)','Настройки':'Settings','Сейчас':'Now','При старте':'At startup','Длительность Wi-Fi, с':'Wi-Fi duration, s','Wi-Fi сейчас':'Wi-Fi now','Wi-Fi при старте':'Wi-Fi at startup','Выключен':'Off','Интервал, мс':'Interval, ms','Окно приёма, мс':'Scan window, ms','Интервал BLE, мс':'BLE interval, ms','Окно BLE, мс':'BLE window, ms','BLE работает постоянно':'BLE runs continuously','Экран ESP32':'ESP32 display','Сайт':'Website','Некорректные параметры радио':'Invalid radio parameters'});
function renderRadio(){
 const w=state.wifi,b=state.ble;
 if(!radioPending)$('wifiEnabled').checked=!!w?.enabled;
 $('wifiEnabled').disabled=!w||radioPending;
 $('wifiStateText').textContent='Wi-Fi '+(w?.enabled?(w.connected?'✓ ':'… ')+(w.remainingSeconds<0?'∞':w.remainingSeconds+' s'):T('Выключен'));
 $('apStateText').textContent=w?.apActive?'AP '+(w.apSsid||'')+' · http://'+(w.apIp||'192.168.4.1')+':80/':'AP '+T('Выключен');
 $('stopAp').disabled=!w?.apActive;
 $('startAp').disabled=w?.apSupported===false;
 $('addWifi').disabled=w?.webEnabled===false;
 $('refreshWifi').disabled=w?.webEnabled===false;
 $('bleStateText').textContent=b?.state||'—';
 $('bleTiming').textContent=b?.intervalMs?b.windowMs+' / '+b.intervalMs+' ms · '+Math.round(b.windowMs*100/b.intervalMs)+'%'+(b.pending?' …':''):'—';
 for(const [host,selected] of [['nativeThemes',settings.nativeTheme||'cream'],['siteThemes',theme]])for(const button of $(host).children)button.setAttribute('aria-pressed',String(button.dataset.scheme===selected));
}
function renderSchemeButtons(){
 for(const host of ['nativeThemes','siteThemes']){
   const target=$(host);target.replaceChildren();
   for(const [id,title] of [['light','Светлая'],['cream','Кремовая'],['emerald','Изумрудная'],['dark','Тёмная'],...(host==='nativeThemes'?[['night','Ночная']]:[])]){
     const b=btn(T(title),async()=>{if(host==='nativeThemes')await perform({op:'appearance.put',theme:id});else{theme=id;localStorage.setItem('tpms.theme',theme);appearance();renderRadio();}});
     b.dataset.scheme=id;target.append(b);
   }
 }
 renderRadio();
}
for(const id of ['wifiWindow','bleWindow','bleInterval','wifiBoot'])$(id).oninput=()=>{radioDirty=true;$(id).classList.remove('invalid');};
$('wifiEnabled').onchange=async e=>{
 const enabled=e.target.checked;radioPending=true;renderRadio();
 try{await api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled,seconds:wifiConfig.startupWindowSeconds??0})});opState(enabled?'✓':T('Выключен'),'ok');}
 catch(error){opState(error.message,'error');}
 finally{setTimeout(()=>{radioPending=false;loadState();},2500);}
};
$('saveRadio').onclick=async()=>{
 for(const id of ['wifiWindow','bleWindow','bleInterval'])if(!$(id).validity.valid || $(id).value===''){$(id).classList.add('invalid');opState(T('Некорректные параметры радио'),'error');return;}
 if(Number($('bleWindow').value)>Number($('bleInterval').value)){$('bleWindow').classList.add('invalid');opState(T('Некорректные параметры радио'),'error');return;}
 if(!await wifiPerform({op:'startup',enabled:$('wifiBoot').checked,seconds:Number($('wifiWindow').value)}))return;
 await perform({op:'radio.put',wifiEnabled:settings.wifiEnabled??true,wifiWindowSeconds:settings.wifiWindowSeconds??0,bleIntervalMs:Number($('bleInterval').value),bleWindowMs:Number($('bleWindow').value)},()=>radioDirty=false);
};
function renderState(){
 renderRadio();
 renderHomeVehicle();
 $('ip').textContent='IP '+(state.ip||'—');$('connection').textContent=T(connected?'Подключено':'Нет связи');$('connection').className=connected?'ok':'error';
 const car=settings.garage.find(c=>c.id===settings.active);$('vehicleTitle').textContent=car?.name||T('Нет машин');$('btStatus').textContent=(state.scan||'—')+' · '+(state.scanMode||'—');
 $('receiverCounters').textContent=`RX ${state.reports||0} / drop ${state.dropped||0} · ${T('Версия')} ${state.settingsRevision??0} · FS ${state.storage?.mounted?'✓':'×'}`;
 tableRows($('wheelRows'),state.sensors||[],(_,i)=>i,(tr,s)=>{cell(tr,0,s.position||'—');cell(tr,1,s.identity||(s.id?code(s.id):'—'),'mono');cell(tr,2,fmt(s.pressure_Bar,2),'number');cell(tr,3,fmt(s.temperatureC,1),'number');const a=cell(tr,4,s.present?age(s.lastSeenMs):'—','number age');a.dataset.last=s.present?s.lastSeenMs:'';cell(tr,5,fmt(s.batteryPercentage),'number');cell(tr,6,s.present?String(s.alarmFlag):'—','number');});
 tableRows($('taskRows'),[...(state.diagnostic?.tasks||[])].sort((a,b)=>b.rp-a.rp),t=>t.tn,(tr,t)=>{cell(tr,0,t.name);cell(tr,1,t.tn,'number');cell(tr,2,t.s,'mono');cell(tr,3,t.p,'number');cell(tr,4,t.sf,'number');cell(tr,5,fmt(t.rp,1),'number');});
 $('scanMode').value=state.scanMode||'passive';
}
function acceptState(s){if(s.msgType!=='snapshot'||!s.ready)return;state=s;stateAt=performance.now();renderState();if(settings.revision!==s.settingsRevision)loadSettings(false);}
async function api(path,options){const r=await fetch(path,{cache:'no-store',...options});let v;try{v=await r.json();}catch{throw Error('HTTP '+r.status);}if(!r.ok)throw Error(v.error||('HTTP '+r.status));return v;}
async function loadState(){try{acceptState(await api('/api/state'));connected=true;}catch(e){connected=false;}renderState();}
function connect(){socket=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/ws');socket.onopen=()=>{connected=true;renderState();};socket.onmessage=e=>{try{const s=JSON.parse(e.data);if(s.msgType==='refresh')loadState();else acceptState(s);}catch{}};socket.onclose=()=>{connected=false;renderState();setTimeout(connect,2000);};socket.onerror=()=>socket.close();}
async function loadSettings(force){try{const next=await api('/api/settings');settings=next;renderSettings(force);}catch(e){opState(e.message,'error');}}
async function perform(command,onSuccess){if(busy)return false;busy=true;opState(T('Сохранение'));for(const b of document.querySelectorAll('#saveVehicle,#newVehicle'))b.disabled=true;
 try{const queued=await api('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({...command,revision:settings.revision})});let result;
  for(let i=0;i<120;i++){await new Promise(r=>setTimeout(r,250));result=await api('/api/operation?id='+queued.id);if(result.done)break;}if(!result?.done)throw Error('operation_timeout');if(!result.ok)throw Error(result.error);
  opState(T('Сохранено'),'ok');if(onSuccess)onSuccess();await loadSettings(false);await loadState();return true;
 }catch(e){opState(T('Ошибка')+': '+e.message,'error');return false;}finally{busy=false;for(const b of document.querySelectorAll('#saveVehicle,#newVehicle'))b.disabled=false;}}
for(const id of ['activeVehicle','homeActiveVehicle'])$(id).onchange=e=>perform({op:'vehicle.select',id:e.target.value});
function renderSettings(force){
 if(!radioDirty || force){$('wifiWindow').value=wifiConfig.startupWindowSeconds??settings.wifiWindowSeconds??0;$('bleWindow').value=settings.bleWindowMs??80;$('bleInterval').value=settings.bleIntervalMs??80;$('wifiBoot').checked=wifiConfig.startupEnabled??settings.wifiEnabled??true;radioDirty=false;}
 for(const id of ['activeVehicle','homeActiveVehicle']){const active=$(id);active.replaceChildren();for(const c of settings.garage){const o=el('option',c.name);o.value=c.id;active.append(o);}active.value=settings.active;active.disabled=!settings.garage.length;}
 tableRows($('garageRows'),settings.garage,c=>c.id,(tr,c)=>{const td=cell(tr,0);td.replaceChildren(btn(c.name,async()=>{if(dirty&&!await confirmAction('Отменить несохранённые правки?'))return;draft=structuredClone(c);dirty=false;renderAssignments();}));cell(tr,1,c.axles.length+' / '+c.axles.reduce((n,a)=>n+a.wheels.length,0),'number');const star=btn(settings.active===c.id?'★':'☆',()=>makeDefaultVehicle(c.id));star.className='icon-action';star.setAttribute('aria-label',T('Показывать при включении')+' '+c.name);star.setAttribute('aria-pressed',String(settings.active===c.id));cell(tr,2).replaceChildren(star);tr.classList.toggle('selected-row',draft?.id===c.id);});
 if(!draft||force){draft=structuredClone(settings.garage.find(c=>c.id===settings.active)||settings.garage[0]||null);dirty=false;renderAssignments();}else if(!dirty){const c=settings.garage.find(c=>c.id===draft.id);if(c)draft=structuredClone(c);renderAssignments();}
 renderKnownDevices();renderSensorPalettes();renderVehicleCanvas();renderState();
}
async function makeDefaultVehicle(id){if(draft?.id===id&&dirty){const value=structuredClone(draft);await perform({op:'vehicle.put',vehicle:value,makeDefault:true},()=>dirty=false);}else if(settings.active!==id)await perform({op:'vehicle.select',id});}
function markDirty(){dirty=true;$('editState').textContent=T('Есть правки');$('editState').className='dirty';}
function input(value,change,label){const i=el('input');i.value=value;i.maxLength=32;i.setAttribute('aria-label',label);i.oninput=()=>{change(i.value);markDirty();};return i;}
function renderAssignments(){renderVehicleCanvas();const body=$('assignmentRows');body.replaceChildren();$('vehicleName').value=draft?.name||'';$('vehicleName').disabled=!draft;$('editState').textContent=T(dirty?'Есть правки':'Сохранено');$('editState').className=dirty?'dirty':'muted';if(!draft)return;
 for(const [ai,a]of draft.axles.entries())for(const [wi,w]of a.wheels.entries()){const tr=el('tr'),axis=el('td'),wheel=el('td'),assign=el('td'),actions=el('td');if(wi===0){const controls=el('div',null,'cell-actions');axis.append(controls);controls.append(input(a.name,v=>a.name=v,T('Ось')),btn('+',()=>{if(a.wheels.length>=8)return; a.wheels.push({name:T('Колесо')+' '+(a.wheels.length+1),sensor:''});markDirty();renderAssignments();},T('Добавить колесо')),btn('×',()=>{if(draft.axles.length<=1)return;draft.axles.splice(ai,1);markDirty();renderAssignments();},T('Удалить ось')));}wheel.append(input(w.name,v=>w.name=v,T('Колесо')));const select=el('select');select.setAttribute('aria-label',a.name+' / '+w.name);let empty=el('option',T('Не назначен'));empty.value='';select.append(empty);for(const k of settings.known){const o=el('option',(k.protocolId||k.identity||k.id)+' · '+k.transport);o.value=k.id;select.append(o);}select.value=w.sensor;select.onchange=()=>{if(select.value)assignInstrument(select.value,ai,wi);else{w.sensor='';markDirty();renderAssignments();}};assign.append(select);actions.append(btn('×',()=>{if(a.wheels.length<=1)return;a.wheels.splice(wi,1);markDirty();renderAssignments();},T('Удалить колесо')));tr.append(axis,wheel,assign,actions);body.append(tr);}
}
$('vehicleName').oninput=e=>{if(draft){draft.name=e.target.value;markDirty();}};
$('newVehicle').onclick=async()=>{if(dirty&&!await confirmAction('Отменить несохранённые правки?'))return;draft={id:'vehicle-'+Date.now().toString(36)+'-'+(++unique),name:T('Новая машина'),axles:[{name:T('Ось')+' 1',wheels:[{name:T('Колесо')+' 1',sensor:''}]}]};dirty=true;renderAssignments();};
$('addAxle').onclick=()=>{if(!draft||draft.axles.length>=16)return;draft.axles.push({name:T('Ось')+' '+(draft.axles.length+1),wheels:[{name:T('Колесо')+' 1',sensor:''}]});markDirty();renderAssignments();};
$('saveVehicle').onclick=()=>{if(!draft)return;const value=structuredClone(draft);perform({op:'vehicle.put',vehicle:value},()=>{dirty=false;});};
$('discardVehicle').onclick=async()=>{if(!dirty||await confirmAction('Отменить несохранённые правки?')){draft=null;renderSettings(true);}};
$('removeVehicle').onclick=async()=>{if(draft&&await confirmAction('Удалить машину?'))perform({op:'vehicle.remove',id:draft.id},()=>{draft=null;dirty=false;});};
async function loadResearch(){if(requestBusy||!['tom','bt'].includes(page))return;requestBusy=true;try{const result=await api('/api/research?scope='+page);researchRows=result.rows||[];$(page+'Count').textContent=researchRows.length+' / 32 · '+(result.evicted||0)+' '+T('Вытеснено');renderResearch();if(selectedReport&&$('details').open){const {scope,id}=selectedReport;try{$('detailText').textContent=JSON.stringify(await api('/api/research?scope='+scope+'&detail='+encodeURIComponent(id)),null,2);}catch{}}}catch(e){opState(e.message,'error');}finally{requestBusy=false;}}
async function showReport(row){selectedReport={scope:page,id:row.id};try{details(row.address,JSON.stringify(await api('/api/research?scope='+page+'&detail='+encodeURIComponent(row.id)),null,2));}catch(e){opState(e.message,'error');}}
function renderResearch(){const tom=page==='tom';const body=$(tom?'tomRows':'btRows');tableRows(body,researchRows,r=>r.id,(tr,r)=>{cell(tr,0,r.address+' / '+r.address_type+' / '+r.sid,'mono');if(tom){cell(tr,1,r.tpms?.sticker||'—','mono');cell(tr,2,r.rssi,'number');cell(tr,3,r.packets,'number');const a=cell(tr,4,age(r.last_seen_ms),'number age');a.dataset.last=r.last_seen_ms;cell(tr,5,r.tpms?'TomTom':r.status==='complete'?'signature / length':r.status);const known=settings.known.find(k=>k.transport==='ble'&&k.protocol==='tomtom'&&k.code===r.tpms?.code);cell(tr,6,known?(known.protocolId||known.identity):'—');const td=cell(tr,7);if(!td.children.length){td.append(btn('ⓘ',()=>showReport(r),T('Подробности')),btn(T('Наш'),()=>{},T('Добавить')));}td.children[0].onclick=()=>showReport(r);td.children[1].textContent=T('Наш');td.children[1].disabled=!!known||!r.tpms;td.children[1].onclick=()=>recognizeResearch(r);}
 else{cell(tr,1,r.name||'—');cell(tr,2,r.classification?.type||'unknown');cell(tr,3,r.rssi,'number');cell(tr,4,r.packets,'number');const a=cell(tr,5,age(r.last_seen_ms),'number age');a.dataset.last=r.last_seen_ms;const td=cell(tr,6);if(!td.children.length)td.append(btn('ⓘ',()=>{},T('Подробности')));td.firstChild.onclick=()=>showReport(r);}});}
$('scanMode').onchange=async e=>{try{await api('/api/scan',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode:e.target.value})});opState(T('Сканирование'),'ok');}catch(e){opState(e.message,'error');}};
async function loadFiles(){try{const fs=await api('/api/files');$('fsUsage').textContent=fmt(fs.used/1024)+' / '+fmt(fs.total/1024)+' KiB';tableRows($('fileRows'),fs.files||[],f=>f.name,(tr,f)=>{cell(tr,0,f.name);cell(tr,1,f.bytes,'number');cell(tr,2,f.owner);const td=cell(tr,3);td.replaceChildren(btn('ⓘ',()=>readFile(f,true),T('Подробности')),btn('↓',()=>readFile(f,false),T('Скачать')));});}catch(e){opState(e.message,'error');}}
async function readFile(f,preview){try{const chunks=[];let offset=0,total=f.bytes;do{const part=await api('/api/file?name='+encodeURIComponent(f.name)+'&offset='+offset);const bytes=Uint8Array.from(part.hex.match(/../g)||[],x=>parseInt(x,16));chunks.push(bytes);offset+=bytes.length;total=part.total;if(!bytes.length)break;}while(offset<total&&offset<4194304);const blob=new Blob(chunks);if(preview){const text=await blob.text();details(f.name, f.name.startsWith('tpms.cfg.')?'TPMSCFG1 · '+T('Версия')+' / '+f.bytes+' bytes\n\n'+new TextDecoder().decode((new Uint8Array(await blob.arrayBuffer())).slice(32)):text);}else{const a=el('a');a.href=URL.createObjectURL(blob);a.download=f.name.replaceAll('/','_');a.click();setTimeout(()=>URL.revokeObjectURL(a.href),5000);}}catch(e){opState(e.message,'error');}}
$('refreshFiles').onclick=loadFiles;$('receiverInfo').onclick=()=>details(T('Приёмник'),JSON.stringify(state,null,2));
const help={tpms:['Секунды с последнего пакета; измерения сохраняются без срока давности.','Seconds since the last packet; readings are retained without expiry.'],research:['До 32 наблюдений на список. TomTom хранится отдельно от общего скана. ⓘ открывает все принятые данные: AD, raw hex, radio, классификацию и поля TPMS. Отсутствие устройства в списке не доказывает отсутствие радиопередачи. Active запрашивает Scan Response; это не соединение GATT.','Up to 32 observations per list. TomTom uses a separate collector. Details expose all received AD, raw hex, radio, classification and TPMS fields. A missing row does not prove radio silence. Active requests Scan Response without GATT.'],files:['Общая FS 4 МиБ. Браузер только читает и скачивает. tpms.* принадлежит TPMS; чужие файлы не изменяются. Настройки: два проверяемых слота. Диагностика поездки: tpms.diag.0 и .1, по 128 КиБ, запись до 6 часов после включения. Общий резерв 1 МиБ.','Shared 4 MiB FS. Read and download only. TPMS owns tpms.*; other files are preserved. Settings use two verified slots. Trip diagnostics: tpms.diag.0 and .1, 128 KiB each, for up to six hours after boot. Shared reserve: 1 MiB.'],garage:['Сначала добавьте найденный прибор в известные, затем назначьте его колесу. У машины можно менять оси и число колёс на каждой оси. Один датчик нельзя назначить двум колёсам одной машины. Выбор активной машины — на странице TPMS. Сохранение транзакционное, конфликты версий отклоняются.','Add a discovered instrument to known sensors, then assign it to a wheel. Edit axles and each axle’s wheel count. A sensor can be assigned once within a vehicle. Select the active vehicle on TPMS. Saves are transactional; revision conflicts are rejected.']};
for(const b of document.querySelectorAll('[data-help]'))b.onclick=()=>details(T('Подробности'),help[b.dataset.help][lang==='ru'?0:1]);
window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
setInterval(()=>{let unix=Number(state.unixSeconds||0)+(performance.now()-stateAt)/1000;$('clock').textContent=unix>1700000000?new Date(unix*1000).toLocaleTimeString(lang==='ru'?'ru-RU':'en-GB',{hour12:false,timeZone:'Asia/Yekaterinburg'}):'--:--:--';for(const e of document.querySelectorAll('.age[data-last]'))e.textContent=e.dataset.last===''?'—':age(e.dataset.last);if(!connected)loadState();if(page==='tom'||page==='bt')loadResearch();if(['home','known','garage','tom'].includes(page))loadTpms();},1000);
loadWifi();localize();loadTpms();page=pages.some(x=>x[0]===location.hash.slice(1))?location.hash.slice(1):(location.pathname==='/setup'?'radio':'home');setPageVisible();loadSettings(true);loadState();connect();if(page==='files')loadFiles();
