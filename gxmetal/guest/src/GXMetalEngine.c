/*
 * GXMetal QuickDraw 3D RAVE drawing engine.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 */

#include <RAVESystem.h>
#include <CodeFragments.h>
#include <Memory.h>
#include <NameRegistry.h>
#include <Quickdraw.h>
#include <stdint.h>
#include <string.h>

#include "GXMetalRegistry.h"
#include "GXMetalTransport.h"

#define GXMETAL_VENDOR_ID UINT32_C(0x47584d54) /* GXMT */
#define GXMETAL_ENGINE_ID UINT32_C(0x00000001)
#define GXMETAL_REVISION  UINT32_C(0x00010000)
#define GXMETAL_STATE_SLOTS 154u
#define GXMETAL_SYNC_SPINS UINT32_C(10000000)

typedef struct GXMetalDrawState {
    GXMetalGuestTransport *transport;
    uint32_t context_id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t framebuffer_offset;
    uint32_t context_flags;
    float float_state[GXMETAL_STATE_SLOTS];
    uint32_t int_state[GXMETAL_STATE_SLOTS];
    const void *texture;
    TQANoticeMethod notices[kQAMethod_NumSelectors];
    void *notice_refcons[kQAMethod_NumSelectors];
    TQABoolean failed;
} GXMetalDrawState;

static const char kGXMetalName[] = "GXMetal";
static GXMetalGuestTransport gTransport;
static GXMetalRegistryInfo gRegistry;
static TQABoolean gTransportConnected;
static uint32_t gNextContextID = 1;

static uint32_t GXMetalFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static TQABoolean GXMetalFindRegistryInfo(GXMetalRegistryInfo *info)
{
    RegEntryIter iterator;
    RegEntryID entry;
    RegPropertyValueSize size = sizeof(*info);
    Boolean done = true;
    OSStatus status;

    status = RegistryEntryIterateCreate(&iterator);
    if (status != noErr) {
        return 0;
    }
    status = RegistryEntrySearch(&iterator, kRegIterSubTrees, &entry, &done,
                                 GXMETAL_REGISTRY_PROPERTY, NULL, 0);
    RegistryEntryIterateDispose(&iterator);
    if (status != noErr || done) {
        return 0;
    }
    status = RegistryPropertyGet(&entry, GXMETAL_REGISTRY_PROPERTY,
                                 info, &size);
    if (status != noErr || size != sizeof(*info) ||
        info->magic != GXMETAL_PROTOCOL_MAGIC ||
        (info->version >> 16) != GXMETAL_REGISTRY_VERSION_MAJOR ||
        info->registers_address == 0 || info->shared_address == 0 ||
        info->framebuffer_address == 0) {
        return 0;
    }
    return 1;
}

static TQABoolean GXMetalTransportAvailable(void)
{
    uint32_t status;

    if (gTransportConnected) {
        status = gxmetal_guest_register_read(&gTransport,
                                             GXMETAL_REG_STATUS);
        if (status == GXMETAL_STATUS_READY) {
            return 1;
        }
        gTransportConnected = 0;
    }
    if (!GXMetalFindRegistryInfo(&gRegistry)) {
        return 0;
    }
    gTransportConnected = gxmetal_guest_transport_connect(
        &gTransport,
        (volatile void *)(uintptr_t)gRegistry.registers_address,
        gRegistry.registers_bytes,
        (void *)(uintptr_t)gRegistry.shared_address,
        gRegistry.shared_bytes,
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE);
    return gTransportConnected;
}

static GXMetalDrawState *GXMetalGetState(const TQADrawContext *drawContext)
{
    if (drawContext == NULL) {
        return NULL;
    }
    return (GXMetalDrawState *)drawContext->drawPrivate;
}

