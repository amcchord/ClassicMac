# GXMetal compatibility contract

GXMetal should be useful to a game we have never seen before, not only to the
titles used while developing it. The rule is simple: advertise a RAVE feature
only when the complete public contract is implemented and covered by a
deterministic test. Unsupported combinations must return `kQANotSupported`
early enough for the RAVE manager to select Apple Software RAVE.

## Current capability matrix

| Area | Public contract | Current status | Validation |
| --- | --- | --- | --- |
| Discovery | PowerPC CFM `tnsl` engine and device association | Supported | Mac OS 9 black-box install and enumeration |
| Draw contexts | GDevice and framebuffer-backed memory devices in RGB16, RGB32, or ARGB32 | Supported when the target is inside VGA memory | Guest conformance, host context tests |
| Primitives | Points, lines, triangles, strips, fans, indexed meshes, and submitted vertex arrays | Supported for Gouraud and textured vertices | Protocol, renderer, Metal, and guest conformance tests |
| Color and depth | Color clear, Z16 clear, all RAVE depth comparisons, and depth-write mask | Supported | Metal and guest framebuffer checks |
| Blending | Premultiplied, interpolated-alpha, and the OpenGL source/destination factors used by the system GLD | Supported | Metal alpha and OpenGL pipeline tests |
| Textures | Alpha1, RGB8_332, RGB555, RGB565, ARGB1555, ARGB4444, RGB32, ARGB32, CL4, CL8, ACL16_88, I8, and AI16_88; mipmaps; repeat/clamp; nearest, bilinear, and trilinear filtering | Supported | Apple Software RAVE oracle, asymmetric upload/sample, packed-nibble and byte-layout, channel-expansion, byte-order, per-pixel alpha, transparent-index, odd-width and row-padding tests, and classic-game runs |
| Texture operations | Decal, modulation, highlight, and their documented combinations | Supported | Metal shader tests |
| Fog | Alpha, linear depth, exponential, and squared-exponential | Supported | Metal and guest conformance tests |
| Alpha test | All seven RAVE comparisons, before blend and depth write | Supported | Metal and guest conformance tests |
| Clipping | Immutable rectangular context clip plus mutable OpenGL scissor | Supported | Host and guest preservation tests |
| Complex regions | Arbitrary non-rectangular QuickDraw regions | Deliberately declined | Guest fallback test |
| Presentation | Single and double buffer, dirty rectangles, RGB555/RGB32/ARGB32 scanout | Supported | Direct and fallback presentation tests |
| Bitmaps | Affine bitmap copy with positive independent X/Y scaling, nearest or linear filtering, clipping, and the supported texture formats | Supported | Guest scaled-extent, filter-state, and clipped-bitmap tests |
| ATI private bridge | OpenGLRendererATI hooks, two-stage lightmaps, NoCopy refresh, and private ARGB4444 | Supported compatibility path | Quake III, Carmageddon II, and host tests |
| Public RAVE multitexture | One RAVE 1.6 secondary stage, `QASubmitMultiTextureParams`, independent reciprocal-W/UV values, add/modulate/alpha/fixed composition, filter, wrap, and enable/disable | Supported and advertised as one accelerated stage | Native Metal tests plus signed-bundle Mac OS 9 conformance |
| Perspective Z | `kQATag_PerspectiveZ` | Reciprocal-W hidden-surface removal and fog, with ordinary Z-function semantics preserved | Host and guest tests make normalized Z and reciprocal W disagree |
| Extended OpenGL state | Wrap, filters, scissor, and blend factors are honored | Draw-buffer selection, line/area stipple, border color, and environment color are not yet implemented | Required GLD paths are covered; unimplemented state must not become a silent dependency |
| Resource access | `QAAccessTexture`, `QAAccessBitmap`, draw-buffer access, and Z-buffer access | Texture and bitmap access are supported for direct-color resources, including mip levels and dirty-rectangle uploads; framebuffer and Z-buffer access remain unavailable | Native partial-upload preservation test plus signed-bundle Mac OS 9 conformance |
| Offscreen and scaled contexts | Offscreen allocation, draw-context copy, and scaling | Deliberately declined | Software fallback |
| Chromakey | `kQATag_Chromakey_r/g/b` and `kQATag_ChromakeyEnable` | Primary texel RGB is compared in the normalized 8-bit color domain before texture operations, fog, blending, or depth writes | Native Metal matching/non-matching tests plus guest conformance |
| Deep Z, CSG, antialias, channel mask, Z sorting | Optional RAVE features | Not advertised | Software fallback |

## General-purpose priorities

1. Keep the capability declaration executable. The guest conformance app
   should query every advertised bit and exercise its state, method, and
   fallback behavior. Unknown or malformed host state must never fault the
   command queue merely because an application probed an unadvertised feature.
2. Complete or narrow extended OpenGL semantics. In particular, either
   implement draw-buffer and stipple behavior or document why the system GLD
   never exposes it to applications.
3. Measure remaining specialized formats against real software before adding
   them. The packed YUV variants stay unadvertised until a game or standards
   probe establishes their value and exact conversion semantics.
4. Prioritize channel masks and the OpenGL draw-buffer contract before more
   specialized features such as CSG or Z-sorted transparency.

## Game qualification

Every release keeps a small standards suite and a varied game suite. The
standards suite is authoritative for advertised behavior; games catch API
usage patterns and integration failures.

- GXMetal Test: discovery, version agreement, exact capability gestalts,
  public RAVE 1.6 multitexture, rendering, fallback, and performance.
- Nanosaur: public RAVE texture, fog, depth, and camera behavior.
- Carmageddon II: large resource sets, CL8/ARGB4444 UI, blending, and long
  high-draw-count sessions.
- Quake III Arena Demo: system GLD/ATI bridge, multitextured lightmaps,
  dynamic scenes, overlays, and captured relative mouse input.

New compatibility work starts with a minimal reproducer and ends with a
standards-level regression test. A title-specific workaround is acceptable
only when it models a real behavior of the original RAVE hardware or system
GLD and is isolated behind that compatibility boundary.
