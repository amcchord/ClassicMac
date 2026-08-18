/*
 * GXMetal Metal renderer.
 *
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "gxmetal_metal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GXMETAL_METAL_MAX_CONTEXTS 32u
/* RAVE applications may retain several generations of menu, HUD, and world
 * textures at once. Carmageddon II reaches 256 live/partially uploaded
 * resources while changing screens; exhausting this table faults the shared
 * command queue permanently and leaves the last completed frame onscreen.
 * Resource records are small and inactive slots are reused, so a larger host
 * table removes that artificial limit without increasing guest VRAM usage. */
#define GXMETAL_METAL_MAX_RESOURCES 4096u

typedef struct GXMetalMetalVertex {
    float x;
    float y;
    float z;
    float inv_w;
    float r;
    float g;
    float b;
    float a;
} GXMetalMetalVertex;

_Static_assert(sizeof(GXMetalMetalVertex) == GXMETAL_GOURAUD_VERTEX_BYTES,
               "Metal Gouraud vertex must match the wire layout");

typedef struct GXMetalMetalViewport {
    float width;
    float height;
} GXMetalMetalViewport;

typedef struct GXMetalMetalTextureVertex {
    float x;
    float y;
    float z;
    float inv_w;
    float r;
    float g;
    float b;
    float a;
    float u_over_w;
    float v_over_w;
    float kd_r;
    float kd_g;
    float kd_b;
    float ks_r;
    float ks_g;
    float ks_b;
} GXMetalMetalTextureVertex;

_Static_assert(sizeof(GXMetalMetalTextureVertex) ==
               GXMETAL_TEXTURE_VERTEX_BYTES,
               "Metal texture vertex must match the wire layout");

typedef struct GXMetalMetalFog {
    uint32_t mode_and_padding[4];
    float color[4];
    float start;
    float end;
    float density;
    float max_depth;
} GXMetalMetalFog;

_Static_assert(sizeof(GXMetalMetalFog) == 48,
               "Metal fog constants must match the shader layout");

typedef struct GXMetalMetalAlphaTest {
    uint32_t function;
    float reference;
} GXMetalMetalAlphaTest;

_Static_assert(sizeof(GXMetalMetalAlphaTest) == 8,
               "Metal alpha-test constants must match the shader layout");

typedef struct GXMetalMetalPresent {
    uint32_t framebuffer_offset;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
} GXMetalMetalPresent;

_Static_assert(sizeof(GXMetalMetalPresent) == 32,
               "Metal present constants must match the shader layout");

typedef struct GXMetalMetalResource {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t flags;
    uint32_t levels;
    int active;
    id<MTLTexture> texture;
} GXMetalMetalResource;

typedef struct GXMetalMetalContext {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t framebuffer_offset;
    uint32_t flags;
    uint32_t z_function;
    uint32_t z_write;
    uint32_t blend;
    uint32_t texture_id;
    uint32_t texture_filter;
    uint32_t texture_op;
    uint32_t texture_wrap_u;
    uint32_t texture_wrap_v;
    uint32_t clip_left;
    uint32_t clip_top;
    uint32_t clip_right;
    uint32_t clip_bottom;
    uint32_t scissor_left;
    uint32_t scissor_top;
    uint32_t scissor_right;
    uint32_t scissor_bottom;
    GXMetalMetalFog fog;
    GXMetalMetalAlphaTest alpha_test;
    int active;
    int committed;
    id<MTLTexture> texture;
    id<MTLTexture> depth_texture;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
} GXMetalMetalContext;

struct GXMetalMetalRenderer {
    uint8_t *framebuffer;
    uint32_t framebuffer_bytes;
    uint8_t *shared;
    uint32_t shared_bytes;
    uint64_t direct_present_count;
    uint64_t fallback_present_count;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipelines[3];
    id<MTLRenderPipelineState> texture_pipelines[3];
    id<MTLRenderPipelineState> clear_pipeline;
    id<MTLRenderPipelineState> depth_clear_pipeline;
    id<MTLComputePipelineState> present_pipeline;
    id<MTLBuffer> framebuffer_buffer;
    id<MTLDepthStencilState> depth_states[9][2];
    id<MTLSamplerState> samplers[3][4];
    GXMetalMetalContext contexts[GXMETAL_METAL_MAX_CONTEXTS];
    GXMetalMetalResource resources[GXMETAL_METAL_MAX_RESOURCES];
};

static NSString *const kGXMetalShaderSource = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct GXVertex { float x; float y; float z; float invW; float r; float g; float b; float a; };\n"
    "struct GXViewport { float width; float height; };\n"
    "struct GXOut { float4 position [[position]]; float4 color;\n"
    "               float invW [[center_no_perspective]]; };\n"
    "struct GXFog {\n"
    "  uint4 modeAndPadding; float4 color;\n"
    "  float start; float end; float density; float maxDepth;\n"
    "};\n"
    "float4 gxmetal_apply_fog(float4 source, float depth,\n"
    "                         constant GXFog &fog) {\n"
    "  uint mode = fog.modeAndPadding.x;\n"
    "  float keep = 1.0;\n"
    "  if (mode == 1u) {\n"
    "    keep = source.a;\n"
    "  } else if (mode == 2u) {\n"
    "    float range = fog.end - fog.start;\n"
    "    keep = abs(range) > 0.000001 ?\n"
    "      (fog.end - depth) / range : (depth <= fog.start ? 1.0 : 0.0);\n"
    "  } else if (mode == 3u) {\n"
    "    keep = exp(-max(fog.density, 0.0) * max(depth, 0.0));\n"
    "  } else if (mode == 4u) {\n"
    "    float scaled = max(fog.density, 0.0) * max(depth, 0.0);\n"
    "    keep = exp(-(scaled * scaled));\n"
    "  }\n"
    "  return mix(fog.color, source, clamp(keep, 0.0, 1.0));\n"
    "}\n"
    "float gxmetal_fog_depth(float z, float invW,\n"
    "                        constant GXFog &fog) {\n"
    "  if (fog.modeAndPadding.y == 0u) return z;\n"
    "  float depth = 1.0 / max(invW, 0.000001);\n"
    "  return fog.maxDepth > 0.0 ? min(depth, fog.maxDepth) : depth;\n"
    "}\n"
    "struct GXAlphaTest { uint function; float reference; };\n"
    "bool gxmetal_alpha_visible(float alpha,\n"
    "                           constant GXAlphaTest &test) {\n"
    "  switch (test.function) {\n"
    "    case 1u: return alpha < test.reference;\n"
    "    case 2u: return alpha == test.reference;\n"
    "    case 3u: return alpha <= test.reference;\n"
    "    case 4u: return alpha > test.reference;\n"
    "    case 5u: return alpha != test.reference;\n"
    "    case 6u: return alpha >= test.reference;\n"
    "    default: return true;\n"
    "  }\n"
    "}\n"
    "vertex GXOut gxmetal_vertex(const device GXVertex *vertices [[buffer(0)]], "
    "                            constant GXViewport &viewport [[buffer(1)]], "
    "                            uint index [[vertex_id]]) {\n"
    "  GXVertex v = vertices[index];\n"
    "  GXOut out;\n"
    "  out.position = float4(v.x / viewport.width * 2.0 - 1.0, "
    "                        1.0 - v.y / viewport.height * 2.0, v.z, 1.0);\n"
    "  out.color = float4(v.r, v.g, v.b, v.a);\n"
    "  out.invW = v.invW;\n"
    "  return out;\n"
    "}\n"
    "fragment float4 gxmetal_fragment(GXOut in [[stage_in]],\n"
    "    constant GXFog &fog [[buffer(0)]],\n"
    "    constant GXAlphaTest &alphaTest [[buffer(1)]]) {\n"
    "  float4 result = clamp(in.color, 0.0, 1.0);\n"
    "  if (!gxmetal_alpha_visible(result.a, alphaTest)) discard_fragment();\n"
    "  float fogDepth = gxmetal_fog_depth(in.position.z, in.invW, fog);\n"
    "  return gxmetal_apply_fog(result, fogDepth, fog);\n"
    "}\n";

