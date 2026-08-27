/* SPDX-License-Identifier: MIT */
#ifndef GXMETAL_ATI_COMPATIBILITY_H
#define GXMETAL_ATI_COMPATIBILITY_H

#include <stdint.h>

/* GXMetal advertises ATI's vendor ID because classic QuickDraw 3D clients
 * and Apple's OpenGLRendererATI deliberately select that compatibility path.
 * Vendor-specific engine IDs below generation four are rejected by later
 * Pangea launchers; generation four also needs revision 30.  Use the next
 * generation while retaining GXMetal's own product revision in the public
 * revision gestalt. */
#define GXMETAL_ATI_ENGINE_ID UINT32_C(5)
#define GXMETAL_ATI_PRIVATE_ENABLE_TAG UINT32_C(1020)
#define GXMETAL_ATI_PRIVATE_METHODS_TAG UINT32_C(1021)

/* ATI 3D Accelerator's private method-table slot 2 replaces the pixels of an
 * already allocated texture. Myth II uses it to fill a pool of blank RAVE
 * textures when a map becomes active. Keep the inferred contract narrow:
 * shipping calls use flags zero, a single base image, and the same format and
 * dimensions that created the resource. The engine performs pointer, magic,
 * format, and transport validation around this arithmetic-only predicate. */
static inline int gxmetal_ati_private_texture_update_is_valid(
    uint32_t flags, uint32_t pixel_type, uint32_t texture_pixel_type,
    int32_t image_width, int32_t image_height, int32_t image_row_bytes,
    uint32_t texture_width, uint32_t texture_height,
    uint32_t texture_levels, uint32_t bytes_per_pixel,
    uint32_t upload_capacity)
{
    uint64_t minimum_row_bytes;
    uint64_t upload_bytes;

    if (flags != 0 || pixel_type != texture_pixel_type ||
        image_width <= 0 || image_height <= 0 || image_row_bytes <= 0 ||
        (uint32_t)image_width != texture_width ||
        (uint32_t)image_height != texture_height || texture_levels != 1 ||
        bytes_per_pixel == 0) {
        return 0;
    }
    minimum_row_bytes = (uint64_t)texture_width * bytes_per_pixel;
    upload_bytes = minimum_row_bytes * texture_height;
    return (uint64_t)(uint32_t)image_row_bytes >= minimum_row_bytes &&
        upload_bytes != 0 && upload_bytes <= upload_capacity;
}

/* ATI 3D Accelerator exposes its chip-family version through private integer
 * tag 1011.  A value of 0x0500 selects the Rage 128 feature path used by
 * Bugdom-era clients.  GXMetal implements the associated public RAVE state
 * and does not expose unsupported hardware-only entry points here. */
#define GXMETAL_ATI_CHIP_VERSION_TAG UINT32_C(1011)
#define GXMETAL_ATI_RAGE128_CHIP_VERSION UINT32_C(0x0500)
#define GXMETAL_ATI_PRIVATE_METHOD_COUNT UINT32_C(64)
#define GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES UINT32_C(128)
#define GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT UINT32_C(4096)
#define GXMETAL_ATI_PRIVATE_CLEAR_COLOR_FIRST_WORD UINT32_C(18)
#define GXMETAL_ATI_PRIVATE_CLEAR_COLOR_WORDS UINT32_C(4)
#define GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD UINT32_C(26)
#define GXMETAL_ATI_PRIVATE_STATE_WORDS UINT32_C(54)

/* OpenGLRendererATI passes this mask as slot 20's second argument. These
 * groups have been mapped against the fallback public-RAVE setter path in
 * Apple's ATI renderer. */
#define GXMETAL_ATI_DIRTY_ALPHA_TEST  UINT32_C(0x000001)
#define GXMETAL_ATI_DIRTY_BLEND       UINT32_C(0x000002)
#define GXMETAL_ATI_DIRTY_CLEAR       UINT32_C(0x000004)
#define GXMETAL_ATI_DIRTY_DEPTH_TEST  UINT32_C(0x000010)
#define GXMETAL_ATI_DIRTY_DEPTH_WRITE UINT32_C(0x000020)
#define GXMETAL_ATI_DIRTY_FOG         UINT32_C(0x000040)
#define GXMETAL_ATI_DIRTY_COLOR_MASK  UINT32_C(0x000400)
#define GXMETAL_ATI_DIRTY_TEXTURE     UINT32_C(0x020000)
#define GXMETAL_ATI_DIRTY_IMPLEMENTED (                                \
    GXMETAL_ATI_DIRTY_ALPHA_TEST | GXMETAL_ATI_DIRTY_BLEND |           \
    GXMETAL_ATI_DIRTY_CLEAR | GXMETAL_ATI_DIRTY_DEPTH_TEST |           \
    GXMETAL_ATI_DIRTY_DEPTH_WRITE | GXMETAL_ATI_DIRTY_FOG |            \
    GXMETAL_ATI_DIRTY_COLOR_MASK | GXMETAL_ATI_DIRTY_TEXTURE)

