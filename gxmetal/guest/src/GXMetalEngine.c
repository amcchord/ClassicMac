/*
 * GXMetal QuickDraw 3D RAVE drawing engine.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 */

#include <RAVESystem.h>
#include <CodeFragments.h>
#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <NameRegistry.h>
#include <Quickdraw.h>
#include <stdint.h>
#include <string.h>

#include "GXMetalRegistry.h"
#include "GXMetalDiagnostics.h"
#include "GXMetalATICompatibility.h"
#include "GXMetalRAVECompatibility.h"
#include "GXMetalTransport.h"
#include "GXMetalVersion.h"

#define GXMETAL_REGISTRATION_VENDOR_ID UINT32_C(0x47584d54) /* GXMT */
#define GXMETAL_LEGACY_VENDOR_ID UINT32_C(1) /* kQAVendor_ATI */
#define GXMETAL_STATE_SLOTS 154u
#define GXMETAL_SYNC_SPINS UINT32_C(10000000)
#define GXMETAL_TEXTURE_MAGIC UINT32_C(0x47585458) /* GXTX */
#define GXMETAL_BITMAP_MAGIC UINT32_C(0x4758424d) /* GXBM */
#define GXMETAL_COLOR_TABLE_MAGIC UINT32_C(0x47584354) /* GXCT */
#define GXMETAL_TEXTURE_MEMORY UINT32_C(0x04000000)
#define GXMETAL_MAX_MIP_LEVELS 15u
#define GXMETAL_MAX_SUBMITTED_VERTICES UINT32_C(65535)
#define GXMETAL_MESH_BATCH_TRIANGLES UINT32_C(256)
#define GXMETAL_DRAW_BATCH_TRIANGLES UINT32_C(64)
#define GXMETAL_DRAW_BATCH_VERTICES (GXMETAL_DRAW_BATCH_TRIANGLES * 3u)
#define GXMETAL_DRAW_BATCH_NONE UINT32_C(0)
#define GXMETAL_DRAW_BATCH_GOURAUD UINT32_C(1)
#define GXMETAL_DRAW_BATCH_TEXTURE UINT32_C(2)
#define GXMETAL_ATI_PIXEL_RGB16 ((TQAImagePixelType)1001)
#define GXMETAL_ATI_PIXEL_RGBA32 ((TQAImagePixelType)1006)
#define GXMETAL_ATI_GESTALT_BOARD_MEMORY ((TQAGestaltSelector)1001)
#define GXMETAL_ATI_GESTALT_ENGINE_METHODS ((TQAGestaltSelector)1002)
#define GXMETAL_ATI_GESTALT_TEXTURE_FLAGS ((TQAGestaltSelector)1005)

typedef struct GXMetalClipRect {
    uint32_t left;
    uint32_t top;
    uint32_t right;
    uint32_t bottom;
} GXMetalClipRect;

typedef struct GXMetalClipWork {
    GXMetalClipRect rects[GXMETAL_MAX_CLIP_RECTS];
    uint32_t previous[GXMETAL_MAX_CLIP_RECTS];
    uint32_t current[GXMETAL_MAX_CLIP_RECTS];
} GXMetalClipWork;

typedef struct GXMetalDrawState {
    struct GXMetalDrawState *next_state;
    const TQADrawContext *draw_context;
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
    uint8_t float_state_valid[GXMETAL_STATE_SLOTS];
    uint8_t int_state_valid[GXMETAL_STATE_SLOTS];
    uint32_t effective_texture_min_filter;
    uint32_t effective_texture_mag_filter;
    uint32_t effective_secondary_texture_min_filter;
    uint32_t effective_secondary_texture_mag_filter;
    const void *texture;
    uint32_t texture_resource_id;
    const void *secondary_texture;
    uint32_t secondary_texture_resource_id;
    Ptr submitted_gouraud_vertices;
    uint32_t submitted_gouraud_count;
    Ptr submitted_texture_vertices;
    uint32_t submitted_texture_count;
    Ptr submitted_multitexture_params;
    uint32_t submitted_multitexture_count;
    Ptr pending_vertices;
    uint32_t pending_kind;
    uint32_t pending_count;
    uint32_t pending_flags;
    uint32_t pending_ati_texel_coordinates;
    TQANoticeMethod notices[kQAMethod_NumSelectors];
    void *notice_refcons[kQAMethod_NumSelectors];
    uint32_t ati_private_enabled;
    uint32_t ati_private_state_synced;
    uint32_t ati_private_frame_started;
    uint32_t ati_private_frame_has_draws;
    uint32_t access_draw_buffer_active;
    TQABoolean failed;
} GXMetalDrawState;

struct TQATexture {
    uint32_t magic;
    uint32_t resource_id;
    uint32_t levels;
    uint32_t pixel_format;
    uint32_t width;
    uint32_t height;
    uint32_t source_pixel_type;
    uint32_t source_flags;
    uint32_t palette_bound;
    uint32_t source_row_bytes[GXMETAL_MAX_MIP_LEVELS];
    Ptr source_pixels[GXMETAL_MAX_MIP_LEVELS];
    const void *live_pixels[GXMETAL_MAX_MIP_LEVELS];
    uint32_t last_refresh_epoch;
    uint32_t access_active;
    uint32_t access_level;
};

/* GCC's may_alias type lets the packet encoder read the object
 * representation of RAVE's all-float vertex structures without copying each
 * 64-byte vertex to a temporary array first. */
typedef uint32_t GXMetalAliasedUInt32 __attribute__((__may_alias__));

struct TQABitmap {
    uint32_t magic;
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t source_pixel_type;
    uint32_t source_flags;
    uint32_t palette_bound;
    uint32_t source_row_bytes;
    Ptr source_pixels;
    uint32_t access_active;
};

struct TQAColorTable {
    uint32_t magic;
    uint32_t entries;
    uint32_t transparent_index_zero;
    uint32_t colors[256];
};

static const char kGXMetalName[] = "GXMetal";
static GXMetalGuestTransport gTransport;
static GXMetalRegistryInfo gRegistry;
static TQABoolean gTransportConnected;
static uint32_t gNextContextID = 1;
static uint32_t gNextResourceID = 1;
static uint32_t gRenderEpoch = 1;
static const TQADrawContext *gLastDrawContext;
static GXMetalDrawState *gDrawStates;
static TQABoolean gRegistrationVendorPending;
static int32_t gDiagnosticStatus = kGXMetalDiagnosticNotLoaded;
static GXMetalDiagnosticSnapshot gDiagnostics = {
    .magic = GXMETAL_DIAGNOSTIC_MAGIC,
    .version = GXMETAL_DIAGNOSTIC_VERSION
};

static TQABoolean GXMetalFlushPendingDraws(GXMetalDrawState *state);
static TQAError GXMetalSync(const TQADrawContext *drawContext);

static const unsigned char kGXMetalDriverTraceName[] = {
    20, 'G', 'X', 'M', 'e', 't', 'a', 'l', ' ', 'D', 'r', 'i', 'v', 'e',
    'r', ' ', 'T', 'r', 'a', 'c', 'e'
};

/* Diagnostic builds checkpoint the native big-endian snapshot to Preferences.
 * A game crash unloads the CFM fragment and loses its globals; this small file
 * preserves the last completed context/resource operation for host analysis. */
static void GXMetalPersistDiagnostics(void)
{
    FSSpec trace;
    short volume = 0;
    short refNum = -1;
    long directory = 0;
    long length = (long)sizeof(gDiagnostics);

    gDiagnostics.status = gDiagnosticStatus;
    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, false,
                   &volume, &directory) != noErr) {
        return;
    }
    (void)FSMakeFSSpec(volume, directory, kGXMetalDriverTraceName, &trace);
    if (FSpOpenDF(&trace, fsWrPerm, &refNum) != noErr) {
        if (FSpCreate(&trace, 'GXMT', 'GXDG', smSystemScript) != noErr ||
            FSpOpenDF(&trace, fsWrPerm, &refNum) != noErr) {
            return;
        }
    }
    (void)SetFPos(refNum, fsFromStart, 0);
    (void)FSWrite(refNum, &length, &gDiagnostics);
    (void)SetEOF(refNum, (long)sizeof(gDiagnostics));
    (void)FSClose(refNum);
}

int32_t GXMetalGetDiagnosticStatus(void)
{
    return gDiagnosticStatus;
}

int32_t GXMetalCopyDiagnostics(GXMetalDiagnosticSnapshot *snapshot,
                               uint32_t snapshotBytes)
{
    if (snapshot == NULL || snapshotBytes < sizeof(*snapshot)) {
        return kQAParamErr;
    }
    gDiagnostics.status = gDiagnosticStatus;
    memcpy(snapshot, &gDiagnostics, sizeof(*snapshot));
    return kQANoErr;
}

static uint32_t GXMetalFloatBits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float GXMetalBitsFloat(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void GXMetalCountPixelType(uint32_t counters[],
                                  TQAImagePixelType pixelType)
{
    uint32_t type = (uint32_t)pixelType;

    if (type < GXMETAL_DIAGNOSTIC_PIXEL_TYPES) {
        counters[type]++;
    }
}

static void GXMetalCountPrivatePixelType(uint32_t counters[],
                                         TQAImagePixelType pixelType)
{
    uint32_t type = (uint32_t)pixelType;

    if (type >= UINT32_C(1000) &&
        type < UINT32_C(1000) + GXMETAL_DIAGNOSTIC_PRIVATE_PIXEL_TYPES) {
        counters[type - UINT32_C(1000)]++;
    }
}

static TQABoolean GXMetalFindRegistryInfo(GXMetalRegistryInfo *info,
                                          RegEntryID *foundEntry)
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
    if (foundEntry != NULL) {
        *foundEntry = entry;
    }
    return 1;
}

static void GXMetalPublishDiagnostics(void)
{
    GXMetalRegistryInfo info;
    RegEntryID entry;

    if (!GXMetalFindRegistryInfo(&info, &entry)) {
        return;
    }
    gDiagnostics.status = gDiagnosticStatus;
    gDiagnostics.registry_framebuffer_address = info.framebuffer_address;
    gDiagnostics.registry_framebuffer_bytes = info.framebuffer_bytes;
    if (RegistryPropertySet(&entry, GXMETAL_DIAGNOSTIC_PROPERTY,
                            &gDiagnostics, sizeof(gDiagnostics)) != noErr) {
        (void)RegistryPropertyDelete(&entry,
                                     GXMETAL_DIAGNOSTIC_PROPERTY);
        (void)RegistryPropertyCreate(&entry, GXMETAL_DIAGNOSTIC_PROPERTY,
                                     &gDiagnostics, sizeof(gDiagnostics));
    }
}

static TQABoolean GXMetalTransportAvailable(void)
{
    uint32_t status;

    if (gTransportConnected) {
        status = gxmetal_guest_register_read(&gTransport,
                                             GXMETAL_REG_STATUS);
        if (status == GXMETAL_STATUS_READY) {
            gDiagnosticStatus = kGXMetalDiagnosticTransportReady;
            return 1;
        }
        gTransportConnected = 0;
    }
    if (!GXMetalFindRegistryInfo(&gRegistry, NULL)) {
        gDiagnosticStatus = kGXMetalDiagnosticRegistryUnavailable;
        return 0;
    }
    gDiagnosticStatus = kGXMetalDiagnosticRegistryReady;
    gDiagnosticStatus = kGXMetalDiagnosticTransportConnecting;
    gTransportConnected = gxmetal_guest_transport_connect(
        &gTransport,
        (volatile void *)(uintptr_t)gRegistry.registers_address,
        gRegistry.registers_bytes,
        (void *)(uintptr_t)gRegistry.shared_address,
        gRegistry.shared_bytes,
        GXMETAL_FEATURE_GOURAUD | GXMETAL_FEATURE_FENCE);
    gDiagnosticStatus = gTransportConnected ?
        kGXMetalDiagnosticTransportReady :
        kGXMetalDiagnosticTransportConnectionFailed;
    gDiagnostics.registry_framebuffer_address = gRegistry.framebuffer_address;
    gDiagnostics.registry_framebuffer_bytes = gRegistry.framebuffer_bytes;
    gDiagnostics.transport_features = (uint32_t)gTransport.features;
    gDiagnostics.transport_status = gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_STATUS);
    return gTransportConnected;
}

int32_t GXMetalProbeTransport(void)
{
    (void)GXMetalTransportAvailable();
    return gDiagnosticStatus;
}

OSErr GXMetalSetRelativeInputMode(Boolean relative)
{
    if (!GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RELATIVE_INPUT) == 0) {
        return unimpErr;
    }
    gxmetal_guest_register_write(&gTransport, GXMETAL_REG_RELATIVE_INPUT,
                                 relative ? 1u : 0u);
    return noErr;
}

OSErr GXMetalGetInputButtonState(UInt32 *buttons)
{
    if (buttons == NULL) {
        return paramErr;
    }
    if (!GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RELATIVE_INPUT) == 0) {
        return unimpErr;
    }
    *buttons = gxmetal_guest_register_read(&gTransport,
                                          GXMETAL_REG_INPUT_BUTTONS);
    return noErr;
}

OSErr GXMetalGetInputState(SInt32 *deltaX, SInt32 *deltaY, UInt32 *buttons)
{
    if (deltaX == NULL || deltaY == NULL || buttons == NULL) {
        return paramErr;
    }
    if (!GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RELATIVE_INPUT) == 0) {
        return unimpErr;
    }
    *deltaX = (SInt32)gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_RELATIVE_X);
    *deltaY = (SInt32)gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_RELATIVE_Y);
    *buttons = gxmetal_guest_register_read(&gTransport,
                                          GXMETAL_REG_INPUT_BUTTONS);
    return noErr;
}

OSErr GXMetalGetInputEvents(SInt32 *deltaX, SInt32 *deltaY, UInt32 *buttons,
                            UInt32 *buttonDownEdges,
                            UInt32 *buttonUpEdges)
{
    if (deltaX == NULL || deltaY == NULL || buttons == NULL ||
        buttonDownEdges == NULL || buttonUpEdges == NULL) {
        return paramErr;
    }
    if (!GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RELATIVE_INPUT) == 0) {
        return unimpErr;
    }
    *deltaX = (SInt32)gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_RELATIVE_X);
    *deltaY = (SInt32)gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_RELATIVE_Y);
    *buttonDownEdges = gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_BUTTONS_DOWN);
    *buttonUpEdges = gxmetal_guest_register_read(
        &gTransport, GXMETAL_REG_INPUT_BUTTONS_UP);
    *buttons = gxmetal_guest_register_read(&gTransport,
                                          GXMETAL_REG_INPUT_BUTTONS);
    return noErr;
}

static TQABoolean GXMetalGlobalPacket(uint16_t opcode, uint32_t bytes,
                                     GXMetalGuestPacket *packet)
{
    if (!GXMetalTransportAvailable() ||
        !gxmetal_guest_packet_begin(&gTransport, opcode, bytes, 0, packet)) {
        return 0;
    }
    return 1;
}

static TQABoolean GXMetalCommitGlobalPacket(GXMetalGuestPacket *packet)
{
    gxmetal_guest_packet_commit(&gTransport, packet);
    return gxmetal_guest_register_read(&gTransport, GXMETAL_REG_STATUS) ==
           GXMETAL_STATUS_READY;
}

/* Resource uploads all reference the one fixed staging area at
 * GXMETAL_UPLOAD_OFFSET.  The command ring stores only that address, not an
 * inline copy of the pixels, so the guest must not reuse the staging bytes
 * until the host has consumed the upload packet.  Games which create several
 * textures back-to-back (notably Carmageddon II's dynamically composed UI)
 * otherwise race QEMU and give an earlier resource the pixels from a later
 * texture. */
static TQABoolean GXMetalCommitUploadPacket(GXMetalGuestPacket *packet)
{
    uint32_t sequence;

    gxmetal_guest_packet_commit(&gTransport, packet);
    if (!gxmetal_guest_emit_fence(&gTransport, &sequence) ||
        !gxmetal_guest_wait(&gTransport, sequence,
                            GXMETAL_SYNC_SPINS)) {
        return 0;
    }
    return gxmetal_guest_register_read(&gTransport, GXMETAL_REG_STATUS) ==
           GXMETAL_STATUS_READY;
}

static TQAError GXMetalUploadResourceRegion(
    uint32_t resourceID, uint32_t level, const void *source,
    uint32_t sourceRowBytes, uint32_t bytesPerPixel,
    uint32_t left, uint32_t top, uint32_t width, uint32_t height)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint8_t *destination;
    const uint8_t *sourceBytes = (const uint8_t *)source;
    uint32_t uploadRowBytes;
    uint64_t length;
    uint32_t y;

    if (source == NULL || bytesPerPixel == 0 || width == 0 || height == 0 ||
        left > UINT16_MAX || top > UINT16_MAX ||
        width > UINT16_MAX || height > UINT16_MAX) {
        return kQAParamErr;
    }
    uploadRowBytes = width * bytesPerPixel;
    length = (uint64_t)uploadRowBytes * height;
    if (sourceRowBytes < (left + width) * bytesPerPixel ||
        length > GXMETAL_UPLOAD_BYTES ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
        return kQANotSupported;
    }
    destination = gTransport.shared + GXMETAL_UPLOAD_OFFSET;
    for (y = 0; y < height; y++) {
        memcpy(destination + y * uploadRowBytes,
               sourceBytes + (top + y) * sourceRowBytes +
                   left * bytesPerPixel,
               uploadRowBytes);
    }
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                       resourceID);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, level);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                       (uint32_t)length);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET,
                       uploadRowBytes);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload +
                       GXMETAL_UPLOAD_DESTINATION_ORIGIN_OFFSET,
                       left | (top << GXMETAL_UPLOAD_DESTINATION_Y_SHIFT));
    return GXMetalCommitUploadPacket(&packet) ? kQANoErr : kQAError;
}

static TQABoolean GXMetalTextureFormat(TQAImagePixelType pixelType,
                                       uint32_t *format,
                                       uint32_t *bytesPerPixel)
{
    if (pixelType == GXMETAL_ATI_PIXEL_RGB16) {
        /* Carmageddon II's ATI private type-1001 atlases use big-endian
         * ARGB4444. The alpha nibble carries antialiased font and HUD masks. */
        *format = GXMETAL_PIXEL_ATI_ARGB4444;
        *bytesPerPixel = 2;
        return 1;
    }
    if (pixelType == GXMETAL_ATI_PIXEL_RGBA32) {
        /* Apple's ATI OpenGL renderer passes GL_RGBA/UNSIGNED_BYTE images
         * straight through as private type 1006, preserving RGBA byte order.
         * Treating these as public ARGB32 makes a basic glTexImage2D exhaust
         * every GLD fallback and report GL_OUT_OF_MEMORY. */
        *format = GXMETAL_PIXEL_ATI_RGBA8888;
        *bytesPerPixel = 4;
        return 1;
    }
    switch (pixelType) {
    case kQAPixel_Alpha1:
        /* Apple Software RAVE stores one byte per Alpha1 texture texel. RAVE
         * Alpha1 bitmaps are packed separately and expanded before upload.
         * The host reduces each uploaded byte to transparent/opaque while
         * supplying the format's neutral white RGB channels. */
        *format = GXMETAL_PIXEL_ALPHA8;
        *bytesPerPixel = 1;
        return 1;
    case kQAPixel_RGB16:
        *format = GXMETAL_PIXEL_RGB555;
        *bytesPerPixel = 2;
        return 1;
    case kQAPixel_ARGB16:
        *format = GXMETAL_PIXEL_ARGB1555;
        *bytesPerPixel = 2;
        return 1;
    case kQAPixel_ARGB16_4444:
        *format = GXMETAL_PIXEL_ARGB4444;
        *bytesPerPixel = 2;
        return 1;
    case kQAPixel_RGB32:
        *format = GXMETAL_PIXEL_RGB8888;
        *bytesPerPixel = 4;
        return 1;
    case kQAPixel_RGB24:
        *format = GXMETAL_PIXEL_RGB24;
        *bytesPerPixel = 3;
        return 1;
    case kQAPixel_ARGB32:
        *format = GXMETAL_PIXEL_ARGB8888;
        *bytesPerPixel = 4;
        return 1;
    case kQAPixel_I8:
        *format = GXMETAL_PIXEL_INTENSITY8;
        *bytesPerPixel = 1;
        return 1;
    case kQAPixel_AI16_88:
        *format = GXMETAL_PIXEL_ALPHA_INTENSITY88;
        *bytesPerPixel = 2;
        return 1;
    case kQAPixel_RGB8_332:
        *format = GXMETAL_PIXEL_RGB332;
        *bytesPerPixel = 1;
        return 1;
    default:
        return 0;
    }
}

static TQAError GXMetalUploadAlpha1BitmapRegion(
    uint32_t resourceID, const void *source, uint32_t sourceRowBytes,
    uint32_t resourceWidth, uint32_t resourceHeight,
    uint32_t left, uint32_t top,
    uint32_t width, uint32_t height)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint8_t *destination;
    const uint8_t *sourceBytes = (const uint8_t *)source;
    uint64_t length = (uint64_t)width * height;
    uint32_t y;

    if (source == NULL || width == 0 || height == 0 ||
        left > resourceWidth || width > resourceWidth - left ||
        top > resourceHeight || height > resourceHeight - top ||
        !gxmetal_rave_alpha1_bitmap_row_is_valid(resourceWidth,
                                                  sourceRowBytes) ||
        length > GXMETAL_UPLOAD_BYTES || !GXMetalTransportAvailable() ||
        ((left != 0 || top != 0 || width != resourceWidth ||
          height != resourceHeight) &&
         (gTransport.features & GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0)) {
        return kQANotSupported;
    }
    destination = gTransport.shared + GXMETAL_UPLOAD_OFFSET;
    for (y = 0; y < height; y++) {
        if (!gxmetal_rave_expand_alpha1_bitmap_row(
                destination + y * width, width,
                sourceBytes + (top + y) * sourceRowBytes,
                sourceRowBytes, left, width)) {
            return kQAParamErr;
        }
    }
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                       resourceID);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, 0);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                       (uint32_t)length);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_DESTINATION_ORIGIN_OFFSET,
                       left | (top << GXMETAL_UPLOAD_DESTINATION_Y_SHIFT));
    return GXMetalCommitUploadPacket(&packet) ? kQANoErr : kQAError;
}

static TQABoolean GXMetalPaletteFormat(TQAImagePixelType pixelType)
{
    switch (pixelType) {
    case kQAPixel_CL4:
    case kQAPixel_CL8:
    case kQAPixel_ACL16_88:
        return 1;
    default:
        return 0;
    }
}

static uint32_t GXMetalPaletteMinimumRowBytes(TQAImagePixelType pixelType,
                                               uint32_t width)
{
    if (pixelType == kQAPixel_CL4) {
        return (width + 1u) / 2u;
    }
    return pixelType == kQAPixel_ACL16_88 ? width * 2u : width;
}

static uint32_t GXMetalPaletteEntries(TQAImagePixelType pixelType)
{
    return pixelType == kQAPixel_CL4 ? 16u : 256u;
}

static void GXMetalDestroyTextureResource(uint32_t resourceID)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_DESTROY,
                             GXMETAL_RESOURCE_DESTROY_PACKET_BYTES,
                             &packet)) {
        return;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DESTROY_RESOURCE_ID_OFFSET,
                       resourceID);
    (void)GXMetalCommitGlobalPacket(&packet);
}

static void GXMetalFreeTextureSources(TQATexture *texture)
{
    uint32_t level;

    if (texture == NULL) {
        return;
    }
    for (level = 0; level < GXMETAL_MAX_MIP_LEVELS; level++) {
        if (texture->source_pixels[level] != NULL) {
            DisposePtr(texture->source_pixels[level]);
            texture->source_pixels[level] = NULL;
        }
    }
}

static TQAError GXMetalTextureNew(unsigned long flags,
                                  TQAImagePixelType pixelType,
                                  const TQAImage images[],
                                  TQATexture **newTexture)
{
    TQATexture *texture;
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t format = 0;
    uint32_t bytesPerPixel = 0;
    uint32_t levels = 1;
    uint32_t width;
    uint32_t height;
    uint32_t level;
    TQABoolean indexed;

    gDiagnostics.texture_new_count++;
    GXMetalCountPixelType(gDiagnostics.texture_new_attempt_by_type,
                          pixelType);
    gDiagnostics.resource_stage = 1;
    gDiagnostics.last_texture_flags = (uint32_t)flags;
    gDiagnostics.last_texture_pixel_type = (uint32_t)pixelType;
    gDiagnostics.last_texture_error = kQAError;
    gDiagnostics.last_texture_image_width = images != NULL ?
        (uint32_t)images[0].width : 0;
    gDiagnostics.last_texture_image_height = images != NULL ?
        (uint32_t)images[0].height : 0;
    gDiagnostics.last_texture_image_row_bytes = images != NULL ?
        (uint32_t)images[0].rowBytes : 0;
    gDiagnostics.last_texture_image_pixels = images != NULL ?
        (uint32_t)(uintptr_t)images[0].pixmap : 0;
    gDiagnostics.last_texture_output_pointer =
        (uint32_t)(uintptr_t)newTexture;
    if ((uint32_t)pixelType >= UINT32_C(1000)) {
        gDiagnostics.private_texture_attempt_count++;
        GXMetalCountPrivatePixelType(
            gDiagnostics.private_texture_attempt_by_type, pixelType);
        gDiagnostics.private_texture_flags_or |= (uint32_t)flags;
        if ((flags & kQATexture_NoCopy) != 0) {
            gDiagnostics.private_texture_attempt_nocopy_count++;
        }
    }
    if (newTexture == NULL) {
        gDiagnostics.last_texture_error = kQAParamErr;
        return kQAParamErr;
    }
    *newTexture = NULL;
    indexed = GXMetalPaletteFormat(pixelType);
    if (images == NULL || images[0].pixmap == NULL ||
        images[0].width <= 0 || images[0].height <= 0 ||
        images[0].width > (long)GXMETAL_MAX_DIMENSION ||
        images[0].height > (long)GXMETAL_MAX_DIMENSION ||
        images[0].rowBytes <= 0 ||
        (!indexed &&
         !GXMetalTextureFormat(pixelType, &format, &bytesPerPixel)) ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_TEXTURE) == 0 ||
        ((pixelType == kQAPixel_I8 || pixelType == kQAPixel_AI16_88) &&
         (gTransport.features & GXMETAL_FEATURE_INTENSITY_FORMATS) == 0) ||
        (pixelType == kQAPixel_Alpha1 &&
         (gTransport.features & GXMETAL_FEATURE_ALPHA1_FORMAT) == 0) ||
        (pixelType == kQAPixel_RGB8_332 &&
         (gTransport.features & GXMETAL_FEATURE_RGB332_FORMAT) == 0) ||
        (pixelType == kQAPixel_RGB24 &&
         (gTransport.features & GXMETAL_FEATURE_RGB24_FORMAT) == 0)) {
        if (images == NULL || images[0].pixmap == NULL ||
            images[0].width <= 0 || images[0].height <= 0 ||
            images[0].width > (long)GXMETAL_MAX_DIMENSION ||
            images[0].height > (long)GXMETAL_MAX_DIMENSION ||
            images[0].rowBytes <= 0) {
            gDiagnostics.texture_reject_invalid_image_count++;
        } else if (!indexed &&
                   !GXMetalTextureFormat(pixelType, &format,
                                         &bytesPerPixel)) {
            gDiagnostics.texture_reject_unsupported_format_count++;
        } else {
            gDiagnostics.texture_reject_transport_count++;
        }
        gDiagnostics.last_texture_error = kQANotSupported;
        return kQANotSupported;
    }
    if (indexed) {
        /* RAVE binds the CL4/CL8/ACL16_88 palette after creating the texture.
         * Keep a private copy of the packed indices (and ACL alpha) and expose
         * a direct-color host resource which the bind method fills. */
        format = GXMETAL_PIXEL_ARGB8888;
    }
    width = (uint32_t)images[0].width;
    height = (uint32_t)images[0].height;
    gDiagnostics.last_texture_width = width;
    gDiagnostics.last_texture_height = height;
    if (flags & kQATexture_Mipmap) {
        uint32_t w = width;
        uint32_t h = height;
        while (w > 1 || h > 1) {
            w = w > 1 ? w >> 1 : 1;
            h = h > 1 ? h >> 1 : 1;
            levels++;
            if (levels > GXMETAL_MAX_MIP_LEVELS) {
                gDiagnostics.last_texture_error = kQANotSupported;
                return kQANotSupported;
            }
        }
    }
    texture = (TQATexture *)NewPtrClear(sizeof(*texture));
    if (texture == NULL) {
        gDiagnostics.last_texture_error = kQAOutOfMemory;
        return kQAOutOfMemory;
    }
    texture->magic = GXMETAL_TEXTURE_MAGIC;
    texture->resource_id = gNextResourceID++;
    if (gNextResourceID == 0) {
        gNextResourceID = 1;
    }
    texture->levels = levels;
    texture->pixel_format = format;
    texture->width = width;
    texture->height = height;
    texture->source_pixel_type = (uint32_t)pixelType;
    texture->source_flags = (uint32_t)flags;
    gDiagnostics.last_texture_levels = levels;
    gDiagnostics.resource_stage = 2;

    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_CREATE,
                             GXMETAL_RESOURCE_CREATE_PACKET_BYTES,
                             &packet)) {
        DisposePtr((Ptr)texture);
        gDiagnostics.last_texture_error = kQAError;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET,
                       texture->resource_id);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET,
                       indexed ? width * 4 :
                           (uint32_t)images[0].rowBytes);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       format);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_FLAGS_OFFSET,
        (flags & kQATexture_FlipOrigin) ?
            GXMETAL_RESOURCE_FLIP_ORIGIN : 0);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, levels);
    if (!GXMetalCommitGlobalPacket(&packet)) {
        DisposePtr((Ptr)texture);
        gDiagnostics.last_texture_error = kQAError;
        return kQAError;
    }

    for (level = 0; level < levels; level++) {
        uint32_t levelWidth = width >> level;
        uint32_t levelHeight = height >> level;
        uint32_t rowBytes;
        uint64_t length;
        if (levelWidth == 0) {
            levelWidth = 1;
        }
        if (levelHeight == 0) {
            levelHeight = 1;
        }
        if (images[level].pixmap == NULL || images[level].width <= 0 ||
            images[level].height <= 0 ||
            (uint32_t)images[level].width != levelWidth ||
            (uint32_t)images[level].height != levelHeight ||
            images[level].rowBytes <= 0) {
            GXMetalDestroyTextureResource(texture->resource_id);
            GXMetalFreeTextureSources(texture);
            DisposePtr((Ptr)texture);
            gDiagnostics.last_texture_error = kQAParamErr;
            return kQAParamErr;
        }
        rowBytes = (uint32_t)images[level].rowBytes;
        length = (uint64_t)rowBytes * levelHeight;
        if (rowBytes < (indexed ?
                GXMetalPaletteMinimumRowBytes(pixelType, levelWidth) :
                levelWidth * bytesPerPixel) ||
            length > GXMETAL_UPLOAD_BYTES) {
            GXMetalDestroyTextureResource(texture->resource_id);
            GXMetalFreeTextureSources(texture);
            DisposePtr((Ptr)texture);
            gDiagnostics.last_texture_error = kQAOutOfVideoMemory;
            return kQAOutOfVideoMemory;
        }
        if (indexed) {
            texture->source_pixels[level] = NewPtr((Size)length);
            if (texture->source_pixels[level] == NULL) {
                GXMetalDestroyTextureResource(texture->resource_id);
                GXMetalFreeTextureSources(texture);
                DisposePtr((Ptr)texture);
                gDiagnostics.last_texture_error = kQAOutOfMemory;
                return kQAOutOfMemory;
            }
            texture->source_row_bytes[level] = rowBytes;
            memcpy(texture->source_pixels[level], images[level].pixmap,
                   (size_t)length);
            gDiagnostics.resource_stage = 3;
            continue;
        }
        if (pixelType == GXMETAL_ATI_PIXEL_RGB16 &&
            (flags & kQATexture_NoCopy) != 0) {
            /* NoCopy explicitly keeps the caller's pixelmap live.  Retain an
             * owned snapshot for change detection in that case only.  Normal
             * textures are immutable after QATextureNew; scanning all of them
             * once per frame is especially expensive under PPC emulation and
             * can also dereference memory the caller has already released. */
            texture->source_pixels[level] = NewPtr((Size)length);
            if (texture->source_pixels[level] == NULL) {
                GXMetalDestroyTextureResource(texture->resource_id);
                GXMetalFreeTextureSources(texture);
                DisposePtr((Ptr)texture);
                gDiagnostics.last_texture_error = kQAOutOfMemory;
                return kQAOutOfMemory;
            }
            texture->source_row_bytes[level] = rowBytes;
            texture->live_pixels[level] = images[level].pixmap;
            memcpy(texture->source_pixels[level], images[level].pixmap,
                   (size_t)length);
        }
        memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
               texture->source_pixels[level] != NULL ?
                   texture->source_pixels[level] : images[level].pixmap,
               (size_t)length);
        if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                                 GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                                 &packet)) {
            GXMetalDestroyTextureResource(texture->resource_id);
            GXMetalFreeTextureSources(texture);
            DisposePtr((Ptr)texture);
            gDiagnostics.last_texture_error = kQAError;
            return kQAError;
        }
        payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                           texture->resource_id);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, level);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                           GXMETAL_UPLOAD_OFFSET);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                           (uint32_t)length);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET,
                           rowBytes);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET,
                           levelWidth);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET,
                           levelHeight);
        if (!GXMetalCommitUploadPacket(&packet)) {
            GXMetalDestroyTextureResource(texture->resource_id);
            GXMetalFreeTextureSources(texture);
            DisposePtr((Ptr)texture);
            gDiagnostics.last_texture_error = kQAError;
            return kQAError;
        }
        gDiagnostics.resource_stage = 3;
    }
    *newTexture = texture;
    gDiagnostics.resource_stage = 4;
    gDiagnostics.last_texture_error = kQANoErr;
    GXMetalCountPixelType(gDiagnostics.texture_new_success_by_type,
                          pixelType);
    if ((uint32_t)pixelType >= UINT32_C(1000)) {
        gDiagnostics.private_texture_success_count++;
        GXMetalCountPrivatePixelType(
            gDiagnostics.private_texture_success_by_type, pixelType);
        if ((flags & kQATexture_NoCopy) != 0) {
            gDiagnostics.private_texture_success_nocopy_count++;
        }
        if (width <= 64 && height <= 64) {
            gDiagnostics.private_texture_success_small_count++;
        } else {
            gDiagnostics.private_texture_success_large_count++;
        }
    }
    if (gDiagnostics.texture_new_count == 1) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return kQANoErr;
}