static NSString *const kGXMetalTextureShaderSource = @
    "struct GXTextureVertex {\n"
    "  float x; float y; float z; float invW;\n"
    "  float r; float g; float b; float a;\n"
    "  float uOverW; float vOverW;\n"
    "  float kd_r; float kd_g; float kd_b;\n"
    "  float ks_r; float ks_g; float ks_b;\n"
    "};\n"
    "struct GXTextureOut {\n"
    "  float4 position [[position]]; float4 color; float2 uv;\n"
    "  float3 kd; float3 ks; float invW [[center_no_perspective]];\n"
    "};\n"
    "vertex GXTextureOut gxmetal_texture_vertex(\n"
    "    const device GXTextureVertex *vertices [[buffer(0)]],\n"
    "    constant GXViewport &viewport [[buffer(1)]],\n"
    "    uint index [[vertex_id]]) {\n"
    "  GXTextureVertex v = vertices[index];\n"
    "  float safeInvW = max(v.invW, 0.000001);\n"
    "  float clipW = 1.0 / safeInvW;\n"
    "  float ndcX = v.x / viewport.width * 2.0 - 1.0;\n"
    "  float ndcY = 1.0 - v.y / viewport.height * 2.0;\n"
    "  GXTextureOut out;\n"
    "  out.position = float4(ndcX * clipW, ndcY * clipW,\n"
    "                        v.z * clipW, clipW);\n"
    "  out.color = float4(v.r, v.g, v.b, v.a);\n"
    /* RAVE texture coordinates use the lower edge as V=0, whereas Metal
     * normalized texture coordinates use the upper edge.  Flip V before
     * perspective interpolation while preserving the u/w, v/w wire form. */
    "  out.uv = float2(v.uOverW, v.invW - v.vOverW) / safeInvW;\n"
    "  out.kd = float3(v.kd_r, v.kd_g, v.kd_b);\n"
    "  out.ks = float3(v.ks_r, v.ks_g, v.ks_b);\n"
    "  out.invW = v.invW;\n"
    "  return out;\n"
    "}\n"
    "fragment float4 gxmetal_texture_fragment(\n"
    "    GXTextureOut in [[stage_in]],\n"
    "    texture2d<float> image [[texture(0)]],\n"
    "    sampler imageSampler [[sampler(0)]],\n"
    "    constant uint &operation [[buffer(0)]],\n"
    "    constant GXFog &fog [[buffer(1)]],\n"
    "    constant GXAlphaTest &alphaTest [[buffer(2)]]) {\n"
    "  float4 texel = image.sample(imageSampler, in.uv);\n"
    "  float4 result = texel;\n"
    "  if ((operation & 4u) != 0u) {\n"
    "    result.rgb = mix(in.color.rgb, texel.rgb, texel.a);\n"
    "    result.a = in.color.a;\n"
    "  } else {\n"
    "    result.a *= in.color.a;\n"
    "  }\n"
    "  if ((operation & 1u) != 0u) result.rgb *= in.kd;\n"
    "  if ((operation & 2u) != 0u) result.rgb += in.ks;\n"
    "  result = clamp(result, 0.0, 1.0);\n"
    "  if (!gxmetal_alpha_visible(result.a, alphaTest)) discard_fragment();\n"
    "  float fogDepth = gxmetal_fog_depth(in.position.z, in.invW, fog);\n"
    "  return gxmetal_apply_fog(result, fogDepth, fog);\n"
    "}\n";

static NSString *const kGXMetalPresentShaderSource = @
    "struct GXPresent {\n"
    "  uint framebufferOffset; uint rowBytes; uint pixelFormat; uint left;\n"
    "  uint top; uint width; uint height; uint reserved;\n"
    "};\n"
    "kernel void gxmetal_present(\n"
    "    texture2d<float, access::read> image [[texture(0)]],\n"
    "    device uchar *framebuffer [[buffer(0)]],\n"
    "    constant GXPresent &present [[buffer(1)]],\n"
    "    uint2 position [[thread_position_in_grid]]) {\n"
    "  if (position.x >= present.width || position.y >= present.height) return;\n"
    "  uint x = present.left + position.x;\n"
    "  uint y = present.top + position.y;\n"
    "  float4 color = clamp(image.read(uint2(x, y)), 0.0, 1.0);\n"
    "  uchar4 rgba = uchar4(color * 255.0 + 0.5);\n"
    "  uint bytesPerPixel = present.pixelFormat == 1u ? 2u : 4u;\n"
    "  uint offset = present.framebufferOffset + y * present.rowBytes +\n"
    "                x * bytesPerPixel;\n"
    "  if (present.pixelFormat == 1u) {\n"
    "    uint packed = ((uint(rgba.r) >> 3) << 10) |\n"
    "                  ((uint(rgba.g) >> 3) << 5) |\n"
    "                  (uint(rgba.b) >> 3);\n"
    "    framebuffer[offset] = uchar(packed >> 8);\n"
    "    framebuffer[offset + 1] = uchar(packed);\n"
    "  } else {\n"
    "    framebuffer[offset] = present.pixelFormat == 2u ? rgba.a : 0;\n"
    "    framebuffer[offset + 1] = rgba.r;\n"
    "    framebuffer[offset + 2] = rgba.g;\n"
    "    framebuffer[offset + 3] = rgba.b;\n"
    "  }\n"
    "}\n";

static float gxmetal_metal_load_float(const uint8_t *bytes)
{
    uint32_t bits = gxmetal_load_le32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t gxmetal_metal_bytes_per_pixel(uint32_t format)
{
    switch (format) {
    case GXMETAL_PIXEL_RGB555:
        return 2;
    case GXMETAL_PIXEL_ARGB8888:
    case GXMETAL_PIXEL_RGB8888:
        return 4;
    default:
        return 0;
    }
}

static GXMetalMetalContext *gxmetal_metal_find_context(
    GXMetalMetalRenderer *renderer, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < GXMETAL_METAL_MAX_CONTEXTS; i++) {
        if (renderer->contexts[i].active && renderer->contexts[i].id == id) {
            return &renderer->contexts[i];
        }
    }
    return NULL;
}

static GXMetalMetalResource *gxmetal_metal_find_resource(
    GXMetalMetalRenderer *renderer, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < GXMETAL_METAL_MAX_RESOURCES; i++) {
        if (renderer->resources[i].active &&
            renderer->resources[i].id == id) {
            return &renderer->resources[i];
        }
    }
    return NULL;
}

static uint32_t gxmetal_metal_resource_bytes_per_pixel(uint32_t format)
{
    switch (format) {
    case GXMETAL_PIXEL_RGB555:
    case GXMETAL_PIXEL_RGB565:
    case GXMETAL_PIXEL_ATI_ARGB4444:
    case GXMETAL_PIXEL_ARGB1555:
    case GXMETAL_PIXEL_ARGB4444:
        return 2;
    case GXMETAL_PIXEL_ARGB8888:
    case GXMETAL_PIXEL_RGB8888:
        return 4;
    default:
        return 0;
    }
}

static void gxmetal_metal_release_frame(GXMetalMetalContext *context)
{
    [context->encoder release];
    context->encoder = nil;
    [context->command_buffer release];
    context->command_buffer = nil;
    context->committed = 0;
}

static void gxmetal_metal_release_context(GXMetalMetalContext *context)
{
    if (context->encoder != nil) {
        [context->encoder endEncoding];
    }
    if (context->command_buffer != nil) {
        if (!context->committed) {
            [context->command_buffer commit];
        }
        [context->command_buffer waitUntilCompleted];
    }
    gxmetal_metal_release_frame(context);
    [context->texture release];
    [context->depth_texture release];
    memset(context, 0, sizeof(*context));
}

static int gxmetal_metal_begin_frame(GXMetalMetalRenderer *renderer,
                                     GXMetalMetalContext *context)
{
    if (context->command_buffer != nil) {
        if (context->encoder != nil) {
            [context->encoder endEncoding];
            [context->encoder release];
            context->encoder = nil;
        }
        if (!context->committed) {
            [context->command_buffer commit];
        }
        [context->command_buffer waitUntilCompleted];
        gxmetal_metal_release_frame(context);
    }
    context->command_buffer = [[renderer->command_queue commandBuffer] retain];
    return context->command_buffer != nil;
}

static int gxmetal_metal_ensure_encoder(GXMetalMetalRenderer *renderer,
                                        GXMetalMetalContext *context,
                                        uint32_t clear_flags,
                                        MTLClearColor clear_color,
                                        double clear_depth)
{
    MTLRenderPassDescriptor *pass;

    if (context->command_buffer == nil &&
        !gxmetal_metal_begin_frame(renderer, context)) {
        return 0;
    }
    if (clear_flags != 0 && context->encoder != nil) {
        [context->encoder endEncoding];
        [context->encoder release];
        context->encoder = nil;
    }
    if (context->encoder != nil) {
        return 1;
    }
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = context->texture;
    pass.colorAttachments[0].loadAction =
        (clear_flags & GXMETAL_CLEAR_COLOR) ? MTLLoadActionClear :
                                             MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = clear_color;
    pass.depthAttachment.texture = context->depth_texture;
    pass.depthAttachment.loadAction =
        (clear_flags & GXMETAL_CLEAR_DEPTH) ? MTLLoadActionClear :
                                             MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = clear_depth;
    context->encoder = [[context->command_buffer
        renderCommandEncoderWithDescriptor:pass] retain];
    if (context->encoder == nil) {
        return 0;
    }
    [context->encoder setRenderPipelineState:renderer->pipelines[0]];
    return 1;
}

static int gxmetal_metal_effective_scissor(
    const GXMetalMetalContext *context, MTLScissorRect *scissor)
{
    uint32_t left = context->clip_left > context->scissor_left ?
        context->clip_left : context->scissor_left;
    uint32_t top = context->clip_top > context->scissor_top ?
        context->clip_top : context->scissor_top;
    uint32_t right = context->clip_right < context->scissor_right ?
        context->clip_right : context->scissor_right;
    uint32_t bottom = context->clip_bottom < context->scissor_bottom ?
        context->clip_bottom : context->scissor_bottom;

    if (left >= right || top >= bottom) {
        return 0;
    }
    scissor->x = left;
    scissor->y = top;
    scissor->width = right - left;
    scissor->height = bottom - top;
    return 1;
}

static int gxmetal_metal_apply_scissor(GXMetalMetalContext *context)
{
    MTLScissorRect scissor;

    if (!gxmetal_metal_effective_scissor(context, &scissor)) {
        return 0;
    }
    [context->encoder setScissorRect:scissor];
    return 1;
}

