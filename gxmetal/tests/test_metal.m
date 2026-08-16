/* SPDX-License-Identifier: MIT */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gxmetal_metal.h"

static unsigned failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

static void make_packet(uint8_t *packet, uint16_t opcode,
                        uint32_t bytes, uint32_t context)
{
    memset(packet, 0, bytes);
    gxmetal_store_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET, opcode);
    gxmetal_store_le16(packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                       GXMETAL_PACKET_HEADER_BYTES);
    gxmetal_store_le32(packet + GXMETAL_PACKET_BYTES_OFFSET, bytes);
    gxmetal_store_le32(packet + GXMETAL_PACKET_CONTEXT_OFFSET, context);
}

static void store_float(uint8_t *destination, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    gxmetal_store_le32(destination, bits);
}

static void set_vertex(uint8_t *bytes, float x, float y,
                       float r, float g, float b)
{
    store_float(bytes + GXMETAL_VERTEX_X_OFFSET, x);
    store_float(bytes + GXMETAL_VERTEX_Y_OFFSET, y);
    store_float(bytes + GXMETAL_VERTEX_Z_OFFSET, 0.5f);
    store_float(bytes + GXMETAL_VERTEX_INV_W_OFFSET, 1.0f);
    store_float(bytes + GXMETAL_VERTEX_R_OFFSET, r);
    store_float(bytes + GXMETAL_VERTEX_G_OFFSET, g);
    store_float(bytes + GXMETAL_VERTEX_B_OFFSET, b);
    store_float(bytes + GXMETAL_VERTEX_A_OFFSET, 1.0f);
}

static uint32_t dispatch(GXMetalMetalRenderer *renderer, uint8_t *bytes,
                         uint32_t length)
{
    GXMetalPacketView packet;
    CHECK(gxmetal_decode_packet(bytes, length, &packet) ==
          GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&packet, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    return gxmetal_metal_dispatch(renderer, &packet);
}

static void test_metal_triangle(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t packet[128];
    uint8_t *payload;
    uint8_t *vertices;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer));

    if (renderer == NULL) {
        puts("GXMetal Metal: no Metal device; test skipped");
        return;
    }
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 1);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 1);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD, 128, 1);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 32);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_vertex(vertices, 8.0f, 8.0f, 1.0f, 0.0f, 0.0f);
    set_vertex(vertices + 32, 56.0f, 8.0f, 0.0f, 1.0f, 0.0f);
    set_vertex(vertices + 64, 32.0f, 56.0f, 0.0f, 0.0f, 1.0f);
    CHECK(dispatch(renderer, packet, 128) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_CLEAR, 64, 1);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_LEFT_OFFSET, 48);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_TOP_OFFSET, 48);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_PRESENT, 32, 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[(16 * 64 + 16) * 2] != 0 ||
          framebuffer[(16 * 64 + 16) * 2 + 1] != 0x1f);
    CHECK(framebuffer[(63 * 64 + 0) * 2] == 0 &&
          framebuffer[(63 * 64 + 0) * 2 + 1] == 0x1f);
    CHECK(framebuffer[(63 * 64 + 63) * 2] == 0x7c &&
          framebuffer[(63 * 64 + 63) * 2 + 1] == 0);
    gxmetal_metal_destroy(renderer);
}

int main(void)
{
    @autoreleasepool {
        test_metal_triangle();
    }
    if (failures != 0) {
        fprintf(stderr, "GXMetal Metal: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal Metal: first triangle passed");
    return EXIT_SUCCESS;
}
