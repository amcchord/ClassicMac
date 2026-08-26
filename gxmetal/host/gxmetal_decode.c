/* SPDX-License-Identifier: MIT */

#include "gxmetal_decode.h"

static uint32_t gxmetal_min_packet_bytes(uint16_t opcode)
{
    switch (opcode) {
    case GXMETAL_OP_PAD:
    case GXMETAL_OP_RESET:
    case GXMETAL_OP_CONTEXT_DESTROY:
    case GXMETAL_OP_FENCE:
        return 16;
    case GXMETAL_OP_TEXTURE_DESTROY:
    case GXMETAL_OP_BITMAP_DESTROY:
    case GXMETAL_OP_BEGIN_FRAME:
    case GXMETAL_OP_END_FRAME:
    case GXMETAL_OP_PRESENT:
    case GXMETAL_OP_SET_STATE:
    case GXMETAL_OP_READBACK:
    case GXMETAL_OP_SET_CLIP_RECTS:
        return 32;
    case GXMETAL_OP_CONTEXT_CREATE:
    case GXMETAL_OP_TEXTURE_CREATE:
    case GXMETAL_OP_TEXTURE_UPLOAD:
    case GXMETAL_OP_BITMAP_CREATE:
    case GXMETAL_OP_BITMAP_UPLOAD:
    case GXMETAL_OP_DRAW_BUFFER_WRITEBACK:
        return 48;
    case GXMETAL_OP_CLEAR:
    case GXMETAL_OP_DRAW_GOURAUD:
    case GXMETAL_OP_DRAW_BITMAP:
        return 64;
    case GXMETAL_OP_DRAW_TEXTURED:
        return 96;
    default:
        return 0;
    }
}

GXMetalDecodeResult gxmetal_decode_packet(const void *bytes,
                                          size_t available_bytes,
                                          GXMetalPacketView *view)
{
    const uint8_t *packet = (const uint8_t *)bytes;
    uint16_t opcode;
    uint16_t header_bytes;
    uint32_t packet_bytes;
    uint32_t minimum;

    if (packet == NULL || view == NULL ||
        available_bytes < GXMETAL_PACKET_HEADER_BYTES) {
        return GXMETAL_DECODE_TRUNCATED;
    }

    opcode = gxmetal_load_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET);
    header_bytes = gxmetal_load_le16(
        packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET);
    packet_bytes = gxmetal_load_le32(packet + GXMETAL_PACKET_BYTES_OFFSET);
    minimum = gxmetal_min_packet_bytes(opcode);

    if (header_bytes != GXMETAL_PACKET_HEADER_BYTES) {
        return GXMETAL_DECODE_BAD_HEADER;
    }
    if (minimum == 0) {
        return GXMETAL_DECODE_BAD_OPCODE;
    }
    if (packet_bytes < minimum || packet_bytes > GXMETAL_MAX_PACKET_BYTES) {
        return GXMETAL_DECODE_BAD_SIZE;
    }
    if ((packet_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        return GXMETAL_DECODE_BAD_ALIGNMENT;
    }
    if ((size_t)packet_bytes > available_bytes) {
        return GXMETAL_DECODE_TRUNCATED;
    }

    view->opcode = opcode;
    view->packet_bytes = packet_bytes;
    view->context_id = gxmetal_load_le32(
        packet + GXMETAL_PACKET_CONTEXT_OFFSET);
    view->sequence = gxmetal_load_le32(
        packet + GXMETAL_PACKET_SEQUENCE_OFFSET);
    view->payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    view->payload_bytes = packet_bytes - GXMETAL_PACKET_HEADER_BYTES;
    return GXMETAL_DECODE_OK;
}