#define GXMETAL_ATI_PRIVATE_ALPHA_FUNCTION_WORD UINT32_C(1)
#define GXMETAL_ATI_PRIVATE_ALPHA_REFERENCE_WORD UINT32_C(2)
#define GXMETAL_ATI_PRIVATE_ALPHA_ENABLE_WORD UINT32_C(3)
#define GXMETAL_ATI_PRIVATE_BLEND_SOURCE_WORD UINT32_C(4)
#define GXMETAL_ATI_PRIVATE_BLEND_DESTINATION_WORD UINT32_C(5)
#define GXMETAL_ATI_PRIVATE_BLEND_ENABLE_WORD UINT32_C(11)
#define GXMETAL_ATI_PRIVATE_CLEAR_DEPTH_WORD UINT32_C(12)
#define GXMETAL_ATI_PRIVATE_DEPTH_ENABLE_WORD UINT32_C(27)
#define GXMETAL_ATI_PRIVATE_DEPTH_WRITE_WORD UINT32_C(28)
#define GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD UINT32_C(29)
#define GXMETAL_ATI_PRIVATE_FOG_DENSITY_WORD UINT32_C(33)
#define GXMETAL_ATI_PRIVATE_FOG_START_WORD UINT32_C(34)
#define GXMETAL_ATI_PRIVATE_FOG_END_WORD UINT32_C(35)
#define GXMETAL_ATI_PRIVATE_FOG_MODE_WORD UINT32_C(37)
#define GXMETAL_ATI_PRIVATE_FOG_ENABLE_WORD UINT32_C(38)
#define GXMETAL_ATI_PRIVATE_CHANNEL_MASK_WORD UINT32_C(52)

#define GXMETAL_ATI_GL_ZERO UINT32_C(0)
#define GXMETAL_ATI_GL_ONE UINT32_C(1)
#define GXMETAL_ATI_GL_EXP UINT32_C(0x0800)
#define GXMETAL_ATI_GL_EXP2 UINT32_C(0x0801)
#define GXMETAL_ATI_GL_LINEAR UINT32_C(0x2601)
#define GXMETAL_ATI_GL_REPEAT UINT32_C(0x2901)
#define GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS UINT32_C(8)

/* OpenGL primitive-mode tokens are the compact values 0..9 in the classic
 * API and are passed unchanged to the ATI GLD's generic geometry callbacks. */
#define GXMETAL_ATI_GL_POINTS         UINT32_C(0)
#define GXMETAL_ATI_GL_LINES          UINT32_C(1)
#define GXMETAL_ATI_GL_LINE_LOOP      UINT32_C(2)
#define GXMETAL_ATI_GL_LINE_STRIP     UINT32_C(3)
#define GXMETAL_ATI_GL_TRIANGLES      UINT32_C(4)
#define GXMETAL_ATI_GL_TRIANGLE_STRIP UINT32_C(5)
#define GXMETAL_ATI_GL_TRIANGLE_FAN   UINT32_C(6)
#define GXMETAL_ATI_GL_QUADS          UINT32_C(7)
#define GXMETAL_ATI_GL_QUAD_STRIP     UINT32_C(8)
#define GXMETAL_ATI_GL_POLYGON        UINT32_C(9)
#define GXMETAL_ATI_POLYGON_MODE_POINT UINT32_C(0)
#define GXMETAL_ATI_POLYGON_MODE_LINE UINT32_C(1)
#define GXMETAL_ATI_POLYGON_MODE_FILL UINT32_C(2)

static inline int gxmetal_ati_private_polygon_mode_is_fill(
    uint32_t polygon_mode)
{
    return polygon_mode == GXMETAL_ATI_POLYGON_MODE_FILL;
}

/* OpenGL compare-function tokens stored in OpenGLRendererATI's private
 * state synchronization block. */
