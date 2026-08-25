/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_RAVE_COMPATIBILITY_H
#define GXMETAL_RAVE_COMPATIBILITY_H

#include <stdint.h>
#include <stddef.h>

/* Public RAVE setters have no error return. Apple's renderers and legacy
 * applications therefore expect an engine to tolerate out-of-range state
 * values without invalidating the whole draw context. Keep the validation
 * here independent of the classic headers so the native suite can test it. */
#define GXMETAL_RAVE_TAG_FOG_MODE UINT32_C(17)
#define GXMETAL_RAVE_FOG_MODE_MAX UINT32_C(4)

static inline int gxmetal_rave_int_state_is_accepted(uint32_t tag,
                                                      uint32_t value)
{
    if (tag == GXMETAL_RAVE_TAG_FOG_MODE) {
        return value <= GXMETAL_RAVE_FOG_MODE_MAX;
    }
    return 1;
}

/* RAVE's public kQAPixel_Alpha1 bitmap format is packed at one bit per
 * pixel. Rows may contain arbitrary caller padding; within each byte the
 * most-significant bit is the leftmost pixel, matching classic QuickDraw
 * bitmaps. GXMetal expands the public bitmap representation to the host's
 * byte-per-texel Alpha8 resource format before upload. Alpha1 textures use
 * their existing Apple Software RAVE-compatible byte layout independently. */
static inline uint32_t gxmetal_rave_alpha1_bitmap_row_bytes(uint32_t width)
{
    return width / 8u + (width % 8u != 0u ? 1u : 0u);
}

static inline int gxmetal_rave_alpha1_bitmap_row_is_valid(
    uint32_t width, size_t row_bytes)
{
    return width != 0u &&
        row_bytes >= gxmetal_rave_alpha1_bitmap_row_bytes(width);
}

static inline int gxmetal_rave_expand_alpha1_bitmap_row(
    uint8_t *destination, size_t destination_bytes,
    const uint8_t *source, size_t source_row_bytes, uint32_t left,
    uint32_t width)
{
    uint32_t x;
    uint64_t end = (uint64_t)left + width;

    if (destination == NULL || source == NULL || width == 0u ||
        destination_bytes < width || end > UINT32_MAX ||
        !gxmetal_rave_alpha1_bitmap_row_is_valid(
            (uint32_t)end, source_row_bytes)) {
        return 0;
    }
    for (x = 0; x < width; x++) {
        uint32_t bit = left + x;
        destination[x] =
            (source[bit / 8u] & (uint8_t)(0x80u >> (bit % 8u))) != 0u ?
            UINT8_MAX : 0u;
    }
    return 1;
}

#endif
