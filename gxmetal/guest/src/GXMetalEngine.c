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
#include "GXMetalTransport.h"

#define GXMETAL_REGISTRATION_VENDOR_ID UINT32_C(0x47584d54) /* GXMT */
#define GXMETAL_LEGACY_VENDOR_ID UINT32_C(1) /* kQAVendor_ATI */
#define GXMETAL_ENGINE_ID UINT32_C(0x00000001)
#define GXMETAL_REVISION  UINT32_C(0x00010400)
#define GXMETAL_STATE_SLOTS 154u
#define GXMETAL_SYNC_SPINS UINT32_C(10000000)
#define GXMETAL_TEXTURE_MAGIC UINT32_C(0x47585458) /* GXTX */
#define GXMETAL_BITMAP_MAGIC UINT32_C(0x4758424d) /* GXBM */
#define GXMETAL_COLOR_TABLE_MAGIC UINT32_C(0x47584354) /* GXCT */
#define GXMETAL_TEXTURE_MEMORY UINT32_C(0x04000000)
#define GXMETAL_MAX_MIP_LEVELS 15u
#define GXMETAL_ATI_PRIVATE_ENABLE_TAG UINT32_C(1020)
#define GXMETAL_ATI_PRIVATE_METHODS_TAG UINT32_C(1021)

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
    uint32_t ati_private_enabled;
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
    uint32_t source_row_bytes[GXMETAL_MAX_MIP_LEVELS];
    Ptr source_pixels[GXMETAL_MAX_MIP_LEVELS];
};

