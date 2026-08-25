/* SPDX-License-Identifier: MIT */

#include <stdio.h>

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

    for (mode = 0; mode <= GXMETAL_RAVE_FOG_MODE_MAX; mode++) {
        CHECK(gxmetal_rave_int_state_is_accepted(
            GXMETAL_RAVE_TAG_FOG_MODE, mode));
    }
    CHECK(!gxmetal_rave_int_state_is_accepted(
        GXMETAL_RAVE_TAG_FOG_MODE, UINT32_C(0x1eb1f0c8)));
    CHECK(gxmetal_rave_int_state_is_accepted(11, UINT32_C(0x1eb1f0c8)));

    if (failures != 0) {
        fprintf(stderr, "GXMetal RAVE compatibility: %u failures\n",
                failures);
        return 1;
    }
    puts("GXMetal RAVE compatibility: all tests passed");
    return 0;
}
