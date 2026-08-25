/* SPDX-License-Identifier: MIT */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

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

static void present_rect(GXMetalMetalRenderer *renderer, uint8_t *packet,
                         uint32_t context, int32_t left, int32_t top,
                         int32_t right, int32_t bottom)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_PRESENT, 32, context);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RECT_LEFT_OFFSET, (uint32_t)left);
    gxmetal_store_le32(payload + GXMETAL_RECT_TOP_OFFSET, (uint32_t)top);
    gxmetal_store_le32(payload + GXMETAL_RECT_RIGHT_OFFSET, (uint32_t)right);
    gxmetal_store_le32(payload + GXMETAL_RECT_BOTTOM_OFFSET,
                       (uint32_t)bottom);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
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

static void set_float_state(GXMetalMetalRenderer *renderer, uint8_t *packet,
                            uint32_t context, uint32_t tag, float value)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_SET_STATE, 32, context);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_FLOAT32);
    store_float(payload + GXMETAL_STATE_VALUE_OFFSET, value);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
}

static void set_resource_state(GXMetalMetalRenderer *renderer,
                               uint8_t *packet, uint32_t context,
                               uint32_t tag, uint32_t resource)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_SET_STATE, 32, context);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, resource);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
}

static void upload_single_pixel_texture(GXMetalMetalRenderer *renderer,
                                        uint8_t *packet, uint8_t *shared,
                                        uint32_t resource,
                                        uint32_t pixel_format,
                                        const uint8_t pixel[4])
{
    uint8_t *payload;

    memcpy(shared + GXMETAL_UPLOAD_OFFSET, pixel, 4);
    make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, resource);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       pixel_format);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, resource);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
}

static void set_texture_vertex(uint8_t *bytes, float x, float y,
                               float u, float v);

static void draw_textured_test_quad(GXMetalMetalRenderer *renderer,
                                    uint32_t context,
                                    int valid_secondary_coordinates)
{
    uint8_t packet[416];
    uint8_t *payload;
    uint8_t *vertices;
    uint32_t i;

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, context);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, context);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, context);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 1);
    for (i = 0; i < 6; i++) {
        float coordinate = valid_secondary_coordinates ? 0.0f : NAN;

        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_R_OFFSET,
                    coordinate);
        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_G_OFFSET,
                    coordinate);
        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_B_OFFSET,
                    valid_secondary_coordinates ? 1.0f : NAN);
    }
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, context);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, context, 0, 0, 64, 64);
}

static void draw_public_multitexture_test_quad(
    GXMetalMetalRenderer *renderer, uint32_t context)
{
    uint8_t packet[512];
    uint8_t *payload;
    uint8_t *vertices;
    uint32_t i;

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, context);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, context);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, sizeof(packet), context);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_MULTI_TEXTURE_VERTEX_BYTES);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES,
                       0, 64, 0, 1);
    for (i = 0; i < 6; i++) {
        uint8_t *vertex = vertices +
            i * GXMETAL_MULTI_TEXTURE_VERTEX_BYTES;

        /* Highlight is intentionally independent from the second UV set. */
        store_float(vertex + GXMETAL_VERTEX_KS_R_OFFSET, 0.25f);
        store_float(vertex + GXMETAL_VERTEX_KS_G_OFFSET, 0.0f);
        store_float(vertex + GXMETAL_VERTEX_KS_B_OFFSET, 0.0f);
        store_float(vertex + GXMETAL_VERTEX_MULTI_INV_W_OFFSET, 1.0f);
        store_float(vertex + GXMETAL_VERTEX_MULTI_U_OVER_W_OFFSET, 0.0f);
        store_float(vertex + GXMETAL_VERTEX_MULTI_V_OVER_W_OFFSET, 0.0f);
    }
    CHECK(dispatch(renderer, packet, sizeof(packet)) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, context);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, context, 0, 0, 64, 64);
}

static void test_metal_triangle(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t control[64];
    uint8_t packet[128];
    uint8_t *payload;
    uint8_t *vertices;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer), NULL, 0);

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
    set_vertex(vertices, -8.0f, -8.0f, 1.0f, 0.0f, 0.0f);
    /* invW is undefined for Gouraud vertices unless PerspectiveZ is active. */
    store_float(vertices + GXMETAL_VERTEX_INV_W_OFFSET, NAN);
    set_vertex(vertices + 32, 56.0f, 8.0f, 0.0f, 1.0f, 0.0f);
    set_vertex(vertices + 64, 32.0f, 56.0f, 0.0f, 0.0f, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_BACKFACING);
    CHECK(dispatch(renderer, packet, 128) == GXMETAL_ERROR_NONE);

    make_packet(control, GXMETAL_OP_END_FRAME, 32, 1);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, control, 1, 0, 0, 64, 64);
    CHECK(framebuffer[(16 * 64 + 16) * 2] != 0x00 ||
          framebuffer[(16 * 64 + 16) * 2 + 1] != 0x1f);

    make_packet(control, GXMETAL_OP_BEGIN_FRAME, 32, 1);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_NONE);
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
    present_rect(renderer, packet, 1, 0, 0, 64, 64);
    CHECK(framebuffer[(16 * 64 + 16) * 2] != 0 ||
          framebuffer[(16 * 64 + 16) * 2 + 1] != 0x1f);
    CHECK(framebuffer[(63 * 64 + 0) * 2] == 0 &&
          framebuffer[(63 * 64 + 0) * 2 + 1] == 0x1f);
    CHECK(framebuffer[(63 * 64 + 63) * 2] == 0x7c &&
          framebuffer[(63 * 64 + 63) * 2 + 1] == 0);
    gxmetal_metal_destroy(renderer);
}

