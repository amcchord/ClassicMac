/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <string.h>

#include "GXMetalRAVECompatibility.h"

static unsigned failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

int main(void)
{
    uint32_t mode;
    uint32_t pixel_type;
    uint32_t length;
    uint32_t min_filter;
    uint32_t mag_filter;
    static const uint8_t packed[2][4] = {
        {0x40, 0x40, 0xff, 0xff},
        {0x80, 0x80, 0xff, 0xff}
    };
    static const uint8_t expected[2][10] = {
        {0, 255, 0, 0, 0, 0, 0, 0, 0, 255},
        {255, 0, 0, 0, 0, 0, 0, 0, 255, 0}
    };
    uint8_t expanded[2][10];

    CHECK(gxmetal_rave_draw_buffer_layout(
        1024, 768, 4096, GXMETAL_PIXEL_RGB8888, GXMETAL_UPLOAD_BYTES,
        &pixel_type, &length));
    CHECK(pixel_type == GXMETAL_RAVE_PIXEL_RGB32);
    CHECK(length == UINT32_C(3145728));
    CHECK(gxmetal_rave_draw_buffer_layout(
        8, 4, 32, GXMETAL_PIXEL_ARGB8888, GXMETAL_UPLOAD_BYTES,
        &pixel_type, &length));
    CHECK(pixel_type == GXMETAL_RAVE_PIXEL_ARGB32 && length == 128u);
    CHECK(gxmetal_rave_draw_buffer_layout(
        8, 4, 16, GXMETAL_PIXEL_RGB555, GXMETAL_UPLOAD_BYTES,
        &pixel_type, &length));
    CHECK(pixel_type == GXMETAL_RAVE_PIXEL_RGB16 && length == 64u);
    CHECK(!gxmetal_rave_draw_buffer_layout(
        8, 4, 31, GXMETAL_PIXEL_RGB8888, GXMETAL_UPLOAD_BYTES,
        &pixel_type, &length));
    CHECK(!gxmetal_rave_draw_buffer_layout(
        1, GXMETAL_UPLOAD_BYTES, 16, GXMETAL_PIXEL_RGB555,
        GXMETAL_UPLOAD_BYTES, &pixel_type, &length));
    CHECK(!gxmetal_rave_draw_buffer_layout(
        8, 4, 32, GXMETAL_PIXEL_DEPTH16, GXMETAL_UPLOAD_BYTES,
        &pixel_type, &length));
    CHECK(!gxmetal_rave_draw_buffer_layout(
        8, 4, 32, GXMETAL_PIXEL_RGB8888, GXMETAL_UPLOAD_BYTES,
        NULL, &length));

    for (mode = 0; mode <= GXMETAL_RAVE_FOG_MODE_MAX; mode++) {
        CHECK(gxmetal_rave_int_state_is_accepted(
            GXMETAL_RAVE_TAG_FOG_MODE, mode));
    }
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_FOG_MODE, UINT32_C(0x1eb1f0c8)));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_TEXTURE_FILTER, UINT32_C(0x1eb1f0c8)));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_Z_FUNCTION, 8u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_Z_FUNCTION, 9u));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_BLEND, 2u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_BLEND, 3u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_PERSPECTIVE_Z, 2u));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_TEXTURE_FILTER,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR));
    CHECK(gxmetal_rave_filter_preset_to_gl(
        GXMETAL_TEXTURE_FILTER_FAST, &min_filter, &mag_filter));
    CHECK(min_filter == GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST);
    CHECK(mag_filter == GXMETAL_RAVE_GL_NEAREST);
    CHECK(gxmetal_rave_filter_preset_to_gl(
        GXMETAL_TEXTURE_FILTER_MID, &min_filter, &mag_filter));
    CHECK(min_filter == GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST);
    CHECK(mag_filter == GXMETAL_RAVE_GL_LINEAR);
    CHECK(gxmetal_rave_filter_preset_to_gl(
        GXMETAL_TEXTURE_FILTER_BEST, &min_filter, &mag_filter));
    CHECK(min_filter == GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR);
    CHECK(mag_filter == GXMETAL_RAVE_GL_LINEAR);
    CHECK(gxmetal_rave_filter_preset_to_gl(
        GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR,
        &min_filter, &mag_filter));
    CHECK(min_filter == GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR);
    CHECK(mag_filter == GXMETAL_RAVE_GL_NEAREST);
    CHECK(!gxmetal_rave_filter_preset_to_gl(
        UINT32_C(0x2704), &min_filter, &mag_filter));
    CHECK(gxmetal_rave_sampler_state_transition(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR, GXMETAL_RAVE_GL_LINEAR,
        &min_filter, &mag_filter) == 0);
    CHECK(gxmetal_rave_sampler_state_transition(
        GXMETAL_RAVE_TAG_TEXTURE_FILTER, GXMETAL_TEXTURE_FILTER_FAST,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR, GXMETAL_RAVE_GL_LINEAR,
        &min_filter, &mag_filter) == 1);
    CHECK(min_filter == GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST);
    CHECK(mag_filter == GXMETAL_RAVE_GL_NEAREST);
    CHECK(gxmetal_rave_sampler_state_transition(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR, min_filter, mag_filter,
        &min_filter, &mag_filter) == 1);
    CHECK(min_filter == GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR);
    CHECK(mag_filter == GXMETAL_RAVE_GL_NEAREST);
    CHECK(gxmetal_rave_sampler_state_transition(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER, GXMETAL_RAVE_GL_LINEAR,
        min_filter, mag_filter, &min_filter, &mag_filter) == 1);
    CHECK(gxmetal_rave_sampler_state_transition(
        GXMETAL_RAVE_TAG_TEXTURE_FILTER, GXMETAL_TEXTURE_FILTER_FAST,
        min_filter, mag_filter, &min_filter, &mag_filter) == 1);
    CHECK(min_filter == GXMETAL_RAVE_GL_NEAREST_MIPMAP_NEAREST);
    CHECK(mag_filter == GXMETAL_RAVE_GL_NEAREST);
    CHECK(gxmetal_rave_sampler_tag_is_secondary(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_FILTER));
    CHECK(gxmetal_rave_sampler_tag_is_secondary(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER));
    CHECK(gxmetal_rave_sampler_tag_is_secondary(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER));
    CHECK(!gxmetal_rave_sampler_tag_is_secondary(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER,
        GXMETAL_RAVE_GL_NEAREST));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER,
        GXMETAL_RAVE_GL_LINEAR));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MAG_FILTER,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_NEAREST));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_MAG_FILTER,
        GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_TEXTURE_MIN_FILTER,
        GXMETAL_RAVE_GL_LINEAR_MIPMAP_LINEAR));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_MIN_FILTER,
        GXMETAL_RAVE_GL_NEAREST_MIPMAP_LINEAR));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_TEXTURE_FILTER, UINT32_C(0x2704)));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_ALPHA_TEST_FUNCTION, 8u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_ALPHA_TEST_FUNCTION, 9u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_ENABLE, 2u));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_CURRENT, UINT32_MAX));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_CURRENT, 0u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_CURRENT, 1u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_MULTI_TEXTURE_OP, 4u));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_TEXTURE_WRAP_U, 2u));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_BLEND_SRC, UINT32_C(0x0308)));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_BLEND_SRC, UINT32_C(0x0309)));
    CHECK(gxmetal_rave_int_state_requires_write_masks(
        GXMETAL_RAVE_TAG_CHANNEL_MASK));
    CHECK(gxmetal_rave_int_state_requires_write_masks(
        GXMETAL_RAVE_TAG_GL_DRAW_BUFFER));
    CHECK(!gxmetal_rave_int_state_requires_write_masks(
        GXMETAL_RAVE_TAG_Z_BUFFER_MASK));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_CHANNEL_MASK, 0));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_CHANNEL_MASK, GXMETAL_RAVE_CHANNEL_MASK_ALL));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_CHANNEL_MASK,
        GXMETAL_RAVE_CHANNEL_MASK_ALL << 1));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_DRAW_BUFFER, 0));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_DRAW_BUFFER, GXMETAL_RAVE_DRAW_BUFFER_ALL));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_GL_DRAW_BUFFER,
        GXMETAL_RAVE_DRAW_BUFFER_ALL << 1));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_Z_BUFFER_MASK, 0));
    CHECK(gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_Z_BUFFER_MASK, 1));
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_Z_BUFFER_MASK, 2));

    CHECK(gxmetal_rave_alpha1_bitmap_row_bytes(1) == 1);
    CHECK(gxmetal_rave_alpha1_bitmap_row_bytes(8) == 1);
    CHECK(gxmetal_rave_alpha1_bitmap_row_bytes(9) == 2);
    CHECK(gxmetal_rave_alpha1_bitmap_row_bytes(10) == 2);
    CHECK(!gxmetal_rave_alpha1_bitmap_row_is_valid(0, 0));
    CHECK(!gxmetal_rave_alpha1_bitmap_row_is_valid(10, 1));
    CHECK(gxmetal_rave_alpha1_bitmap_row_is_valid(10, 2));
    CHECK(gxmetal_rave_alpha1_bitmap_row_is_valid(10, 4));
    memset(expanded, 0xa5, sizeof(expanded));
    CHECK(gxmetal_rave_expand_alpha1_bitmap_row(
        expanded[0], sizeof(expanded[0]), packed[0], sizeof(packed[0]),
        0, 10));
    CHECK(gxmetal_rave_expand_alpha1_bitmap_row(
        expanded[1], sizeof(expanded[1]), packed[1], sizeof(packed[1]),
        0, 10));
    CHECK(memcmp(expanded, expected, sizeof(expected)) == 0);
    CHECK(!gxmetal_rave_expand_alpha1_bitmap_row(
        expanded[0], sizeof(expanded[0]), packed[0], 1, 0, 10));
    CHECK(!gxmetal_rave_expand_alpha1_bitmap_row(
        expanded[0], sizeof(expanded[0]) - 1, packed[0],
        sizeof(packed[0]), 0, 10));

    /* A dirty subregion can begin mid-byte without changing bit order. */
    memset(expanded[0], 0xa5, sizeof(expanded[0]));
    CHECK(gxmetal_rave_expand_alpha1_bitmap_row(
        expanded[0], 8, packed[0], sizeof(packed[0]), 1, 8));
    CHECK(memcmp(expanded[0], expected[0] + 1, 8) == 0);

    if (failures != 0) {
        fprintf(stderr, "GXMetal RAVE compatibility: %u failures\n",
                failures);
        return 1;
    }
    puts("GXMetal RAVE compatibility: all tests passed");
    return 0;
}
