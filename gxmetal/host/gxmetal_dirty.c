/* SPDX-License-Identifier: MIT */

#include "gxmetal_dirty.h"

#include <string.h>

static uint32_t gxmetal_dirty_bytes_per_pixel(uint32_t format)
{
    switch (format) {
    case GXMETAL_PIXEL_RGB555:
        return 2;
    case GXMETAL_PIXEL_ARGB8888:
    case GXMETAL_PIXEL_RGB8888:
        return 4;
    default:
        return 0;
    }
}

static GXMetalDirtyContext *gxmetal_dirty_find_context(
    GXMetalDirtyTracker *tracker, uint32_t id)
{
    uint32_t i;

    for (i = 0; i < GXMETAL_DIRTY_MAX_CONTEXTS; i++) {
        if (tracker->contexts[i].active && tracker->contexts[i].id == id) {
            return &tracker->contexts[i];
        }
    }
    return NULL;
}

static const GXMetalDirtyContext *gxmetal_dirty_find_context_const(
    const GXMetalDirtyTracker *tracker, uint32_t id)
{
    uint32_t i;

    for (i = 0; i < GXMETAL_DIRTY_MAX_CONTEXTS; i++) {
        if (tracker->contexts[i].active && tracker->contexts[i].id == id) {
            return &tracker->contexts[i];
        }
    }
    return NULL;
}

void gxmetal_dirty_init(GXMetalDirtyTracker *tracker,
                        uint32_t framebuffer_bytes)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->framebuffer_bytes = framebuffer_bytes;
}

void gxmetal_dirty_reset(GXMetalDirtyTracker *tracker)
{
    uint32_t framebuffer_bytes = tracker->framebuffer_bytes;

    memset(tracker, 0, sizeof(*tracker));
    tracker->framebuffer_bytes = framebuffer_bytes;
}

static void gxmetal_dirty_track_context(GXMetalDirtyTracker *tracker,
                                        const GXMetalPacketView *packet)
{
    GXMetalDirtyContext *context = NULL;
    uint32_t flags;
    uint32_t pixel_format;
    uint32_t left_top;
    uint32_t right_bottom;
    uint32_t i;
    uint64_t end;

    if (packet->payload_bytes <
            GXMETAL_CONTEXT_CREATE_PACKET_BYTES -
                GXMETAL_PACKET_HEADER_BYTES ||
        gxmetal_dirty_find_context(tracker, packet->context_id) != NULL) {
        return;
    }
    for (i = 0; i < GXMETAL_DIRTY_MAX_CONTEXTS; i++) {
        if (!tracker->contexts[i].active) {
            context = &tracker->contexts[i];
            break;
        }
    }
    if (context == NULL) {
        return;
    }

    context->id = packet->context_id;
    context->width = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_WIDTH_OFFSET);
    context->height = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_HEIGHT_OFFSET);
    context->row_bytes = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET);
    pixel_format = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET);
    context->bytes_per_pixel = gxmetal_dirty_bytes_per_pixel(pixel_format);
    context->framebuffer_offset = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_FRAMEBUFFER_OFFSET);
    flags = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_FLAGS_OFFSET);
    context->clip_right = context->width;
    context->clip_bottom = context->height;
    if ((flags & GXMETAL_CONTEXT_RECT_CLIP) != 0) {
        left_top = gxmetal_load_le32(
            packet->payload + GXMETAL_CONTEXT_CLIP_LEFT_TOP_OFFSET);
        right_bottom = gxmetal_load_le32(
            packet->payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET);
        context->clip_left = left_top & UINT32_C(0xffff);
        context->clip_top = left_top >> 16;
        context->clip_right = right_bottom & UINT32_C(0xffff);
        context->clip_bottom = right_bottom >> 16;
    }

    end = (uint64_t)context->framebuffer_offset +
          (uint64_t)(context->height - 1) * context->row_bytes +
          (uint64_t)context->width * context->bytes_per_pixel;
    if (context->id == 0 || context->width == 0 || context->height == 0 ||
        context->bytes_per_pixel == 0 ||
        context->row_bytes <
            (uint64_t)context->width * context->bytes_per_pixel ||
        context->clip_left > context->clip_right ||
        context->clip_top > context->clip_bottom ||
        context->clip_right > context->width ||
        context->clip_bottom > context->height ||
        end > tracker->framebuffer_bytes) {
        memset(context, 0, sizeof(*context));
        return;
    }
    context->active = 1;
}

