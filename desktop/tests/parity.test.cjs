'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const {gzipSync}=require('node:zlib');
const {createHash}=require('node:crypto');
const {catalog,download,extract,install}=require('../src/catalog.cjs');
const {plan,paste}=require('../src/paste.cjs');
function member(name,data,type='0'){
 const h=Buffer.alloc(512);h.write(name);h.write('0000600\0',100);h.write('0000000\0',108);h.write('0000000\0',116);h.write(data.length.toString(8).padStart(11,'0')+'\0',124);h.write('00000000000\0',136);h.fill(32,148,156);h.write(type,156);h.write('ustar\0',257);h.write('00',263);
 const sum=h.reduce((a,b)=>a+b,0);h.write(sum.toString(8).padStart(6,'0')+'\0 ',148);return Buffer.concat([h,data,Buffer.alloc((512-data.length%512)%512)]);
}
const entry={id:'os9-test',name:'OS9',summary:'Test',minimumAppVersion:'4.0.0',archiveURL:'https://example.com/mac.tar.gz',archiveBytes:100,installedBytes:512,sha256:'a'.repeat(64)};
async function temp(t){const p=await fs.mkdtemp(path.join(os.tmpdir(),'classicmac-parity-'));t.after(()=>fs.rm(p,{recursive:true,force:true}));return p;}
test('Library validates catalog IDs, bounds, checksums and secure origins',()=>{
 assert.equal(catalog(Buffer.from(JSON.stringify({schemaVersion:1,machines:[entry]}))).length,1);
 for(const patch of [{archiveURL:'http://example.com/a'},{sha256:'no'},{installedBytes:0},{id:'../disk'}])assert.throws(()=>catalog(Buffer.from(JSON.stringify({schemaVersion:1,machines:[{...entry,...patch}]}))));
 assert.throws(()=>catalog(Buffer.from(JSON.stringify({schemaVersion:1,machines:[entry,entry]}))));
});
test('Archive extraction preserves logical disk size and rejects traversal, links and duplicate members',async t=>{
 const dir=await temp(t),disk=Buffer.alloc(2097152);disk.write('Mac disk');const config=Buffer.from('{}');
 const m={...entry,installedBytes:disk.length+config.length,diskCapacityBytes:disk.length};
 const archive=path.join(dir,'test.gz');await fs.writeFile(archive,gzipSync(Buffer.concat([member('config.json',config),member('disk.img',disk),Buffer.alloc(1024)])));
 const out=path.join(dir,'safe');await fs.mkdir(out);await extract(archive,out,m);assert.equal((await fs.stat(path.join(out,'disk.img'))).size,disk.length);assert.equal((await fs.readFile(path.join(out,'disk.img'))).subarray(0,8).toString(),'Mac disk');
 for(const [i,items] of [[member('../escape',config)],[member('config.json',config,'2')],[member('config.json',config),member('config.json',config)]].entries()){
  const bad=path.join(dir,'bad'+i);await fs.mkdir(bad);await fs.writeFile(archive,gzipSync(Buffer.concat([...items,Buffer.alloc(1024)])));await assert.rejects(extract(archive,bad,m));
 }
 await assert.rejects(fs.access(path.join(dir,'escape')));
});
test('Quadra archives bound and isolate optional ROM and PRAM',async t=>{
 const dir=await temp(t),config=Buffer.from('{}'),disk=Buffer.alloc(512),rom=Buffer.alloc(1048576);
 const archive=path.join(dir,'quadra.gz');
 for(const [index,firmware] of [[member('Quadra800.rom',rom)],[member('pram.img',Buffer.alloc(256))],[member('pram.img',Buffer.alloc(3))]].entries()){
  const bytes=gzipSync(Buffer.concat([member('config.json',config),member('disk.img',disk),...firmware,Buffer.alloc(1024)]));
  await fs.writeFile(archive,bytes);
  const size=index===0?rom.length:index===1?256:3,m={...entry,installedBytes:config.length+disk.length+size};
  const ppc=path.join(dir,'ppc'+index);await fs.mkdir(ppc);await assert.rejects(extract(archive,ppc,m),/Unsafe/);
  const quadra=path.join(dir,'quadra'+index);await fs.mkdir(quadra);
  if(index===2)await assert.rejects(extract(archive,quadra,{...m,machineFamily:'quadra800'}),/firmware size/);
  else await extract(archive,quadra,{...m,machineFamily:'quadra800'});
 }
});

