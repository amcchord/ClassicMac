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
    static const uint8_t packed[2][4] = {
        {0x40, 0x40, 0xff, 0xff},
        {0x80, 0x80, 0xff, 0xff}
    };
    static const uint8_t expected[2][10] = {
        {0, 255, 0, 0, 0, 0, 0, 0, 0, 255},
        {255, 0, 0, 0, 0, 0, 0, 0, 255, 0}
    };
    uint8_t expanded[2][10];

    for (mode = 0; mode <= GXMETAL_RAVE_FOG_MODE_MAX; mode++) {
        CHECK(gxmetal_rave_int_state_is_accepted(
            GXMETAL_RAVE_TAG_FOG_MODE, mode));
    }
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_FOG_MODE, UINT32_C(0x1eb1f0c8)));
    CHECK(gxmetal_rave_int_state_is_accepted(11, UINT32_C(0x1eb1f0c8)));

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
