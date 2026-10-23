import * as C from './platinum-pixels.mjs';

// Keep semantic HTML and keyboard/accessibility behavior, but paint the same
// integer pixels as the native Mac app. No host widgets or font rasterizer.
const images = new Map();
const canvas = document.createElement('canvas');
function image(p) {
  canvas.width=p.w;canvas.height=p.h;
  canvas.getContext('2d').putImageData(new ImageData(p.data,p.w,p.h),0,0);
  return canvas.toDataURL();
}
function cached(key,draw,w,h,background=null) {
  if(!images.has(key)){const p=new C.Pixels(w,h,background);draw(p);images.set(key,image(p));if(images.size>800)images.delete(images.keys().next().value);}
  return `url("${images.get(key)}")`;
}
let pending=false,active=true;
const controls='button:not(#machines button):not(.popup-item),input[type=checkbox],input[type=radio],progress';
const watched=new WeakSet();
const resize=new ResizeObserver(schedule);
function schedule(){if(!pending){pending=true;requestAnimationFrame(paint);}}
const observer=new MutationObserver(schedule);
const observe=()=>observer.observe(document.body,{subtree:true,childList:true,characterData:true,attributes:true,attributeFilter:['disabled','checked','class','open','hidden','value']});
function state(e){return e.disabled?'disabled':e.matches(':active')||e.dataset.held==='true'?'pressed':'normal';}
function watch(e){if(!watched.has(e)){watched.add(e);resize.observe(e);}}

function paintText(root) {
  const walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);
  const nodes=[];while(walker.nextNode())nodes.push(walker.currentNode);
  for(const node of nodes){
    if(!node.textContent.trim()||node.parentElement.closest('.pixel-text,script,style,option,textarea,input,select,[contenteditable]'))continue;
    const fragment=document.createDocumentFragment();
    for(const word of node.textContent.match(/\S+\s*|\s+/g)||[]){
      const span=document.createElement('span');span.className='pixel-text';span.textContent=word;fragment.append(span);
    }
    const run=document.createElement('span');run.className='pixel-run';run.append(fragment);node.replaceWith(run);
  }
  for(const e of root.querySelectorAll('.pixel-text')){
    const style=getComputedStyle(e.parentElement),font=style.getPropertyValue('--bitmap-font').trim()||'system';
    const rgb=style.color.match(/\d+/g),color=(Number(rgb[0])<<16)|(Number(rgb[1])<<8)|Number(rgb[2]);
    const str=e.textContent,w=C.textWidth(str,font),h=font==='small'?13:16;
    e.style.width=w+'px';e.style.height=h+'px';
    e.style.backgroundImage=cached(`text:${font}:${color}:${str}`,p=>C.text(p,str,0,font==='small'?10:12,color,font),Math.max(1,w),h);
  }
}
function paint() {
  pending=false;observer.disconnect();
  for(const e of document.querySelectorAll(controls)){
    watch(e);const w=Math.round(e.offsetWidth),h=Math.round(e.offsetHeight);if(!w||!h)continue;
    const s=state(e),kind=e.dataset.widget;
    if(kind){e.style.backgroundImage=cached(`widget:${kind}:${s}:${active}`,p=>{if(active)C.titleBox(p,kind,s==='pressed');},13,13);continue;}
    if(e.type==='checkbox'||e.type==='radio'){
      e.style.backgroundImage=cached(`toggle:${e.type}:${e.checked}:${s}:${e.indeterminate}`,p=>C.toggle(p,e.type==='radio',e.checked,s,e.indeterminate),e.type==='radio'?12:14,12);continue;
    }
    if(e.tagName==='PROGRESS'){
      const v=e.hasAttribute('value')?e.value/e.max:null;
      e.style.backgroundImage=cached(`progress:${w}:${v}`,p=>C.progress(p,v),w,14,C.face);continue;
    }
    const def=e.classList.contains('default'),pop=e.classList.contains('popup-button');
    e.style.backgroundImage=cached(`button:${w}:${h}:${s}:${def}:${pop}`,p=>pop?C.popup(p,s):C.button(p,s,def),w,h);
  }
  for(const e of document.querySelectorAll('.window,dialog')){
    watch(e);const w=e.offsetWidth,h=e.offsetHeight;if(!w||!h)continue;
    const title=e.querySelector(':scope > .titlebar strong, :scope > form > .titlebar strong')?.textContent||'';
    const boxes=e.classList.contains('window'),focused=boxes?active&&!document.querySelector('dialog[open]'):true;
    const titleWidth=C.textWidth(title),collapsed=document.body.classList.contains('shaded');
    e.style.backgroundImage=cached(`frame:${w}:${h}:${focused}:${titleWidth}:${boxes}:${collapsed}`,p=>C.windowFrame(p,{active:focused,titleWidth,boxes,growable:boxes,collapsed:boxes&&collapsed}),w,h,C.face);
    e.classList.toggle('inactive',!focused);
  }
  syncPopups();paintText(document.body);observe();
}

