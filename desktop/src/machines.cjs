'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const { randomUUID } = require('node:crypto');

function localName(value, fallback) {
  const name = value ?? fallback;
  if (typeof name !== 'string' || !name || name.length > 100 || /[\\/:\x00-\x1f]/.test(name) || name === '.' || name === '..') {
    throw new Error('A machine file name must stay inside its .classic folder.');
  }
  return name;
}
function validate(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('Invalid machine configuration.');
  const c = { ...value };
  if (!/^[a-f\d]{8}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{4}-[a-f\d]{12}$/i.test(c.id ?? '')) throw new Error('Invalid machine identity.');
  if (!['powerMacG4', 'quadra800', 'powerMac7500'].includes(c.machineFamily)) throw new Error('Unknown Mac model.');
  if (typeof c.name !== 'string' || !c.name.trim() || c.name.length > 100 || /[\x00-\x1f]/.test(c.name)) throw new Error('Give your Mac a name of up to 100 characters.');
  c.name = c.name.trim();
  const limits = c.machineFamily === 'quadra800' ? [16, 1000] : c.machineFamily === 'powerMac7500' ? [32, 32] : [32, 896];
  c.ramMB ??= c.machineFamily === 'quadra800' ? 128 : c.machineFamily === 'powerMac7500' ? 32 : 512;
  if (!Number.isInteger(c.ramMB) || c.ramMB < limits[0] || c.ramMB > limits[1]) throw new Error('Memory is outside this Mac’s supported range.');
  for (const [key, fallback, max] of [['width', 1024, 3840], ['height', 768, 2160]]) {
    c[key] ??= fallback;
    if (!Number.isInteger(c[key]) || c[key] < 480 || c[key] > max) throw new Error('Unsupported screen size.');
  }
  c.depth ??= 16;
  if (![1, 2, 4, 8, 16, 32].includes(c.depth)) throw new Error('Unsupported color depth.');
  c.diskImageName = localName(c.diskImageName, 'disk.img');
  c.pramImageName = localName(c.pramImageName, 'pram.img');
  c.graphicsCard ??= 'standard';
  if (!['standard', 'atiRadeon'].includes(c.graphicsCard)) throw new Error('Unknown graphics card.');
  for (const key of ['sound', 'networking', 'tabletInput', 'classicInputHelpers', 'useG4CPU', 'useEnhancedFramebuffer']) c[key] ??= true;
  for (const key of ['bootFromCD', 'toolsCDInserted']) c[key] ??= false;
  c.useVirtioDisk ??= false;
  for (const key of ['sound', 'networking', 'tabletInput', 'classicInputHelpers', 'useG4CPU', 'useVirtioDisk', 'useEnhancedFramebuffer', 'bootFromCD', 'toolsCDInserted']) {
    if (typeof c[key] !== 'boolean') throw new Error(`Invalid ${key} option.`);
  }
  if (c.machineFamily !== 'powerMacG4') c.useVirtioDisk = false;
  for (const key of ['cdImagePath', 'floppyImagePath', 'sharedFolderPath']) {
    if (c[key] != null && (typeof c[key] !== 'string' || c[key].includes('\0'))) throw new Error(`Invalid ${key}.`);
  }
  return c;
}
async function machineFile(folder, name) {
  const root = await fs.realpath(folder);
  const file = await fs.realpath(path.join(root, localName(name)));
  if (path.dirname(file) !== root) throw new Error('Machine files must be inside the .classic folder.');
  if (!(await fs.stat(file)).isFile()) throw new Error('A machine disk must be a regular file.');
  return file;
}
async function readMachine(folder) {
  const configPath = await machineFile(folder, 'config.json');
  if ((await fs.stat(configPath)).size > 65536) throw new Error('The machine configuration is too large.');
  const config = validate(JSON.parse(await fs.readFile(configPath, 'utf8')));
  await machineFile(folder, config.diskImageName);
  return { folder: await fs.realpath(folder), config };
}
async function writeJSON(file, value) {
  const temporary = `${file}.${randomUUID()}.tmp`;
  try { await fs.writeFile(temporary, JSON.stringify(value, null, 2) + '\n', { flag: 'wx' }); await fs.rename(temporary, file); }
  finally { await fs.rm(temporary, { force: true }); }
}
function freshConfig(name, family = 'powerMacG4') {
  return validate({ id: randomUUID(), name, machineFamily: family, ramMB: family === 'quadra800' ? 128 : family === 'powerMac7500' ? 32 : 512, diskSizeGB: family === 'quadra800' ? 2 : 32 });
}
module.exports = { localName, validate, machineFile, readMachine, writeJSON, freshConfig };