static uint32_t gxmetal_metal_context_create(
    GXMetalMetalRenderer *renderer, const GXMetalPacketView *packet)
{
    GXMetalMetalContext *context = NULL;
    MTLTextureDescriptor *descriptor;
    uint32_t bytes_per_pixel;
    uint64_t end;
    uint32_t i;

    if (gxmetal_metal_find_context(renderer, packet->context_id) != NULL) {
        return GXMETAL_ERROR_BAD_CONTEXT;
    }
    for (i = 0; i < GXMETAL_METAL_MAX_CONTEXTS; i++) {
        if (!renderer->contexts[i].active) {
            context = &renderer->contexts[i];
            break;
        }
    }
    if (context == NULL) {
        return GXMETAL_ERROR_BAD_CONTEXT;
    }
    context->id = packet->context_id;
    context->width = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_WIDTH_OFFSET);
    context->height = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_HEIGHT_OFFSET);
    context->row_bytes = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET);
    context->pixel_format = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET);
    context->framebuffer_offset = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_FRAMEBUFFER_OFFSET);
    context->flags = gxmetal_load_le32(
        packet->payload + GXMETAL_CONTEXT_FLAGS_OFFSET);
    if ((context->flags & GXMETAL_CONTEXT_RECT_CLIP) != 0) {
        uint32_t left_top = gxmetal_load_le32(
            packet->payload + GXMETAL_CONTEXT_CLIP_LEFT_TOP_OFFSET);
        uint32_t right_bottom = gxmetal_load_le32(
            packet->payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET);

        context->clip_left = left_top & UINT32_C(0xffff);
        context->clip_top = left_top >> 16;
        context->clip_right = right_bottom & UINT32_C(0xffff);
        context->clip_bottom = right_bottom >> 16;
    } else {
        context->clip_right = context->width;
        context->clip_bottom = context->height;
    }
    context->scissor_right = context->width;
    context->scissor_bottom = context->height;
    bytes_per_pixel = gxmetal_metal_bytes_per_pixel(context->pixel_format);
    end = (uint64_t)context->framebuffer_offset +
          (uint64_t)(context->height - 1) * context->row_bytes +
          (uint64_t)context->width * bytes_per_pixel;
    if ((context->flags & ~(GXMETAL_CONTEXT_Z16 |
                            GXMETAL_CONTEXT_DOUBLE_BUFFER |
                            GXMETAL_CONTEXT_NO_DITHER |
                            GXMETAL_CONTEXT_RECT_CLIP)) != 0 ||
        context->clip_left > context->clip_right ||
        context->clip_top > context->clip_bottom ||
        context->clip_right > context->width ||
        context->clip_bottom > context->height ||
        bytes_per_pixel == 0 ||
        context->row_bytes < (uint64_t)context->width * bytes_per_pixel ||
        end > renderer->framebuffer_bytes) {
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_BAD_CONTEXT;
    }

    descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:context->width height:context->height mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget |
                       MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    context->texture = [renderer->device newTextureWithDescriptor:descriptor];
    descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
        width:context->width height:context->height mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    context->depth_texture = [renderer->device
        newTextureWithDescriptor:descriptor];
    if (context->texture == nil || context->depth_texture == nil) {
        [context->texture release];
        [context->depth_texture release];
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_RENDERER;
    }
    context->z_function = (context->flags & GXMETAL_CONTEXT_Z16) ?
        GXMETAL_Z_LT : GXMETAL_Z_NONE;
    context->z_write = 1;
    context->blend = GXMETAL_BLEND_INTERPOLATE;
    context->fog.color[3] = 1.0f;
    context->fog.end = 1.0f;
    context->fog.max_depth = 1.0f;
    context->active = 1;
    return GXMETAL_ERROR_NONE;
}

static uint32_t gxmetal_metal_resource_create(
    GXMetalMetalRenderer *renderer, const GXMetalPacketView *packet)
{
    GXMetalMetalResource *resource = NULL;
    MTLTextureDescriptor *descriptor;
    uint32_t bytes_per_pixel;
    uint32_t max_levels = 1;
    uint32_t level_width;
    uint32_t level_height;
    uint32_t i;

    if (renderer->shared == NULL ||
        gxmetal_metal_find_resource(renderer, gxmetal_load_le32(
            packet->payload + GXMETAL_RESOURCE_ID_OFFSET)) != NULL) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    for (i = 0; i < GXMETAL_METAL_MAX_RESOURCES; i++) {
        if (!renderer->resources[i].active) {
            resource = &renderer->resources[i];
            break;
        }
    }
    if (resource == NULL) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    resource->id = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_ID_OFFSET);
    resource->width = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_WIDTH_OFFSET);
    resource->height = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_HEIGHT_OFFSET);
    resource->row_bytes = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_ROW_BYTES_OFFSET);
    resource->pixel_format = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET);
    resource->flags = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_FLAGS_OFFSET);
    resource->levels = gxmetal_load_le32(
        packet->payload + GXMETAL_RESOURCE_LEVELS_OFFSET);
    bytes_per_pixel = gxmetal_metal_resource_bytes_per_pixel(
        resource->pixel_format);
    level_width = resource->width;
    level_height = resource->height;
    while (level_width > 1 || level_height > 1) {
        level_width = level_width > 1 ? level_width >> 1 : 1;
        level_height = level_height > 1 ? level_height >> 1 : 1;
        max_levels++;
    }
    if (bytes_per_pixel == 0 || resource->width > GXMETAL_MAX_DIMENSION ||
        resource->height > GXMETAL_MAX_DIMENSION || resource->levels == 0 ||
        resource->levels > max_levels ||
        (resource->flags & ~GXMETAL_RESOURCE_FLIP_ORIGIN) != 0 ||
        resource->row_bytes < resource->width * bytes_per_pixel) {
        memset(resource, 0, sizeof(*resource));
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    descriptor = [[MTLTextureDescriptor alloc] init];
    descriptor.textureType = MTLTextureType2D;
    descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.width = resource->width;
    descriptor.height = resource->height;
    descriptor.mipmapLevelCount = resource->levels;
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;
    resource->texture = [renderer->device newTextureWithDescriptor:descriptor];
    [descriptor release];
    if (resource->texture == nil) {
        memset(resource, 0, sizeof(*resource));
        return GXMETAL_ERROR_RENDERER;
    }
    resource->active = 1;
    return GXMETAL_ERROR_NONE;
}

static void gxmetal_metal_convert_pixel(uint32_t format,
                                        const uint8_t *source,
                                        uint8_t *destination)
{
    uint16_t value;

    switch (format) {
    case GXMETAL_PIXEL_RGB555:
        value = (uint16_t)((uint16_t)source[0] << 8 | source[1]);
        destination[0] = (uint8_t)(((value >> 10) & 31) * 255 / 31);
        destination[1] = (uint8_t)(((value >> 5) & 31) * 255 / 31);
        destination[2] = (uint8_t)((value & 31) * 255 / 31);
        destination[3] = 255;
        break;
    case GXMETAL_PIXEL_RGB565:
        value = (uint16_t)((uint16_t)source[0] << 8 | source[1]);
        destination[0] = (uint8_t)(((value >> 11) & 31) * 255 / 31);
        destination[1] = (uint8_t)(((value >> 5) & 63) * 255 / 63);
        destination[2] = (uint8_t)((value & 31) * 255 / 31);
        destination[3] = 255;
        break;
    case GXMETAL_PIXEL_ARGB1555:
        value = (uint16_t)((uint16_t)source[0] << 8 | source[1]);
        destination[0] = (uint8_t)(((value >> 10) & 31) * 255 / 31);
        destination[1] = (uint8_t)(((value >> 5) & 31) * 255 / 31);
        destination[2] = (uint8_t)((value & 31) * 255 / 31);
        destination[3] = (value & 0x8000) ? 255 : 0;
        break;
    case GXMETAL_PIXEL_ARGB4444:
    case GXMETAL_PIXEL_ATI_ARGB4444:
        value = (uint16_t)((uint16_t)source[0] << 8 | source[1]);
        destination[0] = (uint8_t)(((value >> 8) & 15) * 17);
        destination[1] = (uint8_t)(((value >> 4) & 15) * 17);
        destination[2] = (uint8_t)((value & 15) * 17);
        destination[3] = (uint8_t)(((value >> 12) & 15) * 17);
        break;
    case GXMETAL_PIXEL_ARGB8888:
        destination[0] = source[1];
        destination[1] = source[2];
        destination[2] = source[3];
        destination[3] = source[0];
        break;
    case GXMETAL_PIXEL_RGB8888:
        destination[0] = source[1];
        destination[1] = source[2];
        destination[2] = source[3];
        destination[3] = 255;
        break;
    default:
        memset(destination, 0, 4);
        break;
    }
}

