# Windows and Linux desktop

ClassicMac 4.5 uses native x64 QEMU engines and a shared Platinum desktop on
Windows 11 and Ubuntu 24.04+/Debian 13. It draws the Mac app’s measured window
frames, controls and bitmap fonts directly, including title widgets and
WindowShade. The home layout follows the Mac app. There is no host menu bar;
New Mac, Open Mac, Library and About are available in the window.

On Windows, download **ClassicMac-4.5.0-windows-x64.exe** and run it. It is a
single portable executable: no installer or companion files. Its bundled
runtime is unpacked into a temporary directory while running. Windows
binaries are unsigned.

On Linux, download **ClassicMac-4.5.0-linux-x64.AppImage**, mark it executable
(`chmod +x ClassicMac-4.5.0-linux-x64.AppImage`) and run it. A DEB is also
available for system installation. Linux builds require glibc 2.39 or newer
and host desktop/audio libraries. AppImage mounting uses FUSE 3; on a minimal
host, `./ClassicMac-4.5.0-linux-x64.AppImage --appimage-extract-and-run`
runs the same single file without FUSE. Machine folders and application preferences
are saved separately, so replacing the executable preserves your Macs.

QMP uses private process pipes; the password-protected display listens only
on loopback. The renderer uses context isolation and has no Node APIs. The
AppImage launcher uses Chromium sandboxing when the host permits user
namespaces, with its standard fallback on hosts that restrict them.

On first launch, a four-step wizard explains software license responsibility,
then lets you download a pre-installed Mac or use your own installation CD
image. It saves your Mac in Documents/ClassicMac, uses recommended hardware
defaults, and finishes with Start Mac or Start Installer. Existing libraries
open normally. Downloads can be paused and resumed; each one is checked with
SHA-256 before installation. A download does not grant software licenses.

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
The shared pixel renderer passes the same independent control/window/font
fixtures as the Mac app at 1× and 2×, with zero differing pixels. See
[4.5 qualification](../docs/4.5.0-validation.md) for coverage and real-host checks.

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
