'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const {spawn}=require('node:child_process');
const {createWriteStream}=require('node:fs');
const {execFileSync}=require('node:child_process');
(async()=>{
  const exe=path.resolve(process.argv[2]),output=path.resolve(process.argv[3]);
  for(const scale of [1,2]){
    const home=path.join(output,'scale-'+scale);await fs.mkdir(home,{recursive:true});
    const child=spawn(exe,[`--force-device-scale-factor=${scale}`,'--disable-gpu'],{env:{...process.env,CLASSICMAC_TEST_HOME:home,CLASSICMAC_TEST_UI:'1'},stdio:['ignore','pipe','pipe']});
    const log=createWriteStream(path.join(home,'app.log'));child.stdout.pipe(log,{end:false});child.stderr.pipe(log,{end:false});child.once('close',()=>log.end());
    await new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{child.kill();reject(new Error('Portable UI qualification timed out'));},120000);
      child.once('error',e=>{clearTimeout(timer);reject(e);});child.once('exit',code=>{clearTimeout(timer);code===0?resolve():reject(new Error('UI process exited '+code));});
    });
    const report=JSON.parse(await fs.readFile(path.join(home,'ui-report.json'),'utf8'));if(!report.passed)throw new Error('UI report did not pass');
    execFileSync(process.execPath,[path.join(__dirname,'check-ui-pixels.cjs'),home],{stdio:'inherit'});
  }
  console.log('Portable application UI and captured pixel gates pass at 1× and 2×.');
})().catch(error=>{console.error(error);process.exitCode=1;});
