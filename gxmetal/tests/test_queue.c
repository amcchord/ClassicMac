/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gxmetal_queue.h"

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
    memset(packet, 0, bytes);
    gxmetal_store_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET, opcode);
    gxmetal_store_le16(packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                       GXMETAL_PACKET_HEADER_BYTES);
    gxmetal_store_le32(packet + GXMETAL_PACKET_BYTES_OFFSET, bytes);
    gxmetal_store_le32(packet + GXMETAL_PACKET_CONTEXT_OFFSET, context);
    gxmetal_store_le32(packet + GXMETAL_PACKET_SEQUENCE_OFFSET, sequence);
}

static uint32_t accept_packet(void *opaque, const GXMetalPacketView *packet)
{
    unsigned *dispatches = (unsigned *)opaque;
    (*dispatches)++;
    return packet->opcode == GXMETAL_OP_CONTEXT_CREATE ?
        GXMETAL_ERROR_NONE : GXMETAL_ERROR_BAD_OPCODE;
}

static void test_fence_and_dispatch(void)
{
    uint8_t shared[256] = {0};
    GXMetalQueue queue;
    unsigned dispatches = 0;

    gxmetal_queue_init(&queue, shared, sizeof(shared), 64, 128,
                       accept_packet, &dispatches);
    make_packet(shared + 64, GXMETAL_OP_CONTEXT_CREATE, 48, 3, 10);
    gxmetal_store_le32(shared + 64 + 16 + GXMETAL_CONTEXT_WIDTH_OFFSET, 640);
    gxmetal_store_le32(shared + 64 + 16 + GXMETAL_CONTEXT_HEIGHT_OFFSET, 480);
    gxmetal_store_le32(shared + 64 + 16 + GXMETAL_CONTEXT_ROW_BYTES_OFFSET,
                       1280);
    gxmetal_store_le32(shared + 64 + 16 + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       GXMETAL_PIXEL_RGB555);
    make_packet(shared + 112, GXMETAL_OP_FENCE, 16, 0, 11);
    CHECK(gxmetal_queue_publish(&queue, 64));
    gxmetal_queue_process(&queue);
    CHECK(queue.status == GXMETAL_STATUS_READY);
    CHECK(queue.consumer == 64);
    CHECK(queue.completed_sequence == 11);
    CHECK(queue.diagnostic == 2);
    CHECK(dispatches == 1);
}

static void test_wrap_pad(void)
{
    uint8_t shared[256] = {0};
    GXMetalQueue queue;

    gxmetal_queue_init(&queue, shared, sizeof(shared), 64, 128, NULL, NULL);
    queue.consumer = 96;
    make_packet(shared + 64 + 96, GXMETAL_OP_PAD, 32, 0, 0);
    make_packet(shared + 64, GXMETAL_OP_FENCE, 16, 0, 77);
    CHECK(gxmetal_queue_publish(&queue, 16));
    gxmetal_queue_process(&queue);
    CHECK(queue.status == GXMETAL_STATUS_READY);
    CHECK(queue.consumer == 16);
    CHECK(queue.completed_sequence == 77);
}

static void test_fault_and_reset(void)
{
    uint8_t shared[256] = {0};
    GXMetalQueue queue;

    gxmetal_queue_init(&queue, shared, sizeof(shared), 64, 128, NULL, NULL);
    make_packet(shared + 64, UINT16_C(0x7fff), 16, 0, 0);
    CHECK(gxmetal_queue_publish(&queue, 16));
    gxmetal_queue_process(&queue);
    CHECK(queue.status & GXMETAL_STATUS_FAULTED);
    CHECK(queue.error == GXMETAL_ERROR_BAD_OPCODE);
    CHECK(!gxmetal_queue_publish(&queue, 32));

    gxmetal_queue_reset(&queue);
    CHECK(queue.status == GXMETAL_STATUS_READY);
    CHECK(queue.error == GXMETAL_ERROR_NONE);
    CHECK(queue.producer == 0 && queue.consumer == 0);
}

static void test_bad_producer_and_bad_pad(void)
{
    uint8_t shared[256] = {0};
    GXMetalQueue queue;

    gxmetal_queue_init(&queue, shared, sizeof(shared), 64, 128, NULL, NULL);
    CHECK(!gxmetal_queue_publish(&queue, 3));
    CHECK(queue.error == GXMETAL_ERROR_BAD_RING);

    gxmetal_queue_reset(&queue);
    make_packet(shared + 64, GXMETAL_OP_PAD, 16, 0, 0);
    CHECK(gxmetal_queue_publish(&queue, 16));
    gxmetal_queue_process(&queue);
    CHECK(queue.error == GXMETAL_ERROR_BAD_PACKET);
}

int main(void)
{
    test_fence_and_dispatch();
    test_wrap_pad();
    test_fault_and_reset();
    test_bad_producer_and_bad_pad();

    if (failures != 0) {
        fprintf(stderr, "GXMetal queue: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal queue: all tests passed");
    return EXIT_SUCCESS;
}