struct TQABitmap {
    uint32_t magic;
    uint32_t resource_id;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
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

static TQABoolean GXMetalTextureFormat(TQAImagePixelType pixelType,
                                       uint32_t *format,
                                       uint32_t *bytesPerPixel)
{
    switch (pixelType) {
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
    default:
        return 0;
    }
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
    gDiagnostics.resource_stage = 1;
    gDiagnostics.last_texture_flags = (uint32_t)flags;
    gDiagnostics.last_texture_pixel_type = (uint32_t)pixelType;
    gDiagnostics.last_texture_error = kQAError;
    if (newTexture == NULL) {
        gDiagnostics.last_texture_error = kQAParamErr;
        return kQAParamErr;
    }
    *newTexture = NULL;
    indexed = pixelType == kQAPixel_CL8;
    if (images == NULL || images[0].pixmap == NULL ||
        images[0].width <= 0 || images[0].height <= 0 ||
        images[0].width > (long)GXMETAL_MAX_DIMENSION ||
        images[0].height > (long)GXMETAL_MAX_DIMENSION ||
        images[0].rowBytes <= 0 ||
        (!indexed &&
         !GXMetalTextureFormat(pixelType, &format, &bytesPerPixel)) ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_TEXTURE) == 0) {
        gDiagnostics.last_texture_error = kQANotSupported;
        return kQANotSupported;
    }
    if (indexed) {
        /* RAVE binds the CL8 palette after creating the texture. Keep a
         * private copy of the indices and expose a direct-color host resource
         * which GXMetalTextureBindColorTable fills once the palette arrives. */
        format = GXMETAL_PIXEL_ARGB8888;
        bytesPerPixel = 1;
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
        if (rowBytes < levelWidth * bytesPerPixel ||
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
        memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
               images[level].pixmap, (size_t)length);
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
        if (!GXMetalCommitGlobalPacket(&packet)) {
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
    if (texture->source_pixel_type != (uint32_t)kQAPixel_CL8 ||
        table->entries != 256 || !GXMetalTransportAvailable()) {
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
            texture->source_row_bytes[level] < width ||
            length > GXMETAL_UPLOAD_BYTES) {
            gDiagnostics.last_texture_bind_error = kQAOutOfVideoMemory;
            return kQAOutOfVideoMemory;
        }
        source = (const uint8_t *)texture->source_pixels[level];
        destination = gTransport.shared + GXMETAL_UPLOAD_OFFSET;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint8_t index = source[y *
                    texture->source_row_bytes[level] + x];
                uint32_t color = table->colors[index];
                uint8_t *pixel = destination + y * rowBytes + x * 4;

                pixel[0] = table->transparent_index_zero && index == 0 ?
                    0 : 255;
                pixel[1] = (uint8_t)(color >> 16);
                pixel[2] = (uint8_t)(color >> 8);
                pixel[3] = (uint8_t)color;
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
        if (!GXMetalCommitGlobalPacket(&packet)) {
            gDiagnostics.last_texture_bind_error = kQAError;
            return kQAError;
        }
        gDiagnostics.resource_stage = 22;
    }
    gDiagnostics.resource_stage = 23;
    gDiagnostics.last_texture_bind_error = kQANoErr;
    if (gDiagnostics.texture_bind_color_table_count == 1) {
        GXMetalPublishDiagnostics();
        GXMetalPersistDiagnostics();
    }
    return kQANoErr;
}

static TQAError GXMetalBitmapBindColorTable(TQABitmap *bitmap,
                                             TQAColorTable *table)
{
    if (bitmap == NULL || bitmap->magic != GXMETAL_BITMAP_MAGIC ||
        table == NULL || table->magic != GXMETAL_COLOR_TABLE_MAGIC) {
        return kQAParamErr;
    }
    /* GXMetal currently advertises direct-color bitmap formats only. RAVE
     * nevertheless requires the complete color-table method family when an
     * engine exposes indexed textures. */
    return kQANotSupported;
}

static TQAError GXMetalBitmapNew(unsigned long flags,
                                 TQAImagePixelType pixelType,
                                 const TQAImage *image,
                                 TQABitmap **newBitmap)
{
    TQABitmap *bitmap;
    GXMetalGuestPacket packet;
    uint8_t *payload;
    uint32_t format;
    uint32_t bytesPerPixel;
    uint32_t width;
    uint32_t height;
    uint32_t rowBytes;
    uint64_t length;

    if (newBitmap == NULL) {
        return kQAParamErr;
    }
    *newBitmap = NULL;
    if (image == NULL || image->pixmap == NULL || image->width <= 0 ||
        image->height <= 0 ||
        image->width > (long)GXMETAL_MAX_DIMENSION ||
        image->height > (long)GXMETAL_MAX_DIMENSION ||
        image->rowBytes <= 0 ||
        (flags & (kQABitmap_NonRelocatable | kQABitmap_NoCopy)) != 0 ||
        !GXMetalTextureFormat(pixelType, &format, &bytesPerPixel) ||
        !GXMetalTransportAvailable() ||
        (gTransport.features & GXMETAL_FEATURE_TEXTURE) == 0) {
        return kQANotSupported;
    }
    width = (uint32_t)image->width;
    height = (uint32_t)image->height;
    rowBytes = (uint32_t)image->rowBytes;
    length = (uint64_t)rowBytes * height;
    if (rowBytes < width * bytesPerPixel || length > GXMETAL_UPLOAD_BYTES) {
        return kQAOutOfVideoMemory;
    }
    bitmap = (TQABitmap *)NewPtrClear(sizeof(*bitmap));
    if (bitmap == NULL) {
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

    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_CREATE,
                             GXMETAL_RESOURCE_CREATE_PACKET_BYTES,
                             &packet)) {
        DisposePtr((Ptr)bitmap);
        return kQAError;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ID_OFFSET,
                       bitmap->resource_id);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET, rowBytes);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET,
                       format);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_FLAGS_OFFSET,
        (flags & kQABitmap_FlipOrigin) ? GXMETAL_RESOURCE_FLIP_ORIGIN : 0);
    gxmetal_store_le32(payload + GXMETAL_RESOURCE_LEVELS_OFFSET, 1);
    if (!GXMetalCommitGlobalPacket(&packet)) {
        DisposePtr((Ptr)bitmap);
        return kQAError;
    }

    memcpy(gTransport.shared + GXMETAL_UPLOAD_OFFSET,
           image->pixmap, (size_t)length);
    if (!GXMetalGlobalPacket(GXMETAL_OP_TEXTURE_UPLOAD,
                             GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES,
                             &packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr((Ptr)bitmap);
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
    if (!GXMetalCommitGlobalPacket(&packet)) {
        GXMetalDestroyTextureResource(bitmap->resource_id);
        DisposePtr((Ptr)bitmap);
        return kQAError;
    }
    *newBitmap = bitmap;
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
    bitmap->magic = 0;
    DisposePtr((Ptr)bitmap);
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

static TQABoolean GXMetalEmitState(GXMetalDrawState *state, uint32_t tag,
                                   uint32_t type, uint32_t value)
{
    GXMetalGuestPacket packet;
    uint8_t *payload;

    if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
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

static void GXMetalStoreTexture(uint8_t *destination,
                                const TQAVTexture *vertex)
{
    float values[16];
    uint32_t i;
    memcpy(values, vertex, sizeof(values));
    for (i = 0; i < 16; i++) {
        gxmetal_store_le32(destination + i * sizeof(uint32_t),
                           GXMetalFloatBits(values[i]));
    }
}

static TQABoolean GXMetalEmitTexture(GXMetalDrawState *state,
                                     uint32_t primitive,
                                     uint32_t count,
                                     const TQAVTexture *vertices,
                                     uint32_t flags)
{
    GXMetalGuestPacket packet;
    uint32_t packetBytes;
    uint8_t *payload;
    uint32_t i;

    if (count == 0 || vertices == NULL || state->texture == NULL ||
        (uint64_t)GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
            (uint64_t)count * GXMETAL_TEXTURE_VERTEX_BYTES >
                GXMETAL_MAX_PACKET_BYTES) {
        state->failed = 1;
        return 0;
    }
    packetBytes = GXMETAL_PACKET_HEADER_BYTES + GXMETAL_DRAW_HEADER_BYTES +
                  count * GXMETAL_TEXTURE_VERTEX_BYTES;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_DRAW_TEXTURED, packetBytes,
                            &packet)) {
        return 0;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_DRAW_PRIMITIVE_OFFSET, primitive);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET, count);
    gxmetal_store_le32(payload + GXMETAL_DRAW_VERTEX_STRIDE_OFFSET,
                       GXMETAL_TEXTURE_VERTEX_BYTES);
    gxmetal_store_le32(payload + GXMETAL_DRAW_FLAGS_OFFSET, flags);
    for (i = 0; i < count; i++) {
        GXMetalStoreTexture(payload + GXMETAL_DRAW_VERTICES_OFFSET +
                            i * GXMETAL_TEXTURE_VERTEX_BYTES, &vertices[i]);
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

    gDiagnostics.draw_method_stage = 100;
    gDiagnostics.set_float_count++;
    gDiagnostics.last_set_float_tag = (uint32_t)tag;
    gDiagnostics.last_set_float_value = GXMetalFloatBits(newValue);
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
        state->ati_private_enabled = (uint32_t)newValue;
        gDiagnostics.draw_method_stage = 111;
        return;
    }
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
    gDiagnostics.draw_method_stage = 111;
}

