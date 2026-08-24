/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host/gxmetal_decode.h"

static unsigned failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

static void make_packet(uint8_t *packet, uint16_t opcode,
                        uint32_t bytes, uint32_t context, uint32_t sequence)
{
    memset(packet, 0, bytes <= 64 ? bytes : 64);
    gxmetal_store_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET, opcode);
    gxmetal_store_le16(packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                       GXMETAL_PACKET_HEADER_BYTES);
    gxmetal_store_le32(packet + GXMETAL_PACKET_BYTES_OFFSET, bytes);
    gxmetal_store_le32(packet + GXMETAL_PACKET_CONTEXT_OFFSET, context);
    gxmetal_store_le32(packet + GXMETAL_PACKET_SEQUENCE_OFFSET, sequence);
}

static void test_endian_helpers(void)
{
    uint8_t bytes[8];

    gxmetal_store_le16(bytes + 1, UINT16_C(0xa1b2));
    CHECK(bytes[1] == 0xb2 && bytes[2] == 0xa1);
    CHECK(gxmetal_load_le16(bytes + 1) == UINT16_C(0xa1b2));

    gxmetal_store_le32(bytes + 3, UINT32_C(0x89abcdef));
    CHECK(bytes[3] == 0xef && bytes[4] == 0xcd &&
          bytes[5] == 0xab && bytes[6] == 0x89);
    CHECK(gxmetal_load_le32(bytes + 3) == UINT32_C(0x89abcdef));
}

static void test_protocol_contract(void)
{
    CHECK(GXMETAL_PROTOCOL_VERSION == UINT32_C(0x0001000e));
    CHECK(GXMETAL_REG_RELATIVE_INPUT == 0x40);
    CHECK(GXMETAL_REG_RELATIVE_INPUT + sizeof(uint32_t) <=
          GXMETAL_REGISTER_BYTES);
    CHECK(GXMETAL_FEATURE_RELATIVE_INPUT == (UINT64_C(1) << 14));
    CHECK(GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX == (UINT64_C(1) << 15));
    CHECK(GXMETAL_FEATURE_RESOURCE_SUBREGION == (UINT64_C(1) << 16));
    CHECK(GXMETAL_FEATURE_INTENSITY_FORMATS == (UINT64_C(1) << 17));
    CHECK(GXMETAL_FEATURE_ALPHA1_FORMAT == (UINT64_C(1) << 18));
    CHECK(GXMETAL_FEATURE_RGB332_FORMAT == (UINT64_C(1) << 19));
    CHECK(GXMETAL_FEATURE_CHROMAKEY == (UINT64_C(1) << 20));
    CHECK(GXMETAL_MULTI_TEXTURE_VERTEX_BYTES == 80);
}

static void test_valid_packet(void)
{
    uint8_t packet[64];
    GXMetalPacketView view;

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, sizeof(packet), 7, 42);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_OK);
    CHECK(view.opcode == GXMETAL_OP_CONTEXT_CREATE);
    CHECK(view.packet_bytes == sizeof(packet));
    CHECK(view.context_id == 7);
    CHECK(view.sequence == 42);
    CHECK(view.payload == packet + GXMETAL_PACKET_HEADER_BYTES);
    CHECK(view.payload_bytes == sizeof(packet) - GXMETAL_PACKET_HEADER_BYTES);
}

static void test_rejected_packets(void)
{
    uint8_t packet[64];
    GXMetalPacketView view;

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, sizeof(packet), 0, 1);
    CHECK(gxmetal_decode_packet(packet, 15, &view) ==
          GXMETAL_DECODE_TRUNCATED);

    gxmetal_store_le16(packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET, 8);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_BAD_HEADER);

    make_packet(packet, UINT16_C(0x7fff), 16, 0, 1);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_BAD_OPCODE);

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 32, 0, 1);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_BAD_SIZE);

    make_packet(packet, GXMETAL_OP_SET_STATE, 34, 0, 1);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_BAD_ALIGNMENT);

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 64, 0, 1);
    CHECK(gxmetal_decode_packet(packet, 48, &view) ==
          GXMETAL_DECODE_TRUNCATED);
}

static void test_ring_boundaries(void)
{
    uint32_t next = UINT32_MAX;

    CHECK(gxmetal_ring_advance(0, 16, 64, &next) == GXMETAL_DECODE_OK);
    CHECK(next == 16);
    CHECK(gxmetal_ring_advance(48, 16, 64, &next) == GXMETAL_DECODE_OK);
    CHECK(next == 0);
    CHECK(gxmetal_ring_advance(48, 32, 64, &next) ==
          GXMETAL_DECODE_BAD_RANGE);
    CHECK(gxmetal_ring_advance(1, 16, 64, &next) ==
          GXMETAL_DECODE_BAD_ALIGNMENT);
    CHECK(gxmetal_ring_advance(0, 17, 64, &next) ==
          GXMETAL_DECODE_BAD_ALIGNMENT);
}

static void test_shared_ranges(void)
{
    CHECK(gxmetal_shared_range_valid(GXMETAL_UPLOAD_OFFSET, 4096,
                                     GXMETAL_SHARED_BYTES, 16));
    CHECK(gxmetal_shared_range_valid(GXMETAL_SHARED_BYTES - 16, 16,
                                     GXMETAL_SHARED_BYTES, 16));
    CHECK(!gxmetal_shared_range_valid(GXMETAL_RING_OFFSET, 16,
                                      GXMETAL_SHARED_BYTES, 16));
    CHECK(!gxmetal_shared_range_valid(GXMETAL_SHARED_BYTES - 16, 32,
                                      GXMETAL_SHARED_BYTES, 16));
    CHECK(!gxmetal_shared_range_valid(GXMETAL_UPLOAD_OFFSET + 1, 16,
                                      GXMETAL_SHARED_BYTES, 16));
    CHECK(!gxmetal_shared_range_valid(GXMETAL_UPLOAD_OFFSET, 16,
                                      GXMETAL_SHARED_BYTES, 3));
    CHECK(!gxmetal_shared_range_valid(GXMETAL_UPLOAD_OFFSET, 0,
                                      GXMETAL_SHARED_BYTES, 16));
}

static void test_semantic_validation(void)
{
    uint8_t packet[272];
    GXMetalPacketView view;

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD, 128, 2, 9);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    CHECK(gxmetal_decode_packet(packet, 128, &view) ==
          GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);

    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_BACKFACING);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_FLAGS_VALID << 1);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_BAD_PACKET);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_NONE);

    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 4);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_BAD_PACKET);

    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, sizeof(packet), 2, 10);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_MULTI_TEXTURE_VERTEX_BYTES);
    CHECK(gxmetal_decode_packet(packet, sizeof(packet), &view) ==
          GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 76);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_BAD_PACKET);

    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_LENGTH_OFFSET, 64);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 16);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_WIDTH_OFFSET, 4);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_HEIGHT_OFFSET, 4);
    CHECK(gxmetal_decode_packet(packet, 48, &view) == GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    gxmetal_store_le32(packet + 16 + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_RING_OFFSET);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_BAD_RESOURCE);

    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 16, 0, 1);
    CHECK(gxmetal_decode_packet(packet, 16, &view) ==
          GXMETAL_DECODE_BAD_SIZE);
}

int main(void)
{
    test_endian_helpers();
    test_protocol_contract();
    test_valid_packet();
    test_rejected_packets();
    test_ring_boundaries();
    test_shared_ranges();
    test_semantic_validation();

    if (failures != 0) {
        fprintf(stderr, "GXMetal protocol: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal protocol: all tests passed");
    return EXIT_SUCCESS;
}
