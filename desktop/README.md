# Windows and Linux desktop

ClassicMac 4.1 uses native x64 QEMU engines and a shared Electron desktop on
Windows 11 and Ubuntu 24.04+/Debian 13. Machine folders, Iconfactory artwork,
bitmap fonts, firmware, timing fixes and custom devices are shared with the
Mac edition. QMP uses private process pipes; the password-protected display
listens only on loopback. The renderer is sandboxed with context isolation.

Unzip the Windows download and run `ClassicMac.exe`, keeping its companion
files. Windows binaries are unsigned. On Linux install the DEB with
`sudo apt install ./ClassicMac-4.1.1-linux-amd64.deb`, or extract the tar.gz and
run `classicmac`. The tar distribution requires a host with working Chromium
sandboxing; the DEB installs the sandbox helper with its standard ownership.
Linux builds require glibc 2.39 or newer and host desktop/audio libraries.

## Platform features

Both editions provide Power Mac G4 and Quadra 800 machines, creation/import,
Library downloads with resume and SHA-256 verification, settings, CD/floppy
images, power controls and Mac US/MacRoman Paste Text. Import an existing
`.classic` folder only after shutting it down on its original computer.
Replace unavailable host-specific media paths in Get Info before starting.

Linux supports classicvirtio 9p host folder sharing. Windows QEMU does not
provide that backend, so folder sharing is unavailable there. Copland's
Power Mac 7500 engine remains macOS-only. ATI 3D uses WGL OpenGL 4.1 on
Windows and EGL/GLES 3 on Linux; hosts without a suitable graphics driver can
use 3D off. Virtual-machine testing does not qualify physical GPU drivers,
audio output or accelerated game compatibility. See the
[qualification report](../docs/desktop-release-validation.md) for measured results.
The shared CSS interface has not passed the native Mac Platinum pixel gate.

Use guest Shut Down or the toolbar before quitting. A nonresponsive guest
remains running until Force Off is explicitly confirmed.

## Build and test

Install Node 24, then the native dependencies in the matching workflow.
Use MSYS2 UCRT64 on Windows and a supported Linux host on Linux:

```sh
# Windows, from MSYS2 UCRT64
desktop/scripts/build-engine.sh
# Linux
desktop/scripts/build-linux.sh
```

Place the qualified `ClassicMacTools.iso` in `output/desktop/runtime/`.
The workflows extract it from the checksum-pinned stable 4.0.0 ZIP.

```sh
cd desktop
npm ci
npm test
npm start
npm run package:win    # on Windows
npm run package:linux  # on Linux
```

For development, set `CLASSICMAC_RUNTIME` to the absolute runtime directory.
Packages automatically use `resources/runtime`. Native workflows retain the
matching patched QEMU source. Linux staging bundles non-system ELF libraries
and their copyright notices; glibc, desktop, audio and GPU drivers stay on
the host. Corresponding sources accompany the published release.

`npm test` covers launch profiles, settings, path/symlink boundaries, bounded
archive extraction, download resumption and paste planning/cancellation.
`tests/smoke-desktop.cjs` boots a disposable checksum-pinned OS9 fixture and
checks display, pause/resume, restart/reconnect and normal shutdown.
`tests/proxmox-desktop.cjs` additionally checks real Library installation,
MacRoman input in the native guest probe and Linux shared-file round trips.
It requires an explicit disposable output directory and the Retro68-built
`tests/guest` probe; it does not use an existing user machine.

IPC checks the sender against the sole local app window, following
[Electron's security guidance](https://www.electronjs.org/docs/latest/tutorial/security/).
No remote website receives filesystem or process APIs.
