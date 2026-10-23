'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const assert = require('node:assert/strict');

async function qualify({ window, dialog, home }) {
  const js=source=>window.webContents.executeJavaScript(source);
  const checks=[];
  async function until(source){const end=Date.now()+20000;while(Date.now()<end){if(await js(source))return;await new Promise(r=>setTimeout(r,50));}throw new Error('UI timeout: '+source);}
  async function shot(name){await new Promise(r=>setTimeout(r,150));await fs.writeFile(path.join(home,name+'.png'),(await window.webContents.capturePage()).toPNG());}
  await until('window.classicmacEvidence?.().setup?.active');
  await shot('welcome');
  assert.equal(await js('document.querySelector("nav") !== null'),false);
  assert.equal(await js('document.getElementById("setup-next").disabled'),true);
  await js('document.getElementById("setup-license").click();document.getElementById("setup-next").click()');
  await until('window.classicmacEvidence().setup.step === "source"');await shot('source');
  await js('document.querySelector("input[name=setup-source][value=installer]").click();document.getElementById("setup-next").click()');
  await until('document.querySelector("#setup-installer-fields .popup-button")');
  await js('document.querySelector("#setup-installer-fields .popup-button").click()');
  await until('document.querySelector(".popup-menu:popover-open")');await shot('popup');
  await js('document.querySelectorAll(".popup-item")[1].click()');
  assert.equal(await js('document.getElementById("setup-model").value'),'quadra800');
  checks.push('Wizard acknowledgment, bitmap radio controls and keyboard accessible Platinum popup');
  const folder=path.join(home,'UI Fixture.classic');await fs.mkdir(folder);
  await fs.writeFile(path.join(folder,'disk.img'),'Disposable UI fixture, never booted.');
  await fs.writeFile(path.join(folder,'config.json'),JSON.stringify(require('./machines.cjs').freshConfig('Mac OS 9.2.2 SuperDeluxe')));
  const original=dialog.showOpenDialog;
  dialog.showOpenDialog=async()=>({canceled:false,filePaths:[folder]});
  try{
    await js('document.getElementById("setup-back").click();document.getElementById("setup-open").click()');
    await until('window.classicmacEvidence().setup.step === "ready"');
    await js('document.getElementById("setup-later").click()');
    await until('window.classicmacEvidence().setup.active === false');
  }finally{dialog.showOpenDialog=original;}
  for(const [w,h] of [[940,700],[780,560]]){
    window.setSize(w,h);await new Promise(r=>setTimeout(r,200));await shot('home-'+w);
    const evidence=await js('window.classicmacEvidence()');
    assert(evidence.toolbar.fitsWidth&&evidence.toolbar.singleLineButtons&&evidence.toolbar.bottomVisible);
    assert.equal(evidence.platinum.windowWidgets,3);assert(evidence.platinum.bitmapLabels>30);
    assert.equal(await js('document.documentElement.scrollWidth <= innerWidth && document.documentElement.scrollHeight <= innerHeight'),true);
  }
  checks.push('No host menu bar; semantic bitmap labels, measured document frame and unclipped home at both window sizes');
  await js('document.getElementById("settings").click()');await until('document.getElementById("editor").open');await shot('get-info');
  await js('document.querySelector("#editor button[value=cancel]").click();document.getElementById("media").click()');await until('document.getElementById("media-dialog").open');await shot('media');
  await js('document.getElementById("close-media").click();document.getElementById("window-collapse").click()');
  await until('document.body.classList.contains("shaded")');assert.equal(window.getBounds().height,23);await shot('windowshade');
  await js('document.getElementById("window-collapse").click()');await until('!document.body.classList.contains("shaded")');assert.equal(window.getBounds().height,560);
  checks.push('Get Info, Media and reversible WindowShade through the actual sandboxed IPC');
  // Capture actual DOM controls after Chromium paints them. The external
  // harness compares every captured device pixel with independent guest PNGs.
  const pixelCases=[];
  for(const state of ['normal','pressed','disabled']){
    pixelCases.push({name:'button-'+state,tag:'button',w:80,h:20,state},{name:'default-'+state,tag:'button',w:86,h:26,state,cls:'default'},{name:'popup-'+state,tag:'button',w:100,h:20,state,cls:'popup-button'});
    for(const on of [false,true])for(const type of ['checkbox','radio'])pixelCases.push({name:type+'-'+(on?'on':'off')+'-'+state,tag:'input',type,on,w:type==='radio'?12:14,h:12,state});
  }
  for(const spec of pixelCases){
    await js(`(()=>{document.getElementById('pixel-capture')?.remove();const pane=document.createElement('div');pane.id='pixel-capture';pane.style='position:fixed;left:0;top:0;width:400px;height:100px;background:#ddd;z-index:2147483647';const e=document.createElement(${JSON.stringify(spec.tag)});e.className=${JSON.stringify(spec.cls||'')};e.style='position:absolute;left:0;top:0;margin:0;width:${spec.w}px;height:${spec.h}px';${spec.type?`e.type=${JSON.stringify(spec.type)};e.checked=${spec.on};`:''}e.disabled=${spec.state==='disabled'};e.dataset.held=${JSON.stringify(String(spec.state==='pressed'))};pane.append(e);document.body.append(pane);})()`);
    await new Promise(r=>setTimeout(r,80));
    await fs.writeFile(path.join(home,'pixel-'+spec.name+'.png'),(await window.webContents.capturePage({x:0,y:0,width:spec.w,height:spec.h})).toPNG());
  }
  await js('document.getElementById("pixel-capture").remove()');
  await fs.writeFile(path.join(home,'ui-report.json'),JSON.stringify({platform:process.platform,version:require('../package.json').version,checks,passed:true},null,2));
}
module.exports={qualify};
