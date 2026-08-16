/*
 * GXMetal guest/host protocol
 *
 * Copyright (c) 2026 ClassicMac contributors
 * SPDX-License-Identifier: MIT
 *
 * This header is shared by the PowerPC RAVE engine, QEMU and native tests.
 * The wire format is explicitly little-endian and does not depend on a C
 * compiler's structure packing. Use the load/store helpers below rather than
 * casting shared memory to native structures.
 */

#ifndef GXMETAL_PROTOCOL_H
#define GXMETAL_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GXMETAL_PROTOCOL_MAGIC            UINT32_C(0x47584d54) /* "GXMT" */
#define GXMETAL_PROTOCOL_VERSION_MAJOR    1u
#define GXMETAL_PROTOCOL_VERSION_MINOR    1u
#define GXMETAL_PROTOCOL_VERSION \
    ((GXMETAL_PROTOCOL_VERSION_MAJOR << 16) | GXMETAL_PROTOCOL_VERSION_MINOR)

/* GXMetal occupies the currently unused end of the std-VGA BAR2 register BAR. */
#define GXMETAL_BAR2_REGISTER_OFFSET      UINT32_C(0x0b00)
#define GXMETAL_REGISTER_BYTES            UINT32_C(0x0100)

/* A separate PCI BAR is shared by the guest and renderer. */
#define GXMETAL_SHARED_BYTES              UINT32_C(0x00400000)
#define GXMETAL_RING_OFFSET               UINT32_C(0x00001000)
#define GXMETAL_RING_BYTES                UINT32_C(0x00100000)
#define GXMETAL_UPLOAD_OFFSET \
    (GXMETAL_RING_OFFSET + GXMETAL_RING_BYTES)
#define GXMETAL_UPLOAD_BYTES \
    (GXMETAL_SHARED_BYTES - GXMETAL_UPLOAD_OFFSET)

#define GXMETAL_PACKET_ALIGNMENT          16u
#define GXMETAL_PACKET_HEADER_BYTES       16u
#define GXMETAL_MAX_PACKET_BYTES          UINT32_C(0x00040000)
#define GXMETAL_MAX_DIMENSION             UINT32_C(0x00004000)
#define GXMETAL_RESET_KEY                 GXMETAL_PROTOCOL_MAGIC

/* Register offsets relative to BAR2 + GXMETAL_BAR2_REGISTER_OFFSET. */
enum GXMetalRegister {
    GXMETAL_REG_MAGIC              = 0x00,
    GXMETAL_REG_VERSION            = 0x04,
    GXMETAL_REG_REGISTER_BYTES     = 0x08,
    GXMETAL_REG_FEATURES_LO        = 0x0c,
    GXMETAL_REG_FEATURES_HI        = 0x10,
    GXMETAL_REG_SHARED_BYTES       = 0x14,
    GXMETAL_REG_RING_OFFSET        = 0x18,
    GXMETAL_REG_RING_BYTES         = 0x1c,
    GXMETAL_REG_PRODUCER           = 0x20,
    GXMETAL_REG_CONSUMER           = 0x24,
    GXMETAL_REG_DOORBELL           = 0x28,
    GXMETAL_REG_STATUS             = 0x2c,
    GXMETAL_REG_ERROR              = 0x30,
    GXMETAL_REG_COMPLETED_SEQUENCE = 0x34,
    GXMETAL_REG_RESET              = 0x38,
    GXMETAL_REG_DIAGNOSTIC         = 0x3c
};

enum GXMetalFeature {
    GXMETAL_FEATURE_GOURAUD       = UINT64_C(1) << 0,
    GXMETAL_FEATURE_Z16           = UINT64_C(1) << 1,
    GXMETAL_FEATURE_TEXTURE       = UINT64_C(1) << 2,
    GXMETAL_FEATURE_BLEND         = UINT64_C(1) << 3,
    GXMETAL_FEATURE_BITMAP        = UINT64_C(1) << 4,
    GXMETAL_FEATURE_DOUBLE_BUFFER = UINT64_C(1) << 5,
    GXMETAL_FEATURE_MESH          = UINT64_C(1) << 6,
    GXMETAL_FEATURE_SCISSOR       = UINT64_C(1) << 7,
    GXMETAL_FEATURE_FENCE         = UINT64_C(1) << 8,
    GXMETAL_FEATURE_METAL         = UINT64_C(1) << 9
};

/* C11 enum constants are restricted to int even though the feature word is 64-bit. */
#define GXMETAL_FEATURE_TRACE             (UINT64_C(1) << 63)

