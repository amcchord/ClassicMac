/* SPDX-License-Identifier: MIT */

#include "gxmetal_renderer.h"

#include <math.h>
#include <string.h>

typedef struct GXMetalFloatVertex {
    float x;
    float y;
    float z;
    float inv_w;
    float r;
    float g;
    float b;
    float a;
} GXMetalFloatVertex;

static float gxmetal_load_float(const uint8_t *bytes)
{
    uint32_t bits = gxmetal_load_le32(bytes);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static GXMetalRenderContext *gxmetal_find_context(GXMetalRenderer *renderer,
                                                   uint32_t id)
{
    uint32_t i;

    for (i = 0; i < GXMETAL_RENDERER_MAX_CONTEXTS; i++) {
        if (renderer->contexts[i].active && renderer->contexts[i].id == id) {
            return &renderer->contexts[i];
        }
    }
    return NULL;
}

static uint32_t gxmetal_bytes_per_pixel(uint32_t format)
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

static uint8_t gxmetal_unorm8(float value)
{
    if (!isfinite(value)) {
        return 0;
    }
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return (uint8_t)(value * 255.0f + 0.5f);
}

static void gxmetal_write_pixel(GXMetalRenderer *renderer,
                                const GXMetalRenderContext *context,
                                int x, int y, float r, float g, float b,
                                float a)
{
    uint32_t bytes_per_pixel = gxmetal_bytes_per_pixel(context->pixel_format);
    uint32_t offset;
    uint8_t *destination;

    if (x < 0 || y < 0 || (uint32_t)x >= context->width ||
        (uint32_t)y >= context->height) {
        return;
    }
    offset = context->framebuffer_offset + (uint32_t)y * context->row_bytes +
             (uint32_t)x * bytes_per_pixel;
    if ((uint64_t)offset + bytes_per_pixel > renderer->framebuffer_bytes) {
        return;
    }
    destination = renderer->framebuffer + offset;
    if (context->pixel_format == GXMETAL_PIXEL_RGB555) {
        uint16_t pixel = (uint16_t)(((uint16_t)(gxmetal_unorm8(r) >> 3) << 10) |
                                    ((uint16_t)(gxmetal_unorm8(g) >> 3) << 5) |
                                    (uint16_t)(gxmetal_unorm8(b) >> 3));
        destination[0] = (uint8_t)(pixel >> 8);
        destination[1] = (uint8_t)pixel;
    } else {
        destination[0] = context->pixel_format == GXMETAL_PIXEL_ARGB8888 ?
            gxmetal_unorm8(a) : 0;
        destination[1] = gxmetal_unorm8(r);
        destination[2] = gxmetal_unorm8(g);
        destination[3] = gxmetal_unorm8(b);
    }
}

static void gxmetal_read_vertex(const uint8_t *bytes,
                                GXMetalFloatVertex *vertex)
{
    vertex->x = gxmetal_load_float(bytes + GXMETAL_VERTEX_X_OFFSET);
    vertex->y = gxmetal_load_float(bytes + GXMETAL_VERTEX_Y_OFFSET);
    vertex->z = gxmetal_load_float(bytes + GXMETAL_VERTEX_Z_OFFSET);
    vertex->inv_w = gxmetal_load_float(bytes + GXMETAL_VERTEX_INV_W_OFFSET);
    vertex->r = gxmetal_load_float(bytes + GXMETAL_VERTEX_R_OFFSET);
    vertex->g = gxmetal_load_float(bytes + GXMETAL_VERTEX_G_OFFSET);
    vertex->b = gxmetal_load_float(bytes + GXMETAL_VERTEX_B_OFFSET);
    vertex->a = gxmetal_load_float(bytes + GXMETAL_VERTEX_A_OFFSET);
}

static int gxmetal_vertex_valid(const GXMetalRenderContext *context,
                                const GXMetalFloatVertex *vertex)
{
    return isfinite(vertex->x) && isfinite(vertex->y) &&
           isfinite(vertex->z) && isfinite(vertex->inv_w) &&
           isfinite(vertex->r) && isfinite(vertex->g) &&
           isfinite(vertex->b) && isfinite(vertex->a) &&
           vertex->x >= 0.0f && vertex->x <= (float)context->width &&
           vertex->y >= 0.0f && vertex->y <= (float)context->height;
}

static float gxmetal_edge(const GXMetalFloatVertex *a,
                          const GXMetalFloatVertex *b, float x, float y)
{
    return (x - a->x) * (b->y - a->y) -
           (y - a->y) * (b->x - a->x);
}

static void gxmetal_draw_triangle(GXMetalRenderer *renderer,
                                  const GXMetalRenderContext *context,
                                  const GXMetalFloatVertex *v0,
                                  const GXMetalFloatVertex *v1,
                                  const GXMetalFloatVertex *v2)
{
    float area = gxmetal_edge(v0, v1, v2->x, v2->y);
    float min_x;
    float max_x;
    float min_y;
    float max_y;
    int x0;
    int x1;
    int y0;
    int y1;
    int x;
    int y;

    if (!isfinite(area) || area == 0.0f) {
        return;
    }
    min_x = fminf(v0->x, fminf(v1->x, v2->x));
    max_x = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    min_y = fminf(v0->y, fminf(v1->y, v2->y));
    max_y = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    min_x = fmaxf(min_x, 0.0f);
    min_y = fmaxf(min_y, 0.0f);
    max_x = fminf(max_x, (float)context->width);
    max_y = fminf(max_y, (float)context->height);
    x0 = (int)floorf(min_x);
    x1 = (int)ceilf(max_x);
    y0 = (int)floorf(min_y);
    y1 = (int)ceilf(max_y);

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            float sample_x = (float)x + 0.5f;
            float sample_y = (float)y + 0.5f;
            float w0 = gxmetal_edge(v1, v2, sample_x, sample_y) / area;
            float w1 = gxmetal_edge(v2, v0, sample_x, sample_y) / area;
            float w2 = gxmetal_edge(v0, v1, sample_x, sample_y) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                gxmetal_write_pixel(renderer, context, x, y,
                    w0 * v0->r + w1 * v1->r + w2 * v2->r,
                    w0 * v0->g + w1 * v1->g + w2 * v2->g,
                    w0 * v0->b + w1 * v1->b + w2 * v2->b,
                    w0 * v0->a + w1 * v1->a + w2 * v2->a);
            }
        }
    }
}