static TQAError GXMetalTextureDetach(TQATexture *texture)
{
    return texture != NULL && texture->magic == GXMETAL_TEXTURE_MAGIC ?
        kQANoErr : kQAParamErr;
}

static void GXMetalTextureDelete(TQATexture *texture)
{
    GXMetalDrawState *state;

    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC) {
        return;
    }
    /* RAVE permits a texture to be deleted while a draw context still has
     * it selected. Flush every batch that legitimately references the live
     * resource, then invalidate the guest-side binding before destroying the
     * host resource. Otherwise a private ATI draw can follow a stale pointer
     * and fault the entire command queue with an unbound textured draw. */
    for (state = gDrawStates; state != NULL; state = state->next_state) {
        TQABoolean primary = state->texture == texture ||
            state->texture_resource_id == texture->resource_id;
        TQABoolean secondary = state->secondary_texture == texture ||
            state->secondary_texture_resource_id == texture->resource_id;

        if (!primary && !secondary) {
            continue;
        }
        (void)GXMetalFlushPendingDraws(state);
        if (primary) {
            state->texture = NULL;
            state->texture_resource_id = 0;
        }
        if (secondary) {
            state->secondary_texture = NULL;
            state->secondary_texture_resource_id = 0;
        }
    }
    GXMetalDestroyTextureResource(texture->resource_id);
    GXMetalFreeTextureSources(texture);
    gDiagnostics.texture_delete_count++;
    gDiagnostics.resource_stage = 30;
    texture->magic = 0;
    DisposePtr((Ptr)texture);
}

static TQAError GXMetalColorTableNew(TQAColorTableType tableType,
                                     void *pixelData,
                                     long transparentIndexFlag,
                                     TQAColorTable **newTable)
{
    TQAColorTable *table;
    uint32_t entries;

    if (newTable == NULL) {
        gDiagnostics.last_color_table_error = kQAParamErr;
        return kQAParamErr;
    }
    gDiagnostics.color_table_new_count++;
    gDiagnostics.resource_stage = 10;
    gDiagnostics.last_color_table_type = (uint32_t)tableType;
    gDiagnostics.last_color_table_transparent =
        (uint32_t)(transparentIndexFlag != 0);
    gDiagnostics.last_color_table_error = kQAError;
    *newTable = NULL;
    if (pixelData == NULL ||
        (tableType != kQAColorTable_CL8_RGB32 &&
         tableType != kQAColorTable_CL4_RGB32)) {
        gDiagnostics.last_color_table_error = kQANotSupported;
        return kQANotSupported;
    }
    entries = tableType == kQAColorTable_CL8_RGB32 ? 256 : 16;
    table = (TQAColorTable *)NewPtrClear(sizeof(*table));
    if (table == NULL) {
        gDiagnostics.last_color_table_error = kQAOutOfMemory;
        return kQAOutOfMemory;
    }
    table->magic = GXMETAL_COLOR_TABLE_MAGIC;
    table->entries = entries;
    table->transparent_index_zero = transparentIndexFlag != 0;
    memcpy(table->colors, pixelData, entries * sizeof(table->colors[0]));
    *newTable = table;
    gDiagnostics.resource_stage = 11;
    gDiagnostics.last_color_table_error = kQANoErr;
    if (gDiagnostics.color_table_new_count == 1) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return kQANoErr;
}

static void GXMetalColorTableDelete(TQAColorTable *table)
{
    if (table == NULL || table->magic != GXMETAL_COLOR_TABLE_MAGIC) {
        return;
    }
    table->magic = 0;
    gDiagnostics.color_table_delete_count++;
    gDiagnostics.resource_stage = 31;
    DisposePtr((Ptr)table);
}

static void GXMetalExpandPalettePixel(const uint8_t *sourceRow,
                                      TQAImagePixelType pixelType,
                                      uint32_t x,
                                      const TQAColorTable *table,
                                      uint8_t *destination)
{
    uint8_t alpha;
    uint8_t index;
    uint32_t color;

    if (pixelType == kQAPixel_ACL16_88) {
        /* RAVE's ACL16_88 word is big-endian on PowerPC: alpha first,
         * followed by the eight-bit color-table index. */
        alpha = sourceRow[x * 2u];
        index = sourceRow[x * 2u + 1u];
    } else if (pixelType == kQAPixel_CL4) {
        /* Universal Interfaces specifies the packed order explicitly: the
         * high nibble is the left/even pixel and the low nibble is right. */
        alpha = 255;
        index = (uint8_t)((sourceRow[x / 2u] >>
                           ((x & 1u) == 0 ? 4 : 0)) & 0x0f);
    } else {
        alpha = 255;
        index = sourceRow[x];
    }
    if (table->transparent_index_zero && index == 0) {
        alpha = 0;
    }
    color = table->colors[index];
    destination[0] = alpha;
    destination[1] = (uint8_t)(color >> 16);
    destination[2] = (uint8_t)(color >> 8);
    destination[3] = (uint8_t)color;
}

static TQAError GXMetalTextureBindColorTable(TQATexture *texture,
                                              TQAColorTable *table)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t level;

    gDiagnostics.texture_bind_color_table_count++;
    gDiagnostics.resource_stage = 20;
    gDiagnostics.last_texture_bind_error = kQAError;
    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        table == NULL || table->magic != GXMETAL_COLOR_TABLE_MAGIC) {
        gDiagnostics.last_texture_bind_error = kQAParamErr;
        return kQAParamErr;
    }
    if (!GXMetalPaletteFormat(
            (TQAImagePixelType)texture->source_pixel_type) ||
        table->entries != GXMetalPaletteEntries(
            (TQAImagePixelType)texture->source_pixel_type) ||
        !GXMetalTransportAvailable()) {
        gDiagnostics.last_texture_bind_error = kQANotSupported;
        return kQANotSupported;
    }
    for (level = 0; level < texture->levels; level++) {
        uint32_t width = texture->width >> level;
        uint32_t height = texture->height >> level;
        uint32_t rowBytes;
        uint32_t x;
        uint32_t y;
        uint64_t length;
        const uint8_t *source;
        uint8_t *destination;

        if (width == 0) {
            width = 1;
        }
        if (height == 0) {
            height = 1;
        }
        rowBytes = width * 4;
        length = (uint64_t)rowBytes * height;
        if (texture->source_pixels[level] == NULL ||
            texture->source_row_bytes[level] <
                GXMetalPaletteMinimumRowBytes(
                    (TQAImagePixelType)texture->source_pixel_type, width) ||
            length > GXMETAL_UPLOAD_BYTES) {
            gDiagnostics.last_texture_bind_error = kQAOutOfVideoMemory;
            return kQAOutOfVideoMemory;
        }
        source = (const uint8_t *)texture->source_pixels[level];
        destination = gTransport.shared + GXMETAL_UPLOAD_OFFSET;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint8_t *pixel = destination + y * rowBytes + x * 4;

                GXMetalExpandPalettePixel(
                    source + y * texture->source_row_bytes[level],
                    (TQAImagePixelType)texture->source_pixel_type,
                    x, table, pixel);
            }
        }
        gDiagnostics.resource_stage = 21;
        if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                                 GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                                 &packet)) {
            gDiagnostics.last_texture_bind_error = kQAError;
            return kQAError;
        }
        payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                           texture->resource_id);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, level);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                           GXMETAL_UPLOAD_OFFSET);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                           (uint32_t)length);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET,
                           rowBytes);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
        if (!GXMetalCommitUploadPacket(&packet)) {
            gDiagnostics.last_texture_bind_error = kQAError;
            return kQAError;
        }
        gDiagnostics.resource_stage = 22;
    }
    gDiagnostics.resource_stage = 23;
    gDiagnostics.last_texture_bind_error = kQANoErr;
    texture->palette_bound = 1;
    if (gDiagnostics.texture_bind_color_table_count == 1) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return kQANoErr;
}

static TQAError GXMetalBitmapBindColorTable(TQABitmap *bitmap,
                                             TQAColorTable *table)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t rowBytes;
    uint32_t x;
    uint32_t y;
    uint64_t length;
    const uint8_t *source;
    uint8_t *destination;

    gDiagnostics.bitmap_bind_color_table_count++;
    gDiagnostics.last_bitmap_bind_error = kQAError;
    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC ||
        table == NULL || table->magic != GXMETAL_COLOR_TABLE_MAGIC) {
        gDiagnostics.last_bitmap_bind_error = kQAParamErr;
        return kQAParamErr;
    }
    if (!GXMetalPaletteFormat(
            (TQAImagePixelType)bitmap->source_pixel_type) ||
        bitmap->source_pixels == NULL ||
        table->entries != GXMetalPaletteEntries(
            (TQAImagePixelType)bitmap->source_pixel_type) ||
        !GXMetalTransportAvailable()) {
        gDiagnostics.last_bitmap_bind_error = kQANotSupported;
        return kQANotSupported;
    }
    rowBytes = bitmap->width * 4;
    length = (uint64_t)rowBytes * bitmap->height;
    if (bitmap->source_row_bytes < GXMetalPaletteMinimumRowBytes(
            (TQAImagePixelType)bitmap->source_pixel_type, bitmap->width) ||
        length > GXMETAL_UPLOAD_BYTES) {
        gDiagnostics.last_bitmap_bind_error = kQAOutOfVideoMemory;
        return kQAOutOfVideoMemory;
    }
    source = (const uint8_t *)bitmap->source_pixels;
    destination = gTransport.shared + GXMETAL_UPLOAD_OFFSET;
    for (y = 0; y < bitmap->height; y++) {
        for (x = 0; x < bitmap->width; x++) {
            uint8_t *pixel = destination + y * rowBytes + x * 4;

            GXMetalExpandPalettePixel(
                source + y * bitmap->source_row_bytes,
                (TQAImagePixelType)bitmap->source_pixel_type,
                x, table, pixel);
        }
    }
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        gDiagnostics.last_bitmap_bind_error = kQAError;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                       bitmap->resource_id);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, 0);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                       (uint32_t)length);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, rowBytes);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, bitmap->width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, bitmap->height);
    if (!GXMetalCommitUploadPacket(&packet)) {
        gDiagnostics.last_bitmap_bind_error = kQAError;
        return kQAError;
    }
    bitmap->palette_bound = 1;
    gDiagnostics.last_bitmap_bind_error = kQANoErr;
    return kQANoErr;
}

static TQAError GXMetalBitmapNew(unsigned long flags,
                                 TQAImagePixelType pixelType,
                                 const TQAImage *image,
                                 TQABitmap **newBitmap)
{
    TQABitmap *bitmap;
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t format = 0;
    uint32_t bytesPerPixel = 0;
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint32_t resourceRowBytes;
    uint64_t sourceLength;
    uint64_t uploadLength;
    TQAError error;
    TQABoolean alpha1;
    TQABoolean indexed;

    gDiagnostics.bitmap_new_count++;
    GXMetalCountPixelType(gDiagnostics.bitmap_new_attempt_by_type,
                          pixelType);
    gDiagnostics.last_bitmap_flags = (uint32_t)flags;
    gDiagnostics.last_bitmap_pixel_type = (uint32_t)pixelType;
    gDiagnostics.last_bitmap_width = image != NULL && image->width > 0 ?
        (uint32_t)image->width : 0;
    gDiagnostics.last_bitmap_height = image != NULL && image->height > 0 ?
        (uint32_t)image->height : 0;
    gDiagnostics.last_bitmap_error = kQAError;
    if (newBitmap == NULL) {
        gDiagnostics.last_bitmap_error = kQAParamErr;
        return kQAParamErr;
    }
    *newBitmap = NULL;
    alpha1 = pixelType == kQAPixel_Alpha1;
    indexed = GXMetalPaletteFormat(pixelType);
    if (image == NULL || image->pixmap == NULL || image->width <= 0 ||
        image->height <= 0 ||
        image->width > (long)GXMETAL_MAX_DIMENSION ||
        image->height > (long)GXMETAL_MAX_DIMENSION ||
        image->rowBytes <= 0 ||
        (flags & (kQABitmap_NonRelocatable | kQABitmap_NoCopy)) != 0 ||
        (!indexed &&
         !GXMetalTextureFormat(pixelType, &format, &bytesPerPixel)) ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_TEXTURE) == 0 ||
        ((pixelType == kQAPixel_I8 || pixelType == kQAPixel_AI16_88) &&
         (gTransport.features & GXMETAL_FEATURE_INTENSITY_FORMATS) == 0) ||
        (pixelType == kQAPixel_Alpha1 &&
         (gTransport.features & GXMETAL_FEATURE_ALPHA1_FORMAT) == 0) ||
        (pixelType == kQAPixel_RGB8_332 &&
         (gTransport.features & GXMETAL_FEATURE_RGB332_FORMAT) == 0) ||
        (pixelType == kQAPixel_RGB24 &&
         (gTransport.features & GXMETAL_FEATURE_RGB24_FORMAT) == 0)) {
        gDiagnostics.last_bitmap_error = kQANotSupported;
        return kQANotSupported;
    }
    if (indexed) {
        /* QABitmapBindColorTable supplies the CL4/CL8/ACL16_88 palette after
         * creation. Preserve packed indices and ACL alpha until then. */
        format = GXMETAL_PIXEL_ARGB8888;
    }
    width = (uint32_t)image->width;
    height = (uint32_t)image->height;
    rowBytes = (uint32_t)image->rowBytes;
    resourceRowBytes = indexed ? width * 4u : alpha1 ? width : rowBytes;
    sourceLength = (uint64_t)rowBytes * height;
    uploadLength = (uint64_t)resourceRowBytes * height;
    if ((indexed && rowBytes <
             GXMetalPaletteMinimumRowBytes(pixelType, width)) ||
        (alpha1 && !gxmetal_rave_alpha1_bitmap_row_is_valid(width,
                                                             rowBytes)) ||
        (!indexed && !alpha1 && rowBytes < width * bytesPerPixel) ||
        sourceLength > UINT32_MAX ||
        (alpha1 ? uploadLength : sourceLength) > GXMETAL_UPLOAD_BYTES) {
        gDiagnostics.last_bitmap_error = kQAOutOfVideoMemory;
        return kQAOutOfVideoMemory;
    }
    bitmap = (TQABitmap *)NewPtrClear(sizeof(*bitmap));
    if (bitmap == NULL) {
        gDiagnostics.last_bitmap_error = kQAOutOfMemory;
        return kQAOutOfMemory;
    }
    bitmap->magic = GXMETAL_BITMAP_MAGIC;
    bitmap->resource_id = gNextResourceID++;
    if (gNextResourceID == 0) {
        gNextResourceID = 1;
    }
    bitmap->width = width;
    bitmap->height = height;
    bitmap->pixel_format = format;
    bitmap->source_pixel_type = (uint32_t)pixelType;
    bitmap->source_flags = (uint32_t)flags;

    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_CREATE,
                             GXMETAL_RESOURCE_CREATE_PACKET_BYTES,
                             &packet)) {
        DisposePtr((Ptr)bitmap);
        gDiagnostics.last_bitmap_error = kQAError;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET,
                       bitmap->resource_id);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET,
                       resourceRowBytes);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       format);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_FLAGS_OFFSET,
        (flags & kQABitmap_FlipOrigin) ? GXMETAL_RESOURCE_FLIP_ORIGIN : 0);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    if (!GXMetalCommitGlobalPacket(&packet)) {
        DisposePtr((Ptr)bitmap);
        gDiagnostics.last_bitmap_error = kQAError;
        return kQAError;
    }

    if (indexed) {
        bitmap->source_pixels = NewPtr((Size)sourceLength);
        if (bitmap->source_pixels == NULL) {
            GXMetalDestroyTextureResource(bitmap->resource_id);
            DisposePtr((Ptr)bitmap);
            gDiagnostics.last_bitmap_error = kQAOutOfMemory;
            return kQAOutOfMemory;
        }
        bitmap->source_row_bytes = rowBytes;
        memcpy(bitmap->source_pixels, image->pixmap, (size_t)sourceLength);
        *newBitmap = bitmap;
        gDiagnostics.last_bitmap_error = kQANoErr;
        GXMetalCountPixelType(gDiagnostics.bitmap_new_success_by_type,
                              pixelType);
        return kQANoErr;
    }

    if (alpha1) {
        error = GXMetalUploadAlpha1BitmapRegion(
            bitmap->resource_id, image->pixmap, rowBytes, width, height,
            0, 0, width, height);
        if (error != kQANoErr) {
            GXMetalDestroyTextureResource(bitmap->resource_id);
            DisposePtr((Ptr)bitmap);
            gDiagnostics.last_bitmap_error = error;
            return error;
        }
        *newBitmap = bitmap;
        gDiagnostics.last_bitmap_error = kQANoErr;
        GXMetalCountPixelType(gDiagnostics.bitmap_new_success_by_type,
                              pixelType);
        return kQANoErr;
    }

    memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
           image->pixmap, (size_t)sourceLength);
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr((Ptr)bitmap);
        gDiagnostics.last_bitmap_error = kQAError;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                       bitmap->resource_id);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, 0);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                       (uint32_t)sourceLength);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, rowBytes);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
    if (!GXMetalCommitUploadPacket(&packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr((Ptr)bitmap);
        gDiagnostics.last_bitmap_error = kQAError;
        return kQAError;
    }
    *newBitmap = bitmap;
    gDiagnostics.last_bitmap_error = kQANoErr;
    GXMetalCountPixelType(gDiagnostics.bitmap_new_success_by_type,
                          pixelType);
    return kQANoErr;
}

static TQAError GXMetalBitmapDetach(TQABitmap *bitmap)
{
    return bitmap != NULL && bitmap->magic == GXMETAL_BITMAP_MAGIC ?
        kQANoErr : kQAParamErr;
}

static void GXMetalBitmapDelete(TQABitmap *bitmap)
{
    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC) {
        return;
    }
    GXMetalDestroyTextureResource(bitmap->resource_id);
    if (bitmap->source_pixels != NULL) {
        DisposePtr(bitmap->source_pixels);
        bitmap->source_pixels = NULL;
    }
    bitmap->magic = 0;
    gDiagnostics.bitmap_delete_count++;
    DisposePtr((Ptr)bitmap);
}

static TQAError GXMetalAccessRect(const TQARect *dirtyRect,
                                  uint32_t resourceWidth,
                                  uint32_t resourceHeight,
                                  uint32_t *left, uint32_t *top,
                                  uint32_t *width, uint32_t *height)
{
    if (left == NULL || top == NULL || width == NULL || height == NULL) {
        return kQAParamErr;
    }
    if (dirtyRect == NULL) {
        *left = 0;
        *top = 0;
        *width = resourceWidth;
        *height = resourceHeight;
        return kQANoErr;
    }
    if (dirtyRect->left < 0 || dirtyRect->top < 0 ||
        dirtyRect->right < dirtyRect->left ||
        dirtyRect->bottom < dirtyRect->top ||
        (uint32_t)dirtyRect->right > resourceWidth ||
        (uint32_t)dirtyRect->bottom > resourceHeight) {
        return kQAParamErr;
    }
    *left = (uint32_t)dirtyRect->left;
    *top = (uint32_t)dirtyRect->top;
    *width = (uint32_t)(dirtyRect->right - dirtyRect->left);
    *height = (uint32_t)(dirtyRect->bottom - dirtyRect->top);
    return kQANoErr;
}

static TQAError GXMetalAccessTexture(TQATexture *texture,
                                     long mipmapLevel, long flags,
                                     TQAPixelBuffer *buffer)
{
    uint32_t level;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t bytesPerPixel;
    uint64_t length;

    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        buffer == NULL || mipmapLevel < 0 ||
        (uint32_t)mipmapLevel >= texture->levels ||
        (flags & ~kQANoCopyNeeded) != 0 || texture->access_active != 0 ||
        (texture->source_flags & kQATexture_NoCopy) != 0 ||
        !GXMetalTextureFormat((TQAImagePixelType)texture->source_pixel_type,
                              &format, &bytesPerPixel) ||
        format != texture->pixel_format ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
        return kQANotSupported;
    }
    level = (uint32_t)mipmapLevel;
    width = texture->width >> level;
    height = texture->height >> level;
    if (width == 0) {
        width = 1;
    }
    if (height == 0) {
        height = 1;
    }
    if (texture->source_pixels[level] == NULL) {
        /* kQANoCopyNeeded means the caller will initialize every byte in its
         * dirty region. Allocate backing only for resources that are truly
         * dynamic; retaining a second copy of every game texture exhausts
         * classic applications' fixed heaps during level loads. */
        if ((flags & kQANoCopyNeeded) == 0) {
            return kQANotSupported;
        }
        texture->source_row_bytes[level] = width * bytesPerPixel;
        length = (uint64_t)texture->source_row_bytes[level] * height;
        if (length == 0 || length > GXMETAL_UPLOAD_BYTES) {
            texture->source_row_bytes[level] = 0;
            return kQANotSupported;
        }
        texture->source_pixels[level] = NewPtr((Size)length);
        if (texture->source_pixels[level] == NULL) {
            texture->source_row_bytes[level] = 0;
            return kQAOutOfMemory;
        }
    } else if (texture->source_row_bytes[level] < width * bytesPerPixel) {
        return kQANotSupported;
    }
    buffer->rowBytes = (long)texture->source_row_bytes[level];
    buffer->pixelType = (TQAImagePixelType)texture->source_pixel_type;
    buffer->width = (long)width;
    buffer->height = (long)height;
    buffer->baseAddr = texture->source_pixels[level];
    texture->access_level = level;
    texture->access_active = 1;
    return kQANoErr;
}

static TQAError GXMetalAccessTextureEnd(TQATexture *texture,
                                        const TQARect *dirtyRect)
{
    uint32_t level;
    uint32_t resourceWidth;
    uint32_t resourceHeight;
    uint32_t format;
    uint32_t bytesPerPixel;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    TQAError error;

    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        texture->access_active == 0) {
        return kQAParamErr;
    }
    level = texture->access_level;
    texture->access_active = 0;
    resourceWidth = texture->width >> level;
    resourceHeight = texture->height >> level;
    if (resourceWidth == 0) {
        resourceWidth = 1;
    }
    if (resourceHeight == 0) {
        resourceHeight = 1;
    }
    error = GXMetalAccessRect(dirtyRect, resourceWidth, resourceHeight,
                              &left, &top, &width, &height);
    if (error != kQANoErr || width == 0 || height == 0) {
        return error;
    }
    if (!GXMetalTextureFormat(
            (TQAImagePixelType)texture->source_pixel_type,
            &format, &bytesPerPixel) || format != texture->pixel_format) {
        return kQANotSupported;
    }
    return GXMetalUploadResourceRegion(
        texture->resource_id, level, texture->source_pixels[level],
        texture->source_row_bytes[level], bytesPerPixel,
        left, top, width, height);
}

static TQAError GXMetalAccessBitmap(TQABitmap *bitmap, long flags,
                                    TQAPixelBuffer *buffer)
{
    uint32_t format;
    uint32_t bytesPerPixel;
    uint32_t minimumRowBytes;
    uint64_t length;
    TQABoolean alpha1;

    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC ||
        buffer == NULL || (flags & ~kQANoCopyNeeded) != 0 ||
        bitmap->access_active != 0 ||
        !GXMetalTextureFormat((TQAImagePixelType)bitmap->source_pixel_type,
                              &format, &bytesPerPixel) ||
        format != bitmap->pixel_format ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
        return kQANotSupported;
    }
    alpha1 = bitmap->source_pixel_type == kQAPixel_Alpha1;
    minimumRowBytes = alpha1 ?
        gxmetal_rave_alpha1_bitmap_row_bytes(bitmap->width) :
        bitmap->width * bytesPerPixel;
    if (bitmap->source_pixels == NULL) {
        if ((flags & kQANoCopyNeeded) == 0) {
            return kQANotSupported;
        }
        bitmap->source_row_bytes = minimumRowBytes;
        length = (uint64_t)bitmap->source_row_bytes * bitmap->height;
        if (length == 0 || length > GXMETAL_UPLOAD_BYTES) {
            bitmap->source_row_bytes = 0;
            return kQANotSupported;
        }
        bitmap->source_pixels = NewPtr((Size)length);
        if (bitmap->source_pixels == NULL) {
            bitmap->source_row_bytes = 0;
            return kQAOutOfMemory;
        }
    } else if (bitmap->source_row_bytes < minimumRowBytes) {
        return kQANotSupported;
    }
    buffer->rowBytes = (long)bitmap->source_row_bytes;
    buffer->pixelType = (TQAImagePixelType)bitmap->source_pixel_type;
    buffer->width = (long)bitmap->width;
    buffer->height = (long)bitmap->height;
    buffer->baseAddr = bitmap->source_pixels;
    bitmap->access_active = 1;
    return kQANoErr;
}

