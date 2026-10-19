import RFB from './browser/novnc/core/rfb.js';
const api = window.classicmac;
const $ = id => document.getElementById(id);
const model = { powerMacG4: 'Power Mac G4', quadra800: 'Quadra 800', powerMac7500: 'Power Mac 7500' };
const icon = { powerMacG4: 'powermac_g4', quadra800: 'quadra_800', powerMac7500: 'powermac_7500' };
let relativePointer = false;
let machines = [], selected, rfb, displayID, displayKey, connectTimer, creating = false, connectionGeneration = 0;
const current = () => machines.find(m => m.id === selected);
function message(text) { $('message-text').textContent = text; if (!$('message').open) $('message').showModal(); }
async function action(fn) { try { await fn(); } catch (error) { message(error.message); } }
$('message-ok').onclick = () => $('message').close();
function updateGameMouse() {
  const captured = document.pointerLockElement === $('display').querySelector('canvas');
  $('capture').hidden = !relativePointer;
  $('capture').textContent = captured ? 'Release game mouse' : 'Capture game mouse';
  $('capture').setAttribute('aria-pressed', String(captured));
  if (relativePointer) $('notice').textContent = captured ? 'Mouse captured. Press Escape to release it.' : 'Click the Mac screen to capture the mouse for this game. Press Escape to release it.';
}
function captureGameMouse() {
  const canvas = $('display').querySelector('canvas');
  if (relativePointer && canvas) { rfb?.focus(); canvas.requestPointerLock(); }
}
$('capture').onclick = () => document.pointerLockElement ? document.exitPointerLock() : captureGameMouse();
document.addEventListener('pointerlockchange', updateGameMouse);
document.addEventListener('pointerlockerror', () => message('Mouse capture failed. Click the Mac screen to try again.'));
$('display').addEventListener('mousedown', event => {
  if (relativePointer && document.pointerLockElement !== $('display').querySelector('canvas')) {
    event.preventDefault(); event.stopPropagation(); captureGameMouse();
  }
}, true);
function connect(m) {
  if (document.pointerLockElement) document.exitPointerLock();
  relativePointer = false; updateGameMouse();
  const generation = ++connectionGeneration;
  clearTimeout(connectTimer);
  if (rfb) { rfb.disconnect(); rfb = null; }
  displayID = m?.running ? m.id : null;
  $('curtain').hidden = false;
  if (!m?.running || !m.display) return;
  const id = m.id;
  rfb = new RFB($('display'), m.display.url, { credentials: { password: m.display.password }, shared: true });
  rfb.scaleViewport = true; rfb.resizeSession = false; rfb.qualityLevel = 8; rfb.compressionLevel = 2;
  rfb.classicInputHelpers = m.classicInputHelpers;
  rfb.addEventListener('connect', () => { if (generation === connectionGeneration && displayID === id) { $('curtain').hidden = true; rfb.focus(); } });
  rfb.addEventListener('disconnect', () => { if (generation === connectionGeneration && displayID === id && current()?.running) connectTimer = setTimeout(() => connect(current()), 1000); });
  rfb.addEventListener('pointertypechange', event => {
    if (generation !== connectionGeneration) return;
    relativePointer = event.detail.relative;
    if (!relativePointer && document.pointerLockElement) document.exitPointerLock();
    updateGameMouse();
  });
  rfb.addEventListener('securityfailure', event => message(event.detail.reason || 'The Mac’s display could not be authenticated.'));
}
function render() {
  const list = $('machines'); list.replaceChildren();
  for (const m of machines) {
    const row = document.createElement('button'); row.className = m.id === selected ? 'selected' : '';
    row.setAttribute('role', 'option'); row.setAttribute('aria-selected', String(m.id === selected));
    const image = new Image(32, 32); image.src = `${icon[m.machineFamily]}-32.png`; image.alt = '';
    const text = document.createElement('span'); text.className = 'machine-description';
    const name = document.createElement('strong'); name.textContent = m.name;
    const detail = document.createElement('small'); detail.textContent = m.paused ? 'Paused' : m.running ? 'Running now' : model[m.machineFamily];
    text.append(name, detail); row.append(image, text); row.onclick = () => { selected = m.id; render(); }; list.append(row);
  }
  const m = current();
  $('name').textContent = m?.name ?? 'Your classic Macs, on your PC.';
  $('status').textContent = m?.paused ? 'Paused' : m?.running ? 'Running now' : 'Ready';
  $('details').replaceChildren();
  if (m) for (const [name, value] of [['Model', model[m.machineFamily]], ['Memory', `${m.ramMB} MB`], ['Hard disk', `${m.diskSizeGB ?? '—'} GB`], ['Screen', `${m.width} × ${m.height}`], ['Graphics', m.graphicsCard === 'atiRadeon' ? 'ATI Radeon' : 'Standard'], ['Sharing', m.sharedFolderPath ? 'On' : 'Off']]) {
    const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = name + ':'; dd.textContent = value; $('details').append(dt, dd);
  }
  for (const id of ['start', 'settings', 'disc', 'reveal', 'share']) $(id).disabled = !m || (id !== 'reveal' && m.machineFamily === 'powerMac7500');
  for (const id of ['pause', 'restart', 'stop', 'force', 'fullscreen', 'paste']) $(id).disabled = !m?.running;
  $('settings').disabled ||= !!m?.running; $('disc').disabled ||= !!m?.running;
  $('floppy').disabled=!m||m.machineFamily!=='quadra800'||m.running;
  $('share').disabled=!m||m.running;
  $('start').textContent = m?.paused ? 'Resume Mac' : m?.running ? 'Show Mac' : 'Start Mac';
  $('pause').textContent = m?.paused ? 'Resume' : 'Pause';
  const nextDisplay = m?.running ? `${m.id}:${m.display?.url}` : null;
  if (nextDisplay !== displayKey) { displayKey = nextDisplay; connect(m); }
  $('notice').textContent = m?.running ? 'Use Shut Down from Mac OS before closing ClassicMac. Force Off is for an unresponsive Mac.' : m ? 'Choose Start Mac, or use Media to select an installation CD.' : 'Open a .classic folder from your Mac, or create a new Mac.';
  updateGameMouse();
}
api.onState(rows => { machines = rows; selected ??= rows[0]?.id; render(); });
api.onProblem(message);
$('open').onclick = () => action(async () => { const id = await api.open(); if (id) selected = id; machines = await api.state(); render(); });
$('start').onclick = () => action(async () => { const m = current(); if (m?.paused) await api.resume(m.id); else if (!m?.running) await api.start(m.id); else $('display').focus(); });
$('pause').onclick = () => action(() => current().paused ? api.resume(selected) : api.pause(selected));
$('stop').onclick = () => action(() => api.shutdown(selected));
$('restart').onclick = () => action(() => api.restart(selected));
$('force').onclick = () => action(() => api.forceOff(selected));
$('disc').onclick = () => action(() => api.disc(selected));
$('floppy').onclick=()=>action(()=>api.floppy(selected));
$('reveal').onclick = () => action(() => api.reveal(selected));
$('fullscreen').onclick = () => action(() => document.fullscreenElement ? document.exitFullscreen() : $('display').requestFullscreen());
let downloading=false,pasting=false;
const platform=await api.platform();
$('share').hidden=platform.name==='win32';
$('share').onclick=()=>action(()=>api.chooseShare(selected));
$('about').onclick=()=>message(`ClassicMac ${platform.version} — ${platform.name==='win32'?'Windows':'Linux'}\n\nClassic Macintosh emulation with QEMU and the Platinum appearance.\n\nMac icons © The Iconfactory, included with permission. Fonts and appearance shared with RetroBoot.\n\nCopland remains available in the macOS edition. Host folder sharing requires Linux or macOS.`);
$('library').onclick=()=>action(async()=>{
 $('catalog').showModal();$('editions').textContent='Loading Library…';
 const rows=await api.catalog();$('editions').replaceChildren();
 for(const entry of rows){
  const section=document.createElement('section'),title=document.createElement('strong'),summary=document.createElement('p'),button=document.createElement('button');
  title.textContent=entry.name;summary.textContent=entry.summary;button.textContent=entry.available?'Download…':'Requires macOS Copland engine';button.disabled=!entry.available;
  button.onclick=()=>action(async()=>{
   if(downloading)throw new Error('Pause or finish the current download first.');downloading=true;
   try{const id=await api.download(entry.id,entry.name);if(id){selected=id;machines=await api.state();render();$('download-status').textContent='Installed. Choose Start Mac to begin.';}}
   finally{downloading=false;}
  });
  section.append(title,summary,button);$('editions').append(section);
 }
});
$('pause-download').onclick=()=>api.pauseDownload();
$('close-catalog').onclick=()=>{if(downloading)message('Pause or finish the download before closing.');else $('catalog').close();};
$('catalog').addEventListener('cancel',event=>{if(downloading)event.preventDefault();});
api.onDownloadProgress(p=>{$('download-status').textContent=`${p.phase}… ${Math.floor(p.completed/p.total*100)}%`;$('download-progress').value=p.completed/p.total;});
$('paste').onclick=()=>{$('paste-status').textContent='';$('paste-dialog').showModal();};
$('send-paste').onclick=()=>action(async()=>{if(pasting)return;pasting=true;$('send-paste').disabled=true;try{await api.paste(selected,$('paste-text').value,$('slow-paste').checked);$('paste-status').textContent='Finished.';}finally{pasting=false;$('send-paste').disabled=false;}});
$('cancel-paste').onclick=()=>{api.cancelPaste();if(!pasting)$('paste-dialog').close();};
$('paste-dialog').addEventListener('cancel',()=>api.cancelPaste());
api.onPasteProgress(p=>{$('paste-status').textContent=`Typed ${p.completed} of ${p.total} characters.`;});