static TQABoolean GXMetalDescribeDevice(const TQADevice *device,
                                        const TQARect *rect,
                                        uint32_t *width,
                                        uint32_t *height,
                                        uint32_t *rowBytes,
                                        uint32_t *pixelFormat,
                                        uint32_t *framebufferOffset)
{
    uintptr_t baseAddress;
    uint32_t bytesPerPixel;
    int32_t left;
    int32_t right;
    int32_t top;
    int32_t bottom;
    uint64_t targetAddress;
    uint64_t targetEnd;

    if (device == NULL || rect == NULL || !GXMetalTransportAvailable()) {
        return 0;
    }
    left = (int32_t)rect->left;
    right = (int32_t)rect->right;
    top = (int32_t)rect->top;
    bottom = (int32_t)rect->bottom;
    if (left < 0 || top < 0 || right <= left || bottom <= top) {
        return 0;
    }

    if (device->deviceType == kQADeviceGDevice) {
        GDHandle graphicsDevice = device->device.gDevice;
        PixMapHandle pixmap;
        uint32_t pixelSize;

        if (graphicsDevice == NULL || *graphicsDevice == NULL) {
            return 0;
        }
        pixmap = (**graphicsDevice).gdPMap;
        if (pixmap == NULL || *pixmap == NULL) {
            return 0;
        }
        baseAddress = (uintptr_t)GetPixBaseAddr(pixmap);
        *rowBytes = (uint32_t)((**pixmap).rowBytes & 0x3fff);
        pixelSize = (uint32_t)(**pixmap).pixelSize;
        if (pixelSize == 16) {
            *pixelFormat = GXMETAL_PIXEL_RGB555;
            bytesPerPixel = 2;
        } else if (pixelSize == 32) {
            *pixelFormat = GXMETAL_PIXEL_RGB8888;
            bytesPerPixel = 4;
        } else {
            return 0;
        }
    } else if (device->deviceType == kQADeviceMemory) {
        const TQADeviceMemory *memory = &device->device.memoryDevice;
        baseAddress = (uintptr_t)memory->baseAddr;
        if (memory->rowBytes <= 0) {
            return 0;
        }
        *rowBytes = (uint32_t)memory->rowBytes;
        switch (memory->pixelType) {
        case kQAPixel_RGB16:
            *pixelFormat = GXMETAL_PIXEL_RGB555;
            bytesPerPixel = 2;
            break;
        case kQAPixel_ARGB32:
            *pixelFormat = GXMETAL_PIXEL_ARGB8888;
            bytesPerPixel = 4;
            break;
        case kQAPixel_RGB32:
            *pixelFormat = GXMETAL_PIXEL_RGB8888;
            bytesPerPixel = 4;
            break;
        default:
            return 0;
        }
    } else {
        return 0;
    }

    *width = (uint32_t)(right - left);
    *height = (uint32_t)(bottom - top);
    if (*rowBytes < *width * bytesPerPixel) {
        return 0;
    }
    targetAddress = (uint64_t)baseAddress + (uint64_t)(uint32_t)top *
        *rowBytes + (uint64_t)(uint32_t)left * bytesPerPixel;
    targetEnd = targetAddress + (uint64_t)(*height - 1) * *rowBytes +
        (uint64_t)*width * bytesPerPixel;
    if (targetAddress < gRegistry.framebuffer_address ||
        targetEnd > (uint64_t)gRegistry.framebuffer_address +
                    gRegistry.framebuffer_bytes) {
        return 0;
    }
    *framebufferOffset = (uint32_t)(targetAddress -
                                    gRegistry.framebuffer_address);
    return 1;
}

static TQABoolean GXMetalBeginPacket(GXMetalDrawState *state,
                                     uint16_t opcode, uint32_t bytes,
                                     GXMetalGuestPacket *packet)
{
    if (state == NULL || state->failed ||
        !gxmetal_guest_packet_begin(state->transport, opcode, bytes,
                                    state->context_id, packet)) {
        if (state != NULL) {
            state->failed = 1;
        }
        return 0;
    }
    return 1;
}

static void GXMetalCommitPacket(GXMetalDrawState *state,
                                GXMetalGuestPacket *packet)
{
    gxmetal_guest_packet_commit(state->transport, packet);
}

