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

#define GXMETAL_METAL_MAX_CONTEXTS 32u
#define GXMETAL_METAL_MAX_RESOURCES 256u

typedef struct GXMetalMetalVertex {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
} GXMetalMetalVertex;

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
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipelines[3];
    id<MTLRenderPipelineState> texture_pipelines[3];
    id<MTLRenderPipelineState> clear_pipeline;
    id<MTLRenderPipelineState> depth_clear_pipeline;
    id<MTLDepthStencilState> depth_states[9][2];
    id<MTLSamplerState> samplers[3][4];
    GXMetalMetalContext contexts[GXMETAL_METAL_MAX_CONTEXTS];
    GXMetalMetalResource resources[GXMETAL_METAL_MAX_RESOURCES];
};

static NSString *const kGXMetalShaderSource = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct GXVertex { float x; float y; float z; float r; float g; float b; float a; };\n"
    "struct GXViewport { float width; float height; };\n"
    "struct GXOut { float4 position [[position]]; float4 color; };\n"
    "vertex GXOut gxmetal_vertex(const device GXVertex *vertices [[buffer(0)]], "
    "                            constant GXViewport &viewport [[buffer(1)]], "
    "                            uint index [[vertex_id]]) {\n"
    "  GXVertex v = vertices[index];\n"
    "  GXOut out;\n"
    "  out.position = float4(v.x / viewport.width * 2.0 - 1.0, "
    "                        1.0 - v.y / viewport.height * 2.0, v.z, 1.0);\n"
    "  out.color = float4(v.r, v.g, v.b, v.a);\n"
    "  return out;\n"
    "}\n"
    "fragment float4 gxmetal_fragment(GXOut in [[stage_in]]) {\n"
    "  return clamp(in.color, 0.0, 1.0);\n"
    "}\n"
    "struct GXTextureVertex {\n"
    "  float x; float y; float z; float invW;\n"
    "  float r; float g; float b; float a;\n"
    "  float uOverW; float vOverW;\n"
    "  float kd_r; float kd_g; float kd_b;\n"
    "  float ks_r; float ks_g; float ks_b;\n"
    "};\n"
    "struct GXTextureOut {\n"
    "  float4 position [[position]]; float4 color; float2 uv;\n"
    "  float3 kd; float3 ks;\n"
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
    "  out.uv = float2(v.uOverW, v.vOverW) / safeInvW;\n"
    "  out.kd = float3(v.kd_r, v.kd_g, v.kd_b);\n"
    "  out.ks = float3(v.ks_r, v.ks_g, v.ks_b);\n"
    "  return out;\n"
    "}\n"
    "fragment float4 gxmetal_texture_fragment(\n"
    "    GXTextureOut in [[stage_in]],\n"
    "    texture2d<float> image [[texture(0)]],\n"
    "    sampler imageSampler [[sampler(0)]],\n"
    "    constant uint &operation [[buffer(0)]]) {\n"
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
    "  return clamp(result, 0.0, 1.0);\n"
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
    bytes_per_pixel = gxmetal_metal_bytes_per_pixel(context->pixel_format);
    end = (uint64_t)context->framebuffer_offset +
          (uint64_t)(context->height - 1) * context->row_bytes +
          (uint64_t)context->width * bytes_per_pixel;
    if (bytes_per_pixel == 0 ||
        context->row_bytes < (uint64_t)context->width * bytes_per_pixel ||
        end > renderer->framebuffer_bytes) {
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_BAD_CONTEXT;
    }

    descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:context->width height:context->height mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
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
    case GXMETAL_PIXEL_ARGB1555:
        value = (uint16_t)((uint16_t)source[0] << 8 | source[1]);
        destination[0] = (uint8_t)(((value >> 10) & 31) * 255 / 31);
        destination[1] = (uint8_t)(((value >> 5) & 31) * 255 / 31);
        destination[2] = (uint8_t)((value & 31) * 255 / 31);
        destination[3] = (value & 0x8000) ? 255 : 0;
        break;
    case GXMETAL_PIXEL_ARGB4444:
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
    if (left == right || top == bottom) {
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
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = context->width;
    scissor.height = context->height;
    [context->encoder setScissorRect:scissor];
    return GXMETAL_ERROR_NONE;
}