function edit(create) {
  creating = create; const form = $('editor').querySelector('form'); form.reset();
  $('editor').className = create ? 'creating' : 'editing'; $('editor-title').textContent = create ? 'New Mac' : 'Get Info';
  const m = create ? { name: 'Mac OS 9' } : current();
  for (const field of form.elements) {
    if (!field.name || !Object.hasOwn(m, field.name)) continue;
    if (field.type === 'checkbox') field.checked = m[field.name]; else field.value = m[field.name];
  }
  if (!create) form.elements.resolution.value = `${m.width}x${m.height}`;
  $('editor').showModal();
}
$('new').onclick = () => edit(true); $('settings').onclick = () => edit(false);
$('editor').querySelector('form').addEventListener('submit', event => {
  if (event.submitter?.value !== 'save') return;
  event.preventDefault();
  action(async () => {
    const form = event.currentTarget, fields = Object.fromEntries(new FormData(form));
    if (creating) { const id = await api.create(fields); if (id) selected = id; }
    else {
      for (const input of form.querySelectorAll('input[type=checkbox]')) fields[input.name] = input.checked;
      for (const key of ['ramMB', 'depth']) fields[key] = Number(fields[key]);
      [fields.width, fields.height] = fields.resolution.split('x').map(Number);
      await api.save(selected, fields);
    }
    $('editor').close(); machines = await api.state(); render();
  });
});
window.classicmacEvidence = () => ({ machines: machines.map(m => ({ id: m.id, running: m.running })), connected: $('curtain').hidden, canvas: !!$('display').querySelector('canvas'), selected });
machines = await api.state(); selected = machines[0]?.id; render();
