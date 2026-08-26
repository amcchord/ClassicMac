/* SPDX-License-Identifier: MIT */

#include <math.h>
#include <stdio.h>

#include "GXMetalATICompatibility.h"

static unsigned failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

#define CHECK_CLOSE(actual, expected) do { \
    if (fabsf((actual) - (expected)) > 0.0001f) { \
        fprintf(stderr, "%s:%d: got %.4f, expected %.4f\n", \
                __FILE__, __LINE__, (double)(actual), (double)(expected)); \
        failures++; \
    } \
} while (0)

static void test_carmageddon_minimap(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        640, 480, 230.0f, 150.0f, 48.0f, 48.0f);

    if (!gxmetal_ati_uses_logical_bitmap_canvas(
            230.0f, 150.0f, 48.0f, 48.0f)) {
        fprintf(stderr, "%s:%d: minimap was not classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
    CHECK_CLOSE(rect.left, 460.0f);
    CHECK_CLOSE(rect.top, 340.0f);
    CHECK_CLOSE(rect.right, 556.0f);
    CHECK_CLOSE(rect.bottom, 436.0f);
}

static void test_physical_context_bitmap(void)
{
    if (gxmetal_ati_uses_logical_bitmap_canvas(
            0.0f, 0.0f, 640.0f, 480.0f)) {
        fprintf(stderr, "%s:%d: full-context bitmap was classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
    if (gxmetal_ati_uses_logical_bitmap_canvas(
            300.0f, 180.0f, 48.0f, 48.0f)) {
        fprintf(stderr, "%s:%d: out-of-canvas bitmap was classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
}

static void test_native_logical_canvas(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        320, 200, 12.0f, 34.0f, 56.0f, 78.0f);

    CHECK_CLOSE(rect.left, 12.0f);
    CHECK_CLOSE(rect.top, 34.0f);
    CHECK_CLOSE(rect.right, 68.0f);
    CHECK_CLOSE(rect.bottom, 112.0f);
}

static void test_larger_four_by_three_context(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        800, 600, 10.0f, 20.0f, 30.0f, 40.0f);

    CHECK_CLOSE(rect.left, 25.0f);
    CHECK_CLOSE(rect.top, 100.0f);
    CHECK_CLOSE(rect.right, 100.0f);
    CHECK_CLOSE(rect.bottom, 200.0f);
}

static void test_later_ati_game_identity(void)
{
    uint32_t value = UINT32_C(0xdeadbeef);

    CHECK(gxmetal_ati_legacy_generation_is_current(
        GXMETAL_ATI_ENGINE_ID, UINT32_C(0x00020101)));
    CHECK(!gxmetal_ati_legacy_generation_is_current(3, 999));
    CHECK(!gxmetal_ati_legacy_generation_is_current(4, 29));
    CHECK(gxmetal_ati_legacy_generation_is_current(4, 30));
    CHECK(gxmetal_ati_private_int(GXMETAL_ATI_CHIP_VERSION_TAG, &value));
    CHECK(value == GXMETAL_ATI_RAGE128_CHIP_VERSION);
    value = UINT32_C(0xdeadbeef);
    CHECK(!gxmetal_ati_private_int(1010, &value));
    CHECK(value == UINT32_C(0xdeadbeef));
}

static void test_private_method_diagnostic_masks(void)
{
    CHECK(gxmetal_ati_private_method_mask(0, 0) == UINT32_C(1));
    CHECK(gxmetal_ati_private_method_mask(31, 0) == UINT32_C(0x80000000));
    CHECK(gxmetal_ati_private_method_mask(32, 1) == UINT32_C(1));
    CHECK(gxmetal_ati_private_method_mask(63, 1) == UINT32_C(0x80000000));
    CHECK(gxmetal_ati_private_method_mask(32, 0) == 0);
    CHECK(gxmetal_ati_private_method_mask(31, 1) == 0);
    CHECK(gxmetal_ati_private_method_mask(64, 0) == 0);
    CHECK(gxmetal_ati_private_method_mask(64, 1) == 0);
    CHECK(gxmetal_ati_private_method_mask(0, 2) == 0);
}

static void test_private_contiguous_vertex_bounds(void)
{
    uint32_t byte_count = UINT32_C(0xdeadbeef);

    CHECK(!gxmetal_ati_private_contiguous_vertex_bytes(0, &byte_count));
    CHECK(byte_count == UINT32_C(0xdeadbeef));
    CHECK(!gxmetal_ati_private_contiguous_vertex_bytes(2, &byte_count));
    CHECK(byte_count == UINT32_C(0xdeadbeef));
    CHECK(gxmetal_ati_private_contiguous_vertex_bytes(3, &byte_count));
    CHECK(byte_count == UINT32_C(384));
    CHECK(gxmetal_ati_private_contiguous_vertex_bytes(
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT, &byte_count));
    CHECK(byte_count == UINT32_C(524288));
    CHECK(!gxmetal_ati_private_contiguous_vertex_bytes(
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT + UINT32_C(1), &byte_count));
    CHECK(byte_count == UINT32_C(524288));
    CHECK(!gxmetal_ati_private_contiguous_vertex_bytes(3, NULL));
}

static void test_private_generic_callback_abi_discriminator(void)
{
    uint32_t vertex_count = UINT32_C(0xdeadbeef);

    CHECK(gxmetal_ati_private_is_contiguous_batch(
        3, GXMETAL_ATI_GL_TRIANGLES));
    CHECK(gxmetal_ati_private_is_contiguous_batch(
        4096, GXMETAL_ATI_GL_POLYGON));
    CHECK(!gxmetal_ati_private_is_contiguous_batch(
        2, GXMETAL_ATI_GL_TRIANGLES));
    CHECK(!gxmetal_ati_private_is_contiguous_batch(
        4097, GXMETAL_ATI_GL_TRIANGLES));
    CHECK(!gxmetal_ati_private_is_contiguous_batch(
        UINT32_C(0x1f7d0a80), UINT32_C(160)));
    CHECK(gxmetal_ati_private_is_fallback_batch(
        UINT32_C(0x1f7d0a80), 4, GXMETAL_ATI_GL_QUADS,
        UINT32_C(0x1f7d0a80)));
    CHECK(!gxmetal_ati_private_is_fallback_batch(
        UINT32_C(0x1f7d0a80), 4, GXMETAL_ATI_GL_QUADS,
        UINT32_C(110)));
    CHECK(!gxmetal_ati_private_is_fallback_batch(
        UINT32_C(0x1f7d0a80), UINT32_C(0x1f7d0b80),
        UINT32_C(160), UINT32_C(110)));
    CHECK(gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 2, &vertex_count));
    CHECK(vertex_count == 4);
    CHECK(gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP,
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT - 2, &vertex_count));
    CHECK(vertex_count == GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT);
    CHECK(!gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_LINE_STRIP, 2, &vertex_count));
    CHECK(!gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 0, &vertex_count));
    CHECK(!gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP,
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT - 1, &vertex_count));
    CHECK(!gxmetal_ati_private_strip_batch_vertex_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 2, NULL));
    vertex_count = UINT32_C(0xdeadbeef);
    CHECK(gxmetal_ati_private_fan_batch_vertex_count(4, &vertex_count));
    CHECK(vertex_count == 6);
    CHECK(gxmetal_ati_private_fan_batch_vertex_count(
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT - 2, &vertex_count));
    CHECK(vertex_count == GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT);
    CHECK(!gxmetal_ati_private_fan_batch_vertex_count(0, &vertex_count));
    CHECK(!gxmetal_ati_private_fan_batch_vertex_count(
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT - 1, &vertex_count));
    CHECK(!gxmetal_ati_private_fan_batch_vertex_count(4, NULL));
    CHECK(gxmetal_ati_private_is_contiguous_triangle(
        UINT32_C(0x1f7d0a00), 3, UINT32_C(0x1f7d0b00)));
    CHECK(!gxmetal_ati_private_is_contiguous_triangle(
        UINT32_C(0x1f7d0a00), UINT32_C(0x1f7d0a80),
        UINT32_C(0x1f7d0b80)));
}

