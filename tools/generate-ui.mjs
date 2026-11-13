// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
import {readFile,writeFile} from 'node:fs/promises';
import {encodeProject,decodeProject} from '../managed_components/nacboruiforlvgl/packages/project-codec/index.js';
import {compileNativeAppearance} from './native-appearance.mjs';
const old=JSON.parse(await readFile(new URL('../src/ui/project.json',import.meta.url),'utf8'));
const bg='#101e28',panel='#1b303d',line='#385563',fg='#e8f0f2',muted='#9eb4c1';
const bind=(source,path=[],format={unknown:'--'})=>[{method:'text',value:{source,path},format}];
function label(id,text,x,y,w,h,source,path=[],font=14,format={unknown:'--'}){return {id,type:'label',props:{text,long_mode:0},style:{x,y,width:w,height:h,text_font:`montserrat_${font}`,text_color:font===28?'#4de0ad':fg,...(['bar','rssi','packets','age','temperature'].includes(path.at(-1))||/-head-(rssi|packets|age)$/.test(id)?{text_align:'LV_TEXT_ALIGN_RIGHT'}:{})},children:[],...(source?{bindings:bind(source,path,format)}:{})};}
function obj(id,x,y,w,h,children,extra={}){return {id,type:'obj',props:{},flags:{SCROLLABLE:false},style:{x,y,width:w,height:h,bg_color:panel,bg_opa:255,border_width:0,pad_all:0,radius:0},children,...extra};}
function button(id,text,x,y,w,h,action,args=0){return {id,type:'button',props:{text},style:{x,y,width:w,height:h,bg_color:panel,bg_opa:['-','<','>','+','×','ⓘ','✕'].includes(text)?0:255,border_color:line,border_width:['-','<','>','+','×','ⓘ','✕'].includes(text)?0:1,radius:0,pad_all:2,text_color:fg,text_font:'montserrat_14'},events:[{event:'clicked',action,args:{literal:args}}],children:[]};}
// LVGL 9.6 CLIP keeps fixed fields from wrapping as values gain digits.
// Explicit newlines in the wheel title and note remain intentional lines.
function fixedText(node){if(node.root)fixedText(node.root);if(node.type==='label')node.props.long_mode=4;for(const child of node.children||[])fixedText(child);return node;}
function screen(id,w,h,children){const root=obj(id+'-root',0,0,w,h,children);Object.assign(root.style,{bg_color:bg,text_color:fg,text_font:'montserrat_14'});return {format:'lvgl-screen-draft-1',id,name:id,size:{width:w,height:h},theme:'none',styles:{},root};}
function tpms(compact=false){const w=compact?320:800,h=compact?122:408,children=[];
 for(let i=0;i<4;i++){const x=compact?(i%2)*160:12+(i%2)*242,y=compact?Math.floor(i/2)*61:12+Math.floor(i/2)*174,tw=compact?158:230,th=compact?60:160;
  children.push(obj('wheel-'+i,x,y,tw,th,[label('wheel-name-'+i,'--',5,2,tw-10,compact?15:38,`wheel.${i+1}`,[],14),label('pressure-'+i,'--',5,compact?18:56,compact?100:148,compact?25:40,`tpms.${i+1}`,['bar'],compact?20:28,{precision:2,unknown:'--'}),label('pressure-unit-'+i,'bar',compact?112:163,compact?25:68,compact?40:60,20),label('temp-'+i,'-- C',5,compact?43:112,compact?78:105,20,`tpms.${i+1}`,['temperature'],14,{precision:1,suffix:' C',unknown:'-- C'}),label('age-'+i,'-- s',compact?85:114,compact?43:112,compact?68:110,20,`tpms.${i+1}`,['age'],14,{precision:0,suffix:' s',unknown:'-- s'})],{style:{x,y,width:tw,height:th,pad_all:0,bg_color:panel,bg_opa:255,border_width:1,border_color:line,radius:compact?0:8}}));
 }
 if(!compact){children.push(label('vehicle','--',514,12,274,54,'receiver',['vehicle'],20),label('ble','--',514,80,274,30,'receiver',['bluetooth']),label('reports','--',514,112,274,30,'receiver',['reports']),label('page','--',514,162,274,30,'receiver',['page']),button('previous','Previous wheels',514,210,130,36,'wheels.page',-1),button('next','Next wheels',652,210,136,36,'wheels.page',1),label('note','Last readings are retained.\nSeconds since last packet.',514,268,274,70));}
 if(compact)children.push(button('wheel-page-next','>',294,0,26,18,'wheels.page',1));
 return fixedText(screen(compact?'tpms-small':'tpms',w,h,children));
}
function scan(id,small=false){const w=small?320:800,h=small?122:408,n=small?2:6,rowh=small?22:28,head=small?0:22,children=[];
 if(!small)children.push(label(id+'-head-address','Address / type / SID',8,0,268,22),label(id+'-head-rssi','RSSI',280,0,70,22),label(id+'-head-packets','packets',360,0,90,22),label(id+'-head-age','age',460,0,90,22),label(id+'-head-kind','classification',560,0,236,22));
 for(let i=0;i<n;i++){let b=button(id+'-row-'+i,'',0,head+i*rowh,w,rowh,id+'.select',0);b.events[0].args={source:id+'.rows',path:[i,'id']};b.children=small?[label(id+'-addr-'+i,'--',4,1,165,rowh-2,id+'.rows',[i,'address']),label(id+'-rssi-'+i,'--',172,1,60,rowh-2,id+'.rows',[i,'rssi']),label(id+'-age-'+i,'--',238,1,78,rowh-2,id+'.rows',[i,'age'])]:[label(id+'-addr-'+i,'--',4,1,272,rowh-2,id+'.rows',[i,'address']),label(id+'-rssi-'+i,'--',280,1,70,rowh-2,id+'.rows',[i,'rssi']),label(id+'-count-'+i,'--',360,1,90,rowh-2,id+'.rows',[i,'packets']),label(id+'-age-'+i,'--',460,1,90,rowh-2,id+'.rows',[i,'age']),label(id+'-kind-'+i,'--',560,1,236,rowh-2,id+'.rows',[i,'kind'])];children.push(b);}
 const y=head+n*rowh;children.push(button(id+'-prev','<',0,y,40,small?24:28,id+'.page',-1),button(id+'-next','>',42,y,40,small?24:28,id+'.page',1),label(id+'-status','--',90,y+2,small?142:530,24,id+'.status'),button(id+'-raw-prev',small?'d<':'Details <',small?240:640,y,small?38:76,small?24:28,id+'.detailpage',-1),button(id+'-raw-next',small?'d>':'Details >',small?280:720,y,small?38:76,small?24:28,id+'.detailpage',1));
 if(!small){children.push(obj(id+'-details',4,y+32,w-8,h-y-36,[label(id+'-raw','Select an advertisement',4,2,w-20,2400,id+'.detail',[],14)],{flags:{SCROLLABLE:true}}));}
 else children.push(obj(id+'-details',0,70,w,52,[label(id+'-raw','Select a row',2,0,w-8,2200,id+'.detail')],{flags:{SCROLLABLE:true}}));
 return screen(id+(small?'-small':''),w,h,children);
}
function radio(small=false){
 const w=small?320:800,h=small?122:408,items=[label('wifi-state','--',8,small?0:8,w-16,small?18:24,'wifi'),button('wifi-open','Wi-Fi 5 min',8,small?20:74,small?148:200,small?24:36,'wifi.open',300),button('wifi-close','Wi-Fi off',small?164:228,small?20:74,small?148:200,small?24:36,'wifi.close',0)];
 if(!small)items.push(label('ble-timing','--',8,36,w-16,24,'ble.control'),label('scheme-title','Цветовая схема',8,126,w-16,24),label('scheme-current','--',8,210,w-16,28,'ui.scheme',['title'],20));
 for(const [i,[id,title]] of [['light','Светлая'],['cream','Кремовая'],['emerald','Изумрудная'],['dark','Тёмная'],['night','Ночная']].entries()){
   const b=button('scheme-'+id,title,small?8+(i%2)*156:8+i*157,small?50+Math.floor(i/2)*28:162,small?148:149,small?24:36,'ui.select_scheme',id);
   b.bindings=[{scope:'common',method:'state',value:[{literal:4},{op:'eq',args:[{source:'ui.scheme',path:['id']},{literal:id}]}]}];items.push(b);
 }
 for(const [i,[key,title]] of [['interval','Интервал, мс'],['window','Окно, мс']].entries()){
  const y=small?140+i*32:260+i*46;
  items.push(label(key+'-label',title,8,y,small?96:170,24),button(key+'-minus','-',small?110:190,y,32,28,'ble.'+key,-5),label(key+'-value','--',small?150:240,y+2,64,24,'ble.'+key),button(key+'-plus','+',small?222:314,y,32,28,'ble.'+key,5));
 }
 const result=fixedText(screen(small?'radio-small':'radio',w,h,items));if(small)result.root.flags.SCROLLABLE=true;return result;
}
function shell(small=false){const w=small?320:800,h=small?170:480,top=small?20:28,bottom=small?28:44,ids=small?['tpms-small','tom-small','bt-small','radio-small']:['tpms','tom','bt','radio'];
 let content=obj('content',0,top,w,h-top-bottom,[],{view:{pages:ids.map(screen=>({screen,retention:'cache'})),initial:ids[0],sizing:'design'}});
 const nav=ids.map((id,i)=>{const b=button('nav-'+i,['TPMS','TomTom','BT scan','Settings'][i],Math.floor(i*w/4),0,Math.floor((i+1)*w/4)-Math.floor(i*w/4),bottom,'');b.events=[{event:'clicked',navigate:{view:'content',screen:id}}];b.styleRules=[{part:'MAIN',state:['CHECKED'],props:{bg_color:line,border_color:'#4de0ad',text_color:'#4de0ad'}}];return b;});
 return screen(small?'shell-small':'shell',w,h,[fixedText(obj('status',0,0,w,top,[label('clock','--:--:--',4,1,small?78:150,top-2,'receiver',['clock']),label('ip','IP --',small?87:300,1,small?229:490,top-2,'receiver',['network'])])),content,obj('menu',0,h-bottom,w,bottom,nav)]);
}
const p={format:old.format,manifest:{id:'tpms-receiver',name:'TPMS receiver',entry:'shell',description:'Application CBOR pages: TPMS, TomTom advertising and full Bluetooth research'},styles:old.styles,screens:{}};
for(const s of [tpms(),scan('tom'),scan('bt'),radio(),shell(),tpms(true),scan('tom',true),scan('bt',true),radio(true),shell(true)])p.screens[s.id]=s;
const ref=(role,state=[])=>({ref:'global-static:ui.'+role,part:'MAIN',state});
function appearanceRoles(node,root=false,inStatus=false){
 if(node.root){appearanceRoles(node.root,true);return;}
 const status=inStatus||node.id==='status';
 for(const key of ['bg_color','text_color','border_color'])delete node.style[key];
 if(node.type==='button'){
   const nav=node.id.startsWith('nav-');
   const row=/^(tom|bt)-row-/.test(node.id);
   node.styles=[ref(row?'panel':'button'),ref('button_pressed',['PRESSED']),ref('button_disabled',['DISABLED']),ref('button_pressed',['CHECKED']),ref('focus_outline',['FOCUS_KEY'])];
   if(row){node.style.border_side='LV_BORDER_SIDE_BOTTOM';node.style.shadow_width=0;}
   if(node.style.bg_opa===0)node.styles=[ref('text'),ref('focus_outline',['FOCUS_KEY'])];
   if(nav){node.styles=node.styles.filter(item=>!item.state.includes('FOCUSED'));delete node.styleRules;node.style.x+=3;node.style.width-=6;node.style.y=-9;node.style.height+=6;node.style.radius=9;node.style.border_width=1;node.style.shadow_width=0;}
 }else node.styles=[ref(status?'status':(root||node.id==='menu'?'canvas':(node.type==='label'?(/^pressure-\d+$/.test(node.id)?'indicator':/^(pressure-unit|temp|age)-/.test(node.id)?'muted':'text'):'panel')))];
 for(const child of node.children||[])appearanceRoles(child,false,status);
}
for(const screen of Object.values(p.screens))appearanceRoles(screen);
await writeFile(new URL('../src/ui/appearance.cbor',import.meta.url),await compileNativeAppearance());
const data=encodeProject(p);decodeProject(data);
await writeFile(new URL('../src/ui/project.json',import.meta.url),JSON.stringify(p,null,2)+'\n');await writeFile(new URL('../src/ui/project.cbor',import.meta.url),data);console.log(`${Object.keys(p.screens).length} screens; ${data.length} CBOR bytes`);
