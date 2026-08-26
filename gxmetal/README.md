# GXMetal

GXMetal is a paravirtual QuickDraw 3D RAVE drawing engine for ClassicMac. Its
PowerPC CFM shared library uses the `shlb` Finder type, `tnsl` creator, and
`tnsl`, `ftag`, and `vers` resources used by shipping hardware engines on Mac
OS 8.5, 8.6, and 9. The guest batches
rendering commands for a QEMU device; the host executes those commands with
Metal and presents through the existing Power Mac display.

This directory contains the protocol, the PowerPC RAVE engine, the VGA NDRV
handoff, and the QEMU transport/renderers. ClassicMac starts its Power
Mac with `VGA.gxmetal=on`; QEMU realizes the control registers and shared PCI
BAR, validates queued packets, completes fences, and latches malformed input as
a diagnostic fault. The existing VGA NDRV publishes the Expansion Manager's
logical BAR mappings through the Name Registry, allowing the `tnsl` to connect
without guessing physical PCI addresses.

On macOS, QEMU now executes direct-color Gouraud points, lines, strips, fans,
triangles, clipped color/depth clears, all RAVE depth comparisons and depth
write masking, interpolated or premultiplied alpha, and double-buffer swaps in a
native Metal render pipeline. The PowerPC engine also creates, uploads, binds,
and destroys RGB555, RGB565, ARGB1555, ARGB4444, RGB32, and ARGB32 textures.
The ATI compatibility path additionally accepts Carmageddon II's private
pixel type 1001 as big-endian ARGB4444, preserving the alpha nibble used for
antialiased menu and HUD glyph masks. This is required for correctly colored
world textures and overlapping interface sprites. Metal
provides perspective-correct sampling, repeat/clamp addressing, nearest,
bilinear, and trilinear mip filtering, with independent OpenGL MIN, MAG, and
mip selection on both texture units, plus RAVE decal, modulation, and
highlight texture operations. Linear, exponential, and squared-exponential
RAVE depth fog is applied in both the Gouraud and textured fragment paths. Fog
distance follows the RAVE contract and is reconstructed from `1 / invW`; the
normalized Z-buffer coordinate remains independent for hidden-surface removal.
All seven RAVE alpha comparisons operate on shaded fragment alpha before
blending and depth writes, supporting masked sprites, foliage, fences, and
cutout texture borders. RAVE chromakey rejects matching primary texture RGB
values in the normalized eight-bit color domain before texture operations,
fog, blending, and depth writes. GXMetal preserves RAVE's per-triangle backfacing
orientation flag through scalar and batched draw entry points without treating
it as a discard request. QuickDraw 3D applies the active backfacing style before
submission; a submitted triangle must still be rendered by the driver.
Rectangular QuickDraw regions and RAVE scissor state are intersected
in Metal; complex regions decline GXMetal so RAVE can select a software engine.
A frame is batched in one Metal command buffer, then `PRESENT`
copies only the dirty rectangle inside the immutable context clip into the
guest's big-endian RGB555 or 32-bit VGA surface. The QEMU bridge mirrors only
validated context-layout metadata and marks the minimal contiguous VRAM span
covering that presented rectangle dirty once per frame; it no longer dirties
the entire VGA aperture for every clear and draw packet. With the default
64 MiB aperture, a full 640x480 RGB555 present now dirties 614,400 bytes rather
than 67,108,864 bytes (109.23x fewer), and partial presents reduce it further.
When QEMU's VRAM is page-aligned, a Metal compute kernel converts the presented
texture directly into that shared buffer in RGB555, ARGB8888, or RGB8888
layout. This removes the temporary RGBA allocation, `getBytes` readback, and
CPU pixel loop from each frame. Unaligned embeddings retain the bounded CPU
readback path automatically.
The device advertises `GXMETAL_FEATURE_METAL` only when that backend initializes
successfully. The bounded, deterministic CPU rasterizer remains the fallback on
other hosts and the correctness oracle for Metal.

Contexts requiring unsupported pixel formats or deep Z still return
`kQANotSupported`, allowing RAVE to select Apple's software renderer instead of
accepting a partially implemented path. Hosts without Metal likewise decline
texture, Z, and double-buffer contexts because the CPU renderer intentionally
remains a small Gouraud correctness oracle.