static void test_private_triangle_strip_expansion(void)
{
    static const uint32_t expected[4][3] = {
        { 0, 1, 2 },
        { 2, 1, 3 },
        { 2, 3, 4 },
        { 4, 3, 5 }
    };
    uint32_t indices[3] = {
        UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef),
        UINT32_C(0xdeadbeef)
    };
    uint32_t triangle_index;

    CHECK(!gxmetal_ati_private_strip_triangle_indices(2, 0, indices));
    CHECK(indices[0] == UINT32_C(0xdeadbeef));
    for (triangle_index = 0; triangle_index < UINT32_C(4);
         ++triangle_index) {
        CHECK(gxmetal_ati_private_strip_triangle_indices(
            6, triangle_index, indices));
        CHECK(indices[0] == expected[triangle_index][0]);
        CHECK(indices[1] == expected[triangle_index][1]);
        CHECK(indices[2] == expected[triangle_index][2]);
    }
    CHECK(!gxmetal_ati_private_strip_triangle_indices(6, 4, indices));
    CHECK(!gxmetal_ati_private_strip_triangle_indices(
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT + UINT32_C(1), 0, indices));
    CHECK(!gxmetal_ati_private_strip_triangle_indices(3, 0, NULL));
}

static void check_private_primitive_triangle(
    uint32_t primitive, uint32_t vertex_count, uint32_t triangle_index,
    uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t indices[3] = {
        UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef),
        UINT32_C(0xdeadbeef)
    };

    CHECK(gxmetal_ati_private_primitive_triangle_indices(
        primitive, vertex_count, triangle_index, indices));
    CHECK(indices[0] == a);
    CHECK(indices[1] == b);
    CHECK(indices[2] == c);
}

