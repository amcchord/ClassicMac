/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_RAVE_COMPATIBILITY_H
#define GXMETAL_RAVE_COMPATIBILITY_H

#include <stdint.h>
#include <stddef.h>

#include "gxmetal_protocol.h"

/* Public RAVE setters have no error return. Apple's renderers and legacy
 * applications therefore expect an engine to tolerate out-of-range state
 * values without invalidating the whole draw context. Keep the validation
 * here independent of the classic headers so the native suite can test it. */
#define GXMETAL_RAVE_TAG_Z_FUNCTION UINT32_C(0)
#define GXMETAL_RAVE_TAG_BLEND UINT32_C(9)
#define GXMETAL_RAVE_TAG_PERSPECTIVE_Z UINT32_C(10)
#define GXMETAL_RAVE_TAG_TEXTURE_FILTER UINT32_C(11)
#define GXMETAL_RAVE_TAG_FOG_MODE UINT32_C(17)
#define GXMETAL_RAVE_FOG_MODE_MAX UINT32_C(4)
#define GXMETAL_RAVE_TAG_CHANNEL_MASK UINT32_C(27)
#define GXMETAL_RAVE_TAG_Z_BUFFER_MASK UINT32_C(28)
#define GXMETAL_RAVE_TAG_CHROMAKEY_ENABLE UINT32_C(30)
#define GXMETAL_RAVE_TAG_ALPHA_TEST_FUNCTION UINT32_C(31)
#define GXMETAL_RAVE_TAG_DONT_SWAP UINT32_C(32)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_ENABLE UINT32_C(33)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_CURRENT UINT32_C(34)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_OP UINT32_C(35)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_FILTER UINT32_C(36)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_WRAP_U UINT32_C(37)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_WRAP_V UINT32_C(38)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER UINT32_C(39)
#define GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER UINT32_C(40)
#define GXMETAL_RAVE_TAG_GL_DRAW_BUFFER UINT32_C(100)
#define GXMETAL_RAVE_TAG_GL_TEXTURE_WRAP_U UINT32_C(101)
#define GXMETAL_RAVE_TAG_GL_TEXTURE_WRAP_V UINT32_C(102)
#define GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER UINT32_C(103)
#define GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER UINT32_C(104)
#define GXMETAL_RAVE_TAG_GL_BLEND_SRC UINT32_C(109)
#define GXMETAL_RAVE_TAG_GL_BLEND_DST UINT32_C(110)
#define GXMETAL_RAVE_CHANNEL_MASK_ALL UINT32_C(0x0f)
#define GXMETAL_RAVE_DRAW_BUFFER_ALL UINT32_C(0x0f)
#define GXMETAL_RAVE_GL_NEAREST UINT32_C(0x2600)
#define GXMETAL_RAVE_GL_LINEAR UINT32_C(0x2601)
#define GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST UINT32_C(0x2700)
#define GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST UINT32_C(0x2701)
#define GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR UINT32_C(0x2702)
#define GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR UINT32_C(0x2703)

#define GXMETAL_RAVE_PIXEL_RGB16 UINT32_C(1)
#define GXMETAL_RAVE_PIXEL_RGB32 UINT32_C(3)
#define GXMETAL_RAVE_PIXEL_ARGB32 UINT32_C(4)