static void GXMetalStoreRect(uint8_t *payload, const TQARect *rect,
                             uint32_t width, uint32_t height)
{
    if (rect == NULL) {
        gxmetal_store_le32(payload + GXMETAL_RECT_LEFT_OFFSET, 0);
        gxmetal_store_le32(payload + GXMETAL_RECT_TOP_OFFSET, 0);
        gxmetal_store_le32(payload + GXMETAL_RECT_RIGHT_OFFSET, width);
        gxmetal_store_le32(payload + GXMETAL_RECT_BOTTOM_OFFSET, height);
    } else {
        gxmetal_store_le32(payload + GXMETAL_RECT_LEFT_OFFSET,
                           (uint32_t)rect->left);
        gxmetal_store_le32(payload + GXMETAL_RECT_TOP_OFFSET,
                           (uint32_t)rect->top);
        gxmetal_store_le32(payload + GXMETAL_RECT_RIGHT_OFFSET,
                           (uint32_t)rect->right);
        gxmetal_store_le32(payload + GXMETAL_RECT_BOTTOM_OFFSET,
                           (uint32_t)rect->bottom);
    }
}

static TQAError GXMetalEmitRect(GXMetalDrawState *state, uint16_t opcode,
                                const TQARect *rect)
{
    GXMetalGuestPacket packet;

    if (!GXMetalBeginPacket(state, opcode, 32, &packet)) {
        return kQAError;
    }
    GXMetalStoreRect(packet.bytes + GXMETAL_PACKET_HEADER_BYTES, rect,
                     state->width, state->height);
    GXMetalCommitPacket(state, &packet);
    return kQANoErr;
}

static void GXMetalStoreGouraud(uint8_t *destination,
                                const TQAVGouraud *vertex)
{
    gxmetal_store_le32(destination + GXMETAL_VERTEX_X_OFFSET,
                       GXMetalFloatBits(vertex->x));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_Y_OFFSET,
                       GXMetalFloatBits(vertex->y));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_Z_OFFSET,
                       GXMetalFloatBits(vertex->z));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_INV_W_OFFSET,
                       GXMetalFloatBits(vertex->invW));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_R_OFFSET,
                       GXMetalFloatBits(vertex->r));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_G_OFFSET,
                       GXMetalFloatBits(vertex->g));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_B_OFFSET,
                       GXMetalFloatBits(vertex->b));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_A_OFFSET,
                       GXMetalFloatBits(vertex->a));
}

static TQABoolean GXMetalEmitGouraud(GXMetalDrawState *state,
                                     uint32_t primitive,
                                     uint32_t count,
                                     const TQAVGouraud *vertices,
                                     uint32_t flags)
{
    GXMetalGuestPacket packet;
    uint32_t packetBytes;
    uint8_t *payload;
    uint32_t i;

    if (count == 0 || vertices == NULL ||
        (uint64_t)GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
            (uint64_t)count * GXMETAL_GOURAUD_VERTEX_BYTES >
                GXMETAL_MAX_PACKET_BYTES) {
        state->failed = 1;
        return 0;
    }
    packetBytes = GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
                  count * GXMETAL_GOURAUD_VERTEX_BYTES;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_DRAW_GOURAUD, packetBytes,
                            &packet)) {
        return 0;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET, primitive);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, count);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_GOURAUD_VERTEX_BYTES);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET, flags);
    for (i = 0; i < count; i++) {
        GXMetalStoreGouraud(payload + GXMETAL_DRAW_VERTICES_OFFSET +
                            i * GXMETAL_GOURAUD_VERTEX_BYTES, &vertices[i]);
    }
    GXMetalCommitPacket(state, &packet);
    return 1;
}

static TQAError GXMetalEmitClear(GXMetalDrawState *state,
                                 const TQARect *rect, uint32_t flags)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (!GXMetalBeginPacket(state, GXMETAL_OP_CLEAR,
                            GXMETAL_CLEAR_PACKET_BYTES, &packet)) {
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CLEAR_FLAGS_OFFSET, flags);
    gxmetal_store_le32(payload + GXMETAL_CLEAR_COLOR_R_OFFSET,
        GXMetalFloatBits(state->float_state[kQATag_ColorBG_r]));
    gxmetal_store_le32(payload + GXMETAL_CLEAR_COLOR_G_OFFSET,
        GXMetalFloatBits(state->float_state[kQATag_ColorBG_g]));
    gxmetal_store_le32(payload + GXMETAL_CLEAR_COLOR_B_OFFSET,
        GXMetalFloatBits(state->float_state[kQATag_ColorBG_b]));
    gxmetal_store_le32(payload + GXMETAL_CLEAR_COLOR_A_OFFSET,
        GXMetalFloatBits(state->float_state[kQATag_ColorBG_a]));
    gxmetal_store_le32(payload + GXMETAL_CLEAR_DEPTH_OFFSET,
        GXMetalFloatBits(state->float_state[kQATagGL_DepthBG]));
    GXMetalStoreRect(payload + GXMETAL_CLEAR_RECT_OFFSET, rect,
                     state->width, state->height);
    GXMetalCommitPacket(state, &packet);
    return kQANoErr;
}

