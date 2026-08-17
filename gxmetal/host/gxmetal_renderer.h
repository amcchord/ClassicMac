/* SPDX-License-Identifier: MIT */

#ifndef GXMETAL_RENDERER_H
#define GXMETAL_RENDERER_H

#include <stdint.h>

#include "gxmetal_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GXMETAL_RENDERER_MAX_CONTEXTS 32u

typedef struct GXMetalRenderContext {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t framebuffer_offset;
    uint32_t flags;
    int active;
} GXMetalRenderContext;

typedef struct GXMetalRenderer {
    uint8_t *framebuffer;
    uint32_t framebuffer_bytes;
    GXMetalRenderContext contexts[GXMETAL_RENDERER_MAX_CONTEXTS];
} GXMetalRenderer;

void gxmetal_renderer_init(GXMetalRenderer *renderer, void *framebuffer,
                           uint32_t framebuffer_bytes);
void gxmetal_renderer_reset(GXMetalRenderer *renderer);
uint32_t gxmetal_renderer_dispatch(void *opaque,
                                   const GXMetalPacketView *packet);

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_RENDERER_H */