static inline int gxmetal_rave_draw_buffer_layout(
    uint32_t width, uint32_t height, uint32_t row_bytes,
    uint32_t pixel_format, uint32_t staging_bytes,
    uint32_t *pixel_type, uint32_t *length)
{
    uint32_t bytes_per_pixel;
    uint32_t mapped_pixel_type;
    uint64_t required;

    if (pixel_format == GXMETAL_PIXEL_RGB555) {
        bytes_per_pixel = 2u;
        mapped_pixel_type = GXMETAL_RAVE_PIXEL_RGB16;
    } else if (pixel_format == GXMETAL_PIXEL_ARGB8888) {
        bytes_per_pixel = 4u;
        mapped_pixel_type = GXMETAL_RAVE_PIXEL_ARGB32;
    } else if (pixel_format == GXMETAL_PIXEL_RGB8888) {
        bytes_per_pixel = 4u;
        mapped_pixel_type = GXMETAL_RAVE_PIXEL_RGB32;
    } else {
        return 0;
    }
    required = (uint64_t)row_bytes * height;
    if (pixel_type == NULL || length == NULL || width == 0u || height == 0u ||
        row_bytes < (uint64_t)width * bytes_per_pixel || required == 0u ||
        required > staging_bytes || required > UINT32_MAX) {
        return 0;
    }
    *pixel_type = mapped_pixel_type;
    *length = (uint32_t)required;
    return 1;
}

static inline int gxmetal_rave_filter_is_accepted(uint32_t value)
{
    return value <= 2u || value == GXMETAL_RAVE_GL_NEAREST ||
           value == GXMETAL_RAVE_GL_LINEAR ||
           value == GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST ||
           value == GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST ||
           value == GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR ||
           value == GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR;
}

static inline int gxmetal_rave_mag_filter_is_accepted(uint32_t value)
{
    return value == GXMETAL_RAVE_GL_NEAREST ||
           value == GXMETAL_RAVE_GL_LINEAR;
}

/* Legacy RAVE exposes one Fast/Mid/Best preset while ATI's OpenGL bridge
 * supplies independent MIN and MAG tags.  Express the presets as exact GL
 * values so temporary engine draws can restore asymmetric OpenGL state
 * without collapsing it back to a single preset. */
static inline int gxmetal_rave_filter_preset_to_gl(
    uint32_t value, uint32_t *min_filter, uint32_t *mag_filter)
{
    uint32_t min_value;
    uint32_t mag_value;

    switch (value) {
    case GXMETAL_TEXTURE_FILTER_FAST:
        min_value = GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST;
        mag_value = GXMETAL_RAVE_GL_NEAREST;
        break;
    case GXMETAL_TEXTURE_FILTER_MID:
        min_value = GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST;
        mag_value = GXMETAL_RAVE_GL_LINEAR;
        break;
    case GXMETAL_TEXTURE_FILTER_BEST:
        min_value = GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR;
        mag_value = GXMETAL_RAVE_GL_LINEAR;
        break;
    case GXMETAL_RAVE_GL_NEAREST:
    case GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST:
    case GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR:
        min_value = value;
        mag_value = GXMETAL_RAVE_GL_NEAREST;
        break;
    case GXMETAL_RAVE_GL_LINEAR:
    case GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST:
    case GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR:
        min_value = value;
        mag_value = GXMETAL_RAVE_GL_LINEAR;
        break;
    default:
        return 0;
    }
    if (min_filter != NULL) {
        *min_filter = min_value;
    }
    if (mag_filter != NULL) {
        *mag_filter = mag_value;
    }
    return 1;
}

static inline int gxmetal_rave_sampler_tag_is_secondary(uint32_t tag)
{
    return tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_FILTER ||
           tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER ||
           tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER;
}

/* Return -1 for a non-sampler/invalid tag, zero when the effective sampler is
 * unchanged, and one when the host must receive this transition. Legacy and
 * GL tags alias the same host sampler, so raw per-tag deduplication is not
 * sufficient after another alias changes MIN, MAG, or mip selection. */
static inline int gxmetal_rave_sampler_state_transition(
    uint32_t tag, uint32_t value, uint32_t current_min,
    uint32_t current_mag, uint32_t *next_min, uint32_t *next_mag)
{
    uint32_t min_filter = current_min;
    uint32_t mag_filter = current_mag;

    if (tag == GXMETAL_RAVE_TAG_TEXTURE_FILTER ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_FILTER) {
        if (!gxmetal_rave_filter_preset_to_gl(
                value, &min_filter, &mag_filter)) {
            return -1;
        }
    } else if (tag == GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER ||
               tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER) {
        if (!gxmetal_rave_filter_is_accepted(value)) {
            return -1;
        }
        min_filter = value;
    } else if (tag == GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER ||
               tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER) {
        if (!gxmetal_rave_mag_filter_is_accepted(value)) {
            return -1;
        }
        mag_filter = value;
    } else {
        return -1;
    }
    if (next_min != NULL) {
        *next_min = min_filter;
    }
    if (next_mag != NULL) {
        *next_mag = mag_filter;
    }
    return min_filter != current_min || mag_filter != current_mag;
}