static void GXMetalSetFloat(TQADrawContext *drawContext, TQATagFloat tag,
                            float newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return;
    }
    state->float_state[(uint32_t)tag] = newValue;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
                            GXMETAL_SET_STATE_PACKET_BYTES, &packet)) {
        return;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, (uint32_t)tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_FLOAT32);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET,
                       GXMetalFloatBits(newValue));
    GXMetalCommitPacket(state, &packet);
}

static void GXMetalSetInt(TQADrawContext *drawContext, TQATagInt tag,
                          unsigned long newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return;
    }
    state->int_state[(uint32_t)tag] = (uint32_t)newValue;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
                            GXMETAL_SET_STATE_PACKET_BYTES, &packet)) {
        return;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, (uint32_t)tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_UINT32);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET,
                       (uint32_t)newValue);
    GXMetalCommitPacket(state, &packet);
}

static void GXMetalSetPtr(TQADrawContext *drawContext, TQATagPtr tag,
                          const void *newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state != NULL && tag == kQATag_Texture) {
        state->texture = newValue;
    }
}

static float GXMetalGetFloat(const TQADrawContext *drawContext,
                             TQATagFloat tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return 0.0f;
    }
    return state->float_state[(uint32_t)tag];
}

static unsigned long GXMetalGetInt(const TQADrawContext *drawContext,
                                   TQATagInt tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return 0;
    }
    return state->int_state[(uint32_t)tag];
}

static void *GXMetalGetPtr(const TQADrawContext *drawContext, TQATagPtr tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL || tag != kQATag_Texture) {
        return NULL;
    }
    return (void *)state->texture;
}

static void GXMetalDrawPoint(const TQADrawContext *drawContext,
                             const TQAVGouraud *vertex)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state != NULL) {
        (void)GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_POINT, 1,
                                 vertex, 0);
    }
}

static void GXMetalDrawLine(const TQADrawContext *drawContext,
                            const TQAVGouraud *v0,
                            const TQAVGouraud *v1)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAVGouraud vertices[2];
    if (state == NULL || v0 == NULL || v1 == NULL) {
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    (void)GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_LINE, 2, vertices, 0);
}

static void GXMetalDrawTriGouraud(const TQADrawContext *drawContext,
                                  const TQAVGouraud *v0,
                                  const TQAVGouraud *v1,
                                  const TQAVGouraud *v2,
                                  unsigned long flags)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAVGouraud vertices[3];
    if (state == NULL || v0 == NULL || v1 == NULL || v2 == NULL) {
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    vertices[2] = *v2;
    (void)GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_TRIANGLE, 3,
                             vertices, (uint32_t)flags);
}

static void GXMetalDrawVGouraud(const TQADrawContext *drawContext,
                                unsigned long nVertices,
                                TQAVertexMode vertexMode,
                                const TQAVGouraud vertices[],
                                const unsigned long flags[])
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t primitive;
    unsigned long i;
    (void)flags;

    if (state == NULL || vertices == NULL || nVertices == 0) {
        return;
    }
    switch (vertexMode) {
    case kQAVertexMode_Point:
        primitive = GXMETAL_PRIMITIVE_POINT;
        break;
    case kQAVertexMode_Line:
        primitive = GXMETAL_PRIMITIVE_LINE;
        break;
    case kQAVertexMode_Tri:
        primitive = GXMETAL_PRIMITIVE_TRIANGLE;
        break;
    case kQAVertexMode_Strip:
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_STRIP;
        break;
    case kQAVertexMode_Fan:
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_FAN;
        break;
    case kQAVertexMode_Polyline:
        for (i = 0; i + 1 < nVertices; i++) {
            GXMetalDrawLine(drawContext, &vertices[i], &vertices[i + 1]);
        }
        return;
    default:
        state->failed = 1;
        return;
    }
    (void)GXMetalEmitGouraud(state, primitive, (uint32_t)nVertices,
                             vertices, 0);
}

