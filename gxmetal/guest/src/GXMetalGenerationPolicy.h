/*
 * GXMetal rendering-generation ownership policy.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef GXMETAL_GENERATION_POLICY_H
#define GXMETAL_GENERATION_POLICY_H

#include <stdint.h>

typedef struct GXMetalGenerationOwner {
    uint32_t high;
    uint32_t low;
    int valid;
} GXMetalGenerationOwner;

static inline int gxmetal_generation_process_equal(
    const GXMetalGenerationOwner *owner, uint32_t high, uint32_t low)
{
    return owner->valid && owner->high == high && owner->low == low;
}

/* A fragment's first rendering context always establishes a host generation.
 * After that, a process change may establish another generation only across
 * an idle boundary.  A second process joining while contexts are live must
 * share their generation rather than invalidating them underneath the first
 * process. */
static inline int gxmetal_generation_should_reset(
    int generation_initialized, int has_live_contexts,
    const GXMetalGenerationOwner *owner, int process_known,
    uint32_t process_high, uint32_t process_low)
{
    if (!generation_initialized) {
        return 1;
    }
    return !has_live_contexts && process_known && owner->valid &&
           !gxmetal_generation_process_equal(owner, process_high,
                                              process_low);
}

static inline void gxmetal_generation_note_owner(
    GXMetalGenerationOwner *owner, int process_known,
    uint32_t process_high, uint32_t process_low)
{
    if (!process_known) {
        owner->valid = 0;
        return;
    }
    owner->high = process_high;
    owner->low = process_low;
    owner->valid = 1;
}

#endif
