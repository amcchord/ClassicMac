const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { validate, freshConfig, readMachine } = require('../src/machines.cjs');
const { argumentsFor } = require('../src/arguments.cjs');
const ports = { vncPort: 61000, webSocketPort: 61001 };
const make = config => ({ folder: '/test/Mac, One.classic', config });
test('Power Mac ATI uses real time, Windows audio and authenticated loopback display', () => {
  const c = { ...freshConfig('Deluxe'), graphicsCard: 'atiRadeon' };
  const a = argumentsFor(make(c), '/resources', ports, 'win32');
  assert(!a.includes('-icount')); assert(a.includes('dsound,id=snd0'));
  assert(a.some(x => x.includes('ati-vga,id=ati0,model=r100')));
  assert(a.some(x => x.includes('websocket=127.0.0.1:61001,password=on')));
  assert(a.includes('usb-mouse,id=classicmac-game-mouse'));
  assert(a.includes('stdio')); assert(!a.some(x => x.startsWith('tcp:')));
  assert(a.some(x => x.includes('Mac,, One.classic')));
});
test('Installer starts force Standard graphics and retain DMA safeguard', () => {
  const c = { ...freshConfig('Install'), bootFromCD: true, cdImagePath: 'C:\\Images\\Mac, OS.iso', graphicsCard: 'atiRadeon' };
  const a = argumentsFor(make(c), '/resources', ports, 'win32');
  assert(!a.some(x => x.includes('ati-vga'))); assert(!a.includes('-icount'));
  assert(a.includes('macio-ide.dma-completion-delay-ns=1000000'));
  assert(a.some(x => x.includes('Mac,, OS.iso'))); assert(a.includes('-boot'));
});
test('Quadra has PRAM, SCSI, ordered NuBus transport and framebuffer', () => {
  const a = argumentsFor(make(freshConfig('Quadra', 'quadra800')), '/resources', ports, 'win32');
  assert(a.includes('q800,fb=none,audiodev=snd0')); assert(a.includes('scsi-hd,scsi-id=0,drive=hd0'));
  assert(a.findIndex(x => x.startsWith('nubus-virtio-mmio,')) < a.findIndex(x => x.startsWith('nubus-qfb,')));
  assert(a.some(x => x.includes('if=mtd'))); assert(a.includes('virtio-tablet-device'));
});
test('Unported features fail clearly instead of silently changing machines', () => {
  assert.throws(() => argumentsFor(make(freshConfig('Copland', 'powerMac7500')), '/r', ports), /Copland/);
  assert.throws(() => argumentsFor(make({ ...freshConfig('Shared'), sharedFolderPath: '/host' }), '/r', ports, 'win32'), /sharing/);
});
test('Malformed or escaping machine settings are rejected', () => {
  for (const name of ['../disk.img', 'C:\\disk.img', '/disk.img', '..', 'disk\0.img']) {
    assert.throws(() => validate({ ...freshConfig('Mac'), diskImageName: name }));
  }
  assert.throws(() => validate({ ...freshConfig('Mac'), sound: 'yes' }));
  assert.throws(() => validate({ ...freshConfig('Mac'), ramMB: 1024 }));
});
test('Imported disk symlinks cannot escape a machine package', async t => {
  const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'classicmac-test-'));
  t.after(() => fs.rm(temp, { recursive: true, force: true }));
  const folder = path.join(temp, 'Mac.classic'); await fs.mkdir(folder);
  await fs.writeFile(path.join(folder, 'config.json'), JSON.stringify(freshConfig('Mac')));
  await fs.writeFile(path.join(temp, 'outside.img'), 'outside');
  try { await fs.symlink(path.join(temp, 'outside.img'), path.join(folder, 'disk.img')); }
  catch (error) { if (error.code === 'EPERM') return t.skip('Windows account cannot create symlinks'); throw error; }
  await assert.rejects(readMachine(folder), /inside/);
});

test('Linux sharing uses the native 9p device and driver loader', () => {
 const a = argumentsFor(make({...freshConfig('Linux'),sharedFolderPath:'/host,files',tabletInput:false}),'/r',ports,'linux');
 assert(a.some(x=>x.includes('local,id=share0,security_model=none,path=/host,,files')));
 assert(a.some(x=>x.includes('virtio-9p-pci'))); assert(a.some(x=>x.includes('loader,addr=')));
});