static void draw_triangle_depth(GXMetalMetalRenderer *renderer,
                                uint8_t *packet, uint32_t context,
                                float z, float inv_w,
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
    store_float(vertices + GXMETAL_VERTEX_INV_W_OFFSET, inv_w);
    store_float(vertices + 32 + GXMETAL_VERTEX_INV_W_OFFSET, inv_w);
    store_float(vertices + 64 + GXMETAL_VERTEX_INV_W_OFFSET, inv_w);
    CHECK(dispatch(renderer, packet, 128) == GXMETAL_ERROR_NONE);
}

static void draw_triangle(GXMetalMetalRenderer *renderer, uint8_t *packet,
                          uint32_t context, float z,
                          float r, float g, float b, float a)
{
    draw_triangle_depth(renderer, packet, context, z, 1.0f,
                        r, g, b, a);
}

static void set_texture_vertex(uint8_t *bytes, float x, float y,
                               float u, float v)
{
    uint32_t i;
    float values[16] = {
        x, y, 0.5f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        u, v, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f
    };
    for (i = 0; i < 16; i++) {
        store_float(bytes + i * 4, values[i]);
    }
}

static void set_texture_vertex_depth(uint8_t *bytes, float x, float y,
                                     float z, float inv_w,
                                     float u, float v)
{
    set_texture_vertex(bytes, x, y, u, v);
    store_float(bytes + GXMETAL_VERTEX_Z_OFFSET, z);
    store_float(bytes + GXMETAL_VERTEX_INV_W_OFFSET, inv_w);
    store_float(bytes + GXMETAL_VERTEX_U_OVER_W_OFFSET, u * inv_w);
    store_float(bytes + GXMETAL_VERTEX_V_OVER_W_OFFSET, v * inv_w);
}

static void draw_textured_triangle_depth(GXMetalMetalRenderer *renderer,
                                         uint8_t *packet,
                                         uint32_t context,
                                         float z, float inv_w,
                                         float u, float v)
{
    enum {
        kPacketBytes = GXMETAL_PACKET_HEADER_BYTES +
            GXMETAL_DRAW_HEADER_BYTES + 3 * GXMETAL_TEXTURE_VERTEX_BYTES
    };
    uint8_t *payload;
    uint8_t *vertices;

    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, kPacketBytes, context);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_TEXTURE_VERTEX_BYTES);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex_depth(vertices, 8, 8, z, inv_w, u, v);
    set_texture_vertex_depth(vertices + GXMETAL_TEXTURE_VERTEX_BYTES,
                             56, 8, z, inv_w, u, v);
    set_texture_vertex_depth(vertices + 2 * GXMETAL_TEXTURE_VERTEX_BYTES,
                             32, 56, z, inv_w, u, v);
    CHECK(dispatch(renderer, packet, kPacketBytes) == GXMETAL_ERROR_NONE);
}

static void poison_unused_texture_color(uint8_t *bytes)
{
    store_float(bytes + GXMETAL_VERTEX_R_OFFSET, NAN);
    store_float(bytes + GXMETAL_VERTEX_G_OFFSET, NAN);
    store_float(bytes + GXMETAL_VERTEX_B_OFFSET, NAN);
}

static uint16_t framebuffer_pixel(const uint8_t *framebuffer,
                                  uint32_t x, uint32_t y)
{
    uint32_t offset = (y * 64 + x) * 2;
    return (uint16_t)((uint16_t)framebuffer[offset] << 8 |
                      framebuffer[offset + 1]);
}

static void test_metal_large_vertex_batches(void)
{
    enum {
        kTextureVertexCount = 96,
        kGouraudVertexCount = 129,
        kPacketBytes = GXMETAL_PACKET_HEADER_BYTES +
            GXMETAL_DRAW_HEADER_BYTES +
            kTextureVertexCount * GXMETAL_TEXTURE_VERTEX_BYTES
    };
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t *packet = calloc(1, kPacketBytes);
    uint8_t *shared = calloc(1, GXMETAL_SHARED_BYTES);
    uint8_t *payload;
    uint8_t *vertices;
    const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    GXMetalMetalRenderer *renderer;
    uint32_t i;

    CHECK(packet != NULL);
    CHECK(shared != NULL);
    if (packet == NULL || shared == NULL) {
        free(packet);
        free(shared);
        return;
    }
    renderer = gxmetal_metal_create(framebuffer, sizeof(framebuffer),
                                     shared, GXMETAL_SHARED_BYTES);
    if (renderer == NULL) {
        free(packet);
        free(shared);
        return;
    }
    upload_single_pixel_texture(renderer, packet, shared, 501,
                                GXMETAL_PIXEL_ARGB8888, white);
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 31);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    set_resource_state(renderer, packet, 31, GXMETAL_STATE_TEXTURE, 501);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 31);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, kPacketBytes, 31);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET,
                       kTextureVertexCount);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_TEXTURE_VERTEX_BYTES);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    for (i = 0; i < kTextureVertexCount; i += 3) {
        set_texture_vertex(vertices + (i + 0) * GXMETAL_TEXTURE_VERTEX_BYTES,
                           0, 0, 0, 0);
        set_texture_vertex(vertices + (i + 1) * GXMETAL_TEXTURE_VERTEX_BYTES,
                           64, 0, 1, 0);
        set_texture_vertex(vertices + (i + 2) * GXMETAL_TEXTURE_VERTEX_BYTES,
                           0, 64, 0, 1);
    }
    CHECK(dispatch(renderer, packet, kPacketBytes) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD,
                GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
                    kGouraudVertexCount * GXMETAL_GOURAUD_VERTEX_BYTES,
                31);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET,
                       kGouraudVertexCount);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    for (i = 0; i < kGouraudVertexCount; i += 3) {
        set_vertex(vertices + (i + 0) * GXMETAL_GOURAUD_VERTEX_BYTES,
                   0, 0, 1, 0, 0);
        set_vertex(vertices + (i + 1) * GXMETAL_GOURAUD_VERTEX_BYTES,
                   64, 0, 0, 1, 0);
        set_vertex(vertices + (i + 2) * GXMETAL_GOURAUD_VERTEX_BYTES,
                   0, 64, 0, 0, 1);
    }
    CHECK(dispatch(renderer, packet,
                   GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
                       kGouraudVertexCount * GXMETAL_GOURAUD_VERTEX_BYTES) ==
          GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 31);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 31, 0, 0, 64, 64);

    gxmetal_metal_destroy(renderer);
    free(packet);
    free(shared);
}

