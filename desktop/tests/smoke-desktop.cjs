'use strict';
// Runs only against a disposable, checksum-pinned public OS9 fixture. It never
// opens the user's library and retains no guest disk in CI artifacts.
const fs = require('node:fs/promises');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');
const { spawn, execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { pipeline } = require('node:stream/promises');
const { Readable } = require('node:stream');
const { createReadStream, createWriteStream } = require('node:fs');
const run = promisify(execFile);

(async () => {
  const exe = path.resolve(process.argv[2]);
  const output = path.resolve(process.argv[3]);
  await fs.mkdir(output, { recursive: true });
  const archive = path.join(output, 'fixture.tar.gz');
  const response = await fetch('https://mcchord.net/classicmac/mac-os-9.2.1-gxmetal-2.3.0-v1.tar.gz');
  if (!response.ok || !response.body) throw new Error('OS9 fixture download failed.');
  await pipeline(Readable.fromWeb(response.body), createWriteStream(archive));
  const hash = createHash('sha256'); for await (const chunk of createReadStream(archive)) hash.update(chunk);
  if (hash.digest('hex') !== '9dedbf11c866b18c8d9bc54843f42872fbdb7fe9838c75fad2cc6abdb50fd5cb') throw new Error('OS9 fixture integrity mismatch.');
  const fixture = path.join(output, 'Windows Test.classic'); await fs.mkdir(fixture);
  await run('tar', ['-xzf', archive, '-C', fixture], { windowsHide: true });
  const configFile = path.join(fixture, 'config.json');
  const config = JSON.parse(await fs.readFile(configFile, 'utf8'));
  Object.assign(config, { id: randomUUID(), name: process.platform === 'win32' ? 'Windows OS 9 Test' : 'Linux OS 9 Test', machineFamily: 'powerMacG4', graphicsCard: 'standard', sound: false, networking: false, toolsCDInserted: false, tabletInput: false, bootFromCD: false });
  for (const key of ['sharedFolderPath', 'cdImagePath', 'floppyImagePath']) delete config[key];
  await fs.writeFile(configFile, JSON.stringify(config));
  const home = path.join(output, 'isolated-home'); await fs.mkdir(home);
  await fs.writeFile(path.join(home, 'library.json'), JSON.stringify([fixture]));
  const screenshot = path.join(output, 'desktop.png');
  const env = { ...process.env, CLASSICMAC_TEST_HOME: home, CLASSICMAC_TEST_START: config.id,
    CLASSICMAC_TEST_SCREENSHOT: screenshot, CLASSICMAC_TEST_WAIT: '2000', CLASSICMAC_TEST_AUTOSTOP: '1', CLASSICMAC_TEST_CYCLE: '1' };
  delete env.CLASSICMAC_RUNTIME; delete env.CLASSICMAC_MAC_APP;
  const child = spawn(exe, ['--disable-gpu'], { env, stdio: ['ignore', 'pipe', 'pipe'] });
  const log = createWriteStream(path.join(output, 'desktop.log')); child.stdout.pipe(log); child.stderr.pipe(log);
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      if (process.platform === 'win32') run('taskkill', ['/pid', String(child.pid), '/t', '/f']).catch(() => {}); else child.kill('SIGTERM');
      reject(new Error('Packaged desktop desktop smoke test timed out.'));
    }, 700000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('exit', code => { clearTimeout(timer); if (code !== 0) reject(new Error(`Desktop exited with ${code}`)); else resolve(); });
  });
  const display = JSON.parse(await fs.readFile(screenshot + '.json', 'utf8'));
  const shutdown = JSON.parse(await fs.readFile(screenshot + '.shutdown.json', 'utf8'));
  if (!display.connected || !display.canvas || !shutdown.cleanShutdown) throw new Error('Guest display or clean shutdown failed.');
  if (!display.toolbar?.fitsWidth || !display.toolbar?.singleLineButtons || !display.toolbar?.bottomVisible) throw new Error('Toolbar controls wrap, overflow or are clipped in the default window.');
  console.log('Packaged desktop app: guest boot/display and clean shutdown passed. Inspect desktop.png for Finder.');
})().catch(error => { console.error(error); process.exitCode = 1; });