test('Qualified public classic base archive imports with prepared hardware',async t=>{
 const env=process.env;
 if(!env.CLASSIC_BASE_TEST_CATALOG||!env.CLASSIC_BASE_TEST_ARCHIVE||!env.CLASSIC_BASE_TEST_ID){t.skip('Set CLASSIC_BASE_TEST_CATALOG, ARCHIVE and ID');return;}
 const dir=await temp(t),m=catalog(await fs.readFile(env.CLASSIC_BASE_TEST_CATALOG)).find(m=>m.id===env.CLASSIC_BASE_TEST_ID);assert.ok(m);
 const cache=path.join(dir,'cache');await fs.mkdir(cache);await fs.copyFile(env.CLASSIC_BASE_TEST_ARCHIVE,path.join(cache,m.sha256+'.tar.gz.part'));
 const folder=await install(m,dir,m.name,cache),c=JSON.parse(await fs.readFile(path.join(folder,'config.json')));
 assert.equal(c.machineFamily,m.machineFamily||'powerMacG4');assert.equal(c.ramMB,128);assert.equal(c.toolsCDInserted,false);assert.equal(c.useEnhancedFramebuffer,false);
 assert.equal(c.width,640);assert.equal(c.height,480);assert.equal(c.depth,c.machineFamily==='quadra800'?8:16);assert.equal(c.cdImagePath,undefined);
 assert.equal((await fs.stat(path.join(folder,'disk.img'))).size,m.diskCapacityBytes);
 if(c.machineFamily==='quadra800')assert.equal(createHash('sha256').update(await fs.readFile(path.join(folder,'Quadra800.rom'))).digest('hex'),'05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca');
 else {assert.equal(c.useVirtioDisk,true);assert.equal(c.useG4CPU,false);assert.equal(c.tabletInput,false);}
});
test('Resumed download requires an exact range and verifies its SHA256',async t=>{
 const dir=await temp(t),bytes=Buffer.from('archive bytes for a resume'),hash=createHash('sha256').update(bytes).digest('hex'),m={...entry,sha256:hash,archiveBytes:bytes.length};
 await fs.writeFile(path.join(dir,hash+'.tar.gz.part'),bytes.subarray(0,7));const original=global.fetch;
 try{
  global.fetch=async(_url,options)=>{assert.equal(options.headers.Range,'bytes=7-');return new Response(bytes.subarray(7),{status:206,headers:{'content-length':String(bytes.length-7),'content-range':`bytes 7-${bytes.length-1}/${bytes.length}`}});};
  assert.deepEqual(await fs.readFile(await download(m,dir)),bytes);
  await fs.writeFile(path.join(dir,hash+'.tar.gz.part'),Buffer.alloc(bytes.length));await assert.rejects(download(m,dir),/SHA-256/);
 }finally{global.fetch=original;}
});
test('Paste plans Mac US chords and rejects unsupported text before typing',async()=>{
 assert.deepEqual(plan('A\r\néñ'),[['shift-a'],['ret'],['alt-e','e'],['alt-n','n']]);
 let sent=0;await assert.rejects(paste({hmp:async()=>sent++},'valid🙂'));assert.equal(sent,0);
 assert.throws(()=>plan('a'.repeat(8193)));assert.deepEqual(plan('$(rm -rf)'),[['shift-4'],['shift-9'],['r'],['m'],['spc'],['minus'],['r'],['f'],['shift-0']]);
});
test('Pause cancels a paste before the next chord',async()=>{
 let sent=0;await assert.rejects(paste({paused:true,hmp:async()=>sent++},'abc'));assert.equal(sent,0);
});

test('Clean ATI editions import prepared hardware and driver-neutral provenance',async t=>{
 const dir=await temp(t),config=Buffer.from(JSON.stringify({machineFamily:'powerMacG4',diskImageName:'disk.img',graphicsCard:'atiRadeon'})),disk=Buffer.alloc(512);
 const bytes=gzipSync(Buffer.concat([member('config.json',config),member('disk.img',disk),Buffer.alloc(1024)]));
 const original=global.fetch;
 try{
  global.fetch=async()=>new Response(bytes,{headers:{'content-length':String(bytes.length)}});
  for(const [id,graphicsCard] of [['mac-os-9.2.1-ati-32gb-v4','atiRadeon'],['mac-os-9.2.1-ati-32gb-v5','atiRadeon'],['unprepared-v1','standard']]){
   const m={...entry,id,osVersion:'9.2.1',sha256:createHash('sha256').update(bytes).digest('hex'),archiveBytes:bytes.length,installedBytes:config.length+disk.length};
   const folder=await install(m,dir,id,path.join(dir,'cache'));
   assert.equal(JSON.parse(await fs.readFile(path.join(folder,'config.json'))).graphicsCard,graphicsCard);
   const metadata=JSON.parse(await fs.readFile(path.join(folder,'template-info.json')));
   assert.equal(metadata.templateID,id);assert.equal(metadata.osVersion,'9.2.1');assert.equal(Object.hasOwn(metadata,'gxMetalVersion'),false);
  }
 }finally{global.fetch=original;}
});
