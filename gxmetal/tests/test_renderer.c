/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "gxmetal_renderer.h"

static unsigned failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

static void store_float(uint8_t *destination, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    gxmetal_store_le32(destination, bits);
}

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

static uint32_t dispatch_packet(GXMetalRenderer *renderer, uint8_t *packet,
                                uint32_t bytes)
{
    GXMetalPacketView view;
    CHECK(gxmetal_decode_packet(packet, bytes, &view) == GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    return gxmetal_renderer_dispatch(renderer, &view);
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

static void test_context_clear_and_triangle(void)
{
    uint8_t framebuffer[32 * 32 * 2] = {0};
    uint8_t packet[128];
    uint8_t *shared;
    GXMetalRenderer renderer;
    uint8_t *vertices;

    shared = calloc(1, GXMETAL_UPLOAD_OFFSET + sizeof(framebuffer));
    CHECK(shared != NULL);
    if (shared == NULL) {
        return;
    }
    gxmetal_renderer_init(&renderer, framebuffer, sizeof(framebuffer));
    gxmetal_renderer_set_shared(&renderer, shared,
                                GXMETAL_UPLOAD_OFFSET +
                                    sizeof(framebuffer));
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE,
                GXMETAL_CONTEXT_CREATE_PACKET_BYTES, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_WIDTH_OFFSET, 32);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_HEIGHT_OFFSET, 32);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 64);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch_packet(&renderer, packet, 48) == GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_CLEAR, GXMETAL_CLEAR_PACKET_BYTES, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_CLEAR_FLAGS_OFFSET,
                       GXMETAL_CLEAR_COLOR);
    store_float(packet + 16 + GXMETAL_CLEAR_COLOR_B_OFFSET, 1.0f);
    store_float(packet + 16 + GXMETAL_CLEAR_COLOR_A_OFFSET, 1.0f);
    gxmetal_store_le32(packet + 16 + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_RIGHT_OFFSET, 32);
    gxmetal_store_le32(packet + 16 + GXMETAL_CLEAR_RECT_OFFSET +
                       GXMETAL_RECT_BOTTOM_OFFSET, 32);
    CHECK(dispatch_packet(&renderer, packet, 64) == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[0] == 0x00 && framebuffer[1] == 0x1f);

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD, 128, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    vertices = packet + 16 + GXMETAL_DRAW_VERTICES_OFFSET;
    set_vertex(vertices, -4.0f, -4.0f, 1.0f, 0.0f, 0.0f);
    store_float(vertices + GXMETAL_VERTEX_INV_W_OFFSET, NAN);
    set_vertex(vertices + 32, 28.0f, 4.0f, 0.0f, 1.0f, 0.0f);
    set_vertex(vertices + 64, 16.0f, 28.0f, 0.0f, 0.0f, 1.0f);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_BACKFACING);
    CHECK(dispatch_packet(&renderer, packet, 128) == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[(8 * 32 + 8) * 2] != 0x00 ||
          framebuffer[(8 * 32 + 8) * 2 + 1] != 0x1f);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_FLAGS_OFFSET,
                       GXMETAL_DRAW_NONE);
    CHECK(dispatch_packet(&renderer, packet, 128) == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[(8 * 32 + 8) * 2] != 0x00 ||
          framebuffer[(8 * 32 + 8) * 2 + 1] != 0x1f);
    CHECK(framebuffer[(31 * 32 + 31) * 2] == 0x00 &&
          framebuffer[(31 * 32 + 31) * 2 + 1] == 0x1f);

    make_packet(packet, GXMETAL_OP_DRAW_GOURAUD, 96, 1);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_LINE);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 2);
    gxmetal_store_le32(packet + 16 + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    vertices = packet + 16 + GXMETAL_DRAW_VERTICES_OFFSET;
    set_vertex(vertices, -1000000.0f, 30.0f, 1.0f, 0.0f, 0.0f);
    set_vertex(vertices + 32, 1000000.0f, 30.0f, 1.0f, 0.0f, 0.0f);
    CHECK(dispatch_packet(&renderer, packet, 96) == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[(30 * 32 + 0) * 2] == 0x7c);
    CHECK(framebuffer[(30 * 32 + 31) * 2] == 0x7c);

    make_packet(packet, GXMETAL_OP_READBACK,
                GXMETAL_READBACK_PACKET_BYTES, 1);
    gxmetal_store_le32(packet + 16 +
                       GXMETAL_READBACK_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(packet + 16 + GXMETAL_READBACK_LENGTH_OFFSET,
                       sizeof(framebuffer));
    gxmetal_store_le32(packet + 16 + GXMETAL_READBACK_ROW_BYTES_OFFSET,
                       64);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_READBACK_PACKET_BYTES) ==
          GXMETAL_ERROR_NONE);
    CHECK(memcmp(shared + GXMETAL_UPLOAD_OFFSET, framebuffer,
                 sizeof(framebuffer)) == 0);
    gxmetal_store_le32(packet + 16 + GXMETAL_READBACK_ROW_BYTES_OFFSET,
                       32);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_READBACK_PACKET_BYTES) ==
          GXMETAL_ERROR_BAD_PACKET);
    free(shared);
}