static void test_metal_texture_upload_and_sampling(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t control[64];
    uint8_t packet[416];
    uint8_t *shared = calloc(1, GXMETAL_SHARED_BYTES);
    uint8_t *payload;
    uint8_t *vertices;
    GXMetalMetalRenderer *renderer;

    CHECK(shared != NULL);
    if (shared == NULL) {
        return;
    }
    renderer = gxmetal_metal_create(framebuffer, sizeof(framebuffer),
                                     shared, GXMETAL_SHARED_BYTES);
    if (renderer == NULL) {
        free(shared);
        return;
    }
    /* Big-endian ARGB32: red, green, blue, white. */
    memcpy(shared + GXMETAL_UPLOAD_OFFSET,
           "\xff\xff\0\0\xff\0\xff\0"
           "\xff\0\0\xff\xff\xff\xff\xff", 16);

    make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, 7);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_ARGB8888);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 7);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 16);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 2);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    /* Dynamic RAVE resources update only their dirty rectangle. Replace the
     * upper-right texel with magenta and leave the other three untouched. */
    memcpy(shared + GXMETAL_UPLOAD_OFFSET, "\xff\xff\0\xff", 4);
    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 7);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 1);
    gxmetal_store_le32(payload +
                       GXMETAL_UPLOAD_DESTINATION_ORIGIN_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    /* OpenGLRendererATI passes raw GL filter enums through both the GL and
     * multitexture RAVE tags. Quake III uses GL_LINEAR_MIPMAP_NEAREST. */
    set_int_state(renderer, packet, 3,
                  GXMETAL_STATE_GL_TEXTURE_MIN_FILTER,
                  GXMETAL_GL_LINEAR_MIPMAP_NEAREST);
    set_int_state(renderer, packet, 3,
                  GXMETAL_STATE_MULTI_TEXTURE_MIN_FILTER,
                  GXMETAL_GL_LINEAR_MIPMAP_NEAREST);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_FILTER,
                  GXMETAL_TEXTURE_FILTER_FAST);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_WRAP_U,
                  GXMETAL_TEXTURE_WRAP_CLAMP);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_WRAP_V,
                  GXMETAL_TEXTURE_WRAP_CLAMP);
    make_packet(packet, GXMETAL_OP_SET_STATE, 32, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       GXMETAL_STATE_TEXTURE);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, 7);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 1);
    /* QuickDraw 3D is allowed to leave RGB undefined unless Decal is active.
     * This mirrors the first textured triangle observed from Nanosaur. */
    poison_unused_texture_color(vertices + 1 * 64);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_BACKFACING);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(control, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, control, 3, 0, 0, 64, 64);

    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 48, 48) == 0x7c1f);

    make_packet(control, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_NONE);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);

    /* RAVE defines V=0 at the lower edge of an ordinary top-down image.
     * Metal defines V=0 at the upper edge, so the host must invert V. */
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 48, 16) == 0x7fff);
    CHECK(framebuffer_pixel(framebuffer, 16, 48) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 48, 48) == 0x7c1f);

    /* Weekend Warrior submits near menu meshes with reciprocal-W values at
     * and above one.  A finite Perspective-Z mapping must preserve ordering
     * across that range instead of saturating both meshes onto depth zero.
     * Draw a farther blue textured triangle first, then a nearer red one;
     * normalized Z deliberately disagrees with reciprocal-W. */
    make_packet(control, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR | GXMETAL_CLEAR_DEPTH);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_DEPTH_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_Z_FUNCTION,
                  GXMETAL_Z_LT);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_Z_BUFFER_MASK, 1);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_PERSPECTIVE_Z, 1);
    draw_textured_triangle_depth(renderer, packet, 3,
                                 0.10f, 2.0f, 0.0f, 0.0f);
    draw_textured_triangle_depth(renderer, packet, 3,
                                 0.90f, 4.0f, 0.0f, 1.0f);
    make_packet(control, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, control, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, control, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 32, 24) == 0x7c00);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_PERSPECTIVE_Z, 0);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_Z_FUNCTION,
                  GXMETAL_Z_NONE);

    /* OpenGLRendererATI exposes Quake III's lightmap as a second texture.
     * The guest carries that stage's homogeneous S/T/Q in the specular wire
     * slots.  A 50% gray lightmap must halve every base-texture channel. */
    memcpy(shared + GXMETAL_UPLOAD_OFFSET, "\xff\x80\x80\x80", 4);
    make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, 9);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_ARGB8888);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 9);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_SET_STATE, 32, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       GXMETAL_STATE_MULTI_TEXTURE);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, 9);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 1);
    for (uint32_t i = 0; i < 6; i++) {
        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_R_OFFSET, 0.0f);
        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_G_OFFSET, 0.0f);
        store_float(vertices + i * 64 + GXMETAL_VERTEX_KS_B_OFFSET, 1.0f);
    }
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x0010);
    CHECK(framebuffer_pixel(framebuffer, 48, 48) == 0x4010);

    /* The RAVE 1.6 wire form keeps secondary homogeneous coordinates out of
     * ks_r/g/b, so a game can combine public multitexture with highlights. */
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_OP,
                  GXMETAL_TEXTURE_HIGHLIGHT);
    draw_public_multitexture_test_quad(renderer, 3);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x1010);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_OP, 0);

    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_OP,
                  GXMETAL_MULTI_TEXTURE_ADD);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x421f);

    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_OP,
                  GXMETAL_MULTI_TEXTURE_BLEND_ALPHA);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x4210);

    /* ATI's private OpenGL bridge reports its opaque GL_ONE/GL_ONE dual
     * stage as RAVE BlendAlpha.  The host must recover additive composition
     * only for that private opaque-RGB case; ordinary RAVE and alpha-bearing
     * textures retain documented BlendAlpha behavior. */
    set_int_state(renderer, packet, 3, GXMETAL_STATE_ATI_PRIVATE, 1);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x4210);
    {
        const uint8_t blue[4] = {0, 0, 0, 255};
        const uint8_t gray[4] = {0, 128, 128, 128};

        upload_single_pixel_texture(renderer, packet, shared, 10,
                                    GXMETAL_PIXEL_RGB8888, blue);
        upload_single_pixel_texture(renderer, packet, shared, 11,
                                    GXMETAL_PIXEL_RGB8888, gray);
    }
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 10);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE, 11);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x421f);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_ATI_PRIVATE, 0);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x4210);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 7);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE, 9);

    /* RAVE chromakey compares the primary source texel before texture
     * operations. A keyed blue texel must preserve the black clear color;
     * changing only the key to red must make the same texture visible. */
    set_int_state(renderer, packet, 3,
                  GXMETAL_STATE_MULTI_TEXTURE_ENABLE, 0);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 10);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_R, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_G, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_B, 1.0f);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_ENABLE, 1);
    draw_textured_test_quad(renderer, 3, 0);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x0000);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_R, 1.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_B, 0.0f);
    draw_textured_test_quad(renderer, 3, 0);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_CHROMAKEY_ENABLE, 0);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 7);
    set_int_state(renderer, packet, 3,
                  GXMETAL_STATE_MULTI_TEXTURE_ENABLE, 1);

    set_float_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_FACTOR,
                    0.25f);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_OP,
                  GXMETAL_MULTI_TEXTURE_FIXED);
    draw_textured_test_quad(renderer, 3, 1);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x109b);

    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_ENABLE, 0);
    /* A bound-but-disabled secondary stage must not make ordinary public
     * TQAVTexture draws consume undefined specular fields as ATI UVs. */
    draw_textured_test_quad(renderer, 3, 0);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);

    /* Common sprite formats must preserve source row padding and PowerPC
     * byte order. I8 expands to opaque gray; AI16_88 stores alpha before
     * intensity, so this half-alpha white texel fails a 0.75 alpha test.
     * Alpha1 follows Apple Software RAVE's byte-per-texel contract: any
     * nonzero first byte is opaque white and padding is ignored. RGB8_332
     * expands the explicitly documented RRR GGG BB layout. */
    {
        const uint8_t intensity[4] = {0x80, 0x11, 0x22, 0x33};
        const uint8_t alpha_intensity[4] = {0x80, 0xff, 0x44, 0x55};
        const uint8_t alpha1_on[4] = {0x01, 0xa5, 0x5a, 0xc3};
        const uint8_t alpha1_off[4] = {0x00, 0xff, 0xff, 0xff};
        const uint8_t rgb332[4] = {0xab, 0x11, 0x22, 0x33};

        upload_single_pixel_texture(renderer, packet, shared, 12,
                                    GXMETAL_PIXEL_INTENSITY8, intensity);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 12);
        draw_textured_test_quad(renderer, 3, 0);
        CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x4210);

        upload_single_pixel_texture(
            renderer, packet, shared, 13,
            GXMETAL_PIXEL_ALPHA_INTENSITY88, alpha_intensity);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 13);
        set_float_state(renderer, packet, 3,
                        GXMETAL_STATE_ALPHA_TEST_REFERENCE, 0.75f);
        set_int_state(renderer, packet, 3,
                      GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                      GXMETAL_ALPHA_TEST_GT);
        draw_textured_test_quad(renderer, 3, 0);
        CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x0000);
        set_int_state(renderer, packet, 3,
                      GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                      GXMETAL_ALPHA_TEST_NONE);

        upload_single_pixel_texture(renderer, packet, shared, 16,
                                    GXMETAL_PIXEL_RGB332, rgb332);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 16);
        draw_textured_test_quad(renderer, 3, 0);
        CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x593f);

        upload_single_pixel_texture(renderer, packet, shared, 14,
                                    GXMETAL_PIXEL_ALPHA8, alpha1_on);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 14);
        set_float_state(renderer, packet, 3,
                        GXMETAL_STATE_ALPHA_TEST_REFERENCE, 0.5f);
        set_int_state(renderer, packet, 3,
                      GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                      GXMETAL_ALPHA_TEST_GT);
        draw_textured_test_quad(renderer, 3, 0);
        CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x7fff);

        upload_single_pixel_texture(renderer, packet, shared, 15,
                                    GXMETAL_PIXEL_ALPHA8, alpha1_off);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 15);
        draw_textured_test_quad(renderer, 3, 0);
        CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x0000);
        set_int_state(renderer, packet, 3,
                      GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                      GXMETAL_ALPHA_TEST_NONE);

        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 12);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 13);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 14);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 15);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 16);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 7);
    }
    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_ENABLE, 1);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_MULTI_TEXTURE_OP,
                  GXMETAL_MULTI_TEXTURE_MODULATE);

    /* Destroying a bound secondary resource must disable that stage rather
     * than poisoning subsequent single-texture draws. */
    make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 9);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 1);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 48, 48) == 0x7c1f);

    /* Carmageddon II's ATI RAVE path reports private pixel type 1001 for
     * big-endian ARGB4444 surfaces. Verify channel order and that a zero
     * alpha nibble preserves the blue clear color behind a white texel. */
    memcpy(shared + GXMETAL_UPLOAD_OFFSET,
           "\xff\0\xf0\xf0\x0f\xff", 6);
    make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 1);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_ATI_ARGB4444);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_SET_STATE, 32, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       GXMETAL_STATE_TEXTURE);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, 8);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_HOST_ATI_UV);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    /* Private ATI/OpenGL T coordinates use zero at the top and negative
     * values downward.  The advertised host transform converts them to the
     * common lower-origin RAVE convention. */
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, -1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, -1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, -1);
    /* ATI's opaque RGB16 path sometimes leaves alpha uninitialized. */
    store_float(vertices + GXMETAL_VERTEX_A_OFFSET, NAN);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 10, 32) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 32, 32) == 0x03e0);
    CHECK(framebuffer_pixel(framebuffer, 54, 32) == 0x001f);

    /* ATI private callers have been observed with both nonnegative RAVE V
     * coordinates and negative top-origin V coordinates.  Clamp sampling a
     * vertical gradient must preserve orientation for both conventions. */
    memcpy(shared + GXMETAL_UPLOAD_OFFSET,
           "\xff\0\xff\0\xf0\x0f\xf0\x0f", 8);
    make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, 18);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_ATI_ARGB4444);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_TEXTURE_UPLOAD, 48, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET, 18);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, 4);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, 2);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, 2);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    set_resource_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE, 18);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_WRAP_U,
                  GXMETAL_TEXTURE_WRAP_CLAMP);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_TEXTURE_WRAP_V,
                  GXMETAL_TEXTURE_WRAP_CLAMP);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_HOST_ATI_UV);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 1);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 1);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 0);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 1);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 0);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 0);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 32, 10) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 32, 54) == 0x001f);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_HOST_ATI_UV);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    /* The first negative V deliberately follows two zero-V vertices. */
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, -1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, -1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, -1);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 32, 10) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 32, 54) == 0x001f);

    make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 18);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 8);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_SET_STATE, 32, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       GXMETAL_STATE_TEXTURE);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, 7);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);

    /* Perspective-Z fog uses 1/invW rather than normalized Z. Nanosaur
     * relies on that distinction: a reciprocal W of 2.0 produces depth 0.5
     * even when the depth-buffer coordinate is near 1.0. */
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_A, 1.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_R, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_G, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_B, 1.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_START, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_END, 1.0f);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_PERSPECTIVE_Z, 1);
    set_int_state(renderer, packet, 3, GXMETAL_STATE_FOG_MODE,
                  GXMETAL_FOG_LINEAR);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex_depth(vertices + 0 * 64, 0, 0, 0.99f, 2.0f, 0, 0);
    set_texture_vertex_depth(vertices + 1 * 64, 64, 0, 0.99f, 2.0f, 1, 0);
    set_texture_vertex_depth(vertices + 2 * 64, 0, 64, 0.99f, 2.0f, 0, 1);
    set_texture_vertex_depth(vertices + 3 * 64, 64, 0, 0.99f, 2.0f, 1, 0);
    set_texture_vertex_depth(vertices + 4 * 64, 64, 64, 0.99f, 2.0f, 1, 1);
    set_texture_vertex_depth(vertices + 5 * 64, 0, 64, 0.99f, 2.0f, 0, 1);
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(((framebuffer_pixel(framebuffer, 16, 48) >> 10) & 31) >= 14);
    CHECK(((framebuffer_pixel(framebuffer, 16, 48) >> 10) & 31) <= 16);
    CHECK((framebuffer_pixel(framebuffer, 16, 48) & 31) >= 14);
    CHECK((framebuffer_pixel(framebuffer, 16, 48) & 31) <= 16);

    /* Without perspective-Z, Gouraud invW is undefined and must not affect
     * fog. A normalized Z of 0.25 keeps 75% red and mixes in 25% blue. */
    set_int_state(renderer, packet, 3, GXMETAL_STATE_PERSPECTIVE_Z, 0);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    draw_triangle(renderer, packet, 3, 0.25f, 1.0f, 0.0f, 0.0f, 1.0f);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(((framebuffer_pixel(framebuffer, 32, 32) >> 10) & 31) >= 22);
    CHECK(((framebuffer_pixel(framebuffer, 32, 32) >> 10) & 31) <= 24);
    CHECK((framebuffer_pixel(framebuffer, 32, 32) & 31) >= 7);
    CHECK((framebuffer_pixel(framebuffer, 32, 32) & 31) <= 9);

    /* Textured alpha testing uses the post-texture-operation alpha. Vertex
     * alpha lowers every opaque texel below the reference, so the blue clear
     * must remain visible across the quad. */
    set_int_state(renderer, packet, 3, GXMETAL_STATE_FOG_MODE,
                  GXMETAL_FOG_NONE);
    set_float_state(renderer, packet, 3,
                    GXMETAL_STATE_ALPHA_TEST_REFERENCE, 0.5f);
    set_int_state(renderer, packet, 3,
                  GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                  GXMETAL_ALPHA_TEST_GT);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 3);
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
    make_packet(packet, GXMETAL_OP_DRAW_TEXTURED, 416, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 6);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET, 64);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_texture_vertex(vertices + 0 * 64, 0, 0, 0, 0);
    set_texture_vertex(vertices + 1 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 2 * 64, 0, 64, 0, 1);
    set_texture_vertex(vertices + 3 * 64, 64, 0, 1, 0);
    set_texture_vertex(vertices + 4 * 64, 64, 64, 1, 1);
    set_texture_vertex(vertices + 5 * 64, 0, 64, 0, 1);
    for (uint32_t i = 0; i < 6; i++) {
        store_float(vertices + i * 64 + GXMETAL_VERTEX_A_OFFSET, 0.25f);
    }
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 16, 48) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 48, 16) == 0x001f);

    make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET, 7);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    gxmetal_metal_destroy(renderer);
    free(shared);
}