#define GXMETAL_ATI_GL_NEVER    UINT32_C(0x0200)
#define GXMETAL_ATI_GL_LESS     UINT32_C(0x0201)
#define GXMETAL_ATI_GL_EQUAL    UINT32_C(0x0202)
#define GXMETAL_ATI_GL_LEQUAL   UINT32_C(0x0203)
#define GXMETAL_ATI_GL_GREATER  UINT32_C(0x0204)
#define GXMETAL_ATI_GL_NOTEQUAL UINT32_C(0x0205)
#define GXMETAL_ATI_GL_GEQUAL   UINT32_C(0x0206)
#define GXMETAL_ATI_GL_ALWAYS   UINT32_C(0x0207)

typedef struct GXMetalBitmapRect {
    float left;
    float top;
    float right;
    float bottom;
} GXMetalBitmapRect;

static inline int gxmetal_ati_legacy_generation_is_current(
    uint32_t engine_id, uint32_t revision)
{
    return engine_id > 4u || (engine_id == 4u && revision >= 30u);
}

static inline int gxmetal_ati_private_int(uint32_t tag, uint32_t *value)
{
    if (tag != GXMETAL_ATI_CHIP_VERSION_TAG || value == 0) {
        return 0;
    }
    *value = GXMETAL_ATI_RAGE128_CHIP_VERSION;
    return 1;
}

/* Apple's ATI GLD obtains a vendor-private callback table through RAVE tag
 * 1021. Keep the bit calculation shared with the native suite so diagnostic
 * instrumentation cannot invoke undefined shifts for the upper 32 slots or
 * for a corrupt selector. */
static inline uint32_t gxmetal_ati_private_method_mask(uint32_t method,
                                                       uint32_t word)
{
    uint32_t first = word * UINT32_C(32);

    if (word >= UINT32_C(2) || method < first ||
        method >= first + UINT32_C(32) ||
        method >= GXMETAL_ATI_PRIVATE_METHOD_COUNT) {
        return 0;
    }
    return UINT32_C(1) << (method - first);
}

/* The ATI GLD's evidenced contiguous-list hooks use fixed 128-byte
 * transformed vertices. Bound the byte calculation in one testable helper
 * before the guest driver validates or walks application-owned memory. */
static inline int gxmetal_ati_private_contiguous_vertex_bytes(
    uint32_t vertex_count, uint32_t *byte_count)
{
    if (byte_count == 0 || vertex_count < UINT32_C(3) ||
        vertex_count > GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT) {
        return 0;
    }
    *byte_count = vertex_count * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
    return 1;
}

/* Slot 60's final argument reports whether the ATI GLD clipped the staged
 * pointer fan. It is descriptive, not an enable: Quake III emits real world
 * geometry with a zero marker, while clipped fans use nonzero values. Keep
 * the count check and marker semantics together so neither form can be
 * silently discarded by a game-specific compatibility heuristic. */
static inline int gxmetal_ati_private_pointer_fan_should_render(
    uint32_t vertex_count, uint32_t clip_marker)
{
    (void)clip_marker;
    return vertex_count >= UINT32_C(3) &&
        vertex_count <= GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT;
}

/* The generic ATI GLD callbacks are overloaded between a batch form
 * (arg2=count, arg6=OpenGL mode) and a reduced triangle form where arg2 is a
 * guest vertex pointer. Guest pointers are well above the bounded count
 * range, so this discriminator avoids dereferencing renderer bookkeeping as
 * geometry without guessing from a single game. */
static inline int gxmetal_ati_private_is_contiguous_batch(
    uint32_t arg2, uint32_t arg6)
{
    return arg2 >= UINT32_C(3) &&
        arg2 <= GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT &&
        arg6 <= GXMETAL_ATI_GL_POLYGON;
}

/* Slots 49/50 normally receive one center pointer, a pointer to the first rim
 * vertex, and totalVertexCount-1 (the rim count). A direct flush instead
 * passes center==rimBase and a total count; normalize that form by advancing
 * the effective rim one vertex. The center may remain separate after GLCore
 * clipping, so only the normalized rim is necessarily contiguous. */
