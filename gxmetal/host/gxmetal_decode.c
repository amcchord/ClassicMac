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
        return 32;
    case GXMETAL_OP_CONTEXT_CREATE:
    case GXMETAL_OP_TEXTURE_CREATE:
    case GXMETAL_OP_TEXTURE_UPLOAD:
    case GXMETAL_OP_BITMAP_CREATE:
    case GXMETAL_OP_BITMAP_UPLOAD:
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

int gxmetal_shared_range_valid(uint32_t offset, uint32_t length,
                               uint32_t shared_bytes, uint32_t alignment)
{
    uint64_t end;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
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
