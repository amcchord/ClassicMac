/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_RAVE_COMPATIBILITY_H
#define GXMETAL_RAVE_COMPATIBILITY_H

#include <stdint.h>

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

#endif