static TQAError GXMetalAccessBitmapEnd(TQABitmap *bitmap,
                                       const TQARect *dirtyRect)
{
    uint32_t format;
    uint32_t bytesPerPixel;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    TQAError error;

    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC ||
        bitmap->access_active == 0) {
        return kQAParamErr;
    }
    bitmap->access_active = 0;
    error = GXMetalAccessRect(dirtyRect, bitmap->width, bitmap->height,
                              &left, &top, &width, &height);
    if (error != kQANoErr || width == 0 || height == 0) {
        return error;
    }
    if (!GXMetalTextureFormat((TQAImagePixelType)bitmap->source_pixel_type,
                              &format, &bytesPerPixel) ||
        format != bitmap->pixel_format) {
        return kQANotSupported;
    }
    if (bitmap->source_pixel_type == kQAPixel_Alpha1) {
        return GXMetalUploadAlpha1BitmapRegion(
            bitmap->resource_id, bitmap->source_pixels,
            bitmap->source_row_bytes, bitmap->width, bitmap->height,
            left, top, width, height);
    }
    return GXMetalUploadResourceRegion(
        bitmap->resource_id, 0, bitmap->source_pixels,
        bitmap->source_row_bytes, bytesPerPixel,
        left, top, width, height);
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

    gDiagnostics.display_reject_reason = kGXMetalDisplayAccepted;
    if (device == NULL || rect == NULL || !GXMetalTransportAvailable()) {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayInvalidArguments;
        return 0;
    }
    gDiagnostics.device_type = (uint32_t)device->deviceType;
    gDiagnostics.bounds_left = (uint32_t)rect->left;
    gDiagnostics.bounds_top = (uint32_t)rect->top;
    gDiagnostics.bounds_right = (uint32_t)rect->right;
    gDiagnostics.bounds_bottom = (uint32_t)rect->bottom;
    left = (int32_t)rect->left;
    right = (int32_t)rect->right;
    top = (int32_t)rect->top;
    bottom = (int32_t)rect->bottom;
    if (left < 0 || top < 0 || right <= left || bottom <= top) {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayInvalidRectangle;
        return 0;
    }

    if (device->deviceType == kQADeviceGDevice) {
        GDHandle graphicsDevice = device->device.gDevice;
        PixMapHandle pixmap;
        uint32_t pixelSize;

        if (graphicsDevice == NULL || *graphicsDevice == NULL) {
            gDiagnostics.display_reject_reason =
                kGXMetalDisplayInvalidGDevice;
            return 0;
        }
        gDiagnostics.device_address = (uint32_t)(uintptr_t)graphicsDevice;
        pixmap = (**graphicsDevice).gdPMap;
        if (pixmap == NULL || *pixmap == NULL) {
            gDiagnostics.display_reject_reason =
                kGXMetalDisplayInvalidPixMap;
            return 0;
        }
        gDiagnostics.pixmap_address = (uint32_t)(uintptr_t)pixmap;
        baseAddress = (uintptr_t)GetPixBaseAddr(pixmap);
        *rowBytes = (uint32_t)((**pixmap).rowBytes & 0x3fff);
        pixelSize = (uint32_t)(**pixmap).pixelSize;
        gDiagnostics.pixel_size = pixelSize;
        if (pixelSize == 16) {
            *pixelFormat = GXMETAL_PIXEL_RGB555;
            bytesPerPixel = 2;
        } else if (pixelSize == 32) {
            *pixelFormat = GXMETAL_PIXEL_RGB8888;
            bytesPerPixel = 4;
        } else {
            gDiagnostics.display_reject_reason =
                kGXMetalDisplayUnsupportedPixelSize;
            return 0;
        }
    } else if (device->deviceType == kQADeviceMemory) {
        const TQADeviceMemory *memory = &device->device.memoryDevice;
        baseAddress = (uintptr_t)memory->baseAddr;
        gDiagnostics.device_address = (uint32_t)(uintptr_t)memory;
        gDiagnostics.pixmap_address = 0;
        gDiagnostics.pixel_size = (uint32_t)memory->pixelType;
        if (memory->rowBytes <= 0) {
            gDiagnostics.display_reject_reason =
                kGXMetalDisplayInvalidMemoryRowBytes;
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
            gDiagnostics.display_reject_reason =
                kGXMetalDisplayUnsupportedMemoryPixelType;
            return 0;
        }
    } else {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayUnsupportedDeviceType;
        return 0;
    }

    *width = (uint32_t)(right - left);
    *height = (uint32_t)(bottom - top);
    gDiagnostics.base_address = (uint32_t)baseAddress;
    gDiagnostics.row_bytes = *rowBytes;
    if (*rowBytes < *width * bytesPerPixel) {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayRowBytesTooSmall;
        return 0;
    }
    targetAddress = (uint64_t)baseAddress + (uint64_t)(uint32_t)top *
        *rowBytes + (uint64_t)(uint32_t)left * bytesPerPixel;
    targetEnd = targetAddress + (uint64_t)(*height - 1) * *rowBytes +
        (uint64_t)*width * bytesPerPixel;
    gDiagnostics.target_address = (uint32_t)targetAddress;
    gDiagnostics.target_end = (uint32_t)targetEnd;
    if (targetAddress < gRegistry.framebuffer_address) {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayBeforeFramebuffer;
        return 0;
    }
    if (targetEnd > (uint64_t)gRegistry.framebuffer_address +
                    gRegistry.framebuffer_bytes) {
        gDiagnostics.display_reject_reason =
            kGXMetalDisplayAfterFramebuffer;
        return 0;
    }
    *framebufferOffset = (uint32_t)(targetAddress -
                                    gRegistry.framebuffer_address);
    return 1;
}

static TQABoolean GXMetalBuildRegionClip(RgnHandle region,
                                         const TQARect *drawRect,
                                         GXMetalClipWork **result,
                                         uint32_t *resultCount)
{
    GXMetalClipWork *work;
    Rect bounds = (**region).rgnBBox;
    uint32_t rectCount = 0;
    uint32_t previousCount = 0;
    short y;

    *result = NULL;
    *resultCount = 0;
    work = (GXMetalClipWork *)NewPtrClear(sizeof(*work));
    if (work == NULL) {
        return 0;
    }
    for (y = bounds.top; y < bounds.bottom; y++) {
        uint32_t currentCount = 0;
        short x = bounds.left;

        while (x < bounds.right) {
            Point point;
            short runLeft;
            short runRight;
            uint32_t index = UINT32_MAX;
            uint32_t i;

            point.v = y;
            while (x < bounds.right) {
                point.h = x;
                if (PtInRgn(point, region)) {
                    break;
                }
                x++;
            }
            if (x >= bounds.right) {
                break;
            }
            runLeft = x;
            while (x < bounds.right) {
                point.h = x;
                if (!PtInRgn(point, region)) {
                    break;
                }
                x++;
            }
            runRight = x;
            for (i = 0; i < previousCount; i++) {
                GXMetalClipRect *candidate =
                    &work->rects[work->previous[i]];

                if (candidate->left ==
                        (uint32_t)(runLeft - drawRect->left) &&
                    candidate->right ==
                        (uint32_t)(runRight - drawRect->left) &&
                    candidate->bottom ==
                        (uint32_t)(y - drawRect->top)) {
                    index = work->previous[i];
                    candidate->bottom++;
                    break;
                }
            }
            if (index == UINT32_MAX) {
                GXMetalClipRect *created;

                if (rectCount >= GXMETAL_MAX_CLIP_RECTS) {
                    DisposePtr((Ptr)work);
                    return 0;
                }
                index = rectCount++;
                created = &work->rects[index];
                created->left = (uint32_t)(runLeft - drawRect->left);
                created->top = (uint32_t)(y - drawRect->top);
                created->right = (uint32_t)(runRight - drawRect->left);
                created->bottom = created->top + 1;
            }
            work->current[currentCount++] = index;
        }
        memcpy(work->previous, work->current,
               currentCount * sizeof(work->previous[0]));
        previousCount = currentCount;
    }
    *result = work;
    *resultCount = rectCount;
    return 1;
}

static TQABoolean GXMetalDescribeClip(const TQAClip *clip,
                                      const TQARect *rect,
                                      uint32_t width, uint32_t height,
                                      uint32_t *left, uint32_t *top,
                                      uint32_t *right, uint32_t *bottom,
                                      GXMetalClipWork **regionWork,
                                      uint32_t *regionRectCount)
{
    RgnHandle region;
    RgnHandle drawRegion = NULL;
    RgnHandle intersection = NULL;
    Rect bounds;
    int32_t clippedLeft;
    int32_t clippedTop;
    int32_t clippedRight;
    int32_t clippedBottom;

    *left = 0;
    *top = 0;
    *right = width;
    *bottom = height;
    *regionWork = NULL;
    *regionRectCount = 0;
    if (clip == NULL) {
        return 1;
    }
    if (clip->clipType != kQAClipRgn ||
        (gTransport.features & GXMETAL_FEATURE_RECT_CLIP) == 0) {
        return 0;
    }
    region = clip->clip.clipRgn;
    if (region == NULL || *region == NULL ||
        (**region).rgnSize < sizeof(Region)) {
        return 0;
    }
    if ((**region).rgnSize == sizeof(Region)) {
        bounds = (**region).rgnBBox;
    } else {
        /* QuickDraw may retain a geometrically complex visible region even
         * when the portion covering the RAVE draw rectangle is a plain
         * rectangle.  Intersect first so fullscreen games such as Oni are
         * not rejected merely because the region has unrelated runs outside
         * their draw surface.  A genuinely non-rectangular intersection is
         * still rejected until the transport has a region-list clip feature;
         * approximating it with its bounding box would draw through holes. */
        drawRegion = NewRgn();
        intersection = NewRgn();
        if (drawRegion == NULL || intersection == NULL) {
            if (intersection != NULL) {
                DisposeRgn(intersection);
            }
            if (drawRegion != NULL) {
                DisposeRgn(drawRegion);
            }
            return 0;
        }
        SetRectRgn(drawRegion, (short)rect->left, (short)rect->top,
                   (short)rect->right, (short)rect->bottom);
        SectRgn(region, drawRegion, intersection);
        if (*intersection == NULL) {
            DisposeRgn(intersection);
            DisposeRgn(drawRegion);
            return 0;
        }
        bounds = (**intersection).rgnBBox;
        if ((**intersection).rgnSize != sizeof(Region) &&
            ((gTransport.features & GXMETAL_FEATURE_REGION_CLIP) == 0 ||
             !GXMetalBuildRegionClip(intersection, rect, regionWork,
                                     regionRectCount))) {
            DisposeRgn(intersection);
            DisposeRgn(drawRegion);
            return 0;
        }
        DisposeRgn(intersection);
        DisposeRgn(drawRegion);
    }
    clippedLeft = bounds.left > rect->left ? bounds.left : rect->left;
    clippedTop = bounds.top > rect->top ? bounds.top : rect->top;
    clippedRight = bounds.right < rect->right ? bounds.right : rect->right;
    clippedBottom = bounds.bottom < rect->bottom ?
        bounds.bottom : rect->bottom;
    if (clippedLeft > rect->right) {
        clippedLeft = rect->right;
    }
    if (clippedTop > rect->bottom) {
        clippedTop = rect->bottom;
    }
    if (clippedRight < rect->left) {
        clippedRight = rect->left;
    }
    if (clippedBottom < rect->top) {
        clippedBottom = rect->top;
    }
    if (clippedRight < clippedLeft) {
        clippedRight = clippedLeft;
    }
    if (clippedBottom < clippedTop) {
        clippedBottom = clippedTop;
    }
    *left = (uint32_t)(clippedLeft - rect->left);
    *top = (uint32_t)(clippedTop - rect->top);
    *right = (uint32_t)(clippedRight - rect->left);
    *bottom = (uint32_t)(clippedBottom - rect->top);
    return *left <= width && *right <= width &&
           *top <= height && *bottom <= height;
}

static TQABoolean GXMetalBeginPacket(GXMetalDrawState *state,
                                     uint16_t opcode, uint32_t bytes,
                                     GXMetalGuestPacket *packet)
{
    int began;

    if (state == NULL || state->failed) {
        if (state != NULL) {
            state->failed = 1;
        }
        return 0;
    }
    if (opcode == GXMETAL_OP_DRAW_GOURAUD ||
        opcode == GXMETAL_OP_DRAW_TEXTURED) {
        began = gxmetal_guest_draw_packet_begin(
            state->transport, opcode, bytes, state->context_id, packet);
    } else {
        began = gxmetal_guest_packet_begin(
            state->transport, opcode, bytes, state->context_id, packet);
    }
    if (!began) {
        state->failed = 1;
        return 0;
    }
    return 1;
}

static void GXMetalCommitPacket(GXMetalDrawState *state,
                                GXMetalGuestPacket *packet)
{
    gxmetal_guest_packet_commit(state->transport, packet);
}

static TQABoolean GXMetalEmitState(GXMetalDrawState *state, uint32_t tag,
                                   uint32_t type, uint32_t value)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (!GXMetalFlushPendingDraws(state) ||
        !GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
                            GXMETAL_SET_STATE_PACKET_BYTES, &packet)) {
        return 0;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET, type);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET, value);
    GXMetalCommitPacket(state, &packet);
    return 1;
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

    if (!GXMetalFlushPendingDraws(state) ||
        !GXMetalBeginPacket(state, opcode, 32, &packet)) {
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

static void GXMetalStoreTexture(uint8_t *destination,
                                const TQAVTexture *vertex)
{
    const GXMetalAliasedUInt32 *values =
        (const GXMetalAliasedUInt32 *)(const void *)vertex;
    uint32_t i;

    for (i = 0; i < 16; i++) {
        gxmetal_store_le32(destination + i * sizeof(uint32_t),
                           values[i]);
    }
}

static void GXMetalStoreMultiTexture(uint8_t *destination,
                                     const TQAVTexture *vertex,
                                     const TQAVMultiTexture *secondary)
{
    GXMetalStoreTexture(destination, vertex);
    gxmetal_store_le32(destination + GXMETAL_VERTEX_MULTI_INV_W_OFFSET,
                       GXMetalFloatBits(secondary->invW));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_MULTI_U_OVER_W_OFFSET,
                       GXMetalFloatBits(secondary->uOverW));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_MULTI_V_OVER_W_OFFSET,
                       GXMetalFloatBits(secondary->vOverW));
    gxmetal_store_le32(destination + GXMETAL_VERTEX_MULTI_RESERVED_OFFSET, 0);
}

static void GXMetalStoreATIPrivateTexture(
    uint8_t *destination, const TQAVTexture *vertex,
    const TQATexture *texture, TQABoolean texelCoordinates)
{
    const GXMetalAliasedUInt32 *values =
        (const GXMetalAliasedUInt32 *)(const void *)vertex;
    float alpha = vertex->a;
    float uOverW = vertex->uOverW;
    float vOverW = vertex->vOverW;
    uint32_t i;

    if (texelCoordinates) {
        uOverW /= (float)texture->width;
        vOverW /= (float)texture->height;
    }
    vOverW = vertex->invW + vOverW;
    if (!(alpha >= 0.0f && alpha <= 1.0f)) {
        alpha = 1.0f;
    }
    for (i = 0; i < 7u; ++i) {
        gxmetal_store_le32(destination + i * sizeof(uint32_t), values[i]);
    }
    gxmetal_store_le32(destination + GXMETAL_VERTEX_A_OFFSET,
                       GXMetalFloatBits(alpha));
    gxmetal_store_le32(destination + 8u * sizeof(uint32_t),
                       GXMetalFloatBits(uOverW));
    gxmetal_store_le32(destination + 9u * sizeof(uint32_t),
                       GXMetalFloatBits(vOverW));
    for (i = 10u; i < 16u; ++i) {
        gxmetal_store_le32(destination + i * sizeof(uint32_t), values[i]);
    }
}

static TQABoolean GXMetalRefreshPrivateTexture(TQATexture *texture)
{
    GXMetalGuestPacket packet;
    uint32_t level;

    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        texture->source_pixel_type !=
            (uint32_t)GXMETAL_ATI_PIXEL_RGB16 ||
        (texture->source_flags & kQATexture_NoCopy) == 0) {
        return 1;
    }
    gDiagnostics.private_texture_refresh_check_count++;
    for (level = 0; level < texture->levels; level++) {
        uint32_t width = texture->width >> level;
        uint32_t height = texture->height >> level;
        uint32_t rowBytes = texture->source_row_bytes[level];
        uint64_t length;
        uint8_t *payload;

        if (width == 0) {
            width = 1;
        }
        if (height == 0) {
            height = 1;
        }
        length = (uint64_t)rowBytes * height;
        if (texture->live_pixels[level] == NULL ||
            texture->source_pixels[level] == NULL ||
            rowBytes < width * 2u || length > GXMETAL_UPLOAD_BYTES) {
            gDiagnostics.private_texture_refresh_error_count++;
            return 0;
        }
        if (memcmp(texture->source_pixels[level],
                   texture->live_pixels[level], (size_t)length) == 0) {
            continue;
        }
        memcpy(texture->source_pixels[level], texture->live_pixels[level],
               (size_t)length);
        memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
               texture->source_pixels[level], (size_t)length);
        if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                                 GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                                 &packet)) {
            gDiagnostics.private_texture_refresh_error_count++;
            return 0;
        }
        payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_RESOURCE_ID_OFFSET,
                           texture->resource_id);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LEVEL_OFFSET, level);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET,
                           GXMETAL_UPLOAD_OFFSET);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_LENGTH_OFFSET,
                           (uint32_t)length);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET,
                           rowBytes);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
        gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
        if (!GXMetalCommitUploadPacket(&packet)) {
            gDiagnostics.private_texture_refresh_error_count++;
            return 0;
        }
        gDiagnostics.private_texture_refresh_upload_count++;
    }
    return 1;
}

static TQABoolean GXMetalEmitTexture(GXMetalDrawState *state,
                                     uint32_t primitive,
                                     uint32_t count,
                                     const TQAVTexture *vertices,
                                     const TQAVMultiTexture *secondary,
                                     uint32_t flags,
                                     int32_t atiTexelCoordinatesOverride)
{
    GXMetalGuestPacket packet;
    const TQATexture *texture;
    TQABoolean atiPrivateTexture;
    TQABoolean atiTexelCoordinates;
    TQABoolean hostATIUVTransform;
    uint32_t drawFlags;
    uint32_t packetBytes;
    uint32_t vertexBytes = secondary != NULL ?
        GXMETAL_MULTI_TEXTURE_VERTEX_BYTES : GXMETAL_TEXTURE_VERTEX_BYTES;
    uint8_t *payload;
    uint32_t i;

    if ((secondary != NULL &&
         (state->transport->features &
          GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) == 0) ||
        count == 0 || vertices == NULL ||
        (uint64_t)GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
            (uint64_t)count * vertexBytes >
                GXMETAL_MAX_PACKET_BYTES) {
        state->failed = 1;
        return 0;
    }
    texture = (const TQATexture *)state->texture;
    if (texture != NULL && texture->magic == GXMETAL_TEXTURE_MAGIC &&
        texture->source_pixel_type ==
            (uint32_t)GXMETAL_ATI_PIXEL_RGB16 &&
        (texture->source_flags & kQATexture_NoCopy) != 0 &&
        texture->last_refresh_epoch != gRenderEpoch) {
        if (!GXMetalRefreshPrivateTexture((TQATexture *)texture)) {
            state->failed = 1;
            return 0;
        }
        ((TQATexture *)texture)->last_refresh_epoch = gRenderEpoch;
    }
    packetBytes = GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
                  count * vertexBytes;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_DRAW_TEXTURED, packetBytes,
                            &packet)) {
        return 0;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET, primitive);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, count);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       vertexBytes);
    atiPrivateTexture = texture != NULL &&
                        texture->magic == GXMETAL_TEXTURE_MAGIC &&
                        texture->source_pixel_type ==
                            (uint32_t)GXMETAL_ATI_PIXEL_RGB16 &&
                        texture->width != 0 && texture->height != 0;
    atiTexelCoordinates = atiTexelCoordinatesOverride >= 0 ?
        (TQABoolean)(atiTexelCoordinatesOverride != 0) : 0;
    if (atiPrivateTexture && atiTexelCoordinatesOverride < 0) {
        for (i = 0; i < count; i++) {
            float limit = vertices[i].invW * 2.0f;

            if (vertices[i].invW > 0.0f &&
                (vertices[i].uOverW < -limit ||
                 vertices[i].uOverW > limit ||
                 vertices[i].vOverW < -limit ||
                 vertices[i].vOverW > limit)) {
                atiTexelCoordinates = 1;
                break;
            }
        }
    }
    hostATIUVTransform = secondary == NULL && atiPrivateTexture &&
        primitive == GXMETAL_PRIMITIVE_TRIANGLE &&
        (state->transport->features &
             GXMETAL_FEATURE_ATI_UV_TRANSFORM) != 0;
    drawFlags = flags | (hostATIUVTransform ?
        GXMETAL_DRAW_HOST_ATI_UV : GXMETAL_DRAW_NONE);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET, drawFlags);
    for (i = 0; i < count; i++) {
        const TQAVTexture *vertex = &vertices[i];

        /* ATI's private type-1001 path is intentionally permissive.  The
         * Carmageddon II renderer mixes normalized model UVs with pixel-space
         * sprite UVs on the same texture type, and its V values use a
         * negative, top-origin convention.  Detect texel coordinates per
         * primitive, normalize only those, then translate both forms into
         * the lower-origin RAVE convention consumed by the common shader. */
        if (secondary != NULL) {
            GXMetalStoreMultiTexture(
                payload + GXMETAL_DRAW_VERTICES_OFFSET + i * vertexBytes,
                vertex, &secondary[i]);
        } else if (atiPrivateTexture && !hostATIUVTransform) {
            GXMetalStoreATIPrivateTexture(
                payload + GXMETAL_DRAW_VERTICES_OFFSET +
                    i * vertexBytes,
                vertex, texture, atiTexelCoordinates);
        } else {
            GXMetalStoreTexture(payload + GXMETAL_DRAW_VERTICES_OFFSET +
                                i * vertexBytes, vertex);
        }
    }
    GXMetalCommitPacket(state, &packet);
    return 1;
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

static TQABoolean GXMetalATIUsesTexelCoordinates(
    const GXMetalDrawState *state, const TQAVTexture *vertices,
    uint32_t count)
{
    const TQATexture *texture;
    uint32_t i;

    if (state == NULL || vertices == NULL) {
        return 0;
    }
    texture = (const TQATexture *)state->texture;
    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        texture->source_pixel_type != (uint32_t)GXMETAL_ATI_PIXEL_RGB16 ||
        texture->width == 0 || texture->height == 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        float limit = vertices[i].invW * 2.0f;

        if (vertices[i].invW > 0.0f &&
            (vertices[i].uOverW < -limit ||
             vertices[i].uOverW > limit ||
             vertices[i].vOverW < -limit ||
             vertices[i].vOverW > limit)) {
            return 1;
        }
    }
    return 0;
}

static TQABoolean GXMetalFlushPendingDraws(GXMetalDrawState *state)
{
    uint32_t kind;
    uint32_t count;
    uint32_t flags;
    uint32_t atiTexelCoordinates;

    if (state == NULL || state->failed) {
        return 0;
    }
    if (state->pending_count == 0) {
        return 1;
    }
    kind = state->pending_kind;
    count = state->pending_count;
    flags = state->pending_flags;
    atiTexelCoordinates = state->pending_ati_texel_coordinates;
    state->pending_kind = GXMETAL_DRAW_BATCH_NONE;
    state->pending_count = 0;
    if (kind == GXMETAL_DRAW_BATCH_GOURAUD) {
        return GXMetalEmitGouraud(
            state, GXMETAL_PRIMITIVE_TRIANGLE, count,
            (const TQAVGouraud *)state->pending_vertices, flags);
    }
    if (kind == GXMETAL_DRAW_BATCH_TEXTURE) {
        return GXMetalEmitTexture(
            state, GXMETAL_PRIMITIVE_TRIANGLE, count,
            (const TQAVTexture *)state->pending_vertices, NULL, flags,
            (int32_t)atiTexelCoordinates);
    }
    state->failed = 1;
    return 0;
}

static TQABoolean GXMetalQueueGouraudTriangle(
    GXMetalDrawState *state, const TQAVGouraud *vertices, uint32_t flags)
{
    TQAVGouraud *pending;

    if (state == NULL || vertices == NULL) {
        return 0;
    }
    if (state->pending_vertices == NULL) {
        return GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_TRIANGLE, 3,
                                  vertices, flags);
    }
    if (state->pending_count != 0 &&
        (state->pending_kind != GXMETAL_DRAW_BATCH_GOURAUD ||
         state->pending_flags != flags)) {
        if (!GXMetalFlushPendingDraws(state)) {
            return 0;
        }
    }
    if (state->pending_count + 3u > GXMETAL_DRAW_BATCH_VERTICES &&
        !GXMetalFlushPendingDraws(state)) {
        return 0;
    }
    if (state->pending_count == 0) {
        state->pending_kind = GXMETAL_DRAW_BATCH_GOURAUD;
        state->pending_flags = flags;
    }
    pending = (TQAVGouraud *)state->pending_vertices;
    memcpy(&pending[state->pending_count], vertices, 3u * sizeof(*pending));
    state->pending_count += 3u;
    return 1;
}

static TQABoolean GXMetalQueueTextureTriangle(
    GXMetalDrawState *state, const TQAVTexture *vertices, uint32_t flags,
    int32_t atiTexelCoordinatesOverride)
{
    TQAVTexture *pending;
    uint32_t atiTexelCoordinates;

    if (state == NULL || vertices == NULL) {
        return 0;
    }
    if (state->pending_vertices == NULL) {
        return GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_TRIANGLE, 3,
                                  vertices, NULL, flags, -1);
    }
    atiTexelCoordinates = atiTexelCoordinatesOverride >= 0 ?
        (uint32_t)(atiTexelCoordinatesOverride != 0) :
        GXMetalATIUsesTexelCoordinates(state, vertices, 3);
    if (state->pending_count != 0 &&
        (state->pending_kind != GXMETAL_DRAW_BATCH_TEXTURE ||
         state->pending_flags != flags ||
         state->pending_ati_texel_coordinates != atiTexelCoordinates)) {
        if (!GXMetalFlushPendingDraws(state)) {
            return 0;
        }
    }
    if (state->pending_count + 3u > GXMETAL_DRAW_BATCH_VERTICES &&
        !GXMetalFlushPendingDraws(state)) {
        return 0;
    }
    if (state->pending_count == 0) {
        state->pending_kind = GXMETAL_DRAW_BATCH_TEXTURE;
        state->pending_flags = flags;
        state->pending_ati_texel_coordinates = atiTexelCoordinates;
    }
    pending = (TQAVTexture *)state->pending_vertices;
    memcpy(&pending[state->pending_count], vertices, 3u * sizeof(*pending));
    state->pending_count += 3u;
    return 1;
}

static TQAError GXMetalEmitClear(GXMetalDrawState *state,
                                 const TQARect *rect, uint32_t flags)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (!GXMetalFlushPendingDraws(state) ||
        !GXMetalBeginPacket(state, GXMETAL_OP_CLEAR,
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

    gDiagnostics.draw_method_stage = 100;
    gDiagnostics.set_float_count++;
    gDiagnostics.last_set_float_tag = (uint32_t)tag;
    gDiagnostics.last_set_float_value = GXMetalFloatBits(newValue);
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return;
    }
    if (state->float_state_valid[(uint32_t)tag] &&
        GXMetalFloatBits(state->float_state[(uint32_t)tag]) ==
            GXMetalFloatBits(newValue)) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    state->float_state[(uint32_t)tag] = newValue;
    state->float_state_valid[(uint32_t)tag] = 1;
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
    gDiagnostics.draw_method_stage = 101;
}

