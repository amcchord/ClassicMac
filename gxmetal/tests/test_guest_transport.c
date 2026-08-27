/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GXMetalRegistry.h"
#include "GXMetalDrawPolicy.h"
#include "GXMetalTransport.h"

static unsigned failures;
static uint32_t registers[GXMETAL_REGISTER_BYTES / sizeof(uint32_t)];
static uint8_t shared[GXMETAL_SHARED_BYTES];

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

static void initialize_device(uint64_t features)
{
    memset(registers, 0, sizeof(registers));
    memset(shared, 0, sizeof(shared));
    set_register(GXMETAL_REG_MAGIC, GXMETAL_PROTOCOL_MAGIC);
    set_register(GXMETAL_REG_VERSION, GXMETAL_PROTOCOL_VERSION);
    set_register(GXMETAL_REG_REGISTER_BYTES, GXMETAL_REGISTER_BYTES);
    set_register(GXMETAL_REG_FEATURES_LO, (uint32_t)features);
    set_register(GXMETAL_REG_FEATURES_HI, (uint32_t)(features >> 32));
    set_register(GXMETAL_REG_SHARED_BYTES, GXMETAL_SHARED_BYTES);
    set_register(GXMETAL_REG_RING_OFFSET, GXMETAL_RING_OFFSET);
    set_register(GXMETAL_REG_RING_BYTES, GXMETAL_RING_BYTES);
    set_register(GXMETAL_REG_STATUS, GXMETAL_STATUS_READY);
}

static void test_probe(void)
{
    GXMetalGuestTransport transport;

    initialize_device(GXMETAL_FEATURE_FENCE | GXMETAL_FEATURE_GOURAUD |
                      GXMETAL_FEATURE_ALPHA_TEST |
                      GXMETAL_FEATURE_RECT_CLIP |
                      GXMETAL_FEATURE_RELATIVE_INPUT);
    CHECK(sizeof(GXMetalRegistryInfo) == 32);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE));
    CHECK(registers[GXMETAL_REG_RESET / 4] == 0);
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == 0);
    CHECK(transport.ring_offset == GXMETAL_RING_OFFSET);
    CHECK(transport.ring_bytes == GXMETAL_RING_BYTES);
    CHECK(transport.features & GXMETAL_FEATURE_GOURAUD);
    CHECK(transport.features & GXMETAL_FEATURE_ALPHA_TEST);
    CHECK(transport.features & GXMETAL_FEATURE_RECT_CLIP);
    CHECK(transport.features & GXMETAL_FEATURE_RELATIVE_INPUT);
    gxmetal_guest_register_write(&transport, GXMETAL_REG_RELATIVE_INPUT, 1);
    CHECK(registers[GXMETAL_REG_RELATIVE_INPUT / 4] == 1);

    set_register(GXMETAL_REG_VERSION, UINT32_C(0x00020000));
    CHECK(!gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared), 0));

    initialize_device(GXMETAL_FEATURE_FENCE);
    CHECK(!gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD));
}

static void test_packet_and_wrap(void)
{
    GXMetalGuestTransport transport;
    GXMetalGuestPacket packet;
    uint32_t published_producer;
    uint32_t published_sequence;
    uint8_t *pad;

    initialize_device(GXMETAL_FEATURE_FENCE | GXMETAL_FEATURE_GOURAUD);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD));
    CHECK(gxmetal_guest_packet_begin(&transport, GXMETAL_OP_CONTEXT_CREATE,
                                     GXMETAL_CONTEXT_CREATE_PACKET_BYTES,
                                     7, &packet));
    CHECK(gxmetal_load_le16(packet.bytes + GXMETAL_PACKET_OPCODE_OFFSET) ==
          GXMETAL_OP_CONTEXT_CREATE);
    CHECK(gxmetal_load_le32(packet.bytes + GXMETAL_PACKET_CONTEXT_OFFSET) ==
          7);
    gxmetal_guest_packet_commit(&transport, &packet);
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == packet.next_producer);
    CHECK(registers[GXMETAL_REG_DOORBELL / 4] == packet.sequence);

    published_producer = registers[GXMETAL_REG_PRODUCER / 4];
    published_sequence = registers[GXMETAL_REG_DOORBELL / 4];
    CHECK(gxmetal_guest_packet_begin(&transport, GXMETAL_OP_CLEAR,
                                     GXMETAL_CLEAR_PACKET_BYTES,
                                     7, &packet));
    gxmetal_guest_packet_commit(&transport, &packet);
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == published_producer);
    CHECK(registers[GXMETAL_REG_DOORBELL / 4] == published_sequence);
    CHECK(gxmetal_guest_flush(&transport));
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == packet.next_producer);
    CHECK(registers[GXMETAL_REG_DOORBELL / 4] == packet.sequence);

    CHECK(gxmetal_guest_packet_begin(
        &transport, GXMETAL_OP_DRAW_BUFFER_WRITEBACK,
        GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES, 7, &packet));
    gxmetal_guest_packet_commit(&transport, &packet);
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == packet.next_producer);
    CHECK(registers[GXMETAL_REG_DOORBELL / 4] == packet.sequence);

    transport.producer = GXMETAL_RING_BYTES - 32;
    transport.published_producer = transport.producer;
    transport.consumer = 192;
    set_register(GXMETAL_REG_PRODUCER, transport.producer);
    set_register(GXMETAL_REG_CONSUMER, 192);
    CHECK(gxmetal_guest_packet_begin(&transport, GXMETAL_OP_DRAW_GOURAUD,
                                     128, 7, &packet));
    pad = shared + GXMETAL_RING_OFFSET + GXMETAL_RING_BYTES - 32;
    CHECK(gxmetal_load_le16(pad + GXMETAL_PACKET_OPCODE_OFFSET) ==
          GXMETAL_OP_PAD);
    CHECK(gxmetal_load_le32(pad + GXMETAL_PACKET_BYTES_OFFSET) == 32);
    CHECK(packet.bytes == shared + GXMETAL_RING_OFFSET);
    CHECK(packet.next_producer == 128);
}

