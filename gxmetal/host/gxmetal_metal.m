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

typedef struct GXMetalMetalVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
} GXMetalMetalVertex;

typedef struct GXMetalMetalViewport {
    float width;
    float height;
} GXMetalMetalViewport;

typedef struct GXMetalMetalContext {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t pixel_format;
    uint32_t framebuffer_offset;
    int active;
    int committed;
    id<MTLTexture> texture;
    id<MTLCommandBuffer> command_buffer;
    id<MTLRenderCommandEncoder> encoder;
} GXMetalMetalContext;

struct GXMetalMetalRenderer {
    uint8_t *framebuffer;
    uint32_t framebuffer_bytes;
    id<MTLDevice> device;
    id<MTLCommandQueue> command_queue;
    id<MTLRenderPipelineState> pipeline;
    GXMetalMetalContext contexts[GXMETAL_METAL_MAX_CONTEXTS];
};

static NSString *const kGXMetalShaderSource = @
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct GXVertex { float x; float y; float r; float g; float b; float a; };\n"
    "struct GXViewport { float width; float height; };\n"
    "struct GXOut { float4 position [[position]]; float4 color; };\n"
    "vertex GXOut gxmetal_vertex(const device GXVertex *vertices [[buffer(0)]], "
    "                            constant GXViewport &viewport [[buffer(1)]], "
    "                            uint index [[vertex_id]]) {\n"
    "  GXVertex v = vertices[index];\n"
    "  GXOut out;\n"
    "  out.position = float4(v.x / viewport.width * 2.0 - 1.0, "
    "                        1.0 - v.y / viewport.height * 2.0, 0.0, 1.0);\n"
    "  out.color = float4(v.r, v.g, v.b, v.a);\n"
    "  return out;\n"
    "}\n"
    "fragment float4 gxmetal_fragment(GXOut in [[stage_in]]) {\n"
    "  return clamp(in.color, 0.0, 1.0);\n"
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
                                        int clear, MTLClearColor clear_color)
{
    MTLRenderPassDescriptor *pass;

    if (context->command_buffer == nil &&
        !gxmetal_metal_begin_frame(renderer, context)) {
        return 0;
    }
    if (clear && context->encoder != nil) {
        [context->encoder endEncoding];
        [context->encoder release];
        context->encoder = nil;
    }
    if (context->encoder != nil) {
        return 1;
    }
    pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = context->texture;
    pass.colorAttachments[0].loadAction = clear ? MTLLoadActionClear :
                                                  MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = clear_color;
    context->encoder = [[context->command_buffer
        renderCommandEncoderWithDescriptor:pass] retain];
    if (context->encoder == nil) {
        return 0;
    }
    [context->encoder setRenderPipelineState:renderer->pipeline];
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
    if (context->texture == nil) {
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_RENDERER;
    }
    context->active = 1;
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
    uint32_t i;

    if ((flags & GXMETAL_CLEAR_COLOR) == 0) {
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
    color = MTLClearColorMake(components[0], components[1], components[2],
                              components[3]);
    if (left == 0 && top == 0 && right == (int32_t)context->width &&
        bottom == (int32_t)context->height) {
        return gxmetal_metal_ensure_encoder(renderer, context, 1, color) ?
            GXMETAL_ERROR_NONE : GXMETAL_ERROR_RENDERER;
    }
    if (!gxmetal_metal_ensure_encoder(renderer, context, 0,
                                      MTLClearColorMake(0, 0, 0, 1))) {
        return GXMETAL_ERROR_RENDERER;
    }
    memset(vertices, 0, sizeof(vertices));
    vertices[0].x = vertices[2].x = (float)left;
    vertices[1].x = vertices[3].x = (float)right;
    vertices[0].y = vertices[1].y = (float)top;
    vertices[2].y = vertices[3].y = (float)bottom;
    for (i = 0; i < 4; i++) {
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
    [context->encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
        vertexStart:0 vertexCount:4];
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
    vertex->r = gxmetal_metal_load_float(source + GXMETAL_VERTEX_R_OFFSET);
    vertex->g = gxmetal_metal_load_float(source + GXMETAL_VERTEX_G_OFFSET);
    vertex->b = gxmetal_metal_load_float(source + GXMETAL_VERTEX_B_OFFSET);
    vertex->a = gxmetal_metal_load_float(source + GXMETAL_VERTEX_A_OFFSET);
    return isfinite(vertex->x) && isfinite(vertex->y) &&
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
                                      MTLClearColorMake(0, 0, 0, 1))) {
        if (draw_vertices != vertices) {
            free(draw_vertices);
        }
        free(vertices);
        return GXMETAL_ERROR_RENDERER;
    }
    viewport.width = (float)context->width;
    viewport.height = (float)context->height;
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

GXMetalMetalRenderer *gxmetal_metal_create(void *framebuffer,
                                            uint32_t framebuffer_bytes)
{
    GXMetalMetalRenderer *renderer;
    id<MTLLibrary> library;
    id<MTLFunction> vertex;
    id<MTLFunction> fragment;
    MTLRenderPipelineDescriptor *descriptor;
    NSError *error = nil;

    if (framebuffer == NULL || framebuffer_bytes == 0) {
        return NULL;
    }
    renderer = calloc(1, sizeof(*renderer));
    if (renderer == NULL) {
        return NULL;
    }
    renderer->framebuffer = framebuffer;
    renderer->framebuffer_bytes = framebuffer_bytes;
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
    descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    renderer->pipeline = [renderer->device
        newRenderPipelineStateWithDescriptor:descriptor error:&error];
    [descriptor release];
    [vertex release];
    [fragment release];
    [library release];
    if (renderer->pipeline == nil) {
        fprintf(stderr, "GXMetal: cannot create Metal pipeline: %s\n",
                error.localizedDescription.UTF8String);
        gxmetal_metal_destroy(renderer);
        return NULL;
    }
    return renderer;
}

void gxmetal_metal_destroy(GXMetalMetalRenderer *renderer)
{
    if (renderer == NULL) {
        return;
    }
    gxmetal_metal_reset(renderer);
    [renderer->pipeline release];
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
            return GXMETAL_ERROR_NONE;
        case GXMETAL_OP_CLEAR:
            return gxmetal_metal_clear(renderer, context, packet);
        case GXMETAL_OP_DRAW_GOURAUD:
            return gxmetal_metal_draw(renderer, context, packet);
        default:
            return GXMETAL_ERROR_BAD_OPCODE;
        }
    }
}