## Transport contract

The backward-compatible version 1.24 wire contract is defined in
`protocol/gxmetal_protocol.h`. All registers and shared-memory packets are
little-endian. Packet sizes are multiples of 16 bytes, packets never cross the
end of the circular command ring, and offsets in commands refer only to the
upload portion of the GXMetal shared BAR. Guest virtual or physical pointers
are never accepted by the host.

The existing std-VGA BAR2 has an unused 256-byte range beginning at `0x0b00`.
GXMetal places its discovery, queue, status, fence, and reset registers there.
A separate 4 MiB PCI BAR contains a 1 MiB command ring and a bounded upload
heap. The device is optional: a missing magic value, incompatible major
version, faulted status, or failed device probe prevents GXMetal from claiming
a RAVE draw context.

Every packet begins with opcode, header size, total size, context ID, and
sequence. Context creation identifies a bounded target in VGA BAR0. Frame
commands carry explicit rectangles. RAVE integer and floating-point state uses
a typed `SET_STATE` payload, while pointer-valued texture and bitmap state is
translated to 32-bit GXMetal resource IDs. Gouraud vertices use eight binary32
values; textured vertices use the exact sixteen-value RAVE layout. Resource
uploads name only a validated offset and length in the upload heap.

The existing primary and secondary OpenGL filter tags carry independent MIN
and MAG state without a protocol extension. MIN accepts all six OpenGL 1.x
choices and selects both spatial and mip filtering; MAG accepts only nearest
or linear. Legacy RAVE Fast/Mid/Best tags continue to set the corresponding
complete sampler preset. GXMetal AGL Probe persists distinct primary base-only,
trilinear, and asymmetric-filter pixels. When Apple OpenGL exposes two ARB
texture units and both CFM entry points, it also persists a separate unit-1
mip sample; older installations report that lane as native-test-only instead
of failing symbol resolution.

The upload heap is a single staging region, so the guest follows every texture
or bitmap upload with a fence before reusing those bytes. This prevents games
which build many small UI atlases back-to-back from racing the host and binding
later pixels to an earlier resource. QuickDraw bitmap draws also suspend any
locally bound ATI texture while emitting their temporary bitmap resource; ATI
sprite coordinate conversion therefore cannot leak into a full-screen bitmap
and collapse it to a scanline.

Private ATI textures are treated as immutable after creation unless the game
explicitly supplies `kQATexture_NoCopy`. Only those caller-backed textures are
checked for CPU-side changes. This avoids rescanning every static world and UI
texture in emulated PowerPC code on every frame.

The Metal backend retains up to 4,096 live texture and bitmap records. This is
deliberately larger than Carmageddon II's observed 477-resource peak: menu,
HUD, and world transitions can retain more than 256 resources at once, and
exhausting the old fixed table faulted the command queue for the rest of the
session.

## Carmageddon II compatibility

The Mac OS 9 RAVE release is validated both by opening its RAVE application
directly and through the original launcher with **640 x 480 Hardware
accelerated** and the highest-quality texture option. The launcher does not
search for a compatible application: it opens the exact relative path
`Carma2:Carmageddon 2 Rave`. Some third-party patched installs rename that file
to `Carmageddoon 2 Rave` (with an extra `o`). Such an install still runs when
opened directly, but the launcher silently returns to Finder. Restoring a copy
with the launcher's exact filename fixes the handoff; GXMetal does not rename
or modify game files.

The producer writes complete packets, performs a PowerPC I/O synchronization,
publishes the producer offset, then rings the doorbell. The host validates the
entire packet before dispatch and advances the consumer offset only after it
has consumed the packet. `PAD` consumes the unused tail when the next packet
would wrap. Fence completion is reported by sequence number.

## Build and test

