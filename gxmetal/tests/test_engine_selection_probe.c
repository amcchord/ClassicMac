#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "GXMetalEngineSelectionProbeLogic.h"

static void test_machine_readable_text_helpers(void)
{
    char result[160];
    GXMetalProbeTextBuffer buffer;

    gxmetal_probe_text_init(&buffer, result, sizeof(result));
    gxmetal_probe_text(&buffer, "{\"signed\":");
    gxmetal_probe_i32(&buffer, INT32_MIN);
    gxmetal_probe_text(&buffer, ",\"unsigned\":");
    gxmetal_probe_u32(&buffer, UINT32_MAX);
    gxmetal_probe_text(&buffer, ",\"hex\":");
    gxmetal_probe_json_string(&buffer, "0x47584d54");
    gxmetal_probe_text(&buffer, ",\"name\":");
    gxmetal_probe_json_string(&buffer, "GX\\\"Metal\n\001\200");
    gxmetal_probe_text_char(&buffer, '}');

    assert(!buffer.truncated);
    assert(strcmp(result,
        "{\"signed\":-2147483648,\"unsigned\":4294967295,"
        "\"hex\":\"0x47584d54\",\"name\":\"GX\\\\\\\"Metal\\n\\u0001\\u0080\"}") == 0);
}

static void test_truncation_is_nul_terminated(void)
{
    char result[5];
    GXMetalProbeTextBuffer buffer;

    gxmetal_probe_text_init(&buffer, result, sizeof(result));
    gxmetal_probe_text(&buffer, "abcdef");
    assert(buffer.truncated);
    assert(buffer.length == 4);
    assert(strcmp(result, "abcd") == 0);
}

static void test_hex_is_fixed_width(void)
{
    char result[16];
    GXMetalProbeTextBuffer buffer;

    gxmetal_probe_text_init(&buffer, result, sizeof(result));
    gxmetal_probe_hex32(&buffer, UINT32_C(0x12ab));
    assert(!buffer.truncated);
    assert(strcmp(result, "0x000012ab") == 0);
}

int main(void)
{
    test_machine_readable_text_helpers();
    test_truncation_is_nul_terminated();
    test_hex_is_fixed_width();
    puts("GXMetal engine-selection probe logic tests passed");
    return 0;
}