static void test_rejects_framebuffer_overflow(void)
{
    uint8_t framebuffer[64] = {0};
    uint8_t packet[48];
    GXMetalRenderer renderer;

    gxmetal_renderer_init(&renderer, framebuffer, sizeof(framebuffer));
    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE, 48, 2);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_WIDTH_OFFSET, 32);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_HEIGHT_OFFSET, 32);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 64);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch_packet(&renderer, packet, 48) ==
          GXMETAL_ERROR_BAD_CONTEXT);
}

static void test_draw_buffer_dirty_writeback(void)
{
    uint8_t framebuffer[4 * 12];
    uint8_t packet[48];
    uint8_t *shared = calloc(1, GXMETAL_UPLOAD_OFFSET + sizeof(framebuffer));
    uint8_t *staging;
    GXMetalRenderer renderer;
    uint32_t x;
    uint32_t y;

    CHECK(shared != NULL);
    if (shared == NULL) {
        return;
    }
    memset(framebuffer, 0x5a, sizeof(framebuffer));
    gxmetal_renderer_init(&renderer, framebuffer, sizeof(framebuffer));
    gxmetal_renderer_set_shared(&renderer, shared,
                                GXMETAL_UPLOAD_OFFSET + sizeof(framebuffer));

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE,
                GXMETAL_CONTEXT_CREATE_PACKET_BYTES, 3);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_WIDTH_OFFSET, 4);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_HEIGHT_OFFSET, 4);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 12);
    gxmetal_store_le32(packet + 16 + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_CONTEXT_CREATE_PACKET_BYTES) ==
          GXMETAL_ERROR_NONE);

    make_packet(packet, GXMETAL_OP_READBACK,
                GXMETAL_READBACK_PACKET_BYTES, 3);
    gxmetal_store_le32(packet + 16 +
                       GXMETAL_READBACK_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(packet + 16 + GXMETAL_READBACK_LENGTH_OFFSET,
                       sizeof(framebuffer));
    gxmetal_store_le32(packet + 16 + GXMETAL_READBACK_ROW_BYTES_OFFSET, 12);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_READBACK_PACKET_BYTES) ==
          GXMETAL_ERROR_NONE);

    staging = shared + GXMETAL_UPLOAD_OFFSET;
    memset(staging, 0x33, sizeof(framebuffer));
    for (y = 1; y < 3; y++) {
        for (x = 1; x < 3; x++) {
            staging[y * 12 + x * 2] = 0x7c;
            staging[y * 12 + x * 2 + 1] = 0x00;
        }
    }
    make_packet(packet, GXMETAL_OP_DRAW_BUFFER_WRITEBACK,
                GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES, 3);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_SHARED_OFFSET_OFFSET,
        GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_LENGTH_OFFSET, sizeof(framebuffer));
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_ROW_BYTES_OFFSET, 12);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET +
        GXMETAL_RECT_LEFT_OFFSET, 1);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET +
        GXMETAL_RECT_TOP_OFFSET, 1);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET +
        GXMETAL_RECT_RIGHT_OFFSET, 3);
    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET +
        GXMETAL_RECT_BOTTOM_OFFSET, 3);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES) ==
          GXMETAL_ERROR_NONE);
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 4; x++) {
            uint32_t offset = y * 12 + x * 2;

            if (x >= 1 && x < 3 && y >= 1 && y < 3) {
                CHECK(framebuffer[offset] == 0x7c &&
                      framebuffer[offset + 1] == 0x00);
            } else {
                CHECK(framebuffer[offset] == 0x5a &&
                      framebuffer[offset + 1] == 0x5a);
            }
        }
        CHECK(framebuffer[y * 12 + 8] == 0x5a &&
              framebuffer[y * 12 + 11] == 0x5a);
    }

    gxmetal_store_le32(packet + 16 +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET +
        GXMETAL_RECT_RIGHT_OFFSET, 5);
    CHECK(dispatch_packet(&renderer, packet,
                          GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES) ==
          GXMETAL_ERROR_BAD_PACKET);
    free(shared);
}

int main(void)
{
    test_context_clear_and_triangle();
    test_rejects_framebuffer_overflow();
    test_draw_buffer_dirty_writeback();
    if (failures != 0) {
        fprintf(stderr, "GXMetal renderer: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal renderer: all tests passed");
    return EXIT_SUCCESS;
}