static uint32_t gxmetal_metal_resource_upload(
    GXMetalMetalRenderer *renderer, const GXMetalPacketView *packet)
{
    GXMetalMetalResource *resource = gxmetal_metal_find_resource(
        renderer, gxmetal_load_le32(packet->payload +
                                    GXMETAL_UPLOAD_RESOURCE_ID_OFFSET));
    uint32_t level = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_LEVEL_OFFSET);
    uint32_t offset = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET);
    uint32_t length = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_LENGTH_OFFSET);
    uint32_t row_bytes = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_ROW_BYTES_OFFSET);
    uint32_t width = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_WIDTH_OFFSET);
    uint32_t height = gxmetal_load_le32(
        packet->payload + GXMETAL_UPLOAD_HEIGHT_OFFSET);
    uint32_t expected_width;
    uint32_t expected_height;
    uint32_t bytes_per_pixel;
    uint8_t *converted;
    uint32_t x;
    uint32_t y;

    if (resource == NULL || level >= resource->levels ||
        renderer->shared == NULL ||
        !gxmetal_shared_range_valid(offset, length, renderer->shared_bytes,
                                    GXMETAL_PACKET_ALIGNMENT)) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    expected_width = resource->width >> level;
    expected_height = resource->height >> level;
    if (expected_width == 0) {
        expected_width = 1;
    }
    if (expected_height == 0) {
        expected_height = 1;
    }
    bytes_per_pixel = gxmetal_metal_resource_bytes_per_pixel(
        resource->pixel_format);
    if (width != expected_width || height != expected_height ||
        row_bytes < width * bytes_per_pixel ||
        (uint64_t)row_bytes * height > length) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    converted = malloc((size_t)width * height * 4);
    if (converted == NULL) {
        return GXMETAL_ERROR_RENDERER;
    }
    for (y = 0; y < height; y++) {
        uint32_t source_y =
            (resource->flags & GXMETAL_RESOURCE_FLIP_ORIGIN) ?
                height - y - 1 : y;
        const uint8_t *source = renderer->shared + offset +
                                source_y * row_bytes;
        for (x = 0; x < width; x++) {
            gxmetal_metal_convert_pixel(resource->pixel_format,
                source + x * bytes_per_pixel,
                converted + ((size_t)y * width + x) * 4);
        }
    }
    [resource->texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
        mipmapLevel:level withBytes:converted bytesPerRow:width * 4];
    free(converted);
    return GXMETAL_ERROR_NONE;
}

static uint32_t gxmetal_metal_resource_destroy(
    GXMetalMetalRenderer *renderer, const GXMetalPacketView *packet)
{
    GXMetalMetalResource *resource = gxmetal_metal_find_resource(
        renderer, gxmetal_load_le32(packet->payload +
                                    GXMETAL_DESTROY_RESOURCE_ID_OFFSET));
    uint32_t i;

    if (resource == NULL) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    for (i = 0; i < GXMETAL_METAL_MAX_CONTEXTS; i++) {
        if (renderer->contexts[i].active &&
            renderer->contexts[i].texture_id == resource->id) {
            renderer->contexts[i].texture_id = 0;
        }
    }
    [resource->texture release];
    memset(resource, 0, sizeof(*resource));
    return GXMETAL_ERROR_NONE;
}

static uint32_t gxmetal_metal_clear(GXMetalMetalRenderer *renderer,
                                    GXMetalMetalContext *context,
                                    const GXMetalPacketView *packet)
{
    uint32_t flags = gxmetal_load_le32(
        packet->payload + GXMETAL_CLEAR_FLAGS_OFFSET);
    int32_t left = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_CLEAR_RECT_OFFSET + GXMETAL_RECT_LEFT_OFFSET);
    int32_t top = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_CLEAR_RECT_OFFSET + GXMETAL_RECT_TOP_OFFSET);
    int32_t right = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_CLEAR_RECT_OFFSET + GXMETAL_RECT_RIGHT_OFFSET);
    int32_t bottom = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_CLEAR_RECT_OFFSET + GXMETAL_RECT_BOTTOM_OFFSET);
    GXMetalMetalVertex vertices[4];
    GXMetalMetalViewport viewport;
    MTLClearColor color;
    MTLScissorRect scissor;
    MTLScissorRect effective_scissor;
    GXMetalMetalFog no_fog = {0};
    GXMetalMetalAlphaTest no_alpha_test = {GXMETAL_ALPHA_TEST_NONE, 0.0f};
    float components[4];
    float depth;
    uint32_t i;

    if ((flags & (GXMETAL_CLEAR_COLOR | GXMETAL_CLEAR_DEPTH)) == 0) {
        return GXMETAL_ERROR_NONE;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (int32_t)context->width) {
        right = (int32_t)context->width;
    }
    if (bottom > (int32_t)context->height) {
        bottom = (int32_t)context->height;
    }
    if (left > right || top > bottom) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    if (!gxmetal_metal_effective_scissor(context, &effective_scissor)) {
        return GXMETAL_ERROR_NONE;
    }
    if (left < (int32_t)effective_scissor.x) {
        left = (int32_t)effective_scissor.x;
    }
    if (top < (int32_t)effective_scissor.y) {
        top = (int32_t)effective_scissor.y;
    }
    if (right > (int32_t)(effective_scissor.x +
                          effective_scissor.width)) {
        right = (int32_t)(effective_scissor.x + effective_scissor.width);
    }
    if (bottom > (int32_t)(effective_scissor.y +
                           effective_scissor.height)) {
        bottom = (int32_t)(effective_scissor.y + effective_scissor.height);
    }
    if (left >= right || top >= bottom) {
        return GXMETAL_ERROR_NONE;
    }
    components[0] = gxmetal_metal_load_float(
        packet->payload + GXMETAL_CLEAR_COLOR_R_OFFSET);
    components[1] = gxmetal_metal_load_float(
        packet->payload + GXMETAL_CLEAR_COLOR_G_OFFSET);
    components[2] = gxmetal_metal_load_float(
        packet->payload + GXMETAL_CLEAR_COLOR_B_OFFSET);
    components[3] = gxmetal_metal_load_float(
        packet->payload + GXMETAL_CLEAR_COLOR_A_OFFSET);
    depth = gxmetal_metal_load_float(
        packet->payload + GXMETAL_CLEAR_DEPTH_OFFSET);
    color = MTLClearColorMake(components[0], components[1], components[2],
                              components[3]);
    if (left == 0 && top == 0 && right == (int32_t)context->width &&
        bottom == (int32_t)context->height) {
        return gxmetal_metal_ensure_encoder(renderer, context, flags, color,
                                             depth) ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_RENDERER;
    }
    if (!gxmetal_metal_ensure_encoder(renderer, context, 0,
                                      MTLClearColorMake(0, 0, 0, 1), 1.0)) {
        return GXMETAL_ERROR_RENDERER;
    }
    memset(vertices, 0, sizeof(vertices));
    vertices[0].x = vertices[2].x = (float)left;
    vertices[1].x = vertices[3].x = (float)right;
    vertices[0].y = vertices[1].y = (float)top;
    vertices[2].y = vertices[3].y = (float)bottom;
    for (i = 0; i < 4; i++) {
        vertices[i].z = depth;
        vertices[i].r = components[0];
        vertices[i].g = components[1];
        vertices[i].b = components[2];
        vertices[i].a = components[3];
    }
    viewport.width = (float)context->width;
    viewport.height = (float)context->height;
    scissor.x = (NSUInteger)left;
    scissor.y = (NSUInteger)top;
    scissor.width = (NSUInteger)(right - left);
    scissor.height = (NSUInteger)(bottom - top);
    [context->encoder setScissorRect:scissor];
    [context->encoder setVertexBytes:vertices length:sizeof(vertices)
        atIndex:0];
    [context->encoder setVertexBytes:&viewport length:sizeof(viewport)
        atIndex:1];
    [context->encoder setFragmentBytes:&no_fog length:sizeof(no_fog)
        atIndex:0];
    [context->encoder setFragmentBytes:&no_alpha_test
        length:sizeof(no_alpha_test) atIndex:1];
    if (flags & GXMETAL_CLEAR_COLOR) {
        [context->encoder setRenderPipelineState:renderer->clear_pipeline];
        [context->encoder setDepthStencilState:
            renderer->depth_states[GXMETAL_Z_TRUE][0]];
        [context->encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
            vertexStart:0 vertexCount:4];
    }
    if (flags & GXMETAL_CLEAR_DEPTH) {
        [context->encoder setRenderPipelineState:
            renderer->depth_clear_pipeline];
        [context->encoder setDepthStencilState:
            renderer->depth_states[GXMETAL_Z_TRUE][1]];
        [context->encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
            vertexStart:0 vertexCount:4];
    }
    [context->encoder setScissorRect:effective_scissor];
    return GXMETAL_ERROR_NONE;
}

static int gxmetal_metal_read_vertex(const GXMetalMetalContext *context,
                                     const uint8_t *source,
                                     GXMetalMetalVertex *vertex)
{
    vertex->x = gxmetal_metal_load_float(source + GXMETAL_VERTEX_X_OFFSET);
    vertex->y = gxmetal_metal_load_float(source + GXMETAL_VERTEX_Y_OFFSET);
    vertex->z = gxmetal_metal_load_float(source + GXMETAL_VERTEX_Z_OFFSET);
    vertex->inv_w = 1.0f;
    if (context->fog.mode_and_padding[0] >= GXMETAL_FOG_LINEAR &&
        context->fog.mode_and_padding[1] != 0) {
        vertex->inv_w = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_INV_W_OFFSET);
    }
    vertex->r = gxmetal_metal_load_float(source + GXMETAL_VERTEX_R_OFFSET);
    vertex->g = gxmetal_metal_load_float(source + GXMETAL_VERTEX_G_OFFSET);
    vertex->b = gxmetal_metal_load_float(source + GXMETAL_VERTEX_B_OFFSET);
    vertex->a = gxmetal_metal_load_float(source + GXMETAL_VERTEX_A_OFFSET);
    return isfinite(vertex->x) && isfinite(vertex->y) &&
           isfinite(vertex->z) && vertex->z >= 0.0f && vertex->z <= 1.0f &&
           isfinite(vertex->inv_w) && vertex->inv_w > 0.0f &&
           isfinite(vertex->r) && isfinite(vertex->g) &&
           isfinite(vertex->b) && isfinite(vertex->a);
}

