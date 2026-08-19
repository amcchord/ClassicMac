/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GXMetalTransport.h"
#include "gxmetal_queue.h"
#include "gxmetal_renderer.h"

static unsigned failures;
static uint32_t registers[GXMETAL_REGISTER_BYTES / sizeof(uint32_t)];
static uint8_t shared[GXMETAL_SHARED_BYTES];
static uint8_t framebuffer[64 * 64 * 2];

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #expression); \
        failures++; \
    } \
} while (0)

static void set_register(uint32_t offset, uint32_t value)
{
    registers[offset / sizeof(uint32_t)] = value;
}

static void initialize_registers(void)
{
    memset(registers, 0, sizeof(registers));
    set_register(GXMETAL_REG_MAGIC, GXMETAL_PROTOCOL_MAGIC);
    set_register(GXMETAL_REG_VERSION, GXMETAL_PROTOCOL_VERSION);
    set_register(GXMETAL_REG_REGISTER_BYTES, GXMETAL_REGISTER_BYTES);
    set_register(GXMETAL_REG_FEATURES_LO,
                 GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE);
    set_register(GXMETAL_REG_SHARED_BYTES, GXMETAL_SHARED_BYTES);
    set_register(GXMETAL_REG_RING_OFFSET, GXMETAL_RING_OFFSET);
    set_register(GXMETAL_REG_RING_BYTES, GXMETAL_RING_BYTES);
    set_register(GXMETAL_REG_STATUS, GXMETAL_STATUS_READY);
}

static void pump_host(GXMetalQueue *queue)
{
    CHECK(gxmetal_queue_publish(queue,
        registers[GXMETAL_REG_PRODUCER / sizeof(uint32_t)]));
    gxmetal_queue_process(queue);
    set_register(GXMETAL_REG_CONSUMER, queue->consumer);
    set_register(GXMETAL_REG_STATUS, queue->status);
    set_register(GXMETAL_REG_ERROR, queue->error);
    set_register(GXMETAL_REG_COMPLETED_SEQUENCE,
                 queue->completed_sequence);
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

static void test_first_triangle_pipeline(void)
{
    GXMetalGuestTransport guest;
    GXMetalGuestPacket packet;
    GXMetalRenderer renderer;
    GXMetalQueue queue;
    uint8_t *payload;
    uint8_t *vertices;
    uint32_t fence;

    memset(shared, 0, sizeof(shared));
    memset(framebuffer, 0, sizeof(framebuffer));
    initialize_registers();
    gxmetal_renderer_init(&renderer, framebuffer, sizeof(framebuffer));
    gxmetal_queue_init(&queue, shared, sizeof(shared), GXMETAL_RING_OFFSET,
                       GXMETAL_RING_BYTES, gxmetal_renderer_dispatch,
                       &renderer);
    CHECK(gxmetal_guest_transport_connect(
        &guest, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE));

    CHECK(gxmetal_guest_packet_begin(&guest, GXMETAL_OP_CONTEXT_CREATE,
                                     GXMETAL_CONTEXT_CREATE_PACKET_BYTES,
                                     1, &packet));
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, 64);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, 128);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    gxmetal_guest_packet_commit(&guest, &packet);
    pump_host(&queue);
    CHECK(queue.error == GXMETAL_ERROR_NONE);

    CHECK(gxmetal_guest_packet_begin(&guest, GXMETAL_OP_DRAW_GOURAUD,
                                     128, 1, &packet));
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, 3);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    vertices = payload + GXMETAL_DRAW_VERTICES_OFFSET;
    set_vertex(vertices, 8.0f, 8.0f, 1.0f, 0.0f, 0.0f);
    set_vertex(vertices + 32, 56.0f, 8.0f, 0.0f, 1.0f, 0.0f);
    set_vertex(vertices + 64, 32.0f, 56.0f, 0.0f, 0.0f, 1.0f);
    gxmetal_guest_packet_commit(&guest, &packet);
    CHECK(gxmetal_guest_flush(&guest));
    pump_host(&queue);
    CHECK(queue.error == GXMETAL_ERROR_NONE);
    CHECK(framebuffer[(16 * 64 + 16) * 2] != 0 ||
          framebuffer[(16 * 64 + 16) * 2 + 1] != 0);
    CHECK(framebuffer[(63 * 64 + 63) * 2] == 0 &&
          framebuffer[(63 * 64 + 63) * 2 + 1] == 0);

    CHECK(gxmetal_guest_emit_fence(&guest, &fence));
    pump_host(&queue);
    CHECK(gxmetal_guest_wait(&guest, fence, 1));
    CHECK(queue.diagnostic == 3);
}

int main(void)
{
    test_first_triangle_pipeline();
    if (failures != 0) {
        fprintf(stderr, "GXMetal pipeline: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal pipeline: first triangle passed");
    return EXIT_SUCCESS;
}