static void GXMetalSetInt(TQADrawContext *drawContext, TQATagInt tag,
                          unsigned long newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t nextSamplerMin = 0;
    uint32_t nextSamplerMag = 0;
    int samplerTransition = -1;

    gDiagnostics.draw_method_stage = 110;
    gDiagnostics.set_int_count++;
    gDiagnostics.last_set_int_tag = (uint32_t)tag;
    gDiagnostics.last_set_int_value = (uint32_t)newValue;
    if (state != NULL &&
        (uint32_t)tag == GXMETAL_ATI_PRIVATE_ENABLE_TAG) {
        uint32_t enabled = newValue != 0 ? 1u : 0u;

        if (state->ati_private_enabled == enabled) {
            gDiagnostics.draw_method_stage = 111;
            return;
        }
        if (!GXMetalFlushPendingDraws(state)) {
            return;
        }
        state->ati_private_enabled = enabled;
        if (enabled != 0) {
            state->ati_private_state_synced = 0;
        }
        (void)GXMetalEmitState(state, GXMETAL_STATE_ATI_PRIVATE,
                               GXMETAL_STATE_UINT32, enabled);
        gDiagnostics.draw_method_stage = 111;
        return;
    }
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return;
    }
    if (!gxmetal_rave_int_state_is_accepted((uint32_t)tag,
                                             (uint32_t)newValue)) {
        /* A state setter cannot report an error through the RAVE ABI. Do not
         * forward malformed legacy input as a protocol error: the host would
         * fault the transport and every later draw would fail. */
        gDiagnostics.rejected_int_state_count++;
        gDiagnostics.last_rejected_int_state_tag = (uint32_t)tag;
        gDiagnostics.last_rejected_int_state_value = (uint32_t)newValue;
        GXMetalPersistDiagnostics();
        return;
    }
    if (gxmetal_rave_int_state_requires_write_masks((uint32_t)tag) &&
        (state->transport->features & GXMETAL_FEATURE_WRITE_MASKS) == 0) {
        /* Older hosts accepted these documented RAVE/OpenGL tags but
         * ignored them. Keep mixed-version installs usable while making the
         * unsupported request visible in the persistent diagnostics. */
        gDiagnostics.rejected_int_state_count++;
        gDiagnostics.last_rejected_int_state_tag = (uint32_t)tag;
        gDiagnostics.last_rejected_int_state_value = (uint32_t)newValue;
        GXMetalPersistDiagnostics();
        return;
    }
    if ((uint32_t)tag == kQATag_MultiTextureEnable && newValue > 1u) {
        /* GXMetal advertises one secondary stage. Ignore probes for later
         * stages without poisoning an otherwise valid draw context. */
        return;
    }
    if (gxmetal_rave_sampler_tag_is_secondary((uint32_t)tag)) {
        samplerTransition = gxmetal_rave_sampler_state_transition(
            (uint32_t)tag, (uint32_t)newValue,
            state->effective_secondary_texture_min_filter,
            state->effective_secondary_texture_mag_filter,
            &nextSamplerMin, &nextSamplerMag);
    } else {
        samplerTransition = gxmetal_rave_sampler_state_transition(
            (uint32_t)tag, (uint32_t)newValue,
            state->effective_texture_min_filter,
            state->effective_texture_mag_filter,
            &nextSamplerMin, &nextSamplerMag);
    }
    if (samplerTransition == 0) {
        /* Alias tags have independent getter-visible values even when they
         * resolve to the sampler state that is already active on the host. */
        state->int_state[(uint32_t)tag] = (uint32_t)newValue;
        state->int_state_valid[(uint32_t)tag] = 1;
        return;
    }
    if (samplerTransition < 0 &&
        state->int_state_valid[(uint32_t)tag] &&
        state->int_state[(uint32_t)tag] == (uint32_t)newValue) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    state->int_state[(uint32_t)tag] = (uint32_t)newValue;
    state->int_state_valid[(uint32_t)tag] = 1;
    if (samplerTransition >= 0) {
        if (gxmetal_rave_sampler_tag_is_secondary((uint32_t)tag)) {
            state->effective_secondary_texture_min_filter = nextSamplerMin;
            state->effective_secondary_texture_mag_filter = nextSamplerMag;
        } else {
            state->effective_texture_min_filter = nextSamplerMin;
            state->effective_texture_mag_filter = nextSamplerMag;
        }
    }
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
    gDiagnostics.draw_method_stage = 111;
}

static void GXMetalSetPtr(TQADrawContext *drawContext, TQATagPtr tag,
                          const void *newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQATexture *texture = (const TQATexture *)newValue;
    uint32_t resourceID = 0;
    uint32_t multiTextureStage = state != NULL ?
        state->int_state[kQATag_MultiTextureCurrent] : UINT32_MAX;
    TQABoolean publicMultiTexture = state != NULL &&
        (state->transport->features &
         GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) != 0;
    TQABoolean primaryTextureTag = tag == kQATag_Texture ||
        (state != NULL && state->ati_private_enabled &&
         tag == kQATag_MultiTexture &&
         multiTextureStage == UINT32_MAX);
    TQABoolean secondaryTextureTag =
        state != NULL && (state->ati_private_enabled || publicMultiTexture) &&
        tag == kQATag_MultiTexture && multiTextureStage == 0u;
    TQABoolean clearSecondaryTexture = primaryTextureTag &&
        state != NULL && state->ati_private_enabled;
    GXMetalGuestPacket packet;
    uint8_t *payload;

    gDiagnostics.draw_method_stage = 120;
    gDiagnostics.set_ptr_count++;
    gDiagnostics.last_set_ptr_tag = (uint32_t)tag;
    gDiagnostics.last_set_ptr_value = (uint32_t)(uintptr_t)newValue;
    if (primaryTextureTag) {
        gDiagnostics.set_texture_count++;
        if (texture == NULL) {
            gDiagnostics.set_texture_null_count++;
            gDiagnostics.last_set_texture_magic = 0;
        } else {
            gDiagnostics.last_set_texture_magic = texture->magic;
            if (texture->magic == GXMETAL_TEXTURE_MAGIC) {
                gDiagnostics.set_texture_valid_count++;
            } else {
                gDiagnostics.set_texture_invalid_count++;
            }
        }
    }
    if (state == NULL || (!primaryTextureTag && !secondaryTextureTag) ||
        (texture != NULL && texture->magic != GXMETAL_TEXTURE_MAGIC)) {
        return;
    }
    resourceID = texture != NULL ? texture->resource_id : 0;
    if (primaryTextureTag) {
        /* The classic Memory Manager may reuse a deleted TQATexture's
         * address immediately.  Pointer equality alone would then suppress
         * the new resource binding even though the host cleared the old ID. */
        if (state->texture == newValue &&
            state->texture_resource_id == resourceID &&
            (!clearSecondaryTexture ||
             (state->secondary_texture == NULL &&
              state->secondary_texture_resource_id == 0))) {
            return;
        }
    } else if (state->secondary_texture == newValue &&
               state->secondary_texture_resource_id == resourceID) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    if (primaryTextureTag) {
        state->texture = newValue;
        state->texture_resource_id = resourceID;
        /* ATI only rebinds stage zero for a draw that actually uses a second
         * texture.  Do not let the preceding draw's lightmap leak across a
         * later primary-texture change. */
        if (clearSecondaryTexture) {
            state->secondary_texture = NULL;
            state->secondary_texture_resource_id = 0;
        }
    } else {
        if (publicMultiTexture && !state->ati_private_enabled) {
            /* The host's legacy ATI path starts enabled. Synchronize the
             * public RAVE default (zero stages) before binding stage zero. */
            (void)GXMetalEmitState(
                state, GXMETAL_STATE_MULTI_TEXTURE_ENABLE,
                GXMETAL_STATE_UINT32,
                state->int_state[kQATag_MultiTextureEnable]);
        }
        state->secondary_texture = newValue;
        state->secondary_texture_resource_id = resourceID;
    }
    if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
                            GXMETAL_SET_STATE_PACKET_BYTES, &packet)) {
        return;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET,
                       primaryTextureTag ? (uint32_t)kQATag_Texture :
                                           GXMETAL_STATE_MULTI_TEXTURE);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET,
                       resourceID);
    GXMetalCommitPacket(state, &packet);
    if (clearSecondaryTexture) {
        (void)GXMetalEmitState(state, GXMETAL_STATE_MULTI_TEXTURE,
                               GXMETAL_STATE_RESOURCE, 0);
    }
    gDiagnostics.draw_method_stage = 121;
}

static float GXMetalGetFloat(const TQADrawContext *drawContext,
                             TQATagFloat tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    gDiagnostics.draw_method_stage = 130;
    gDiagnostics.get_float_count++;
    gDiagnostics.last_get_float_tag = (uint32_t)tag;
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        gDiagnostics.last_get_float_value = 0;
        return 0.0f;
    }
    gDiagnostics.last_get_float_value =
        GXMetalFloatBits(state->float_state[(uint32_t)tag]);
    gDiagnostics.draw_method_stage = 131;
    return state->float_state[(uint32_t)tag];
}

static unsigned long GXMetalGetInt(const TQADrawContext *drawContext,
                                   TQATagInt tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t compatibilityValue;
    gDiagnostics.draw_method_stage = 140;
    gDiagnostics.get_int_count++;
    gDiagnostics.last_get_int_tag = (uint32_t)tag;
    if (state != NULL &&
        (uint32_t)tag == GXMETAL_ATI_PRIVATE_ENABLE_TAG) {
        gDiagnostics.last_get_int_value = state->ati_private_enabled;
        gDiagnostics.draw_method_stage = 141;
        return state->ati_private_enabled;
    }
    if (state != NULL && gxmetal_ati_private_int(
            (uint32_t)tag, &compatibilityValue)) {
        gDiagnostics.last_get_int_value = compatibilityValue;
        gDiagnostics.draw_method_stage = 141;
        return compatibilityValue;
    }
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        gDiagnostics.last_get_int_value = 0;
        gDiagnostics.draw_method_stage = 141;
        return 0;
    }
    gDiagnostics.last_get_int_value = state->int_state[(uint32_t)tag];
    gDiagnostics.draw_method_stage = 141;
    return state->int_state[(uint32_t)tag];
}

typedef TQAError (*GXMetalATIPrivateMethod)(uint32_t arg0, uint32_t arg1,
                                            uint32_t arg2, uint32_t arg3,
                                            uint32_t arg4, uint32_t arg5,
                                            uint32_t arg6, uint32_t arg7);

static TQABoolean GXMetalDiagnosticMemoryRangeIsReadable(uint32_t address,
                                                         uint32_t byteCount)
{
    return address >= UINT32_C(0x00100000) &&
        address < UINT32_C(0x20000000) &&
        byteCount <= UINT32_C(0x20000000) - address;
}

/* OpenGLRendererATI geometry hooks receive its renderer object, not the
 * public RAVE context. The draw-context pointer is the third 32-bit word in
 * that object. Prefer it so multiple OpenGL contexts cannot accidentally
 * submit through whichever RAVE context happened to be used most recently. */
static GXMetalDrawState *GXMetalATIPrivateGetState(uint32_t rendererAddress)
{
    GXMetalDrawState *state = NULL;

    gDiagnostics.ati_private_context_resolve_count++;
    gDiagnostics.ati_private_context_last_renderer = rendererAddress;
    if (GXMetalDiagnosticMemoryRangeIsReadable(
            rendererAddress, 3u * (uint32_t)sizeof(uint32_t))) {
        const uint32_t *rendererWords =
            (const uint32_t *)(uintptr_t)rendererAddress;
        const uint32_t drawContextAddress = rendererWords[2];

        gDiagnostics.ati_private_context_last_draw_context =
            drawContextAddress;
        state = GXMetalGetState(
            (const TQADrawContext *)(uintptr_t)drawContextAddress);
    }
    if (state == NULL) {
        gDiagnostics.ati_private_context_fallback_count++;
        gDiagnostics.ati_private_context_last_draw_context =
            (uint32_t)(uintptr_t)gLastDrawContext;
        state = GXMetalGetState(gLastDrawContext);
    }
    return state;
}

static void GXMetalTraceATIPrivateMethod(
    uint32_t method, uint32_t arg0, uint32_t arg1, uint32_t arg2,
    uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6,
    uint32_t arg7)
{
    const uint32_t argumentValues[8] = {
        arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7
    };

    gDiagnostics.ati_private_call_count++;
    if (method < GXMETAL_DIAGNOSTIC_ATI_METHODS) {
        uint32_t argumentIndex;

        gDiagnostics.ati_private_method_call_count[method]++;
        if (method == 28u || method == 29u) {
            uint32_t argumentBase = (method - 28u) * 8u;

            for (argumentIndex = 0; argumentIndex < 8u;
                 ++argumentIndex) {
                gDiagnostics.ati_private_method28_29_last_args[
                    argumentBase + argumentIndex] =
                        argumentValues[argumentIndex];
            }
        }
    }
    gDiagnostics.ati_private_method_mask_low |=
        gxmetal_ati_private_method_mask(method, 0);
    gDiagnostics.ati_private_method_mask_high |=
        gxmetal_ati_private_method_mask(method, 1);
    gDiagnostics.ati_private_last_method = method;
    gDiagnostics.ati_private_last_arg0 = arg0;
    gDiagnostics.ati_private_last_arg1 = arg1;
    gDiagnostics.ati_private_last_arg2 = arg2;
    gDiagnostics.ati_private_last_arg3 = arg3;
    gDiagnostics.ati_private_last_arg4 = arg4;
    gDiagnostics.ati_private_last_arg5 = arg5;
    gDiagnostics.ati_private_last_arg6 = arg6;
    gDiagnostics.ati_private_last_arg7 = arg7;
    if (method >= UINT32_C(41) && method <= UINT32_C(60)) {
        uint32_t geometryIndex = method - UINT32_C(41);
        uint32_t argumentIndex = geometryIndex * UINT32_C(8);

        gDiagnostics.ati_private_geometry_call_count[geometryIndex]++;
        gDiagnostics.ati_private_geometry_current_frame_call_count[
            geometryIndex]++;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex] = arg0;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 1] = arg1;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 2] = arg2;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 3] = arg3;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 4] = arg4;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 5] = arg5;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 6] = arg6;
        gDiagnostics.ati_private_geometry_last_args[argumentIndex + 7] = arg7;
    }
    if (method >= UINT32_C(41) && method <= UINT32_C(44)) {
        uint32_t fillIndex = method - UINT32_C(41);

        gDiagnostics.ati_private_fill41_44_call_count[fillIndex]++;
        gDiagnostics.ati_private_fill41_44_last_vertex_count[fillIndex] =
            arg1;
        gDiagnostics.ati_private_fill41_44_last_primitive[fillIndex] = arg2;
        if (arg2 < UINT32_C(32)) {
            gDiagnostics.ati_private_fill41_44_primitive_mask[fillIndex] |=
                UINT32_C(1) << arg2;
        }
        if (arg1 >
            gDiagnostics.ati_private_fill41_44_max_vertex_count[fillIndex]) {
            gDiagnostics.ati_private_fill41_44_max_vertex_count[fillIndex] =
                arg1;
        }
    }
    if (method == 20) {
        const uint32_t *stateWords = (const uint32_t *)(uintptr_t)arg2;
        uint32_t wordIndex;

        gDiagnostics.ati_private_state20_call_count++;
        gDiagnostics.ati_private_state20_dirty_mask_or |= arg1;
        gDiagnostics.ati_private_state20_arg0 = arg0;
        gDiagnostics.ati_private_state20_arg1 = arg1;
        gDiagnostics.ati_private_state20_arg2 = arg2;
        gDiagnostics.ati_private_state20_arg3 = arg3;
        gDiagnostics.ati_private_state20_arg4 = arg4;
        gDiagnostics.ati_private_state20_arg5 = arg5;
        gDiagnostics.ati_private_state20_arg6 = arg6;
        gDiagnostics.ati_private_state20_arg7 = arg7;
        gDiagnostics.ati_private_state20_snapshot_valid = 0;
        if (GXMetalDiagnosticMemoryRangeIsReadable(
                arg2, sizeof(gDiagnostics.ati_private_state20_words))) {
            for (wordIndex = 0;
                 wordIndex < GXMETAL_DIAGNOSTIC_ATI_STATE20_WORDS;
                 ++wordIndex) {
                gDiagnostics.ati_private_state20_words[wordIndex] =
                    stateWords[wordIndex];
            }
            if (gDiagnostics.ati_private_state20_call_count > 1u &&
                gDiagnostics.ati_private_state20_word53_last !=
                    stateWords[53]) {
                gDiagnostics.ati_private_state20_word53_change_count++;
            }
            gDiagnostics.ati_private_state20_word53_last = stateWords[53];
            gDiagnostics.ati_private_state20_word53_or |= stateWords[53];
            if (stateWords[53] != 0) {
                gDiagnostics
                    .ati_private_state20_word53_nonzero_call_count++;
                if (gDiagnostics
                        .ati_private_state20_word53_first_nonzero_frame == 0) {
                    gDiagnostics
                        .ati_private_state20_word53_first_nonzero_frame =
                            gDiagnostics.ati_private_frame_sequence;
                }
            }
            gDiagnostics.ati_private_state20_snapshot_valid = 1;
        }
    }
    if (method == 21) {
        const uint32_t *arg1Words = (const uint32_t *)(uintptr_t)arg1;
        const uint32_t *arg4Words = (const uint32_t *)(uintptr_t)arg4;
        uint32_t wordIndex;

        gDiagnostics.ati_private_pixel21_call_count++;
        gDiagnostics.ati_private_pixel21_arg0 = arg0;
        gDiagnostics.ati_private_pixel21_arg1 = arg1;
        gDiagnostics.ati_private_pixel21_arg2 = arg2;
        gDiagnostics.ati_private_pixel21_arg3 = arg3;
        gDiagnostics.ati_private_pixel21_arg4 = arg4;
        gDiagnostics.ati_private_pixel21_arg5 = arg5;
        gDiagnostics.ati_private_pixel21_arg6 = arg6;
        gDiagnostics.ati_private_pixel21_arg7 = arg7;
        gDiagnostics.ati_private_pixel21_arg1_snapshot_valid = 0;
        gDiagnostics.ati_private_pixel21_arg4_snapshot_valid = 0;
        if (GXMetalDiagnosticMemoryRangeIsReadable(
                arg1, sizeof(gDiagnostics.ati_private_pixel21_arg1_words))) {
            for (wordIndex = 0;
                 wordIndex < GXMETAL_DIAGNOSTIC_ATI_PIXEL21_WORDS;
                 ++wordIndex) {
                gDiagnostics.ati_private_pixel21_arg1_words[wordIndex] =
                    arg1Words[wordIndex];
            }
            gDiagnostics.ati_private_pixel21_arg1_snapshot_valid = 1;
        }
        if (GXMetalDiagnosticMemoryRangeIsReadable(
                arg4, sizeof(gDiagnostics.ati_private_pixel21_arg4_words))) {
            for (wordIndex = 0;
                 wordIndex < GXMETAL_DIAGNOSTIC_ATI_PIXEL21_WORDS;
                 ++wordIndex) {
                gDiagnostics.ati_private_pixel21_arg4_words[wordIndex] =
                    arg4Words[wordIndex];
            }
            gDiagnostics.ati_private_pixel21_arg4_snapshot_valid = 1;
        }
    }
    if (method == 27) {
        const uint32_t *rectWords = (const uint32_t *)(uintptr_t)arg1;
        uint32_t wordIndex;

        gDiagnostics.ati_private_clear27_call_count++;
        gDiagnostics.ati_private_clear27_arg0 = arg0;
        gDiagnostics.ati_private_clear27_arg1 = arg1;
        gDiagnostics.ati_private_clear27_arg2 = arg2;
        gDiagnostics.ati_private_clear27_arg3 = arg3;
        gDiagnostics.ati_private_clear27_arg4 = arg4;
        gDiagnostics.ati_private_clear27_arg5 = arg5;
        gDiagnostics.ati_private_clear27_arg6 = arg6;
        gDiagnostics.ati_private_clear27_arg7 = arg7;
        gDiagnostics.ati_private_clear27_rect_snapshot_valid = 0;
        if (GXMetalDiagnosticMemoryRangeIsReadable(
                arg1, sizeof(gDiagnostics.ati_private_clear27_rect_words))) {
            for (wordIndex = 0; wordIndex < 4; ++wordIndex) {
                gDiagnostics.ati_private_clear27_rect_words[wordIndex] =
                    rectWords[wordIndex];
            }
            gDiagnostics.ati_private_clear27_rect_snapshot_valid = 1;
        }
    }
    if (method == 47) {
        static const uint8_t wordOffsets[10] = {
            4, 5, 6, 7, 12, 13, 14, 15, 16, 17
        };
        const uint32_t *vertices =
            (const uint32_t *)(uintptr_t)arg1;
        uint32_t vertexIndex;
        uint32_t wordIndex;

        gDiagnostics.ati_private_draw47_call_count++;
        gDiagnostics.ati_private_draw47_arg0 = arg0;
        gDiagnostics.ati_private_draw47_arg1 = arg1;
        gDiagnostics.ati_private_draw47_arg2 = arg2;
        gDiagnostics.ati_private_draw47_arg3 = arg3;
        gDiagnostics.ati_private_draw47_arg4 = arg4;
        gDiagnostics.ati_private_draw47_arg5 = arg5;
        gDiagnostics.ati_private_draw47_arg6 = arg6;
        gDiagnostics.ati_private_draw47_arg7 = arg7;
        gDiagnostics.ati_private_draw47_vertex_snapshot_valid = 0;
        if (arg2 >= 3 &&
            GXMetalDiagnosticMemoryRangeIsReadable(
                arg1, 3u * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES)) {
            for (vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
                for (wordIndex = 0; wordIndex < 10; ++wordIndex) {
                    gDiagnostics.ati_private_draw47_vertex_words[
                        vertexIndex * 10u + wordIndex] =
                            vertices[vertexIndex * 32u +
                                     wordOffsets[wordIndex]];
                }
            }
            gDiagnostics.ati_private_draw47_vertex_snapshot_valid = 1;
        }
    }
    if (method == 48) {
        static const uint8_t wordOffsets[13] = {
            4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 20, 21, 23
        };
        const uint32_t *vertices =
            (const uint32_t *)(uintptr_t)arg1;
        uint32_t vertexIndex;
        uint32_t wordIndex;
        uint32_t vertexCountBucket = arg2;

        if (vertexCountBucket >=
            GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS) {
            vertexCountBucket =
                GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS - 1u;
        }
        gDiagnostics.ati_private_draw48_vertex_count_buckets[
            vertexCountBucket]++;
        if (arg2 > gDiagnostics.ati_private_draw48_max_vertex_count) {
            gDiagnostics.ati_private_draw48_max_vertex_count = arg2;
        }
        if (arg2 < 3u || (arg2 % 3u) != 0) {
            gDiagnostics
                .ati_private_draw48_invalid_vertex_count_call_count++;
        }
        gDiagnostics.ati_private_draw48_vertex_snapshot_valid_mask = 0;
        if (arg2 >= 3 &&
            GXMetalDiagnosticMemoryRangeIsReadable(
                arg1, 3u * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES)) {
            for (vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
                for (wordIndex = 0;
                     wordIndex <
                         GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX;
                     ++wordIndex) {
                    gDiagnostics.ati_private_draw48_vertex_words[
                        vertexIndex *
                            GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX +
                        wordIndex] =
                            vertices[vertexIndex * 32u +
                                     wordOffsets[wordIndex]];
                }
                gDiagnostics.ati_private_draw48_vertex_snapshot_valid_mask |=
                    UINT32_C(1) << vertexIndex;
            }
        }
    }
    if (method == 49) {
        uint32_t fanRimAddress;
        uint32_t fanRimCount;

        gDiagnostics.ati_private_draw49_call_count++;
        if (!gxmetal_ati_private_fan_layout(
                arg1, arg2, arg3, &fanRimAddress, &fanRimCount)) {
            fanRimCount = 0;
        }
        gDiagnostics.ati_private_draw49_last_vertex_count =
            fanRimCount == 0 ? 0 : fanRimCount + UINT32_C(1);
        gDiagnostics.ati_private_draw49_last_primitive =
            GXMETAL_ATI_GL_TRIANGLE_FAN;
        gDiagnostics.ati_private_draw49_primitive_mask |=
            UINT32_C(1) << GXMETAL_ATI_GL_TRIANGLE_FAN;
        if (gDiagnostics.ati_private_draw49_last_vertex_count >
            gDiagnostics.ati_private_draw49_max_vertex_count) {
            gDiagnostics.ati_private_draw49_max_vertex_count =
                gDiagnostics.ati_private_draw49_last_vertex_count;
        }
    }
    if (method == 50) {
        static const uint8_t wordOffsets[13] = {
            4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 20, 21, 23
        };
        uint32_t vertexAddresses[3];
        uint32_t vertexIndex;
        uint32_t wordIndex;
        uint32_t fanRimAddress;
        uint32_t fanRimCount;

        gDiagnostics.ati_private_draw50_call_count++;
        if (!gxmetal_ati_private_fan_layout(
                arg1, arg2, arg3, &fanRimAddress, &fanRimCount)) {
            fanRimAddress = 0;
            fanRimCount = 0;
        }
        vertexAddresses[0] = arg1;
        vertexAddresses[1] = fanRimAddress;
        vertexAddresses[2] = fanRimAddress <= UINT32_MAX -
            GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES ?
                fanRimAddress + GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES : 0;
        gDiagnostics.ati_private_draw50_last_vertex_count =
            fanRimCount == 0 ? 0 : fanRimCount + UINT32_C(1);
        gDiagnostics.ati_private_draw50_last_primitive =
            GXMETAL_ATI_GL_TRIANGLE_FAN;
        gDiagnostics.ati_private_draw50_primitive_mask |=
            UINT32_C(1) << GXMETAL_ATI_GL_TRIANGLE_FAN;
        if (gDiagnostics.ati_private_draw50_last_vertex_count >
            gDiagnostics.ati_private_draw50_max_vertex_count) {
            gDiagnostics.ati_private_draw50_max_vertex_count =
                gDiagnostics.ati_private_draw50_last_vertex_count;
        }
        gDiagnostics.ati_private_draw50_vertex_snapshot_valid_mask = 0;
        for (vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
            const uint32_t *vertexWords =
                (const uint32_t *)(uintptr_t)vertexAddresses[vertexIndex];

            if (!GXMetalDiagnosticMemoryRangeIsReadable(
                    vertexAddresses[vertexIndex],
                    GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES)) {
                continue;
            }
            for (wordIndex = 0;
                 wordIndex <
                     GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX;
                 ++wordIndex) {
                gDiagnostics.ati_private_draw50_vertex_words[
                    vertexIndex *
                        GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX +
                    wordIndex] =
                        vertexWords[wordOffsets[wordIndex]];
            }
            gDiagnostics.ati_private_draw50_vertex_snapshot_valid_mask |=
                UINT32_C(1) << vertexIndex;
        }
    }
    if (method == 51) {
        gDiagnostics.ati_private_draw51_call_count++;
        gDiagnostics.ati_private_draw51_last_vertex_count = arg2;
        gDiagnostics.ati_private_draw51_last_primitive =
            GXMETAL_ATI_GL_TRIANGLE_STRIP;
        gDiagnostics.ati_private_draw51_primitive_mask |=
            UINT32_C(1) << GXMETAL_ATI_GL_TRIANGLE_STRIP;
        if (arg2 > gDiagnostics.ati_private_draw51_max_vertex_count) {
            gDiagnostics.ati_private_draw51_max_vertex_count = arg2;
        }
    }
    if (method == 52) {
        gDiagnostics.ati_private_draw52_call_count++;
        gDiagnostics.ati_private_draw52_last_vertex_count = arg2;
        gDiagnostics.ati_private_draw52_last_primitive =
            GXMETAL_ATI_GL_TRIANGLE_STRIP;
        gDiagnostics.ati_private_draw52_primitive_mask |=
            UINT32_C(1) << GXMETAL_ATI_GL_TRIANGLE_STRIP;
        if (arg2 > gDiagnostics.ati_private_draw52_max_vertex_count) {
            gDiagnostics.ati_private_draw52_max_vertex_count = arg2;
        }
    }
    if (method == 60) {
        static const uint8_t wordOffsets[13] = {
            4, 5, 6, 7, 12, 13, 14, 15, 16, 17, 20, 21, 23
        };
        const uint32_t *vertexPointers =
            (const uint32_t *)(uintptr_t)arg1;
        uint32_t captureCount = arg2;
        uint32_t vertexCountBucket = arg2;
        uint32_t vertexIndex;
        uint32_t wordIndex;

        if (captureCount > GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES) {
            captureCount = GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES;
        }
        if (vertexCountBucket >=
            GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS) {
            vertexCountBucket =
                GXMETAL_DIAGNOSTIC_ATI_VERTEX_COUNT_BUCKETS - 1u;
        }
        gDiagnostics.ati_private_draw60_vertex_count_buckets[
            vertexCountBucket]++;
        gDiagnostics.ati_private_draw60_clip_marker_or |= arg4;
        if (arg4 < GXMETAL_DIAGNOSTIC_ATI_CLIP_MARKER_VALUES) {
            gDiagnostics.ati_private_draw60_clip_marker_low_value_count[
                arg4]++;
        } else {
            gDiagnostics
                .ati_private_draw60_clip_marker_high_value_call_count++;
        }
        gDiagnostics.ati_private_draw60_pointer_snapshot_valid = 0;
        if (arg4 == 0) {
            gDiagnostics.ati_private_draw60_zero_clip_marker_call_count++;
        } else {
            uint32_t argumentIndex;

            gDiagnostics.ati_private_draw60_nonzero_clip_marker_call_count++;
            for (argumentIndex = 0; argumentIndex < 8u; ++argumentIndex) {
                gDiagnostics.ati_private_draw60_nonzero_last_args[
                    argumentIndex] = argumentValues[argumentIndex];
            }
            gDiagnostics
                .ati_private_draw60_nonzero_pointer_snapshot_valid = 0;
            gDiagnostics.ati_private_draw60_nonzero_pointer_count = arg2;
            gDiagnostics
                .ati_private_draw60_nonzero_vertex_snapshot_valid_mask = 0;
            for (vertexIndex = 0;
                 vertexIndex < GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES;
                 ++vertexIndex) {
                gDiagnostics.ati_private_draw60_nonzero_vertex_pointers[
                    vertexIndex] = 0;
            }
        }
        gDiagnostics.ati_private_draw60_pointer_count = arg2;
        gDiagnostics.ati_private_draw60_vertex_snapshot_valid_mask = 0;
        for (vertexIndex = 0;
             vertexIndex < GXMETAL_DIAGNOSTIC_ATI_CAPTURE_VERTICES;
             ++vertexIndex) {
            gDiagnostics.ati_private_draw60_vertex_pointers[vertexIndex] = 0;
        }
        if (captureCount != 0 &&
            GXMetalDiagnosticMemoryRangeIsReadable(
                arg1, captureCount * (uint32_t)sizeof(*vertexPointers))) {
            gDiagnostics.ati_private_draw60_pointer_snapshot_valid = 1;
            for (vertexIndex = 0; vertexIndex < captureCount;
                 ++vertexIndex) {
                const uint32_t vertexAddress = vertexPointers[vertexIndex];
                const uint32_t *vertexWords =
                    (const uint32_t *)(uintptr_t)vertexAddress;

                gDiagnostics.ati_private_draw60_vertex_pointers[vertexIndex] =
                    vertexAddress;
                if (arg4 != 0) {
                    gDiagnostics
                        .ati_private_draw60_nonzero_vertex_pointers[
                            vertexIndex] = vertexAddress;
                }
                if (!GXMetalDiagnosticMemoryRangeIsReadable(
                        vertexAddress,
                        GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES)) {
                    continue;
                }
                for (wordIndex = 0;
                     wordIndex <
                         GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX;
                     ++wordIndex) {
                    gDiagnostics.ati_private_draw60_vertex_words[
                        vertexIndex *
                            GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX +
                        wordIndex] =
                            vertexWords[wordOffsets[wordIndex]];
                    if (arg4 != 0) {
                        gDiagnostics
                            .ati_private_draw60_nonzero_vertex_words[
                                vertexIndex *
                                    GXMETAL_DIAGNOSTIC_ATI_CAPTURE_WORDS_PER_VERTEX +
                                wordIndex] =
                                    vertexWords[wordOffsets[wordIndex]];
                    }
                }
                gDiagnostics.ati_private_draw60_vertex_snapshot_valid_mask |=
                    UINT32_C(1) << vertexIndex;
                if (arg4 != 0) {
                    gDiagnostics
                        .ati_private_draw60_nonzero_vertex_snapshot_valid_mask |=
                            UINT32_C(1) << vertexIndex;
                }
            }
            if (arg4 != 0) {
                gDiagnostics
                    .ati_private_draw60_nonzero_pointer_snapshot_valid = 1;
            }
        }
    }
}

