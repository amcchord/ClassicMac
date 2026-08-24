/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef HW_DISPLAY_GXMETAL_QEMU_H
#define HW_DISPLAY_GXMETAL_QEMU_H

#include "system/memory.h"
#include "qapi/error.h"
#include "qom/object.h"

#include "gxmetal_queue.h"
#include "gxmetal_dirty.h"
#include "gxmetal_metal.h"
#include "gxmetal_renderer.h"

typedef struct QemuConsole QemuConsole;
typedef struct QEMUTimer QEMUTimer;

typedef struct GXMetalQemuState {
    MemoryRegion registers;
    MemoryRegion shared;
    MemoryRegion *framebuffer_region;
    QemuConsole *console;
    QEMUTimer *console_refresh_timer;
    int64_t last_console_refresh_ns;
    GXMetalQueue queue;
    GXMetalDirtyTracker dirty;
    GXMetalMetalRenderer *metal;
    GXMetalRenderer renderer;
    uint64_t features;
    uint32_t active_contexts;
    bool relative_input;
    bool guest_cursor_visible;
    bool relative_input_effective;
} GXMetalQemuState;

bool gxmetal_qemu_init(GXMetalQemuState *state, Object *owner,
                       MemoryRegion *framebuffer_region,
                       uint32_t framebuffer_bytes, QemuConsole *console,
                       Error **errp);
void gxmetal_qemu_set_guest_cursor_visible(GXMetalQemuState *state,
                                           bool visible);
void gxmetal_qemu_reset(GXMetalQemuState *state);

#endif /* HW_DISPLAY_GXMETAL_QEMU_H */
