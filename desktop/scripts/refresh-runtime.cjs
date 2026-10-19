'use strict';
// Engine caches contain binaries. Always refresh the current app's notices
// and artwork before packaging, then regenerate the integrity inventory.
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const runtime = path.join(root, 'output/desktop/runtime');
if (!fs.existsSync(path.join(runtime, 'bin/qemu-system-ppc' + (process.platform === 'win32' ? '.exe' : '')))) throw new Error('Build the desktop engines first.');
fs.cpSync(path.join(root, 'Resources/Iconfactory'), path.join(runtime, 'Iconfactory'), { recursive: true });
fs.copyFileSync(path.join(root, 'platinum/fonts/LICENSE-fonts.txt'), path.join(runtime, 'Licenses/Platinum-fonts.txt'));
fs.copyFileSync(path.join(root, 'THIRD_PARTY_NOTICES.md'), path.join(runtime, 'ThirdPartyNotices.md'));
for (const [source, name] of [['README.txt', 'MacOS-icons.txt'], ['manifest.json', 'MacOS-icon-provenance.json']]) {
  fs.copyFileSync(path.join(root, 'platinum/macos-icons', source), path.join(runtime, 'Licenses', name));
}
const manifest = {};
function visit(dir) {
  for (const item of fs.readdirSync(dir, { withFileTypes: true })) {
    const file = path.join(dir, item.name);
    if (item.isDirectory()) visit(file);
    else if (item.isFile() && item.name !== 'runtime-sha256.json') {
      manifest[path.relative(runtime, file).split(path.sep).join('/')] = createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    }
  }
}
visit(runtime);
fs.writeFileSync(path.join(runtime, 'runtime-sha256.json'), JSON.stringify(manifest, null, 2) + '\n');
