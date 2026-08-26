#include <stdio.h>
#include <string.h>

#include "gxmetal_dirty.h"

static unsigned failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void make_packet(uint8_t *packet, uint16_t opcode, uint32_t bytes,
                        uint32_t context)
{
    memset(packet, 0, bytes);
    gxmetal_store_le16(packet + GXMETAL_PACKET_OPCODE_OFFSET, opcode);
    gxmetal_store_le16(packet + GXMETAL_PACKET_HEADER_BYTES_OFFSET,
                       GXMETAL_PACKET_HEADER_BYTES);
    gxmetal_store_le32(packet + GXMETAL_PACKET_BYTES_OFFSET, bytes);
    gxmetal_store_le32(packet + GXMETAL_PACKET_CONTEXT_OFFSET, context);
}

static GXMetalPacketView decode(uint8_t *packet, uint32_t bytes)
{
    GXMetalPacketView view;

    memset(&view, 0, sizeof(view));
    CHECK(gxmetal_decode_packet(packet, bytes, &view) == GXMETAL_DECODE_OK);
    CHECK(gxmetal_validate_packet(&view, GXMETAL_SHARED_BYTES) ==
          GXMETAL_ERROR_NONE);
    return view;
}

static void make_context(uint8_t *packet, uint32_t id, uint32_t width,
                         uint32_t height, uint32_t row_bytes,
                         uint32_t format, uint32_t framebuffer_offset,
                         uint32_t flags, uint32_t left, uint32_t top,
                         uint32_t right, uint32_t bottom)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_CONTEXT_CREATE,
                GXMETAL_CONTEXT_CREATE_PACKET_BYTES, id);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_WIDTH_OFFSET, width);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_HEIGHT_OFFSET, height);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_ROW_BYTES_OFFSET, row_bytes);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_PIXEL_FORMAT_OFFSET, format);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FRAMEBUFFER_OFFSET,
                       framebuffer_offset);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_FLAGS_OFFSET, flags);
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_LEFT_TOP_OFFSET,
                       left | (top << 16));
    gxmetal_store_le32(payload + GXMETAL_CONTEXT_CLIP_RIGHT_BOTTOM_OFFSET,
                       right | (bottom << 16));
}

static GXMetalPacketView make_present(uint8_t *packet, uint32_t id,
                                      int32_t left, int32_t top,
                                      int32_t right, int32_t bottom)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_PRESENT, 32, id);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload + GXMETAL_RECT_LEFT_OFFSET, (uint32_t)left);
    gxmetal_store_le32(payload + GXMETAL_RECT_TOP_OFFSET, (uint32_t)top);
    gxmetal_store_le32(payload + GXMETAL_RECT_RIGHT_OFFSET, (uint32_t)right);
    gxmetal_store_le32(payload + GXMETAL_RECT_BOTTOM_OFFSET,
                       (uint32_t)bottom);
    return decode(packet, 32);
}

static GXMetalPacketView make_writeback(uint8_t *packet, uint32_t id,
                                        uint32_t left, uint32_t top,
                                        uint32_t right, uint32_t bottom)
{
    uint8_t *payload;

    make_packet(packet, GXMETAL_OP_DRAW_BUFFER_WRITEBACK,
                GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES, id);
    payload = packet + GXMETAL_PACKET_HEADER_BYTES;
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_SHARED_OFFSET_OFFSET,
        GXMETAL_UPLOAD_OFFSET);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_LENGTH_OFFSET, 640u * 480u * 2u);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_ROW_BYTES_OFFSET, 1280);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET + GXMETAL_RECT_LEFT_OFFSET,
        left);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET + GXMETAL_RECT_TOP_OFFSET,
        top);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET + GXMETAL_RECT_RIGHT_OFFSET,
        right);
    gxmetal_store_le32(payload +
        GXMETAL_DRAW_BUFFER_WRITEBACK_RECT_OFFSET + GXMETAL_RECT_BOTTOM_OFFSET,
        bottom);
    return decode(packet, GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES);
}

static void test_full_and_partial_ranges(void)
{
    GXMetalDirtyTracker tracker;
    GXMetalDirtyRange range;
    GXMetalPacketView view;
    uint8_t packet[GXMETAL_CONTEXT_CREATE_PACKET_BYTES];

    gxmetal_dirty_init(&tracker, 64u * 1024u * 1024u);
    view = make_present(packet, 1, 0, 0, 640, 480);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);

    make_context(packet, 1, 640, 480, 1280, GXMETAL_PIXEL_RGB555,
                 0, 0, 0, 0, 0, 0);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);

    view = make_present(packet, 1, 0, 0, 640, 480);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_RANGE);
    CHECK(range.offset == 0);
    CHECK(range.length == 640u * 480u * 2u);

    view = make_present(packet, 1, 10, 20, 30, 40);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_RANGE);
    CHECK(range.offset == 20u * 1280u + 10u * 2u);
    CHECK(range.length == 19u * 1280u + 20u * 2u);

    view = make_present(packet, 1, -100, -100, 12, 3);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_RANGE);
    CHECK(range.offset == 0);
    CHECK(range.length == 2u * 1280u + 12u * 2u);

    view = make_present(packet, 1, 700, 0, 800, 10);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_EMPTY);
    CHECK(range.length == 0);

    view = make_present(packet, 1, 30, 20, 10, 40);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);
}