static void test_full_ring_and_fence(void)
{
    GXMetalGuestTransport transport;
    GXMetalGuestPacket packet;
    uint32_t sequence = 0;

    initialize_device(GXMETAL_FEATURE_FENCE);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_FENCE));
    transport.producer = 0;
    set_register(GXMETAL_REG_CONSUMER, GXMETAL_PACKET_ALIGNMENT);
    transport.consumer = GXMETAL_PACKET_ALIGNMENT;
    CHECK(!gxmetal_guest_packet_begin(&transport, GXMETAL_OP_CLEAR,
                                      GXMETAL_CLEAR_PACKET_BYTES, 1,
                                      &packet));

    set_register(GXMETAL_REG_CONSUMER, 0);
    CHECK(gxmetal_guest_emit_fence(&transport, &sequence));
    CHECK(sequence != 0);
    set_register(GXMETAL_REG_COMPLETED_SEQUENCE, sequence);
    CHECK(gxmetal_guest_wait(&transport, sequence, 1));
    set_register(GXMETAL_REG_STATUS, GXMETAL_STATUS_FAULTED);
    CHECK(!gxmetal_guest_wait(&transport, sequence, 1));
}

static void test_draw_packet_skips_redundant_clear(void)
{
    GXMetalGuestTransport transport;
    GXMetalGuestPacket packet;
    uint8_t *destination;

    initialize_device(GXMETAL_FEATURE_GOURAUD);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD));
    destination = shared + GXMETAL_RING_OFFSET;
    memset(destination, 0xa5, 128);
    CHECK(gxmetal_guest_draw_packet_begin(
        &transport, GXMETAL_OP_DRAW_GOURAUD, 128, 7, &packet));
    CHECK(packet.bytes == destination);
    CHECK(gxmetal_load_le16(packet.bytes + GXMETAL_PACKET_OPCODE_OFFSET) ==
          GXMETAL_OP_DRAW_GOURAUD);
    CHECK(packet.bytes[GXMETAL_PACKET_HEADER_BYTES] == 0xa5);
    CHECK(!gxmetal_guest_draw_packet_begin(
        &transport, GXMETAL_OP_CLEAR, GXMETAL_CLEAR_PACKET_BYTES,
        7, &packet));
}

static void test_auxiliary_connect_preserves_in_flight_packet(void)
{
    GXMetalGuestTransport engine;
    GXMetalGuestTransport auxiliary;
    GXMetalGuestPacket packet;
    uint8_t header[GXMETAL_PACKET_HEADER_BYTES];

    initialize_device(GXMETAL_FEATURE_FENCE | GXMETAL_FEATURE_GOURAUD |
                      GXMETAL_FEATURE_RELATIVE_INPUT);
    CHECK(gxmetal_guest_transport_connect(
        &engine, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE));
    CHECK(gxmetal_guest_packet_begin(&engine, GXMETAL_OP_CONTEXT_CREATE,
                                     GXMETAL_CONTEXT_CREATE_PACKET_BYTES,
                                     1, &packet));
    memcpy(header, packet.bytes, sizeof(header));

    /* GXMetalInput connects independently while the RAVE packet is not yet
     * published.  A generic connect must neither reset the device nor reuse
     * and overwrite the engine's unpublished ring slot. */
    set_register(GXMETAL_REG_RESET, 0);
    CHECK(gxmetal_guest_transport_connect(
        &auxiliary, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_RELATIVE_INPUT));
    CHECK(registers[GXMETAL_REG_RESET / 4] == 0);
    CHECK(memcmp(header, packet.bytes, sizeof(header)) == 0);
    CHECK(gxmetal_load_le16(packet.bytes + GXMETAL_PACKET_OPCODE_OFFSET) ==
          GXMETAL_OP_CONTEXT_CREATE);
}

typedef struct TestGuestDrawBatch {
    GXMetalGuestTransport *transport;
    uint32_t pending_kind;
    uint32_t pending_count;
    uint32_t pending_flags;
} TestGuestDrawBatch;