static uint32_t gxmetal_metal_draw(GXMetalMetalRenderer *renderer,
                                   GXMetalMetalContext *context,
                                   const GXMetalPacketView *packet)
{
    const uint8_t *source = packet->payload + GXMETAL_DRAW_VERTICES_OFFSET;
    GXMetalMetalVertex *vertices;
    GXMetalMetalVertex *draw_vertices;
    GXMetalMetalViewport viewport;
    MTLPrimitiveType metal_primitive;
    uint32_t primitive = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_PRIMITIVE_OFFSET);
    uint32_t count = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET);
    uint32_t draw_count = count;
    uint32_t i;

    vertices = malloc((size_t)count * sizeof(*vertices));
    if (vertices == NULL) {
        return GXMETAL_ERROR_RENDERER;
    }
    for (i = 0; i < count; i++) {
        if (!gxmetal_metal_read_vertex(context,
                source + i * GXMETAL_GOURAUD_VERTEX_BYTES,
                &vertices[i])) {
            free(vertices);
            return GXMETAL_ERROR_BAD_PACKET;
        }
    }
    draw_vertices = vertices;
    switch (primitive) {
    case GXMETAL_PRIMITIVE_POINT:
        metal_primitive = MTLPrimitiveTypePoint;
        break;
    case GXMETAL_PRIMITIVE_LINE:
        metal_primitive = MTLPrimitiveTypeLine;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE:
        metal_primitive = MTLPrimitiveTypeTriangle;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE_STRIP:
        metal_primitive = MTLPrimitiveTypeTriangleStrip;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE_FAN:
        draw_count = (count - 2) * 3;
        draw_vertices = malloc((size_t)draw_count * sizeof(*draw_vertices));
        if (draw_vertices == NULL) {
            free(vertices);
            return GXMETAL_ERROR_RENDERER;
        }
        for (i = 0; i < count - 2; i++) {
            draw_vertices[i * 3] = vertices[0];
            draw_vertices[i * 3 + 1] = vertices[i + 1];
            draw_vertices[i * 3 + 2] = vertices[i + 2];
        }
        metal_primitive = MTLPrimitiveTypeTriangle;
        break;
    default:
        free(vertices);
        return GXMETAL_ERROR_BAD_PACKET;
    }

    if (!gxmetal_metal_ensure_encoder(renderer, context, 0,
                                      MTLClearColorMake(0, 0, 0, 1), 1.0)) {
        if (draw_vertices != vertices) {
            free(draw_vertices);
        }
        free(vertices);
        return GXMETAL_ERROR_RENDERER;
    }
    if (!gxmetal_metal_apply_scissor(context)) {
        if (draw_vertices != vertices) {
            free(draw_vertices);
        }
        free(vertices);
        return GXMETAL_ERROR_NONE;
    }
    viewport.width = (float)context->width;
    viewport.height = (float)context->height;
    [context->encoder setRenderPipelineState:
        renderer->pipelines[context->blend <= GXMETAL_BLEND_INTERPOLATE ?
                            context->blend : GXMETAL_BLEND_INTERPOLATE]];
    [context->encoder setDepthStencilState:
        renderer->depth_states[context->z_function <= GXMETAL_Z_FALSE ?
                               context->z_function : GXMETAL_Z_NONE]
                              [context->z_write != 0]];
    [context->encoder setVertexBytes:draw_vertices
        length:(NSUInteger)draw_count * sizeof(*draw_vertices) atIndex:0];
    [context->encoder setVertexBytes:&viewport
        length:sizeof(viewport) atIndex:1];
    [context->encoder setFragmentBytes:&context->fog
        length:sizeof(context->fog) atIndex:0];
    [context->encoder setFragmentBytes:&context->alpha_test
        length:sizeof(context->alpha_test) atIndex:1];
    [context->encoder drawPrimitives:metal_primitive vertexStart:0
        vertexCount:draw_count];
    if (draw_vertices != vertices) {
        free(draw_vertices);
    }
    free(vertices);
    return GXMETAL_ERROR_NONE;
}

static int gxmetal_metal_read_texture_vertex(
    const GXMetalMetalContext *context, const uint8_t *source,
    GXMetalMetalTextureVertex *vertex)
{
    uint32_t texture_op = context->texture_op;

    memset(vertex, 0, sizeof(*vertex));
    vertex->x = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_X_OFFSET);
    vertex->y = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_Y_OFFSET);
    vertex->z = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_Z_OFFSET);
    vertex->inv_w = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_INV_W_OFFSET);
    vertex->a = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_A_OFFSET);
    vertex->u_over_w = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_U_OVER_W_OFFSET);
    vertex->v_over_w = gxmetal_metal_load_float(
        source + GXMETAL_VERTEX_V_OVER_W_OFFSET);

    if (!isfinite(vertex->x) || !isfinite(vertex->y) ||
        !isfinite(vertex->z) || !isfinite(vertex->inv_w) ||
        !isfinite(vertex->a) || !isfinite(vertex->u_over_w) ||
        !isfinite(vertex->v_over_w) || vertex->z < 0.0f ||
        vertex->z > 1.0f || vertex->inv_w <= 0.0f) {
        return 0;
    }

    /* RAVE only requires color, diffuse, and specular components when the
     * corresponding texture operation consumes them. QuickDraw 3D leaves
     * the other fields undefined, so never validate or forward those bytes.
     * Metal will clip finite screen coordinates outside the draw context. */
    vertex->r = vertex->g = vertex->b = 1.0f;
    vertex->kd_r = vertex->kd_g = vertex->kd_b = 1.0f;
    if (texture_op & GXMETAL_TEXTURE_DECAL) {
        vertex->r = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_R_OFFSET);
        vertex->g = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_G_OFFSET);
        vertex->b = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_B_OFFSET);
        if (!isfinite(vertex->r) || !isfinite(vertex->g) ||
            !isfinite(vertex->b)) {
            return 0;
        }
    }
    if (texture_op & GXMETAL_TEXTURE_MODULATE) {
        vertex->kd_r = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KD_R_OFFSET);
        vertex->kd_g = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KD_G_OFFSET);
        vertex->kd_b = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KD_B_OFFSET);
        if (!isfinite(vertex->kd_r) || !isfinite(vertex->kd_g) ||
            !isfinite(vertex->kd_b)) {
            return 0;
        }
    }
    if (texture_op & GXMETAL_TEXTURE_HIGHLIGHT) {
        vertex->ks_r = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KS_R_OFFSET);
        vertex->ks_g = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KS_G_OFFSET);
        vertex->ks_b = gxmetal_metal_load_float(
            source + GXMETAL_VERTEX_KS_B_OFFSET);
        if (!isfinite(vertex->ks_r) || !isfinite(vertex->ks_g) ||
            !isfinite(vertex->ks_b)) {
            return 0;
        }
    }
    return 1;
}

