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
#include "GXMetalTransport.h"
#include "GXMetalVersion.h"

#define GXMETAL_REGISTRATION_VENDOR_ID UINT32_C(0x47584d54) /* GXMT */
#define GXMETAL_LEGACY_VENDOR_ID UINT32_C(1) /* kQAVendor_ATI */
#define GXMETAL_ENGINE_ID UINT32_C(0x00000001)
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
#define GXMETAL_ATI_PRIVATE_ENABLE_TAG UINT32_C(1020)
#define GXMETAL_ATI_PRIVATE_METHODS_TAG UINT32_C(1021)
#define GXMETAL_ATI_PIXEL_RGB16 ((TQAImagePixelType)1001)
#define GXMETAL_ATI_GESTALT_BOARD_MEMORY ((TQAGestaltSelector)1001)
#define GXMETAL_ATI_GESTALT_ENGINE_METHODS ((TQAGestaltSelector)1002)
#define GXMETAL_ATI_GESTALT_TEXTURE_FLAGS ((TQAGestaltSelector)1005)

typedef struct GXMetalDrawState {
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
    uint32_t ati_private_frame_started;
    uint32_t ati_private_frame_has_draws;
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
static TQABoolean gRegistrationVendorPending;
static int32_t gDiagnosticStatus = kGXMetalDiagnosticNotLoaded;
static GXMetalDiagnosticSnapshot gDiagnostics = {
    .magic = GXMETAL_DIAGNOSTIC_MAGIC,
    .version = GXMETAL_DIAGNOSTIC_VERSION
};

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
    switch (pixelType) {
    case kQAPixel_Alpha1:
        /* Despite the historical name, Apple Software RAVE stores one byte
         * per Alpha1 texel. The host reduces that byte to transparent/opaque
         * while supplying the format's neutral white RGB channels. */
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
         (gTransport.features & GXMETAL_FEATURE_RGB332_FORMAT) == 0)) {
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
        if (indexed) {
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
            texture->live_pixels[level] = images[level].pixmap;
        }
        memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
               texture->source_pixels[level],
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
    if (pixelType == GXMETAL_ATI_PIXEL_RGB16) {
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
    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC) {
        return;
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
    uint64_t length;
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
         (gTransport.features & GXMETAL_FEATURE_RGB332_FORMAT) == 0)) {
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
    length = (uint64_t)rowBytes * height;
    if (rowBytes < (indexed ?
            GXMetalPaletteMinimumRowBytes(pixelType, width) :
            width * bytesPerPixel) || length > GXMETAL_UPLOAD_BYTES) {
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
                       indexed ? width * 4 : rowBytes);
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

    bitmap->source_pixels = NewPtr((Size)length);
    if (bitmap->source_pixels == NULL) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr((Ptr)bitmap);
        gDiagnostics.last_bitmap_error = kQAOutOfMemory;
        return kQAOutOfMemory;
    }
    bitmap->source_row_bytes = rowBytes;
    memcpy(bitmap->source_pixels, image->pixmap, (size_t)length);
    if (indexed) {
        *newBitmap = bitmap;
        gDiagnostics.last_bitmap_error = kQANoErr;
        GXMetalCountPixelType(gDiagnostics.bitmap_new_success_by_type,
                              pixelType);
        return kQANoErr;
    }

    memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
           bitmap->source_pixels, (size_t)length);
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr(bitmap->source_pixels);
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
                       (uint32_t)length);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET, rowBytes);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_UPLOAD_HEIGHT_OFFSET, height);
    if (!GXMetalCommitUploadPacket(&packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr(bitmap->source_pixels);
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

    if (texture == NULL || texture->magic != GXMETAL_TEXTURE_MAGIC ||
        buffer == NULL || mipmapLevel < 0 ||
        (uint32_t)mipmapLevel >= texture->levels ||
        (flags & ~kQANoCopyNeeded) != 0 || texture->access_active != 0 ||
        (texture->source_flags & kQATexture_NoCopy) != 0 ||
        !GXMetalTextureFormat((TQAImagePixelType)texture->source_pixel_type,
                              &format, &bytesPerPixel) ||
        format != texture->pixel_format ||
        texture->source_pixels[mipmapLevel] == NULL ||
        texture->source_row_bytes[mipmapLevel] < bytesPerPixel ||
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

    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC ||
        buffer == NULL || (flags & ~kQANoCopyNeeded) != 0 ||
        bitmap->access_active != 0 ||
        !GXMetalTextureFormat((TQAImagePixelType)bitmap->source_pixel_type,
                              &format, &bytesPerPixel) ||
        format != bitmap->pixel_format || bitmap->source_pixels == NULL ||
        bitmap->source_row_bytes < bytesPerPixel ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_RESOURCE_SUBREGION) == 0) {
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

static TQABoolean GXMetalDescribeClip(const TQAClip *clip,
                                      const TQARect *rect,
                                      uint32_t width, uint32_t height,
                                      uint32_t *left, uint32_t *top,
                                      uint32_t *right, uint32_t *bottom)
{
    RgnHandle region;
    Rect bounds;
    int32_t clippedLeft;
    int32_t clippedTop;
    int32_t clippedRight;
    int32_t clippedBottom;

    *left = 0;
    *top = 0;
    *right = width;
    *bottom = height;
    if (clip == NULL) {
        return 1;
    }
    if (clip->clipType != kQAClipRgn ||
        (gTransport.features & GXMETAL_FEATURE_RECT_CLIP) == 0) {
        return 0;
    }
    region = clip->clip.clipRgn;
    if (region == NULL || *region == NULL ||
        (**region).rgnSize != sizeof(Region)) {
        return 0;
    }
    bounds = (**region).rgnBBox;
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

static TQABoolean GXMetalFlushPendingDraws(GXMetalDrawState *state);

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
        (void)GXMetalEmitState(state, GXMETAL_STATE_ATI_PRIVATE,
                               GXMETAL_STATE_UINT32, enabled);
        gDiagnostics.draw_method_stage = 111;
        return;
    }
    if (state == NULL || (uint32_t)tag >= GXMETAL_STATE_SLOTS) {
        return;
    }
    if ((uint32_t)tag == kQATag_MultiTextureEnable && newValue > 1u) {
        /* GXMetal advertises one secondary stage. Ignore probes for later
         * stages without poisoning an otherwise valid draw context. */
        return;
    }
    if (state->int_state_valid[(uint32_t)tag] &&
        state->int_state[(uint32_t)tag] == (uint32_t)newValue) {
        return;
    }
    if (!GXMetalFlushPendingDraws(state)) {
        return;
    }
    state->int_state[(uint32_t)tag] = (uint32_t)newValue;
    state->int_state_valid[(uint32_t)tag] = 1;
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
    gDiagnostics.draw_method_stage = 140;
    gDiagnostics.get_int_count++;
    gDiagnostics.last_get_int_tag = (uint32_t)tag;
    if (state != NULL &&
        (uint32_t)tag == GXMETAL_ATI_PRIVATE_ENABLE_TAG) {
        gDiagnostics.last_get_int_value = state->ati_private_enabled;
        gDiagnostics.draw_method_stage = 141;
        return state->ati_private_enabled;
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

#define GXMETAL_ATI_PRIVATE_METHOD_COUNT 64u

static TQABoolean GXMetalDiagnosticMemoryRangeIsReadable(uint32_t address,
                                                         uint32_t byteCount)
{
    return address >= UINT32_C(0x00100000) &&
        address < UINT32_C(0x20000000) &&
        byteCount <= UINT32_C(0x20000000) - address;
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
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    (void)arg5; (void)arg6; (void)arg7;
    gDiagnostics.draw_method_stage = 271;
    if (state == NULL) {
        return kQAParamErr;
    }
    if ((state->context_flags & GXMETAL_CONTEXT_Z16) == 0) {
        return kQANotSupported;
    }
    return GXMetalEmitClear(state, NULL, GXMETAL_CLEAR_DEPTH);
}

/* OpenGLRendererATI retains this private table after context creation and
 * older ATI engines expose considerably more than the two clear hooks used
 * by Carmageddon II.  Keep a conservatively sized table so an optional ATI
 * hook never walks into unrelated data.  Unidentified entries intentionally
 * report success without touching guest or host state; all actual rendering
 * still arrives through the public RAVE methods that GXMetal implements. */
#define GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(index)                             \
    static TQAError GXMetalATIPrivateMethod##index(                         \
        uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3,         \
        uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7)         \
    {                                                                       \
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
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(20)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(21)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(22)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(25)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(26)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(27)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(28)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(29)
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
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(41)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(42)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(43)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(44)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(45)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(46)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(47)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(49)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(50)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(51)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(52)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(53)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(55)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(56)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(57)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(58)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(59)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(61)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(62)
GXMETAL_ATI_PRIVATE_NOOP_WRAPPER(63)

/* OpenGLRendererATI passes the selected primary RAVE texture in register r9
 * (the seventh argument) to private hook 16 before submitting geometry. */
static TQAError GXMetalATIPrivateMethod16(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    const TQATexture *texture = (const TQATexture *)(uintptr_t)arg6;
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
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;

    if (state == NULL || state->failed) {
        return kQANoErr;
    }
    if (state->ati_private_frame_started) {
        if (state->ati_private_frame_has_draws &&
            (!GXMetalFlushPendingDraws(state) ||
             GXMetalEmitRect(state, GXMETAL_OP_PRESENT, NULL) != kQANoErr)) {
            return kQANoErr;
        }
        state->ati_private_frame_started = 0;
        state->ati_private_frame_has_draws = 0;
    }
    if (GXMetalEmitClear(
            state, NULL,
            GXMETAL_CLEAR_COLOR |
                ((state->context_flags & GXMETAL_CONTEXT_Z16) != 0 ?
                     GXMETAL_CLEAR_DEPTH : 0)) != kQANoErr) {
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
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    (void)arg0; (void)arg1; (void)arg2; (void)arg3;
    (void)arg4; (void)arg5; (void)arg6; (void)arg7;

    if (state != NULL && !state->failed && state->ati_private_frame_started) {
        if (state->ati_private_frame_has_draws &&
            (!GXMetalFlushPendingDraws(state) ||
             GXMetalEmitRect(state, GXMETAL_OP_PRESENT, NULL) != kQANoErr)) {
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
        if (GXMetalEmitClear(
                state, NULL,
                GXMETAL_CLEAR_COLOR |
                    ((state->context_flags & GXMETAL_CONTEXT_Z16) != 0 ?
                         GXMETAL_CLEAR_DEPTH : 0)) != kQANoErr) {
            return false;
        }
        state->ati_private_frame_started = 1;
        state->ati_private_frame_has_draws = 0;
    }
    return true;
}

static TQABoolean GXMetalATIPrivateQueueTriangle(
    GXMetalDrawState *state, const TQAVTexture triangle[3])
{
    TQAVGouraud gouraud[3];
    TQABoolean queued;
    uint32_t vertexIndex;

    if (state->texture != NULL) {
        /* OpenGLRendererATI supplies homogeneous, normalized coordinates in
         * private hooks 48, 54, and 60. Texel-coordinate detection belongs to
         * the older public ATI RAVE path and only adds per-triangle floating
         * point work here. */
        queued = GXMetalQueueTextureTriangle(state, triangle, 0, 0);
        if (queued) {
            state->ati_private_frame_has_draws = 1;
        }
        return queued;
    }
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
    return queued;
}

/* OpenGLRendererATI uses hook 48 for a contiguous triangle list.  Each
 * transformed vertex occupies 128 bytes and uses the same layout as the
 * individually addressed vertices passed to hook 60.  This path carries a
 * substantial portion of Quake III's clipped world geometry; treating it as
 * a boundary-only callback leaves the context clear color visible through
 * otherwise valid walls and sky surfaces. */
static TQAError GXMetalATIPrivateMethod48(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    TQAVTexture triangle[3];
    uint32_t vertexIndex;
    uint32_t triangleIndex;
    (void)arg0; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;

    if (state == NULL || state->failed || arg2 < 3u || arg2 > 4096u ||
        !GXMetalDiagnosticMemoryRangeIsReadable(arg1, arg2 * 128u)) {
        return kQANoErr;
    }
    if (!GXMetalATIPrivatePrepareDraw(state)) {
        return kQANoErr;
    }
    for (triangleIndex = 0; triangleIndex + 2u < arg2;
         triangleIndex += 3u) {
        for (vertexIndex = 0; vertexIndex < 3u; ++vertexIndex) {
            if (!GXMetalATIPrivateConvertVertex(
                    arg1 + (triangleIndex + vertexIndex) * 128u,
                    &triangle[vertexIndex])) {
                return kQANoErr;
            }
        }
        if (!GXMetalATIPrivateQueueTriangle(state, triangle)) {
            return kQANoErr;
        }
    }
    return kQANoErr;
}

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

/* Hook 54 is OpenGLRendererATI's contiguous convex-fan path. Quake III uses
 * it for transformed screen-space quads such as transparent overlays. The
 * vertices have the same 128-byte layout as hooks 48 and 60; a four-vertex
 * call becomes triangles 0-1-2 and 0-2-3. */
static TQAError GXMetalATIPrivateMethod54(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    TQAVTexture triangle[3];
    uint32_t triangleIndex;
    (void)arg0; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;

    if (state == NULL || state->failed || arg2 < 3u || arg2 > 4096u ||
        !GXMetalDiagnosticMemoryRangeIsReadable(arg1, arg2 * 128u)) {
        return kQANoErr;
    }
    if (!GXMetalATIPrivateConvertVertex(arg1, &triangle[0]) ||
        !GXMetalATIPrivateConvertVertex(arg1 + 128u, &triangle[1]) ||
        !GXMetalATIPrivatePrepareDraw(state)) {
        return kQANoErr;
    }

    for (triangleIndex = 0; triangleIndex + 2u < arg2;
         ++triangleIndex) {
        if (!GXMetalATIPrivateConvertVertex(
                arg1 + (triangleIndex + 2u) * 128u, &triangle[2])) {
            return kQANoErr;
        }
        if (!GXMetalATIPrivateQueueTriangle(state, triangle)) {
            return kQANoErr;
        }
        triangle[1] = triangle[2];
    }
    return kQANoErr;
}

/* OpenGLRendererATI's hot draw hook receives an array of pointers to its
 * 128-byte transformed vertices.  Translate the primary stage into ordinary
 * RAVE vertices so the common GXMetal packet path can render it.  The input
 * is a convex fan and world surfaces can contain far more than the small
 * triangles and quads seen during initial reverse engineering.  Stream each
 * fan triangle directly instead of dropping the entire polygon or allocating
 * a temporary array in this hot path. */
static TQAError GXMetalATIPrivateMethod60(uint32_t arg0, uint32_t arg1,
                                          uint32_t arg2, uint32_t arg3,
                                          uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7)
{
    const uint32_t *vertexPointers;
    GXMetalDrawState *state = GXMetalGetState(gLastDrawContext);
    TQAVTexture triangle[3];
    uint32_t triangleIndex;
    (void)arg0; (void)arg3; (void)arg4; (void)arg5;
    (void)arg6; (void)arg7;

    if (state == NULL || state->failed) {
        return kQANoErr;
    }
    if (arg2 < 3u || arg2 > 4096u ||
        !GXMetalDiagnosticMemoryRangeIsReadable(
            arg1, arg2 * (uint32_t)sizeof(*vertexPointers))) {
        return kQANoErr;
    }
    vertexPointers = (const uint32_t *)(uintptr_t)arg1;
    if (!GXMetalATIPrivateConvertVertex(vertexPointers[0], &triangle[0]) ||
        !GXMetalATIPrivateConvertVertex(vertexPointers[1], &triangle[1])) {
        return kQANoErr;
    }

    if (!GXMetalATIPrivatePrepareDraw(state)) {
        return kQANoErr;
    }

    for (triangleIndex = 0; triangleIndex + 2u < arg2;
         ++triangleIndex) {
        if (!GXMetalATIPrivateConvertVertex(
                vertexPointers[triangleIndex + 2u], &triangle[2])) {
            return kQANoErr;
        }
        if (!GXMetalATIPrivateQueueTriangle(state, triangle)) {
            return kQANoErr;
        }
        triangle[1] = triangle[2];
    }
    return kQANoErr;
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
    (void)GXMetalEmitState(state, kQATag_TextureFilter, GXMETAL_STATE_UINT32,
                           state->int_state[kQATag_TextureFilter]);
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
        if (state->context_flags & GXMETAL_CONTEXT_Z16) {
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
        (state->context_flags & GXMETAL_CONTEXT_Z16) == 0) {
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

    gDiagnostics.draw_private_new_count++;
    gDiagnostics.context_flags = (uint32_t)flags;
    gDiagnostics.context_error = kQANoErr;
    gDiagnosticStatus = kGXMetalDiagnosticCreatingContext;
    GXMetalPersistDiagnostics();
    if (newDrawContext == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticContextInvalidArguments;
        gDiagnostics.context_error = kQAParamErr;
        GXMetalPublishDiagnostics();
        return kQAParamErr;
    }
    if (!GXMetalTransportAvailable()) {
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        return kQANotSupported;
    }
    if ((flags & (kQAContext_DeepZ | kQAContext_Cache |
                  kQAContext_Scale)) != 0 ||
        ((flags & kQAContext_NoZBuffer) == 0 &&
         (gTransport.features & GXMETAL_FEATURE_Z16) == 0) ||
        ((flags & kQAContext_DoubleBuffer) != 0 &&
         (gTransport.features & GXMETAL_FEATURE_DOUBLE_BUFFER) == 0)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextUnsupportedFlags;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        return kQANotSupported;
    }
    state = (GXMetalDrawState *)NewPtrClear(sizeof(*state));
    if (state == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticContextOutOfMemory;
        gDiagnostics.context_error = kQAOutOfMemory;
        GXMetalPublishDiagnostics();
        return kQAOutOfMemory;
    }
    if (!GXMetalDescribeDevice(device, rect, &state->width, &state->height,
                               &state->row_bytes, &state->pixel_format,
                               &state->framebuffer_offset)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextDisplayRejected;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
        DisposePtr((Ptr)state);
        return kQANotSupported;
    }
    gDiagnostics.context_width = state->width;
    gDiagnostics.context_height = state->height;
    gDiagnostics.context_row_bytes = state->row_bytes;
    gDiagnostics.context_pixel_format = state->pixel_format;
    gDiagnostics.context_framebuffer_offset = state->framebuffer_offset;
    if (!GXMetalDescribeClip(clip, rect, state->width, state->height,
                             &clipLeft, &clipTop, &clipRight, &clipBottom)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextClipRejected;
        gDiagnostics.context_error = kQANotSupported;
        GXMetalPublishDiagnostics();
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
    state->int_state[kQATag_TextureOp] = kQATextureOp_None;
    state->int_state[kQATag_BitmapFilter] = kQAFilter_Fast;
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
    if (clip != NULL) {
        contextFlags |= GXMETAL_CONTEXT_RECT_CLIP;
    }
    state->context_flags = contextFlags;

    if (!GXMetalBeginPacket(state, GXMETAL_OP_CONTEXT_CREATE,
                            GXMETAL_CONTEXT_CREATE_PACKET_BYTES, &packet)) {
        gDiagnosticStatus = kGXMetalDiagnosticContextPacketFailed;
        gDiagnostics.context_error = kQAError;
        GXMetalPublishDiagnostics();
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
    if (gxmetal_guest_register_read(state->transport, GXMETAL_REG_STATUS) !=
        GXMETAL_STATUS_READY) {
        gDiagnosticStatus = kGXMetalDiagnosticContextTransportFault;
        gDiagnostics.context_error = kQAError;
        GXMetalPublishDiagnostics();
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
        newDrawContext->drawPrivate = NULL;
        DisposePtr((Ptr)state);
        return error;
    }
    state->pending_vertices = NewPtr(
        (Size)(GXMETAL_DRAW_BATCH_VERTICES * sizeof(TQAVTexture)));
    gLastDrawContext = newDrawContext;
    gDiagnosticStatus = kGXMetalDiagnosticContextReady;
    GXMetalPublishDiagnostics();
    GXMetalPersistDiagnostics();
    return kQANoErr;
}

static void GXMetalDrawPrivateDelete(TQADrawPrivate *drawPrivate)
{
    GXMetalDrawState *state = (GXMetalDrawState *)drawPrivate;
    GXMetalGuestPacket packet;

    if (state == NULL) {
        return;
    }
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
        value = GXMETAL_ENGINE_ID;
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
