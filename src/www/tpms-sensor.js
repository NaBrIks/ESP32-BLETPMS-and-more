// Application component: a TPMS instrument, independent of any wheel position.
(()=>{
class TpmsSensor extends HTMLElement {
 set model(v){this._model=v;this.update();}get model(){return this._model;}
 connectedCallback(){if(!this.button){this.button=document.createElement('button');this.button.type='button';this.button.className='sensor-tire';this.fields={};for(const name of ['protocol','transport','number','pressure','tags','age']){const e=document.createElement(name==='pressure'?'strong':'span');e.className='sensor-'+name;this.fields[name]=e;this.button.append(e);}this.replaceChildren(this.button);this.button.onclick=()=>!this._model?.readOnly&&this.dispatchEvent(new CustomEvent('tpms-select',{bubbles:true,detail:{id:this._model?.id}}));}this.update();}
 update(){if(!this.button||!this._model)return;const m=this._model;this.dataset.sensorId=m.id;this.button.draggable=!m.readOnly;this.button.tabIndex=m.readOnly?-1:0;this.toggleAttribute("data-readonly",!!m.readOnly);this.toggleAttribute("data-hide-age",!!m.hideAge);this.button.classList.toggle('pressure-changed',!!m.changed);this.button.classList.toggle('sensor-selected',!!m.selected);this.button.setAttribute('aria-pressed',String(!!m.selected));this.button.setAttribute('aria-label',m.accessible);for(const [key,value]of Object.entries({protocol:m.protocol,transport:m.transport,number:m.number,pressure:m.pressure,tags:m.tags,age:m.age}))this.fields[key].textContent=value;}
}
customElements.define('tpms-sensor',TpmsSensor);
})();