static void test_metal_depth_blend_and_double_buffer(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
    uint8_t packet[128];
    uint8_t *payload;
    uint16_t pixel;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer), NULL, 0);

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
    present_rect(renderer, packet, 2, 0, 0, 64, 64);
    pixel = (uint16_t)((uint16_t)framebuffer[(24 * 64 + 32) * 2] << 8 |
                       framebuffer[(24 * 64 + 32) * 2 + 1]);
    CHECK(((pixel >> 10) & 31) >= 13 && ((pixel >> 10) & 31) <= 17);
    CHECK(((pixel >> 5) & 31) >= 13 && ((pixel >> 5) & 31) <= 17);
    CHECK((pixel & 31) == 0);

    /* Apple requires Perspective-Z to use invW for hidden-surface removal
     * while preserving the ordinary Z-function result: LT must still select
     * the nearer surface. Deliberately make Z and invW disagree so this
     * cannot pass by continuing to use normalized Z. */
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
    set_int_state(renderer, packet, 2, GXMETAL_STATE_PERSPECTIVE_Z, 1);
    draw_triangle_depth(renderer, packet, 2, 0.10f, 0.25f,
                        0, 0, 1, 1);
    draw_triangle_depth(renderer, packet, 2, 0.90f, 0.75f,
                        1, 0, 0, 1);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 2, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 32, 24) == 0x7c00);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_PERSPECTIVE_Z, 0);

    /* Alpha testing happens before blending and depth writes. A rejected
     * fragment leaves both the blue color and cleared depth untouched. */
    set_float_state(renderer, packet, 2,
                    GXMETAL_STATE_ALPHA_TEST_REFERENCE, 0.5f);
    set_int_state(renderer, packet, 2,
                  GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                  GXMETAL_ALPHA_TEST_GT);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 2);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR | GXMETAL_CLEAR_DEPTH);
    store_float(payload + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_DEPTH_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    draw_triangle(renderer, packet, 2, 0.20f, 1, 0, 0, 0.25f);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 2, 0, 0, 64, 64);
    pixel = framebuffer_pixel(framebuffer, 32, 24);
    CHECK(pixel == 0x001f);

    /* The complementary comparison accepts the same fragment. Because the
     * rejected draw did not update depth, it now blends over blue. */
    set_int_state(renderer, packet, 2,
                  GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                  GXMETAL_ALPHA_TEST_LE);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    draw_triangle(renderer, packet, 2, 0.20f, 1, 0, 0, 0.25f);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 2, 0, 0, 64, 64);
    pixel = framebuffer_pixel(framebuffer, 32, 24);
    CHECK(((pixel >> 10) & 31) >= 6 && ((pixel >> 10) & 31) <= 9);
    CHECK((pixel & 31) >= 22 && (pixel & 31) <= 25);

    /* ATI's classic OpenGL/RAVE bridge uses GL_SRC_COLOR as a source blend
     * factor even though core OpenGL 1.x only lists it for the destination.
     * Future Cop depends on accepting this vendor behavior; rejecting it
     * faults the guest command stream and freezes at the 3D transition. */
    set_int_state(renderer, packet, 2,
                  GXMETAL_STATE_ALPHA_TEST_FUNCTION,
                  GXMETAL_ALPHA_TEST_TRUE);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_Z_FUNCTION,
                  GXMETAL_Z_NONE);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_BLEND,
                  GXMETAL_BLEND_OPENGL);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_GL_BLEND_SRC,
                  GXMETAL_GL_SRC_COLOR);
    set_int_state(renderer, packet, 2, GXMETAL_STATE_GL_BLEND_DST,
                  GXMETAL_GL_ONE);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 2);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    draw_triangle(renderer, packet, 2, 0.20f, 1, 0, 0, 1);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 2);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 2, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 32, 24) == 0x7c00);

    make_packet(packet, GXMETAL_OP_SET_STATE, 32, 2);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       GXMETAL_STATE_ALPHA_TEST_FUNCTION);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_UINT32);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET,
                       GXMETAL_ALPHA_TEST_TRUE + 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_BAD_PACKET);
    gxmetal_metal_destroy(renderer);
}