static void GXMetalRenderStart(const TQADrawContext *drawContext,
                               const TQARect *dirtyRect,
                               const TQADrawContext *initialContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    (void)initialContext;
    if (state == NULL) {
        return;
    }
    if (GXMetalEmitRect(state, GXMETAL_OP_BEGIN_FRAME, dirtyRect) ==
        kQANoErr) {
        uint32_t clearFlags = GXMETAL_CLEAR_COLOR;
        if (state->context_flags & GXMETAL_CONTEXT_Z16) {
            clearFlags |= GXMETAL_CLEAR_DEPTH;
        }
        (void)GXMetalEmitClear(state, dirtyRect, clearFlags);
    }
}

static TQAError GXMetalRenderEnd(const TQADrawContext *drawContext,
                                 const TQARect *modifiedRect)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAError error;

    if (state == NULL || state->failed) {
        return kQAError;
    }
    error = GXMetalEmitRect(state, GXMETAL_OP_END_FRAME, modifiedRect);
    if (error == kQANoErr &&
        state->int_state[kQATag_DontSwap] == 0) {
        error = GXMetalEmitRect(state, GXMETAL_OP_PRESENT, modifiedRect);
    }
    if (error == kQANoErr &&
        state->notices[kQAMethod_RenderCompletion].standardNoticeMethod !=
            NULL) {
        state->notices[kQAMethod_RenderCompletion].standardNoticeMethod(
            drawContext,
            state->notice_refcons[kQAMethod_RenderCompletion]);
    }
    return error;
}

static TQAError GXMetalSync(const TQADrawContext *drawContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t sequence;

    if (state == NULL || state->failed ||
        !gxmetal_guest_emit_fence(state->transport, &sequence) ||
        !gxmetal_guest_wait(state->transport, sequence,
                            GXMETAL_SYNC_SPINS)) {
        if (state != NULL) {
            state->failed = 1;
        }
        return kQAError;
    }
    return kQANoErr;
}

static TQAError GXMetalFlush(const TQADrawContext *drawContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    return state != NULL && !state->failed ? kQANoErr : kQAError;
}

static TQAError GXMetalRenderAbort(const TQADrawContext *drawContext)
{
    return GXMetalSync(drawContext);
}

static TQAError GXMetalSetNoticeMethod(const TQADrawContext *drawContext,
                                       TQAMethodSelector method,
                                       TQANoticeMethod callback,
                                       void *refCon)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL || method < 0 || method >= kQAMethod_NumSelectors) {
        return kQAParamErr;
    }
    state->notices[method] = callback;
    state->notice_refcons[method] = refCon;
    return kQANoErr;
}

static TQAError GXMetalGetNoticeMethod(const TQADrawContext *drawContext,
                                       TQAMethodSelector method,
                                       TQANoticeMethod *callback,
                                       void **refCon)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL || callback == NULL || refCon == NULL || method < 0 ||
        method >= kQAMethod_NumSelectors) {
        return kQAParamErr;
    }
    *callback = state->notices[method];
    *refCon = state->notice_refcons[method];
    return kQANoErr;
}

static TQAError GXMetalClearDrawBuffer(const TQADrawContext *drawContext,
                                       const TQARect *rect,
                                       const TQADrawContext *initialContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    (void)initialContext;
    if (state == NULL) {
        return kQAParamErr;
    }
    return GXMetalEmitClear(state, rect, GXMETAL_CLEAR_COLOR);
}

