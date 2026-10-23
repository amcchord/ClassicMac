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
  await fs.writeFile(path.join(home,'ui-report.json'),JSON.stringify({platform:process.platform,version:require('../package.json').version,checks,passed:true},null,2));
}
module.exports={qualify};