enum GXMetalStatus {
    GXMETAL_STATUS_READY       = 1u << 0,
    GXMETAL_STATUS_PROCESSING  = 1u << 1,
    GXMETAL_STATUS_FAULTED     = 1u << 2,
    GXMETAL_STATUS_DEVICE_LOST = 1u << 3
};

enum GXMetalError {
    GXMETAL_ERROR_NONE             = 0,
    GXMETAL_ERROR_BAD_VERSION      = 1,
    GXMETAL_ERROR_BAD_RING         = 2,
    GXMETAL_ERROR_BAD_PACKET       = 3,
    GXMETAL_ERROR_BAD_OPCODE       = 4,
    GXMETAL_ERROR_BAD_CONTEXT      = 5,
    GXMETAL_ERROR_BAD_RESOURCE     = 6,
    GXMETAL_ERROR_RENDERER         = 7,
    GXMETAL_ERROR_DEVICE_LOST      = 8
};

/* Every command begins with this 16-byte logical header. */
enum GXMetalPacketHeaderOffset {
    GXMETAL_PACKET_OPCODE_OFFSET       = 0,
    GXMETAL_PACKET_HEADER_BYTES_OFFSET = 2,
    GXMETAL_PACKET_BYTES_OFFSET        = 4,
    GXMETAL_PACKET_CONTEXT_OFFSET      = 8,
    GXMETAL_PACKET_SEQUENCE_OFFSET     = 12
};

enum GXMetalOpcode {
    GXMETAL_OP_PAD              = 0x0000,
    GXMETAL_OP_RESET            = 0x0001,

    GXMETAL_OP_CONTEXT_CREATE   = 0x0100,
    GXMETAL_OP_CONTEXT_DESTROY  = 0x0101,
    GXMETAL_OP_BEGIN_FRAME      = 0x0102,
    GXMETAL_OP_END_FRAME        = 0x0103,
    GXMETAL_OP_PRESENT          = 0x0104,
    GXMETAL_OP_FENCE            = 0x0105,

    GXMETAL_OP_SET_STATE        = 0x0200,
    GXMETAL_OP_CLEAR            = 0x0201,

    GXMETAL_OP_DRAW_GOURAUD     = 0x0300,
    GXMETAL_OP_DRAW_TEXTURED    = 0x0301,
    GXMETAL_OP_DRAW_BITMAP      = 0x0302,

    GXMETAL_OP_TEXTURE_CREATE   = 0x0400,
    GXMETAL_OP_TEXTURE_UPLOAD   = 0x0401,
    GXMETAL_OP_TEXTURE_DESTROY  = 0x0402,
    GXMETAL_OP_BITMAP_CREATE    = 0x0410,
    GXMETAL_OP_BITMAP_UPLOAD    = 0x0411,
    GXMETAL_OP_BITMAP_DESTROY   = 0x0412
};

enum GXMetalPixelFormat {
    GXMETAL_PIXEL_RGB555  = 1,
    GXMETAL_PIXEL_ARGB8888 = 2,
    GXMETAL_PIXEL_RGB8888 = 3,
    GXMETAL_PIXEL_DEPTH16 = 4,
    GXMETAL_PIXEL_ALPHA8  = 5,
    GXMETAL_PIXEL_INDEX8  = 6,
    GXMETAL_PIXEL_ARGB1555 = 7,
    GXMETAL_PIXEL_ARGB4444 = 8
};

enum GXMetalPrimitive {
    GXMETAL_PRIMITIVE_POINT         = 0,
    GXMETAL_PRIMITIVE_LINE          = 1,
    GXMETAL_PRIMITIVE_TRIANGLE      = 2,
    GXMETAL_PRIMITIVE_TRIANGLE_STRIP = 3,
    GXMETAL_PRIMITIVE_TRIANGLE_FAN  = 4
};

enum GXMetalContextFlag {
    GXMETAL_CONTEXT_Z16           = 1u << 0,
    GXMETAL_CONTEXT_DOUBLE_BUFFER = 1u << 1,
    GXMETAL_CONTEXT_NO_DITHER     = 1u << 2
};

enum GXMetalClearFlag {
    GXMETAL_CLEAR_COLOR = 1u << 0,
    GXMETAL_CLEAR_DEPTH = 1u << 1
};

enum GXMetalStateValueType {
    GXMETAL_STATE_UINT32   = 0,
    GXMETAL_STATE_FLOAT32  = 1,
    GXMETAL_STATE_RESOURCE = 2
};