static void test_private_generic_primitive_expansion(void)
{
    uint32_t untouched[3] = {
        UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef),
        UINT32_C(0xdeadbeef)
    };

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_TRIANGLES, 7) == 2);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLES, 7, 0, 0, 1, 2);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLES, 7, 1, 3, 4, 5);

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 5) == 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 5, 0, 0, 1, 2);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 5, 1, 2, 1, 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLE_STRIP, 5, 2, 2, 3, 4);

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_TRIANGLE_FAN, 5) == 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLE_FAN, 5, 0, 0, 1, 2);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_TRIANGLE_FAN, 5, 2, 0, 3, 4);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_POLYGON, 5, 1, 0, 2, 3);

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_QUADS, 9) == 4);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUADS, 9, 0, 0, 1, 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUADS, 9, 1, 1, 2, 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUADS, 9, 2, 4, 5, 7);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUADS, 9, 3, 5, 6, 7);

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_QUAD_STRIP, 7) == 4);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUAD_STRIP, 7, 0, 0, 1, 2);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUAD_STRIP, 7, 1, 2, 1, 3);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUAD_STRIP, 7, 2, 2, 3, 4);
    check_private_primitive_triangle(
        GXMETAL_ATI_GL_QUAD_STRIP, 7, 3, 4, 3, 5);

    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_POINTS, 20) == 0);
    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_LINE_STRIP, 20) == 0);
    CHECK(gxmetal_ati_private_primitive_triangle_count(
        GXMETAL_ATI_GL_TRIANGLES,
        GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT + UINT32_C(1)) == 0);
    CHECK(!gxmetal_ati_private_primitive_triangle_indices(
        GXMETAL_ATI_GL_TRIANGLES, 3, 1, untouched));
    CHECK(untouched[0] == UINT32_C(0xdeadbeef));
}

static void test_private_clear_color_state(void)
{
    uint32_t state_words[22] = {0};
    uint32_t color_bits[4] = {
        UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef),
        UINT32_C(0xdeadbeef), UINT32_C(0xdeadbeef)
    };

    state_words[18] = UINT32_C(0x3d23d70a);
    state_words[19] = UINT32_C(0x3d75c28f);
    state_words[20] = UINT32_C(0x3e0f5c29);
    state_words[21] = UINT32_C(0x3f800000);
    CHECK(!gxmetal_ati_private_clear_color_bits(
        state_words, UINT32_C(21), color_bits));
    CHECK(color_bits[0] == UINT32_C(0xdeadbeef));
    CHECK(gxmetal_ati_private_clear_color_bits(
        state_words, UINT32_C(22), color_bits));
    CHECK(color_bits[0] == UINT32_C(0x3d23d70a));
    CHECK(color_bits[1] == UINT32_C(0x3d75c28f));
    CHECK(color_bits[2] == UINT32_C(0x3e0f5c29));
    CHECK(color_bits[3] == UINT32_C(0x3f800000));
    CHECK(!gxmetal_ati_private_clear_color_bits(NULL, 22, color_bits));
    CHECK(!gxmetal_ati_private_clear_color_bits(state_words, 22, NULL));
}

