#ifndef GXMETAL_ENGINE_SELECTION_PROBE_LOGIC_H
#define GXMETAL_ENGINE_SELECTION_PROBE_LOGIC_H

#include <stddef.h>
#include <stdint.h>

typedef struct GXMetalProbeTextBuffer {
    char *data;
    size_t capacity;
    size_t length;
    int truncated;
} GXMetalProbeTextBuffer;

static inline void gxmetal_probe_text_init(GXMetalProbeTextBuffer *buffer,
                                           char *data, size_t capacity)
{
    buffer->data = data;
    buffer->capacity = capacity;
    buffer->length = 0;
    buffer->truncated = 0;
    if (capacity != 0) {
        data[0] = '\0';
    }
}

static inline void gxmetal_probe_text_char(GXMetalProbeTextBuffer *buffer,
                                           char value)
{
    if (buffer->capacity != 0 && buffer->length + 1 < buffer->capacity) {
        buffer->data[buffer->length++] = value;
        buffer->data[buffer->length] = '\0';
    } else {
        buffer->truncated = 1;
    }
}

static inline void gxmetal_probe_text(GXMetalProbeTextBuffer *buffer,
                                      const char *value)
{
    while (*value != '\0') {
        gxmetal_probe_text_char(buffer, *value++);
    }
}

static inline void gxmetal_probe_u32(GXMetalProbeTextBuffer *buffer,
                                     uint32_t value)
{
    char digits[10];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0) {
        gxmetal_probe_text_char(buffer, digits[--count]);
    }
}

static inline void gxmetal_probe_i32(GXMetalProbeTextBuffer *buffer,
                                     int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        gxmetal_probe_text_char(buffer, '-');
        magnitude = (uint32_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t)value;
    }
    gxmetal_probe_u32(buffer, magnitude);
}

static inline void gxmetal_probe_hex32(GXMetalProbeTextBuffer *buffer,
                                       uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    gxmetal_probe_text(buffer, "0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        gxmetal_probe_text_char(buffer, digits[(value >> shift) & 0xfu]);
    }
}

static inline void gxmetal_probe_json_string(GXMetalProbeTextBuffer *buffer,
                                             const char *value)
{
    static const char digits[] = "0123456789abcdef";
    const unsigned char *cursor = (const unsigned char *)value;

    gxmetal_probe_text_char(buffer, '"');
    while (*cursor != '\0') {
        unsigned char character = *cursor++;

        switch (character) {
        case '"':
            gxmetal_probe_text(buffer, "\\\"");
            break;
        case '\\':
            gxmetal_probe_text(buffer, "\\\\");
            break;
        case '\b':
            gxmetal_probe_text(buffer, "\\b");
            break;
        case '\f':
            gxmetal_probe_text(buffer, "\\f");
            break;
        case '\n':
            gxmetal_probe_text(buffer, "\\n");
            break;
        case '\r':
            gxmetal_probe_text(buffer, "\\r");
            break;
        case '\t':
            gxmetal_probe_text(buffer, "\\t");
            break;
        default:
            if (character < 0x20u || character >= 0x80u) {
                gxmetal_probe_text(buffer, "\\u00");
                gxmetal_probe_text_char(buffer, digits[character >> 4]);
                gxmetal_probe_text_char(buffer, digits[character & 0x0fu]);
            } else {
                gxmetal_probe_text_char(buffer, (char)character);
            }
            break;
        }
    }
    gxmetal_probe_text_char(buffer, '"');
}

#endif
