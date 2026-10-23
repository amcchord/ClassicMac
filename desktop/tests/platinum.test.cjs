'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { PNG } = require('pngjs');
const fixtures = path.resolve(__dirname, '../../app/Tests/ClassicMacTests/Fixtures/Platinum');
test('desktop renderer matches the independent Mac Appearance Manager captures at 1× and 2×', async () => {
  const C = await import('../ui/platinum-pixels.mjs');
  const cases = [];
  for (const state of ['normal','pressed','disabled']) {
    cases.push(['button-'+state,80,20,p=>C.button(p,state)],['default-'+state,86,26,p=>C.button(p,state,true)],['popup-'+state,100,20,p=>C.popup(p,state)],['checkbox-mixed-'+state,14,12,p=>C.toggle(p,false,false,state,true)],['os9-bevel-'+state,100,22,p=>C.bevel(p,state)]);
    for (const on of [false,true]) for (const radio of [false,true]) cases.push([(radio?'radio':'checkbox')+'-'+(on?'on':'off')+'-'+state,radio?12:14,12,p=>C.toggle(p,radio,on,state)]);
  }
  cases.push(['group',250,100,C.group],['sunken',104,26,C.sunken],['focus',256,56,C.focus]);
  for (const [i,value] of [0.3,0.7].entries()) cases.push(['progress-'+i,250,14,p=>C.progress(p,value)]);
  for (const kind of ['close','zoom','collapse']) cases.push(['os8-'+kind,13,13,p=>C.titleBox(p,kind)],['os9-'+kind+'-pressed',13,13,p=>C.titleBox(p,kind,true)]);
  for (const state of ['active','inactive','collapsed']) cases.push(['os9-window-'+state,333,state==='collapsed'?23:169,p=>C.windowFrame(p,{active:state!=='inactive',collapsed:state==='collapsed'})]);
  cases.push(['system-ascii',256,120,p=>{p.fill(0,0,p.w,p.h,0xffffff);for(let code=32;code<127;code++){const i=code-32;C.text(p,String.fromCodePoint(code),i%16*16,Math.floor(i/16)*20+14);}}]);
  for (const [name,w,h,draw] of cases) {
    const p = new C.Pixels(w,h);draw(p);
    const expected = PNG.sync.read(fs.readFileSync(path.join(fixtures,name+'.png')));
    assert.equal(expected.width,w,name);assert.equal(expected.height,h,name);
    for (const scale of [1,2]) {
      let differences=0;
      for(let y=0;y<h*scale;y++)for(let x=0;x<w*scale;x++){
        const i=(Math.floor(y/scale)*w+Math.floor(x/scale))*4;
        if(!p.data.subarray(i,i+4).every((v,k)=>v===expected.data[i+k]))differences++;
      }
      assert.equal(differences,0,`${name} at ${scale}×: differing device pixels`);
    }
  }
  const advances=JSON.parse(fs.readFileSync(path.join(fixtures,'system-advances.json')));
  advances.forEach((width,i)=>assert.equal(C.textWidth(String.fromCodePoint(i+32)),width));
});
