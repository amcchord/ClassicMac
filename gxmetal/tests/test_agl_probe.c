#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "GXMetalAGLProbeLogic.h"

static void test_readback_accepts_green_triangle_on_blue_background(void)
{
    const uint8_t triangle[3] = {8, 244, 12};
    const uint8_t background[3] = {10, 14, 31};

    assert(gxmetal_agl_probe_readback_matches(triangle, background, 0));
}

static void test_readback_rejects_errors_and_wrong_surfaces(void)
{
    const uint8_t green[3] = {8, 244, 12};
    const uint8_t blue[3] = {10, 14, 31};
    const uint8_t red[3] = {240, 8, 8};
    const uint8_t black[3] = {0, 0, 0};

    assert(!gxmetal_agl_probe_readback_matches(green, blue, 0x0502));
    assert(!gxmetal_agl_probe_readback_matches(red, blue, 0));
    assert(!gxmetal_agl_probe_readback_matches(green, black, 0));
}

static void test_extended_readback_covers_texture_blend_and_depth(void)
{
    const uint8_t texture[3] = {246, 242, 8};
    const uint8_t blend[3] = {132, 8, 18};
    const uint8_t depth[3] = {244, 6, 5};
    const uint8_t black[3] = {0, 0, 0};

    assert(gxmetal_agl_probe_extended_readback_matches(
        texture, blend, depth, 0));
    assert(!gxmetal_agl_probe_extended_readback_matches(
        black, blend, depth, 0));
    assert(!gxmetal_agl_probe_extended_readback_matches(
        texture, black, depth, 0));
    assert(!gxmetal_agl_probe_extended_readback_matches(
        texture, blend, black, 0));
    assert(!gxmetal_agl_probe_extended_readback_matches(
        texture, blend, depth, 0x0502));
}

static void test_clipped_texture_readback_requires_the_visible_remnant(void)
{
    const uint8_t yellow[3] = {246, 242, 8};
    const uint8_t background[3] = {10, 14, 31};

    assert(gxmetal_agl_probe_clipped_texture_matches(yellow, 0));
    assert(!gxmetal_agl_probe_clipped_texture_matches(background, 0));
    assert(!gxmetal_agl_probe_clipped_texture_matches(yellow, 0x0502));
}

static void test_filled_mode_readback_covers_topology_and_triangle_guard(void)
{
    const uint8_t filled[GXMETAL_AGL_PROBE_FILLED_MODE_COUNT][3] = {
        {8, 244, 12}, {9, 240, 10}, {10, 238, 11},
        {7, 242, 9}, {11, 239, 8}, {8, 241, 12}
    };
    const uint8_t background[3] = {10, 14, 31};
    uint8_t broken[GXMETAL_AGL_PROBE_FILLED_MODE_COUNT][3];

    memcpy(broken, filled, sizeof(broken));
    assert(gxmetal_agl_probe_filled_modes_match(filled, background, 0));
    broken[4][1] = 0;
    assert(!gxmetal_agl_probe_filled_modes_match(broken, background, 0));
    assert(!gxmetal_agl_probe_filled_modes_match(filled, filled[0], 0));
    assert(!gxmetal_agl_probe_filled_modes_match(
        filled, background, 0x0502));
}

static void test_sampler_readback_covers_min_mag_and_mip_selection(void)
{
    const uint8_t base_only[3] = {244, 8, 7};
    const uint8_t trilinear[3] = {149, 106, 8};
    const uint8_t asymmetric[3] = {119, 5, 136};
    const uint8_t nearest[3] = {4, 3, 245};
    const uint8_t wrong_mip[3] = {8, 242, 10};

    assert(gxmetal_agl_probe_sampler_primary_matches(
        base_only, trilinear, asymmetric, 0));
    assert(!gxmetal_agl_probe_sampler_primary_matches(
        wrong_mip, trilinear, asymmetric, 0));
    assert(!gxmetal_agl_probe_sampler_primary_matches(
        base_only, wrong_mip, asymmetric, 0));
    assert(!gxmetal_agl_probe_sampler_primary_matches(
        base_only, trilinear, nearest, 0));
    assert(!gxmetal_agl_probe_sampler_primary_matches(
        base_only, trilinear, asymmetric, 0x0502));
}

static void test_sampler_unit1_readback_requires_trilinear_mip_blend(void)
{
    const uint8_t trilinear[3] = {149, 106, 242};
    const uint8_t nearest_mip[3] = {8, 242, 240};

    assert(gxmetal_agl_probe_sampler_unit1_matches(trilinear, 0));
    assert(!gxmetal_agl_probe_sampler_unit1_matches(nearest_mip, 0));
    assert(!gxmetal_agl_probe_sampler_unit1_matches(trilinear, 0x0502));
}

static void test_extension_matching_observes_token_boundaries(void)
{
    const char *extensions =
        "GL_EXT_compiled_vertex_array GL_ARB_multitexture GL_EXT_bgra";

    assert(gxmetal_agl_probe_has_extension(extensions,
                                           "GL_ARB_multitexture"));
    assert(gxmetal_agl_probe_has_extension(extensions, "GL_EXT_bgra"));
    assert(!gxmetal_agl_probe_has_extension(extensions,
                                            "ARB_multitexture"));
    assert(!gxmetal_agl_probe_has_extension(extensions,
                                            "GL_ARB_multi"));
    assert(!gxmetal_agl_probe_has_extension(extensions, "GL EXT"));
    assert(!gxmetal_agl_probe_has_extension(NULL, "GL_EXT_bgra"));
}

int main(void)
{
    test_readback_accepts_green_triangle_on_blue_background();
    test_readback_rejects_errors_and_wrong_surfaces();
    test_extended_readback_covers_texture_blend_and_depth();
    test_clipped_texture_readback_requires_the_visible_remnant();
    test_filled_mode_readback_covers_topology_and_triangle_guard();
    test_sampler_readback_covers_min_mag_and_mip_selection();
    test_sampler_unit1_readback_requires_trilinear_mip_blend();
    test_extension_matching_observes_token_boundaries();
    puts("GXMetal AGL probe logic tests passed");
    return 0;
}
