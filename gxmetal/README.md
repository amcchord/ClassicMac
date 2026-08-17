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
and destroys RGB555, ARGB1555, ARGB4444, RGB32, and ARGB32 textures. Metal
provides perspective-correct sampling, repeat/clamp addressing, nearest,
bilinear, and trilinear mip filtering, plus RAVE decal, modulation, and
highlight texture operations. A frame is batched in one Metal command buffer,
then `PRESENT` copies the completed image into the guest's big-endian RGB555 or
32-bit VGA surface.
The device advertises `GXMETAL_FEATURE_METAL` only when that backend initializes
successfully. The bounded, deterministic CPU rasterizer remains the fallback on
other hosts and the correctness oracle for Metal.

Contexts requiring unsupported pixel formats or deep Z still return
`kQANotSupported`, allowing RAVE to select Apple's software renderer instead of
accepting a partially implemented path. Hosts without Metal likewise decline
texture, Z, and double-buffer contexts because the CPU renderer intentionally
remains a small Gouraud correctness oracle.

## Transport contract

The backward-compatible version 1.1 wire contract is defined in
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

The producer writes complete packets, performs a PowerPC I/O synchronization,
publishes the producer offset, then rings the doorbell. The host validates the
entire packet before dispatch and advances the consumer offset only after it
has consumed the packet. `PAD` consumes the unused tail when the next packet
would wrap. Fence completion is reported by sequence number.

## Build and test

Run the protocol, queue, guest-producer, renderer, and complete first-triangle
pipeline tests on the host. macOS additionally compiles the Objective-C backend
with strict warnings and verifies real Metal triangles, clipped clears, depth
ordering, alpha blending, a double-buffer presentation, and a four-color
big-endian texture upload/sample/destroy cycle by reading back the resulting
guest-format framebuffer:

```sh
make -C gxmetal test
```

Build the PowerPC CFM `tnsl`, one-click installer, and conformance application
with the existing Retro68 toolchain and Apple Universal Interfaces:

```sh
scripts/build-gxmetal.sh
```

The build produces `GXMetal.bin`, `GXMetalInstaller.bin`, and
`GXMetalTest.bin` in `gxmetal/guest/bin`. They are MacBinary files so their PEF
data forks, `cfrg` resources, and Finder metadata survive transfer to HFS. The
build verifies that the driver has the `shlb`/`tnsl` Finder pair and complete
RAVE/CFM discovery resources, its PEF loader header contains a CFM initialization
entry (rather than an application main entry), the entry descriptor invokes
`QARegisterEngine`, and the test app imports the public RAVE
discovery/context/texture/bitmap APIs.

`scripts/build-guest-cd.sh` rebuilds those three matching artifacts and places
them in the `GXMetal` folder on the ClassicMac Tools CD. In the guest,
double-click **Install GXMetal**. It finds the active System Folder, copies both
forks of `GXMetal` into Extensions using a rollback-safe temporary rename, and
asks for a restart. After restarting, **GXMetal Test** enumerates the engines
that RAVE actually registered, selects GXMetal by its gestalt name, exercises a
Z-buffered and double-buffered render with Gouraud shading, alpha blending, and
an uploaded texture, and a partially clipped uploaded bitmap; waits on the host
fence; then validates red, blue, blended-purple, and bitmap-green pixels directly
in the guest framebuffer. A missing device or host feature fails the test
explicitly and remains eligible for Apple's normal software RAVE fallback.

Build the patched QEMU binaries and black-box test the GXMetal PCI layout:

```sh
scripts/build-qemu.sh
```

The build checks that the PowerPC VGA device accepts the `gxmetal` property,
that BAR2 remains a 4 KiB register aperture, that the prefetchable GXMetal BAR4
is 4 MiB, and that invalid configurations fail realization.

## Versioning and safety

The high 16 bits of `GXMETAL_REG_VERSION` are the incompatible major version;
the low 16 bits add backward-compatible functionality. Every optional rendering
path also has a feature bit. Unknown opcodes, malformed sizes, ring crossings,
and out-of-range uploads fault the queue instead of being interpreted.

Device loss and protocol faults are surfaced at the next RAVE synchronization
boundary. New contexts then fail cleanly, allowing applications and RAVE to use
the software engine; no GXMetal path writes outside the dedicated shared BAR or
the active VGA framebuffer bounds.