static void test_private_depth_function_state(void)
{
    static const uint32_t gl_functions[8] = {
        GXMETAL_ATI_GL_NEVER, GXMETAL_ATI_GL_LESS,
        GXMETAL_ATI_GL_EQUAL, GXMETAL_ATI_GL_LEQUAL,
        GXMETAL_ATI_GL_GREATER, GXMETAL_ATI_GL_NOTEQUAL,
        GXMETAL_ATI_GL_GEQUAL, GXMETAL_ATI_GL_ALWAYS
    };
    static const uint32_t rave_functions[8] = { 8, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t state_words[27] = {0};
    uint32_t result = UINT32_C(0xdeadbeef);
    uint32_t i;

    CHECK(!gxmetal_ati_private_depth_function(
        state_words, GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD,
        &result));
    CHECK(result == UINT32_C(0xdeadbeef));
    for (i = 0; i < UINT32_C(8); i++) {
        state_words[GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD] =
            gl_functions[i];
        CHECK(gxmetal_ati_private_depth_function(
            state_words, UINT32_C(27), &result));
        CHECK(result == rave_functions[i]);
    }
    state_words[GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD] =
        UINT32_C(0xdeadbeef);
    CHECK(!gxmetal_ati_private_depth_function(
        state_words, UINT32_C(27), &result));
    CHECK(!gxmetal_ati_private_depth_function(NULL, UINT32_C(27), &result));
    CHECK(!gxmetal_ati_private_depth_function(
        state_words, UINT32_C(27), NULL));
}

static void test_private_slot20_state_groups(void)
{
    uint32_t state_words[GXMETAL_ATI_PRIVATE_STATE_WORDS] = {0};
    uint32_t function;
    uint32_t reference;
    uint32_t blend;
    uint32_t source;
    uint32_t destination;
    uint32_t write_enabled;
    uint32_t fog_mode;
    uint32_t fog_bits[7];
    uint32_t mask;

    CHECK(GXMETAL_ATI_DIRTY_IMPLEMENTED == UINT32_C(0x020477));
    CHECK(gxmetal_ati_private_msb_boolean(UINT32_C(0x01000000)) == 1);
    CHECK(gxmetal_ati_private_msb_boolean(UINT32_C(0x00000001)) == 0);

    state_words[GXMETAL_ATI_PRIVATE_ALPHA_FUNCTION_WORD] =
        GXMETAL_ATI_GL_NEVER;
    state_words[GXMETAL_ATI_PRIVATE_ALPHA_REFERENCE_WORD] =
        UINT32_C(0x3f000000);
    CHECK(gxmetal_ati_private_alpha_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &function, &reference));
    CHECK(function == 0 && reference == UINT32_C(0x3f000000));
    state_words[GXMETAL_ATI_PRIVATE_ALPHA_ENABLE_WORD] =
        UINT32_C(0x01000000);
    CHECK(gxmetal_ati_private_alpha_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &function, &reference));
    CHECK(function == 8);
    state_words[GXMETAL_ATI_PRIVATE_ALPHA_FUNCTION_WORD] =
        GXMETAL_ATI_GL_GEQUAL;
    CHECK(gxmetal_ati_private_alpha_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &function, &reference));
    CHECK(function == 6);

    CHECK(gxmetal_ati_private_blend_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &blend, &source, &destination));
    CHECK(blend == 2 && source == GXMETAL_ATI_GL_ONE &&
          destination == GXMETAL_ATI_GL_ZERO);
    state_words[GXMETAL_ATI_PRIVATE_BLEND_ENABLE_WORD] =
        UINT32_C(0x01000000);
    state_words[GXMETAL_ATI_PRIVATE_BLEND_SOURCE_WORD] =
        UINT32_C(0x0302);
    state_words[GXMETAL_ATI_PRIVATE_BLEND_DESTINATION_WORD] =
        UINT32_C(0x0303);
    CHECK(gxmetal_ati_private_blend_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &blend, &source, &destination));
    CHECK(blend == 2 && source == UINT32_C(0x0302) &&
          destination == UINT32_C(0x0303));

    state_words[GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD] =
        GXMETAL_ATI_GL_LEQUAL;
    state_words[GXMETAL_ATI_PRIVATE_DEPTH_WRITE_WORD] =
        UINT32_C(0x01000000);
    CHECK(gxmetal_ati_private_depth_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &function, &write_enabled));
    CHECK(function == 0 && write_enabled == 1);
    state_words[GXMETAL_ATI_PRIVATE_DEPTH_ENABLE_WORD] =
        UINT32_C(0x01000000);
    CHECK(gxmetal_ati_private_depth_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &function, &write_enabled));
    CHECK(function == 3 && write_enabled == 1);

    state_words[GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD] = 1;
    state_words[GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD + 1] = 2;
    state_words[GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD + 2] = 3;
    state_words[GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD + 3] = 4;
    state_words[GXMETAL_ATI_PRIVATE_FOG_DENSITY_WORD] = 5;
    state_words[GXMETAL_ATI_PRIVATE_FOG_START_WORD] = 6;
    state_words[GXMETAL_ATI_PRIVATE_FOG_END_WORD] = 7;
    CHECK(gxmetal_ati_private_fog_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &fog_mode, fog_bits));
    CHECK(fog_mode == 0 && fog_bits[0] == 1 && fog_bits[3] == 4 &&
          fog_bits[4] == 5 && fog_bits[5] == 6 && fog_bits[6] == 7);
    state_words[GXMETAL_ATI_PRIVATE_FOG_ENABLE_WORD] =
        UINT32_C(0x01000000);
    state_words[GXMETAL_ATI_PRIVATE_FOG_MODE_WORD] = GXMETAL_ATI_GL_EXP2;
    CHECK(gxmetal_ati_private_fog_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &fog_mode, fog_bits));
    CHECK(fog_mode == 4);
    state_words[GXMETAL_ATI_PRIVATE_FOG_MODE_WORD] = GXMETAL_ATI_GL_LINEAR;
    CHECK(gxmetal_ati_private_fog_state(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS,
        &fog_mode, fog_bits));
    CHECK(fog_mode == 2);

    state_words[GXMETAL_ATI_PRIVATE_CHANNEL_MASK_WORD] =
        UINT32_C(0x01000101);
    CHECK(gxmetal_ati_private_channel_mask(
        state_words, GXMETAL_ATI_PRIVATE_STATE_WORDS, &mask));
    CHECK(mask == UINT32_C(0x0d));
    CHECK(!gxmetal_ati_private_channel_mask(
        state_words, GXMETAL_ATI_PRIVATE_CHANNEL_MASK_WORD, &mask));
}