/* Carmageddon II uses the two-function ATI RAVE compatibility table returned
 * by private pointer tag 1021. Reverse engineering ATI 3D Accelerator 5.0.4
 * confirms that entries zero and one invoke its color-buffer and depth-buffer
 * clear paths. Older games use these before RAVE 1.6's public clear methods.
 * The second argument is reserved by the ATI wrappers and Carmageddon passes
 * zero, so both operations cover the complete draw context. */
static TQAError GXMetalATIPrivateMethod0(uint32_t arg0, uint32_t arg1,
                                         uint32_t arg2, uint32_t arg3,
                                         uint32_t arg4, uint32_t arg5,
                                         uint32_t arg6, uint32_t arg7)
{
    const TQADrawContext *drawContext =
        (const TQADrawContext *)(uintptr_t)arg0;
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalTraceATIPrivateMethod(0, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    (void)arg5; (void)arg6; (void)arg7;
    gDiagnostics.draw_method_stage = 270;
    if (state == NULL) {
        return kQAParamErr;
    }
    return GXMetalEmitClear(state, NULL, GXMETAL_CLEAR_COLOR);
}

static TQAError GXMetalATIPrivateMethod1(uint32_t arg0, uint32_t arg1,
                                         uint32_t arg2, uint32_t arg3,
                                         uint32_t arg4, uint32_t arg5,
                                         uint32_t arg6, uint32_t arg7)
{
    const TQADrawContext *drawContext =
        (const TQADrawContext *)(uintptr_t)arg0;
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalTraceATIPrivateMethod(1, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    (void)arg5; (void)arg6; (void)arg7;
    gDiagnostics.draw_method_stage = 271;
    if (state == NULL) {
        return kQAParamErr;
    }
    if ((state->context_flags & GXMETAL_CONTEXT_DEPTH_MASK) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitClear(state, NULL, GXMETAL_CLEAR_DEPTH);
}

/* OpenGLRendererATI retains this private table after context creation and
 * older ATI engines expose considerably more than the two clear hooks used
 * by Carmageddon II. Keep a conservatively sized table so an optional ATI
 * hook never walks into unrelated data. Unidentified entries intentionally
 * report success without touching guest or host state; known OpenGL geometry
 * callbacks are implemented separately below. */
#define GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(index)                             \
    static TQAError GXMetalATIPrivateMethod##index(                         \
        uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,         \
        uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7)         \
    {                                                                       \
        GXMetalTraceATIPrivateMethod(index, arg0, arg1, arg2, arg3, arg4,   \
                                     arg5, arg6, arg7);                     \
        (void)arg0; (void)arg1; (void)arg2; (void)arg3;                     \
        (void)arg4; (void)arg5; (void)arg6; (void)arg7;                     \
        return kQANoErr;                                                    \
    }

GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(2)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(3)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(4)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(5)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(6)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(7)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(8)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(9)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(10)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(11)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(12)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(13)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(14)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(15)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(17)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(18)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(19)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(21)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(22)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(25)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(30)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(31)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(32)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(33)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(34)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(35)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(36)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(37)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(38)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(39)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(40)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(45)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(46)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(61)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(62)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(63)

/* Slot 20 synchronizes OpenGLRendererATI's derived render state before clear
 * and draw callbacks. The dirty mask and field layout below match the ATI
 * fallback path that emits equivalent public RAVE setters. Force the first
 * snapshot because GXMetal's public RAVE defaults intentionally differ from
 * OpenGL defaults (notably blend and depth testing). */
static TQAError GXMetalATIPrivateMethod20(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    const uint32_t *stateWords = (const uint32_t *)(uintptr_t)arg2;
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);
    TQADrawContext *drawContext = (TQADrawContext *)(uintptr_t)arg0;
    uint32_t dirtyMask = arg1;
    uint32_t colorBits[4];
    uint32_t depthFunction;
    uint32_t depthWrite;
    uint32_t alphaFunction;
    uint32_t alphaReferenceBits;
    uint32_t blend;
    uint32_t blendSource;
    uint32_t blendDestination;
    uint32_t fogMode;
    uint32_t fogBits[7];
    uint32_t channelMask;
    const uint32_t *textureParameters;
    uint32_t textureParameterAddresses[2];
    uint32_t textureWrapU;
    uint32_t textureWrapV;
    uint32_t textureMinFilter;
    uint32_t textureMagFilter;
    uint32_t textureBorderBits[4];
    uint32_t textureStage;
    double clearDepth;

    GXMetalTraceATIPrivateMethod(20, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    if (state == NULL || state->failed ||
        !GXMetalDiagnosticMemoryRangeIsReadable(
            arg2, GXMETAL_ATI_PRIVATE_STATE_WORDS * sizeof(*stateWords))) {
        return kQANoErr;
    }
    if (state->ati_private_state_synced == 0) {
        dirtyMask |= GXMETAL_ATI_DIRTY_IMPLEMENTED;
        state->ati_private_state_synced = 1;
    }
    /* ATI marks depth-write state dirty whenever blend changes because its
     * hardware-private encoding combined both bits. Preserve that dependency
     * even though GXMetal exposes the two settings independently. */
    if ((dirtyMask & GXMETAL_ATI_DIRTY_BLEND) != 0) {
        dirtyMask |= GXMETAL_ATI_DIRTY_DEPTH_WRITE;
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_TEXTURE) != 0) {
        dirtyMask |= GXMETAL_ATI_DIRTY_FOG;
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_CLEAR) != 0 &&
        gxmetal_ati_private_clear_color_bits(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, colorBits)) {
        /* glClearDepth is stored as the native PowerPC double at byte 0x30.
         * memcpy avoids imposing stricter alignment on the derived block. */
        memcpy(&clearDepth,
               &stateWords[GXMETAL_ATI_PRIVATE_CLEAR_DEPTH_WORD],
               sizeof(clearDepth));
        GXMetalSetFloat(drawContext, kQATag_ColorBG_r,
                        GXMetalBitsFloat(colorBits[0]));
        GXMetalSetFloat(drawContext, kQATag_ColorBG_g,
                        GXMetalBitsFloat(colorBits[1]));
        GXMetalSetFloat(drawContext, kQATag_ColorBG_b,
                        GXMetalBitsFloat(colorBits[2]));
        GXMetalSetFloat(drawContext, kQATag_ColorBG_a,
                        GXMetalBitsFloat(colorBits[3]));
        GXMetalSetFloat(drawContext, kQATagGL_DepthBG, (float)clearDepth);
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_ALPHA_TEST) != 0 &&
        gxmetal_ati_private_alpha_state(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, &alphaFunction,
            &alphaReferenceBits)) {
        GXMetalSetFloat(drawContext, kQATag_AlphaTestRef,
                        GXMetalBitsFloat(alphaReferenceBits));
        GXMetalSetInt(drawContext, kQATag_AlphaTestFunc, alphaFunction);
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_BLEND) != 0 &&
        gxmetal_ati_private_blend_state(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, &blend,
            &blendSource, &blendDestination)) {
        GXMetalSetInt(drawContext, kQATagGL_BlendSrc, blendSource);
        GXMetalSetInt(drawContext, kQATagGL_BlendDst, blendDestination);
        GXMetalSetInt(drawContext, kQATag_Blend, blend);
    }
    if ((dirtyMask & (GXMETAL_ATI_DIRTY_DEPTH_TEST |
                      GXMETAL_ATI_DIRTY_DEPTH_WRITE)) != 0 &&
        gxmetal_ati_private_depth_state(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, &depthFunction,
            &depthWrite)) {
        if ((dirtyMask & GXMETAL_ATI_DIRTY_DEPTH_TEST) != 0) {
            GXMetalSetInt(drawContext, kQATag_ZFunction, depthFunction);
        }
        if ((dirtyMask & GXMETAL_ATI_DIRTY_DEPTH_WRITE) != 0) {
            GXMetalSetInt(drawContext, kQATag_ZBufferMask, depthWrite);
        }
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_FOG) != 0 &&
        gxmetal_ati_private_fog_state(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, &fogMode,
            fogBits)) {
        GXMetalSetFloat(drawContext, kQATag_FogColor_r,
                        GXMetalBitsFloat(fogBits[0]));
        GXMetalSetFloat(drawContext, kQATag_FogColor_g,
                        GXMetalBitsFloat(fogBits[1]));
        GXMetalSetFloat(drawContext, kQATag_FogColor_b,
                        GXMetalBitsFloat(fogBits[2]));
        GXMetalSetFloat(drawContext, kQATag_FogColor_a,
                        GXMetalBitsFloat(fogBits[3]));
        GXMetalSetFloat(drawContext, kQATag_FogDensity,
                        GXMetalBitsFloat(fogBits[4]));
        GXMetalSetFloat(drawContext, kQATag_FogStart,
                        GXMetalBitsFloat(fogBits[5]));
        GXMetalSetFloat(drawContext, kQATag_FogEnd,
                        GXMetalBitsFloat(fogBits[6]));
        GXMetalSetInt(drawContext, kQATag_FogMode, fogMode);
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_COLOR_MASK) != 0 &&
        gxmetal_ati_private_channel_mask(
            stateWords, GXMETAL_ATI_PRIVATE_STATE_WORDS, &channelMask)) {
        GXMetalSetInt(drawContext, kQATag_ChannelMask, channelMask);
    }
    if ((dirtyMask & GXMETAL_ATI_DIRTY_TEXTURE) != 0 && arg3 != 0 &&
        GXMetalDiagnosticMemoryRangeIsReadable(
            arg3, sizeof(textureParameterAddresses))) {
        memcpy(textureParameterAddresses, (const void *)(uintptr_t)arg3,
               sizeof(textureParameterAddresses));
        for (textureStage = 0; textureStage < UINT32_C(2);
             ++textureStage) {
            if (textureParameterAddresses[textureStage] == 0 ||
                !GXMetalDiagnosticMemoryRangeIsReadable(
                    textureParameterAddresses[textureStage],
                    GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS *
                        sizeof(uint32_t))) {
                continue;
            }
            textureParameters = (const uint32_t *)(uintptr_t)
                textureParameterAddresses[textureStage];
            if (!gxmetal_ati_private_texture_parameters(
                    textureParameters,
                    GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS,
                    &textureWrapU, &textureWrapV, &textureMinFilter,
                    &textureMagFilter, textureBorderBits)) {
                continue;
            }
            if (textureStage == 0) {
                GXMetalSetInt(drawContext, kQATagGL_TextureWrapU,
                              textureWrapU);
                GXMetalSetInt(drawContext, kQATagGL_TextureWrapV,
                              textureWrapV);
                GXMetalSetInt(drawContext, kQATagGL_TextureMinFilter,
                              textureMinFilter);
                GXMetalSetInt(drawContext, kQATagGL_TextureMagFilter,
                              textureMagFilter);
                GXMetalSetFloat(drawContext, kQATagGL_TextureBorder_r,
                                GXMetalBitsFloat(textureBorderBits[0]));
                GXMetalSetFloat(drawContext, kQATagGL_TextureBorder_g,
                                GXMetalBitsFloat(textureBorderBits[1]));
                GXMetalSetFloat(drawContext, kQATagGL_TextureBorder_b,
                                GXMetalBitsFloat(textureBorderBits[2]));
                GXMetalSetFloat(drawContext, kQATagGL_TextureBorder_a,
                                GXMetalBitsFloat(textureBorderBits[3]));
            } else {
                GXMetalSetInt(drawContext, kQATag_MultiTextureWrapU,
                              textureWrapU);
                GXMetalSetInt(drawContext, kQATag_MultiTextureWrapV,
                              textureWrapV);
                GXMetalSetInt(drawContext, kQATag_MultiTextureMinFilter,
                              textureMinFilter);
                GXMetalSetInt(drawContext, kQATag_MultiTextureMagFilter,
                              textureMagFilter);
                GXMetalSetFloat(drawContext, kQATag_MultiTextureBorder_r,
                                GXMetalBitsFloat(textureBorderBits[0]));
                GXMetalSetFloat(drawContext, kQATag_MultiTextureBorder_g,
                                GXMetalBitsFloat(textureBorderBits[1]));
                GXMetalSetFloat(drawContext, kQATag_MultiTextureBorder_b,
                                GXMetalBitsFloat(textureBorderBits[2]));
                GXMetalSetFloat(drawContext, kQATag_MultiTextureBorder_a,
                                GXMetalBitsFloat(textureBorderBits[3]));
            }
        }
    }
    return kQANoErr;
}

/* glrATIClear calls slot 27 with the draw context and an optional four-word
 * rectangle. The clear color itself was synchronized through slot 20. */
static TQAError GXMetalATIPrivateMethod27(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    const TQARect *rect = (const TQARect *)(uintptr_t)arg1;
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);

    GXMetalTraceATIPrivateMethod(27, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    if (state == NULL || state->failed ||
        (arg1 != 0 && !GXMetalDiagnosticMemoryRangeIsReadable(
                          arg1, sizeof(*rect)))) {
        return kQANoErr;
    }
    return GXMetalEmitClear(state, rect, GXMETAL_CLEAR_COLOR);
}

/* The matching ATI clear hook handles GL_DEPTH_BUFFER_BIT. Its arguments
 * mirror slot 27: a public RAVE draw context and an optional damage rect. */
