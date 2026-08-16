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

static void set_vertex_z_alpha(uint8_t *bytes, float x, float y, float z,
                               float r, float g, float b, float a)
{
    set_vertex(bytes, x, y, r, g, b);
    store_float(bytes + GXMETAL_VERTEX_Z_OFFSET, z);
    store_float(bytes + GXMETAL_VERTEX_A_OFFSET, a);
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

static void set_int_state(GXMetalMetalRenderer *renderer, uint8_t *packet,
                          uint32_t context, uint32_t tag, uint32_t value)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_SET_STATE, 32, context);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_UINT32);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, value);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
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

static void draw_triangle(GXMetalMetalRenderer *renderer, uint8_t *packet,
                          uint32_t context, float z,
                          float r, float g, float b, float a)
{
    uint8_t *payload;
    uint8_t *vertices;

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD, 128, context);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 32);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_vertex_z_alpha(vertices, 8, 8, z, r, g, b, a);
    set_vertex_z_alpha(vertices + 32, 56, 8, z, r, g, b, a);
    set_vertex_z_alpha(vertices + 64, 32, 56, z, r, g, b, a);
    CHECK(dispatch(renderer, packet, 128) == GXMETAL_ERROR_NONE);
}

static void test_metal_depth_blend_and_double_buffer(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t packet[128];
    uint8_t *payload;
    uint16_t pixel;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer));

    if (renderer == NULL) {
        return;
    }
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 2);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FLAGS_OFFSET,
                       GXMETAL_CONTEXT_Z16 |
                       GXMETAL_CONTEXT_DOUBLE_BUFFER);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 2);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR | GXMETAL_CLEAR_DEPTH);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_DEPTH_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    set_int_state(renderer, packet, 2, GXMETAL_STATE_Z_FUNCTION,
                  GXMETAL_Z_LT);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_Z_BUFFER_MASK, 1);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_BLEND,
                  GXMETAL_BLEND_INTERPOLATE);

    draw_triangle(renderer, packet, 2, 0.75f, 1, 0, 0, 1);
    draw_triangle(renderer, packet, 2, 0.25f, 0, 1, 0, 1);
    draw_triangle(renderer, packet, 2, 0.90f, 0, 0, 1, 1);
    draw_triangle(renderer, packet, 2, 0.10f, 1, 0, 0, 0.5f);

    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_PRESENT, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    pixel = (uint16_t)((uint16_t)framebuffer[(24 * 64 + 32) * 2] << 8 |
                       framebuffer[(24 * 64 + 32) * 2 + 1]);
    CHECK(((pixel >> 10) & 31) >= 13 && ((pixel >> 10) & 31) <= 17);
    CHECK(((pixel >> 5) & 31) >= 13 && ((pixel >> 5) & 31) <= 17);
    CHECK((pixel & 31) == 0);
    gxmetal_metal_destroy(renderer);
}

int main(void)
{
    @autoreleasepool {
        test_metal_triangle();
        test_metal_depth_blend_and_double_buffer();
    }
    if (failures != 0) {
        fprintf(stderr, "GXMetal Metal: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal Metal: first triangle passed");
    return EXIT_SUCCESS;
}