static uint32_t gxmetal_metal_draw_textured(
    GXMetalMetalRenderer *renderer, GXMetalMetalContext *context,
    const GXMetalPacketView *packet)
{
    const uint8_t *source = packet->payload + GXMETAL_DRAW_VERTICES_OFFSET;
    GXMetalMetalResource *resource = gxmetal_metal_find_resource(
        renderer, context->texture_id);
    GXMetalMetalTextureVertex *vertices;
    GXMetalMetalTextureVertex *draw_vertices;
    GXMetalMetalViewport viewport;
    MTLPrimitiveType metal_primitive;
    uint32_t primitive = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_PRIMITIVE_OFFSET);
    uint32_t count = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET);
    uint32_t draw_count = count;
    uint32_t filter = context->texture_filter <= GXMETAL_TEXTURE_FILTER_BEST ?
        context->texture_filter : GXMETAL_TEXTURE_FILTER_FAST;
    uint32_t address_mode = context->texture_wrap_u |
                            (context->texture_wrap_v << 1);
    uint32_t i;

    if (resource == NULL) {
        return GXMETAL_ERROR_BAD_RESOURCE;
    }
    if (context->texture_op & GXMETAL_TEXTURE_SHRINK) {
        address_mode = 3;
    }
    vertices = malloc((size_t)count * sizeof(*vertices));
    if (vertices == NULL) {
        return GXMETAL_ERROR_RENDERER;
    }
    for (i = 0; i < count; i++) {
        if (!gxmetal_metal_read_texture_vertex(
                context, source + i * GXMETAL_TEXTURE_VERTEX_BYTES,
                &vertices[i])) {
            free(vertices);
            return GXMETAL_ERROR_BAD_PACKET;
        }
    }
    draw_vertices = vertices;
    switch (primitive) {
    case GXMETAL_PRIMITIVE_POINT:
        metal_primitive = MTLPrimitiveTypePoint;
        break;
    case GXMETAL_PRIMITIVE_LINE:
        metal_primitive = MTLPrimitiveTypeLine;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE:
        metal_primitive = MTLPrimitiveTypeTriangle;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE_STRIP:
        metal_primitive = MTLPrimitiveTypeTriangleStrip;
        break;
    case GXMETAL_PRIMITIVE_TRIANGLE_FAN:
        draw_count = (count - 2) * 3;
        draw_vertices = malloc((size_t)draw_count * sizeof(*draw_vertices));
        if (draw_vertices == NULL) {
            free(vertices);
            return GXMETAL_ERROR_RENDERER;
        }
        for (i = 0; i < count - 2; i++) {
            draw_vertices[i * 3] = vertices[0];
            draw_vertices[i * 3 + 1] = vertices[i + 1];
            draw_vertices[i * 3 + 2] = vertices[i + 2];
        }
        metal_primitive = MTLPrimitiveTypeTriangle;
        break;
    default:
        free(vertices);
        return GXMETAL_ERROR_BAD_PACKET;
    }
    if (!gxmetal_metal_ensure_encoder(renderer, context, 0,
                                      MTLClearColorMake(0, 0, 0, 1), 1.0)) {
        if (draw_vertices != vertices) {
            free(draw_vertices);
        }
        free(vertices);
        return GXMETAL_ERROR_RENDERER;
    }
    if (!gxmetal_metal_apply_scissor(context)) {
        if (draw_vertices != vertices) {
            free(draw_vertices);
        }
        free(vertices);
        return GXMETAL_ERROR_NONE;
    }
    viewport.width = (float)context->width;
    viewport.height = (float)context->height;
    [context->encoder setRenderPipelineState:
        renderer->texture_pipelines[
            context->blend <= GXMETAL_BLEND_INTERPOLATE ?
            context->blend : GXMETAL_BLEND_INTERPOLATE]];
    [context->encoder setDepthStencilState:
        renderer->depth_states[context->z_function <= GXMETAL_Z_FALSE ?
                               context->z_function : GXMETAL_Z_NONE]
                              [context->z_write != 0]];
    [context->encoder setVertexBytes:draw_vertices
        length:(NSUInteger)draw_count * sizeof(*draw_vertices) atIndex:0];
    [context->encoder setVertexBytes:&viewport length:sizeof(viewport)
        atIndex:1];
    [context->encoder setFragmentTexture:resource->texture atIndex:0];
    [context->encoder setFragmentSamplerState:
        renderer->samplers[filter][address_mode] atIndex:0];
    [context->encoder setFragmentBytes:&context->texture_op
        length:sizeof(context->texture_op) atIndex:0];
    [context->encoder setFragmentBytes:&context->fog
        length:sizeof(context->fog) atIndex:1];
    [context->encoder setFragmentBytes:&context->alpha_test
        length:sizeof(context->alpha_test) atIndex:2];
    [context->encoder drawPrimitives:metal_primitive vertexStart:0
        vertexCount:draw_count];
    if (draw_vertices != vertices) {
        free(draw_vertices);
    }
    free(vertices);
    return GXMETAL_ERROR_NONE;
}

static uint8_t gxmetal_metal_to_u8(uint8_t value)
{
    return value;
}

static int gxmetal_metal_wait_render(GXMetalMetalContext *context)
{
    if (context->command_buffer == nil) {
        return 1;
    }
    [context->command_buffer waitUntilCompleted];
    if (context->command_buffer.status == MTLCommandBufferStatusError) {
        fprintf(stderr, "GXMetal: Metal command buffer failed: %s\n",
                context->command_buffer.error.localizedDescription.UTF8String);
        return 0;
    }
    return 1;
}

static int gxmetal_metal_present_direct(GXMetalMetalRenderer *renderer,
                                        GXMetalMetalContext *context,
                                        uint32_t left, uint32_t top,
                                        uint32_t width, uint32_t height)
{
    GXMetalMetalPresent present;
    id<MTLCommandBuffer> command_buffer;
    id<MTLComputeCommandEncoder> encoder;
    NSUInteger thread_width;
    NSUInteger thread_height;

    if (!gxmetal_metal_direct_present_available(renderer)) {
        return 0;
    }
    memset(&present, 0, sizeof(present));
    present.framebuffer_offset = context->framebuffer_offset;
    present.row_bytes = context->row_bytes;
    present.pixel_format = context->pixel_format;
    present.left = left;
    present.top = top;
    present.width = width;
    present.height = height;

    command_buffer = [[renderer->command_queue commandBuffer] retain];
    if (command_buffer == nil) {
        return 0;
    }
    encoder = [[command_buffer computeCommandEncoder] retain];
    if (encoder == nil) {
        [command_buffer release];
        return 0;
    }
    [encoder setComputePipelineState:renderer->present_pipeline];
    [encoder setTexture:context->texture atIndex:0];
    [encoder setBuffer:renderer->framebuffer_buffer offset:0 atIndex:0];
    [encoder setBytes:&present length:sizeof(present) atIndex:1];
    thread_width = renderer->present_pipeline.threadExecutionWidth;
    if (thread_width == 0) {
        thread_width = 1;
    }
    thread_height = renderer->present_pipeline.maxTotalThreadsPerThreadgroup /
                    thread_width;
    if (thread_height == 0) {
        thread_height = 1;
    } else if (thread_height > 16) {
        thread_height = 16;
    }
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(thread_width, thread_height, 1)];
    [encoder endEncoding];
    [encoder release];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    if (command_buffer.status == MTLCommandBufferStatusError) {
        fprintf(stderr, "GXMetal: direct Metal present failed: %s\n",
                command_buffer.error.localizedDescription.UTF8String);
        [command_buffer release];
        return 0;
    }
    [command_buffer release];
    return 1;
}

static uint32_t gxmetal_metal_present(GXMetalMetalRenderer *renderer,
                                      GXMetalMetalContext *context,
                                      const GXMetalPacketView *packet)
{
    uint8_t *pixels;
    int32_t left = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_RECT_LEFT_OFFSET);
    int32_t top = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_RECT_TOP_OFFSET);
    int32_t right = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_RECT_RIGHT_OFFSET);
    int32_t bottom = (int32_t)gxmetal_load_le32(
        packet->payload + GXMETAL_RECT_BOTTOM_OFFSET);
    uint32_t region_width;
    uint32_t region_height;
    uint32_t source_row_bytes;
    uint32_t bytes_per_pixel = gxmetal_metal_bytes_per_pixel(
        context->pixel_format);
    uint32_t x;
    uint32_t y;

    if (left > right || top > bottom) {
        return GXMETAL_ERROR_BAD_PACKET;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > (int32_t)context->width) {
        right = (int32_t)context->width;
    }
    if (bottom > (int32_t)context->height) {
        bottom = (int32_t)context->height;
    }
    if (left < (int32_t)context->clip_left) {
        left = (int32_t)context->clip_left;
    }
    if (top < (int32_t)context->clip_top) {
        top = (int32_t)context->clip_top;
    }
    if (right > (int32_t)context->clip_right) {
        right = (int32_t)context->clip_right;
    }
    if (bottom > (int32_t)context->clip_bottom) {
        bottom = (int32_t)context->clip_bottom;
    }

    if (context->command_buffer != nil) {
        if (context->encoder != nil) {
            [context->encoder endEncoding];
            [context->encoder release];
            context->encoder = nil;
        }
        if (!context->committed) {
            [context->command_buffer commit];
            context->committed = 1;
        }
    }

    if (left >= right || top >= bottom) {
        if (!gxmetal_metal_wait_render(context)) {
            gxmetal_metal_release_frame(context);
            return GXMETAL_ERROR_RENDERER;
        }
        gxmetal_metal_release_frame(context);
        return GXMETAL_ERROR_NONE;
    }
    region_width = (uint32_t)(right - left);
    region_height = (uint32_t)(bottom - top);
    if (gxmetal_metal_present_direct(renderer, context,
                                     (uint32_t)left, (uint32_t)top,
                                     region_width, region_height)) {
        renderer->direct_present_count++;
        if (!gxmetal_metal_wait_render(context)) {
            gxmetal_metal_release_frame(context);
            return GXMETAL_ERROR_RENDERER;
        }
        gxmetal_metal_release_frame(context);
        return GXMETAL_ERROR_NONE;
    }
    if (!gxmetal_metal_wait_render(context)) {
        gxmetal_metal_release_frame(context);
        return GXMETAL_ERROR_RENDERER;
    }
    renderer->fallback_present_count++;
    source_row_bytes = region_width * 4;
    pixels = malloc((size_t)source_row_bytes * region_height);
    if (pixels == NULL) {
        gxmetal_metal_release_frame(context);
        return GXMETAL_ERROR_RENDERER;
    }
    [context->texture getBytes:pixels bytesPerRow:source_row_bytes
        fromRegion:MTLRegionMake2D((NSUInteger)left, (NSUInteger)top,
                                   region_width, region_height)
        mipmapLevel:0];
    for (y = 0; y < region_height; y++) {
        const uint8_t *source = pixels + y * source_row_bytes;
        uint8_t *destination = renderer->framebuffer +
            context->framebuffer_offset + ((uint32_t)top + y) *
            context->row_bytes + (uint32_t)left * bytes_per_pixel;
        for (x = 0; x < region_width; x++) {
            uint8_t r = gxmetal_metal_to_u8(source[x * 4]);
            uint8_t g = gxmetal_metal_to_u8(source[x * 4 + 1]);
            uint8_t b = gxmetal_metal_to_u8(source[x * 4 + 2]);
            uint8_t a = gxmetal_metal_to_u8(source[x * 4 + 3]);
            if (context->pixel_format == GXMETAL_PIXEL_RGB555) {
                uint16_t value = (uint16_t)(((uint16_t)(r >> 3) << 10) |
                                            ((uint16_t)(g >> 3) << 5) |
                                            (uint16_t)(b >> 3));
                destination[x * bytes_per_pixel] = (uint8_t)(value >> 8);
                destination[x * bytes_per_pixel + 1] = (uint8_t)value;
            } else {
                destination[x * bytes_per_pixel] =
                    context->pixel_format == GXMETAL_PIXEL_ARGB8888 ? a : 0;
                destination[x * bytes_per_pixel + 1] = r;
                destination[x * bytes_per_pixel + 2] = g;
                destination[x * bytes_per_pixel + 3] = b;
            }
        }
    }
    free(pixels);
    gxmetal_metal_release_frame(context);
    return GXMETAL_ERROR_NONE;
}