static void GXMetalSetPtr(TQADrawContext *drawContext, TQATagPtr tag,
                          const void *newValue)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQATexture *texture = (const TQATexture *)newValue;
    GXMetalGuestPacket packet;
    uint8_t *payload;

    gDiagnostics.draw_method_stage = 120;
    gDiagnostics.set_ptr_count++;
    gDiagnostics.last_set_ptr_tag = (uint32_t)tag;
    gDiagnostics.last_set_ptr_value = (uint32_t)(uintptr_t)newValue;
    if (state == NULL || tag != kQATag_Texture ||
        (texture != NULL && texture->magic != GXMETAL_TEXTURE_MAGIC)) {
        return;
    }
    state->texture = newValue;
    if (!GXMetalBeginPacket(state, GXMETAL_OP_SET_STATE,
                            GXMETAL_SET_STATE_PACKET_BYTES, &packet)) {
        return;
    }
    payload = packet.bytes + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_STATE_TAG_OFFSET, (uint32_t)tag);
    gxmetal_store_le32(payload + GXMETAL_STATE_TYPE_OFFSET,
                       GXMETAL_STATE_RESOURCE);
    gxmetal_store_le32(payload + GXMETAL_STATE_VALUE_OFFSET,
                       texture != NULL ? texture->resource_id : 0);
    GXMetalCommitPacket(state, &packet);
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
        return 0;
    }
    gDiagnostics.last_get_int_value = state->int_state[(uint32_t)tag];
    gDiagnostics.draw_method_stage = 141;
    return state->int_state[(uint32_t)tag];
}