static int flush_test_gouraud_batch(TestGuestDrawBatch *batch)
{
    GXMetalGuestPacket packet;
    uint32_t packet_bytes;
    uint8_t *payload;
    uint32_t i;

    if (batch->pending_count == 0) {
        return 1;
    }
    packet_bytes = GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
        batch->pending_count * GXMETAL_GOURAUD_VERTEX_BYTES;
    if (!gxmetal_guest_draw_packet_begin(
            batch->transport, GXMETAL_OP_DRAW_GOURAUD, packet_bytes, 9,
            &packet)) {
        return 0;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET,
                       GXMETAL_PRIMITIVE_TRIANGLE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET,
                       batch->pending_count);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET,
                       batch->pending_flags);
    memset(payload + GXMETAL_DRAW_VERTICES_OFFSET, 0,
           batch->pending_count * GXMETAL_GOURAUD_VERTEX_BYTES);
    for (i = 0; i < batch->pending_count; i++) {
        gxmetal_store_le32(payload + GXMETAL_DRAW_VERTICES_OFFSET +
                           i * GXMETAL_GOURAUD_VERTEX_BYTES +
                           GXMETAL_VERTEX_INV_W_OFFSET,
                           UINT32_C(0x3f800000));
    }
    gxmetal_guest_packet_commit(batch->transport, &packet);
    batch->pending_kind = GXMETAL_DRAW_BATCH_NONE;
    batch->pending_count = 0;
    return 1;
}

static int queue_test_gouraud_triangle(TestGuestDrawBatch *batch,
                                       uint32_t flags)
{
    if (!gxmetal_guest_draw_batch_can_append(
            batch->pending_count, batch->pending_kind,
            batch->pending_flags, 0,
            GXMETAL_DRAW_BATCH_GOURAUD, flags, 0) &&
        !flush_test_gouraud_batch(batch)) {
        return 0;
    }
    if (batch->pending_count == 0) {
        batch->pending_kind = GXMETAL_DRAW_BATCH_GOURAUD;
        batch->pending_flags = flags;
    }
    batch->pending_count += 3;
    return 1;
}

static void run_homogeneous_emission_case(uint64_t features,
                                          uint32_t expected_packets)
{
    GXMetalGuestTransport transport;
    TestGuestDrawBatch batch;
    uint32_t private_flags;
    uint32_t offset = 0;
    uint32_t packet_index = 0;

    initialize_device(GXMETAL_FEATURE_GOURAUD | features);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD));
    memset(&batch, 0, sizeof(batch));
    batch.transport = &transport;
    private_flags = gxmetal_guest_ati_private_draw_flags(
        transport.features);
    CHECK(private_flags ==
          ((features & GXMETAL_FEATURE_HOMOGENEOUS_DRAW) != 0 ?
               GXMETAL_DRAW_HOMOGENEOUS : GXMETAL_DRAW_NONE));

    /* A public RAVE triangle followed by private ATI/OpenGL geometry may
     * coalesce only for an old host that did not negotiate provenance. */
    CHECK(queue_test_gouraud_triangle(&batch, GXMETAL_DRAW_NONE));
    CHECK(queue_test_gouraud_triangle(&batch, private_flags));
    CHECK(flush_test_gouraud_batch(&batch));
    CHECK(gxmetal_guest_flush(&transport));

    while (offset < transport.producer) {
        const uint8_t *bytes = shared + GXMETAL_RING_OFFSET + offset;
        const uint8_t *payload = bytes + GXMETAL_PACKET_HEADER_BYTES;
        uint32_t packet_bytes = gxmetal_load_le32(
            bytes + GXMETAL_PACKET_BYTES_OFFSET);
        uint32_t expected_flags = packet_index == 0 ?
            GXMETAL_DRAW_NONE : GXMETAL_DRAW_HOMOGENEOUS;
        uint32_t expected_vertices = expected_packets == 1 ? 6 : 3;

        CHECK(packet_bytes >= GXMETAL_PACKET_HEADER_BYTES +
                              GXMETAL_DRAW_HEADER_BYTES);
        CHECK(gxmetal_load_le16(bytes + GXMETAL_PACKET_OPCODE_OFFSET) ==
              GXMETAL_OP_DRAW_GOURAUD);
        CHECK(gxmetal_load_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET) ==
              expected_flags);
        CHECK(gxmetal_load_le32(payload +
                               GXMETAL_DRAW_VERTEX_COUNT_OFFSET) ==
              expected_vertices);
        packet_index++;
        if (packet_bytes == 0) {
            break;
        }
        offset += packet_bytes;
    }
    CHECK(packet_index == expected_packets);
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] == transport.producer);
}

static void test_homogeneous_feature_gated_batch_emission(void)
{
    run_homogeneous_emission_case(0, 1);
    run_homogeneous_emission_case(GXMETAL_FEATURE_HOMOGENEOUS_DRAW, 2);
}

int main(void)
{
    test_probe();
    test_packet_and_wrap();
    test_full_ring_and_fence();
    test_draw_packet_skips_redundant_clear();
    test_auxiliary_connect_preserves_in_flight_packet();
    test_homogeneous_feature_gated_batch_emission();

    if (failures != 0) {
        fprintf(stderr, "GXMetal guest transport: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal guest transport: all tests passed");
    return EXIT_SUCCESS;
}