static TQAError GXMetalClearZBuffer(const TQADrawContext *drawContext,
                                    const TQARect *rect,
                                    const TQADrawContext *initialContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    (void)initialContext;
    if (state == NULL ||
        (state->context_flags & GXMETAL_CONTEXT_Z16) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitClear(state, rect, GXMETAL_CLEAR_DEPTH);
}

static TQAError GXMetalSwapBuffers(const TQADrawContext *drawContext,
                                   const TQARect *dirtyRect)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    if (state == NULL ||
        (state->context_flags & GXMETAL_CONTEXT_DOUBLE_BUFFER) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitRect(state, GXMETAL_OP_PRESENT, dirtyRect);
}

static TQAError GXMetalRegisterMethods(TQADrawContext *drawContext)
{
    TQADrawMethod method;
    TQAError error;

#define GXMETAL_REGISTER_METHOD(tag, member, callback) do { \
    memset(&method, 0, sizeof(method)); \
    method.member = callback; \
    error = QARegisterDrawMethod(drawContext, tag, method); \
    if (error != kQANoErr) { \
        return error; \
    } \
} while (0)

    GXMETAL_REGISTER_METHOD(kQASetFloat, setFloat, GXMetalSetFloat);
    GXMETAL_REGISTER_METHOD(kQASetInt, setInt, GXMetalSetInt);
    GXMETAL_REGISTER_METHOD(kQASetPtr, setPtr, GXMetalSetPtr);
    GXMETAL_REGISTER_METHOD(kQAGetFloat, getFloat, GXMetalGetFloat);
    GXMETAL_REGISTER_METHOD(kQAGetInt, getInt, GXMetalGetInt);
    GXMETAL_REGISTER_METHOD(kQAGetPtr, getPtr, GXMetalGetPtr);
    GXMETAL_REGISTER_METHOD(kQADrawPoint, drawPoint, GXMetalDrawPoint);
    GXMETAL_REGISTER_METHOD(kQADrawLine, drawLine, GXMetalDrawLine);
    GXMETAL_REGISTER_METHOD(kQADrawTriGouraud, drawTriGouraud,
                            GXMetalDrawTriGouraud);
    GXMETAL_REGISTER_METHOD(kQADrawVGouraud, drawVGouraud,
                            GXMetalDrawVGouraud);
    GXMETAL_REGISTER_METHOD(kQARenderStart, renderStart, GXMetalRenderStart);
    GXMETAL_REGISTER_METHOD(kQARenderEnd, renderEnd, GXMetalRenderEnd);
    GXMETAL_REGISTER_METHOD(kQARenderAbort, renderAbort, GXMetalRenderAbort);
    GXMETAL_REGISTER_METHOD(kQAFlush, flush, GXMetalFlush);
    GXMETAL_REGISTER_METHOD(kQASync, sync, GXMetalSync);
    GXMETAL_REGISTER_METHOD(kQASetNoticeMethod, setNoticeMethod,
                            GXMetalSetNoticeMethod);
    GXMETAL_REGISTER_METHOD(kQAGetNoticeMethod, getNoticeMethod,
                            GXMetalGetNoticeMethod);
    if (drawContext->version >= kQAVersion_1_6) {
        GXMETAL_REGISTER_METHOD(kQClearDrawBuffer, clearDrawBuffer,
                                GXMetalClearDrawBuffer);
        GXMETAL_REGISTER_METHOD(kQClearZBuffer, clearZBuffer,
                                GXMetalClearZBuffer);
        GXMETAL_REGISTER_METHOD(kQSwapBuffers, swapBuffers,
                                GXMetalSwapBuffers);
    }
#undef GXMETAL_REGISTER_METHOD
    return kQANoErr;
}

