/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_METAL_H
#define GXMETAL_METAL_H

#include <stdint.h>

#include "gxmetal_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GXMetalMetalRenderer GXMetalMetalRenderer;

GXMetalMetalRenderer *gxmetal_metal_create(void *framebuffer,
                                            uint32_t framebuffer_bytes,
                                            void *shared,
                                            uint32_t shared_bytes);
void gxmetal_metal_destroy(GXMetalMetalRenderer *renderer);
int gxmetal_metal_direct_present_available(
    const GXMetalMetalRenderer *renderer);
uint64_t gxmetal_metal_direct_present_count(
    const GXMetalMetalRenderer *renderer);
uint64_t gxmetal_metal_fallback_present_count(
    const GXMetalMetalRenderer *renderer);
void gxmetal_metal_reset(GXMetalMetalRenderer *renderer);
uint32_t gxmetal_metal_dispatch(void *opaque,
                                const GXMetalPacketView *packet);

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_METAL_H */
