/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_DECODE_H
#define GXMETAL_DECODE_H

#include <stddef.h>
#include <stdint.h>

#include "gxmetal_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GXMetalDecodeResult {
    GXMETAL_DECODE_OK = 0,
    GXMETAL_DECODE_TRUNCATED,
    GXMETAL_DECODE_BAD_HEADER,
    GXMETAL_DECODE_BAD_SIZE,
    GXMETAL_DECODE_BAD_ALIGNMENT,
    GXMETAL_DECODE_BAD_OPCODE,
    GXMETAL_DECODE_BAD_RANGE
} GXMetalDecodeResult;

typedef struct GXMetalPacketView {
    uint16_t opcode;
    uint32_t packet_bytes;
    uint32_t context_id;
    uint32_t sequence;
    const uint8_t *payload;
    uint32_t payload_bytes;
} GXMetalPacketView;

GXMetalDecodeResult gxmetal_decode_packet(const void *bytes,
                                          size_t available_bytes,
                                          GXMetalPacketView *view);
GXMetalDecodeResult gxmetal_ring_advance(uint32_t position,
                                         uint32_t packet_bytes,
                                         uint32_t ring_bytes,
                                         uint32_t *next_position);
uint32_t gxmetal_validate_packet(const GXMetalPacketView *packet,
                                 uint32_t shared_bytes);
int gxmetal_shared_range_valid(uint32_t offset, uint32_t length,
                               uint32_t shared_bytes, uint32_t alignment);
const char *gxmetal_decode_result_string(GXMetalDecodeResult result);

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_DECODE_H */