static inline int gxmetal_rave_gl_blend_factor_is_accepted(uint32_t value)
{
    return value <= 1u ||
           (value >= UINT32_C(0x0300) && value <= UINT32_C(0x0308));
}

static inline int gxmetal_rave_int_state_requires_write_masks(uint32_t tag)
{
    return tag == GXMETAL_RAVE_TAG_CHANNEL_MASK ||
           tag == GXMETAL_RAVE_TAG_GL_DRAW_BUFFER;
}

static inline int gxmetal_rave_int_state_is_accepted(uint32_t tag,
                                                      uint32_t value)
{
    if (tag == GXMETAL_RAVE_TAG_Z_FUNCTION) {
        return value <= 8u;
    }
    if (tag == GXMETAL_RAVE_TAG_BLEND) {
        return value <= 2u;
    }
    if (tag == GXMETAL_RAVE_TAG_PERSPECTIVE_Z ||
        tag == GXMETAL_RAVE_TAG_Z_BUFFER_MASK ||
        tag == GXMETAL_RAVE_TAG_CHROMAKEY_ENABLE ||
        tag == GXMETAL_RAVE_TAG_DONT_SWAP ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_ENABLE) {
        return value <= 1u;
    }
    if (tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_CURRENT) {
        /* RAVE numbers additional texture units from zero and uses -1 for
         * the primary unit.  Apple's ATI GLD emits that primary sentinel
         * before every ordinary OpenGL texture bind.  GXMetal exposes one
         * additional unit, so only primary (-1) and secondary (0) are valid. */
        return value == UINT32_MAX || value == 0u;
    }
    if (tag == GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER) {
        return gxmetal_rave_mag_filter_is_accepted(value);
    }
    if (tag == GXMETAL_RAVE_TAG_TEXTURE_FILTER ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_FILTER ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER ||
        tag == GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER) {
        return gxmetal_rave_filter_is_accepted(value);
    }
    if (tag == GXMETAL_RAVE_TAG_FOG_MODE) {
        return value <= GXMETAL_RAVE_FOG_MODE_MAX;
    }
    if (tag == GXMETAL_RAVE_TAG_CHANNEL_MASK) {
        return (value & ~GXMETAL_RAVE_CHANNEL_MASK_ALL) == 0u;
    }
    if (tag == GXMETAL_RAVE_TAG_GL_DRAW_BUFFER) {
        return (value & ~GXMETAL_RAVE_DRAW_BUFFER_ALL) == 0u;
    }
    if (tag == GXMETAL_RAVE_TAG_ALPHA_TEST_FUNCTION) {
        /* Value eight is GXMetal's ATI-private extension for enabled
         * GL_NEVER. Public RAVE clients continue to use the documented 0..7
         * range. */
        return value <= 8u;
    }
    if (tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_OP) {
        return value <= 3u;
    }
    if (tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_WRAP_U ||
        tag == GXMETAL_RAVE_TAG_MULTI_TEXTURE_WRAP_V ||
        tag == GXMETAL_RAVE_TAG_GL_TEXTURE_WRAP_U ||
        tag == GXMETAL_RAVE_TAG_GL_TEXTURE_WRAP_V) {
        return value <= 1u;
    }
    if (tag == GXMETAL_RAVE_TAG_GL_BLEND_SRC ||
        tag == GXMETAL_RAVE_TAG_GL_BLEND_DST) {
        return gxmetal_rave_gl_blend_factor_is_accepted(value);
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