static TQAError GXMetalATIPrivateMethod28(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    const TQARect *rect = (const TQARect *)(uintptr_t)arg1;
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);

    GXMetalTraceATIPrivateMethod(28, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    if (state == NULL || state->failed ||
        (arg1 != 0 && !GXMetalDiagnosticMemoryRangeIsReadable(
                          arg1, sizeof(*rect)))) {
        return kQANoErr;
    }
    if ((state->context_flags & GXMETAL_CONTEXT_DEPTH_MASK) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitClear(state, rect, GXMETAL_CLEAR_DEPTH);
}

/* glrSwapBuffers closes the acquire/release bracket through slot 24 and then
 * invokes slot 29 with the public RAVE context plus optional damage rect. */
static TQAError GXMetalATIPrivateMethod29(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    const TQARect *rect = (const TQARect *)(uintptr_t)arg1;
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);
    uint32_t geometryIndex;

    GXMetalTraceATIPrivateMethod(29, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    if (state == NULL || state->failed ||
        (arg1 != 0 && !GXMetalDiagnosticMemoryRangeIsReadable(
                          arg1, sizeof(*rect)))) {
        return kQANoErr;
    }
    if (GXMetalEmitRect(state, GXMETAL_OP_PRESENT, rect) != kQANoErr) {
        return kQANoErr;
    }
    for (geometryIndex = 0;
         geometryIndex < GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS;
         ++geometryIndex) {
        uint32_t frameCallCount =
            gDiagnostics.ati_private_geometry_current_frame_call_count[
                geometryIndex];

        if (frameCallCount >
            gDiagnostics.ati_private_geometry_max_frame_call_count[
                geometryIndex]) {
            gDiagnostics.ati_private_geometry_max_frame_call_count[
                geometryIndex] = frameCallCount;
            gDiagnostics.ati_private_geometry_max_frame_call_frame[
                geometryIndex] =
                    gDiagnostics.ati_private_frame_sequence + UINT32_C(1);
        }
        gDiagnostics.ati_private_geometry_current_frame_call_count[
            geometryIndex] = 0;
    }
    gDiagnostics.ati_private_frame_sequence++;
    if ((gDiagnostics.ati_private_frame_sequence & UINT32_C(31)) == 0) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return kQANoErr;
}

/* glrFinish invokes slot 26 with the draw context and no other semantic
 * arguments. Use GXMetal's public sync path so queued private triangles are
 * submitted and its host fence has completed before OpenGL continues. */
static TQAError GXMetalATIPrivateMethod26(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(26, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    (void)arg5; (void)arg6; (void)arg7;
    return GXMetalSync((const TQADrawContext *)(uintptr_t)arg0);
}

/* OpenGLRendererATI passes the selected primary RAVE texture in register r9
 * (the seventh argument) to private hook 16 before submitting geometry. */
static TQAError GXMetalATIPrivateMethod16(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalATIPrivateGetState(arg0);
    const TQATexture *texture = (const TQATexture *)(uintptr_t)arg6;
    GXMetalTraceATIPrivateMethod(16, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg7;

    if (state == NULL || state->failed ||
        !GXMetalDiagnosticMemoryRangeIsReadable(arg6, sizeof(*texture)) ||
        texture->magic != GXMETAL_TEXTURE_MAGIC) {
        return kQANoErr;
    }
    if (state->texture != texture ||
        state->texture_resource_id != texture->resource_id) {
        if (!GXMetalFlushPendingDraws(state)) {
            return kQANoErr;
        }
        state->texture = texture;
        state->texture_resource_id = texture->resource_id;
        state->secondary_texture = NULL;
        state->secondary_texture_resource_id = 0;
        (void)GXMetalEmitState(state, kQATag_Texture,
                               GXMETAL_STATE_RESOURCE,
                               texture->resource_id);
        (void)GXMetalEmitState(state, GXMETAL_STATE_MULTI_TEXTURE,
                               GXMETAL_STATE_RESOURCE, 0);
    }
    gDiagnostics.current_texture_resource_id = texture->resource_id;
    gDiagnostics.current_texture_flags = texture->source_flags;
    gDiagnostics.current_texture_pixel_type = texture->source_pixel_type;
    gDiagnostics.current_texture_width = texture->width;
    gDiagnostics.current_texture_height = texture->height;
    return kQANoErr;
}

/* OpenGLRendererATI brackets complete frames with private hooks 23 and 24.
 * Quake submits many transformed batches between each matched pair. */
static TQAError GXMetalATIPrivateMethod23(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);
    GXMetalTraceATIPrivateMethod(23, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;

    if (state == NULL || state->failed) {
        return kQANoErr;
    }
    if (state->ati_private_frame_started) {
        if (GXMetalEmitRect(state, GXMETAL_OP_END_FRAME, NULL) !=
            kQANoErr) {
            return kQANoErr;
        }
        state->ati_private_frame_started = 0;
        state->ati_private_frame_has_draws = 0;
    }
    if (GXMetalEmitRect(state, GXMETAL_OP_BEGIN_FRAME, NULL) != kQANoErr) {
        return kQANoErr;
    }
    state->ati_private_frame_started = 1;
    state->ati_private_frame_has_draws = 0;
    return kQANoErr;
}

static TQAError GXMetalATIPrivateMethod24(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalGetState(
        (const TQADrawContext *)(uintptr_t)arg0);
    GXMetalTraceATIPrivateMethod(24, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;

    if (state != NULL && !state->failed && state->ati_private_frame_started) {
        if (GXMetalEmitRect(state, GXMETAL_OP_END_FRAME, NULL) !=
            kQANoErr) {
            return kQANoErr;
        }
        state->ati_private_frame_started = 0;
        state->ati_private_frame_has_draws = 0;
    }
    return kQANoErr;
}

static TQABoolean GXMetalATIPrivateConvertVertex(uint32_t address,
                                                 TQAVTexture *destination);

static TQABoolean GXMetalATIPrivatePrepareDraw(GXMetalDrawState *state)
{
    if (state == NULL || state->failed) {
        return false;
    }
    /* OpenGLRendererATI does not publish OpenGL's default texture
     * environment through the public RAVE state API. */
    if (!state->int_state_valid[kQATag_TextureOp]) {
        if (!GXMetalFlushPendingDraws(state)) {
            return false;
        }
        state->int_state[kQATag_TextureOp] = kQATextureOp_Modulate;
        state->int_state_valid[kQATag_TextureOp] = 1;
        if (!GXMetalEmitState(state, kQATag_TextureOp,
                              GXMETAL_STATE_UINT32,
                              kQATextureOp_Modulate)) {
            return false;
        }
    }
    if (!state->ati_private_frame_started) {
        if (GXMetalEmitRect(state, GXMETAL_OP_BEGIN_FRAME, NULL) !=
            kQANoErr) {
            return false;
        }
        state->ati_private_frame_started = 1;
        state->ati_private_frame_has_draws = 0;
    }
    return true;
}

enum GXMetalATIPrivateGeometryAnomaly {
    kGXMetalATIPrivateGeometryNonfinite = UINT32_C(1) << 0,
    kGXMetalATIPrivateGeometryNonpositiveInvW = UINT32_C(1) << 1,
    kGXMetalATIPrivateGeometryExtremeXY = UINT32_C(1) << 2,
    kGXMetalATIPrivateGeometryExtremeZ = UINT32_C(1) << 3
};

static uint32_t GXMetalATIPrivateGeometryIndex(uint32_t sourceMethod)
{
    if (sourceMethod < 41u || sourceMethod > 60u) {
        return GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS;
    }
    return sourceMethod - 41u;
}

static void GXMetalATIPrivateRecordInputReject(uint32_t sourceMethod)
{
    uint32_t geometryIndex = GXMetalATIPrivateGeometryIndex(sourceMethod);

    if (geometryIndex < GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS) {
        gDiagnostics.ati_private_geometry_input_rejected_call_count[
            geometryIndex]++;
    }
}

static uint32_t GXMetalATIPrivateVertexAnomalyFlags(
    const TQAVTexture *vertex)
{
    const float values[13] = {
        vertex->r, vertex->g, vertex->b, vertex->a,
        vertex->x, vertex->y, vertex->z, vertex->invW,
        vertex->uOverW, vertex->vOverW,
        vertex->ks_r, vertex->ks_g, vertex->ks_b
    };
    uint32_t flags = 0;
    uint32_t valueIndex;

    for (valueIndex = 0; valueIndex < 13u; ++valueIndex) {
        if ((GXMetalFloatBits(values[valueIndex]) &
             UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) {
            flags |= kGXMetalATIPrivateGeometryNonfinite;
        }
    }
    if (vertex->invW <= 0.0f) {
        flags |= kGXMetalATIPrivateGeometryNonpositiveInvW;
    }
    if (vertex->x < -8192.0f || vertex->x > 8192.0f ||
        vertex->y < -8192.0f || vertex->y > 8192.0f) {
        flags |= kGXMetalATIPrivateGeometryExtremeXY;
    }
    if (vertex->z < -8.0f || vertex->z > 8.0f) {
        flags |= kGXMetalATIPrivateGeometryExtremeZ;
    }
    return flags;
}

static void GXMetalATIPrivateRecordTriangleAnomaly(
    uint32_t sourceMethod, const TQAVTexture triangle[3],
    const uint32_t vertexAddresses[3])
{
    uint32_t flags = 0;
    uint32_t vertexIndex;

    for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
        flags |= GXMetalATIPrivateVertexAnomalyFlags(&triangle[vertexIndex]);
    }
    if (flags == 0) {
        return;
    }
    gDiagnostics.ati_private_geometry_anomaly_count++;
    gDiagnostics.ati_private_geometry_anomaly_flags_or |= flags;
    if (gDiagnostics.ati_private_geometry_first_anomaly_method == 0) {
        uint32_t wordIndex;

        gDiagnostics.ati_private_geometry_first_anomaly_method =
            sourceMethod;
        gDiagnostics.ati_private_geometry_first_anomaly_frame =
            gDiagnostics.ati_private_frame_sequence;
        gDiagnostics.ati_private_geometry_first_anomaly_flags = flags;
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            const float values[13] = {
                triangle[vertexIndex].r, triangle[vertexIndex].g,
                triangle[vertexIndex].b, triangle[vertexIndex].a,
                triangle[vertexIndex].x, triangle[vertexIndex].y,
                triangle[vertexIndex].z, triangle[vertexIndex].invW,
                triangle[vertexIndex].uOverW,
                triangle[vertexIndex].vOverW,
                triangle[vertexIndex].ks_r,
                triangle[vertexIndex].ks_g,
                triangle[vertexIndex].ks_b
            };

            gDiagnostics
                .ati_private_geometry_first_anomaly_vertex_addresses[
                    vertexIndex] = vertexAddresses[vertexIndex];
            for (wordIndex = 0; wordIndex < 13u; ++wordIndex) {
                gDiagnostics
                    .ati_private_geometry_first_anomaly_vertex_words[
                        vertexIndex * 13u + wordIndex] =
                            GXMetalFloatBits(values[wordIndex]);
            }
        }
    }
}

/* A sudden per-frame callback burst is a stronger transition marker than a
 * large cumulative method count. Preserve the first converted triangle at
 * that boundary so a later persistence checkpoint cannot overwrite the
 * primitive that selected a high-volume GLD path. This is diagnostic only:
 * the triangle is still queued normally. */
static void GXMetalATIPrivateRecordFirstGeometryBurst(
    const GXMetalDrawState *state, uint32_t sourceMethod,
    const TQAVTexture triangle[3], const uint32_t vertexAddresses[3])
{
    uint32_t geometryIndex = GXMetalATIPrivateGeometryIndex(sourceMethod);
    uint32_t vertexIndex;

    if (state == NULL ||
        geometryIndex >= GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS ||
        gDiagnostics.ati_private_geometry_first_burst_method != 0 ||
        gDiagnostics.ati_private_geometry_current_frame_call_count[
            geometryIndex] <
                GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_BURST_CALLS) {
        return;
    }
    gDiagnostics.ati_private_geometry_first_burst_method = sourceMethod;
    gDiagnostics.ati_private_geometry_first_burst_frame =
        gDiagnostics.ati_private_frame_sequence + UINT32_C(1);
    gDiagnostics.ati_private_geometry_first_burst_call_count =
        gDiagnostics.ati_private_geometry_current_frame_call_count[
            geometryIndex];
    gDiagnostics.ati_private_geometry_first_burst_viewport_width =
        state->width;
    gDiagnostics.ati_private_geometry_first_burst_viewport_height =
        state->height;
    for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
        const float values[13] = {
            triangle[vertexIndex].r, triangle[vertexIndex].g,
            triangle[vertexIndex].b, triangle[vertexIndex].a,
            triangle[vertexIndex].x, triangle[vertexIndex].y,
            triangle[vertexIndex].z, triangle[vertexIndex].invW,
            triangle[vertexIndex].uOverW,
            triangle[vertexIndex].vOverW,
            triangle[vertexIndex].ks_r,
            triangle[vertexIndex].ks_g,
            triangle[vertexIndex].ks_b
        };
        uint32_t wordIndex;

        gDiagnostics.ati_private_geometry_first_burst_vertex_addresses[
            vertexIndex] = vertexAddresses[vertexIndex];
        for (wordIndex = 0; wordIndex < 13u; ++wordIndex) {
            gDiagnostics.ati_private_geometry_first_burst_vertex_words[
                vertexIndex * 13u + wordIndex] =
                    GXMetalFloatBits(values[wordIndex]);
        }
    }
}

static TQABoolean GXMetalATIPrivateQueueTriangle(
    GXMetalDrawState *state, const TQAVTexture triangle[3],
    TQABoolean allowTexture, uint32_t sourceMethod,
    const uint32_t vertexAddresses[3])
{
    TQAVGouraud gouraud[3];
    TQABoolean queued;
    uint32_t geometryIndex = GXMetalATIPrivateGeometryIndex(sourceMethod);
    uint32_t vertexIndex;

    if (geometryIndex < GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS) {
        gDiagnostics.ati_private_geometry_triangle_attempt_count[
            geometryIndex]++;
    }
    GXMetalATIPrivateRecordTriangleAnomaly(
        sourceMethod, triangle, vertexAddresses);
    GXMetalATIPrivateRecordFirstGeometryBurst(
        state, sourceMethod, triangle, vertexAddresses);
    if (allowTexture && state->texture != NULL) {
        /* OpenGLRendererATI supplies homogeneous, normalized coordinates in
         * private hooks 47, 48, 54, and 60. Texel-coordinate detection
         * belongs to the older public ATI RAVE path and only adds
         * per-triangle floating point work here. */
        queued = GXMetalQueueTextureTriangle(state, triangle, 0, 0);
        if (queued) {
            state->ati_private_frame_has_draws = 1;
        }
    } else {
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            gouraud[vertexIndex].x = triangle[vertexIndex].x;
            gouraud[vertexIndex].y = triangle[vertexIndex].y;
            gouraud[vertexIndex].z = triangle[vertexIndex].z;
            gouraud[vertexIndex].invW = triangle[vertexIndex].invW;
            gouraud[vertexIndex].r = triangle[vertexIndex].r;
            gouraud[vertexIndex].g = triangle[vertexIndex].g;
            gouraud[vertexIndex].b = triangle[vertexIndex].b;
            gouraud[vertexIndex].a = triangle[vertexIndex].a;
        }
        queued = GXMetalQueueGouraudTriangle(state, gouraud, 0);
        if (queued) {
            state->ati_private_frame_has_draws = 1;
        }
    }
    if (geometryIndex < GXMETAL_DIAGNOSTIC_ATI_GEOMETRY_METHODS) {
        if (queued) {
            gDiagnostics.ati_private_geometry_triangle_queued_count[
                geometryIndex]++;
        } else {
            gDiagnostics.ati_private_geometry_triangle_rejected_count[
                geometryIndex]++;
        }
    }
    return queued;
}

/* OpenGLRendererATI uses hooks 47 and 48 for contiguous triangle lists.
 * Captured AGL, Quake, and Oni calls confirm their fixed 128-byte transformed-
 * vertex layout. */
static TQAError GXMetalATIPrivateDrawContiguous(uint32_t sourceMethod,
                                                uint32_t rendererAddress,
                                                uint32_t vertexAddress,
                                                uint32_t vertexCount,
                                                uint32_t polygonMode,
                                                TQABoolean allowTexture)
{
    GXMetalDrawState *state = GXMetalATIPrivateGetState(rendererAddress);
    TQAVTexture triangle[3];
    uint32_t vertexAddresses[3];
    uint32_t byteCount;
    uint32_t vertexIndex;
    uint32_t triangleIndex;

    if (state == NULL || state->failed ||
        !gxmetal_ati_private_polygon_mode_is_fill(polygonMode) ||
        !gxmetal_ati_private_contiguous_vertex_bytes(
            vertexCount, &byteCount) ||
        !GXMetalDiagnosticMemoryRangeIsReadable(vertexAddress, byteCount)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    if (!GXMetalATIPrivatePrepareDraw(state)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    for (triangleIndex = 0; triangleIndex + 2u < vertexCount;
         triangleIndex += 3u) {
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            vertexAddresses[vertexIndex] =
                vertexAddress + (triangleIndex + vertexIndex) *
                    GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
            if (!GXMetalATIPrivateConvertVertex(
                    vertexAddresses[vertexIndex],
                    &triangle[vertexIndex])) {
                GXMetalATIPrivateRecordInputReject(sourceMethod);
                return kQANoErr;
            }
        }
        if (!GXMetalATIPrivateQueueTriangle(
                state, triangle, allowTexture, sourceMethod,
                vertexAddresses)) {
            return kQANoErr;
        }
    }
    return kQANoErr;
}

static TQAError GXMetalATIPrivateMethod47(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(47, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    return GXMetalATIPrivateDrawContiguous(
        47u, arg0, arg1, arg2, arg3, false);
}

static TQAError GXMetalATIPrivateMethod48(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(48, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;
    return GXMetalATIPrivateDrawContiguous(
        48u, arg0, arg1, arg2, arg3, true);
}

/* OpenGLRendererATI's generic transformed-primitive callbacks have both a
 * contiguous batch form and a form reduced to individual vertex pointers.
 * The batch form carries the original OpenGL mode in its seventh C argument;
 * expand its filled modes into independent triangles. */
static TQAError GXMetalATIPrivateDrawContiguousPrimitive(
    uint32_t sourceMethod, uint32_t rendererAddress, uint32_t vertexAddress,
    uint32_t vertexCount, uint32_t primitive, TQABoolean allowTexture)
{
    GXMetalDrawState *state = GXMetalATIPrivateGetState(rendererAddress);
    TQAVTexture triangle[3];
    uint32_t vertexAddresses[3];
    uint32_t byteCount;
    uint32_t indices[3];
    uint32_t triangleCount;
    uint32_t triangleIndex;
    uint32_t vertexIndex;

    triangleCount = gxmetal_ati_private_primitive_triangle_count(
        primitive, vertexCount);
    if (state == NULL || state->failed || triangleCount == 0 ||
        !gxmetal_ati_private_contiguous_vertex_bytes(
            vertexCount, &byteCount) ||
        !GXMetalDiagnosticMemoryRangeIsReadable(vertexAddress, byteCount)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    if (!GXMetalATIPrivatePrepareDraw(state)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    for (triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex) {
        if (!gxmetal_ati_private_primitive_triangle_indices(
                primitive, vertexCount, triangleIndex, indices)) {
            GXMetalATIPrivateRecordInputReject(sourceMethod);
            return kQANoErr;
        }
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            vertexAddresses[vertexIndex] =
                vertexAddress + indices[vertexIndex] *
                    GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
            if (!GXMetalATIPrivateConvertVertex(
                    vertexAddresses[vertexIndex],
                    &triangle[vertexIndex])) {
                GXMetalATIPrivateRecordInputReject(sourceMethod);
                return kQANoErr;
            }
        }
        if (!GXMetalATIPrivateQueueTriangle(
                state, triangle, allowTexture, sourceMethod,
                vertexAddresses)) {
            return kQANoErr;
        }
    }
    return kQANoErr;
}

/* ATI slots 49/50 are fan raster callbacks. GLCore supplies the center
 * separately from a contiguous rim so clipped slow-path fans can retain v0
 * while advancing pairs along a newly generated rim. arg3 is totalCount-1,
 * not an OpenGL primitive token. */
static TQAError GXMetalATIPrivateDrawFan(
    uint32_t sourceMethod, uint32_t rendererAddress,
    uint32_t centerAddress, uint32_t rimAddress,
    uint32_t countField, uint32_t polygonMode,
    TQABoolean allowTexture)
{
    GXMetalDrawState *state = GXMetalATIPrivateGetState(rendererAddress);
    TQAVTexture triangle[3];
    uint32_t vertexAddresses[3];
    uint32_t effectiveRimAddress;
    uint32_t rimVertexCount;
    uint32_t rimBytes;
    uint32_t triangleIndex;
    uint32_t vertexIndex;

    if (state == NULL || state->failed ||
        !gxmetal_ati_private_polygon_mode_is_fill(polygonMode) ||
        !gxmetal_ati_private_fan_layout(
            centerAddress, rimAddress, countField,
            &effectiveRimAddress, &rimVertexCount)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    rimBytes = rimVertexCount * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
    if (!GXMetalDiagnosticMemoryRangeIsReadable(
            centerAddress, GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES) ||
        !GXMetalDiagnosticMemoryRangeIsReadable(
            effectiveRimAddress, rimBytes) ||
        !GXMetalATIPrivatePrepareDraw(state)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    for (triangleIndex = 0;
         triangleIndex + UINT32_C(1) < rimVertexCount;
         ++triangleIndex) {
        vertexAddresses[0] = centerAddress;
        vertexAddresses[1] = effectiveRimAddress + triangleIndex *
            GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
        vertexAddresses[2] = effectiveRimAddress +
            (triangleIndex + UINT32_C(1)) *
                GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            if (!GXMetalATIPrivateConvertVertex(
                    vertexAddresses[vertexIndex],
                    &triangle[vertexIndex])) {
                GXMetalATIPrivateRecordInputReject(sourceMethod);
                return kQANoErr;
            }
        }
        if (!GXMetalATIPrivateQueueTriangle(
                state, triangle, allowTexture, sourceMethod,
                vertexAddresses)) {
            return kQANoErr;
        }
    }
    return kQANoErr;
}

/* Filled GL_QUADS, GL_QUAD_STRIP, and GL_POLYGON batches use the older ATI
 * fallback callback ABI: count in arg1, mode in arg2, and the contiguous
 * transformed-vertex base in arg4. Slots 41/42 belong to the ordinary
 * renderer family and 43/44 to its Texture* family. Within either family the
 * odd slot is installed without a loaded primary texture and the even slot
 * with one, so odd callbacks must never sample stale texture state. */
#define GXMETAL_ATI_PRIVATE_FILL_METHOD(index, allow_texture)               \
    static TQAError GXMetalATIPrivateMethod##index(                         \
        uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,         \
        uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7)         \
    {                                                                       \
        GXMetalTraceATIPrivateMethod(index, arg0, arg1, arg2, arg3, arg4,   \
                                     arg5, arg6, arg7);                     \
        (void)arg3; (void)arg5; (void)arg6; (void)arg7;                    \
        return GXMetalATIPrivateDrawContiguousPrimitive(                    \
            index, arg0, arg4, arg1, arg2, allow_texture);                  \
    }

GXMETAL_ATI_PRIVATE_FILL_METHOD(41, false)
GXMETAL_ATI_PRIVATE_FILL_METHOD(42, true)
GXMETAL_ATI_PRIVATE_FILL_METHOD(43, false)
GXMETAL_ATI_PRIVATE_FILL_METHOD(44, true)

/* GLCore has already transformed, culled, and clipped these callbacks. The
 * ATI table's 49/50 pair rasterizes untextured/textured fans using a separate
 * center plus contiguous rim. The 51/52 pair rasterizes contiguous triangle
 * strips; each pair's final meaningful argument is polygon mode, while later
 * volatile registers are not callback arguments. */
static TQAError GXMetalATIPrivateMethod49(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(49, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg5; (void)arg6; (void)arg7;
    return GXMetalATIPrivateDrawFan(
        49u, arg0, arg1, arg2, arg3, arg4, false);
}

static TQAError GXMetalATIPrivateMethod50(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(50, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg5; (void)arg6; (void)arg7;
    return GXMetalATIPrivateDrawFan(
        50u, arg0, arg1, arg2, arg3, arg4, true);
}

static TQAError GXMetalATIPrivateMethod51(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(51, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;
    if (!gxmetal_ati_private_polygon_mode_is_fill(arg3)) {
        GXMetalATIPrivateRecordInputReject(51u);
        return kQANoErr;
    }
    return GXMetalATIPrivateDrawContiguousPrimitive(
        51u, arg0, arg1, arg2,
        GXMETAL_ATI_GL_TRIANGLE_STRIP, false);
}

static TQAError GXMetalATIPrivateMethod52(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(52, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;
    if (!gxmetal_ati_private_polygon_mode_is_fill(arg3)) {
        GXMetalATIPrivateRecordInputReject(52u);
        return kQANoErr;
    }
    return GXMetalATIPrivateDrawContiguousPrimitive(
        52u, arg0, arg1, arg2,
        GXMETAL_ATI_GL_TRIANGLE_STRIP, true);
}

/* Immediate-mode filled primitives occupy fixed odd/even callback pairs:
 * 53/54 quads, 55/56 quad strips, and 57/58 polygons. The base is arg1 and
 * the bounded vertex count is arg2; arg5 is the last transformed-vertex
 * pointer and is diagnostic only. Odd slots are untextured, while even slots
 * may use the currently loaded primary texture. Pointer-sized internal edge
 * calls fail the shared count bound without dereferencing renderer state. */
#define GXMETAL_ATI_PRIVATE_FIXED_METHOD(index, primitive, allow_texture)   \
    static TQAError GXMetalATIPrivateMethod##index(                         \
        uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,         \
        uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7)         \
    {                                                                       \
        GXMetalTraceATIPrivateMethod(index, arg0, arg1, arg2, arg3, arg4,   \
                                     arg5, arg6, arg7);                     \
        (void)arg3; (void)arg4; (void)arg5;                               \
        (void)arg6; (void)arg7;                                             \
        return GXMetalATIPrivateDrawContiguousPrimitive(                    \
            index, arg0, arg1, arg2, primitive, allow_texture);             \
    }

GXMETAL_ATI_PRIVATE_FIXED_METHOD(53, GXMETAL_ATI_GL_QUADS, false)
GXMETAL_ATI_PRIVATE_FIXED_METHOD(54, GXMETAL_ATI_GL_QUADS, true)
GXMETAL_ATI_PRIVATE_FIXED_METHOD(55, GXMETAL_ATI_GL_QUAD_STRIP, false)
GXMETAL_ATI_PRIVATE_FIXED_METHOD(56, GXMETAL_ATI_GL_QUAD_STRIP, true)
GXMETAL_ATI_PRIVATE_FIXED_METHOD(57, GXMETAL_ATI_GL_POLYGON, false)
GXMETAL_ATI_PRIVATE_FIXED_METHOD(58, GXMETAL_ATI_GL_POLYGON, true)

static TQABoolean GXMetalATIPrivateConvertVertex(uint32_t address,
                                                 TQAVTexture *destination)
{
    const float *source;

    if (destination == NULL ||
        !GXMetalDiagnosticMemoryRangeIsReadable(address, 24u * sizeof(float))) {
        return false;
    }
    source = (const float *)(uintptr_t)address;
    destination->x = source[12];
    destination->y = source[13];
    destination->z = source[14];
    destination->invW = source[15];
    destination->r = source[4];
    destination->g = source[5];
    destination->b = source[6];
    destination->a = source[7];
    destination->uOverW = source[16];
    destination->vOverW = source[17];
    destination->kd_r = source[4];
    destination->kd_g = source[5];
    destination->kd_b = source[6];
    /* OpenGLRendererATI stores its secondary stage's homogeneous S/T/Q
     * coordinates at words 20, 21, and 23.  Q is either reciprocal-W or
     * 1 depending on the OpenGL texture-coordinate path.  TQAVTexture's
     * specular slots occupy otherwise unused wire fields, so carry the
     * complete triplet through for GXMetal's dual-texture shader. */
    destination->ks_r = source[20];
    destination->ks_g = source[21];
    destination->ks_b = source[23];
    return true;
}

/* ATI's paired dispatch+0x48 callbacks are batch-boundary/finish paths, not
 * ordinary polygon renderers. Reverse engineering both the normal and accelerated
 * Rage 128 tables shows that slot 60 consumes only its context and emits
 * hardware commit writes; slot 59 propagates four floats into internal state
 * without walking its apparent pointer/count arguments. Apple's GLD leaves
 * redundant end-of-primitive geometry values in those registers. Treating
 * them as a pointer fan redraws already-submitted surfaces, including Oni's
 * full-screen transition quad. Slot 59 is a safe no-op until its internal
 * four-float state meaning is evidenced. Real ATI hardware has already staged
 * clipped geometry by slot 60.  The fourth argument describes that clipping
 * state; zero means an ordinary, unclipped fan rather than an empty draw.
 * Reconstruct the fan for both forms, then flush the pending batch. */
static TQAError GXMetalATIPrivateDrawPointerFan(
    uint32_t sourceMethod, uint32_t rendererAddress,
    uint32_t pointerAddress, uint32_t vertexCount)
{
    const uint32_t *vertexPointers;
    GXMetalDrawState *state = GXMetalATIPrivateGetState(rendererAddress);
    TQAVTexture triangle[3];
    uint32_t triangleAddresses[3];
    uint32_t triangleIndex;

    if (state == NULL || state->failed || vertexCount < 3u ||
        vertexCount > GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT ||
        !GXMetalDiagnosticMemoryRangeIsReadable(
            pointerAddress,
            vertexCount * (uint32_t)sizeof(*vertexPointers))) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    vertexPointers = (const uint32_t *)(uintptr_t)pointerAddress;
    if (!GXMetalATIPrivateConvertVertex(vertexPointers[0], &triangle[0]) ||
        !GXMetalATIPrivateConvertVertex(vertexPointers[1], &triangle[1]) ||
        !GXMetalATIPrivatePrepareDraw(state)) {
        GXMetalATIPrivateRecordInputReject(sourceMethod);
        return kQANoErr;
    }
    triangleAddresses[0] = vertexPointers[0];
    triangleAddresses[1] = vertexPointers[1];
    for (triangleIndex = 0; triangleIndex + 2u < vertexCount;
         ++triangleIndex) {
        triangleAddresses[2] = vertexPointers[triangleIndex + 2u];
        if (!GXMetalATIPrivateConvertVertex(
                triangleAddresses[2], &triangle[2])) {
            GXMetalATIPrivateRecordInputReject(sourceMethod);
            return kQANoErr;
        }
        if (!GXMetalATIPrivateQueueTriangle(
                state, triangle, true, sourceMethod,
                triangleAddresses)) {
            return kQANoErr;
        }
        triangle[1] = triangle[2];
        triangleAddresses[1] = triangleAddresses[2];
    }
    return kQANoErr;
}

static TQAError GXMetalATIPrivateFinish(uint32_t rendererAddress)
{
    GXMetalDrawState *state = GXMetalATIPrivateGetState(rendererAddress);

    if (state != NULL && !state->failed) {
        (void)GXMetalFlushPendingDraws(state);
    }
    return kQANoErr;
}

static TQAError GXMetalATIPrivateMethod59(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(59, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;
    return kQANoErr;
}

static TQAError GXMetalATIPrivateMethod60(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalTraceATIPrivateMethod(60, arg0, arg1, arg2, arg3, arg4, arg5,
                                 arg6, arg7);
    (void)arg3; (void)arg5; (void)arg6; (void)arg7;
    if (gxmetal_ati_private_pointer_fan_should_render(arg2, arg4)) {
        (void)GXMetalATIPrivateDrawPointerFan(60u, arg0, arg1, arg2);
    } else {
        GXMetalATIPrivateRecordInputReject(60u);
    }
    return GXMetalATIPrivateFinish(arg0);
}

static GXMetalATIPrivateMethod
    gGXMetalATIPrivateMethods[GXMETAL_ATI_PRIVATE_METHOD_COUNT] = {
        GXMetalATIPrivateMethod0, GXMetalATIPrivateMethod1,
        GXMetalATIPrivateMethod2, GXMetalATIPrivateMethod3,
        GXMetalATIPrivateMethod4, GXMetalATIPrivateMethod5,
        GXMetalATIPrivateMethod6, GXMetalATIPrivateMethod7,
        GXMetalATIPrivateMethod8, GXMetalATIPrivateMethod9,
        GXMetalATIPrivateMethod10, GXMetalATIPrivateMethod11,
        GXMetalATIPrivateMethod12, GXMetalATIPrivateMethod13,
        GXMetalATIPrivateMethod14, GXMetalATIPrivateMethod15,
        GXMetalATIPrivateMethod16, GXMetalATIPrivateMethod17,
        GXMetalATIPrivateMethod18, GXMetalATIPrivateMethod19,
        GXMetalATIPrivateMethod20, GXMetalATIPrivateMethod21,
        GXMetalATIPrivateMethod22, GXMetalATIPrivateMethod23,
        GXMetalATIPrivateMethod24, GXMetalATIPrivateMethod25,
        GXMetalATIPrivateMethod26, GXMetalATIPrivateMethod27,
        GXMetalATIPrivateMethod28, GXMetalATIPrivateMethod29,
        GXMetalATIPrivateMethod30, GXMetalATIPrivateMethod31,
        GXMetalATIPrivateMethod32, GXMetalATIPrivateMethod33,
        GXMetalATIPrivateMethod34, GXMetalATIPrivateMethod35,
        GXMetalATIPrivateMethod36, GXMetalATIPrivateMethod37,
        GXMetalATIPrivateMethod38, GXMetalATIPrivateMethod39,
        GXMetalATIPrivateMethod40, GXMetalATIPrivateMethod41,
        GXMetalATIPrivateMethod42, GXMetalATIPrivateMethod43,
        GXMetalATIPrivateMethod44, GXMetalATIPrivateMethod45,
        GXMetalATIPrivateMethod46, GXMetalATIPrivateMethod47,
        GXMetalATIPrivateMethod48, GXMetalATIPrivateMethod49,
        GXMetalATIPrivateMethod50, GXMetalATIPrivateMethod51,
        GXMetalATIPrivateMethod52, GXMetalATIPrivateMethod53,
        GXMetalATIPrivateMethod54, GXMetalATIPrivateMethod55,
        GXMetalATIPrivateMethod56, GXMetalATIPrivateMethod57,
        GXMetalATIPrivateMethod58, GXMetalATIPrivateMethod59,
        GXMetalATIPrivateMethod60, GXMetalATIPrivateMethod61,
        GXMetalATIPrivateMethod62, GXMetalATIPrivateMethod63
    };

typedef TQAError (*GXMetalATIEngineMethod)(const void *device,
                                           uint32_t *value);

/* OpenGLRendererATI asks every ATI RAVE engine for selector 1002, then calls
 * entries seven and eight without checking the Gestalt result.  These two
 * hooks return optional texture-layout flags and a floating-point bias.  A
 * conservative zero response selects the renderer's portable paths while
 * keeping all texture allocation and upload work on RAVE's public API. */
static TQAError GXMetalATIEngineQueryZero(const void *device,
                                          uint32_t *value)
{
    (void)device;
    if (value == NULL) {
        return kQAParamErr;
    }
    *value = 0;
    return kQANoErr;
}

static GXMetalATIEngineMethod gGXMetalATIEngineMethods[9] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    GXMetalATIEngineQueryZero,
    GXMetalATIEngineQueryZero
};

static void *GXMetalGetPtr(const TQADrawContext *drawContext, TQATagPtr tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t multiTextureStage = state != NULL ?
        state->int_state[kQATag_MultiTextureCurrent] : UINT32_MAX;
    gDiagnostics.draw_method_stage = 150;
    gDiagnostics.get_ptr_count++;
    gDiagnostics.last_get_ptr_tag = (uint32_t)tag;
    GXMetalPersistDiagnostics();
    if (state != NULL &&
        (uint32_t)tag == GXMETAL_ATI_PRIVATE_METHODS_TAG) {
        gDiagnostics.last_get_ptr_value =
            (uint32_t)(uintptr_t)gGXMetalATIPrivateMethods;
        gDiagnostics.draw_method_stage = 151;
        GXMetalPersistDiagnostics();
        return (void *)gGXMetalATIPrivateMethods;
    }
    if (state != NULL && tag == kQATag_MultiTexture &&
        multiTextureStage == 0u &&
        (state->ati_private_enabled ||
         (state->transport->features &
          GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) != 0)) {
        gDiagnostics.last_get_ptr_value =
            (uint32_t)(uintptr_t)state->secondary_texture;
        gDiagnostics.draw_method_stage = 151;
        GXMetalPersistDiagnostics();
        return (void *)state->secondary_texture;
    }
    if (state == NULL || tag != kQATag_Texture) {
        gDiagnostics.last_get_ptr_value = 0;
        gDiagnostics.draw_method_stage = 151;
        GXMetalPersistDiagnostics();
        return NULL;
    }
    gDiagnostics.last_get_ptr_value = (uint32_t)(uintptr_t)state->texture;
    gDiagnostics.draw_method_stage = 151;
    GXMetalPersistDiagnostics();
    return (void *)state->texture;
}

static void GXMetalTraceDrawMethod(uint32_t method)
{
    uint32_t mask = method < 32 ? UINT32_C(1) << method : 0;

    gDiagnostics.draw_method_stage = 300 + method;
    gDiagnostics.draw_call_count++;
    gDiagnostics.last_draw_method = method;
    if (mask != 0 && (gDiagnostics.draw_method_mask & mask) == 0) {
        gDiagnostics.draw_method_mask |= mask;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
}

static void GXMetalTraceTextureState(const GXMetalDrawState *state,
                                     const TQAVTexture *vertex)
{
    const TQATexture *texture;

    if (state == NULL) {
        return;
    }
    gDiagnostics.current_state_texture_op =
        state->int_state[kQATag_TextureOp];
    gDiagnostics.current_state_texture_filter =
        state->int_state[kQATag_TextureFilter];
    gDiagnostics.current_state_blend = state->int_state[kQATag_Blend];
    gDiagnostics.current_state_z_function =
        state->int_state[kQATag_ZFunction];
    gDiagnostics.current_state_z_buffer_mask =
        state->int_state[kQATag_ZBufferMask];
    gDiagnostics.current_state_perspective_z =
        state->int_state[kQATag_PerspectiveZ];
    gDiagnostics.current_state_fog_mode =
        state->int_state[kQATag_FogMode];
    gDiagnostics.current_state_fog_color_a =
        GXMetalFloatBits(state->float_state[kQATag_FogColor_a]);
    gDiagnostics.current_state_fog_color_r =
        GXMetalFloatBits(state->float_state[kQATag_FogColor_r]);
    gDiagnostics.current_state_fog_color_g =
        GXMetalFloatBits(state->float_state[kQATag_FogColor_g]);
    gDiagnostics.current_state_fog_color_b =
        GXMetalFloatBits(state->float_state[kQATag_FogColor_b]);
    gDiagnostics.current_state_fog_start =
        GXMetalFloatBits(state->float_state[kQATag_FogStart]);
    gDiagnostics.current_state_fog_end =
        GXMetalFloatBits(state->float_state[kQATag_FogEnd]);
    gDiagnostics.current_state_fog_density =
        GXMetalFloatBits(state->float_state[kQATag_FogDensity]);
    gDiagnostics.current_state_fog_max_depth =
        GXMetalFloatBits(state->float_state[kQATag_FogMaxDepth]);
    gDiagnostics.current_state_alpha_test =
        state->int_state[kQATag_AlphaTestFunc];
    gDiagnostics.current_state_alpha_reference =
        GXMetalFloatBits(state->float_state[kQATag_AlphaTestRef]);
    gDiagnostics.current_state_wrap_u =
        state->int_state[kQATagGL_TextureWrapU];
    gDiagnostics.current_state_wrap_v =
        state->int_state[kQATagGL_TextureWrapV];

    texture = (const TQATexture *)state->texture;
    if (texture != NULL && texture->magic == GXMETAL_TEXTURE_MAGIC) {
        gDiagnostics.current_texture_resource_id = texture->resource_id;
        gDiagnostics.current_texture_flags = texture->source_flags;
        gDiagnostics.current_texture_pixel_type = texture->source_pixel_type;
        gDiagnostics.current_texture_width = texture->width;
        gDiagnostics.current_texture_height = texture->height;
        gDiagnostics.current_texture_palette_bound = texture->palette_bound;
    } else {
        gDiagnostics.current_texture_resource_id = 0;
        gDiagnostics.current_texture_flags = 0;
        gDiagnostics.current_texture_pixel_type = UINT32_MAX;
        gDiagnostics.current_texture_width = 0;
        gDiagnostics.current_texture_height = 0;
        gDiagnostics.current_texture_palette_bound = 0;
    }
    if (vertex != NULL) {
        gDiagnostics.last_texture_vertex_x = GXMetalFloatBits(vertex->x);
        gDiagnostics.last_texture_vertex_y = GXMetalFloatBits(vertex->y);
        gDiagnostics.last_texture_vertex_z = GXMetalFloatBits(vertex->z);
        gDiagnostics.last_texture_vertex_inv_w =
            GXMetalFloatBits(vertex->invW);
        gDiagnostics.last_texture_vertex_a = GXMetalFloatBits(vertex->a);
        gDiagnostics.last_texture_vertex_u_over_w =
            GXMetalFloatBits(vertex->uOverW);
        gDiagnostics.last_texture_vertex_v_over_w =
            GXMetalFloatBits(vertex->vOverW);
        gDiagnostics.last_texture_vertex_kd_r =
            GXMetalFloatBits(vertex->kd_r);
        gDiagnostics.last_texture_vertex_kd_g =
            GXMetalFloatBits(vertex->kd_g);
        gDiagnostics.last_texture_vertex_kd_b =
            GXMetalFloatBits(vertex->kd_b);
    }
}

static void GXMetalDrawPoint(const TQADrawContext *drawContext,
                             const TQAVGouraud *vertex)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalTraceDrawMethod(kQADrawPoint);
    if (state != NULL && GXMetalFlushPendingDraws(state)) {
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
    GXMetalTraceDrawMethod(kQADrawLine);
    if (state == NULL || v0 == NULL || v1 == NULL) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
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
    GXMetalTraceDrawMethod(kQADrawTriGouraud);
    gDiagnostics.draw_tri_gouraud_count++;
    if (state == NULL || v0 == NULL || v1 == NULL || v2 == NULL) {
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    vertices[2] = *v2;
    (void)GXMetalQueueGouraudTriangle(state, vertices, (uint32_t)flags);
}

static float GXMetalClampUnit(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    return value > 1.0f ? 1.0f : value;
}

/* Carmageddon II's ATI RAVE path uses TQAVTexture for both textured and
 * untextured lit materials. For the latter it binds a NULL texture and still
 * submits Modulate/Highlight vertices. ATI treats that as an opaque white
 * texel, allowing kd + ks to carry the material lighting. Preserve that
 * compatibility behavior by lowering the triangle to an equivalent Gouraud
 * draw instead of silently dropping it. */
static void GXMetalUnboundTextureVertex(const GXMetalDrawState *state,
                                        const TQAVTexture *source,
                                        TQAVGouraud *destination)
{
    uint32_t operation = state->int_state[kQATag_TextureOp];
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;

    if (operation & kQATextureOp_Modulate) {
        red *= source->kd_r;
        green *= source->kd_g;
        blue *= source->kd_b;
    }
    if (operation & kQATextureOp_Highlight) {
        red += source->ks_r;
        green += source->ks_g;
        blue += source->ks_b;
    }
    destination->x = source->x;
    destination->y = source->y;
    destination->z = source->z;
    destination->invW = source->invW;
    destination->r = GXMetalClampUnit(red);
    destination->g = GXMetalClampUnit(green);
    destination->b = GXMetalClampUnit(blue);
    destination->a = GXMetalClampUnit(source->a);
}

static TQABoolean GXMetalPublicMultiTextureActive(
    const GXMetalDrawState *state);

static void GXMetalDrawTriTexture(const TQADrawContext *drawContext,
                                  const TQAVTexture *v0,
                                  const TQAVTexture *v1,
                                  const TQAVTexture *v2,
                                  unsigned long flags)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAVTexture vertices[3];
    GXMetalTraceDrawMethod(kQADrawTriTexture);
    gDiagnostics.draw_tri_texture_count++;
    GXMetalTraceTextureState(state, v0);
    if (state == NULL) {
        gDiagnostics.draw_tri_texture_reject_count++;
        gDiagnostics.draw_texture_null_state_count++;
        return;
    }
    if (state->texture == NULL) {
        TQAVGouraud gouraud[3];

        if (v0 == NULL || v1 == NULL || v2 == NULL) {
            gDiagnostics.draw_tri_texture_reject_count++;
            gDiagnostics.draw_texture_null_vertex_count++;
            return;
        }
        gDiagnostics.draw_texture_null_resource_count++;
        gDiagnostics.draw_texture_unbound_fallback_count++;
        GXMetalUnboundTextureVertex(state, v0, &gouraud[0]);
        GXMetalUnboundTextureVertex(state, v1, &gouraud[1]);
        GXMetalUnboundTextureVertex(state, v2, &gouraud[2]);
        (void)GXMetalQueueGouraudTriangle(state, gouraud, (uint32_t)flags);
        return;
    }
    if (((const TQATexture *)state->texture)->magic !=
            GXMETAL_TEXTURE_MAGIC) {
        gDiagnostics.draw_tri_texture_reject_count++;
        gDiagnostics.draw_texture_invalid_resource_count++;
        return;
    }
    if (v0 == NULL || v1 == NULL || v2 == NULL) {
        gDiagnostics.draw_tri_texture_reject_count++;
        gDiagnostics.draw_texture_null_vertex_count++;
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    vertices[2] = *v2;
    if (GXMetalPublicMultiTextureActive(state)) {
        if (state->submitted_multitexture_count < 3u ||
            !GXMetalFlushPendingDraws(state)) {
            state->failed = 1;
            return;
        }
        (void)GXMetalEmitTexture(
            state, GXMETAL_PRIMITIVE_TRIANGLE, 3u, vertices,
            (const TQAVMultiTexture *)
                state->submitted_multitexture_params,
            (uint32_t)flags, -1);
        return;
    }
    (void)GXMetalQueueTextureTriangle(
        state, vertices, (uint32_t)flags, -1);
}

static void GXMetalSubmitVerticesGouraud(
    const TQADrawContext *drawContext, unsigned long nVertices,
    const TQAVGouraud *vertices)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    Ptr replacement = NULL;
    uint32_t count = (uint32_t)nVertices;

    GXMetalTraceDrawMethod(kQASubmitVerticesGouraud);
    if (state == NULL) {
        return;
    }
    if (nVertices != (unsigned long)count ||
        count > GXMETAL_MAX_SUBMITTED_VERTICES ||
        (count != 0 && vertices == NULL)) {
        state->failed = 1;
        return;
    }
    if (count != 0) {
        replacement = NewPtr((Size)((uint64_t)count * sizeof(*vertices)));
        if (replacement == NULL) {
            state->failed = 1;
            return;
        }
        memcpy(replacement, vertices, (size_t)count * sizeof(*vertices));
    }
    if (state->submitted_gouraud_vertices != NULL) {
        DisposePtr(state->submitted_gouraud_vertices);
    }
    state->submitted_gouraud_vertices = replacement;
    state->submitted_gouraud_count = count;
}

static void GXMetalSubmitVerticesTexture(
    const TQADrawContext *drawContext, unsigned long nVertices,
    const TQAVTexture *vertices)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    Ptr replacement = NULL;
    uint32_t count = (uint32_t)nVertices;

    GXMetalTraceDrawMethod(kQASubmitVerticesTexture);
    if (state == NULL) {
        return;
    }
    if (nVertices != (unsigned long)count ||
        count > GXMETAL_MAX_SUBMITTED_VERTICES ||
        (count != 0 && vertices == NULL)) {
        state->failed = 1;
        return;
    }
    if (count != 0) {
        replacement = NewPtr((Size)((uint64_t)count * sizeof(*vertices)));
        if (replacement == NULL) {
            state->failed = 1;
            return;
        }
        memcpy(replacement, vertices, (size_t)count * sizeof(*vertices));
    }
    if (state->submitted_texture_vertices != NULL) {
        DisposePtr(state->submitted_texture_vertices);
    }
    state->submitted_texture_vertices = replacement;
    state->submitted_texture_count = count;
}

static void GXMetalSubmitMultiTextureParams(
    const TQADrawContext *drawContext, unsigned long nParams,
    const TQAVMultiTexture *params)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    Ptr replacement = NULL;
    uint32_t count = (uint32_t)nParams;

    GXMetalTraceDrawMethod(kQSubmitMultiTextureParams);
    if (state == NULL ||
        (state->transport->features &
         GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) == 0) {
        return;
    }
    if (state->int_state[kQATag_MultiTextureCurrent] != 0u ||
        nParams != (unsigned long)count ||
        count > GXMETAL_MAX_SUBMITTED_VERTICES ||
        (count != 0 && params == NULL)) {
        state->failed = 1;
        return;
    }
    if (count != 0) {
        replacement = NewPtr((Size)((uint64_t)count * sizeof(*params)));
        if (replacement == NULL) {
            state->failed = 1;
            return;
        }
        memcpy(replacement, params, (size_t)count * sizeof(*params));
    }
    if (state->submitted_multitexture_params != NULL) {
        DisposePtr(state->submitted_multitexture_params);
    }
    state->submitted_multitexture_params = replacement;
    state->submitted_multitexture_count = count;
}

static TQABoolean GXMetalMeshTriangleIsValid(
    const TQAIndexedTriangle *triangle, uint32_t vertexCount)
{
    return triangle != NULL && triangle->vertices[0] < vertexCount &&
           triangle->vertices[1] < vertexCount &&
           triangle->vertices[2] < vertexCount;
}

static TQABoolean GXMetalPublicMultiTextureActive(
    const GXMetalDrawState *state)
{
    return state != NULL && !state->ati_private_enabled &&
           (state->transport->features &
           GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) != 0 &&
           state->int_state[kQATag_MultiTextureEnable] != 0u &&
           state->secondary_texture != NULL;
}

static void GXMetalDrawTriMeshGouraud(
    const TQADrawContext *drawContext, unsigned long nTriangles,
    const TQAIndexedTriangle *triangles)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQAVGouraud *submitted;
    TQAVGouraud *batch;
    uint32_t triangleCount = (uint32_t)nTriangles;
    uint32_t triangleIndex = 0;

    GXMetalTraceDrawMethod(kQADrawTriMeshGouraud);
    if (state == NULL || nTriangles == 0) {
        return;
    }
    if (nTriangles != (unsigned long)triangleCount || triangles == NULL ||
        state->submitted_gouraud_vertices == NULL) {
        state->failed = 1;
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    submitted = (const TQAVGouraud *)state->submitted_gouraud_vertices;
    batch = (TQAVGouraud *)NewPtr((Size)(GXMETAL_MESH_BATCH_TRIANGLES * 3u *
                                        sizeof(*batch)));
    if (batch == NULL) {
        state->failed = 1;
        return;
    }
    while (triangleIndex < triangleCount && !state->failed) {
        uint32_t flags = (uint32_t)triangles[triangleIndex].triangleFlags;
        uint32_t batchTriangles = 0;
        while (triangleIndex + batchTriangles < triangleCount &&
               batchTriangles < GXMETAL_MESH_BATCH_TRIANGLES &&
               triangles[triangleIndex + batchTriangles].triangleFlags ==
                   (unsigned long)flags) {
            const TQAIndexedTriangle *triangle =
                &triangles[triangleIndex + batchTriangles];
            uint32_t destination = batchTriangles * 3u;
            if (!GXMetalMeshTriangleIsValid(
                    triangle, state->submitted_gouraud_count)) {
                state->failed = 1;
                break;
            }
            batch[destination] = submitted[triangle->vertices[0]];
            batch[destination + 1] = submitted[triangle->vertices[1]];
            batch[destination + 2] = submitted[triangle->vertices[2]];
            batchTriangles++;
        }
        if (batchTriangles != 0 &&
            !GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_TRIANGLE,
                                batchTriangles * 3u, batch, flags)) {
            state->failed = 1;
        }
        triangleIndex += batchTriangles;
    }
    DisposePtr((Ptr)batch);
}

static void GXMetalDrawTriMeshTexture(
    const TQADrawContext *drawContext, unsigned long nTriangles,
    const TQAIndexedTriangle *triangles)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQAVTexture *submitted;
    const TQAVMultiTexture *submittedMulti = NULL;
    TQAVTexture *batch;
    TQAVMultiTexture *multiBatch = NULL;
    TQAVGouraud *gouraudBatch;
    TQABoolean multiTexture;
    uint32_t triangleCount = (uint32_t)nTriangles;
    uint32_t triangleIndex = 0;

    GXMetalTraceDrawMethod(kQADrawTriMeshTexture);
    if (state == NULL || nTriangles == 0) {
        return;
    }
    if (nTriangles != (unsigned long)triangleCount || triangles == NULL ||
        state->submitted_texture_vertices == NULL) {
        state->failed = 1;
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    submitted = (const TQAVTexture *)state->submitted_texture_vertices;
    if (GXMetalMeshTriangleIsValid(
            &triangles[0], state->submitted_texture_count)) {
        GXMetalTraceTextureState(
            state, &submitted[triangles[0].vertices[0]]);
    } else {
        GXMetalTraceTextureState(state, NULL);
    }
    multiTexture = GXMetalPublicMultiTextureActive(state);
    if (multiTexture) {
        if (state->submitted_multitexture_count !=
            state->submitted_texture_count) {
            state->failed = 1;
            return;
        }
        submittedMulti = (const TQAVMultiTexture *)
            state->submitted_multitexture_params;
    }
    if (state->texture == NULL) {
        gouraudBatch = (TQAVGouraud *)NewPtr(
            (Size)(GXMETAL_MESH_BATCH_TRIANGLES * 3u *
                   sizeof(*gouraudBatch)));
        if (gouraudBatch == NULL) {
            state->failed = 1;
            return;
        }
        gDiagnostics.draw_texture_null_resource_count++;
        gDiagnostics.draw_texture_unbound_fallback_count++;
        while (triangleIndex < triangleCount && !state->failed) {
            uint32_t flags =
                (uint32_t)triangles[triangleIndex].triangleFlags;
            uint32_t batchTriangles = 0;
            while (triangleIndex + batchTriangles < triangleCount &&
                   batchTriangles < GXMETAL_MESH_BATCH_TRIANGLES &&
                   triangles[triangleIndex + batchTriangles].triangleFlags ==
                       (unsigned long)flags) {
                const TQAIndexedTriangle *triangle =
                    &triangles[triangleIndex + batchTriangles];
                uint32_t destination = batchTriangles * 3u;
                if (!GXMetalMeshTriangleIsValid(
                        triangle, state->submitted_texture_count)) {
                    state->failed = 1;
                    break;
                }
                GXMetalUnboundTextureVertex(
                    state, &submitted[triangle->vertices[0]],
                    &gouraudBatch[destination]);
                GXMetalUnboundTextureVertex(
                    state, &submitted[triangle->vertices[1]],
                    &gouraudBatch[destination + 1]);
                GXMetalUnboundTextureVertex(
                    state, &submitted[triangle->vertices[2]],
                    &gouraudBatch[destination + 2]);
                batchTriangles++;
            }
            if (batchTriangles != 0 &&
                !GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_TRIANGLE,
                                    batchTriangles * 3u, gouraudBatch,
                                    flags)) {
                state->failed = 1;
            }
            triangleIndex += batchTriangles;
        }
        DisposePtr((Ptr)gouraudBatch);
        return;
    }
    batch = (TQAVTexture *)NewPtr((Size)(GXMETAL_MESH_BATCH_TRIANGLES * 3u *
                                        sizeof(*batch)));
    if (batch == NULL) {
        state->failed = 1;
        return;
    }
    if (multiTexture) {
        multiBatch = (TQAVMultiTexture *)NewPtr(
            (Size)(GXMETAL_MESH_BATCH_TRIANGLES * 3u *
                   sizeof(*multiBatch)));
        if (multiBatch == NULL) {
            DisposePtr((Ptr)batch);
            state->failed = 1;
            return;
        }
    }
    while (triangleIndex < triangleCount && !state->failed) {
        uint32_t flags = (uint32_t)triangles[triangleIndex].triangleFlags;
        uint32_t batchTriangles = 0;
        while (triangleIndex + batchTriangles < triangleCount &&
               batchTriangles < GXMETAL_MESH_BATCH_TRIANGLES &&
               triangles[triangleIndex + batchTriangles].triangleFlags ==
                   (unsigned long)flags) {
            const TQAIndexedTriangle *triangle =
                &triangles[triangleIndex + batchTriangles];
            uint32_t destination = batchTriangles * 3u;
            if (!GXMetalMeshTriangleIsValid(
                    triangle, state->submitted_texture_count)) {
                state->failed = 1;
                break;
            }
            batch[destination] = submitted[triangle->vertices[0]];
            batch[destination + 1] = submitted[triangle->vertices[1]];
            batch[destination + 2] = submitted[triangle->vertices[2]];
            if (multiTexture) {
                multiBatch[destination] =
                    submittedMulti[triangle->vertices[0]];
                multiBatch[destination + 1] =
                    submittedMulti[triangle->vertices[1]];
                multiBatch[destination + 2] =
                    submittedMulti[triangle->vertices[2]];
            }
            batchTriangles++;
        }
        if (batchTriangles != 0 &&
            !GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_TRIANGLE,
                                batchTriangles * 3u, batch, multiBatch,
                                flags, -1)) {
            state->failed = 1;
        }
        triangleIndex += batchTriangles;
    }
    if (multiBatch != NULL) {
        DisposePtr((Ptr)multiBatch);
    }
    DisposePtr((Ptr)batch);
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

    GXMetalTraceDrawMethod(kQADrawVGouraud);
    if (state == NULL || vertices == NULL || nVertices == 0) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
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
        if (nVertices % 3 != 0) {
            state->failed = 1;
            return;
        }
        if (flags != NULL) {
            for (i = 0; i < nVertices; i += 3) {
                (void)GXMetalQueueGouraudTriangle(
                    state, &vertices[i], (uint32_t)flags[i / 3]);
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE;
        break;
    case kQAVertexMode_Strip:
        if (flags != NULL) {
            TQAVGouraud triangle[3];
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[0] = vertices[i + (i & 1)];
                triangle[1] = vertices[i + ((i & 1) == 0)];
                triangle[2] = vertices[i + 2];
                (void)GXMetalQueueGouraudTriangle(
                    state, triangle, (uint32_t)flags[i]);
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_STRIP;
        break;
    case kQAVertexMode_Fan:
        if (flags != NULL) {
            TQAVGouraud triangle[3];
            triangle[0] = vertices[0];
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[1] = vertices[i + 1];
                triangle[2] = vertices[i + 2];
                (void)GXMetalQueueGouraudTriangle(
                    state, triangle, (uint32_t)flags[i]);
            }
            return;
        }
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

static void GXMetalDrawVTexture(const TQADrawContext *drawContext,
                                unsigned long nVertices,
                                TQAVertexMode vertexMode,
                                const TQAVTexture vertices[],
                                const unsigned long flags[])
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQAVMultiTexture *secondary = NULL;
    TQABoolean multiTexture = 0;
    uint32_t primitive;
    unsigned long i;

    GXMetalTraceDrawMethod(kQADrawVTexture);
    if (state == NULL || vertices == NULL || nVertices == 0) {
        return;
    }
    if (state->texture == NULL) {
        TQAVGouraud *gouraud;

        if (nVertices > GXMETAL_MAX_SUBMITTED_VERTICES) {
            state->failed = 1;
            return;
        }
        gouraud = (TQAVGouraud *)NewPtr(
            (Size)((uint64_t)nVertices * sizeof(*gouraud)));
        if (gouraud == NULL) {
            state->failed = 1;
            return;
        }
        for (i = 0; i < nVertices; i++) {
            GXMetalUnboundTextureVertex(state, &vertices[i], &gouraud[i]);
        }
        gDiagnostics.draw_texture_null_resource_count++;
        gDiagnostics.draw_texture_unbound_fallback_count++;
        GXMetalDrawVGouraud(drawContext, nVertices, vertexMode, gouraud,
                            flags);
        DisposePtr((Ptr)gouraud);
        return;
    }
    multiTexture = GXMetalPublicMultiTextureActive(state);
    if (multiTexture) {
        if (nVertices > GXMETAL_MAX_SUBMITTED_VERTICES ||
            state->submitted_multitexture_count != (uint32_t)nVertices) {
            state->failed = 1;
            return;
        }
        secondary = (const TQAVMultiTexture *)
            state->submitted_multitexture_params;
    }
    if (!GXMetalFlushPendingDraws(state)) {
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
        if (nVertices % 3 != 0) {
            state->failed = 1;
            return;
        }
        if (flags != NULL) {
            for (i = 0; i < nVertices; i += 3) {
                if (multiTexture) {
                    (void)GXMetalEmitTexture(
                        state, GXMETAL_PRIMITIVE_TRIANGLE, 3u,
                        &vertices[i], &secondary[i],
                        (uint32_t)flags[i / 3], -1);
                } else {
                    (void)GXMetalQueueTextureTriangle(
                        state, &vertices[i], (uint32_t)flags[i / 3], -1);
                }
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE;
        break;
    case kQAVertexMode_Strip:
        if (flags != NULL) {
            TQAVTexture triangle[3];
            TQAVMultiTexture multiTriangle[3];
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[0] = vertices[i + (i & 1)];
                triangle[1] = vertices[i + ((i & 1) == 0)];
                triangle[2] = vertices[i + 2];
                if (multiTexture) {
                    multiTriangle[0] = secondary[i + (i & 1)];
                    multiTriangle[1] =
                        secondary[i + ((i & 1) == 0)];
                    multiTriangle[2] = secondary[i + 2];
                    (void)GXMetalEmitTexture(
                        state, GXMETAL_PRIMITIVE_TRIANGLE, 3u, triangle,
                        multiTriangle, (uint32_t)flags[i], -1);
                } else {
                    (void)GXMetalQueueTextureTriangle(
                        state, triangle, (uint32_t)flags[i], -1);
                }
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_STRIP;
        break;
    case kQAVertexMode_Fan:
        if (flags != NULL) {
            TQAVTexture triangle[3];
            TQAVMultiTexture multiTriangle[3];
            triangle[0] = vertices[0];
            if (multiTexture) {
                multiTriangle[0] = secondary[0];
            }
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[1] = vertices[i + 1];
                triangle[2] = vertices[i + 2];
                if (multiTexture) {
                    multiTriangle[1] = secondary[i + 1];
                    multiTriangle[2] = secondary[i + 2];
                    (void)GXMetalEmitTexture(
                        state, GXMETAL_PRIMITIVE_TRIANGLE, 3u, triangle,
                        multiTriangle, (uint32_t)flags[i], -1);
                } else {
                    (void)GXMetalQueueTextureTriangle(
                        state, triangle, (uint32_t)flags[i], -1);
                }
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_FAN;
        break;
    case kQAVertexMode_Polyline:
        for (i = 0; i + 1 < nVertices; i++) {
            (void)GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_LINE, 2,
                                     &vertices[i], multiTexture ?
                                         &secondary[i] : NULL,
                                     0, -1);
        }
        return;
    default:
        state->failed = 1;
        return;
    }
    (void)GXMetalEmitTexture(state, primitive, (uint32_t)nVertices,
                             vertices, secondary, 0, -1);
}

static void GXMetalBitmapVertex(TQAVTexture *vertex,
                                const TQAVGouraud *source,
                                float x, float y, float u, float v)
{
    memset(vertex, 0, sizeof(*vertex));
    vertex->x = x;
    vertex->y = y;
    vertex->z = source->z;
    /* QADrawBitmap is an affine screen-space copy. The source Gouraud
     * vertex's invW is optional unless perspective-Z is enabled, and older
     * games (including Carmageddon II) leave it at zero. Use the canonical
     * affine homogeneous coordinate instead of propagating that sentinel
     * into texture-coordinate division on the host. */
    vertex->invW = 1.0f;
    vertex->r = source->r;
    vertex->g = source->g;
    vertex->b = source->b;
    vertex->a = source->a;
    vertex->uOverW = u;
    /* QADrawBitmap places image row zero at the top of the destination.
     * Textured RAVE primitives use a lower-left V origin, so compensate in
     * these engine-generated vertices before the Metal shader performs the
     * normal RAVE-to-Metal origin conversion. */
    vertex->vOverW = 1.0f - v;
    vertex->kd_r = source->r;
    vertex->kd_g = source->g;
    vertex->kd_b = source->b;
}

static void GXMetalDrawBitmap(const TQADrawContext *drawContext,
                              const TQAVGouraud *v, TQABitmap *bitmap)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQATexture *savedTexture;
    uint32_t savedTextureResourceID;
    TQAVTexture vertices[4];
    float left;
    float top;
    float right;
    float bottom;
    float drawWidth;
    float drawHeight;
    float destinationWidth;
    float destinationHeight;
    float scaleX;
    float scaleY;
    float u0;
    float v0;
    float u1;
    float v1;
    uint32_t bitmapFilter;

    GXMetalTraceDrawMethod(kQADrawBitmap);
    gDiagnostics.draw_bitmap_count++;

    if (state == NULL || v == NULL || bitmap == NULL ||
        bitmap->magic != GXMETAL_BITMAP_MAGIC) {
        gDiagnostics.draw_bitmap_reject_count++;
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    scaleX = state->float_state[kQATag_BitmapScale_x];
    scaleY = state->float_state[kQATag_BitmapScale_y];
    drawWidth = (float)bitmap->width * scaleX;
    drawHeight = (float)bitmap->height * scaleY;
    bitmapFilter = state->int_state[kQATag_BitmapFilter];
    if (!(scaleX > 0.0f) || !(scaleY > 0.0f) ||
        !(drawWidth <= (float)GXMETAL_MAX_DIMENSION) ||
        !(drawHeight <= (float)GXMETAL_MAX_DIMENSION) ||
        bitmapFilter > kQAFilter_Best) {
        gDiagnostics.draw_bitmap_reject_count++;
        return;
    }
    if (state->ati_private_enabled &&
        gxmetal_ati_uses_logical_bitmap_canvas(
            v->x, v->y, drawWidth, drawHeight)) {
        GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
            state->width, state->height, v->x, v->y,
            drawWidth, drawHeight);
        left = rect.left;
        top = rect.top;
        right = rect.right;
        bottom = rect.bottom;
    } else {
        left = v->x;
        top = v->y;
        right = left + drawWidth;
        bottom = top + drawHeight;
    }
    destinationWidth = right - left;
    destinationHeight = bottom - top;
    if (right <= 0.0f || bottom <= 0.0f ||
        left >= (float)state->width || top >= (float)state->height) {
        return;
    }
    u0 = 0.0f;
    v0 = 0.0f;
    u1 = 1.0f;
    v1 = 1.0f;
    if (left < 0.0f) {
        u0 = -left / destinationWidth;
        left = 0.0f;
    }
    if (top < 0.0f) {
        v0 = -top / destinationHeight;
        top = 0.0f;
    }
    if (right > (float)state->width) {
        u1 -= (right - (float)state->width) / destinationWidth;
        right = (float)state->width;
    }
    if (bottom > (float)state->height) {
        v1 -= (bottom - (float)state->height) / destinationHeight;
        bottom = (float)state->height;
    }
    GXMetalBitmapVertex(&vertices[0], v, left, top, u0, v0);
    GXMetalBitmapVertex(&vertices[1], v, right, top, u1, v0);
    GXMetalBitmapVertex(&vertices[2], v, left, bottom, u0, v1);
    GXMetalBitmapVertex(&vertices[3], v, right, bottom, u1, v1);

    savedTexture = (const TQATexture *)state->texture;
    savedTextureResourceID = state->texture_resource_id;
    /* The bitmap resource is bound directly by ID and has no TQATexture
     * wrapper.  Hide the previously bound ATI texture while emitting this
     * quad so GXMetalEmitTexture does not apply ATI's private UV transform to
     * an ordinary QADrawBitmap operation. */
    state->texture = NULL;
    state->texture_resource_id = 0;
    if (!GXMetalEmitState(state, kQATag_Texture, GXMETAL_STATE_RESOURCE,
                          bitmap->resource_id) ||
        !GXMetalEmitState(state, kQATag_TextureOp, GXMETAL_STATE_UINT32,
                          kQATextureOp_None) ||
        !GXMetalEmitState(state, kQATag_TextureFilter, GXMETAL_STATE_UINT32,
                          bitmapFilter) ||
        !GXMetalEmitState(state, kQATagGL_TextureWrapU, GXMETAL_STATE_UINT32,
                          kQAGL_Clamp) ||
        !GXMetalEmitState(state, kQATagGL_TextureWrapV, GXMETAL_STATE_UINT32,
                          kQAGL_Clamp) ||
        !GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_TRIANGLE_STRIP, 4,
                            vertices, NULL, 0, -1)) {
        state->texture = savedTexture;
        state->texture_resource_id = savedTextureResourceID;
        return;
    }
    state->texture = savedTexture;
    state->texture_resource_id = savedTextureResourceID;
    (void)GXMetalEmitState(state, kQATag_Texture, GXMETAL_STATE_RESOURCE,
                           savedTextureResourceID);
    (void)GXMetalEmitState(state, kQATag_TextureOp, GXMETAL_STATE_UINT32,
                           state->int_state[kQATag_TextureOp]);
    /* The bitmap's legacy filter preset changes MIN, MAG, and mip selection
     * on the host. Restore the exact effective OpenGL pair rather than the
     * last legacy preset, which may predate asymmetric GL state. */
    (void)GXMetalEmitState(state, kQATagGL_TextureMinFilter,
                           GXMETAL_STATE_UINT32,
                           state->effective_texture_min_filter);
    (void)GXMetalEmitState(state, kQATagGL_TextureMagFilter,
                           GXMETAL_STATE_UINT32,
                           state->effective_texture_mag_filter);
    (void)GXMetalEmitState(state, kQATagGL_TextureWrapU,
                           GXMETAL_STATE_UINT32,
                           state->int_state[kQATagGL_TextureWrapU]);
    (void)GXMetalEmitState(state, kQATagGL_TextureWrapV,
                           GXMETAL_STATE_UINT32,
                           state->int_state[kQATagGL_TextureWrapV]);
}

static void GXMetalRenderStart(const TQADrawContext *drawContext,
                               const TQARect *dirtyRect,
                               const TQADrawContext *initialContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    gDiagnostics.draw_method_stage = 200;
    gDiagnostics.render_start_count++;
    (void)initialContext;
    if (state == NULL) {
        return;
    }
    gRenderEpoch++;
    if (gRenderEpoch == 0) {
        gRenderEpoch = 1;
    }
    if (GXMetalEmitRect(state, GXMETAL_OP_BEGIN_FRAME, dirtyRect) ==
        kQANoErr) {
        uint32_t clearFlags = GXMETAL_CLEAR_COLOR;
        if (state->context_flags & GXMETAL_CONTEXT_DEPTH_MASK) {
            clearFlags |= GXMETAL_CLEAR_DEPTH;
        }
        (void)GXMetalEmitClear(state, dirtyRect, clearFlags);
    }
    gDiagnostics.draw_method_stage = 201;
}

static TQAError GXMetalRenderEnd(const TQADrawContext *drawContext,
                                 const TQARect *modifiedRect)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAError error;

    gDiagnostics.draw_method_stage = 210;
    gDiagnostics.render_end_count++;
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
    gDiagnostics.draw_method_stage = 211;
    if ((gDiagnostics.render_end_count & UINT32_C(63)) == 0) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return error;
}

static TQAError GXMetalSync(const TQADrawContext *drawContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t sequence;

    gDiagnostics.draw_method_stage = 230;
    gDiagnostics.sync_count++;
    if (state == NULL || state->failed ||
        !GXMetalFlushPendingDraws(state) ||
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
    gDiagnostics.draw_method_stage = 220;
    gDiagnostics.flush_count++;
    return state != NULL && !state->failed &&
           GXMetalFlushPendingDraws(state) ? kQANoErr : kQAError;
}

static TQAError GXMetalAccessDrawBuffer(const TQADrawContext *drawContext,
                                        TQAPixelBuffer *buffer)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t pixelType;
    uint32_t length;
    uint32_t sequence;

    if (state == NULL || buffer == NULL) {
        return kQAParamErr;
    }
    if (state->access_draw_buffer_active != 0) {
        return kQAParamErr;
    }
    if (state->failed) {
        return kQAError;
    }
    if ((state->transport->features &
         (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
          GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK)) !=
            (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
             GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK) ||
        !gxmetal_rave_draw_buffer_layout(
            state->width, state->height, state->row_bytes,
            state->pixel_format, GXMETAL_UPLOAD_BYTES,
            &pixelType, &length)) {
        return kQANotSupported;
    }
    if (!GXMetalFlushPendingDraws(state) ||
        !gxmetal_guest_packet_begin(
            state->transport, GXMETAL_OP_READBACK,
            GXMETAL_READBACK_PACKET_BYTES, state->context_id, &packet)) {
        state->failed = 1;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_READBACK_SHARED_OFFSET_OFFSET,
                       GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload + GXMETAL_READBACK_LENGTH_OFFSET, length);
    gxmetal_store_le32(payload + GXMETAL_READBACK_ROW_BYTES_OFFSET,
                       state->row_bytes);
    gxmetal_store_le32(payload + GXMETAL_READBACK_RESERVED_OFFSET, 0);
    gxmetal_guest_packet_commit(state->transport, &packet);
    if (!gxmetal_guest_emit_fence(state->transport, &sequence) ||
        !gxmetal_guest_wait(state->transport, sequence,
                            GXMETAL_SYNC_SPINS)) {
        state->failed = 1;
        return kQAError;
    }

    buffer->rowBytes = (long)state->row_bytes;
    buffer->pixelType = (TQAImagePixelType)pixelType;
    buffer->width = (long)state->width;
    buffer->height = (long)state->height;
    buffer->baseAddr = state->transport->shared + GXMETAL_UPLOAD_OFFSET;
    state->access_draw_buffer_active = 1;
    return kQANoErr;
}

static TQAError GXMetalAccessDrawBufferEnd(
    const TQADrawContext *drawContext, const TQARect *dirtyRect)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t pixelType;
    uint32_t length;
    uint32_t sequence;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    TQAError error;

    if (state == NULL || state->access_draw_buffer_active == 0) {
        return kQAParamErr;
    }
    state->access_draw_buffer_active = 0;
    if (state->failed) {
        return kQAError;
    }
    error = GXMetalAccessRect(dirtyRect, state->width, state->height,
                              &left, &top, &width, &height);
    if (error != kQANoErr || width == 0 || height == 0) {
        return error;
    }
    if (!gxmetal_rave_draw_buffer_layout(
            state->width, state->height, state->row_bytes,
            state->pixel_format, GXMETAL_UPLOAD_BYTES,
            &pixelType, &length) ||
        !gxmetal_guest_packet_begin(
            state->transport, GXMETAL_OP_DRAW_BUFFER_WRITEBACK,
            GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES,
            state->context_id, &packet)) {
        state->failed = 1;
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_SHARED_OFFSET_OFFSET,
        GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_LENGTH_OFFSET, length);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_ROW_BYTES_OFFSET, state->row_bytes);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RESERVED_OFFSET, 0);
    payload += GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET;
    gxmetal_store_le32(payload + GXMETAL_RECT_LEFT_OFFSET, left);
    gxmetal_store_le32(payload + GXMETAL_RECT_TOP_OFFSET, top);
    gxmetal_store_le32(payload + GXMETAL_RECT_RIGHT_OFFSET, left + width);
    gxmetal_store_le32(payload + GXMETAL_RECT_BOTTOM_OFFSET, top + height);
    gxmetal_guest_packet_commit(state->transport, &packet);

    /* The fixed staging aperture is also used by resource uploads. Wait for
     * the host to consume the dirty pixels before returning it to callers. */
    if (!gxmetal_guest_emit_fence(state->transport, &sequence) ||
        !gxmetal_guest_wait(state->transport, sequence,
                            GXMETAL_SYNC_SPINS)) {
        state->failed = 1;
        return kQAError;
    }
    return kQANoErr;
}

static TQAError GXMetalRenderAbort(const TQADrawContext *drawContext)
{
    gDiagnostics.draw_method_stage = 240;
    gDiagnostics.render_abort_count++;
    return GXMetalSync(drawContext);
}

static TQAError GXMetalSetNoticeMethod(const TQADrawContext *drawContext,
                                       TQAMethodSelector method,
                                       TQANoticeMethod callback,
                                       void *refCon)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    gDiagnostics.draw_method_stage = 250;
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
    gDiagnostics.draw_method_stage = 251;
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
    gDiagnostics.draw_method_stage = 260;
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
    gDiagnostics.draw_method_stage = 261;
    (void)initialContext;
    if (state == NULL ||
        (state->context_flags & GXMETAL_CONTEXT_DEPTH_MASK) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitClear(state, rect, GXMETAL_CLEAR_DEPTH);
}

static TQAError GXMetalSwapBuffers(const TQADrawContext *drawContext,
                                   const TQARect *dirtyRect)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    gDiagnostics.draw_method_stage = 262;
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
    GXMETAL_REGISTER_METHOD(kQADrawTriTexture, drawTriTexture,
                            GXMetalDrawTriTexture);
    GXMETAL_REGISTER_METHOD(kQASubmitVerticesGouraud,
                            submitVerticesGouraud,
                            GXMetalSubmitVerticesGouraud);
    GXMETAL_REGISTER_METHOD(kQASubmitVerticesTexture,
                            submitVerticesTexture,
                            GXMetalSubmitVerticesTexture);
    GXMETAL_REGISTER_METHOD(kQADrawTriMeshGouraud, drawTriMeshGouraud,
                            GXMetalDrawTriMeshGouraud);
    GXMETAL_REGISTER_METHOD(kQADrawTriMeshTexture, drawTriMeshTexture,
                            GXMetalDrawTriMeshTexture);
    GXMETAL_REGISTER_METHOD(kQADrawVGouraud, drawVGouraud,
                            GXMetalDrawVGouraud);
    GXMETAL_REGISTER_METHOD(kQADrawVTexture, drawVTexture,
                            GXMetalDrawVTexture);
    GXMETAL_REGISTER_METHOD(kQADrawBitmap, drawBitmap, GXMetalDrawBitmap);
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
        if ((gTransport.features &
             GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) != 0) {
            GXMETAL_REGISTER_METHOD(kQSubmitMultiTextureParams,
                                    submitMultiTextureParams,
                                    GXMetalSubmitMultiTextureParams);
        }
        if ((gTransport.features &
             (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
              GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK)) ==
            (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
             GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK)) {
            GXMETAL_REGISTER_METHOD(kQAccessDrawBuffer, accessDrawBuffer,
                                    GXMetalAccessDrawBuffer);
            GXMETAL_REGISTER_METHOD(kQAccessDrawBufferEnd,
                                    accessDrawBufferEnd,
                                    GXMetalAccessDrawBufferEnd);
        }
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
    uint32_t clipLeft;
    uint32_t clipTop;
    uint32_t clipRight;
    uint32_t clipBottom;
    GXMetalClipWork *regionWork = NULL;
    uint32_t regionRectCount = 0;
    uint32_t i;

    gDiagnostics.draw_private_new_count++;
    gDiagnostics.context_flags = (uint32_t)flags;
    gDiagnostics.context_error = kQANoErr;
    gDiagnosticStatus = kGXMetalDiagnosticCreatingContext;
    GXMetalPersistDiagnostics();
    if (newDrawContext == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticContextInvalidArguments;
        gDiagnostics.context_error = kQAParamErr;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        return kQAParamErr;
    }
    if (!GXMetalTransportAvailable()) {
        gDiagnosticStatus = kGXMetalDiagnosticContextTransportFault;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    if ((flags & (kQAContext_Cache | kQAContext_Scale)) != 0 ||
        ((flags & kQAContext_DeepZ) != 0 &&
         (gTransport.features & GXMETAL_FEATURE_DEEP_Z) == 0) ||
        ((flags & kQAContext_NoZBuffer) == 0 &&
         (gTransport.features & GXMETAL_FEATURE_Z16) == 0) ||
        ((flags & kQAContext_DoubleBuffer) != 0 &&
         (gTransport.features & GXMETAL_FEATURE_DOUBLE_BUFFER) == 0)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextUnsupportedFlags;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    state = (GXMetalDrawState *)NewPtrClear(sizeof(*state));
    if (state == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticContextOutOfMemory;
        gDiagnostics.context_error = kQAOutOfMemory;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        return kQAOutOfMemory;
    }
    if (!GXMetalDescribeDevice(device, rect, &state->width, &state->height,
                               &state->row_bytes, &state->pixel_format,
                               &state->framebuffer_offset)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextDisplayRejected;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        DisposePtr((Ptr)state);
        return kQANotSupported;
    }
    gDiagnostics.context_width = state->width;
    gDiagnostics.context_height = state->height;
    gDiagnostics.context_row_bytes = state->row_bytes;
    gDiagnostics.context_pixel_format = state->pixel_format;
    gDiagnostics.context_framebuffer_offset = state->framebuffer_offset;
    if (!GXMetalDescribeClip(clip, rect, state->width, state->height,
                             &clipLeft, &clipTop, &clipRight, &clipBottom,
                             &regionWork, &regionRectCount)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextClipRejected;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        DisposePtr((Ptr)state);
        return kQANotSupported;
    }
    gDiagnostics.context_clip_left = clipLeft;
    gDiagnostics.context_clip_top = clipTop;
    gDiagnostics.context_clip_right = clipRight;
    gDiagnostics.context_clip_bottom = clipBottom;
    state->transport = &gTransport;
    state->context_id = gNextContextID++;
    if (gNextContextID == 0) {
        gNextContextID = 1;
    }
    state->float_state[kQATag_ColorBG_a] = 1.0f;
    state->float_state[kQATag_Width] = 1.0f;
    state->float_state[kQATag_BitmapScale_x] = 1.0f;
    state->float_state[kQATag_BitmapScale_y] = 1.0f;
    state->float_state[kQATagGL_DepthBG] = 1.0f;
    state->int_state[kQATag_Blend] = kQABlend_Interpolate;
    state->int_state[kQATag_TextureFilter] = kQATextureFilter_Fast;
    (void)gxmetal_rave_filter_preset_to_gl(
        kQATextureFilter_Fast, &state->effective_texture_min_filter,
        &state->effective_texture_mag_filter);
    (void)gxmetal_rave_filter_preset_to_gl(
        kQATextureFilter_Fast,
        &state->effective_secondary_texture_min_filter,
        &state->effective_secondary_texture_mag_filter);
    state->int_state[kQATag_TextureOp] = kQATextureOp_None;
    state->int_state[kQATag_BitmapFilter] = kQAFilter_Fast;
    state->int_state[kQATag_ChannelMask] = kQAChannelMask_r |
                                           kQAChannelMask_g |
                                           kQAChannelMask_b |
                                           kQAChannelMask_a;
    state->int_state[kQATagGL_DrawBuffer] =
        (flags & kQAContext_DoubleBuffer) != 0 ?
            kQAGL_DrawBuffer_BackLeft : kQAGL_DrawBuffer_FrontLeft;
    state->int_state[kQATag_ZBufferMask] = kQAZBufferMask_Enable;
    if ((flags & kQAContext_NoZBuffer) == 0) {
        contextFlags |= (flags & kQAContext_DeepZ) != 0 ?
            GXMETAL_CONTEXT_DEEP_Z : GXMETAL_CONTEXT_Z16;
        state->int_state[kQATag_ZFunction] = kQAZFunction_LT;
    }
    if (flags & kQAContext_DoubleBuffer) {
        contextFlags |= GXMETAL_CONTEXT_DOUBLE_BUFFER;
    }
    if (flags & kQAContext_NoDither) {
        contextFlags |= GXMETAL_CONTEXT_NO_DITHER;
    }
    if (clip != NULL) {
        contextFlags |= GXMETAL_CONTEXT_RECT_CLIP;
    }
    if (regionWork != NULL) {
        contextFlags |= GXMETAL_CONTEXT_REGION_CLIP;
    }
    state->context_flags = contextFlags;

    if (!GXMetalBeginPacket(state, GXMETAL_OP_CONTEXT_CREATE,
                            GXMETAL_CONTEXT_CREATE_PACKET_BYTES, &packet)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextPacketFailed;
        gDiagnostics.context_error = kQAError;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        if (regionWork != NULL) {
            DisposePtr((Ptr)regionWork);
        }
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
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_LEFT_TOP_OFFSET,
                       clipLeft | (clipTop << 16));
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET,
                       clipRight | (clipBottom << 16));
    GXMetalCommitPacket(state, &packet);
    if (regionWork != NULL) {
        uint32_t packetBytes = GXMETAL_CLIP_RECTS_BASE_PACKET_BYTES +
            regionRectCount * GXMETAL_RECT_PAYLOAD_BYTES;

        if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_CLIP_RECTS,
                                packetBytes, &packet)) {
            DisposePtr((Ptr)regionWork);
            gDiagnosticStatus = kGXMetalDiagnosticContextPacketFailed;
            gDiagnostics.context_error = kQAError;
            GXMetalPublishDiagnostics();
            GXMetalPersistDiagnostics();
            DisposePtr((Ptr)state);
            return kQAError;
        }
        payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
        gxmetal_store_le32(payload + GXMETAL_CLIP_RECTS_COUNT_OFFSET,
                           regionRectCount);
        gxmetal_store_le32(payload + GXMETAL_CLIP_RECTS_RESERVED0_OFFSET, 0);
        gxmetal_store_le32(payload + GXMETAL_CLIP_RECTS_RESERVED1_OFFSET, 0);
        gxmetal_store_le32(payload + GXMETAL_CLIP_RECTS_RESERVED2_OFFSET, 0);
        for (i = 0; i < regionRectCount; i++) {
            uint8_t *wireRect = payload + GXMETAL_CLIP_RECTS_RECTS_OFFSET +
                i * GXMETAL_RECT_PAYLOAD_BYTES;
            GXMetalClipRect *sourceRect = &regionWork->rects[i];

            gxmetal_store_le32(wireRect + GXMETAL_RECT_LEFT_OFFSET,
                               sourceRect->left);
            gxmetal_store_le32(wireRect + GXMETAL_RECT_TOP_OFFSET,
                               sourceRect->top);
            gxmetal_store_le32(wireRect + GXMETAL_RECT_RIGHT_OFFSET,
                               sourceRect->right);
            gxmetal_store_le32(wireRect + GXMETAL_RECT_BOTTOM_OFFSET,
                               sourceRect->bottom);
        }
        GXMetalCommitPacket(state, &packet);
        DisposePtr((Ptr)regionWork);
        regionWork = NULL;
    }
    if (gxmetal_guest_register_read(state->transport, GXMETAL_REG_STATUS) !=
        GXMETAL_STATUS_READY) {
        gDiagnosticStatus = kGXMetalDiagnosticContextTransportFault;
        gDiagnostics.context_error = kQAError;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        DisposePtr((Ptr)state);
        return kQAError;
    }

    newDrawContext->drawPrivate = (TQADrawPrivate *)state;
    state->draw_context = newDrawContext;
    error = GXMetalRegisterMethods(newDrawContext);
    if (error != kQANoErr) {
        gDiagnosticStatus = kGXMetalDiagnosticContextMethodFailed;
        gDiagnostics.context_error = error;
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
        if (GXMetalBeginPacket(state, GXMETAL_OP_CONTEXT_DESTROY,
                               GXMETAL_PACKET_HEADER_BYTES, &packet)) {
            GXMetalCommitPacket(state, &packet);
        }
        newDrawContext->drawPrivate = NULL;
        DisposePtr((Ptr)state);
        return error;
    }
    state->pending_vertices = NewPtr(
        (Size)(GXMETAL_DRAW_BATCH_VERTICES * sizeof(TQAVTexture)));
    state->next_state = gDrawStates;
    gDrawStates = state;
    gLastDrawContext = newDrawContext;
    gDiagnostics.draw_private_new_success_count++;
    gDiagnosticStatus = kGXMetalDiagnosticContextReady;
    GXMetalPublishDiagnostics();
    GXMetalPersistDiagnostics();
    return kQANoErr;
}

static void GXMetalDrawPrivateDelete(TQADrawPrivate *drawPrivate)
{
    GXMetalDrawState *state = (GXMetalDrawState *)drawPrivate;
    GXMetalDrawState **link;
    GXMetalGuestPacket packet;

    if (state == NULL) {
        return;
    }
    gDiagnostics.draw_private_delete_count++;
    (void)GXMetalFlushPendingDraws(state);
    if (!state->failed && GXMetalBeginPacket(state,
            GXMETAL_OP_CONTEXT_DESTROY, GXMETAL_PACKET_HEADER_BYTES,
            &packet)) {
        GXMetalCommitPacket(state, &packet);
    }
    if (state->submitted_gouraud_vertices != NULL) {
        DisposePtr(state->submitted_gouraud_vertices);
    }
    if (state->submitted_texture_vertices != NULL) {
        DisposePtr(state->submitted_texture_vertices);
    }
    if (state->submitted_multitexture_params != NULL) {
        DisposePtr(state->submitted_multitexture_params);
    }
    if (state->pending_vertices != NULL) {
        DisposePtr(state->pending_vertices);
    }
    if (gLastDrawContext == state->draw_context) {
        gLastDrawContext = NULL;
    }
    for (link = &gDrawStates; *link != NULL; link = &(*link)->next_state) {
        if (*link == state) {
            *link = state->next_state;
            break;
        }
    }
    DisposePtr((Ptr)state);
    GXMetalPublishDiagnostics();
    GXMetalPersistDiagnostics();
}

static TQAError GXMetalEngineCheckDevice(const TQADevice *device)
{
    TQARect rect;
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint32_t pixelFormat;
    uint32_t framebufferOffset;

    gDiagnosticStatus = kGXMetalDiagnosticCheckingDevice;
    gDiagnostics.check_device_count++;
    GXMetalPersistDiagnostics();
    if (device == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    if (!GXMetalTransportAvailable()) {
        gDiagnosticStatus = kGXMetalDiagnosticTransportUnavailable;
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    if (device->deviceType == kQADeviceGDevice) {
        GDHandle graphicsDevice = device->device.gDevice;
        PixMapHandle pixmap;
        if (graphicsDevice == NULL || *graphicsDevice == NULL) {
            gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
            GXMetalPersistDiagnostics();
            return kQANotSupported;
        }
        pixmap = (**graphicsDevice).gdPMap;
        if (pixmap == NULL || *pixmap == NULL) {
            gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
            GXMetalPersistDiagnostics();
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
        gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    if (!GXMetalDescribeDevice(device, &rect, &width, &height, &rowBytes,
                               &pixelFormat, &framebufferOffset)) {
        gDiagnosticStatus = kGXMetalDiagnosticDisplayRejected;
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    gDiagnosticStatus = kGXMetalDiagnosticDeviceAccepted;
    GXMetalPersistDiagnostics();
    return kQANoErr;
}

static TQAError GXMetalEngineGestalt(TQAGestaltSelector selector,
                                     void *response)
{
    uint32_t value;
    uint64_t features = GXMetalTransportAvailable() ? gTransport.features : 0;

    gDiagnostics.gestalt_count++;
    gDiagnostics.last_gestalt_selector = (uint32_t)selector;
    GXMetalPersistDiagnostics();
    if (response == NULL) {
        GXMetalPersistDiagnostics();
        return kQAParamErr;
    }
    switch ((uint32_t)selector) {
    case kQAGestalt_OptionalFeatures:
        value = kQAOptional_BoundToDevice | kQAOptional_NoDither |
                kQAOptional_ClearDrawBuffer | kQAOptional_OpenGL |
                kQAOptional_PerspectiveZ;
        if (features & GXMETAL_FEATURE_BLEND) {
            value |= kQAOptional_Blend | kQAOptional_BlendAlpha;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAOptional_Texture | kQAOptional_TextureHQ |
                     kQAOptional_TextureColor | kQAOptional_CL4 |
                     kQAOptional_CL8;
        }
        if (features & GXMETAL_FEATURE_Z16) {
            value |= kQAOptional_ZBufferMask | kQAOptional_ClearZBuffer;
        }
        if (features & GXMETAL_FEATURE_WRITE_MASKS) {
            value |= kQAOptional_ChannelMask;
        }
        if (features & GXMETAL_FEATURE_FOG_DEPTH) {
            value |= kQAOptional_FogDepth;
        }
        if (features & GXMETAL_FEATURE_ALPHA_TEST) {
            value |= kQAOptional_AlphaTest;
        }
        if (features & GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) {
            value |= kQAOptional_MultiTextures;
        }
        if (features & GXMETAL_FEATURE_RESOURCE_SUBREGION) {
            value |= kQAOptional_AccessTexture |
                     kQAOptional_AccessBitmap;
        }
        if ((features &
             (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
              GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK)) ==
            (GXMETAL_FEATURE_ACCESS_DRAW_BUFFER |
             GXMETAL_FEATURE_DRAW_BUFFER_WRITEBACK)) {
            value |= kQAOptional_AccessDrawBuffer;
        }
        break;
    case kQAGestalt_FastFeatures:
        value = kQAFast_Line | kQAFast_Gouraud;
        if (features & GXMETAL_FEATURE_BLEND) {
            value |= kQAFast_Blend;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAFast_Texture | kQAFast_TextureHQ |
                     kQAFast_CL4 | kQAFast_CL8;
        }
        if (features & GXMETAL_FEATURE_FOG_DEPTH) {
            value |= kQAFast_FogDepth;
        }
        if (features & GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) {
            value |= kQAFast_MultiTextures;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAFast_BitmapScale;
        }
        break;
    case kQAGestalt_TextureMemory:
    case kQAGestalt_FastTextureMemory:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            GXMETAL_TEXTURE_MEMORY : 0;
        break;
    case GXMETAL_ATI_GESTALT_BOARD_MEMORY:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            GXMETAL_TEXTURE_MEMORY : 0;
        break;
    case GXMETAL_ATI_GESTALT_ENGINE_METHODS:
        value = (uint32_t)(uintptr_t)gGXMetalATIEngineMethods;
        break;
    case GXMETAL_ATI_GESTALT_TEXTURE_FLAGS:
        value = 0;
        break;
    case kQAGestalt_MultiTextureMax:
        value = (features & GXMETAL_FEATURE_MULTI_TEXTURE_VERTEX) ? 1u : 0u;
        break;
    case kQAGestalt_OptionalFeatures2:
        value = (features & GXMETAL_FEATURE_DOUBLE_BUFFER) ?
            kQAOptional2_SwapBuffers : 0;
        if (features & GXMETAL_FEATURE_CHROMAKEY) {
            value |= kQAOptional2_Chromakey;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAOptional2_FlipOrigin;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAOptional2_BitmapScale;
        }
        break;
    case kQAGestalt_VendorID:
        /* Register under GXMetal's stable identity, then expose ATI's legacy
         * vendor ID to the system GLD so OpenGLRendererATI can bridge classic
         * OpenGL clients to this RAVE engine. */
        value = gRegistrationVendorPending ?
            GXMETAL_REGISTRATION_VENDOR_ID : GXMETAL_LEGACY_VENDOR_ID;
        gRegistrationVendorPending = 0;
        break;
    case kQAGestalt_EngineID:
        value = GXMETAL_ATI_ENGINE_ID;
        break;
    case kQAGestalt_Revision:
        value = GXMETAL_PRODUCT_REVISION;
        break;
    case kQAGestalt_ASCIINameLength:
        value = (uint32_t)strlen(kGXMetalName);
        break;
    case kQAGestalt_ASCIIName:
        strcpy((char *)response, kGXMetalName);
        GXMetalPersistDiagnostics();
        return kQANoErr;
    case kQAGestalt_DrawContextPixelTypesAllowed:
    case kQAGestalt_DrawContextPixelTypesPreferred:
        value = (UINT32_C(1) << kQAPixel_RGB16) |
                (UINT32_C(1) << kQAPixel_RGB32) |
                (UINT32_C(1) << kQAPixel_ARGB32);
        break;
    case kQAGestalt_TexturePixelTypesAllowed:
    case kQAGestalt_TexturePixelTypesPreferred:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            (UINT32_C(1) << kQAPixel_RGB16) |
            (UINT32_C(1) << kQAPixel_ARGB16) |
            (UINT32_C(1) << kQAPixel_RGB32) |
            (UINT32_C(1) << kQAPixel_ARGB32) |
            (UINT32_C(1) << kQAPixel_CL4) |
            (UINT32_C(1) << kQAPixel_CL8) |
            (UINT32_C(1) << kQAPixel_ARGB16_4444) : 0;
        if (features & GXMETAL_FEATURE_INTENSITY_FORMATS) {
            value |= (UINT32_C(1) << kQAPixel_I8) |
                     (UINT32_C(1) << kQAPixel_AI16_88);
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= UINT32_C(1) << kQAPixel_ACL16_88;
        }
        if (features & GXMETAL_FEATURE_ALPHA1_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_Alpha1;
        }
        if (features & GXMETAL_FEATURE_RGB332_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_RGB8_332;
        }
        if (features & GXMETAL_FEATURE_RGB24_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_RGB24;
        }
        break;
    case kQAGestalt_BitmapPixelTypesAllowed:
    case kQAGestalt_BitmapPixelTypesPreferred:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            (UINT32_C(1) << kQAPixel_RGB16) |
            (UINT32_C(1) << kQAPixel_RGB32) |
            (UINT32_C(1) << kQAPixel_ARGB32) |
            (UINT32_C(1) << kQAPixel_CL4) |
            (UINT32_C(1) << kQAPixel_CL8) : 0;
        if (features & GXMETAL_FEATURE_INTENSITY_FORMATS) {
            value |= (UINT32_C(1) << kQAPixel_I8) |
                     (UINT32_C(1) << kQAPixel_AI16_88);
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= UINT32_C(1) << kQAPixel_ACL16_88;
        }
        if (features & GXMETAL_FEATURE_ALPHA1_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_Alpha1;
        }
        if (features & GXMETAL_FEATURE_RGB332_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_RGB8_332;
        }
        if (features & GXMETAL_FEATURE_RGB24_FORMAT) {
            value |= UINT32_C(1) << kQAPixel_RGB24;
        }
        break;
    default:
        GXMetalPersistDiagnostics();
        return kQAGestaltUnknown;
    }
    *(uint32_t *)response = value;
    GXMetalPersistDiagnostics();
    return kQANoErr;
}

TQAError GXMetalEngineGetMethod(TQAEngineMethodTag methodTag,
                                TQAEngineMethod *method)
{
    gDiagnostics.get_method_count++;
    if ((uint32_t)methodTag < 32) {
        gDiagnostics.method_mask |= UINT32_C(1) << (uint32_t)methodTag;
    }
    GXMetalPersistDiagnostics();
    if (method == NULL) {
        GXMetalPersistDiagnostics();
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
    case kQATextureNew:
        method->textureNew = GXMetalTextureNew;
        break;
    case kQATextureDetach:
        method->textureDetach = GXMetalTextureDetach;
        break;
    case kQATextureDelete:
        method->textureDelete = GXMetalTextureDelete;
        break;
    case kQABitmapNew:
        method->bitmapNew = GXMetalBitmapNew;
        break;
    case kQABitmapDetach:
        method->bitmapDetach = GXMetalBitmapDetach;
        break;
    case kQABitmapDelete:
        method->bitmapDelete = GXMetalBitmapDelete;
        break;
    case kQAColorTableNew:
        method->colorTableNew = GXMetalColorTableNew;
        break;
    case kQAColorTableDelete:
        method->colorTableDelete = GXMetalColorTableDelete;
        break;
    case kQATextureBindColorTable:
        method->textureBindColorTable = GXMetalTextureBindColorTable;
        break;
    case kQABitmapBindColorTable:
        method->bitmapBindColorTable = GXMetalBitmapBindColorTable;
        break;
    case kQAAccessTexture:
        if (!GXMetalTransportAvailable() ||
            (gTransport.features &
             GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
            return kQANotSupported;
        }
        method->accessTexture = GXMetalAccessTexture;
        break;
    case kQAAccessTextureEnd:
        if (!GXMetalTransportAvailable() ||
            (gTransport.features &
             GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
            return kQANotSupported;
        }
        method->accessTextureEnd = GXMetalAccessTextureEnd;
        break;
    case kQAAccessBitmap:
        if (!GXMetalTransportAvailable() ||
            (gTransport.features &
             GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
            return kQANotSupported;
        }
        method->accessBitmap = GXMetalAccessBitmap;
        break;
    case kQAAccessBitmapEnd:
        if (!GXMetalTransportAvailable() ||
            (gTransport.features &
             GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
            return kQANotSupported;
        }
        method->accessBitmapEnd = GXMetalAccessBitmapEnd;
        break;
    default:
        GXMetalPersistDiagnostics();
        return kQANotSupported;
    }
    GXMetalPersistDiagnostics();
    return kQANoErr;
}

OSErr GXMetalCFMInitialize(const CFragInitBlock *initBlock)
{
    TQAError error;

    (void)initBlock;
    memset(&gDiagnostics, 0, sizeof(gDiagnostics));
    gDiagnostics.magic = GXMETAL_DIAGNOSTIC_MAGIC;
    gDiagnostics.version = GXMETAL_DIAGNOSTIC_VERSION;
    gDiagnostics.initialize_count++;
    gDiagnosticStatus = kGXMetalDiagnosticInitializing;
    gRegistrationVendorPending = 1;
    GXMetalPersistDiagnostics();
    error = QARegisterEngine(GXMetalEngineGetMethod);
    gDiagnostics.registration_error = error;
    gDiagnosticStatus = error == kQANoErr ?
        kGXMetalDiagnosticRegistered : kGXMetalDiagnosticRegistrationFailed;
    GXMetalPublishDiagnostics();
    GXMetalPersistDiagnostics();
    return (OSErr)error;
}