static void test_metal_rect_clip_scissor_and_dirty_present(void)
{
    uint8_t framebuffer[64 * 64 * 2];
    uint8_t packet[128];
    uint8_t *payload;
    GXMetalMetalRenderer *renderer;
    uint32_t i;

    for (i = 0; i < 64 * 64; i++) {
        framebuffer[i * 2] = 0x03;
        framebuffer[i * 2 + 1] = 0xe0;
    }
    renderer = gxmetal_metal_create(framebuffer, sizeof(framebuffer),
                                     NULL, 0);
    if (renderer == NULL) {
        return;
    }
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 4);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FLAGS_OFFSET,
                       GXMETAL_CONTEXT_RECT_CLIP);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_LEFT_TOP_OFFSET,
                       8 | (8u << 16));
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET,
                       56 | (56u << 16));
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 4);
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
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 4, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 4, 4) == 0x03e0);
    CHECK(framebuffer_pixel(framebuffer, 12, 12) == 0x001f);

    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_LEFT, 16);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_TOP, 16);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_RIGHT, 32);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_BOTTOM, 32);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 4);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 4, 0, 0, 64, 64);
    CHECK(framebuffer_pixel(framebuffer, 20, 20) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 40, 40) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 4, 4) == 0x03e0);

    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_LEFT, 0);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_TOP, 0);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_RIGHT, 64);
    set_int_state(renderer, packet, 4, GXMETAL_STATE_SCISSOR_BOTTOM, 64);
    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 4);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 64);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 4);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 4, 40, 40, 48, 48);
    CHECK(framebuffer_pixel(framebuffer, 44, 44) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 36, 36) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 4, 4) == 0x03e0);

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 5);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FLAGS_OFFSET,
                       GXMETAL_CONTEXT_RECT_CLIP);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET,
                       65 | (64u << 16));
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_BAD_CONTEXT);
    gxmetal_metal_destroy(renderer);
}

