/* SPDX-License-Identifier: MIT */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

static void test_metal_triangle(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
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

static void test_metal_texture_upload_and_sampling(void)
{
    uint8_t framebuffer[64 * 64 * 2] = {0};
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

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 3);
    payload = packet + 16;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch(renderer, packet, 48) == GXMETAL_ERROR_NONE);
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
    CHECK(dispatch(renderer, packet, 416) == GXMETAL_ERROR_NONE);
    make_packet(packet, GXMETAL_OP_END_FRAME, 32, 3);
    CHECK(dispatch(renderer, packet, 32) == GXMETAL_ERROR_NONE);
    present_rect(renderer, packet, 3, 0, 0, 64, 64);

    /* RAVE defines V=0 at the lower edge of an ordinary top-down image.
     * Metal defines V=0 at the upper edge, so the host must invert V. */
    CHECK(framebuffer_pixel(framebuffer, 16, 16) == 0x001f);
    CHECK(framebuffer_pixel(framebuffer, 48, 16) == 0x7fff);
    CHECK(framebuffer_pixel(framebuffer, 16, 48) == 0x7c00);
    CHECK(framebuffer_pixel(framebuffer, 48, 48) == 0x03e0);

    /* Nanosaur uses depth fog on textured terrain. Render the same quad into
     * blue linear fog: its lower-left red texel becomes half-red/half-blue. */
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_A, 1.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_R, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_G, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_COLOR_B, 1.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_START, 0.0f);
    set_float_state(renderer, packet, 3, GXMETAL_STATE_FOG_END, 1.0f);
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
    CHECK(((framebuffer_pixel(framebuffer, 16, 48) >> 10) & 31) >= 14);
    CHECK(((framebuffer_pixel(framebuffer, 16, 48) >> 10) & 31) <= 16);
    CHECK((framebuffer_pixel(framebuffer, 16, 48) & 31) >= 14);
    CHECK((framebuffer_pixel(framebuffer, 16, 48) & 31) <= 16);

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

int main(void)
{
    @autoreleasepool {
        test_metal_triangle();
        test_metal_depth_blend_and_double_buffer();
        test_metal_texture_upload_and_sampling();
        test_metal_rect_clip_scissor_and_dirty_present();
    }
    if (failures != 0) {
        fprintf(stderr, "GXMetal Metal: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal Metal: first triangle passed");
    return EXIT_SUCCESS;
}