static void gxmetal_draw_line(GXMetalRenderer *renderer,
                              const GXMetalRenderContext *context,
                              const GXMetalFloatVertex *v0,
                              const GXMetalFloatVertex *v1)
{
    float delta_x = v1->x - v0->x;
    float delta_y = v1->y - v0->y;
    float length = fmaxf(fabsf(delta_x), fabsf(delta_y));
    int steps = (int)ceilf(length);
    int i;

    if (steps < 1) {
        steps = 1;
    }
    for (i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        gxmetal_write_pixel(renderer, context,
            (int)floorf(v0->x + delta_x * t),
            (int)floorf(v0->y + delta_y * t),
            v0->r + (v1->r - v0->r) * t,
            v0->g + (v1->g - v0->g) * t,
            v0->b + (v1->b - v0->b) * t,
            v0->a + (v1->a - v0->a) * t);
    }
}

static uint32_t gxmetal_render_context_create(GXMetalRenderer *renderer,
                                               const GXMetalPacketView *packet)
{
    GXMetalRenderContext *context = NULL;
    uint32_t bytes_per_pixel;
    uint32_t i;
    uint64_t last_byte;

    if (gxmetal_find_context(renderer, packet->context_id) != NULL) {
        return GXMETAL_ERROR_BAD_CONTEXT;
    }
    for (i = 0; i < GXMETAL_RENDERER_MAX_CONTEXTS; i++) {
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
    bytes_per_pixel = gxmetal_bytes_per_pixel(context->pixel_format);
    last_byte = (uint64_t)context->framebuffer_offset +
        (uint64_t)(context->height - 1) * context->row_bytes +
        (uint64_t)context->width * bytes_per_pixel;
    if (bytes_per_pixel == 0 ||
        context->row_bytes <
            (uint64_t)context->width * bytes_per_pixel ||
        last_byte > renderer->framebuffer_bytes) {
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_BAD_CONTEXT;
    }
    context->active = 1;
    return GXMETAL_ERROR_NONE;
}

static uint32_t gxmetal_render_clear(GXMetalRenderer *renderer,
                                     GXMetalRenderContext *context,
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
    float r = gxmetal_load_float(packet->payload +
                                 GXMETAL_CLEAR_COLOR_R_OFFSET);
    float g = gxmetal_load_float(packet->payload +
                                 GXMETAL_CLEAR_COLOR_G_OFFSET);
    float b = gxmetal_load_float(packet->payload +
                                 GXMETAL_CLEAR_COLOR_B_OFFSET);
    float a = gxmetal_load_float(packet->payload +
                                 GXMETAL_CLEAR_COLOR_A_OFFSET);
    int32_t x;
    int32_t y;

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
    for (y = top; y < bottom; y++) {
        for (x = left; x < right; x++) {
            gxmetal_write_pixel(renderer, context, x, y, r, g, b, a);
        }
    }
    return GXMETAL_ERROR_NONE;
}

static uint32_t gxmetal_render_gouraud(GXMetalRenderer *renderer,
                                       GXMetalRenderContext *context,
                                       const GXMetalPacketView *packet)
{
    uint32_t primitive = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_PRIMITIVE_OFFSET);
    uint32_t count = gxmetal_load_le32(
        packet->payload + GXMETAL_DRAW_VERTEX_COUNT_OFFSET);
    const uint8_t *vertices = packet->payload + GXMETAL_DRAW_VERTICES_OFFSET;
    uint32_t i;

    if (primitive == GXMETAL_PRIMITIVE_POINT) {
        for (i = 0; i < count; i++) {
            GXMetalFloatVertex vertex;
            gxmetal_read_vertex(vertices + i * GXMETAL_GOURAUD_VERTEX_BYTES,
                                &vertex);
            if (!gxmetal_vertex_valid(context, &vertex)) {
                return GXMETAL_ERROR_BAD_PACKET;
            }
            gxmetal_write_pixel(renderer, context, (int)floorf(vertex.x),
                                (int)floorf(vertex.y), vertex.r, vertex.g,
                                vertex.b, vertex.a);
        }
        return GXMETAL_ERROR_NONE;
    }
    if (primitive == GXMETAL_PRIMITIVE_LINE) {
        for (i = 0; i < count; i += 2) {
            GXMetalFloatVertex v0;
            GXMetalFloatVertex v1;
            gxmetal_read_vertex(vertices + i * GXMETAL_GOURAUD_VERTEX_BYTES,
                                &v0);
            gxmetal_read_vertex(vertices +
                                (i + 1) * GXMETAL_GOURAUD_VERTEX_BYTES, &v1);
            if (!gxmetal_vertex_valid(context, &v0) ||
                !gxmetal_vertex_valid(context, &v1)) {
                return GXMETAL_ERROR_BAD_PACKET;
            }
            gxmetal_draw_line(renderer, context, &v0, &v1);
        }
        return GXMETAL_ERROR_NONE;
    }

    for (i = 0; i + 2 < count; i +=
         primitive == GXMETAL_PRIMITIVE_TRIANGLE ? 3 : 1) {
        uint32_t i0 = primitive == GXMETAL_PRIMITIVE_TRIANGLE_FAN ? 0 : i;
        uint32_t i1 = primitive == GXMETAL_PRIMITIVE_TRIANGLE_FAN ? i + 1 :
                      i + 1;
        uint32_t i2 = primitive == GXMETAL_PRIMITIVE_TRIANGLE_FAN ? i + 2 :
                      i + 2;
        GXMetalFloatVertex v0;
        GXMetalFloatVertex v1;
        GXMetalFloatVertex v2;

        gxmetal_read_vertex(vertices + i0 * GXMETAL_GOURAUD_VERTEX_BYTES,
                            &v0);
        gxmetal_read_vertex(vertices + i1 * GXMETAL_GOURAUD_VERTEX_BYTES,
                            &v1);
        gxmetal_read_vertex(vertices + i2 * GXMETAL_GOURAUD_VERTEX_BYTES,
                            &v2);
        if (!gxmetal_vertex_valid(context, &v0) ||
            !gxmetal_vertex_valid(context, &v1) ||
            !gxmetal_vertex_valid(context, &v2)) {
            return GXMETAL_ERROR_BAD_PACKET;
        }
        gxmetal_draw_triangle(renderer, context, &v0, &v1, &v2);
    }
    return GXMETAL_ERROR_NONE;
}

