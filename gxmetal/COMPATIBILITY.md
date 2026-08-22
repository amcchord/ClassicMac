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
| Textures | RGB555, RGB565, ARGB1555, ARGB4444, RGB32, ARGB32, and CL8; mipmaps; repeat/clamp; nearest, bilinear, and trilinear filtering | Supported | Asymmetric upload/sample tests and classic-game runs |
| Texture operations | Decal, modulation, highlight, and their documented combinations | Supported | Metal shader tests |
| Fog | Alpha, linear depth, exponential, and squared-exponential | Supported | Metal and guest conformance tests |
| Alpha test | All seven RAVE comparisons, before blend and depth write | Supported | Metal and guest conformance tests |
| Clipping | Immutable rectangular context clip plus mutable OpenGL scissor | Supported | Host and guest preservation tests |
| Complex regions | Arbitrary non-rectangular QuickDraw regions | Deliberately declined | Guest fallback test |
| Presentation | Single and double buffer, dirty rectangles, RGB555/RGB32/ARGB32 scanout | Supported | Direct and fallback presentation tests |
| Bitmaps | Unscaled affine bitmap copy with the supported texture formats | Supported | Guest clipped-bitmap test |
| ATI private bridge | OpenGLRendererATI hooks, two-stage lightmaps, NoCopy refresh, and private ARGB4444 | Supported compatibility path | Quake III, Carmageddon II, and host tests |
| Public RAVE multitexture | `QASubmitMultiTextureParams` and `kQAOptional_MultiTextures` | Not yet exposed | Must remain unadvertised |
| Perspective Z | `kQATag_PerspectiveZ` | Reciprocal-W hidden-surface removal and fog, with ordinary Z-function semantics preserved | Host and guest tests make normalized Z and reciprocal W disagree |
| Extended OpenGL state | Wrap, filters, scissor, and blend factors are honored | Draw-buffer selection, line/area stipple, border color, and environment color are not yet implemented | Required GLD paths are covered; unimplemented state must not become a silent dependency |
| Resource access | `QAAccessTexture`, `QAAccessBitmap`, draw-buffer access, and Z-buffer access | Not exposed | Games use immutable uploads or the guarded NoCopy refresh path |
| Offscreen and scaled contexts | Offscreen allocation, draw-context copy, and scaling | Deliberately declined | Software fallback |
| Deep Z, CSG, antialias, chromakey, channel mask, Z sorting | Optional RAVE features | Not advertised | Software fallback |

## General-purpose priorities

1. Implement public two-stage RAVE 1.6 multitexture. Give secondary
   reciprocal-W and UV values their own wire fields so multitexture can coexist
   with specular highlight, register `QASubmitMultiTextureParams`, then
   advertise one secondary stage only after host and guest conformance pass.
2. Make the capability declaration executable. The guest conformance app
   should query every advertised bit and exercise its state, method, and
   fallback behavior. Unknown or malformed host state must never fault the
   command queue merely because an application probed an unadvertised feature.
3. Complete or narrow extended OpenGL semantics. In particular, either
   implement draw-buffer and stipple behavior or document why the system GLD
   never exposes it to applications.
4. Add dynamic-resource access methods with dirty-rectangle uploads. This is
   the public RAVE mechanism for frequently changing textures and bitmaps and
   avoids game-specific memory watching.
5. Expand common sprite and UI formats in measured order: I8, AI16_88,
   ACL16_88, Alpha1, then CL4. Each format needs endian, alpha, palette, and
   row-padding tests before it is advertised.
6. Add bitmap scale/filter and chromakey before attempting more specialized
   features such as CSG or Z-sorted transparency.

## Game qualification

Every release keeps a small standards suite and a varied game suite. The
standards suite is authoritative for advertised behavior; games catch API
usage patterns and integration failures.

- GXMetal Test: discovery, version agreement, required gestalts, rendering,
  fallback, and performance.
- Nanosaur: public RAVE texture, fog, depth, and camera behavior.
- Carmageddon II: large resource sets, CL8/ARGB4444 UI, blending, and long
  high-draw-count sessions.
- Quake III Arena Demo: system GLD/ATI bridge, multitextured lightmaps,
  dynamic scenes, overlays, and captured relative mouse input.

New compatibility work starts with a minimal reproducer and ends with a
standards-level regression test. A title-specific workaround is acceptable
only when it models a real behavior of the original RAVE hardware or system
GLD and is isolated behind that compatibility boundary.