static TQAError GXMetalDrawPrivateNew(TQADrawContext *newDrawContext,
                                      const TQADevice *device,
                                      const TQARect *rect,
                                      const TQAClip *clip,
                                      unsigned long flags)
{
    GXMetalDrawState *state;
    GXMetalGuestPacket packet;
    uint8_t *payload;
    TQAError error;
    uint32_t contextFlags = 0;
    (void)clip;

    if (newDrawContext == NULL || !GXMetalTransportAvailable() ||
        (flags & (kQAContext_DeepZ | kQAContext_Cache |
                  kQAContext_Scale)) != 0 ||
        ((flags & kQAContext_NoZBuffer) == 0 &&
         (gTransport.features & GXMETAL_FEATURE_Z16) == 0) ||
        ((flags & kQAContext_DoubleBuffer) != 0 &&
         (gTransport.features & GXMETAL_FEATURE_DOUBLE_BUFFER) == 0)) {
        return kQANotSupported;
    }
    state = (GXMetalDrawState *)NewPtrClear(sizeof(*state));
    if (state == NULL) {
        return kQAOutOfMemory;
    }
    if (!GXMetalDescribeDevice(device, rect, &state->width, &state->height,
                               &state->row_bytes, &state->pixel_format,
                               &state->framebuffer_offset)) {
        DisposePtr((Ptr)state);
        return kQANotSupported;
    }
    state->transport = &gTransport;
    state->context_id = gNextContextID++;
    if (gNextContextID == 0) {
        gNextContextID = 1;
    }
    state->float_state[kQATag_ColorBG_a] = 1.0f;
    state->float_state[kQATag_Width] = 1.0f;
    state->float_state[kQATagGL_DepthBG] = 1.0f;
    state->int_state[kQATag_Blend] = kQABlend_Interpolate;
    state->int_state[kQATag_ZBufferMask] = kQAZBufferMask_Enable;
    if ((flags & kQAContext_NoZBuffer) == 0) {
        contextFlags |= GXMETAL_CONTEXT_Z16;
        state->int_state[kQATag_ZFunction] = kQAZFunction_LT;
    }
    if (flags & kQAContext_DoubleBuffer) {
        contextFlags |= GXMETAL_CONTEXT_DOUBLE_BUFFER;
    }
    if (flags & kQAContext_NoDither) {
        contextFlags |= GXMETAL_CONTEXT_NO_DITHER;
    }
    state->context_flags = contextFlags;

    if (!GXMetalBeginPacket(state, GXMETAL_OP_CONTEXT_CREATE,
                            GXMETAL_CONTEXT_CREATE_PACKET_BYTES, &packet)) {
        DisposePtr((Ptr)state);
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, state->width);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET,
                       state->height);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET,
                       state->row_bytes);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET,
                       state->pixel_format);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FRAMEBUFFER_OFFSET,
                       state->framebuffer_offset);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FLAGS_OFFSET, contextFlags);
    GXMetalCommitPacket(state, &packet);
    if (gxmetal_guest_register_read(state->transport, GXMETAL_REG_STATUS) !=
        GXMETAL_STATUS_READY) {
        DisposePtr((Ptr)state);
        return kQAError;
    }

    newDrawContext->drawPrivate = (TQADrawPrivate *)state;
    error = GXMetalRegisterMethods(newDrawContext);
    if (error != kQANoErr) {
        newDrawContext->drawPrivate = NULL;
        DisposePtr((Ptr)state);
        return error;
    }
    return kQANoErr;
}

static void GXMetalDrawPrivateDelete(TQADrawPrivate *drawPrivate)
{
    GXMetalDrawState *state = (GXMetalDrawState *)drawPrivate;
    GXMetalGuestPacket packet;

    if (state == NULL) {
        return;
    }
    if (!state->failed && GXMetalBeginPacket(state,
            GXMETAL_OP_CONTEXT_DESTROY, GXMETAL_PACKET_HEADER_BYTES,
            &packet)) {
        GXMetalCommitPacket(state, &packet);
    }
    DisposePtr((Ptr)state);
}

static TQAError GXMetalEngineCheckDevice(const TQADevice *device)
{
    TQARect rect;
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint32_t pixelFormat;
    uint32_t framebufferOffset;

    if (device == NULL || !GXMetalTransportAvailable()) {
        return kQANotSupported;
    }
    if (device->deviceType == kQADeviceGDevice) {
        GDHandle graphicsDevice = device->device.gDevice;
        PixMapHandle pixmap;
        if (graphicsDevice == NULL || *graphicsDevice == NULL) {
            return kQANotSupported;
        }
        pixmap = (**graphicsDevice).gdPMap;
        if (pixmap == NULL || *pixmap == NULL) {
            return kQANotSupported;
        }
        rect.left = 0;
        rect.top = 0;
        rect.right = (**pixmap).bounds.right - (**pixmap).bounds.left;
        rect.bottom = (**pixmap).bounds.bottom - (**pixmap).bounds.top;
    } else if (device->deviceType == kQADeviceMemory) {
        rect.left = 0;
        rect.top = 0;
        rect.right = device->device.memoryDevice.width;
        rect.bottom = device->device.memoryDevice.height;
    } else {
        return kQANotSupported;
    }
    return GXMetalDescribeDevice(device, &rect, &width, &height, &rowBytes,
                                 &pixelFormat, &framebufferOffset) ?
        kQANoErr : kQANotSupported;
}

