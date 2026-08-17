/* SPDX-License-Identifier: MIT */

#include "gxmetal_metal.h"

GXMetalMetalRenderer *gxmetal_metal_create(void *framebuffer,
                                            uint32_t framebuffer_bytes,
                                            void *shared,
                                            uint32_t shared_bytes)
{
    (void)framebuffer;
    (void)framebuffer_bytes;
    (void)shared;
    (void)shared_bytes;
    return NULL;
}

void gxmetal_metal_destroy(GXMetalMetalRenderer *renderer)
{
    (void)renderer;
}

int gxmetal_metal_direct_present_available(
    const GXMetalMetalRenderer *renderer)
{
    (void)renderer;
    return 0;
}

uint64_t gxmetal_metal_direct_present_count(
    const GXMetalMetalRenderer *renderer)
{
    (void)renderer;
    return 0;
}

uint64_t gxmetal_metal_fallback_present_count(
    const GXMetalMetalRenderer *renderer)
{
    (void)renderer;
    return 0;
}

void gxmetal_metal_reset(GXMetalMetalRenderer *renderer)
{
    (void)renderer;
}

uint32_t gxmetal_metal_dispatch(void *opaque,
                                const GXMetalPacketView *packet)
{
    (void)opaque;
    (void)packet;
    return GXMETAL_ERROR_RENDERER;
}