void gxmetal_dirty_observe_success(GXMetalDirtyTracker *tracker,
                                   const GXMetalPacketView *packet)
{
    GXMetalDirtyContext *context;

    if (tracker == NULL || packet == NULL) {
        return;
    }
    switch (packet->opcode) {
    case GXMETAL_OP_RESET:
        gxmetal_dirty_reset(tracker);
        break;
    case GXMETAL_OP_CONTEXT_CREATE:
        gxmetal_dirty_track_context(tracker, packet);
        break;
    case GXMETAL_OP_CONTEXT_DESTROY:
        context = gxmetal_dirty_find_context(tracker, packet->context_id);
        if (context != NULL) {
            memset(context, 0, sizeof(*context));
        }
        break;
    default:
        break;
    }
}

static GXMetalDirtyResult gxmetal_dirty_rect_range(
    const GXMetalDirtyTracker *tracker, const GXMetalPacketView *packet,
    uint32_t rect_offset, GXMetalDirtyRange *range)
{
    const GXMetalDirtyContext *context;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    uint64_t start;
    uint64_t end;

    if (tracker == NULL || packet == NULL || range == NULL ||
        packet->payload_bytes < rect_offset + GXMETAL_RECT_PAYLOAD_BYTES) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    context = gxmetal_dirty_find_context_const(tracker, packet->context_id);
    if (context == NULL) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    left = (int32_t)gxmetal_load_le32(
        packet->payload + rect_offset + GXMETAL_RECT_LEFT_OFFSET);
    top = (int32_t)gxmetal_load_le32(
        packet->payload + rect_offset + GXMETAL_RECT_TOP_OFFSET);
    right = (int32_t)gxmetal_load_le32(
        packet->payload + rect_offset + GXMETAL_RECT_RIGHT_OFFSET);
    bottom = (int32_t)gxmetal_load_le32(
        packet->payload + rect_offset + GXMETAL_RECT_BOTTOM_OFFSET);
    if (left > right || top > bottom) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (int32_t)context->width) {
        right = (int32_t)context->width;
    }
    if (bottom > (int32_t)context->height) {
        bottom = (int32_t)context->height;
    }
    if (left < (int32_t)context->clip_left) {
        left = (int32_t)context->clip_left;
    }
    if (top < (int32_t)context->clip_top) {
        top = (int32_t)context->clip_top;
    }
    if (right > (int32_t)context->clip_right) {
        right = (int32_t)context->clip_right;
    }
    if (bottom > (int32_t)context->clip_bottom) {
        bottom = (int32_t)context->clip_bottom;
    }
    if (left >= right || top >= bottom) {
        range->offset = 0;
        range->length = 0;
        return GXMETAL_DIRTY_EMPTY;
    }

    start = (uint64_t)context->framebuffer_offset +
            (uint64_t)(uint32_t)top * context->row_bytes +
            (uint64_t)(uint32_t)left * context->bytes_per_pixel;
    end = (uint64_t)context->framebuffer_offset +
          (uint64_t)((uint32_t)bottom - 1) * context->row_bytes +
          (uint64_t)(uint32_t)right * context->bytes_per_pixel;
    if (start >= end || end > tracker->framebuffer_bytes ||
        end - start > UINT32_MAX) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    range->offset = (uint32_t)start;
    range->length = (uint32_t)(end - start);
    return GXMETAL_DIRTY_RANGE;
}

GXMetalDirtyResult gxmetal_dirty_present_range(
    const GXMetalDirtyTracker *tracker, const GXMetalPacketView *packet,
    GXMetalDirtyRange *range)
{
    if (packet == NULL || packet->opcode != GXMETAL_OP_PRESENT) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    return gxmetal_dirty_rect_range(tracker, packet, 0, range);
}

GXMetalDirtyResult gxmetal_dirty_writeback_range(
    const GXMetalDirtyTracker *tracker, const GXMetalPacketView *packet,
    GXMetalDirtyRange *range)
{
    if (packet == NULL ||
        packet->opcode != GXMETAL_OP_DRAW_BUFFER_WRITEBACK) {
        return GXMETAL_DIRTY_FALLBACK;
    }
    return gxmetal_dirty_rect_range(
        tracker, packet, GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET, range);
}
