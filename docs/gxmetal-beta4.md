# GXMetal in ClassicMac 1.7.0 beta 4

## What changed

GXMetal is an end-to-end paravirtual QuickDraw 3D RAVE accelerator for the
ClassicMac Power Mac. A PowerPC CFM shared library with the classic `shlb` file
type and `tnsl` creator registers as a RAVE drawing engine in Mac OS. It turns
RAVE calls into bounded packets in a shared PCI command queue. QEMU validates
the packets, preserves their ordering with fences, and renders accepted work
through Metal on the host.

The beta ships the driver, **Install GXMetal**, and **GXMetal Test** together in
the GXMetal folder on the ClassicMac Tools CD. The installer copies both forks
and the required Finder metadata into the active System Folder. It uses a
temporary destination and retains the previous driver until replacement
succeeds, avoiding a half-installed extension. Restarting lets QuickDraw 3D
discover the new engine. Removing GXMetal from Extensions and restarting
returns the guest to its normal software configuration.

## Where the speedup comes from

The guest engine does less work per RAVE call by buffering state and geometry
in a versioned command ring. The host then executes a frame in one Metal
command buffer instead of rasterizing its triangles on the emulated PowerPC.
Textures are uploaded into bounded shared storage and retained as Metal
textures; their sampling, texture operation, alpha test, fog, blending, and
depth tests happen in the GPU pipeline.

Presentation avoids two formerly dominant copies. A compute kernel converts
the completed Metal texture directly into QEMU's page-aligned VRAM in the
guest's big-endian RGB555, ARGB8888, or RGB8888 layout. That eliminates the
per-frame temporary RGBA allocation, `getBytes` GPU readback, and CPU pixel
conversion loop. If an embedding cannot expose aligned shared VRAM, GXMetal
selects the older bounded CPU conversion automatically.

QEMU also tracks the context layout and the clipped presented rectangle. For a
640x480 RGB555 frame it now marks 614,400 bytes dirty rather than the full
67,108,864-byte VGA aperture—a 109.23x reduction. A partial present marks only
the minimal contiguous span that covers that rectangle.

## Measured OS 9 result

The conformance application runs the same deterministic 120-frame mixed
texture/Gouraud workload through GXMetal and through an independently selected
non-GXMetal RAVE engine. On the beta 4 release-candidate host build it recorded:

| Engine | Guest-measured time |
| --- | ---: |
| GXMetal | 48,756 microseconds |
| Apple Software RAVE | 545,219 microseconds |
| Ratio | **11.18x** |

This is a focused RAVE workload rather than a promise that every game will run
11.18x faster. A game also spends time in simulation, sound, file access, and
PowerPC code outside RAVE. The result proves that the driver was discovered,
the complete guest-to-host path ran, the software fallback remained usable,
and the accelerated portion was materially faster under matched guest-side
timing.

## Correctness and fallback gates

Native deterministic tests cover protocol framing, queue wrap/padding,
malformed-input rejection, guest transport, reference rasterization, dirty
ranges, and the complete first-triangle path. The Metal tests read pixels back
in guest framebuffer format and cover Gouraud and textured triangles, depth,
blending, alpha test, fog, clipping, scissoring, bitmaps, double buffering,
orientation-flagged triangles, all three presentation formats, partial
rectangle preservation, direct VRAM presentation, and forced CPU fallback.

The Mac OS 9 application independently proves actual RAVE discovery and checks
red, blue, blended, rejected, fogged, bitmap, clipped, scalar-orientation, and
batched-orientation pixels in VRAM before benchmarking. The beta 4 candidate
passed that suite and selected the direct Metal-to-VRAM path in the real QEMU
process.

GXMetal declines unsupported framebuffer formats, deep-Z requests, complex
QuickDraw regions, missing host features, incompatible protocol versions, and
faulted transports. That is deliberate: RAVE can select Apple Software instead
of receiving a context that will render only part of a game's workload.

## Nanosaur validation

Nanosaur is the first real-game target because it uses the original RAVE API,
textured terrain and models, Gouraud lighting, depth, alpha-tested scenery,
fog, bitmap HUD drawing, and double buffering in one readily visible workload.
Captured frame replay exposed an important semantic mismatch: RAVE's submitted
backfacing bit describes triangle orientation after QuickDraw 3D has applied
its active style; it is not a request for the driver to discard the triangle.
Treating it as culling removed 1,697 of 2,061 captured draws. GXMetal now
preserves the flag through scalar and batched entry points while rendering the
submitted triangles, with host and Mac OS 9 regression coverage for that rule.

The driver remains experimental while real games broaden the compatibility
surface. Run GXMetal Test first, keep a copy of the guest disk, and report the
game, Mac OS version, color depth, and a screenshot for any visual mismatch.