// Native <select> dropdowns otherwise expose the Windows/GTK appearance.
// The original select retains its form value; the popup handles user input.
const popups=new WeakMap();
function syncPopups(){
  for(const select of document.querySelectorAll('select')){
    let button=popups.get(select);
    if(!button){
      button=document.createElement('button');button.type='button';button.className='popup-button';button.setAttribute('aria-haspopup','listbox');
      button.onclick=()=>openPopup(select,button);
      button.onkeydown=event=>{
        if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){
          event.preventDefault();const options=[...select.options].filter(o=>!o.disabled),i=options.indexOf(select.selectedOptions[0]);
          const next=event.key==='Home'?0:event.key==='End'?options.length-1:Math.max(0,Math.min(options.length-1,i+(event.key==='ArrowDown'?1:-1)));
          if(options[next]){select.value=options[next].value;select.dispatchEvent(new Event('change',{bubbles:true}));schedule();}
        }
      };
      select.after(button);select.classList.add('popup-source');select.tabIndex=-1;select.setAttribute('aria-hidden','true');popups.set(select,button);
    }
    const label=select.selectedOptions[0]?.textContent||'Choose…';
    if(button.textContent!==label)button.textContent=label;
    button.disabled=select.disabled;button.style.minWidth=select.id==='setup-edition'||select.id==='setup-model'?'300px':'120px';
    button.setAttribute('aria-label',select.getAttribute('aria-label')||select.closest('label')?.childNodes[0]?.textContent?.trim()||label);
  }
}
function openPopup(select,button){
  const menu=document.createElement('div');menu.className='popup-menu';menu.setAttribute('popover','auto');menu.setAttribute('role','listbox');
  const options=[...select.options].filter(o=>!o.hidden);
  for(const option of options){
    const item=document.createElement('button');item.type='button';item.className='popup-item';item.textContent=option.textContent;item.disabled=option.disabled;
    item.setAttribute('role','option');item.setAttribute('aria-selected',String(option.selected));
    item.onclick=()=>{select.value=option.value;select.dispatchEvent(new Event('change',{bubbles:true}));menu.hidePopover();button.focus();schedule();};
    menu.append(item);
  }
  // A modal's popup must belong to that modal to remain interactive.
  (select.closest('dialog')||document.body).append(menu);
  const rect=button.getBoundingClientRect();menu.style.minWidth=Math.round(rect.width)+'px';
  menu.style.left=Math.round(rect.left)+'px';menu.style.top=Math.round(Math.min(rect.bottom,innerHeight-Math.min(options.length*20+6,300)))+'px';
  menu.showPopover();button.dataset.held='true';button.setAttribute('aria-expanded','true');
  const items=[...menu.querySelectorAll('button:not(:disabled)')];
  (items.find(i=>i.getAttribute('aria-selected')==='true')||items[0])?.focus();
  menu.onkeydown=e=>{
    const i=items.indexOf(document.activeElement);
    if(['ArrowDown','ArrowUp','Home','End'].includes(e.key)){e.preventDefault();items[e.key==='Home'?0:e.key==='End'?items.length-1:(i+(e.key==='ArrowDown'?1:items.length-1))%items.length]?.focus();}
    if(e.key==='Escape'){e.preventDefault();menu.hidePopover();button.focus();}
  };
  menu.addEventListener('toggle',()=>{if(!menu.matches(':popover-open')){delete button.dataset.held;button.setAttribute('aria-expanded','false');menu.remove();schedule();}});
  schedule();
}
export function initPlatinum(api){
  for(const type of ['pointerdown','pointerup','pointercancel','keydown','keyup','change','focusin','focusout'])document.addEventListener(type,schedule,true);
  window.addEventListener('resize',schedule);
  api.onWindowState?.(value=>{active=value.focused;document.body.classList.toggle('shaded',value.shaded);schedule();});
  for(const [id,action] of [['window-close','close'],['window-zoom','zoom'],['window-collapse','collapse']])document.getElementById(id).onclick=()=>api.windowAction(action);
  // Keep all dialog layouts within the available work area at small sizes.
  document.addEventListener('keydown',event=>{
    if(event.key!=='Enter'||event.defaultPrevented||event.target.closest('textarea,button,select,.popup-menu')||document.pointerLockElement)return;
    if(document.querySelector('dialog[open]')){const button=[...document.querySelectorAll('dialog[open] .default:not(:disabled)')].at(-1);if(button){event.preventDefault();button.click();}}
    else if(document.getElementById('setup').hidden===false){const button=document.getElementById('setup-next');if(!button.disabled){event.preventDefault();button.click();}}
  });
  const grip=document.getElementById('window-grow');let drag;
  grip.onpointerdown=e=>{drag={x:e.screenX,y:e.screenY,w:outerWidth,h:outerHeight};grip.setPointerCapture(e.pointerId);};
  grip.onpointermove=e=>{if(drag)api.resizeWindow({width:drag.w+e.screenX-drag.x,height:drag.h+e.screenY-drag.y});};
  grip.onpointerup=grip.onpointercancel=()=>{drag=null;};
  observe();schedule();
}
