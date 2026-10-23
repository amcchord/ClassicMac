'use strict';
// Check the actual Chromium captures, in addition to the pure renderer gate.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const {PNG}=require('pngjs');
const home=path.resolve(process.argv[2]);
const fixtures=path.resolve(__dirname,'../../app/Tests/ClassicMacTests/Fixtures/Platinum');
const results=[];
(async()=>{
const C=await import('../ui/platinum-pixels.mjs');
const deviceScale=PNG.sync.read(fs.readFileSync(path.join(home,'pixel-button-normal.png'))).width/80;
for(const file of fs.readdirSync(home).filter(f=>f.startsWith('pixel-')&&f.endsWith('.png'))){
  const actual=PNG.sync.read(fs.readFileSync(path.join(home,file)));
  let expected;
  if(file==='pixel-window-title.png'){
    const w=actual.width/deviceScale,title='ClassicMac',tw=C.textWidth(title),p=new C.Pixels(w,23);
    C.windowFrame(p,{titleWidth:tw,collapsed:true});C.text(p,title,Math.floor((w-1-tw)/2),16);
    expected={width:w,height:20,data:Buffer.from(p.data.slice(0,w*20*4))};
  }else expected=PNG.sync.read(fs.readFileSync(path.join(fixtures,file.slice(6))));
  const scale=actual.width/expected.width;
  assert([1,2].includes(scale),'Expected an integer 1×/2× device scale');assert.equal(actual.height,expected.height*scale);
  let different=0;
  for(let y=0;y<actual.height;y++)for(let x=0;x<actual.width;x++){
    const a=(y*actual.width+x)*4,e=(Math.floor(y/scale)*expected.width+Math.floor(x/scale))*4;
    if(!actual.data.subarray(a,a+4).every((v,k)=>v===expected.data[e+k]))different++;
  }
  assert.equal(different,0,file+': differing captured device pixels');results.push({file,scale,differing_pixels:different});
}
assert.equal(results.length,22,'Expected all DOM control states and the composed native title frame');
fs.writeFileSync(path.join(home,'captured-pixels.json'),JSON.stringify({passed:true,results},null,2)+'\n');
console.log('All 21 DOM control captures and the composed Mac title frame match with zero differing pixels.');
})().catch(error=>{console.error(error);process.exitCode=1;});
