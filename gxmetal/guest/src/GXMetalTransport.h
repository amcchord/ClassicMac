/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_GUEST_TRANSPORT_H
#define GXMETAL_GUEST_TRANSPORT_H

#include <stdint.h>

#include "gxmetal_protocol.h"

typedef struct GXMetalGuestTransport {
    volatile uint32_t *registers;
    uint8_t *shared;
    uint32_t shared_bytes;
    uint32_t ring_offset;
    uint32_t ring_bytes;
    uint32_t producer;
    uint32_t consumer;
    uint32_t published_producer;
    uint32_t pending_bytes;
    uint32_t pending_packets;
    uint32_t next_sequence;
    uint32_t last_sequence;
    uint32_t status;
    uint64_t features;
} GXMetalGuestTransport;

typedef struct GXMetalGuestPacket {
    uint8_t *bytes;
    uint32_t packet_bytes;
    uint32_t next_producer;
    uint32_t sequence;
    uint16_t opcode;
} GXMetalGuestPacket;

int gxmetal_guest_transport_connect(GXMetalGuestTransport *transport,
                                    volatile void *registers,
                                    uint32_t register_bytes,
                                    void *shared,
                                    uint32_t shared_bytes,
                                    uint64_t required_features);
uint32_t gxmetal_guest_register_read(const GXMetalGuestTransport *transport,
                                     uint32_t offset);
void gxmetal_guest_register_write(const GXMetalGuestTransport *transport,
                                  uint32_t offset, uint32_t value);
int gxmetal_guest_packet_begin(GXMetalGuestTransport *transport,
                               uint16_t opcode, uint32_t packet_bytes,
                               uint32_t context_id,
                               GXMetalGuestPacket *packet);
int gxmetal_guest_draw_packet_begin(GXMetalGuestTransport *transport,
                                    uint16_t opcode, uint32_t packet_bytes,
                                    uint32_t context_id,
                                    GXMetalGuestPacket *packet);
void gxmetal_guest_packet_commit(GXMetalGuestTransport *transport,
                                 const GXMetalGuestPacket *packet);
int gxmetal_guest_flush(GXMetalGuestTransport *transport);
int gxmetal_guest_emit_fence(GXMetalGuestTransport *transport,
                             uint32_t *sequence);
int gxmetal_guest_wait(const GXMetalGuestTransport *transport,
                       uint32_t sequence, uint32_t spin_limit);

#endif /* GXMETAL_GUEST_TRANSPORT_H */