static void test_clip_offset_padding_and_lifetime(void)
{
    GXMetalDirtyTracker tracker;
    GXMetalDirtyRange range;
    GXMetalPacketView view;
    uint8_t packet[GXMETAL_CONTEXT_CREATE_PACKET_BYTES];

    gxmetal_dirty_init(&tracker, 1024u * 1024u);
    make_context(packet, 9, 64, 64, 320, GXMETAL_PIXEL_ARGB8888,
                 4096, GXMETAL_CONTEXT_RECT_CLIP, 8, 10, 56, 50);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);

    view = make_present(packet, 9, 0, 0, 64, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_RANGE);
    CHECK(range.offset == 4096u + 10u * 320u + 8u * 4u);
    CHECK(range.length == 39u * 320u + 48u * 4u);

    view = make_present(packet, 9, 0, 0, 8, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_EMPTY);

    make_packet(packet, GXMETAL_OP_CONTEXT_DESTROY, 16, 9);
    view = decode(packet, 16);
    gxmetal_dirty_observe_success(&tracker, &view);
    view = make_present(packet, 9, 0, 0, 64, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);

    make_context(packet, 10, 64, 64, 256, GXMETAL_PIXEL_ARGB8888,
                 4096, 0, 0, 0, 0, 0);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);
    make_packet(packet, GXMETAL_OP_RESET, 16, 0);
    view = decode(packet, 16);
    gxmetal_dirty_observe_success(&tracker, &view);
    view = make_present(packet, 10, 0, 0, 64, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);
}

static void test_writeback_range(void)
{
    GXMetalDirtyTracker tracker;
    GXMetalDirtyRange range;
    GXMetalPacketView view;
    uint8_t packet[GXMETAL_DRAW_BUFFER_WRITEBACK_PACKET_BYTES];

    gxmetal_dirty_init(&tracker, 64u * 1024u * 1024u);
    make_context(packet, 12, 640, 480, 1280, GXMETAL_PIXEL_RGB555,
                 4096, 0, 0, 0, 0, 0);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);

    view = make_writeback(packet, 12, 10, 20, 30, 40);
    CHECK(gxmetal_dirty_writeback_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_RANGE);
    CHECK(range.offset == 4096u + 20u * 1280u + 10u * 2u);
    CHECK(range.length == 19u * 1280u + 20u * 2u);

    view.opcode = GXMETAL_OP_PRESENT;
    CHECK(gxmetal_dirty_writeback_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);
}

static void test_invalid_metadata_falls_back(void)
{
    GXMetalDirtyTracker tracker;
    GXMetalDirtyRange range;
    GXMetalPacketView view;
    uint8_t packet[GXMETAL_CONTEXT_CREATE_PACKET_BYTES];

    gxmetal_dirty_init(&tracker, 4096);
    make_context(packet, 3, 64, 64, 128, GXMETAL_PIXEL_RGB555,
                 0, 0, 0, 0, 0, 0);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);
    view = make_present(packet, 3, 0, 0, 64, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);

    gxmetal_dirty_reset(&tracker);
    make_context(packet, 4, 64, 64, 128, GXMETAL_PIXEL_RGB555,
                 0, GXMETAL_CONTEXT_RECT_CLIP, 40, 10, 20, 50);
    view = decode(packet, GXMETAL_CONTEXT_CREATE_PACKET_BYTES);
    gxmetal_dirty_observe_success(&tracker, &view);
    view = make_present(packet, 4, 0, 0, 64, 64);
    CHECK(gxmetal_dirty_present_range(&tracker, &view, &range) ==
          GXMETAL_DIRTY_FALLBACK);
}

int main(void)
{
    test_full_and_partial_ranges();
    test_clip_offset_padding_and_lifetime();
    test_writeback_range();
    test_invalid_metadata_falls_back();
    if (failures != 0) {
        fprintf(stderr, "GXMetal dirty tracking: %u failure(s)\n", failures);
        return 1;
    }
    puts("GXMetal dirty tracking: all tests passed");
    return 0;
}
