// SPDX-FileCopyrightText: 2026 Oleg Kozhevnikov
// SPDX-License-Identifier: MIT
import {readFile} from 'node:fs/promises';
import {createProject,encodeProject} from '../../NaCBORUIforLVGL/packages/project-codec/index.js';
import {roleStyles} from '../../NaCBORUIforLVGL/examples/windows_player/schemes.mjs';

// Product policy on top of the shared Appearance roles; the public demo is unchanged.
export async function compileNativeAppearance() {
  const catalog=JSON.parse(await readFile(new URL('../../NaCBORUIforLVGL/examples/windows_player/schemes.json',import.meta.url),'utf8'));
  catalog.schemes.night={name:'Ночная',canvas:'#000000',surface:'#000000',surfaceAlt:'#000000',
    border:'#480000',text:'#aa0000',muted:'#700000',bar:'#000000',barText:'#900000',
    primary:'#b00000',hover:'#b00000'};
  const styles={},screens=[];
  for(const [id,p] of Object.entries(catalog.schemes)) {
    const roles=roleStyles(p);
    roles.button={bg_color:p.surface,bg_opa:255,text_color:p.text,border_color:p.border,
      border_width:1,shadow_width:0,outline_width:0};
    // An inset outline changes stroke thickness without moving text or hit targets.
    roles.button_pressed={bg_color:p.surface,text_color:p.text,border_color:p.primary,
      outline_color:p.primary,outline_width:2,outline_pad:-2};
    roles.button_focus={...roles.button_pressed};
    roles.focus_outline={outline_color:p.primary,outline_width:2,outline_pad:-2};
    // This application uses the shared indicator role only for pressure numerals.
    roles.indicator={text_color:id==='night'?'#e00000':p.text};
    for(const [role,props] of Object.entries(roles))styles[id+'.'+role]={scope:'global',name:id+'.'+role,attach:false,props};
    screens.push({format:'lvgl-screen-draft-1',id,name:p.name,size:{width:64,height:64},styles:{},
      root:{id:'root',type:'obj',props:{},style:{},children:[]}});
  }
  return encodeProject(createProject(screens,{id:'windows-schemes',name:'TPMS appearance resources',styles}));
}
