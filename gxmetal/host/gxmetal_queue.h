/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_QUEUE_H
#define GXMETAL_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "gxmetal_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*GXMetalDispatchPacket)(void *opaque,
                                          const GXMetalPacketView *packet);

typedef struct GXMetalQueue {
    uint8_t *shared;
    uint32_t shared_bytes;
    uint32_t ring_offset;
    uint32_t ring_bytes;
    uint32_t producer;
    uint32_t consumer;
    uint32_t status;
    uint32_t error;
    uint32_t completed_sequence;
    uint32_t diagnostic;
    GXMetalDispatchPacket dispatch;
    void *dispatch_opaque;
} GXMetalQueue;

void gxmetal_queue_init(GXMetalQueue *queue, void *shared,
                        uint32_t shared_bytes, uint32_t ring_offset,
                        uint32_t ring_bytes, GXMetalDispatchPacket dispatch,
                        void *dispatch_opaque);
void gxmetal_queue_reset(GXMetalQueue *queue);
int gxmetal_queue_publish(GXMetalQueue *queue, uint32_t producer);
void gxmetal_queue_process(GXMetalQueue *queue);

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_QUEUE_H */