/* RAVE state tags and values used by the host without depending on Mac headers. */
enum GXMetalStateTag {
    GXMETAL_STATE_Z_FUNCTION   = 0,
    GXMETAL_STATE_BLEND        = 9,
    GXMETAL_STATE_PERSPECTIVE_Z = 10,
    GXMETAL_STATE_TEXTURE_FILTER = 11,
    GXMETAL_STATE_TEXTURE_OP   = 12,
    GXMETAL_STATE_TEXTURE      = 13,
    GXMETAL_STATE_Z_BUFFER_MASK = 28,
    GXMETAL_STATE_DONT_SWAP    = 32,
    GXMETAL_STATE_TEXTURE_WRAP_U = 101,
    GXMETAL_STATE_TEXTURE_WRAP_V = 102
};

enum GXMetalZFunction {
    GXMETAL_Z_NONE  = 0,
    GXMETAL_Z_LT    = 1,
    GXMETAL_Z_EQ    = 2,
    GXMETAL_Z_LE    = 3,
    GXMETAL_Z_GT    = 4,
    GXMETAL_Z_NE    = 5,
    GXMETAL_Z_GE    = 6,
    GXMETAL_Z_TRUE  = 7,
    GXMETAL_Z_FALSE = 8
};

enum GXMetalBlendMode {
    GXMETAL_BLEND_PREMULTIPLY = 0,
    GXMETAL_BLEND_INTERPOLATE = 1,
    GXMETAL_BLEND_OPENGL      = 2
};

enum GXMetalTextureFilter {
    GXMETAL_TEXTURE_FILTER_FAST = 0,
    GXMETAL_TEXTURE_FILTER_MID  = 1,
    GXMETAL_TEXTURE_FILTER_BEST = 2
};

enum GXMetalTextureWrap {
    GXMETAL_TEXTURE_WRAP_REPEAT = 0,
    GXMETAL_TEXTURE_WRAP_CLAMP  = 1
};

enum GXMetalTextureOperation {
    GXMETAL_TEXTURE_MODULATE = 1u << 0,
    GXMETAL_TEXTURE_HIGHLIGHT = 1u << 1,
    GXMETAL_TEXTURE_DECAL = 1u << 2,
    GXMETAL_TEXTURE_SHRINK = 1u << 3,
    GXMETAL_TEXTURE_BLEND = 1u << 4
};

enum GXMetalResourceFlag {
    GXMETAL_RESOURCE_FLIP_ORIGIN = 1u << 0
};

/*
 * Fixed payload offsets. All signed coordinates use two's-complement int32;
 * floating-point values are IEEE-754 binary32 transported as little-endian
 * uint32 bit patterns.
 */
#define GXMETAL_CONTEXT_CREATE_PACKET_BYTES       48u
#define GXMETAL_CONTEXT_WIDTH_OFFSET               0u
#define GXMETAL_CONTEXT_HEIGHT_OFFSET              4u
#define GXMETAL_CONTEXT_ROW_BYTES_OFFSET           8u
#define GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET       12u
#define GXMETAL_CONTEXT_FRAMEBUFFER_OFFSET        16u
#define GXMETAL_CONTEXT_FLAGS_OFFSET              20u
#define GXMETAL_CONTEXT_RESERVED0_OFFSET          24u
#define GXMETAL_CONTEXT_RESERVED1_OFFSET          28u

#define GXMETAL_RECT_PAYLOAD_BYTES                16u
#define GXMETAL_RECT_LEFT_OFFSET                   0u
#define GXMETAL_RECT_TOP_OFFSET                    4u
#define GXMETAL_RECT_RIGHT_OFFSET                  8u
#define GXMETAL_RECT_BOTTOM_OFFSET                12u

#define GXMETAL_SET_STATE_PACKET_BYTES            32u
#define GXMETAL_STATE_TAG_OFFSET                   0u
#define GXMETAL_STATE_TYPE_OFFSET                  4u
#define GXMETAL_STATE_VALUE_OFFSET                 8u
#define GXMETAL_STATE_RESERVED_OFFSET             12u

#define GXMETAL_CLEAR_PACKET_BYTES                64u
#define GXMETAL_CLEAR_FLAGS_OFFSET                 0u
#define GXMETAL_CLEAR_COLOR_R_OFFSET               4u
#define GXMETAL_CLEAR_COLOR_G_OFFSET               8u
#define GXMETAL_CLEAR_COLOR_B_OFFSET              12u
#define GXMETAL_CLEAR_COLOR_A_OFFSET              16u
#define GXMETAL_CLEAR_DEPTH_OFFSET                20u
#define GXMETAL_CLEAR_RECT_OFFSET                 24u
#define GXMETAL_CLEAR_RESERVED_OFFSET             40u

