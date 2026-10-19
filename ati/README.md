# Emulated ATI Radeon R100 (`ati-vga,model=r100`)

The Power Mac G4's 3D-capable display card. Mac OS 9's own ATI software
(*ATI Driver Update*'s `ATY,Rage6p` display driver, *ATI Radeon 3D
Accelerator*, *ATI Resource Manager* and Apple's *OpenGLRendererATI*) drives
an emulated PCI Radeon (1002:5144). Its command processor, 2D engine and 3D
engine run in QEMU; 3D draws are rendered on the Mac's GPU through OpenGL.
No ClassicMac guest driver is involved. `scripts/build-qemu.sh` applies the
patches below last, after every other ClassicMac patch; only `hw/display/ati*`
(and the display `meson.build`/`trace-events`) change, so machines without the
card are unaffected.

## Files

| File | What | Source |
| --- | --- | --- |
| `retroboot-ati-r100.patch` | the R100 model: register file, PLLs/CRTC modes, EDID from `xres`/`yres`, command processor (PIO queues, ring, indirect buffers, PCI GART, scratch write-back), 2D engine, 3D engine on a host GL thread, `x-accel-stats` | verbatim from RetroBoot, see Provenance |
| `classicmac-ati-macos.patch` | ClassicMac: macOS host GL back end (CGL, OpenGL 4.1 Core profile, `OpenGL.framework` loaded with `dlopen()` when the card is realized), GLSL 4.10 shader header, `CLASSICMAC_ATI_3D=off`, the `x-gl-renderer` status property | this repository |
| `openbios-ati-radeon.patch` | OpenBIOS: names the card `ATY,Rage6p`, enables bus mastering, programs the `-g` boot mode through the ATI CRTC and drops the QEMU VGA driver property, only for that device | verbatim from RetroBoot, applied by `scripts/build-openbios.sh` |
| `tests/test_qemu_ati.py` | build gate: realizes the card on `mac99` and checks GL status and the off switch, plus the standard VGA argument set | this repository |
| `tests/run-gl-smoke.sh`, `tests/ati_gl_smoke.c` | stand-alone host GL check of the tree's `ati_gl.c` and `ati_glr.c` shaders (context, FBO, draw, readback) | this repository |
| `tests/guest_run.py` | headless guest harness (VNC socket, HMP screendump, QMP input) for both cards | this repository |

## Provenance and license

- `retroboot-ati-r100.patch`: RetroBoot `emulator/patches/retroboot-ati-r100.patch`
  at RetroBoot commit `1860881266449ca2575061e0bd4055c1265cf217` (last changed
  by `eed8be751edf115375603a04f180158a0e6fc03d`, 2026-10-15), SHA-256
  `163bf0451d3b13e5098ed4fa9fa60e25eb030135b85a16adf5a230aef32a9cf4`. Its
  patched files are identical to RetroBoot `research/gfx/qemu/hw/display/`.
  RetroBoot's base is ClassicMac `3e24594` + QEMU v11.0.2 `e545d8bb9d63`.
- `openbios-ati-radeon.patch`: RetroBoot `emulator/openbios/retroboot-ati-radeon.patch`
  (last changed by `d43e582f11732c3bc38d5358093d6255f75636cf`), SHA-256
  `c6282bb965fdb742d9a5c25bfdacd2aa7c3a1b460971b99b5d64866996bd0a88`. It
  applies to OpenBIOS `e1e703ac` + `powermac/openbios-classic-macos-8.patch`
  alone; RetroBoot's AirPort, sound-objects, PMU and find-wordlist OpenBIOS
  patches are not needed.
- License: GPL-2.0-or-later, as the QEMU and OpenBIOS files they modify
  (QEMU's `ati*.c` by BALATON Zoltan; additions "Copyright (c) 2026 RetroBoot
  contributors" and "ClassicMac contributors"). The R100 register names and
  semantics follow AMD's documentation and the MIT-licensed X.org/Mesa
  drivers; no ATI or Apple binary is part of this repository.

## Host GL on macOS

RetroBoot's Linux back end loads `libEGL.so.1`/`libGLESv2.so.2` and needs
`EGL_MESA_platform_surfaceless`. On macOS, `ati_gl.c` instead loads
`/System/Library/Frameworks/OpenGL.framework/OpenGL`, chooses an accelerated
OpenGL 4.1 Core pixel format (any 4.1 Core renderer if no GPU renderer
exists; the info string then says "(software)"), creates a CGL context with
no drawable and makes it current on the `ati-gl` thread. Every renderer draw
targets a framebuffer object. All `ATI_GL_FUNCS` entry points are core in
both OpenGL ES 3.0 and OpenGL 4.1 and are resolved with `dlsym()`; only
`CGLTypes.h` is included, so no deprecated declaration is used and
`qemu-system-ppc` gains no link-time dependency (the build checks this).
Shaders get `#version 410 core` instead of `#version 300 es`; the fragment
output carries an explicit `layout(location = 0)`.

If no context can be created, QEMU logs `ati: no host GL (...); 3D drawing is
discarded`, `x-accel-stats` ends with `gl=off` and `x-gl-renderer` is empty:
the card keeps working as a 2D card (RetroBoot's behavior), but Mac OS's ATI
3D software then draws nothing visible, so the app should not offer the card
without host GL.

## Run-time switches and status

- `CLASSICMAC_ATI_3D=off` (or RetroBoot's `RETROBOOT_ATI_3D=off`) in QEMU's
  environment turns the 3D engine off.
- QOM `x-accel-stats` (string): `ops2d=… draws3d=… verts3d=… flips=… gl=on|off`.
- QOM `x-gl-renderer` (string): e.g. `CGL; GL 4.1 Metal - 91.7; Apple M5 Max`,
  empty when 3D is off.

## Classic Mac OpenGL idle handoff

`classicmac-radeon-idle.patch` initializes the Radeon WAIT_UNTIL mask with the
2D/3D idle-clean waits at card reset. The renderer is idle at this handoff.
The Mac driver polls this mask in `glFinish`; a zero mask leaves it retrying
for about 150 ms per finish, reducing the Quake III id intro to about 3 fps.
The mask remains writable and reads continue to return the programmed value.
Rage 128 behavior is unchanged.

On the same guest without restarting Quake or sound, changing only the mask
from zero to `0x30000` restores 30 uploads/s and clearing it restores the
slowdown. Fresh-launch, replay and game-input checks use
`input/tests/superdeluxe-quake3.py`; no game executable or preferences change.