static int gxmetal_metal_read_vertex(const GXMetalMetalContext *context,
                                     const uint8_t *source,
                                     GXMetalMetalVertex *vertex)
{
    vertex->x = gxmetal_metal_load_float(source + GXMETAL_VERTEX_X_OFFSET);
    vertex->y = gxmetal_metal_load_float(source + GXMETAL_VERTEX_Y_OFFSET);
    vertex->z = gxmetal_metal_load_float(source + GXMETAL_VERTEX_Z_OFFSET);
    vertex->r = gxmetal_metal_load_float(source + GXMETAL_VERTEX_R_OFFSET);
    vertex->g = gxmetal_metal_load_float(source + GXMETAL_VERTEX_G_OFFSET);
    vertex->b = gxmetal_metal_load_float(source + GXMETAL_VERTEX_B_OFFSET);
    vertex->a = gxmetal_metal_load_float(source + GXMETAL_VERTEX_A_OFFSET);
    return isfinite(vertex->x) && isfinite(vertex->y) &&
           isfinite(vertex->z) && vertex->z >= 0.0f && vertex->z <= 1.0f &&
           isfinite(vertex->r) && isfinite(vertex->g) &&
           isfinite(vertex->b) && isfinite(vertex->a) &&
           vertex->x >= 0.0f && vertex->x <= (float)context->width &&
           vertex->y >= 0.0f && vertex->y <= (float)context->height;
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
        if (!gxmetal_metal_read_vertex(
                context, source + i * GXMETAL_GOURAUD_VERTEX_BYTES,
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
    float values[16];
    uint32_t i;

    for (i = 0; i < 16; i++) {
        values[i] = gxmetal_metal_load_float(source + i * 4);
        if (!isfinite(values[i])) {
            return 0;
        }
    }
    memcpy(vertex, values, sizeof(values));
    return vertex->x >= 0.0f && vertex->x <= (float)context->width &&
           vertex->y >= 0.0f && vertex->y <= (float)context->height &&
           vertex->z >= 0.0f && vertex->z <= 1.0f &&
           vertex->inv_w > 0.0f;
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

static uint32_t gxmetal_metal_present(GXMetalMetalRenderer *renderer,
                                      GXMetalMetalContext *context)
{
    uint8_t *pixels;
    uint32_t source_row_bytes = context->width * 4;
    uint32_t bytes_per_pixel = gxmetal_metal_bytes_per_pixel(
        context->pixel_format);
    uint32_t x;
    uint32_t y;

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
        [context->command_buffer waitUntilCompleted];
        if (context->command_buffer.status == MTLCommandBufferStatusError) {
            fprintf(stderr, "GXMetal: Metal command buffer failed: %s\n",
                    context->command_buffer.error.localizedDescription.UTF8String);
            gxmetal_metal_release_frame(context);
            return GXMETAL_ERROR_RENDERER;
        }
    }

    pixels = malloc((size_t)source_row_bytes * context->height);
    if (pixels == NULL) {
        gxmetal_metal_release_frame(context);
        return GXMETAL_ERROR_RENDERER;
    }
    [context->texture getBytes:pixels bytesPerRow:source_row_bytes
        fromRegion:MTLRegionMake2D(0, 0, context->width, context->height)
        mipmapLevel:0];
    for (y = 0; y < context->height; y++) {
        const uint8_t *source = pixels + y * source_row_bytes;
        uint8_t *destination = renderer->framebuffer +
            context->framebuffer_offset + y * context->row_bytes;
        for (x = 0; x < context->width; x++) {
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
    NSError *error = nil;
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
    library = [renderer->device newLibraryWithSource:kGXMetalShaderSource
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
    [vertex release];
    [fragment release];
    [texture_vertex release];
    [texture_fragment release];
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
            return gxmetal_metal_present(renderer, context);
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