static void test_metal_direct_present_formats(void)
{
    uint8_t packet[128];
    uint8_t *payload;
    uint8_t *framebuffer;
    uint8_t *unaligned_allocation;
    uint8_t *unaligned_framebuffer;
    GXMetalMetalRenderer *renderer;
    long page_size = sysconf(_SC_PAGESIZE);
    size_t framebuffer_bytes;
    uint32_t format;

    CHECK(page_size > 0);
    if (page_size <= 0) {
        return;
    }
    unaligned_allocation = malloc((size_t)page_size * 2 + 1);
    CHECK(unaligned_allocation != NULL);
    if (unaligned_allocation == NULL) {
        return;
    }
    unaligned_framebuffer = unaligned_allocation;
    if (((uintptr_t)unaligned_framebuffer % (uintptr_t)page_size) == 0) {
        unaligned_framebuffer++;
    }
    renderer = gxmetal_metal_create(unaligned_framebuffer,
                                     (uint32_t)page_size, NULL, 0);
    if (renderer != NULL) {
        CHECK(!gxmetal_metal_direct_present_available(renderer));
        memset(unaligned_framebuffer, 0, (size_t)page_size);
        make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 19);
        payload = packet + 16;
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 8);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 8);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 16);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                           GXMETAL_PIXEL_RGB555);
        CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 19);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_CLEAR, 64, 19);
        payload = packet + 16;
        gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                           GXMETAL_CLEAR_COLOR);
        store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 1.0f);
        store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
        gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                           GXMETAL_RECT_RIGHT_OFFSET, 8);
        gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                           GXMETAL_RECT_BOTTOM_OFFSET, 8);
        CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_END_FRAME, 32, 19);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        present_rect(renderer, packet, 19, 0, 0, 8, 8);
        CHECK(unaligned_framebuffer[(4 * 8 + 4) * 2] == 0x7c);
        CHECK(unaligned_framebuffer[(4 * 8 + 4) * 2 + 1] == 0x00);
        CHECK(gxmetal_metal_direct_present_count(renderer) == 0);
        CHECK(gxmetal_metal_fallback_present_count(renderer) == 1);
        gxmetal_metal_destroy(renderer);
    }
    free(unaligned_allocation);

    framebuffer_bytes = (64u * 64u * 4u + (size_t)page_size - 1) /
                        (size_t)page_size * (size_t)page_size;
    framebuffer = NULL;
    CHECK(posix_memalign((void **)&framebuffer, (size_t)page_size,
                         framebuffer_bytes) == 0);
    if (framebuffer == NULL) {
        return;
    }
    renderer = gxmetal_metal_create(framebuffer,
                                     (uint32_t)framebuffer_bytes,
                                     NULL, 0);
    if (renderer == NULL) {
        free(framebuffer);
        return;
    }
    CHECK(gxmetal_metal_direct_present_available(renderer));
    if (!gxmetal_metal_direct_present_available(renderer)) {
        gxmetal_metal_destroy(renderer);
        free(framebuffer);
        return;
    }

    for (format = GXMETAL_PIXEL_RGB555;
         format <= GXMETAL_PIXEL_RGB8888; format++) {
        uint32_t context = 20 + format;
        uint32_t bytes_per_pixel = format == GXMETAL_PIXEL_RGB555 ? 2 : 4;
        uint32_t row_bytes = 64 * bytes_per_pixel;
        uint32_t offset = 12 * row_bytes + 12 * bytes_per_pixel;

        memset(framebuffer, 0x5a, framebuffer_bytes);
        make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, context);
        payload = packet + 16;
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET,
                           row_bytes);
        gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                           format);
        CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

        make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, context);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_CLEAR, 64, context);
        payload = packet + 16;
        gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                           GXMETAL_CLEAR_COLOR);
        store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 1.0f);
        store_float(payload + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
        store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 0.5f);
        gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                           GXMETAL_RECT_RIGHT_OFFSET, 64);
        gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                           GXMETAL_RECT_BOTTOM_OFFSET, 64);
        CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
        make_packet(packet, GXMETAL_OP_END_FRAME, 32, context);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
        present_rect(renderer, packet, context, 8, 10, 32, 40);

        CHECK(framebuffer[0] == 0x5a);
        if (format == GXMETAL_PIXEL_RGB555) {
            CHECK(framebuffer[offset] == 0x7c);
            CHECK(framebuffer[offset + 1] == 0x1f);
        } else if (format == GXMETAL_PIXEL_ARGB8888) {
            CHECK(framebuffer[offset] == 0x80);
            CHECK(framebuffer[offset + 1] == 0xff);
            CHECK(framebuffer[offset + 2] == 0x00);
            CHECK(framebuffer[offset + 3] == 0xff);
        } else {
            CHECK(framebuffer[offset] == 0x00);
            CHECK(framebuffer[offset + 1] == 0xff);
            CHECK(framebuffer[offset + 2] == 0x00);
            CHECK(framebuffer[offset + 3] == 0xff);
        }

        make_packet(packet, GXMETAL_OP_CONTEXT_DESTROY, 16, context);
        CHECK(dispatch(renderer, packet, 16) == GXMETAL_ERROR_NONE);
    }
    CHECK(gxmetal_metal_direct_present_count(renderer) == 3);
    CHECK(gxmetal_metal_fallback_present_count(renderer) == 0);
    gxmetal_metal_destroy(renderer);
    free(framebuffer);
}

