/*
 * GXMetal paravirtual command transport for QEMU std-VGA.
 *
 * Metal is the primary renderer on macOS.  The portable reference rasterizer
 * remains the deterministic fallback on other hosts.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "system/reset.h"
#include "ui/console.h"
#include "ui/input.h"

#include "gxmetal_qemu.h"

#define GXMETAL_CONSOLE_REFRESH_NS (NANOSECONDS_PER_SECOND / 60)

static void gxmetal_console_refresh(void *opaque)
{
    GXMetalQemuState *state = opaque;

    graphic_hw_update(state->console);
    state->last_console_refresh_ns =
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

static void gxmetal_console_present(GXMetalQemuState *state)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    int64_t deadline = state->last_console_refresh_ns +
                       GXMETAL_CONSOLE_REFRESH_NS;

    if (state->last_console_refresh_ns == 0 || now >= deadline) {
        timer_del(state->console_refresh_timer);
        gxmetal_console_refresh(state);
        return;
    }

    /* Coalesce faster guest presents into one host scanout refresh.  The
     * timer is deliberately retained for the trailing frame: if the guest
     * stops presenting before the next 60 Hz boundary, VNC and Cocoa still
     * receive the newest contents of shared VGA VRAM. */
    timer_mod_ns(state->console_refresh_timer, deadline);
}

static uint32_t gxmetal_render_dispatch(void *opaque,
                                        const GXMetalPacketView *packet)
{
    GXMetalQemuState *state = opaque;
    GXMetalDirtyRange dirty_range;
    GXMetalDirtyResult dirty_result;
    uint32_t error;

    if (state->metal != NULL) {
        error = gxmetal_metal_dispatch(state->metal, packet);
    } else {
        error = gxmetal_renderer_dispatch(&state->renderer, packet);
    }
    if (error != GXMETAL_ERROR_NONE) {
        return error;
    }

    gxmetal_dirty_observe_success(&state->dirty, packet);
    if (state->metal != NULL) {
        if (packet->opcode == GXMETAL_OP_PRESENT) {
            dirty_result = gxmetal_dirty_present_range(
                &state->dirty, packet, &dirty_range);
            if (dirty_result == GXMETAL_DIRTY_RANGE) {
                memory_region_set_dirty(state->framebuffer_region,
                                        dirty_range.offset,
                                        dirty_range.length);
            } else if (dirty_result == GXMETAL_DIRTY_FALLBACK) {
                memory_region_set_dirty(state->framebuffer_region, 0,
                                        state->renderer.framebuffer_bytes);
            }
            /* Metal writes directly into the shared VGA VRAM allocation.
             * Dirty bits alone are insufficient when untracked scanout is
             * active: no CPU store wakes the display backend, so VNC and
             * other frontends can retain the pre-game frame indefinitely.
             * Refresh after Metal has completed the VRAM write, but do not
             * make an 85+ FPS guest pay for redundant 15-bit VGA conversion
             * more often than the 60 Hz host console can display it. */
            gxmetal_console_present(state);
        }
    } else if (packet->opcode == GXMETAL_OP_CLEAR ||
               packet->opcode == GXMETAL_OP_DRAW_GOURAUD) {
        /* The portable oracle renders directly into guest VRAM. */
        memory_region_set_dirty(state->framebuffer_region, 0,
                                state->renderer.framebuffer_bytes);
    }
    return GXMETAL_ERROR_NONE;
}

static uint64_t gxmetal_register_read(void *opaque, hwaddr address,
                                      unsigned size)
{
    GXMetalQemuState *state = opaque;

    (void)size;

    switch (address) {
    case GXMETAL_REG_MAGIC:
        return GXMETAL_PROTOCOL_MAGIC;
    case GXMETAL_REG_VERSION:
        return GXMETAL_PROTOCOL_VERSION;
    case GXMETAL_REG_REGISTER_BYTES:
        return GXMETAL_REGISTER_BYTES;
    case GXMETAL_REG_FEATURES_LO:
        return (uint32_t)state->features;
    case GXMETAL_REG_FEATURES_HI:
        return state->features >> 32;
    case GXMETAL_REG_SHARED_BYTES:
        return GXMETAL_SHARED_BYTES;
    case GXMETAL_REG_RING_OFFSET:
        return GXMETAL_RING_OFFSET;
    case GXMETAL_REG_RING_BYTES:
        return GXMETAL_RING_BYTES;
    case GXMETAL_REG_PRODUCER:
        return state->queue.producer;
    case GXMETAL_REG_CONSUMER:
        return state->queue.consumer;
    case GXMETAL_REG_STATUS:
        return state->queue.status;
    case GXMETAL_REG_ERROR:
        return state->queue.error;
    case GXMETAL_REG_COMPLETED_SEQUENCE:
        return state->queue.completed_sequence;
    case GXMETAL_REG_DIAGNOSTIC:
        return state->queue.diagnostic;
    case GXMETAL_REG_RELATIVE_INPUT:
        return state->relative_input;
    default:
        return 0;
    }
}

