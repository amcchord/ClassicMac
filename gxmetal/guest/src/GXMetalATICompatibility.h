/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_ATI_COMPATIBILITY_H
#define GXMETAL_ATI_COMPATIBILITY_H

#include <stdint.h>

typedef struct GXMetalBitmapRect {
    float left;
    float top;
    float right;
    float bottom;
} GXMetalBitmapRect;

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