static void test_metal_carmageddon_resource_working_set(void)
{
    enum { kCarmageddonResourceWorkingSet = 300 };
    uint8_t framebuffer[2] = {0};
    uint8_t shared = 0;
    uint8_t packet[48];
    uint8_t *payload;
    uint32_t resource;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer), &shared, sizeof(shared));

    if (renderer == NULL) {
        return;
    }
    for (resource = 1; resource <= kCarmageddonResourceWorkingSet;
         resource++) {
        uint32_t resource_id = UINT32_C(1) +
            (resource - 1) * UINT32_C(8192);

        make_packet(packet, GXMETAL_OP_TEXTURE_CREATE, 48, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        /* Deliberately collide every ID in the host lookup table. */
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET, resource_id);
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, 1);
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, 1);
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, 2);
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                           GXMETAL_PIXEL_RGB555);
        gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
        CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
    }
    for (resource = 1; resource <= kCarmageddonResourceWorkingSet;
         resource++) {
        uint32_t resource_id = UINT32_C(1) +
            (resource - 1) * UINT32_C(8192);

        make_packet(packet, GXMETAL_OP_TEXTURE_DESTROY, 32, 0);
        payload = packet + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET,
                           resource_id);
        CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    }
    gxmetal_metal_destroy(renderer);
}