static void gxmetal_log_new_fault(GXMetalQemuState *state, uint32_t old_status)
{
    if (!(old_status & GXMETAL_STATUS_FAULTED) &&
        (state->queue.status & GXMETAL_STATUS_FAULTED)) {
        const uint8_t *packet = state->queue.shared +
            state->queue.ring_offset + state->queue.consumer;

        qemu_log_mask(LOG_GUEST_ERROR,
                      "GXMetal: queue fault %u at consumer 0x%x "
                      "(producer 0x%x), packet "
                      "op=%u bytes=%u context=%u sequence=%u "
                      "payload=%08x/%08x/%08x/%08x\n",
                      state->queue.error, state->queue.consumer,
                      state->queue.producer,
                      gxmetal_load_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_BYTES_OFFSET),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_CONTEXT_OFFSET),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_SEQUENCE_OFFSET),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_HEADER_BYTES),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_HEADER_BYTES + 4),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_HEADER_BYTES + 8),
                      gxmetal_load_le32(packet + GXMETAL_PACKET_HEADER_BYTES + 12));
    }
}

static void gxmetal_register_write(void *opaque, hwaddr address,
                                   uint64_t value, unsigned size)
{
    GXMetalQemuState *state = opaque;
    uint32_t old_status = state->queue.status;

    (void)size;

    switch (address) {
    case GXMETAL_REG_PRODUCER:
        gxmetal_queue_publish(&state->queue, (uint32_t)value);
        break;
    case GXMETAL_REG_DOORBELL:
        gxmetal_queue_process(&state->queue);
        break;
    case GXMETAL_REG_RESET:
        if (value == GXMETAL_RESET_KEY) {
            gxmetal_qemu_reset(state);
        }
        break;
    case GXMETAL_REG_RELATIVE_INPUT:
        if (state->features & GXMETAL_FEATURE_RELATIVE_INPUT) {
            state->relative_input = value != 0;
            qemu_input_set_relative_mode(state->relative_input);
        }
        break;
    default:
        break;
    }
    gxmetal_log_new_fault(state, old_status);
}

static const MemoryRegionOps gxmetal_register_ops = {
    .read = gxmetal_register_read,
    .write = gxmetal_register_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void gxmetal_qemu_system_reset(void *opaque)
{
    gxmetal_qemu_reset(opaque);
}

bool gxmetal_qemu_init(GXMetalQemuState *state, Object *owner,
                       MemoryRegion *framebuffer_region,
                       uint32_t framebuffer_bytes, QemuConsole *console,
                       Error **errp)
{
    uint8_t *shared;
    uint8_t *framebuffer;

    memset(state, 0, sizeof(*state));
    if (framebuffer_region == NULL || framebuffer_bytes == 0) {
        error_setg(errp, "GXMetal requires a framebuffer memory region");
        return false;
    }
    if (!memory_region_init_ram(&state->shared, owner, "gxmetal.shared",
                                GXMETAL_SHARED_BYTES, errp)) {
        return false;
    }
    shared = memory_region_get_ram_ptr(&state->shared);
    memset(shared, 0, GXMETAL_SHARED_BYTES);

    framebuffer = memory_region_get_ram_ptr(framebuffer_region);
    state->framebuffer_region = framebuffer_region;
    state->console = console;
    state->console_refresh_timer = timer_new_ns(
        QEMU_CLOCK_REALTIME, gxmetal_console_refresh, state);
    gxmetal_renderer_init(&state->renderer, framebuffer, framebuffer_bytes);
    gxmetal_dirty_init(&state->dirty, framebuffer_bytes);
    state->metal = gxmetal_metal_create(framebuffer, framebuffer_bytes,
                                         shared, GXMETAL_SHARED_BYTES);
    state->features = GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE;
    if (state->metal != NULL) {
        state->features |= GXMETAL_FEATURE_METAL |
                           GXMETAL_FEATURE_Z16 |
                           GXMETAL_FEATURE_BLEND |
                           GXMETAL_FEATURE_TEXTURE |
                           GXMETAL_FEATURE_DOUBLE_BUFFER |
                           GXMETAL_FEATURE_SCISSOR |
                           GXMETAL_FEATURE_FOG_DEPTH |
                           GXMETAL_FEATURE_ALPHA_TEST |
                           GXMETAL_FEATURE_RECT_CLIP |
                           GXMETAL_FEATURE_ATI_UV_TRANSFORM |
                           GXMETAL_FEATURE_RELATIVE_INPUT |
                           GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX;
    } else {
        state->features |= GXMETAL_FEATURE_TRACE;
    }
    gxmetal_queue_init(&state->queue, shared, GXMETAL_SHARED_BYTES,
                       GXMETAL_RING_OFFSET, GXMETAL_RING_BYTES,
                       gxmetal_render_dispatch, state);
    memory_region_init_io(&state->registers, owner, &gxmetal_register_ops,
                          state, "gxmetal.registers",
                          GXMETAL_REGISTER_BYTES);
    qemu_register_reset(gxmetal_qemu_system_reset, state);
    return true;
}

void gxmetal_qemu_reset(GXMetalQemuState *state)
{
    state->relative_input = false;
    qemu_input_set_relative_mode(false);
    timer_del(state->console_refresh_timer);
    state->last_console_refresh_ns = 0;
    gxmetal_queue_reset(&state->queue);
    gxmetal_dirty_reset(&state->dirty);
    gxmetal_metal_reset(state->metal);
    gxmetal_renderer_reset(&state->renderer);
}