static MTLCompareFunction gxmetal_metal_compare(uint32_t function)
{
    switch (function) {
    case GXMETAL_Z_LT:    return MTLCompareFunctionLess;
    case GXMETAL_Z_EQ:    return MTLCompareFunctionEqual;
    case GXMETAL_Z_LE:    return MTLCompareFunctionLessEqual;
    case GXMETAL_Z_GT:    return MTLCompareFunctionGreater;
    case GXMETAL_Z_NE:    return MTLCompareFunctionNotEqual;
    case GXMETAL_Z_GE:    return MTLCompareFunctionGreaterEqual;
    case GXMETAL_Z_TRUE:  return MTLCompareFunctionAlways;
    case GXMETAL_Z_FALSE: return MTLCompareFunctionNever;
    case GXMETAL_Z_NONE:
    default:              return MTLCompareFunctionAlways;
    }
}

static id<MTLRenderPipelineState> gxmetal_metal_make_pipeline(
    GXMetalMetalRenderer *renderer, id<MTLFunction> vertex,
    id<MTLFunction> fragment, uint32_t blend, int color_write,
    NSError **error)
{
    MTLRenderPipelineDescriptor *descriptor =
        [[MTLRenderPipelineDescriptor alloc] init];
    MTLRenderPipelineColorAttachmentDescriptor *attachment;
    id<MTLRenderPipelineState> pipeline;

    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    attachment = descriptor.colorAttachments[0];
    attachment.writeMask = color_write ? MTLColorWriteMaskAll :
                                         MTLColorWriteMaskNone;
    if (blend <= GXMETAL_BLEND_INTERPOLATE) {
        attachment.blendingEnabled = YES;
        attachment.rgbBlendOperation = MTLBlendOperationAdd;
        attachment.alphaBlendOperation = MTLBlendOperationAdd;
        attachment.sourceRGBBlendFactor =
            blend == GXMETAL_BLEND_PREMULTIPLY ? MTLBlendFactorOne :
                                                MTLBlendFactorSourceAlpha;
        attachment.destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
    }
    pipeline = [renderer->device
        newRenderPipelineStateWithDescriptor:descriptor error:error];
    [descriptor release];
    return pipeline;
}

static int gxmetal_metal_make_depth_states(GXMetalMetalRenderer *renderer)
{
    uint32_t function;
    uint32_t write;

    for (function = GXMETAL_Z_NONE; function <= GXMETAL_Z_FALSE; function++) {
        for (write = 0; write < 2; write++) {
            MTLDepthStencilDescriptor *descriptor =
                [[MTLDepthStencilDescriptor alloc] init];
            descriptor.depthCompareFunction = gxmetal_metal_compare(function);
            descriptor.depthWriteEnabled = write != 0 &&
                                           function != GXMETAL_Z_NONE;
            renderer->depth_states[function][write] = [renderer->device
                newDepthStencilStateWithDescriptor:descriptor];
            [descriptor release];
            if (renderer->depth_states[function][write] == nil) {
                return 0;
            }
        }
    }
    return 1;
}

static uint32_t gxmetal_metal_set_state(GXMetalMetalContext *context,
                                        const GXMetalPacketView *packet)
{
    uint32_t tag = gxmetal_load_le32(
        packet->payload + GXMETAL_STATE_TAG_OFFSET);
    uint32_t type = gxmetal_load_le32(
        packet->payload + GXMETAL_STATE_TYPE_OFFSET);
    uint32_t value = gxmetal_load_le32(
        packet->payload + GXMETAL_STATE_VALUE_OFFSET);

    if (tag == GXMETAL_STATE_TEXTURE) {
        if (type != GXMETAL_STATE_RESOURCE) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->texture_id = value;
        return GXMETAL_ERROR_NONE;
    }
    if (type == GXMETAL_STATE_FLOAT32) {
        float float_value = gxmetal_metal_load_float(
            packet->payload + GXMETAL_STATE_VALUE_OFFSET);

        if (!isfinite(float_value)) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        switch (tag) {
        case GXMETAL_STATE_FOG_COLOR_A:
            context->fog.color[3] = float_value;
            break;
        case GXMETAL_STATE_FOG_COLOR_R:
            context->fog.color[0] = float_value;
            break;
        case GXMETAL_STATE_FOG_COLOR_G:
            context->fog.color[1] = float_value;
            break;
        case GXMETAL_STATE_FOG_COLOR_B:
            context->fog.color[2] = float_value;
            break;
        case GXMETAL_STATE_FOG_START:
            context->fog.start = float_value;
            break;
        case GXMETAL_STATE_FOG_END:
            context->fog.end = float_value;
            break;
        case GXMETAL_STATE_FOG_DENSITY:
            context->fog.density = float_value;
            break;
        case GXMETAL_STATE_FOG_MAX_DEPTH:
            context->fog.max_depth = float_value;
            break;
        case GXMETAL_STATE_ALPHA_TEST_REFERENCE:
            context->alpha_test.reference = float_value;
            break;
        default:
            break;
        }
        return GXMETAL_ERROR_NONE;
    }
    if (type != GXMETAL_STATE_UINT32) {
        return GXMETAL_ERROR_NONE;
    }
    switch (tag) {
    case GXMETAL_STATE_Z_FUNCTION:
        if (value > GXMETAL_Z_FALSE) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->z_function = value;
        break;
    case GXMETAL_STATE_Z_BUFFER_MASK:
        if (value > 1) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->z_write = value;
        break;
    case GXMETAL_STATE_BLEND:
        if (value > GXMETAL_BLEND_OPENGL) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->blend = value;
        break;
    case GXMETAL_STATE_TEXTURE_FILTER:
        if (value > GXMETAL_TEXTURE_FILTER_BEST) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->texture_filter = value;
        break;
    case GXMETAL_STATE_TEXTURE_OP:
        context->texture_op = value;
        break;
    case GXMETAL_STATE_PERSPECTIVE_Z:
        if (value > 1) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        /* The second word is shader-visible padding in the fog constants.
         * RAVE only requires Gouraud invW when perspective-Z is enabled;
         * otherwise depth fog is derived from the normalized Z coordinate. */
        context->fog.mode_and_padding[1] = value;
        break;
    case GXMETAL_STATE_FOG_MODE:
        if (value > GXMETAL_FOG_EXPONENTIAL_SQUARED) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->fog.mode_and_padding[0] = value;
        break;
    case GXMETAL_STATE_ALPHA_TEST_FUNCTION:
        if (value > GXMETAL_ALPHA_TEST_TRUE) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->alpha_test.function = value;
        break;
    case GXMETAL_STATE_TEXTURE_WRAP_U:
        if (value > GXMETAL_TEXTURE_WRAP_CLAMP) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->texture_wrap_u = value;
        break;
    case GXMETAL_STATE_TEXTURE_WRAP_V:
        if (value > GXMETAL_TEXTURE_WRAP_CLAMP) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        context->texture_wrap_v = value;
        break;
    case GXMETAL_STATE_SCISSOR_LEFT:
        context->scissor_left = value < context->width ?
            value : context->width;
        break;
    case GXMETAL_STATE_SCISSOR_TOP:
        context->scissor_top = value < context->height ?
            value : context->height;
        break;
    case GXMETAL_STATE_SCISSOR_RIGHT:
        context->scissor_right = value < context->width ?
            value : context->width;
        break;
    case GXMETAL_STATE_SCISSOR_BOTTOM:
        context->scissor_bottom = value < context->height ?
            value : context->height;
        break;
    default:
        break;
    }
    return GXMETAL_ERROR_NONE;
}

