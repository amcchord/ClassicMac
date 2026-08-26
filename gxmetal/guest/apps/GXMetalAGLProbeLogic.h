#ifndef GXMETAL_AGL_PROBE_LOGIC_H
#define GXMETAL_AGL_PROBE_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#define GXMETAL_AGL_PROBE_FILLED_MODE_COUNT 6

/* Keep the result predicate independent from the classic Toolbox application
 * so host tests can cover the exact readback and extension checks used by the
 * Mac OS 9 probe. */
static int gxmetal_agl_probe_readback_matches(
    const uint8_t triangle[3], const uint8_t background[3], uint32_t error)
{
    return error == 0 &&
           triangle[0] < 64 && triangle[1] > 176 && triangle[2] < 64 &&
           background[0] < 64 && background[1] < 64 &&
           background[2] > background[0] &&
           background[2] > background[1];
}

static int gxmetal_agl_probe_extended_readback_matches(
    const uint8_t texture[3], const uint8_t blend[3],
    const uint8_t depth[3], uint32_t error)
{
    return error == 0 &&
           texture[0] > 176 && texture[1] > 176 && texture[2] < 80 &&
           blend[0] > 96 && blend[0] < 224 && blend[1] < 64 &&
           blend[2] < 96 && blend[0] > (uint8_t)(blend[2] + 64) &&
           depth[0] > 176 && depth[1] < 64 && depth[2] < 64;
}

static int gxmetal_agl_probe_clipped_texture_matches(
    const uint8_t clipped_texture[3], uint32_t error)
{
    return error == 0 && clipped_texture[0] > 176 &&
           clipped_texture[1] > 176 && clipped_texture[2] < 80;
}

static int gxmetal_agl_probe_filled_modes_match(
    const uint8_t samples[GXMETAL_AGL_PROBE_FILLED_MODE_COUNT][3],
    const uint8_t triangle_list_guard[3], uint32_t error)
{
    size_t index;

    if (error != 0 || triangle_list_guard[0] >= 64 ||
        triangle_list_guard[1] >= 64 ||
        triangle_list_guard[2] <= triangle_list_guard[0] ||
        triangle_list_guard[2] <= triangle_list_guard[1]) {
        return 0;
    }
    for (index = 0; index < GXMETAL_AGL_PROBE_FILLED_MODE_COUNT; ++index) {
        if (samples[index][0] >= 64 || samples[index][1] <= 176 ||
            samples[index][2] >= 64) {
            return 0;
        }
    }
    return 1;
}

static int gxmetal_agl_probe_sampler_primary_matches(
    const uint8_t base_only[3], const uint8_t trilinear[3],
    const uint8_t asymmetric[3], uint32_t error)
{
    int red_blue_delta = (int)asymmetric[0] - (int)asymmetric[2];

    if (red_blue_delta < 0) {
        red_blue_delta = -red_blue_delta;
    }
    return error == 0 &&
           base_only[0] > 176 && base_only[1] < 64 && base_only[2] < 64 &&
           trilinear[0] > 64 && trilinear[0] < 192 &&
           trilinear[1] > 64 && trilinear[1] < 192 && trilinear[2] < 64 &&
           asymmetric[0] > 64 && asymmetric[0] < 192 &&
           asymmetric[1] < 64 &&
           asymmetric[2] > 64 && asymmetric[2] < 192 &&
           red_blue_delta < 64;
}

static int gxmetal_agl_probe_sampler_unit1_matches(
    const uint8_t sample[3], uint32_t error)
{
    return error == 0 && sample[0] > 64 && sample[0] < 192 &&
           sample[1] > 64 && sample[1] < 192 && sample[2] > 176;
}

static int gxmetal_agl_probe_has_extension(const char *extensions,
                                            const char *name)
{
    const char *candidate;
    size_t name_length;

    if (extensions == NULL || name == NULL || *name == '\0' || *name == ' ') {
        return 0;
    }
    name_length = 0;
    while (name[name_length] != '\0') {
        if (name[name_length] == ' ') {
            return 0;
        }
        name_length++;
    }
    candidate = extensions;
    while (*candidate != '\0') {
        size_t index = 0;

        while (candidate[index] != '\0' && candidate[index] != ' ' &&
               index < name_length && candidate[index] == name[index]) {
            index++;
        }
        if (index == name_length &&
            (candidate[index] == '\0' || candidate[index] == ' ')) {
            return 1;
        }
        while (*candidate != '\0' && *candidate != ' ') {
            candidate++;
        }
        while (*candidate == ' ') {
            candidate++;
        }
    }
    return 0;
}

#endif