Run the protocol, queue, guest-producer, renderer, and complete first-triangle
pipeline tests on the host. macOS additionally compiles the Objective-C backend
with strict warnings and verifies real Metal triangles, clipped clears, depth
ordering, alpha blending, alpha rejection before depth writes, a double-buffer
presentation, and a four-color big-endian texture upload/sample/destroy cycle.
Both Gouraud and post-texture-operation alpha testing are asserted. The texture
test uses an asymmetric image to catch vertical-origin regressions, then repeats
the draw through linear depth fog with deliberately different normalized Z and
reciprocal-W distance values. Gouraud and textured backface cases prove
that orientation-flagged triangles still update framebuffer and depth state, while
protocol tests reject unknown draw flags. A separate clip test proves immutable
context clipping, mutable scissoring, untouched framebuffer preservation, and
dirty-rectangle-only presentation. A host-independent scanout test proves
clipped, padded-row, offset, empty, destroyed, reset, and malformed-context
dirty-range behavior. The Metal test also forces both direct and fallback
present selection and validates the direct kernel's three guest framebuffer
formats plus partial-rectangle preservation. It also verifies ATI's
big-endian ARGB4444 channel order and alpha blending, then creates and destroys
a 300-texture working set to guard against the former Carmageddon resource
ceiling. Every result is read back from the guest-format framebuffer:

```sh
make -C gxmetal test
```

Build the PowerPC CFM `tnsl`, one-click installer, and conformance application
with the existing Retro68 toolchain and Apple Universal Interfaces:

```sh
scripts/build-gxmetal.sh
```

The original supplied icon artwork lives unchanged at
`guest/art/GXMetalIcon-master.gif`.
`tools/build_icon_resources.py` crops its alpha silhouette and deterministically
generates the tracked `guest/src/GXMetalIcon.r` with 32- and 16-pixel 8-bit,
4-bit, and monochrome classic icon members. Regenerate it with a Pillow-enabled
Python when the master artwork changes:

```sh
python3 gxmetal/tools/build_icon_resources.py \
  gxmetal/guest/art/GXMetalIcon-master.gif \
  gxmetal/guest/src/GXMetalIcon.r
```

The build produces `GXMetal.bin`, `GXMetalInput.bin`, `GXMetalStartup.bin`,
`GXMetalInstaller.bin`, and `GXMetalTest.bin` in `gxmetal/guest/bin`. They are
MacBinary files so their PEF data forks, resource forks, and Finder metadata
survive transfer to HFS. `GXMetalStartup.bin` is a small 68K `INIT` companion:
Mac OS 9 does not execute an `INIT` embedded in the required `shlb`/`tnsl` RAVE
library, so the companion displays GXMetal's puzzle-piece M in the normal
startup extension row. `GXMetalInput.bin` is an InputSprocket driver that
switches ClassicMac to captured relative host motion while a game owns its
mouse, recenters the guest cursor for unlimited turning, and restores the
seamless absolute pointer when the game releases input. Older hosts retain the
original seamless-pointer bridge. The build verifies the driver's complete
RAVE/CFM discovery resources and initialization descriptor, the InputSprocket
driver's exported discovery callbacks, the companion's executable `INIT` and
icon family, and the test app's public RAVE imports.

`scripts/build-guest-cd.sh` rebuilds those five matching artifacts and places
them in the `GXMetal` folder on the ClassicMac Tools CD. In the guest,
double-click **Install GXMetal**. It finds the active System Folder and stages
both forks of the RAVE driver, InputSprocket bridge, and startup companion
before changing anything. On an update, the already-loaded old files are
renamed, made invisible, and changed to an inert non-extension Finder type
before the new set takes its canonical names. The new startup companion
deletes those rollback copies during the required restart, so stale drivers
cannot be rediscovered. After
restarting, **GXMetal Test** enumerates the engines
that RAVE actually registered, selects GXMetal by its gestalt name, exercises a
Z-buffered and double-buffered render with Gouraud shading, alpha blending,
alpha testing, depth fog, an uploaded texture, and a partially clipped uploaded
bitmap inside a rectangular QuickDraw clip; waits on the host fence; then
validates red, blue, blended-purple, alpha-rejected green, preserved clipped
pixels, fogged-purple, bitmap-green, and scalar and batched orientation-flagged
red pixels directly in the guest framebuffer. It also
requires a deliberately complex region to return
`kQANotSupported`. A missing device or host feature fails the test explicitly
and remains eligible for Apple's normal software RAVE fallback.
The same app then runs a fixed 120-frame mixed texture/Gouraud workload first
through GXMetal and then through an independently selected non-GXMetal engine.
It records both microsecond totals and the fixed-point speedup in
`System Folder:Preferences:GXMetal Test Results`, proving that the software
fallback remains usable while producing a repeatable guest-level performance
comparison.