static void test_private_texture_parameter_state(void)
{
    uint32_t parameters[GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS] = {
        GXMETAL_ATI_GL_REPEAT, UINT32_C(0x812f),
        UINT32_C(0x2703), UINT32_C(0x2601),
        UINT32_C(0x3dcccccd), UINT32_C(0x3e4ccccd),
        UINT32_C(0x3e99999a), UINT32_C(0x3f800000)
    };
    uint32_t wrap_u;
    uint32_t wrap_v;
    uint32_t min_filter;
    uint32_t mag_filter;
    uint32_t border[4];

    CHECK(gxmetal_ati_private_texture_parameters(
        parameters, GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS,
        &wrap_u, &wrap_v, &min_filter, &mag_filter, border));
    CHECK(wrap_u == 0 && wrap_v == 1);
    CHECK(min_filter == UINT32_C(0x2703));
    CHECK(mag_filter == UINT32_C(0x2601));
    CHECK(border[0] == UINT32_C(0x3dcccccd));
    CHECK(border[3] == UINT32_C(0x3f800000));
    CHECK(!gxmetal_ati_private_texture_parameters(
        parameters, GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS - 1,
        &wrap_u, &wrap_v, &min_filter, &mag_filter, border));
}

int main(void)
{
    test_carmageddon_minimap();
    test_physical_context_bitmap();
    test_native_logical_canvas();
    test_larger_four_by_three_context();
    test_later_ati_game_identity();
    test_private_method_diagnostic_masks();
    test_private_contiguous_vertex_bounds();
    test_private_generic_callback_abi_discriminator();
    test_private_triangle_strip_expansion();
    test_private_generic_primitive_expansion();
    test_private_clear_color_state();
    test_private_depth_function_state();
    test_private_slot20_state_groups();
    test_private_texture_parameter_state();
    if (failures != 0) {
        fprintf(stderr, "GXMetal ATI compatibility: %u failures\n",
                failures);
        return 1;
    }
    puts("GXMetal ATI compatibility: all tests passed");
    return 0;
}