static TQAError GXMetalEngineGestalt(TQAGestaltSelector selector,
                                     void *response)
{
    uint32_t value;
    uint64_t features = GXMetalTransportAvailable() ? gTransport.features : 0;

    if (response == NULL) {
        return kQAParamErr;
    }
    switch (selector) {
    case kQAGestalt_OptionalFeatures:
        value = kQAOptional_BoundToDevice | kQAOptional_NoDither |
                kQAOptional_ClearDrawBuffer;
        if (features & GXMETAL_FEATURE_BLEND) {
            value |= kQAOptional_Blend | kQAOptional_BlendAlpha;
        }
        if (features & GXMETAL_FEATURE_Z16) {
            value |= kQAOptional_ZBufferMask | kQAOptional_ClearZBuffer;
        }
        break;
    case kQAGestalt_FastFeatures:
        value = kQAFast_Line | kQAFast_Gouraud;
        if (features & GXMETAL_FEATURE_BLEND) {
            value |= kQAFast_Blend;
        }
        break;
    case kQAGestalt_TextureMemory:
    case kQAGestalt_FastTextureMemory:
    case kQAGestalt_MultiTextureMax:
        value = 0;
        break;
    case kQAGestalt_OptionalFeatures2:
        value = (features & GXMETAL_FEATURE_DOUBLE_BUFFER) ?
            kQAOptional2_SwapBuffers : 0;
        break;
    case kQAGestalt_VendorID:
        value = GXMETAL_VENDOR_ID;
        break;
    case kQAGestalt_EngineID:
        value = GXMETAL_ENGINE_ID;
        break;
    case kQAGestalt_Revision:
        value = GXMETAL_REVISION;
        break;
    case kQAGestalt_ASCIINameLength:
        value = (uint32_t)strlen(kGXMetalName);
        break;
    case kQAGestalt_ASCIIName:
        strcpy((char *)response, kGXMetalName);
        return kQANoErr;
    case kQAGestalt_DrawContextPixelTypesAllowed:
    case kQAGestalt_DrawContextPixelTypesPreferred:
        value = (UINT32_C(1) << kQAPixel_RGB16) |
                (UINT32_C(1) << kQAPixel_RGB32) |
                (UINT32_C(1) << kQAPixel_ARGB32);
        break;
    case kQAGestalt_TexturePixelTypesAllowed:
    case kQAGestalt_TexturePixelTypesPreferred:
    case kQAGestalt_BitmapPixelTypesAllowed:
    case kQAGestalt_BitmapPixelTypesPreferred:
        value = 0;
        break;
    default:
        return kQAGestaltUnknown;
    }
    *(uint32_t *)response = value;
    return kQANoErr;
}

TQAError GXMetalEngineGetMethod(TQAEngineMethodTag methodTag,
                                TQAEngineMethod *method)
{
    if (method == NULL) {
        return kQAParamErr;
    }
    switch (methodTag) {
    case kQADrawPrivateNew:
        method->drawPrivateNew = GXMetalDrawPrivateNew;
        break;
    case kQADrawPrivateDelete:
        method->drawPrivateDelete = GXMetalDrawPrivateDelete;
        break;
    case kQAEngineCheckDevice:
        method->engineCheckDevice = GXMetalEngineCheckDevice;
        break;
    case kQAEngineGestalt:
        method->engineGestalt = GXMetalEngineGestalt;
        break;
    default:
        return kQANotSupported;
    }
    return kQANoErr;
}

OSErr GXMetalCFMInitialize(const CFragInitBlock *initBlock)
{
    (void)initBlock;
    return (OSErr)QARegisterEngine(GXMetalEngineGetMethod);
}