static inline int gxmetal_ati_private_fan_layout(
    uint32_t center_address, uint32_t rim_address, uint32_t count_field,
    uint32_t *effective_rim_address, uint32_t *effective_rim_vertex_count)
{
    if (effective_rim_address == 0 || effective_rim_vertex_count == 0) {
        return 0;
    }
    if (center_address == rim_address) {
        if (count_field < UINT32_C(3) ||
            count_field > GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT ||
            rim_address > UINT32_MAX -
                GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES) {
            return 0;
        }
        *effective_rim_address = rim_address +
            GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
        *effective_rim_vertex_count = count_field - UINT32_C(1);
        return 1;
    }
    if (count_field < UINT32_C(2) ||
        count_field >
            GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT - UINT32_C(1)) {
        return 0;
    }
    *effective_rim_address = rim_address;
    *effective_rim_vertex_count = count_field;
    return 1;
}

/* Some triangle siblings use the slot-48 shape: a three-vertex contiguous
 * base plus an end pointer to vertex two. Requiring both the exact count and
 * exact 2*stride relation makes this form unambiguous with three independent
 * vertex pointers. */
static inline int gxmetal_ati_private_is_contiguous_triangle(
    uint32_t arg1, uint32_t arg2, uint32_t arg5)
{
    return arg2 == UINT32_C(3) &&
        arg1 <= UINT32_MAX -
            UINT32_C(2) * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES &&
        arg5 == arg1 +
            UINT32_C(2) * GXMETAL_ATI_PRIVATE_VERTEX_STRIDE_BYTES;
}

/* OpenGLRendererATI's generic callbacks pass GL_TRIANGLE_STRIP as one of
 * several filled modes. OpenGL defines alternating source winding for
 * successive strip triangles; swap the first two vertices of odd triangles
 * so every emitted independent triangle retains the caller's facing. */
static inline int gxmetal_ati_private_strip_triangle_indices(
    uint32_t vertex_count, uint32_t triangle_index, uint32_t indices[3])
{
    if (indices == 0 || vertex_count < UINT32_C(3) ||
        vertex_count > GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT ||
        triangle_index >= vertex_count - UINT32_C(2)) {
        return 0;
    }
    if ((triangle_index & UINT32_C(1)) != 0) {
        indices[0] = triangle_index + UINT32_C(1);
        indices[1] = triangle_index;
    } else {
        indices[0] = triangle_index;
        indices[1] = triangle_index + UINT32_C(1);
    }
    indices[2] = triangle_index + UINT32_C(2);
    return 1;
}

/* Hooks 49..52 and the fallback filled callbacks share this topology helper.
 * Return the independent-triangle count for the filled OpenGL modes GXMetal
 * can emit;
 * points and lines remain non-destructive no-ops until their raster contract
 * is implemented. Dangling vertices are ignored as OpenGL requires. */
static inline uint32_t gxmetal_ati_private_primitive_triangle_count(
    uint32_t primitive, uint32_t vertex_count)
{
    if (vertex_count > GXMETAL_ATI_PRIVATE_MAX_VERTEX_COUNT) {
        return 0;
    }
    switch (primitive) {
    case GXMETAL_ATI_GL_TRIANGLES:
        return vertex_count / UINT32_C(3);
    case GXMETAL_ATI_GL_TRIANGLE_STRIP:
    case GXMETAL_ATI_GL_TRIANGLE_FAN:
    case GXMETAL_ATI_GL_POLYGON:
        return vertex_count >= UINT32_C(3) ?
            vertex_count - UINT32_C(2) : 0;
    case GXMETAL_ATI_GL_QUADS:
        return (vertex_count / UINT32_C(4)) * UINT32_C(2);
    case GXMETAL_ATI_GL_QUAD_STRIP:
        return vertex_count >= UINT32_C(4) ?
            (vertex_count / UINT32_C(2) - UINT32_C(1)) * UINT32_C(2) : 0;
    default:
        return 0;
    }
}

