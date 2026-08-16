# GXMetal

GXMetal is a paravirtual QuickDraw 3D RAVE drawing engine for ClassicMac. Its
PowerPC CFM shared library uses the classic `tnsl` file type expected by RAVE on
Mac OS 8.5, 8.6, and 9. The guest batches rendering commands for a QEMU device;
the host will execute those commands with Metal and present through the existing
Power Mac display.

This directory currently contains the protocol, a fail-closed engine skeleton,
and the first QEMU transport milestone. ClassicMac starts its Power Mac with
`VGA.gxmetal=on`; QEMU then realizes the control registers and shared PCI BAR,
validates queued packets, completes fences, and latches malformed input as a
diagnostic fault. The host deliberately advertises only trace/fence support at
this stage. The engine therefore continues to return `kQANotSupported` instead
of claiming a drawing context before rasterization is implemented, leaving
Apple's software renderer as the automatic fallback.

## Transport contract

The version 1.0 wire contract is defined in
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

Run the portable protocol conformance tests on the host:

```sh
make -C gxmetal test
```

Build the PowerPC CFM `tnsl` MacBinary with the existing Retro68 toolchain and
Apple Universal Interfaces:

```sh
scripts/build-gxmetal.sh
```

The latter produces `gxmetal/guest/bin/GXMetal.bin`. It is a MacBinary file so
its `tnsl` Finder type and `cfrg` resource survive transfer to an HFS volume.
The build also verifies that the PEF loader header contains a CFM initialization
entry (rather than an application main entry) and that the entry descriptor
invokes `QARegisterEngine`.

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