GXMetalMetalRenderer *gxmetal_metal_create(void *framebuffer,
                                            uint32_t framebuffer_bytes,
                                            void *shared,
                                            uint32_t shared_bytes)
{
    GXMetalMetalRenderer *renderer;
    id<MTLLibrary> library;
    id<MTLFunction> vertex;
    id<MTLFunction> fragment;
    id<MTLFunction> texture_vertex;
    id<MTLFunction> texture_fragment;
    id<MTLFunction> present_function;
    NSString *shader_source;
    NSError *error = nil;
    long page_size;
    uint32_t i;

    if (framebuffer == NULL || framebuffer_bytes == 0) {
        return NULL;
    }
    renderer = calloc(1, sizeof(*renderer));
    if (renderer == NULL) {
        return NULL;
    }
    renderer->framebuffer = framebuffer;
    renderer->framebuffer_bytes = framebuffer_bytes;
    renderer->shared = shared;
    renderer->shared_bytes = shared_bytes;
    renderer->device = [MTLCreateSystemDefaultDevice() retain];
    if (renderer->device == nil) {
        free(renderer);
        return NULL;
    }
    renderer->command_queue = [renderer->device newCommandQueue];
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size > 0 &&
        ((uintptr_t)framebuffer % (uintptr_t)page_size) == 0 &&
        (framebuffer_bytes % (uint32_t)page_size) == 0) {
        renderer->framebuffer_buffer = [renderer->device
            newBufferWithBytesNoCopy:framebuffer
            length:framebuffer_bytes
            options:MTLResourceStorageModeShared
            deallocator:nil];
    }
    shader_source = [[kGXMetalShaderSource
        stringByAppendingString:kGXMetalTextureShaderSource]
        stringByAppendingString:kGXMetalPresentShaderSource];
    library = [renderer->device newLibraryWithSource:shader_source
              options:nil error:&error];
    if (library == nil || renderer->command_queue == nil) {
        fprintf(stderr, "GXMetal: cannot initialize Metal: %s\n",
                error.localizedDescription.UTF8String);
        gxmetal_metal_destroy(renderer);
        [library release];
        return NULL;
    }
    vertex = [library newFunctionWithName:@"gxmetal_vertex"];
    fragment = [library newFunctionWithName:@"gxmetal_fragment"];
    texture_vertex = [library newFunctionWithName:@"gxmetal_texture_vertex"];
    texture_fragment = [library
        newFunctionWithName:@"gxmetal_texture_fragment"];
    present_function = [library newFunctionWithName:@"gxmetal_present"];
    for (i = 0; i < 3; i++) {
        renderer->pipelines[i] = gxmetal_metal_make_pipeline(
            renderer, vertex, fragment, i, 1, &error);
        renderer->texture_pipelines[i] = gxmetal_metal_make_pipeline(
            renderer, texture_vertex, texture_fragment, i, 1, &error);
    }
    renderer->depth_clear_pipeline = gxmetal_metal_make_pipeline(
        renderer, vertex, fragment, UINT32_MAX, 0, &error);
    renderer->clear_pipeline = gxmetal_metal_make_pipeline(
        renderer, vertex, fragment, UINT32_MAX, 1, &error);
    if (present_function != nil) {
        renderer->present_pipeline = [renderer->device
            newComputePipelineStateWithFunction:present_function
            error:&error];
    }
    [vertex release];
    [fragment release];
    [texture_vertex release];
    [texture_fragment release];
    [present_function release];
    [library release];
    if (renderer->pipelines[0] == nil || renderer->pipelines[1] == nil ||
        renderer->pipelines[2] == nil ||
        renderer->texture_pipelines[0] == nil ||
        renderer->texture_pipelines[1] == nil ||
        renderer->texture_pipelines[2] == nil ||
        renderer->clear_pipeline == nil ||
        !gxmetal_metal_make_depth_states(renderer)) {
        fprintf(stderr, "GXMetal: cannot create Metal pipeline: %s\n",
                error.localizedDescription.UTF8String);
        gxmetal_metal_destroy(renderer);
        return NULL;
    }
    for (i = 0; i < 3; i++) {
        uint32_t address_mode;
        for (address_mode = 0; address_mode < 4; address_mode++) {
            MTLSamplerDescriptor *sampler =
                [[MTLSamplerDescriptor alloc] init];
            sampler.minFilter = i == GXMETAL_TEXTURE_FILTER_FAST ?
                MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
            sampler.magFilter = i == GXMETAL_TEXTURE_FILTER_FAST ?
                MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
            sampler.mipFilter = i == GXMETAL_TEXTURE_FILTER_BEST ?
                MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
            sampler.sAddressMode = (address_mode & 1) ?
                MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
            sampler.tAddressMode = (address_mode & 2) ?
                MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
            renderer->samplers[i][address_mode] = [renderer->device
                newSamplerStateWithDescriptor:sampler];
            [sampler release];
            if (renderer->samplers[i][address_mode] == nil) {
                gxmetal_metal_destroy(renderer);
                return NULL;
            }
        }
    }
    return renderer;
}

void gxmetal_metal_destroy(GXMetalMetalRenderer *renderer)
{
    uint32_t i;

    if (renderer == NULL) {
        return;
    }
    gxmetal_metal_reset(renderer);
    for (i = 0; i < 3; i++) {
        [renderer->pipelines[i] release];
        [renderer->texture_pipelines[i] release];
        {
            uint32_t address_mode;
            for (address_mode = 0; address_mode < 4; address_mode++) {
                [renderer->samplers[i][address_mode] release];
            }
        }
    }
    [renderer->clear_pipeline release];
    [renderer->depth_clear_pipeline release];
    [renderer->present_pipeline release];
    [renderer->framebuffer_buffer release];
    for (i = 0; i < 9; i++) {
        uint32_t write;
        for (write = 0; write < 2; write++) {
            [renderer->depth_states[i][write] release];
        }
    }
    [renderer->command_queue release];
    [renderer->device release];
    free(renderer);
}

int gxmetal_metal_direct_present_available(
    const GXMetalMetalRenderer *renderer)
{
    return renderer != NULL && renderer->present_pipeline != nil &&
           renderer->framebuffer_buffer != nil;
}

uint64_t gxmetal_metal_direct_present_count(
    const GXMetalMetalRenderer *renderer)
{
    return renderer != NULL ? renderer->direct_present_count : 0;
}

uint64_t gxmetal_metal_fallback_present_count(
    const GXMetalMetalRenderer *renderer)
{
    return renderer != NULL ? renderer->fallback_present_count : 0;
}

void gxmetal_metal_reset(GXMetalMetalRenderer *renderer)
{
    uint32_t i;
    if (renderer == NULL) {
        return;
    }
    for (i = 0; i < GXMETAL_METAL_MAX_CONTEXTS; i++) {
        if (renderer->contexts[i].active) {
            gxmetal_metal_release_context(&renderer->contexts[i]);
        }
    }
    for (i = 0; i < GXMETAL_METAL_MAX_RESOURCES; i++) {
        if (renderer->resources[i].active) {
            [renderer->resources[i].texture release];
            memset(&renderer->resources[i], 0,
                   sizeof(renderer->resources[i]));
        }
    }
}

uint32_t gxmetal_metal_dispatch(void *opaque,
                                const GXMetalPacketView *packet)
{
    GXMetalMetalRenderer *renderer = opaque;
    GXMetalMetalContext *context;

    @autoreleasepool {
        if (packet->opcode == GXMETAL_OP_CONTEXT_CREATE) {
            return gxmetal_metal_context_create(renderer, packet);
        }
        if (packet->opcode == GXMETAL_OP_TEXTURE_CREATE) {
            return gxmetal_metal_resource_create(renderer, packet);
        }
        if (packet->opcode == GXMETAL_OP_TEXTURE_UPLOAD) {
            return gxmetal_metal_resource_upload(renderer, packet);
        }
        if (packet->opcode == GXMETAL_OP_TEXTURE_DESTROY) {
            return gxmetal_metal_resource_destroy(renderer, packet);
        }
        context = gxmetal_metal_find_context(renderer, packet->context_id);
        if (context == NULL) {
            return GXMETAL_ERROR_BAD_CONTEXT;
        }
        switch (packet->opcode) {
        case GXMETAL_OP_CONTEXT_DESTROY:
            gxmetal_metal_release_context(context);
            return GXMETAL_ERROR_NONE;
        case GXMETAL_OP_BEGIN_FRAME:
            return gxmetal_metal_begin_frame(renderer, context) ?
                GXMETAL_ERROR_NONE : GXMETAL_ERROR_RENDERER;
        case GXMETAL_OP_END_FRAME:
            if (context->encoder != nil) {
                [context->encoder endEncoding];
                [context->encoder release];
                context->encoder = nil;
            }
            if (context->command_buffer != nil && !context->committed) {
                [context->command_buffer commit];
                context->committed = 1;
            }
            return GXMETAL_ERROR_NONE;
        case GXMETAL_OP_PRESENT:
            return gxmetal_metal_present(renderer, context, packet);
        case GXMETAL_OP_SET_STATE:
            return gxmetal_metal_set_state(context, packet);
        case GXMETAL_OP_CLEAR:
            return gxmetal_metal_clear(renderer, context, packet);
        case GXMETAL_OP_DRAW_GOURAUD:
            return gxmetal_metal_draw(renderer, context, packet);
        case GXMETAL_OP_DRAW_TEXTURED:
            return gxmetal_metal_draw_textured(renderer, context, packet);
        default:
            return GXMETAL_ERROR_BAD_OPCODE;
        }
    }
}