void gxmetal_renderer_init(GXMetalRenderer *renderer, void *framebuffer,
                           uint32_t framebuffer_bytes)
{
    memset(renderer, 0, sizeof(*renderer));
    renderer->framebuffer = framebuffer;
    renderer->framebuffer_bytes = framebuffer_bytes;
}

void gxmetal_renderer_reset(GXMetalRenderer *renderer)
{
    memset(renderer->contexts, 0, sizeof(renderer->contexts));
}

uint32_t gxmetal_renderer_dispatch(void *opaque,
                                   const GXMetalPacketView *packet)
{
    GXMetalRenderer *renderer = opaque;
    GXMetalRenderContext *context;

    if (packet->opcode == GXMETAL_OP_CONTEXT_CREATE) {
        return gxmetal_render_context_create(renderer, packet);
    }
    context = gxmetal_find_context(renderer, packet->context_id);
    if (context == NULL) {
        return GXMETAL_ERROR_BAD_CONTEXT;
    }

    switch (packet->opcode) {
    case GXMETAL_OP_CONTEXT_DESTROY:
        memset(context, 0, sizeof(*context));
        return GXMETAL_ERROR_NONE;
    case GXMETAL_OP_BEGIN_FRAME:
    case GXMETAL_OP_END_FRAME:
    case GXMETAL_OP_PRESENT:
    case GXMETAL_OP_SET_STATE:
        return GXMETAL_ERROR_NONE;
    case GXMETAL_OP_CLEAR:
        return gxmetal_render_clear(renderer, context, packet);
    case GXMETAL_OP_DRAW_GOURAUD:
        return gxmetal_render_gouraud(renderer, context, packet);
    default:
        return GXMETAL_ERROR_BAD_OPCODE;
    }
}
