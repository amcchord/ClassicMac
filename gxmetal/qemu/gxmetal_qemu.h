/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_DISPLAY_GXMETAL_QEMU_H
#define HW_DISPLAY_GXMETAL_QEMU_H

#include "system/memory.h"
#include "qapi/error.h"
#include "qom/object.h"

#include "gxmetal_queue.h"
#include "gxmetal_renderer.h"

typedef struct GXMetalQemuState {
    MemoryRegion registers;
    MemoryRegion shared;
    MemoryRegion *framebuffer_region;
    GXMetalQueue queue;
    GXMetalRenderer renderer;
    uint64_t features;
} GXMetalQemuState;

bool gxmetal_qemu_init(GXMetalQemuState *state, Object *owner,
                       MemoryRegion *framebuffer_region,
                       uint32_t framebuffer_bytes, Error **errp);
void gxmetal_qemu_reset(GXMetalQemuState *state);

#endif /* HW_DISPLAY_GXMETAL_QEMU_H */
