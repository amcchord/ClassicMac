/* SPDX-License-Identifier: MIT */

#include "GXMetalTransport.h"

#include <stddef.h>
#include <string.h>

static uint32_t gxmetal_native_to_le32(uint32_t value)
{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ((value & UINT32_C(0x000000ff)) << 24) |
           ((value & UINT32_C(0x0000ff00)) << 8) |
           ((value & UINT32_C(0x00ff0000)) >> 8) |
           ((value & UINT32_C(0xff000000)) >> 24);
#else
    return value;
#endif
}

static uint32_t gxmetal_le32_to_native(uint32_t value)
{
    return gxmetal_native_to_le32(value);
}

static void gxmetal_guest_barrier(void)
{
#if defined(__POWERPC__)
    __asm__ volatile("sync" ::: "memory");
#elif defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

uint32_t gxmetal_guest_register_read(const GXMetalGuestTransport *transport,
                                     uint32_t offset)
{
    uint32_t value;

    value = transport->registers[offset / sizeof(uint32_t)];
    gxmetal_guest_barrier();
    return gxmetal_le32_to_native(value);
}

void gxmetal_guest_register_write(const GXMetalGuestTransport *transport,
                                  uint32_t offset, uint32_t value)
{
    transport->registers[offset / sizeof(uint32_t)] =
        gxmetal_native_to_le32(value);
    gxmetal_guest_barrier();
}

int gxmetal_guest_transport_connect(GXMetalGuestTransport *transport,
                                    volatile void *registers,
                                    uint32_t register_bytes,
                                    void *shared,
                                    uint32_t shared_bytes,
                                    uint64_t required_features)
{
    uint32_t version;
    uint32_t status;
    uint64_t features;

    if (transport == NULL || registers == NULL || shared == NULL ||
        register_bytes < GXMETAL_REGISTER_BYTES) {
        return 0;
    }
    memset(transport, 0, sizeof(*transport));
    transport->registers = (volatile uint32_t *)registers;
    transport->shared = (uint8_t *)shared;

    if (gxmetal_guest_register_read(transport, GXMETAL_REG_MAGIC) !=
        GXMETAL_PROTOCOL_MAGIC) {
        return 0;
    }
    version = gxmetal_guest_register_read(transport, GXMETAL_REG_VERSION);
    if ((version >> 16) != GXMETAL_PROTOCOL_VERSION_MAJOR ||
        gxmetal_guest_register_read(transport, GXMETAL_REG_REGISTER_BYTES) >
            register_bytes ||
        gxmetal_guest_register_read(transport, GXMETAL_REG_SHARED_BYTES) !=
            shared_bytes) {
        return 0;
    }

    transport->shared_bytes = shared_bytes;
    transport->ring_offset = gxmetal_guest_register_read(
        transport, GXMETAL_REG_RING_OFFSET);
    transport->ring_bytes = gxmetal_guest_register_read(
        transport, GXMETAL_REG_RING_BYTES);
    if (transport->ring_bytes < GXMETAL_PACKET_ALIGNMENT ||
        (transport->ring_offset & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        (transport->ring_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        (uint64_t)transport->ring_offset + transport->ring_bytes >
            shared_bytes) {
        return 0;
    }

    features = gxmetal_guest_register_read(transport,
                                           GXMETAL_REG_FEATURES_LO);
    features |= (uint64_t)gxmetal_guest_register_read(
                    transport, GXMETAL_REG_FEATURES_HI) << 32;
    if ((features & required_features) != required_features) {
        return 0;
    }
    status = gxmetal_guest_register_read(transport, GXMETAL_REG_STATUS);
    if ((status & GXMETAL_STATUS_READY) == 0 ||
        (status & (GXMETAL_STATUS_FAULTED |
                   GXMETAL_STATUS_DEVICE_LOST)) != 0) {
        return 0;
    }

    transport->features = features;
    transport->producer = gxmetal_guest_register_read(
        transport, GXMETAL_REG_PRODUCER);
    if (transport->producer >= transport->ring_bytes ||
        (transport->producer & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        return 0;
    }
    transport->next_sequence = 1;
    return 1;
}

static uint32_t gxmetal_guest_ring_free(uint32_t producer,
                                        uint32_t consumer,
                                        uint32_t ring_bytes)
{
    if (consumer > producer) {
        return consumer - producer;
    }
    return ring_bytes - producer + consumer;
}

int gxmetal_guest_packet_begin(GXMetalGuestTransport *transport,
                               uint16_t opcode, uint32_t packet_bytes,
                               uint32_t context_id,
                               GXMetalGuestPacket *packet)
{
    uint32_t consumer;
    uint32_t free_bytes;
    uint32_t tail_bytes;
    uint32_t required_bytes;
    uint8_t *destination;

    if (transport == NULL || packet == NULL || opcode == GXMETAL_OP_PAD ||
        packet_bytes < GXMETAL_PACKET_HEADER_BYTES ||
        packet_bytes > GXMETAL_MAX_PACKET_BYTES ||
        (packet_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        return 0;
    }
    if (gxmetal_guest_register_read(transport, GXMETAL_REG_STATUS) !=
        GXMETAL_STATUS_READY) {
        return 0;
    }

    consumer = gxmetal_guest_register_read(transport, GXMETAL_REG_CONSUMER);
    if (consumer >= transport->ring_bytes ||
        (consumer & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        return 0;
    }
    free_bytes = gxmetal_guest_ring_free(transport->producer, consumer,
                                         transport->ring_bytes);
    tail_bytes = transport->ring_bytes - transport->producer;
    required_bytes = packet_bytes;
    if (tail_bytes < packet_bytes) {
        required_bytes += tail_bytes;
    }
    /* Keep one aligned slot empty so producer == consumer means empty. */
    if (free_bytes < required_bytes + GXMETAL_PACKET_ALIGNMENT) {
        return 0;
    }

    if (tail_bytes < packet_bytes) {
        destination = transport->shared + transport->ring_offset +
                      transport->producer;
        memset(destination, 0, tail_bytes);
        gxmetal_store_le16(destination + GXMETAL_PACKET_OPCODE_OFFSET,
                           GXMETAL_OP_PAD);
        gxmetal_store_le16(destination +
                           GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                           GXMETAL_PACKET_HEADER_BYTES);
        gxmetal_store_le32(destination + GXMETAL_PACKET_BYTES_OFFSET,
                           tail_bytes);
        transport->producer = 0;
    }

    destination = transport->shared + transport->ring_offset +
                  transport->producer;
    memset(destination, 0, packet_bytes);
    packet->sequence = transport->next_sequence++;
    if (transport->next_sequence == 0) {
        transport->next_sequence = 1;
    }
    gxmetal_store_le16(destination + GXMETAL_PACKET_OPCODE_OFFSET, opcode);
    gxmetal_store_le16(destination + GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                       GXMETAL_PACKET_HEADER_BYTES);
    gxmetal_store_le32(destination + GXMETAL_PACKET_BYTES_OFFSET,
                       packet_bytes);
    gxmetal_store_le32(destination + GXMETAL_PACKET_CONTEXT_OFFSET,
                       context_id);
    gxmetal_store_le32(destination + GXMETAL_PACKET_SEQUENCE_OFFSET,
                       packet->sequence);

    packet->bytes = destination;
    packet->packet_bytes = packet_bytes;
    packet->next_producer = transport->producer + packet_bytes;
    if (packet->next_producer == transport->ring_bytes) {
        packet->next_producer = 0;
    }
    return 1;
}

void gxmetal_guest_packet_commit(GXMetalGuestTransport *transport,
                                 const GXMetalGuestPacket *packet)
{
    gxmetal_guest_barrier();
    transport->producer = packet->next_producer;
    gxmetal_guest_register_write(transport, GXMETAL_REG_PRODUCER,
                                 transport->producer);
    gxmetal_guest_register_write(transport, GXMETAL_REG_DOORBELL,
                                 packet->sequence);
}

int gxmetal_guest_emit_fence(GXMetalGuestTransport *transport,
                             uint32_t *sequence)
{
    GXMetalGuestPacket packet;

    if (!gxmetal_guest_packet_begin(transport, GXMETAL_OP_FENCE,
                                    GXMETAL_PACKET_HEADER_BYTES, 0,
                                    &packet)) {
        return 0;
    }
    gxmetal_guest_packet_commit(transport, &packet);
    if (sequence != NULL) {
        *sequence = packet.sequence;
    }
    return 1;
}

int gxmetal_guest_wait(const GXMetalGuestTransport *transport,
                       uint32_t sequence, uint32_t spin_limit)
{
    uint32_t i;

    for (i = 0; i < spin_limit; i++) {
        uint32_t status = gxmetal_guest_register_read(
            transport, GXMETAL_REG_STATUS);
        if (status & (GXMETAL_STATUS_FAULTED |
                      GXMETAL_STATUS_DEVICE_LOST)) {
            return 0;
        }
        if (gxmetal_guest_register_read(
                transport, GXMETAL_REG_COMPLETED_SEQUENCE) == sequence) {
            return 1;
        }
    }
    return 0;
}