/* Draw payload header followed by vertex_count fixed-stride vertices. */
#define GXMETAL_DRAW_HEADER_BYTES                 16u
#define GXMETAL_DRAW_PRIMITIVE_OFFSET              0u
#define GXMETAL_DRAW_VERTEX_COUNT_OFFSET           4u
#define GXMETAL_DRAW_VERTEX_STRIDE_OFFSET          8u
#define GXMETAL_DRAW_FLAGS_OFFSET                 12u
#define GXMETAL_DRAW_VERTICES_OFFSET              16u
#define GXMETAL_GOURAUD_VERTEX_BYTES              32u
#define GXMETAL_TEXTURE_VERTEX_BYTES              64u

/* Both vertex forms start with x, y, z, invW, r, g, b, a. */
#define GXMETAL_VERTEX_X_OFFSET                    0u
#define GXMETAL_VERTEX_Y_OFFSET                    4u
#define GXMETAL_VERTEX_Z_OFFSET                    8u
#define GXMETAL_VERTEX_INV_W_OFFSET               12u
#define GXMETAL_VERTEX_R_OFFSET                   16u
#define GXMETAL_VERTEX_G_OFFSET                   20u
#define GXMETAL_VERTEX_B_OFFSET                   24u
#define GXMETAL_VERTEX_A_OFFSET                   28u
#define GXMETAL_VERTEX_U_OVER_W_OFFSET            32u
#define GXMETAL_VERTEX_V_OVER_W_OFFSET            36u
#define GXMETAL_VERTEX_KD_R_OFFSET                40u
#define GXMETAL_VERTEX_KD_G_OFFSET                44u
#define GXMETAL_VERTEX_KD_B_OFFSET                48u
#define GXMETAL_VERTEX_KS_R_OFFSET                52u
#define GXMETAL_VERTEX_KS_G_OFFSET                56u
#define GXMETAL_VERTEX_KS_B_OFFSET                60u

#define GXMETAL_RESOURCE_CREATE_PACKET_BYTES      48u
#define GXMETAL_RESOURCE_ID_OFFSET                 0u
#define GXMETAL_RESOURCE_WIDTH_OFFSET              4u
#define GXMETAL_RESOURCE_HEIGHT_OFFSET             8u
#define GXMETAL_RESOURCE_ROW_BYTES_OFFSET         12u
#define GXMETAL_RESOURCE_PIXEL_FORMAT_OFFSET      16u
#define GXMETAL_RESOURCE_FLAGS_OFFSET             20u
#define GXMETAL_RESOURCE_LEVELS_OFFSET            24u
#define GXMETAL_RESOURCE_RESERVED_OFFSET          28u

#define GXMETAL_RESOURCE_UPLOAD_PACKET_BYTES      48u
#define GXMETAL_UPLOAD_RESOURCE_ID_OFFSET          0u
#define GXMETAL_UPLOAD_LEVEL_OFFSET                4u
#define GXMETAL_UPLOAD_SHARED_OFFSET_OFFSET        8u
#define GXMETAL_UPLOAD_LENGTH_OFFSET              12u
#define GXMETAL_UPLOAD_ROW_BYTES_OFFSET           16u
#define GXMETAL_UPLOAD_WIDTH_OFFSET               20u
#define GXMETAL_UPLOAD_HEIGHT_OFFSET              24u
#define GXMETAL_UPLOAD_RESERVED_OFFSET            28u

#define GXMETAL_RESOURCE_DESTROY_PACKET_BYTES     32u
#define GXMETAL_DESTROY_RESOURCE_ID_OFFSET         0u

/* Little-endian helpers safe for unaligned command-ring addresses. */
static inline uint16_t gxmetal_load_le16(const void *address)
{
    const uint8_t *p = (const uint8_t *)address;
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t gxmetal_load_le32(const void *address)
{
    const uint8_t *p = (const uint8_t *)address;
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline void gxmetal_store_le16(void *address, uint16_t value)
{
    uint8_t *p = (uint8_t *)address;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static inline void gxmetal_store_le32(void *address, uint32_t value)
{
    uint8_t *p = (uint8_t *)address;
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

#ifdef __cplusplus
}
#endif

#endif /* GXMETAL_PROTOCOL_H */
