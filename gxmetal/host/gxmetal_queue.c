/* SPDX-License-Identifier: MIT */

#include "gxmetal_queue.h"

#include <string.h>

static void gxmetal_queue_fault(GXMetalQueue *queue, uint32_t error)
{
    queue->error = error;
    queue->status &= ~GXMETAL_STATUS_PROCESSING;
    queue->status |= GXMETAL_STATUS_FAULTED;
}

void gxmetal_queue_init(GXMetalQueue *queue, void *shared,
                        uint32_t shared_bytes, uint32_t ring_offset,
                        uint32_t ring_bytes, GXMetalDispatchPacket dispatch,
                        void *dispatch_opaque)
{
    memset(queue, 0, sizeof(*queue));
    queue->shared = (uint8_t *)shared;
    queue->shared_bytes = shared_bytes;
    queue->ring_offset = ring_offset;
    queue->ring_bytes = ring_bytes;
    queue->dispatch = dispatch;
    queue->dispatch_opaque = dispatch_opaque;

    if (shared == NULL || ring_bytes < GXMETAL_PACKET_ALIGNMENT ||
        (ring_bytes & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        (ring_offset & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0 ||
        (uint64_t)ring_offset + ring_bytes > shared_bytes) {
        gxmetal_queue_fault(queue, GXMETAL_ERROR_BAD_RING);
        return;
    }
    queue->status = GXMETAL_STATUS_READY;
}

void gxmetal_queue_reset(GXMetalQueue *queue)
{
    queue->producer = 0;
    queue->consumer = 0;
    queue->error = GXMETAL_ERROR_NONE;
    queue->completed_sequence = 0;
    queue->diagnostic = 0;
    queue->status = GXMETAL_STATUS_READY;
}

int gxmetal_queue_publish(GXMetalQueue *queue, uint32_t producer)
{
    if (queue->status & GXMETAL_STATUS_FAULTED) {
        return 0;
    }
    if (producer >= queue->ring_bytes ||
        (producer & (GXMETAL_PACKET_ALIGNMENT - 1)) != 0) {
        gxmetal_queue_fault(queue, GXMETAL_ERROR_BAD_RING);
        return 0;
    }
    queue->producer = producer;
    return 1;
}

void gxmetal_queue_process(GXMetalQueue *queue)
{
    uint32_t packet_budget;

    if (queue->status & GXMETAL_STATUS_FAULTED) {
        return;
    }

    queue->status |= GXMETAL_STATUS_PROCESSING;
    packet_budget = queue->ring_bytes / GXMETAL_PACKET_ALIGNMENT;
    while (queue->consumer != queue->producer) {
        GXMetalPacketView packet;
        GXMetalDecodeResult result;
        uint32_t available = queue->ring_bytes - queue->consumer;
        uint32_t next;
        uint32_t dispatch_error = GXMETAL_ERROR_NONE;

        if (packet_budget == 0) {
            gxmetal_queue_fault(queue, GXMETAL_ERROR_BAD_RING);
            return;
        }
        packet_budget--;

        result = gxmetal_decode_packet(
            queue->shared + queue->ring_offset + queue->consumer,
            available, &packet);
        if (result != GXMETAL_DECODE_OK) {
            gxmetal_queue_fault(queue,
                result == GXMETAL_DECODE_BAD_OPCODE ?
                    GXMETAL_ERROR_BAD_OPCODE : GXMETAL_ERROR_BAD_PACKET);
            return;
        }
        result = gxmetal_ring_advance(queue->consumer, packet.packet_bytes,
                                      queue->ring_bytes, &next);
        if (result != GXMETAL_DECODE_OK) {
            gxmetal_queue_fault(queue, GXMETAL_ERROR_BAD_PACKET);
            return;
        }
        dispatch_error = gxmetal_validate_packet(&packet,
                                                 queue->shared_bytes);
        if (dispatch_error != GXMETAL_ERROR_NONE) {
            gxmetal_queue_fault(queue, dispatch_error);
            return;
        }

        switch (packet.opcode) {
        case GXMETAL_OP_PAD:
            if (next != 0) {
                gxmetal_queue_fault(queue, GXMETAL_ERROR_BAD_PACKET);
                return;
            }
            break;
        case GXMETAL_OP_RESET:
            queue->error = GXMETAL_ERROR_NONE;
            queue->completed_sequence = 0;
            queue->diagnostic = 0;
            break;
        case GXMETAL_OP_FENCE:
            queue->completed_sequence = packet.sequence;
            break;
        default:
            if (queue->dispatch == NULL) {
                dispatch_error = GXMETAL_ERROR_BAD_OPCODE;
            } else {
                dispatch_error = queue->dispatch(queue->dispatch_opaque,
                                                 &packet);
            }
            if (dispatch_error != GXMETAL_ERROR_NONE) {
                gxmetal_queue_fault(queue, dispatch_error);
                return;
            }
            break;
        }

        queue->consumer = next;
        queue->diagnostic++;
    }

    queue->status &= ~GXMETAL_STATUS_PROCESSING;
}
