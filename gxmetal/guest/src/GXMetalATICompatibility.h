/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_ATI_COMPATIBILITY_H
#define GXMETAL_ATI_COMPATIBILITY_H

#include <stdint.h>

/* GXMetal advertises ATI's vendor ID because classic QuickDraw 3D clients
 * and Apple's OpenGLRendererATI deliberately select that compatibility path.
 * Vendor-specific engine IDs below generation four are rejected by later
 * Pangea launchers; generation four also needs revision 30.  Use the next
 * generation while retaining GXMetal's own product revision in the public
 * revision gestalt. */
#define GXMETAL_ATI_ENGINE_ID UINT32_C(5)

/* ATI 3D Accelerator exposes its chip-family version through private integer
 * tag 1011.  A value of 0x0500 selects the Rage 128 feature path used by
 * Bugdom-era clients.  GXMetal implements the associated public RAVE state
 * and does not expose unsupported hardware-only entry points here. */
#define GXMETAL_ATI_CHIP_VERSION_TAG UINT32_C(1011)
#define GXMETAL_ATI_RAGE128_CHIP_VERSION UINT32_C(0x0500)

typedef struct GXMetalBitmapRect {
    float left;
    float top;
    float right;
    float bottom;
} GXMetalBitmapRect;

static inline int gxmetal_ati_legacy_generation_is_current(
    uint32_t engine_id, uint32_t revision)
{
    return engine_id > 4u || (engine_id == 4u && revision >= 30u);
}

static inline int gxmetal_ati_private_int(uint32_t tag, uint32_t *value)
{
    if (tag != GXMETAL_ATI_CHIP_VERSION_TAG || value == 0) {
        return 0;
    }
    *value = GXMETAL_ATI_RAGE128_CHIP_VERSION;
    return 1;
}

static inline int gxmetal_ati_uses_logical_bitmap_canvas(
    float x, float y, float width, float height)
{
    return x >= 0.0f && y >= 0.0f && width > 0.0f && height > 0.0f &&
        x + width <= 320.0f && y + height <= 200.0f;
}

/* ATI's classic RAVE compatibility mode accepts small QADrawBitmap overlays
 * inside the game's 320x200 HUD coordinate system.  Full-context bitmaps are
 * already in physical pixels and are excluded by the predicate above.  Scale
 * the logical canvas uniformly to the draw-context width and center it
 * vertically.  Carmageddon II's 640x480 mode therefore becomes a
 * pixel-doubled 640x400 HUD with 40-pixel top and bottom margins, matching the
 * software renderer. */
static inline GXMetalBitmapRect gxmetal_ati_bitmap_rect(
    uint32_t context_width, uint32_t context_height,
    float x, float y, float width, float height)
{
    GXMetalBitmapRect result;
    float scale = context_width != 0 ?
        (float)context_width / 320.0f : 1.0f;
    float top_margin = ((float)context_height - 200.0f * scale) * 0.5f;

    result.left = x * scale;
    result.top = y * scale + top_margin;
    result.right = (x + width) * scale;
    result.bottom = (y + height) * scale + top_margin;
    return result;
}

#endif