static inline int gxmetal_ati_private_primitive_triangle_indices(
    uint32_t primitive, uint32_t vertex_count, uint32_t triangle_index,
    uint32_t indices[3])
{
    uint32_t base;
    uint32_t triangle_count =
        gxmetal_ati_private_primitive_triangle_count(
            primitive, vertex_count);

    if (indices == 0 || triangle_index >= triangle_count) {
        return 0;
    }
    switch (primitive) {
    case GXMETAL_ATI_GL_TRIANGLES:
        base = triangle_index * UINT32_C(3);
        indices[0] = base;
        indices[1] = base + UINT32_C(1);
        indices[2] = base + UINT32_C(2);
        return 1;
    case GXMETAL_ATI_GL_TRIANGLE_STRIP:
        return gxmetal_ati_private_strip_triangle_indices(
            vertex_count, triangle_index, indices);
    case GXMETAL_ATI_GL_TRIANGLE_FAN:
    case GXMETAL_ATI_GL_POLYGON:
        indices[0] = 0;
        indices[1] = triangle_index + UINT32_C(1);
        indices[2] = triangle_index + UINT32_C(2);
        return 1;
    case GXMETAL_ATI_GL_QUADS:
        base = (triangle_index / UINT32_C(2)) * UINT32_C(4);
        indices[0] = base + ((triangle_index & UINT32_C(1)) != 0 ?
            UINT32_C(1) : 0);
        indices[1] = base + ((triangle_index & UINT32_C(1)) != 0 ?
            UINT32_C(2) : UINT32_C(1));
        indices[2] = base + UINT32_C(3);
        return 1;
    case GXMETAL_ATI_GL_QUAD_STRIP:
        /* Apple's GLCore reference path lowers the interleaved pair order as
         * an alternating strip. Keep its exact diagonal and provoking-
         * vertex order for interpolation and nonplanar input. */
        return gxmetal_ati_private_strip_triangle_indices(
            (vertex_count / UINT32_C(2)) * UINT32_C(2),
            triangle_index, indices);
    default:
        return 0;
    }
}

/* OpenGLRendererATI's slot-20 state synchronization block stores clear
 * R/G/B/A as consecutive IEEE-754 words 18..21. Keep the offset and bounds
 * check shared with the native compatibility suite. */
static inline int gxmetal_ati_private_clear_color_bits(
    const uint32_t *state_words, uint32_t word_count, uint32_t color_bits[4])
{
    uint32_t color_index;

    if (state_words == 0 || color_bits == 0 ||
        word_count < GXMETAL_ATI_PRIVATE_CLEAR_COLOR_FIRST_WORD +
                     GXMETAL_ATI_PRIVATE_CLEAR_COLOR_WORDS) {
        return 0;
    }
    for (color_index = 0;
         color_index < GXMETAL_ATI_PRIVATE_CLEAR_COLOR_WORDS;
         ++color_index) {
        color_bits[color_index] = state_words[
            GXMETAL_ATI_PRIVATE_CLEAR_COLOR_FIRST_WORD + color_index];
    }
    return 1;
}

/* Word 26 carries the effective OpenGL depth comparison.  In particular,
 * OpenGLRendererATI writes GL_ALWAYS while depth testing is disabled.  If
 * the RAVE engine leaves its context defaulted to Less, coplanar 2D layers
 * are rejected after the first layer writes Z (the black Oni menu failure).
 * Return the numerically equivalent kQAZFunction value without requiring
 * classic Mac headers in the native compatibility test. */
static inline int gxmetal_ati_private_depth_function(
    const uint32_t *state_words, uint32_t word_count,
    uint32_t *rave_function)
{
    uint32_t value;

    if (state_words == 0 || rave_function == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD) {
        return 0;
    }
    value = state_words[GXMETAL_ATI_PRIVATE_DEPTH_FUNCTION_WORD];
    switch (value) {
    case GXMETAL_ATI_GL_NEVER:    *rave_function = 8; break;
    case GXMETAL_ATI_GL_LESS:     *rave_function = 1; break;
    case GXMETAL_ATI_GL_EQUAL:    *rave_function = 2; break;
    case GXMETAL_ATI_GL_LEQUAL:   *rave_function = 3; break;
    case GXMETAL_ATI_GL_GREATER:  *rave_function = 4; break;
    case GXMETAL_ATI_GL_NOTEQUAL: *rave_function = 5; break;
    case GXMETAL_ATI_GL_GEQUAL:   *rave_function = 6; break;
    case GXMETAL_ATI_GL_ALWAYS:   *rave_function = 7; break;
    default:
        return 0;
    }
    return 1;
}

/* Boolean fields in the derived ATI state block are bytes at the first
 * address of a big-endian word. Looking at the numeric word's most-
 * significant byte keeps this helper correct on the PowerPC guest and easy
 * to exercise in native tests. */
static inline uint32_t gxmetal_ati_private_msb_boolean(uint32_t word)
{
    return (word >> UINT32_C(24)) != 0 ? UINT32_C(1) : UINT32_C(0);
}

