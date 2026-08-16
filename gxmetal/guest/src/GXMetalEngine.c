/*
 * GXMetal QuickDraw 3D RAVE engine registration skeleton.
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 */

#include <RAVESystem.h>
#include <CodeFragments.h>
#include <stdint.h>
#include <string.h>

#include "../../protocol/gxmetal_protocol.h"

#define GXMETAL_VENDOR_ID UINT32_C(0x47584d54) /* GXMT */
#define GXMETAL_ENGINE_ID UINT32_C(0x00000001)
#define GXMETAL_REVISION  UINT32_C(0x00010000)

static const char kGXMetalName[] = "GXMetal (development)";

/*
 * Transport probing is deliberately fail-closed until the QEMU device exists.
 * Registering the engine lets RAVE discover the tnsl, while CheckDevice keeps
 * applications on Apple's software engine and preserves a safe fallback.
 */
static TQABoolean GXMetalTransportAvailable(void)
{
    return 0;
}

static TQAError GXMetalDrawPrivateNew(TQADrawContext *newDrawContext,
                                      const TQADevice *device,
                                      const TQARect *rect,
                                      const TQAClip *clip,
                                      unsigned long flags)
{
    (void)newDrawContext;
    (void)device;
    (void)rect;
    (void)clip;
    (void)flags;
    return kQANotSupported;
}

static void GXMetalDrawPrivateDelete(TQADrawPrivate *drawPrivate)
{
    (void)drawPrivate;
}

static TQAError GXMetalEngineCheckDevice(const TQADevice *device)
{
    if (device == NULL || !GXMetalTransportAvailable()) {
        return kQANotSupported;
    }
    if (device->deviceType != kQADeviceGDevice &&
        device->deviceType != kQADeviceMemory) {
        return kQANotSupported;
    }
    return kQANoErr;
}

static TQAError GXMetalEngineGestalt(TQAGestaltSelector selector,
                                     void *response)
{
    uint32_t value;

    if (response == NULL) {
        return kQAParamErr;
    }

    switch (selector) {
    case kQAGestalt_OptionalFeatures:
    case kQAGestalt_FastFeatures:
    case kQAGestalt_TextureMemory:
    case kQAGestalt_FastTextureMemory:
    case kQAGestalt_OptionalFeatures2:
    case kQAGestalt_MultiTextureMax:
        value = 0;
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
    /* RAVE finds tnsl files and runs their CFM initialization routine. */
    (void)initBlock;
    return (OSErr)QARegisterEngine(GXMetalEngineGetMethod);
}
