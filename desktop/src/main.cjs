'use strict';
const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const fs = require('node:fs/promises');
const path = require('node:path');
const http = require('node:http');
const { randomBytes } = require('node:crypto');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { readMachine, validate, writeJSON, freshConfig, machineFile } = require('./machines.cjs');
const { Engine } = require('./engine.cjs');
const catalog = require('./catalog.cjs');
const {paste,plan:pastePlan} = require('./paste.cjs');
let catalogRows=[], downloadJob, pasteJob;
const run = promisify(execFile);

const runtime = process.env.CLASSICMAC_RUNTIME || path.join(process.resourcesPath, 'runtime');
const assets = path.join(__dirname, '..', 'ui');
const token = randomBytes(24).toString('hex');
const machines = new Map();
const engines = new Map();
let window, server, origin, quitting = false;
const indexFile = () => path.join(app.getPath('userData'), 'library.json');
function binary(family) {
  if (process.platform === 'darwin' && process.env.CLASSICMAC_MAC_APP) {
    const base = process.env.CLASSICMAC_MAC_APP;
    return path.join(base, 'Contents/Helpers', family === 'quadra800' ? 'Quadra 800.app/Contents/MacOS/qemu-system-m68k' : 'Power Mac G4.app/Contents/MacOS/qemu-system-ppc');
  }
  return path.join(runtime, 'bin', `qemu-system-${family === 'quadra800' ? 'm68k' : 'ppc'}${process.platform === 'win32' ? '.exe' : ''}`);
}
function imageTool() {
  if (process.platform === 'darwin' && process.env.CLASSICMAC_MAC_APP) return path.join(process.env.CLASSICMAC_MAC_APP, 'Contents/Helpers/Quadra 800.app/Contents/MacOS/qemu-img');
  return path.join(runtime, 'bin', 'qemu-img' + (process.platform === 'win32' ? '.exe' : ''));
}
async function persist() { await writeJSON(indexFile(), [...machines.values()].map(m => m.folder)); }
async function register(folder) {
  const machine = await readMachine(folder);
  const old = machines.get(machine.config.id);
  if (old && old.folder !== machine.folder) throw new Error('A different folder already uses this Mac’s identity. Duplicate it in ClassicMac for Mac before importing both.');
  machines.set(machine.config.id, machine); await persist(); return machine;
}
async function state() {
  const rows = [];
  for (const [id, m] of machines) {
    const e = engines.get(id);
    let preview;
    try {
      const file = await machineFile(m.folder, 'preview.png');
      if ((await fs.stat(file)).size < 5 * 1024 * 1024) preview = 'data:image/png;base64,' + (await fs.readFile(file)).toString('base64');
    } catch { /* Previews are optional. */ }
    rows.push({ ...m.config, folder: m.folder, running: !!e, paused: e?.paused ?? false, preview,
      display: e?.endpoint ? { url: `ws://127.0.0.1:${e.endpoint.webSocketPort}`, password: e.password } : null });
  }
  return rows.sort((a, b) => a.name.localeCompare(b.name));
}
async function notify() {
  if (window && !window.isDestroyed()) window.webContents.send('state', await state());
}
function lookup(id) {
  const machine = machines.get(id);
  if (!machine) throw new Error('This Mac is not in your library.');
  return machine;
}
async function start(id) {
  if (engines.has(id)) return;
  const m = lookup(id);
  // Re-read just before use; imported folders can have been edited or moved.
  m.config = (await readMachine(m.folder)).config;
  for (const key of ['cdImagePath', 'floppyImagePath']) if (m.config[key]) await fs.access(m.config[key]);
  if (m.config.machineFamily === 'quadra800') {
    try { await machineFile(m.folder, m.config.pramImageName); }
    catch { await fs.copyFile(path.join(runtime, 'pram-seed.img'), path.join(m.folder, m.config.pramImageName), require('node:fs').constants.COPYFILE_EXCL); }
  }
  if (m.config.toolsCDInserted) await fs.access(path.join(runtime, 'ClassicMacTools.iso'));
  const e = new Engine(m, runtime, binary(m.config.machineFamily));
  engines.set(id, e);
  e.once('exit', async result => {
    if (engines.get(id) !== e) return;
    engines.delete(id);
    if (result.restart && !quitting) {
      try { await start(id); } catch (error) { window?.webContents.send('problem', error.message); }
    } else {
      if (result.code && !e.stopping) window?.webContents.send('problem', 'The Mac stopped unexpectedly. ' + e.log);
      await notify();
    }
  });
  try { await e.start(); }
  catch (error) { if (engines.get(id) === e) engines.delete(id); throw error; }
  await notify();
}
function handle(name, handler) {
  ipcMain.handle(name, async (event, ...args) => {
    if (event.sender !== window?.webContents || event.senderFrame !== window.webContents.mainFrame || !event.senderFrame.url.startsWith(`${origin}/${token}/`)) throw new Error('Untrusted application request.');
    return handler(...args);
  });
}
function installHandlers() {
  handle('state', state);
  handle('open', async () => {
    const result = await dialog.showOpenDialog(window, { title: 'Open a ClassicMac machine folder', properties: ['openDirectory'] });
    if (result.canceled) return null;
    const m = await register(result.filePaths[0]); await notify(); return m.config.id;
  });
  handle('create', async fields => {
    const c = freshConfig(fields.name, fields.machineFamily);
    const size = Number(fields.diskSizeGB);
    if (![1, 2, 4, 8, 16, 32, 64, 120].includes(size)) throw new Error('Choose a supported disk capacity.');
    c.diskSizeGB = size;
    const result = await dialog.showSaveDialog(window, { title: 'Save New Mac', defaultPath: path.join(app.getPath('documents'), c.name.replace(/[<>:"/\\|?*]/g, '-') + '.classic') });
    if (result.canceled) return null;
    const folder = result.filePath.endsWith('.classic') ? result.filePath : result.filePath + '.classic';
    await fs.mkdir(folder); // No recursive overwrite of an existing machine.
    try {
      await run(imageTool(), ['create', '-f', 'raw', '-o', 'preallocation=off', path.join(folder, 'disk.img'), `${size}G`], { windowsHide: true });
      if (c.machineFamily === 'quadra800') await fs.copyFile(path.join(runtime, 'pram-seed.img'), path.join(folder, 'pram.img'));
      await writeJSON(path.join(folder, 'config.json'), c);
    } catch (error) {
      // This folder was created by this invocation; remove only its known files.
      for (const name of ['disk.img', 'pram.img', 'config.json']) await fs.rm(path.join(folder, name), { force: true });
      await fs.rmdir(folder); throw error;
    }
    const m = await register(folder); await notify(); return m.config.id;
  });
  handle('save', async (id, fields) => {
    const m = lookup(id);
    if (engines.has(id)) throw new Error('Shut this Mac down before changing its settings.');
    const allowed = ['name', 'ramMB', 'width', 'height', 'depth', 'graphicsCard', 'sound', 'networking', 'useG4CPU', 'useEnhancedFramebuffer', 'tabletInput', 'classicInputHelpers', 'toolsCDInserted', 'bootFromCD'];
    const next = { ...m.config };
    for (const key of allowed) if (Object.hasOwn(fields, key)) next[key] = fields[key];
    // Only the Unix hosts provide QEMU's native 9p backend.
    if (fields.disableSharing === true) delete next.sharedFolderPath;
    if (fields.ejectFloppy === true) delete next.floppyImagePath;
    if (fields.ejectCD === true) { delete next.cdImagePath; next.bootFromCD = false; }
    m.config = validate(next); await writeJSON(path.join(m.folder, 'config.json'), m.config); await notify();
  });
  handle('disc', async id => {
    const m = lookup(id);
    if (engines.has(id)) throw new Error('Shut this Mac down before changing its startup disc.');
    const result = await dialog.showOpenDialog(window, { title: 'Choose a CD image', properties: ['openFile'], filters: [{ name: 'Disc images', extensions: ['iso', 'img', 'toast', 'cdr'] }] });
    if (result.canceled) return;
    m.config.cdImagePath = result.filePaths[0]; m.config.bootFromCD = true;
    await writeJSON(path.join(m.folder, 'config.json'), m.config); await notify();
  });
  handle('floppy', async id => {
    const m=lookup(id);if(m.config.machineFamily!=='quadra800')throw new Error('Floppy images require a Quadra.');
    if(engines.has(id))throw new Error('Shut this Mac down before changing its floppy.');
    const result=await dialog.showOpenDialog(window,{title:'Choose a Floppy Image',properties:['openFile'],filters:[{name:'Floppy images',extensions:['img','dsk','ima','raw']}]});
    if(result.canceled)return;m.config.floppyImagePath=result.filePaths[0];await writeJSON(path.join(m.folder,'config.json'),m.config);await notify();
  });
  handle('start', start);
  for (const action of ['pause', 'resume', 'shutdown']) handle(action, async id => {
    const engine = engines.get(id); if (!engine) throw new Error('This Mac is not running.');
    await engine[action](); await notify();
  });
  handle('restart', async id => {
    const engine = engines.get(id); if (!engine) throw new Error('This Mac is not running.');
    await engine.restart();
  });
  handle('forceOff', async id => {
    const engine = engines.get(id); if (!engine) return;
    const result = await dialog.showMessageBox(window, { type: 'warning', message: `Force off “${lookup(id).config.name}”?`, detail: 'Unsaved changes will be lost. Use Shut Down in Mac OS whenever possible.', buttons: ['Cancel', 'Force Off'], defaultId: 0, cancelId: 0 });
    if (result.response === 1) await engine.forceOff();
  });
  handle('reveal', id => shell.showItemInFolder(lookup(id).folder));
  handle('catalog', async () => { catalogRows=await catalog.loadCatalog(); return catalogRows.map(m=>({...m,available:(m.machineFamily||'powerMacG4')==='powerMacG4'})); });
  handle('download', async (id,name) => {
    if(downloadJob)throw new Error('Finish or pause the current download first.');
    const entry=catalogRows.find(m=>m.id===id);if(!entry)throw new Error('Reload the Library and choose an edition.');
    const result=await dialog.showOpenDialog(window,{title:'Save Downloaded Mac In',defaultPath:app.getPath('documents'),properties:['openDirectory','createDirectory']});
    if(result.canceled)return null;
    const controller=new AbortController();downloadJob=controller;
    try{
      const folder=await catalog.install(entry,result.filePaths[0],name,path.join(app.getPath('userData'),'Downloads'),{signal:controller.signal,
        progress:value=>window?.webContents.send('download-progress',value)});
      const m=await register(folder);await notify();return m.config.id;
    }finally{if(downloadJob===controller)downloadJob=null;}
  });
  handle('pauseDownload',()=>downloadJob?.abort());
  handle('paste',async(id,text,slow=false)=>{
    const engine=engines.get(id);if(!engine||engine.paused)throw new Error('Start or resume the Mac before pasting.');
    if(pasteJob)throw new Error('Finish or cancel the current paste first.');
    pastePlan(text);const controller=new AbortController();pasteJob=controller;
    try{await paste(engine,text,{slow:slow===true,signal:controller.signal,progress:(completed,total)=>window?.webContents.send('paste-progress',{completed,total})});}
    finally{if(pasteJob===controller)pasteJob=null;}
  });
  handle('cancelPaste',()=>pasteJob?.abort());
  handle('chooseShare',async id=>{
    if(process.platform==='win32')throw new Error('Host folder sharing requires Linux or macOS.');
    const m=lookup(id);if(engines.has(id))throw new Error('Shut down this Mac before changing sharing.');
    const result=await dialog.showOpenDialog(window,{title:'Share a Folder with This Mac',properties:['openDirectory']});if(result.canceled)return;
    m.config.sharedFolderPath=result.filePaths[0];m.config.sharedVolumeName='Shared Files';await writeJSON(path.join(m.folder,'config.json'),m.config);await notify();
  });
  handle('platform',()=>({name:process.platform,version:app.getVersion()}));
}

async function serve() {
  const mime = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.png': 'image/png', '.ttf': 'font/ttf', '.ico': 'image/x-icon' };
  server = http.createServer(async (req, res) => {
    try {
      if (req.method !== 'GET' || req.headers.host !== new URL(origin).host) { res.writeHead(403); return res.end(); }
      const url = new URL(req.url, origin);
      if (!url.pathname.startsWith(`/${token}/`)) { res.writeHead(404); return res.end(); }
      const relative = decodeURIComponent(url.pathname.slice(token.length + 2));
      const browser = relative.startsWith('browser/');
      const root = browser ? path.join(runtime, 'Browser') : assets;
      const file = path.resolve(root, browser ? relative.slice(8) : relative || 'index.html');
      if (!file.startsWith(root + path.sep)) { res.writeHead(403); return res.end(); }
      const extension = path.extname(file);
      if (!mime[extension] || (await fs.stat(file)).size > 8 * 1024 * 1024) { res.writeHead(404); return res.end(); }
      res.writeHead(200, { 'Content-Type': mime[extension], 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
        'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self' ws://127.0.0.1:*; object-src 'none'; frame-ancestors 'none'" });
      res.end(await fs.readFile(file));
    } catch { res.writeHead(404); res.end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  origin = `http://127.0.0.1:${server.address().port}`;
}

// Set the disposable profile before Electron takes its lock or starts helpers.
if (process.env.CLASSICMAC_TEST_HOME) app.setPath('userData', process.env.CLASSICMAC_TEST_HOME);
if (!app.requestSingleInstanceLock()) app.quit();
else app.whenReady().then(async () => {
  await fs.mkdir(app.getPath('userData'), { recursive: true });
  let folders = [];
  try { folders = JSON.parse(await fs.readFile(indexFile(), 'utf8')); } catch { /* First launch. */ }
  for (const folder of Array.isArray(folders) ? folders.slice(0, 500) : []) {
    try { const m = await readMachine(folder); machines.set(m.config.id, m); } catch { /* Preserve missing entries in the on-disk index until a user makes a change. */ }
  }
  await serve();
  installHandlers();
  window = new BrowserWindow({ width: 1060, height: 840, minWidth: 780, minHeight: 560, title: 'ClassicMac', backgroundColor: '#c0c0c0', autoHideMenuBar: true,
    icon: path.join(assets, 'app.ico'), webPreferences: { preload: path.join(__dirname, 'preload.cjs'), nodeIntegration: false, contextIsolation: true, sandbox: true } });
  window.setMenu(null);
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
  window.webContents.on('will-navigate', (event, url) => { if (url !== `${origin}/${token}/index.html`) event.preventDefault(); });
  window.webContents.on('will-attach-webview', event => event.preventDefault());
  window.on('blur',()=>pasteJob?.abort());
  window.on('close', event => {
    if ((engines.size || downloadJob) && !quitting) {
      event.preventDefault();
      dialog.showMessageBox(window, { type: 'info', message: 'Shut down your running Macs before quitting ClassicMac.', detail: 'Choose Shut Down from the Special menu in Mac OS, or use the Shut Down button. Use Force Off only if a Mac does not respond.' });
    }
  });
  app.on('second-instance', () => { window.show(); window.focus(); });
  await window.loadURL(`${origin}/${token}/index.html`);
  if (process.env.CLASSICMAC_TEST_START) await start(process.env.CLASSICMAC_TEST_START);
  if (process.env.CLASSICMAC_TEST_CYCLE && process.env.CLASSICMAC_TEST_START) {
    const id = process.env.CLASSICMAC_TEST_START, engine = engines.get(id);
    await require('./qualification.cjs').waitForFinder(engine,app.getPath('userData'));
    await engine.pause();
    if ((await engine.command('query-status')).status !== 'paused') throw new Error('Pause failed.');
    await engine.resume();
    if ((await engine.command('query-status')).status !== 'running') throw new Error('Resume failed.');
    await engine.restart();
    const deadline = Date.now() + 60000;
    while ((!engines.has(id) || engines.get(id) === engine || !engines.get(id).initialized) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 100));
    if (!engines.has(id) || engines.get(id) === engine) throw new Error('Restart did not launch a new engine.');
    await require('./qualification.cjs').waitForFinder(engines.get(id),app.getPath('userData'));
  }
  if (process.env.CLASSICMAC_TEST_PARITY === '1') {
    if(!process.env.CLASSICMAC_TEST_HOME||!process.env.CLASSICMAC_TEST_START)throw new Error('Qualification requires an isolated home and fixture.');
    await require('./qualification.cjs').qualify({window,engines,id:process.env.CLASSICMAC_TEST_START,home:app.getPath('userData'),imageTool,version:app.getVersion()});
    app.quit();return;
  }
  if (process.env.CLASSICMAC_TEST_SCREENSHOT) setTimeout(async () => {
    try {
      await fs.writeFile(process.env.CLASSICMAC_TEST_SCREENSHOT, (await window.webContents.capturePage()).toPNG());
      const evidence = await window.webContents.executeJavaScript('window.classicmacEvidence?.()');
      await fs.writeFile(process.env.CLASSICMAC_TEST_SCREENSHOT + '.json', JSON.stringify(evidence, null, 2));
    } catch (error) { console.error(error); }
    if (process.env.CLASSICMAC_TEST_AUTOSTOP) {
      for (const engine of engines.values()) await engine.shutdown();
      const deadline = Date.now() + 40000;
      while (engines.size && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 250));
      await fs.writeFile(process.env.CLASSICMAC_TEST_SCREENSHOT + '.shutdown.json', JSON.stringify({ cleanShutdown: engines.size === 0 }));
      if(engines.size&&process.env.CLASSICMAC_TEST_HOME){for(const e of engines.values())await e.forceOff();app.exit(1);return;}
    }
    if (!engines.size) app.quit();
  }, Number(process.env.CLASSICMAC_TEST_WAIT || 2000));
}).catch(async error => { console.error(error); if(process.env.CLASSICMAC_TEST_HOME)for(const e of engines.values())await e.forceOff(); app.exit(1); });
app.on('window-all-closed', () => { if (!engines.size) app.quit(); });
app.on('quit', () => { quitting = true; downloadJob?.abort(); pasteJob?.abort(); server?.close(); });