static inline int gxmetal_ati_private_alpha_state(
    const uint32_t *state_words, uint32_t word_count,
    uint32_t *rave_function, uint32_t *reference_bits)
{
    uint32_t function;

    if (state_words == 0 || rave_function == 0 || reference_bits == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_ALPHA_ENABLE_WORD) {
        return 0;
    }
    *reference_bits =
        state_words[GXMETAL_ATI_PRIVATE_ALPHA_REFERENCE_WORD];
    if (!gxmetal_ati_private_msb_boolean(
            state_words[GXMETAL_ATI_PRIVATE_ALPHA_ENABLE_WORD])) {
        *rave_function = 0;
        return 1;
    }
    function = state_words[GXMETAL_ATI_PRIVATE_ALPHA_FUNCTION_WORD];
    switch (function) {
    case GXMETAL_ATI_GL_NEVER:    *rave_function = 8; break;
    case GXMETAL_ATI_GL_LESS:     *rave_function = 1; break;
    case GXMETAL_ATI_GL_EQUAL:    *rave_function = 2; break;
    case GXMETAL_ATI_GL_LEQUAL:   *rave_function = 3; break;
    case GXMETAL_ATI_GL_GREATER:  *rave_function = 4; break;
    case GXMETAL_ATI_GL_NOTEQUAL: *rave_function = 5; break;
    case GXMETAL_ATI_GL_GEQUAL:   *rave_function = 6; break;
    case GXMETAL_ATI_GL_ALWAYS:   *rave_function = 7; break;
    default:
        return 0;
    }
    return 1;
}

static inline int gxmetal_ati_private_blend_state(
    const uint32_t *state_words, uint32_t word_count,
    uint32_t *rave_blend, uint32_t *source_factor,
    uint32_t *destination_factor)
{
    if (state_words == 0 || rave_blend == 0 || source_factor == 0 ||
        destination_factor == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_BLEND_ENABLE_WORD) {
        return 0;
    }
    /* Public RAVE has no disabled blend enumerant. Keep GXMetal on its GL
     * pipeline and express disabled blending as ONE,ZERO, which is the exact
     * OpenGL result without changing public RAVE blend semantics. */
    *rave_blend = UINT32_C(2);
    if (gxmetal_ati_private_msb_boolean(
            state_words[GXMETAL_ATI_PRIVATE_BLEND_ENABLE_WORD])) {
        *source_factor =
            state_words[GXMETAL_ATI_PRIVATE_BLEND_SOURCE_WORD];
        *destination_factor =
            state_words[GXMETAL_ATI_PRIVATE_BLEND_DESTINATION_WORD];
    } else {
        *source_factor = GXMETAL_ATI_GL_ONE;
        *destination_factor = GXMETAL_ATI_GL_ZERO;
    }
    return 1;
}

static inline int gxmetal_ati_private_depth_state(
    const uint32_t *state_words, uint32_t word_count,
    uint32_t *rave_function, uint32_t *write_enabled)
{
    if (state_words == 0 || rave_function == 0 || write_enabled == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_DEPTH_WRITE_WORD) {
        return 0;
    }
    *write_enabled = gxmetal_ati_private_msb_boolean(
        state_words[GXMETAL_ATI_PRIVATE_DEPTH_WRITE_WORD]);
    if (!gxmetal_ati_private_msb_boolean(
            state_words[GXMETAL_ATI_PRIVATE_DEPTH_ENABLE_WORD])) {
        *rave_function = 0;
        return 1;
    }
    return gxmetal_ati_private_depth_function(
        state_words, word_count, rave_function);
}

