'use strict';
const path = require('node:path');
const { validate } = require('./machines.cjs');
const option = value => String(value).replaceAll(',', ',,');

function argumentsFor(machine, resources, endpoint, platform = process.platform) {
  const c = validate(machine.config);
  if (c.machineFamily === 'powerMac7500') throw new Error('Copland requires the macOS edition. Use that edition for this machine.');
  if (c.sharedFolderPath && platform === 'win32') throw new Error('Host folder sharing requires Linux or macOS. Turn it off in Get Info for Windows.');
  const ppc = c.machineFamily === 'powerMacG4';
  const cdBoot = !!(c.bootFromCD && c.cdImagePath);
  const ati = ppc && !cdBoot && c.graphicsCard === 'atiRadeon';
  const firmware = path.join(resources, 'qemu', 'pc-bios');
  const disk = option(path.join(machine.folder, c.diskImageName));
  const audio = c.sound ? (platform === 'win32' ? 'dsound' : platform === 'darwin' ? 'coreaudio' : 'pa') : 'none';
  const a = ['-L', firmware, '-name', option(c.name), '-m', String(c.ramMB), '-audiodev', `${audio},id=snd0`,
    '-display', `vnc=127.0.0.1:${endpoint.vncPort - 5900},websocket=127.0.0.1:${endpoint.webSocketPort},password=on,share=force-shared`,
    '-qmp', 'stdio', '-monitor', 'none', '-serial', 'none', '-action', 'reboot=shutdown'];
  if (ppc) {
    a.push('-device', 'usb-mouse,id=classicmac-game-mouse', '-accel', 'tcg,tb-size=512', '-M', 'mac99,via=cuda,audiodev=snd0', '-cpu', c.useG4CPU ? '7400' : 'g3',
      '-global', `macio-ide.dma-completion-delay-ns=${cdBoot ? 1000000 : 0}`, '-prom-env', 'output-device=ttya',
      '-drive', `file=${disk},format=raw,media=disk`);
    if (ati) {
      const w = Math.min(1920, Math.max(640, Math.floor(c.width / 8) * 8));
      const h = Math.min(1440, Math.max(480, c.height));
      a.push('-vga', 'none', '-device', `ati-vga,id=ati0,model=r100,vgamem_mb=32,romfile=,guest_hwcursor=on,xres=${w},yres=${h},refresh_rate=60000`,
        '-g', '640x480x32', '-prom-env', `classicmac-display=${w}x${h}`, '-prom-env', `retroboot-display=${w}x${h}`);
    } else {
      a.push('-vga', 'std', '-global', 'VGA.host-resize=on', '-global', 'VGA.vgamem_mb=64',
        '-global', 'VGA.packed-lowbpp=on', '-global', 'VGA.untracked-vram=on', '-global', 'VGA.hardware-cursor=on',
        '-g', `${c.width}x${c.height}x${c.depth === 32 ? 32 : 15}`);
      if (!cdBoot) a.push('-icount', 'shift=4,sleep=off', '-global', 'VGA.classicmac-boot-handoff=on');
    }
    a.push('-drive', `if=ide,index=${cdBoot ? 2 : 3},media=cdrom,id=cd0,readonly=on${c.cdImagePath ? `,file=${option(c.cdImagePath)},format=raw` : ''}`,
      '-drive', `if=ide,index=${cdBoot ? 3 : 2},media=cdrom,id=tools0,readonly=on`);
    const tools = c.toolsCDInserted && !cdBoot;
    if (tools) a.push('-drive', `if=none,id=classicmac-tools,file=${option(path.join(resources, 'ClassicMacTools.iso'))},format=raw,readonly=on`, '-device', 'virtio-blk-pci,drive=classicmac-tools');
    if (c.tabletInput || tools) {
      a.push('-device', `loader,addr=0x4000000,file=${option(path.join(resources, 'ndrvloader'))}`, '-prom-env', 'boot-command=init-program go');
      if (c.tabletInput) a.push('-device', 'virtio-tablet-pci');
    }
    // Mac OS 8.5/8.6 need the same early Virtio CD mirror as the Mac app.
    if (cdBoot && !c.useG4CPU) {
      if (!c.tabletInput && !tools) a.push('-device', `loader,addr=0x4000000,file=${option(path.join(resources, 'ndrvloader'))}`, '-prom-env', 'boot-command=init-program go');
      a.push('-drive', `if=none,id=bootcd,file=${option(c.cdImagePath)},format=raw,readonly=on`, '-device', 'virtio-blk-pci,drive=bootcd', '-prom-env', 'boot-device=virtio0:\\:tbxi');
    } else if (cdBoot) a.push('-boot', 'd');
  } else {
    a.push('-M', `q800${c.useEnhancedFramebuffer ? ',fb=none' : ''},audiodev=snd0`, '-bios', path.join(resources, 'Quadra800.rom'),
      '-drive', `file=${option(path.join(machine.folder, c.pramImageName))},format=raw,if=mtd`,
      '-device', 'scsi-hd,scsi-id=0,drive=hd0', '-drive', `file=${disk},format=raw,if=none,id=hd0`,
      '-device', 'scsi-cd,scsi-id=3,drive=cd0', '-drive', `media=cdrom,if=none,id=cd0,readonly=on${c.cdImagePath ? `,file=${option(c.cdImagePath)},format=raw` : ''}`,
      '-device', 'scsi-cd,scsi-id=4,drive=tools0', '-drive', `media=cdrom,if=none,id=tools0,readonly=on${c.toolsCDInserted ? `,file=${option(path.join(resources, 'ClassicMacTools.iso'))},format=raw` : ''}`,
      '-device', `nubus-virtio-mmio,romfile=${option(path.join(resources, 'declrom'))}`,
      '-drive', `if=none,id=fd0${c.floppyImagePath ? `,file=${option(c.floppyImagePath)},format=raw` : ',media=cdrom,readonly=off'}`, '-device', 'virtio-blk-device,drive=fd0,removable=on');
    if (c.useEnhancedFramebuffer) a.push('-device', `nubus-qfb,width=${c.width},height=${c.height},depth=${c.depth}`);
    else a.push('-g', `${c.width}x${c.height}x${c.depth}`);
    if (c.tabletInput) a.push('-device', 'virtio-tablet-device');
    if (cdBoot) a.push('-boot', 'd');
  }
  if (c.sharedFolderPath) {
    const tag = c.sharedVolumeName || 'Shared Files';
    if (typeof tag !== 'string' || tag.length > 31 || /[\x00-\x1f]/.test(tag)) throw new Error('Invalid shared volume name.');
    a.push('-fsdev', `local,id=share0,security_model=none,path=${option(c.sharedFolderPath)}`,
      '-device', `${ppc ? 'virtio-9p-pci' : 'virtio-9p-device'},fsdev=share0,mount_tag=${option(tag)}`);
    if (ppc && !c.tabletInput && !c.toolsCDInserted) a.push('-device', `loader,addr=0x4000000,file=${option(path.join(resources, 'ndrvloader'))}`, '-prom-env', 'boot-command=init-program go');
  }
  a.push('-nic', c.networking ? `user,model=${ppc ? 'sungem' : 'dp83932'}` : 'none');
  return a;
}
module.exports = { argumentsFor, option };