GXMetalDecodeResult gxmetal_ring_advance(uint32_t position,
                                         uint32_t packet_bytes,
                                         uint32_t ring_bytes,
                                         uint32_t *next_position)
{
    if (next_position == NULL || ring_bytes < GXMETAL_PACKET_ALIGNMENT ||
        (ring_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        position >= ring_bytes ||
        (position & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        packet_bytes < GXMETAL_PACKET_HEADER_BYTES ||
        (packet_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        return GXMETAL_DECODE_BAD_ALIGNMENT;
    }
    if (packet_bytes > ring_bytes - position) {
        return GXMETAL_DECODE_BAD_RANGE;
    }

    *next_position = position + packet_bytes;
    if (*next_position == ring_bytes) {
        *next_position = 0;
    }
    return GXMETAL_DECODE_OK;
}

static int gxmetal_pixel_format_valid(uint32_t format)
{
    return format >= GXMETAL_PIXEL_RGB555 &&
           format <= GXMETAL_PIXEL_RGB24;
}

static int gxmetal_draw_pixel_format_valid(uint32_t format)
{
    return format == GXMETAL_PIXEL_RGB555 ||
           format == GXMETAL_PIXEL_ARGB8888 ||
           format == GXMETAL_PIXEL_RGB8888;
}

static int gxmetal_primitive_count_valid(uint32_t primitive, uint32_t count)
{
    switch (primitive) {
    case GXMETAL_PRIMITIVE_POINT:
        return count >= 1;
    case GXMETAL_PRIMITIVE_LINE:
        return count >= 2 && (count & 1) == 0;
    case GXMETAL_PRIMITIVE_TRIANGLE:
        return count >= 3 && count % 3 == 0;
    case GXMETAL_PRIMITIVE_TRIANGLE_STRIP:
    case GXMETAL_PRIMITIVE_TRIANGLE_FAN:
        return count >= 3;
    default:
        return 0;
    }
}

static uint32_t gxmetal_validate_draw(const GXMetalPacketView *packet,
                                      uint32_t vertex_bytes,
                                      uint32_t alternate_vertex_bytes)
{
    uint32_t primitive;
    uint32_t count;
    uint32_t stride;
    uint32_t flags;
    uint64_t expected;

    if (packet->payload_bytes < GXMETAL_DRAW_HEADER_BYTES) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    primitive = gxmetal_load_le32(packet->payload +
                                  GXMETAL_DRAW_PRIMITIVE_OFFSET);
    count = gxmetal_load_le32(packet->payload +
                              GXMETAL_DRAW_VERTEX_COUNT_OFFSET);
    stride = gxmetal_load_le32(packet->payload +
                               GXMETAL_DRAW_VERTEX_STRIDE_OFFSET);
    flags = gxmetal_load_le32(packet->payload + GXMETAL_DRAW_FLAGS_OFFSET);
    if (stride != vertex_bytes && stride != alternate_vertex_bytes) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    expected = GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
               (uint64_t)count * stride;
    if ((flags & ~GXMETAL_DRAW_FLAGS_VALID) != 0 ||
        (flags != GXMETAL_DRAW_NONE &&
         primitive != GXMETAL_PRIMITIVE_TRIANGLE) ||
        !gxmetal_primitive_count_valid(primitive, count) ||
        expected != packet->packet_bytes || expected > GXMETAL_MAX_PACKET_BYTES) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    return GXMETAL_ERROR_NONE;
}

uint32_t gxmetal_validate_packet(const GXMetalPacketView *packet,
                                 uint32_t shared_bytes)
{
    const uint8_t *payload;
    uint32_t tag;
    uint32_t type;
    uint32_t value;

    if (packet == NULL) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    payload = packet->payload;

    switch (packet->opcode) {
    case GXMETAL_OP_PAD:
        return packet->context_id == 0 && packet->sequence == 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    case GXMETAL_OP_RESET:
        return packet->packet_bytes == 16 && packet->context_id == 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    case GXMETAL_OP_FENCE:
        return packet->packet_bytes == 16 && packet->context_id == 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;

    case GXMETAL_OP_CONTEXT_CREATE:
        if (packet->packet_bytes != GXMETAL_CONTEXT_CREATE_PACKET_BYTES ||
            packet->context_id == 0 ||
            gxmetal_load_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET) == 0 ||
            gxmetal_load_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET) >
                GXMETAL_MAX_DIMENSION ||
            gxmetal_load_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET) == 0 ||
            gxmetal_load_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET) >
                GXMETAL_MAX_DIMENSION ||
            gxmetal_load_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET) == 0 ||
            !gxmetal_draw_pixel_format_valid(gxmetal_load_le32(
                payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET))) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        return GXMETAL_ERROR_NONE;
    case GXMETAL_OP_CONTEXT_DESTROY:
        return packet->packet_bytes == 16 && packet->context_id != 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    case GXMETAL_OP_BEGIN_FRAME:
    case GXMETAL_OP_END_FRAME:
    case GXMETAL_OP_PRESENT:
        return packet->packet_bytes == 32 && packet->context_id != 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    case GXMETAL_OP_READBACK:
    {
        uint32_t length = gxmetal_load_le32(
            payload + GXMETAL_READBACK_LENGTH_OFFSET);
        uint32_t row_bytes = gxmetal_load_le32(
            payload + GXMETAL_READBACK_ROW_BYTES_OFFSET);

        return packet->packet_bytes == GXMETAL_READBACK_PACKET_BYTES &&
               packet->context_id != 0 && row_bytes != 0 &&
               length >= row_bytes && length % row_bytes == 0 &&
               gxmetal_load_le32(
                   payload + GXMETAL_READBACK_RESERVED_OFFSET) == 0 &&
               gxmetal_shared_range_valid(
                   gxmetal_load_le32(
                       payload + GXMETAL_READBACK_SHARED_OFFSET_OFFSET),
                   length, shared_bytes, 16) ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    }
    case GXMETAL_OP_DRAW_BUFFER_WRITEBACK:
    {
        uint32_t length = gxmetal_load_le32(
            payload + GXMETAL_DRAW_BUFFER_WRITEBACK_LENGTH_OFFSET);
        uint32_t row_bytes = gxmetal_load_le32(
            payload + GXMETAL_DRAW_BUFFER_WRITEBACK_ROW_BYTES_OFFSET);
        const uint8_t *rect = payload +
            GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET;
        uint32_t left = gxmetal_load_le32(rect + GXMETAL_RECT_LEFT_OFFSET);
        uint32_t top = gxmetal_load_le32(rect + GXMETAL_RECT_TOP_OFFSET);
        uint32_t right = gxmetal_load_le32(rect + GXMETAL_RECT_RIGHT_OFFSET);
        uint32_t bottom = gxmetal_load_le32(
            rect + GXMETAL_RECT_BOTTOM_OFFSET);

        return packet->packet_bytes ==
                   GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES &&
               packet->context_id != 0 && row_bytes != 0 &&
               length >= row_bytes && length % row_bytes == 0 &&
               gxmetal_load_le32(payload +
                   GXMETAL_DRAW_BUFFER_WRITEBACK_RESERVED_OFFSET) == 0 &&
               left < right && top < bottom &&
               right <= GXMETAL_MAX_DIMENSION &&
               bottom <= GXMETAL_MAX_DIMENSION &&
               gxmetal_shared_range_valid(
                   gxmetal_load_le32(payload +
                       GXMETAL_DRAW_BUFFER_WRITEBACK_SHARED_OFFSET_OFFSET),
                   length, shared_bytes, GXMETAL_PACKET_ALIGNMENT) ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
    }
    case GXMETAL_OP_SET_CLIP_RECTS:
    {
        uint32_t count = gxmetal_load_le32(
            payload + GXMETAL_CLIP_RECTS_COUNT_OFFSET);
        uint64_t expected = GXMETAL_CLIP_RECTS_BASE_PACKET_BYTES +
            (uint64_t)count * GXMETAL_RECT_PAYLOAD_BYTES;
        uint32_t i;

        if (packet->context_id == 0 || count > GXMETAL_MAX_CLIP_RECTS ||
            expected != packet->packet_bytes ||
            gxmetal_load_le32(payload +
                              GXMETAL_CLIP_RECTS_RESERVED0_OFFSET) != 0 ||
            gxmetal_load_le32(payload +
                              GXMETAL_CLIP_RECTS_RESERVED1_OFFSET) != 0 ||
            gxmetal_load_le32(payload +
                              GXMETAL_CLIP_RECTS_RESERVED2_OFFSET) != 0) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        for (i = 0; i < count; i++) {
            const uint8_t *rect = payload + GXMETAL_CLIP_RECTS_RECTS_OFFSET +
                i * GXMETAL_RECT_PAYLOAD_BYTES;
            uint32_t left = gxmetal_load_le32(
                rect + GXMETAL_RECT_LEFT_OFFSET);
            uint32_t top = gxmetal_load_le32(
                rect + GXMETAL_RECT_TOP_OFFSET);
            uint32_t right = gxmetal_load_le32(
                rect + GXMETAL_RECT_RIGHT_OFFSET);
            uint32_t bottom = gxmetal_load_le32(
                rect + GXMETAL_RECT_BOTTOM_OFFSET);

            if (left >= right || top >= bottom ||
                right > GXMETAL_MAX_DIMENSION ||
                bottom > GXMETAL_MAX_DIMENSION) {
                return GXMETAL_ERROR_BAD_PACKET;
            }
        }
        return GXMETAL_ERROR_NONE;
    }

    case GXMETAL_OP_SET_STATE:
        if (packet->packet_bytes != GXMETAL_SET_STATE_PACKET_BYTES ||
            packet->context_id == 0) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        tag = gxmetal_load_le32(payload + GXMETAL_STATE_TAG_OFFSET);
        type = gxmetal_load_le32(payload + GXMETAL_STATE_TYPE_OFFSET);
        value = gxmetal_load_le32(payload + GXMETAL_STATE_VALUE_OFFSET);
        if (type > GXMETAL_STATE_RESOURCE) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        if (tag == GXMETAL_STATE_CHANNEL_MASK ||
            tag == GXMETAL_STATE_GL_DRAW_BUFFER) {
            return type == GXMETAL_STATE_UINT32 &&
                   (value & ~UINT32_C(0x0f)) == 0 ?
                GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
        }
        if (tag == GXMETAL_STATE_Z_BUFFER_MASK) {
            return type == GXMETAL_STATE_UINT32 && value <= 1u ?
                GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;
        }
        return GXMETAL_ERROR_NONE;
    case GXMETAL_OP_CLEAR:
        return packet->packet_bytes == GXMETAL_CLEAR_PACKET_BYTES &&
               packet->context_id != 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;

    case GXMETAL_OP_DRAW_GOURAUD:
        if (packet->context_id == 0) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        return gxmetal_validate_draw(packet, GXMETAL_GOURAUD_VERTEX_BYTES,
                                     GXMETAL_GOURAUD_VERTEX_BYTES);
    case GXMETAL_OP_DRAW_TEXTURED:
        if (packet->context_id == 0) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        return gxmetal_validate_draw(packet, GXMETAL_TEXTURE_VERTEX_BYTES,
                                     GXMETAL_MULTI_TEXTURE_VERTEX_BYTES);
    case GXMETAL_OP_DRAW_BITMAP:
        return packet->packet_bytes == 64 && packet->context_id != 0 ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_PACKET;

    case GXMETAL_OP_TEXTURE_CREATE:
    case GXMETAL_OP_BITMAP_CREATE:
        if (packet->packet_bytes != GXMETAL_RESOURCE_CREATE_PACKET_BYTES ||
            packet->context_id != 0 ||
            gxmetal_load_le32(payload + GXMETAL_RESOURCE_ID_OFFSET) == 0 ||
            gxmetal_load_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET) == 0 ||
            gxmetal_load_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET) == 0 ||
            gxmetal_load_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET) == 0 ||
            !gxmetal_pixel_format_valid(gxmetal_load_le32(
                payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET))) {
            return GXMETAL_ERROR_BAD_RESOURCE;
        }
        return GXMETAL_ERROR_NONE;
    case GXMETAL_OP_TEXTURE_UPLOAD:
    case GXMETAL_OP_BITMAP_UPLOAD:
    {
        uint32_t length;
        uint32_t row_bytes;
        uint32_t width;
        uint32_t height;

        if (packet->packet_bytes != GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES ||
            packet->context_id != 0) {
            return GXMETAL_ERROR_BAD_RESOURCE;
        }
        length = gxmetal_load_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET);
        row_bytes = gxmetal_load_le32(payload +
                                      GXMETAL_UPLOAD_ROW_BYTES_OFFSET);
        width = gxmetal_load_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET);
        height = gxmetal_load_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET);
        if (gxmetal_load_le32(payload +
                              GXMETAL_UPLOAD_RESOURCE_ID_OFFSET) == 0 ||
            row_bytes == 0 || width == 0 || height == 0 ||
            (uint64_t)row_bytes * height > length ||
            !gxmetal_shared_range_valid(
                gxmetal_load_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET),
                length,
                shared_bytes, 16)) {
            return GXMETAL_ERROR_BAD_RESOURCE;
        }
        return GXMETAL_ERROR_NONE;
    }
    case GXMETAL_OP_TEXTURE_DESTROY:
    case GXMETAL_OP_BITMAP_DESTROY:
        if (packet->packet_bytes != GXMETAL_RESOURCE_DESTROY_PACKET_BYTES ||
            packet->context_id != 0 ||
            gxmetal_load_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET) == 0) {
            return GXMETAL_ERROR_BAD_RESOURCE;
        }
        return GXMETAL_ERROR_NONE;
    default:
        return GXMETAL_ERROR_BAD_OPCODE;
    }
}

int gxmetal_shared_range_valid(uint32_t offset, uint32_t length,
                               uint32_t shared_bytes, uint32_t alignment)
{
    uint64_t end;

    if (length == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        (offset & (alignment - 1)) != 0) {
        return 0;
    }
    end = (uint64_t)offset + (uint64_t)length;
    return offset >= GXMETAL_UPLOAD_OFFSET && end <= shared_bytes;
}

const char *gxmetal_decode_result_string(GXMetalDecodeResult result)
{
    switch (result) {
    case GXMETAL_DECODE_OK:            return "ok";
    case GXMETAL_DECODE_TRUNCATED:     return "truncated packet";
    case GXMETAL_DECODE_BAD_HEADER:    return "invalid header";
    case GXMETAL_DECODE_BAD_SIZE:      return "invalid packet size";
    case GXMETAL_DECODE_BAD_ALIGNMENT: return "invalid alignment";
    case GXMETAL_DECODE_BAD_OPCODE:    return "unknown opcode";
    case GXMETAL_DECODE_BAD_RANGE:     return "invalid shared-memory range";
    default:                           return "unknown decoder result";
    }
}