static void test_metal_gamma_present(void)
{
    uint8_t framebuffer[8 * 8 * 2] = {0};
    uint8_t packet[64];
    uint8_t *payload;
    uint8_t red[256];
    uint8_t green[256];
    uint8_t blue[256];
    uint32_t i;
    GXMetalMetalRenderer *renderer = gxmetal_metal_create(
        framebuffer, sizeof(framebuffer), NULL, 0);

    if (renderer == NULL) {
        return;
    }
    for (i = 0; i < 256; i++) {
        red[i] = (uint8_t)(255 - i);
        green[i] = (uint8_t)(i / 2);
        blue[i] = 0;
    }
    gxmetal_metal_set_gamma(renderer, red, green, blue);

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 1);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 16);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_BEGIN_FRAME, 32, 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_CLEAR, 64, 1);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(payload + GXMETAL_CLEAR_COLOR_R_OFFSET, 0.25f);
    store_float(payload + GXMETAL_CLEAR_COLOR_G_OFFSET, 0.5f);
    store_float(payload + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
    store_float(payload + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 8);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 8);
    CHECK(dispatch(renderer, packet, 64) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 1);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 1, 0, 0, 8, 8);

    CHECK(framebuffer[0] == 0x5d);
    CHECK(framebuffer[1] == 0x00);
    CHECK(gxmetal_metal_fallback_present_count(renderer) == 1);
    gxmetal_metal_destroy(renderer);
}

int main(void)
{
    @autoreleasepool {
        test_metal_triangle();
        test_metal_large_vertex_batches();
        test_metal_depth_blend_and_double_buffer();
        test_metal_texture_upload_and_sampling();
        test_metal_rect_clip_scissor_and_dirty_present();
        test_metal_direct_present_formats();
        test_metal_carmageddon_resource_working_set();
        test_metal_gamma_present();
    }
    if (failures != 0) {
        fprintf(stderr, "GXMetal Metal: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal Metal: all tests passed");
    return EXIT_SUCCESS;
}