/* fog_bits returns R,G,B,A,density,start,end in that order. */
static inline int gxmetal_ati_private_fog_state(
    const uint32_t *state_words, uint32_t word_count,
    uint32_t *rave_mode, uint32_t fog_bits[7])
{
    uint32_t gl_mode;
    uint32_t index;

    if (state_words == 0 || rave_mode == 0 || fog_bits == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_FOG_ENABLE_WORD) {
        return 0;
    }
    for (index = 0; index < UINT32_C(4); ++index) {
        fog_bits[index] = state_words[
            GXMETAL_ATI_PRIVATE_FOG_COLOR_FIRST_WORD + index];
    }
    fog_bits[4] = state_words[GXMETAL_ATI_PRIVATE_FOG_DENSITY_WORD];
    fog_bits[5] = state_words[GXMETAL_ATI_PRIVATE_FOG_START_WORD];
    fog_bits[6] = state_words[GXMETAL_ATI_PRIVATE_FOG_END_WORD];
    if (!gxmetal_ati_private_msb_boolean(
            state_words[GXMETAL_ATI_PRIVATE_FOG_ENABLE_WORD])) {
        *rave_mode = 0;
        return 1;
    }
    gl_mode = state_words[GXMETAL_ATI_PRIVATE_FOG_MODE_WORD];
    switch (gl_mode) {
    case GXMETAL_ATI_GL_LINEAR: *rave_mode = 2; break;
    case GXMETAL_ATI_GL_EXP:    *rave_mode = 3; break;
    case GXMETAL_ATI_GL_EXP2:   *rave_mode = 4; break;
    default:
        return 0;
    }
    return 1;
}

static inline int gxmetal_ati_private_channel_mask(
    const uint32_t *state_words, uint32_t word_count, uint32_t *mask)
{
    uint32_t bytes;

    if (state_words == 0 || mask == 0 ||
        word_count <= GXMETAL_ATI_PRIVATE_CHANNEL_MASK_WORD) {
        return 0;
    }
    bytes = state_words[GXMETAL_ATI_PRIVATE_CHANNEL_MASK_WORD];
    *mask = ((bytes >> UINT32_C(24)) != 0 ? UINT32_C(1) : 0) |
        ((bytes >> UINT32_C(16)) & UINT32_C(0xff) ? UINT32_C(2) : 0) |
        ((bytes >> UINT32_C(8)) & UINT32_C(0xff) ? UINT32_C(4) : 0) |
        ((bytes & UINT32_C(0xff)) != 0 ? UINT32_C(8) : 0);
    return 1;
}

/* Each pointer in slot20 arg3 addresses wrapU, wrapV, min-filter,
 * mag-filter, then border R/G/B/A. ATI treats every wrap token other than
 * GL_REPEAT as clamp on this classic path. border_bits preserves R,G,B,A. */
static inline int gxmetal_ati_private_texture_parameters(
    const uint32_t *parameter_words, uint32_t word_count,
    uint32_t *wrap_u, uint32_t *wrap_v, uint32_t *min_filter,
    uint32_t *mag_filter, uint32_t border_bits[4])
{
    uint32_t index;

    if (parameter_words == 0 || wrap_u == 0 || wrap_v == 0 ||
        min_filter == 0 || mag_filter == 0 || border_bits == 0 ||
        word_count < GXMETAL_ATI_PRIVATE_TEXTURE_PARAMETER_WORDS) {
        return 0;
    }
    *wrap_u = parameter_words[0] == GXMETAL_ATI_GL_REPEAT ? 0 : 1;
    *wrap_v = parameter_words[1] == GXMETAL_ATI_GL_REPEAT ? 0 : 1;
    *min_filter = parameter_words[2];
    *mag_filter = parameter_words[3];
    for (index = 0; index < UINT32_C(4); ++index) {
        border_bits[index] = parameter_words[4 + index];
    }
    return 1;
}

static inline int gxmetal_ati_uses_logical_bitmap_canvas(
    float x, float y, float width, float height)
{
    return x >= 0.0f && y >= 0.0f && width > 0.0f && height > 0.0f &&
        x + width <= 320.0f && y + height <= 200.0f;
}

/* ATI's classic RAVE compatibility mode accepts small QADrawBitmap overlays
 * inside the game's 320x200 HUD coordinate system.  Full-context bitmaps are
 * already in physical pixels and are excluded by the predicate above.  Scale
 * the logical canvas uniformly to the draw-context width and center it
 * vertically.  Carmageddon II's 640x480 mode therefore becomes a
 * pixel-doubled 640x400 HUD with 40-pixel top and bottom margins, matching the
 * software renderer. */
static inline GXMetalBitmapRect gxmetal_ati_bitmap_rect(
    uint32_t context_width, uint32_t context_height,
    float x, float y, float width, float height)
{
    GXMetalBitmapRect result;
    float scale = context_width != 0 ?
        (float)context_width / 320.0f : 1.0f;
    float top_margin = ((float)context_height - 200.0f * scale) * 0.5f;

    result.left = x * scale;
    result.top = y * scale + top_margin;
    result.right = (x + width) * scale;
    result.bottom = (y + height) * scale + top_margin;
    return result;
}

#endif
