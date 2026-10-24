'use strict';
const fs=require('node:fs/promises');
const {createReadStream}=require('node:fs');
const path=require('node:path');
const {createHash,randomUUID}=require('node:crypto');
const {createGunzip}=require('node:zlib');
const {freshConfig,validate,writeJSON}=require('./machines.cjs');
const CATALOG_URL='https://mcchord.net/classicmac/catalog-v2.json';
const QUADRA_ROM_SHA256='05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca';
const limit=140*1073741824;
const currentVersion=require('../package.json').version;
function supportedVersion(minimum){const a=currentVersion.split('.').map(Number),b=minimum.split('.').map(Number);for(let i=0;i<3;i++){if((a[i]||0)!==(b[i]||0))return(a[i]||0)>(b[i]||0);}return true;}
function secureURL(value) { const u=new URL(value); if(u.protocol!=='https:'||u.username||u.password||u.hash) throw new Error('Library downloads require HTTPS.'); return u; }
function catalog(data) {
 if(data.length>1048576) throw new Error('The Library catalog is too large.');
 const d=JSON.parse(data.toString());
 if(d.schemaVersion!==1||!Array.isArray(d.machines)||d.machines.length>50) throw new Error('Unsupported Library catalog.');
 const seen=new Set();
 for(const m of d.machines) {
  if(!/^[a-z0-9][a-z0-9._-]{0,99}$/.test(m.id)||seen.has(m.id)||typeof m.name!=='string'||!m.name.trim()||m.name.length>100||typeof m.summary!=='string'||m.summary.length>1000||!/^\d{1,6}\.\d{1,6}(\.\d{1,6})?$/.test(m.minimumAppVersion)||!Number.isSafeInteger(m.archiveBytes)||m.archiveBytes<=0||m.archiveBytes>limit||!Number.isSafeInteger(m.installedBytes)||m.installedBytes<512||m.installedBytes>limit||! /^[a-f0-9]{64}$/.test(m.sha256)) throw new Error('Invalid Library entry.');
  secureURL(m.archiveURL);seen.add(m.id);
  if(!['powerMacG4','powerMac7500','quadra800'].includes(m.machineFamily||'powerMacG4'))throw new Error('Unknown Library Mac model.');
 }
 return d.machines;
}
async function request(url,headers={},signal) {
 const initial=secureURL(url);
 for(let count=0;count<4;count++) {
  const response=await fetch(url,{headers,signal,redirect:'manual'});
  if([301,302,303,307,308].includes(response.status)) {
   const next=secureURL(new URL(response.headers.get('location'),url).href);
   if(next.origin!==initial.origin) throw new Error('The download redirected to a different site.');
   await response.body?.cancel();url=next.href;continue;
  }
  return response;
 }
 throw new Error('Too many download redirects.');
}
async function loadCatalog(signal) {
 const r=await request(CATALOG_URL,{},signal);if(!r.ok||!r.body)throw new Error('The Library catalog could not be downloaded.');
 const chunks=[];let bytes=0;
 for await(const chunk of r.body) {bytes+=chunk.length;if(bytes>1048576){await r.body.cancel().catch(()=>{});throw new Error('The catalog is too large.');}chunks.push(Buffer.from(chunk));}
 return catalog(Buffer.concat(chunks));
}
async function writeAll(file,buffer,position=null) {
 let offset=0;while(offset<buffer.length){const r=await file.write(buffer,offset,buffer.length-offset,position===null?null:position+offset);if(!r.bytesWritten)throw new Error('The disk stopped accepting writes.');offset+=r.bytesWritten;}
}
async function download(m,cache,{signal,progress=()=>{}}={}) {
 await fs.mkdir(cache,{recursive:true});const file=path.join(cache,m.sha256+'.tar.gz.part');
 let offset=0;try{offset=(await fs.stat(file)).size;}catch{}
 if(offset>m.archiveBytes){await fs.unlink(file);offset=0;}
 if(offset<m.archiveBytes){
  let r=await request(m.archiveURL,offset?{Range:`bytes=${offset}-`}:{},signal);
  if(offset&&r.status===200){offset=0;}
  else if(offset) {
   const range=r.headers.get('content-range');
   if(r.status!==206||range!==`bytes ${offset}-${m.archiveBytes-1}/${m.archiveBytes}`)throw new Error('The server could not resume this download safely.');
  }
  if(!r.ok||!r.body)throw new Error('The machine archive could not be downloaded.');
  const length=Number(r.headers.get('content-length'));
  if(length!==m.archiveBytes-offset)throw new Error('The archive size does not match the Library catalog.');
  const handle=await fs.open(file,offset?'a':'w');
  try{for await(const part of r.body){signal?.throwIfAborted();if(offset+part.length>m.archiveBytes)throw new Error('Archive exceeds its catalog size.');await writeAll(handle,part);offset+=part.length;progress({phase:'Downloading',completed:offset,total:m.archiveBytes});}}
  finally{await handle.close();}
 }
 if(offset!==m.archiveBytes)throw new Error('The download ended early. Retry to resume.');
 let checked=0;const hash=createHash('sha256');for await(const part of createReadStream(file)){signal?.throwIfAborted();hash.update(part);checked+=part.length;progress({phase:'Checking download',completed:checked,total:m.archiveBytes});}
 if(hash.digest('hex')!==m.sha256){await fs.unlink(file);throw new Error('The archive failed its SHA-256 integrity check. Retry the download.');}
 return file;
}
class Reader {
 constructor(stream,max){this.iterator=stream[Symbol.asyncIterator]();this.buffer=Buffer.alloc(0);this.max=max;this.total=0;}
 async read(n,optional=false){
  while(this.buffer.length<n){const {value,done}=await this.iterator.next();if(done){if(optional&&this.buffer.length===0)return null;throw new Error('Truncated machine archive.');}this.total+=value.length;if(this.total>this.max)throw new Error('Machine archive expands beyond its declared size.');this.buffer=Buffer.concat([this.buffer,value]);}
  const result=this.buffer.subarray(0,n);this.buffer=this.buffer.subarray(n);return result;
 }
}
function number(field){
 let value=0n;
 if(field[0]&128){for(const [i,b] of field.entries())value=value*256n+BigInt(i?b:b&127);}
 else{const s=field.toString('ascii').replace(/\0.*$/s,'').trim();if(!/^[0-7]+$/.test(s))throw new Error('Invalid archive number.');value=BigInt('0o'+s);}
 if(value>BigInt(limit))throw new Error('Archive member is too large.');return Number(value);
}
async function extract(archive,folder,m,{signal,progress=()=>{}}={}) {
 const input=createReadStream(archive),gzip=createGunzip({chunkSize:1048576});input.on('error',error=>gzip.destroy(error));input.pipe(gzip);
 const reader=new Reader(gzip,m.installedBytes+65536);let total=0;const seen=new Set(),zero=Buffer.alloc(1048576);
 try{
  for(;;){
   signal?.throwIfAborted();const header=await reader.read(512);
   if(header.equals(Buffer.alloc(512))){if(!(await reader.read(512)).equals(Buffer.alloc(512)))throw new Error('Invalid archive end.');let padding=0,tail;while((tail=await reader.read(512,true))){padding+=512;if(padding>10240||!tail.equals(Buffer.alloc(512)))throw new Error('Invalid archive padding.');}break;}
   const checksum=number(header.subarray(148,156));let sum=0;header.forEach((b,i)=>{sum+=i>=148&&i<156?32:b;});
   const name=new TextDecoder('utf-8',{fatal:true}).decode(header.subarray(0,100)).split('\0')[0];
   const allowed=['config.json','disk.img','preview.png'];
   if(m.machineFamily==='quadra800')allowed.push('Quadra800.rom','pram.img');
   if(checksum!==sum||![0,48].includes(header[156])||header.subarray(157,257).some(Boolean)||header.subarray(345,500).some(Boolean)||!allowed.includes(name)||seen.has(name))throw new Error('Unsafe machine archive member.');
   seen.add(name);const size=number(header.subarray(124,136));
   if(size<=0||size>m.installedBytes-total||(name==='config.json'&&size>65536)||(name==='preview.png'&&size>16*1048576)||(name==='disk.img'&&(size<512||size%512||(m.diskCapacityBytes&&size!==m.diskCapacityBytes))))throw new Error('Archive member size does not match its catalog.');
   if((name==='Quadra800.rom'&&size!==1048576)||(name==='pram.img'&&![256,512].includes(size)))throw new Error('Invalid Quadra firmware size.');
   const file=await fs.open(path.join(folder,name),'wx',0o600);
   try{
    let position=0;
    while(position<size){signal?.throwIfAborted();const n=Math.min(1048576,size-position);const chunk=await reader.read(n);
     // Seek over zero chunks to preserve sparse files on supporting filesystems.
     if(name!=='disk.img'||!chunk.equals(zero.subarray(0,n)))await writeAll(file,chunk,position);
     position+=n;total+=n;progress({phase:'Installing',completed:total,total:m.installedBytes});
    }
    await file.truncate(size);
   }finally{await file.close();}
   const pad=(512-size%512)%512;if(pad&&(await reader.read(pad)).some(Boolean))throw new Error('Invalid archive member padding.');
  }
  if(total!==m.installedBytes||!seen.has('disk.img')||!seen.has('config.json'))throw new Error('Incomplete machine archive.');
 }finally{input.destroy();gzip.destroy();}
}
async function install(m,parent,name,cache,options={}) {
 if(!supportedVersion(m.minimumAppVersion))throw new Error('Update ClassicMac to install this Library edition.');
 const family=m.machineFamily||'powerMacG4';
 if(!['powerMacG4','quadra800'].includes(family))throw new Error('This edition needs the macOS Copland engine.');
 const archive=await download(m,cache,options);
 const stats=await fs.statfs(parent);if(stats.bavail*stats.bsize<m.installedBytes+268435456)throw new Error('There is not enough space to install this Mac.');
 let folder;
 const base=(name||m.name).trim().replace(/[<>:"/\\|?*]/g,'-').replace(/[. ]+$/,'').slice(0,100)||'Mac OS';
 for(let n=1;n<=10000;n++){const candidate=path.join(parent,base+(n===1?'':` ${n}`)+'.classic');try{await fs.mkdir(candidate,{mode:0o700});folder=candidate;break;}catch(e){if(e.code!=='EEXIST')throw e;}}
 if(!folder)throw new Error('Choose another machine name.');
 try{
  await extract(archive,folder,m,options);
  const source=JSON.parse(await fs.readFile(path.join(folder,'config.json'),'utf8'));
  if((source.machineFamily||'powerMacG4')!==family||source.diskImageName!=='disk.img'||(source.pramImageName||'pram.img')!=='pram.img')throw new Error('The template does not match its Library entry.');
  if(family==='quadra800'){
   let rom;try{rom=await fs.readFile(path.join(folder,'Quadra800.rom'));}catch(error){if(error.code!=='ENOENT')throw error;}
   if(rom&&createHash('sha256').update(rom).digest('hex')!==QUADRA_ROM_SHA256)throw new Error('The Quadra ROM failed its integrity check.');
  }
  const c=freshConfig(name||m.name,family);for(const key of ['ramMB','width','height','depth','networking','sound','useG4CPU','useVirtioDisk','tabletInput','classicInputHelpers','useEnhancedFramebuffer'])if(Object.hasOwn(source,key))c[key]=source[key];
  c.diskSizeGB=Math.ceil((await fs.stat(path.join(folder,'disk.img'))).size/1073741824);c.toolsCDInserted=source.toolsCDInserted??(family==='powerMacG4');
  if(/^(?:os922-(?:superdeluxe|deluxe)|mac-os-9\.2\.1-ati-32gb)-v\d+$/.test(m.id))c.graphicsCard='atiRadeon';
  await writeJSON(path.join(folder,'config.json'),validate(c));
  await writeJSON(path.join(folder,'template-info.json'),{schemaVersion:1,templateID:m.id,osVersion:m.osVersion,archiveSHA256:m.sha256,importedAt:new Date().toISOString()});
  return folder;
 }catch(error){for(const file of ['config.json','disk.img','preview.png','template-info.json','Quadra800.rom','pram.img'])await fs.rm(path.join(folder,file),{force:true});await fs.rmdir(folder);throw error;}
}
module.exports={CATALOG_URL,catalog,loadCatalog,download,extract,install,number,supportedVersion};