typedef void (*GXMetalATIPrivateMethod)(const TQADrawContext *drawContext,
                                        unsigned long value);

/* Carmageddon II uses the two-function ATI RAVE compatibility table returned
 * by private pointer tag 1021.  The calls only bracket ATI-specific cache
 * hints; GXMetal is already coherent, so both operations are intentional
 * no-ops.  Supplying the table is nevertheless mandatory: the game calls it
 * unconditionally after selecting an ATI engine. */
static void GXMetalATIPrivateMethod0(const TQADrawContext *drawContext,
                                     unsigned long value)
{
    (void)drawContext;
    (void)value;
    gDiagnostics.draw_method_stage = 270;
}

static void GXMetalATIPrivateMethod1(const TQADrawContext *drawContext,
                                     unsigned long value)
{
    (void)drawContext;
    (void)value;
    gDiagnostics.draw_method_stage = 271;
}

static GXMetalATIPrivateMethod gGXMetalATIPrivateMethods[2] = {
    GXMetalATIPrivateMethod0,
    GXMetalATIPrivateMethod1
};

static void *GXMetalGetPtr(const TQADrawContext *drawContext, TQATagPtr tag)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    gDiagnostics.draw_method_stage = 150;
    gDiagnostics.get_ptr_count++;
    gDiagnostics.last_get_ptr_tag = (uint32_t)tag;
    if (state != NULL &&
        (uint32_t)tag == GXMETAL_ATI_PRIVATE_METHODS_TAG) {
        gDiagnostics.last_get_ptr_value =
            (uint32_t)(uintptr_t)gGXMetalATIPrivateMethods;
        gDiagnostics.draw_method_stage = 151;
        return (void *)gGXMetalATIPrivateMethods;
    }
    if (state == NULL || tag != kQATag_Texture) {
        gDiagnostics.last_get_ptr_value = 0;
        return NULL;
    }
    gDiagnostics.last_get_ptr_value = (uint32_t)(uintptr_t)state->texture;
    gDiagnostics.draw_method_stage = 151;
    return (void *)state->texture;
}

static void GXMetalTraceDrawMethod(uint32_t method)
{
    gDiagnostics.draw_method_stage = 300 + method;
    gDiagnostics.draw_call_count++;
    gDiagnostics.last_draw_method = method;
}

static void GXMetalDrawPoint(const TQADrawContext *drawContext,
                             const TQAVGouraud *vertex)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    GXMetalTraceDrawMethod(kQADrawPoint);
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
    GXMetalTraceDrawMethod(kQADrawLine);
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
    GXMetalTraceDrawMethod(kQADrawTriGouraud);
    if (state == NULL || v0 == NULL || v1 == NULL || v2 == NULL) {
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    vertices[2] = *v2;
    (void)GXMetalEmitGouraud(state, GXMETAL_PRIMITIVE_TRIANGLE, 3,
                             vertices, (uint32_t)flags);
}

