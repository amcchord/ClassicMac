/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_DIRTY_H
#define GXMETAL_DIRTY_H

#include <stdint.h>

#include "gxmetal_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GXMETAL_DIRTY_MAX_CONTEXTS 32u

typedef struct GXMetalDirtyContext {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t bytes_per_pixel;
    uint32_t framebuffer_offset;
    uint32_t clip_left;
    uint32_t clip_top;
    uint32_t clip_right;
    uint32_t clip_bottom;
    int active;
} GXMetalDirtyContext;

typedef struct GXMetalDirtyTracker {
    uint32_t framebuffer_bytes;
    GXMetalDirtyContext contexts[GXMETAL_DIRTY_MAX_CONTEXTS];
} GXMetalDirtyTracker;

typedef struct GXMetalDirtyRange {
    uint32_t offset;
    uint32_t length;
} GXMetalDirtyRange;

typedef enum GXMetalDirtyResult {
    GXMETAL_DIRTY_FALLBACK = -1,
    GXMETAL_DIRTY_EMPTY = 0,
    GXMETAL_DIRTY_RANGE = 1
} GXMetalDirtyResult;

void gxmetal_dirty_init(GXMetalDirtyTracker *tracker,
                        uint32_t framebuffer_bytes);
void gxmetal_dirty_reset(GXMetalDirtyTracker *tracker);
/* Call only after the renderer has accepted the packet. */
void gxmetal_dirty_observe_success(GXMetalDirtyTracker *tracker,
                                   const GXMetalPacketView *packet);
/* FALLBACK requests conservative full-VRAM invalidation; EMPTY requests none. */
GXMetalDirtyResult gxmetal_dirty_present_range(
    const GXMetalDirtyTracker *tracker, const GXMetalPacketView *packet,
    GXMetalDirtyRange *range);

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_DIRTY_H */