Build the patched QEMU binaries and black-box test the GXMetal PCI layout:

```sh
scripts/build-qemu.sh
```

The build checks that the PowerPC VGA device accepts the `gxmetal` property,
that BAR2 remains a 4 KiB register aperture, that the prefetchable GXMetal BAR4
is 4 MiB, and that invalid configurations fail realization.

Set `GXMETAL_PROFILE=1` in QEMU's host environment to print a two-second
rolling presentation profile to standard error. The profile reports actual
PRESENT-packet FPS, direct versus fallback presentation, presentation time,
draw packets per frame, vertices per draw, the percentage of one-triangle
packets, and resource-table probes per lookup. It is disabled by default and
does not add logging to normal releases.

`COMPATIBILITY.md` is the audited RAVE capability contract and prioritized
general-purpose roadmap. It distinguishes standards-level support from the
ATI private compatibility bridge and from features that must continue to fall
back to Apple Software RAVE.

On macOS, validate the exact Developer ID-signed application against a supplied
Mac OS 9 disk without modifying the source image:

```sh
scripts/test-gxmetal-os9.sh /path/to/mac-os-9-disk.img
```

The harness verifies the bundle, APFS-clones (or copies) the disk, extracts the
matching driver, startup companion, and test application from the bundle's own
Tools CD, installs them only into the clone, boots the bundled GXMetal-capable
QEMU, and reads the flushed PASS/FAIL record back from the clone. It retains the
temporary directory and final screenshot as auditable evidence.

For interactive debugging without a Cocoa window, start the bundled QEMU with
`-display none`, a local `-vnc unix:/path/to/vnc.sock` endpoint, and a local
monitor socket. The dependency-free helper can capture the raw framebuffer and
send keys, Mac Command-key chords, or pointer clicks:

```sh
python3 scripts/gxmetal-vnc.py \
  --unix-socket /path/to/vnc.sock \
  --chord Super_L+o \
  --screenshot /tmp/gxmetal.png
```

`Super_L` is QEMU VNC's Mac Command key. The same path was used for the
Carmageddon II launcher, menu, race, sustained-input, and clean-quit tests, so
those checks do not depend on the foreground ClassicMac window.

### Carmageddon II performance validation

Host presentation telemetry identified two CPU-side submission bottlenecks.
The texture table originally required a linear scan of a 4,096-entry array;
the Carmageddon menu averaged about 30 probes per texture lookup and dense
gameplay reached roughly 170-195. An 8,192-bucket collision-safe hash table
reduces that to one probe in the measured working set. Small host vertex arrays
also use stack storage, avoiding thousands of short-lived allocations.

More importantly, 99.92% of the game's original draw packets contained one
triangle. The guest now preserves adjacent Gouraud or textured triangles in a
64-triangle batch until render state, texture, orientation flags, ATI private
coordinate mode, ordering, or synchronization requires a flush. This reduced
measured gameplay from roughly 1,300-2,500 host draw packets per frame to
220-380 without changing the submitted triangle order.

On the disposable Mac OS 9.2 Carmageddon II validation disk, a sustained
20-sample gameplay window measured 78.54 FPS minimum, 84.35 FPS average, and
100.75 FPS maximum. Active driving samples measured 73-81 FPS. The game's
static menu remains paced internally at about 47.4 FPS even after draw traffic
falls from 1,177 to 272 packets per frame; that is game timing rather than host
renderer saturation. The current in-guest conformance workload passes and
measures GXMetal at 12.77x Apple Software RAVE on the same VM.

## Versioning and safety

The high 16 bits of `GXMETAL_REG_VERSION` are the incompatible major version;
the low 16 bits add backward-compatible functionality. Every optional rendering
path also has a feature bit. Unknown opcodes, malformed sizes, ring crossings,
and out-of-range uploads fault the queue instead of being interpreted.

Device loss and protocol faults are surfaced at the next RAVE synchronization
boundary. New contexts then fail cleanly, allowing applications and RAVE to use
the software engine; no GXMetal path writes outside the dedicated shared BAR or
the active VGA framebuffer bounds.