static void GXMetalDrawTriTexture(const TQADrawContext *drawContext,
                                  const TQAVTexture *v0,
                                  const TQAVTexture *v1,
                                  const TQAVTexture *v2,
                                  unsigned long flags)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    TQAVTexture vertices[3];
    GXMetalTraceDrawMethod(kQADrawTriTexture);
    if (state == NULL || v0 == NULL || v1 == NULL || v2 == NULL) {
        return;
    }
    vertices[0] = *v0;
    vertices[1] = *v1;
    vertices[2] = *v2;
    (void)GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_TRIANGLE, 3,
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

    GXMetalTraceDrawMethod(kQADrawVGouraud);
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
        if (nVertices % 3 != 0) {
            state->failed = 1;
            return;
        }
        if (flags != NULL) {
            for (i = 0; i < nVertices; i += 3) {
                (void)GXMetalEmitGouraud(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, &vertices[i],
                    (uint32_t)flags[i / 3]);
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
                (void)GXMetalEmitGouraud(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, triangle,
                    (uint32_t)flags[i]);
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
                (void)GXMetalEmitGouraud(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, triangle,
                    (uint32_t)flags[i]);
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
    uint32_t primitive;
    unsigned long i;

    GXMetalTraceDrawMethod(kQADrawVTexture);
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
        if (nVertices % 3 != 0) {
            state->failed = 1;
            return;
        }
        if (flags != NULL) {
            for (i = 0; i < nVertices; i += 3) {
                (void)GXMetalEmitTexture(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, &vertices[i],
                    (uint32_t)flags[i / 3]);
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE;
        break;
    case kQAVertexMode_Strip:
        if (flags != NULL) {
            TQAVTexture triangle[3];
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[0] = vertices[i + (i & 1)];
                triangle[1] = vertices[i + ((i & 1) == 0)];
                triangle[2] = vertices[i + 2];
                (void)GXMetalEmitTexture(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, triangle,
                    (uint32_t)flags[i]);
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_STRIP;
        break;
    case kQAVertexMode_Fan:
        if (flags != NULL) {
            TQAVTexture triangle[3];
            triangle[0] = vertices[0];
            for (i = 0; i + 2 < nVertices; i++) {
                triangle[1] = vertices[i + 1];
                triangle[2] = vertices[i + 2];
                (void)GXMetalEmitTexture(
                    state, GXMETAL_PRIMITIVE_TRIANGLE, 3, triangle,
                    (uint32_t)flags[i]);
            }
            return;
        }
        primitive = GXMETAL_PRIMITIVE_TRIANGLE_FAN;
        break;
    case kQAVertexMode_Polyline:
        for (i = 0; i + 1 < nVertices; i++) {
            (void)GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_LINE, 2,
                                     &vertices[i], 0);
        }
        return;
    default:
        state->failed = 1;
        return;
    }
    (void)GXMetalEmitTexture(state, primitive, (uint32_t)nVertices,
                             vertices, 0);
}

static TQAVTexture GXMetalBitmapVertex(const TQAVGouraud *source,
                                       float x, float y, float u, float v)
{
    TQAVTexture vertex;

    memset(&vertex, 0, sizeof(vertex));
    vertex.x = x;
    vertex.y = y;
    vertex.z = source->z;
    vertex.invW = source->invW > 0.0f ? source->invW : 1.0f;
    vertex.r = source->r;
    vertex.g = source->g;
    vertex.b = source->b;
    vertex.a = source->a;
    vertex.uOverW = u * vertex.invW;
    /* QADrawBitmap places image row zero at the top of the destination.
     * Textured RAVE primitives use a lower-left V origin, so compensate in
     * these engine-generated vertices before the Metal shader performs the
     * normal RAVE-to-Metal origin conversion. */
    vertex.vOverW = (1.0f - v) * vertex.invW;
    vertex.kd_r = source->r;
    vertex.kd_g = source->g;
    vertex.kd_b = source->b;
    return vertex;
}

static void GXMetalDrawBitmap(const TQADrawContext *drawContext,
                              const TQAVGouraud *v, TQABitmap *bitmap)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    const TQATexture *savedTexture;
    TQAVTexture vertices[4];
    float left;
    float top;
    float right;
    float bottom;
    float u0;
    float v0;
    float u1;
    float v1;

    GXMetalTraceDrawMethod(kQADrawBitmap);

    if (state == NULL || v == NULL || bitmap == NULL ||
        bitmap->magic != GXMETAL_BITMAP_MAGIC) {
        return;
    }
    left = v->x;
    top = v->y;
    right = left + (float)bitmap->width;
    bottom = top + (float)bitmap->height;
    if (right <= 0.0f || bottom <= 0.0f ||
        left >= (float)state->width || top >= (float)state->height) {
        return;
    }
    u0 = 0.0f;
    v0 = 0.0f;
    u1 = 1.0f;
    v1 = 1.0f;
    if (left < 0.0f) {
        u0 = -left / (float)bitmap->width;
        left = 0.0f;
    }
    if (top < 0.0f) {
        v0 = -top / (float)bitmap->height;
        top = 0.0f;
    }
    if (right > (float)state->width) {
        u1 -= (right - (float)state->width) / (float)bitmap->width;
        right = (float)state->width;
    }
    if (bottom > (float)state->height) {
        v1 -= (bottom - (float)state->height) / (float)bitmap->height;
        bottom = (float)state->height;
    }
    vertices[0] = GXMetalBitmapVertex(v, left, top, u0, v0);
    vertices[1] = GXMetalBitmapVertex(v, right, top, u1, v0);
    vertices[2] = GXMetalBitmapVertex(v, left, bottom, u0, v1);
    vertices[3] = GXMetalBitmapVertex(v, right, bottom, u1, v1);

    savedTexture = (const TQATexture *)state->texture;
    if (!GXMetalEmitState(state, kQATag_Texture, GXMETAL_STATE_RESOURCE,
                          bitmap->resource_id) ||
        !GXMetalEmitState(state, kQATag_TextureOp, GXMETAL_STATE_UINT32,
                          kQATextureOp_None) ||
        !GXMetalEmitState(state, kQATag_TextureFilter, GXMETAL_STATE_UINT32,
                          kQATextureFilter_Fast) ||
        !GXMetalEmitState(state, kQATagGL_TextureWrapU, GXMETAL_STATE_UINT32,
                          kQAGL_Clamp) ||
        !GXMetalEmitState(state, kQATagGL_TextureWrapV, GXMETAL_STATE_UINT32,
                          kQAGL_Clamp) ||
        !GXMetalEmitTexture(state, GXMETAL_PRIMITIVE_TRIANGLE_STRIP, 4,
                            vertices, 0)) {
        return;
    }
    (void)GXMetalEmitState(state, kQATag_Texture, GXMETAL_STATE_RESOURCE,
                           savedTexture != NULL ? savedTexture->resource_id : 0);
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
    return error;
}

static TQAError GXMetalSync(const TQADrawContext *drawContext)
{
    GXMetalDrawState *state = GXMetalGetState(drawContext);
    uint32_t sequence;

    gDiagnostics.draw_method_stage = 230;
    gDiagnostics.sync_count++;
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
    gDiagnostics.draw_method_stage = 220;
    gDiagnostics.flush_count++;
    return state != NULL && !state->failed ? kQANoErr : kQAError;
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
    state->float_state[kQATagGL_DepthBG] = 1.0f;
    state->int_state[kQATag_Blend] = kQABlend_Interpolate;
    state->int_state[kQATag_TextureFilter] = kQATextureFilter_Fast;
    state->int_state[kQATag_TextureOp] = kQATextureOp_None;
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
    error = GXMetalRegisterMethods(newDrawContext);
    if (error != kQANoErr) {
        gDiagnosticStatus = kGXMetalDiagnosticContextMethodFailed;
        gDiagnostics.context_error = error;
        GXMetalPublishDiagnostics();
        newDrawContext->drawPrivate = NULL;
        DisposePtr((Ptr)state);
        return error;
    }
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

    gDiagnosticStatus = kGXMetalDiagnosticCheckingDevice;
    gDiagnostics.check_device_count++;
    if (device == NULL) {
        gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
        return kQANotSupported;
    }
    if (!GXMetalTransportAvailable()) {
        gDiagnosticStatus = kGXMetalDiagnosticTransportUnavailable;
        return kQANotSupported;
    }
    if (device->deviceType == kQADeviceGDevice) {
        GDHandle graphicsDevice = device->device.gDevice;
        PixMapHandle pixmap;
        if (graphicsDevice == NULL || *graphicsDevice == NULL) {
            gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
            return kQANotSupported;
        }
        pixmap = (**graphicsDevice).gdPMap;
        if (pixmap == NULL || *pixmap == NULL) {
            gDiagnosticStatus = kGXMetalDiagnosticInvalidDevice;
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
        return kQANotSupported;
    }
    if (!GXMetalDescribeDevice(device, &rect, &width, &height, &rowBytes,
                               &pixelFormat, &framebufferOffset)) {
        gDiagnosticStatus = kGXMetalDiagnosticDisplayRejected;
        return kQANotSupported;
    }
    gDiagnosticStatus = kGXMetalDiagnosticDeviceAccepted;
    return kQANoErr;
}

static TQAError GXMetalEngineGestalt(TQAGestaltSelector selector,
                                     void *response)
{
    uint32_t value;
    uint64_t features = GXMetalTransportAvailable() ? gTransport.features : 0;

    gDiagnostics.gestalt_count++;
    gDiagnostics.last_gestalt_selector = (uint32_t)selector;
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
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAOptional_Texture | kQAOptional_TextureHQ |
                     kQAOptional_TextureColor | kQAOptional_CL8;
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
        break;
    case kQAGestalt_FastFeatures:
        value = kQAFast_Line | kQAFast_Gouraud;
        if (features & GXMETAL_FEATURE_BLEND) {
            value |= kQAFast_Blend;
        }
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAFast_Texture | kQAFast_TextureHQ | kQAFast_CL8;
        }
        if (features & GXMETAL_FEATURE_FOG_DEPTH) {
            value |= kQAFast_FogDepth;
        }
        break;
    case kQAGestalt_TextureMemory:
    case kQAGestalt_FastTextureMemory:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            GXMETAL_TEXTURE_MEMORY : 0;
        break;
    case kQAGestalt_MultiTextureMax:
        value = 0;
        break;
    case kQAGestalt_OptionalFeatures2:
        value = (features & GXMETAL_FEATURE_DOUBLE_BUFFER) ?
            kQAOptional2_SwapBuffers : 0;
        if (features & GXMETAL_FEATURE_TEXTURE) {
            value |= kQAOptional2_FlipOrigin;
        }
        break;
    case kQAGestalt_VendorID:
        /* OS 9 queries the identity immediately after QARegisterEngine and
         * rejects a second public ATI identity before CheckDevice. Several
         * games later refuse any accelerated engine which does not report
         * kQAVendor_ATI. Give the manager's one-time registration query the
         * private GXMetal identity, then expose ATI compatibility to apps. */
        value = gRegistrationVendorPending ?
            GXMETAL_REGISTRATION_VENDOR_ID : GXMETAL_LEGACY_VENDOR_ID;
        gRegistrationVendorPending = 0;
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
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            (UINT32_C(1) << kQAPixel_RGB16) |
            (UINT32_C(1) << kQAPixel_ARGB16) |
            (UINT32_C(1) << kQAPixel_RGB32) |
            (UINT32_C(1) << kQAPixel_ARGB32) |
            (UINT32_C(1) << kQAPixel_CL8) |
            (UINT32_C(1) << kQAPixel_ARGB16_4444) : 0;
        break;
    case kQAGestalt_BitmapPixelTypesAllowed:
    case kQAGestalt_BitmapPixelTypesPreferred:
        value = (features & GXMETAL_FEATURE_TEXTURE) ?
            (UINT32_C(1) << kQAPixel_RGB16) |
            (UINT32_C(1) << kQAPixel_RGB32) |
            (UINT32_C(1) << kQAPixel_ARGB32) : 0;
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
    gDiagnostics.get_method_count++;
    if ((uint32_t)methodTag < 32) {
        gDiagnostics.method_mask |= UINT32_C(1) << (uint32_t)methodTag;
    }
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
    default:
        return kQANotSupported;
    }
    return kQANoErr;
}

OSErr GXMetalCFMInitialize(const CFragInitBlock *initBlock)
{
    TQAError error;

    (void)initBlock;
    gDiagnostics.initialize_count++;
    gDiagnosticStatus = kGXMetalDiagnosticInitializing;
    gRegistrationVendorPending = 1;
    error = QARegisterEngine(GXMetalEngineGetMethod);
    gDiagnostics.registration_error = error;
    gDiagnosticStatus = error == kQANoErr ?
        kGXMetalDiagnosticRegistered : kGXMetalDiagnosticRegistrationFailed;
    GXMetalPublishDiagnostics();
    return (OSErr)error;
}
