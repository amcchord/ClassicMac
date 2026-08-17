/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GXMetalRegistry.h"
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
                      GXMETAL_FEATURE_RECT_CLIP);
    CHECK(sizeof(GXMetalRegistryInfo) == 32);
    CHECK(gxmetal_guest_transport_connect(
        &transport, registers, sizeof(registers), shared, sizeof(shared),
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE));
    CHECK(transport.ring_offset == GXMETAL_RING_OFFSET);
    CHECK(transport.ring_bytes == GXMETAL_RING_BYTES);
    CHECK(transport.features & GXMETAL_FEATURE_GOURAUD);
    CHECK(transport.features & GXMETAL_FEATURE_ALPHA_TEST);
    CHECK(transport.features & GXMETAL_FEATURE_RECT_CLIP);

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
    CHECK(registers[GXMETAL_REG_PRODUCER / 4] ==
          GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    CHECK(registers[GXMETAL_REG_DOORBELL / 4] == packet.sequence);

    transport.producer = GXMETAL_RING_BYTES - 32;
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

int main(void)
{
    test_probe();
    test_packet_and_wrap();
    test_full_ring_and_fence();

    if (failures != 0) {
        fprintf(stderr, "GXMetal guest transport: %u failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    puts("GXMetal guest transport: all tests passed");
    return EXIT_SUCCESS;
}
