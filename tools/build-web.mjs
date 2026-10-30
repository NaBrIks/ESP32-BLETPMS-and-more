import {readFile,writeFile} from 'node:fs/promises';
const path=new URL('../src/www/',import.meta.url);
const [head,app,tooltip,car,sensor,sensorUi,assignment,home]=await Promise.all(['shell-head.html','app.js','tooltip.inc.js','icons/car.svg','tpms-sensor.js','sensor-ui.js','wheel-assignment.js','icons/home.svg'].map(f=>readFile(new URL(f,path),'utf8')));
const markup=head.replace(/^\uFEFF/,'').replace('<script>',`<template id="icon-car">${car.replace(/<!--[\s\S]*?-->/g,'').trim()}</template><template id="icon-home">${home.trim()}</template>\n<script>`);
if(!app.includes('localize();loadTpms();page='))throw Error('Application initialization marker missing');
await writeFile(new URL('ble.html',path),markup+assignment+'\n'+sensor.replace(/^\uFEFF/,'')+'\n'+app.replace(/^\uFEFF/,'').replace('localize();loadTpms();page=',sensorUi.replace(/^\uFEFF/,'')+'\nlocalize();loadTpms();page=')+'\n</script><script type="module">\n'+tooltip.replace(/^\uFEFF/,'')+'\n</script></body></html>\n');
console.log('Built embedded site');
