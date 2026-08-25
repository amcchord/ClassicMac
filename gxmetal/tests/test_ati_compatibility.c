/* SPDX-License-Identifier: MIT */

#include <math.h>
#include <stdio.h>

#include "GXMetalATICompatibility.h"

static unsigned failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", \
                __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

#define CHECK_CLOSE(actual, expected) do { \
    if (fabsf((actual) - (expected)) > 0.0001f) { \
        fprintf(stderr, "%s:%d: got %.4f, expected %.4f\n", \
                __FILE__, __LINE__, (double)(actual), (double)(expected)); \
        failures++; \
    } \
} while (0)

static void test_carmageddon_minimap(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        640, 480, 230.0f, 150.0f, 48.0f, 48.0f);

    if (!gxmetal_ati_uses_logical_bitmap_canvas(
            230.0f, 150.0f, 48.0f, 48.0f)) {
        fprintf(stderr, "%s:%d: minimap was not classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
    CHECK_CLOSE(rect.left, 460.0f);
    CHECK_CLOSE(rect.top, 340.0f);
    CHECK_CLOSE(rect.right, 556.0f);
    CHECK_CLOSE(rect.bottom, 436.0f);
}

static void test_physical_context_bitmap(void)
{
    if (gxmetal_ati_uses_logical_bitmap_canvas(
            0.0f, 0.0f, 640.0f, 480.0f)) {
        fprintf(stderr, "%s:%d: full-context bitmap was classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
    if (gxmetal_ati_uses_logical_bitmap_canvas(
            300.0f, 180.0f, 48.0f, 48.0f)) {
        fprintf(stderr, "%s:%d: out-of-canvas bitmap was classified as logical\n",
                __FILE__, __LINE__);
        failures++;
    }
}

static void test_native_logical_canvas(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        320, 200, 12.0f, 34.0f, 56.0f, 78.0f);

    CHECK_CLOSE(rect.left, 12.0f);
    CHECK_CLOSE(rect.top, 34.0f);
    CHECK_CLOSE(rect.right, 68.0f);
    CHECK_CLOSE(rect.bottom, 112.0f);
}

static void test_larger_four_by_three_context(void)
{
    GXMetalBitmapRect rect = gxmetal_ati_bitmap_rect(
        800, 600, 10.0f, 20.0f, 30.0f, 40.0f);

    CHECK_CLOSE(rect.left, 25.0f);
    CHECK_CLOSE(rect.top, 100.0f);
    CHECK_CLOSE(rect.right, 100.0f);
    CHECK_CLOSE(rect.bottom, 200.0f);
}

static void test_later_ati_game_identity(void)
{
    uint32_t value = UINT32_C(0xdeadbeef);

    CHECK(gxmetal_ati_legacy_generation_is_current(
        GXMETAL_ATI_ENGINE_ID, UINT32_C(0x00020101)));
    CHECK(!gxmetal_ati_legacy_generation_is_current(3, 999));
    CHECK(!gxmetal_ati_legacy_generation_is_current(4, 29));
    CHECK(gxmetal_ati_legacy_generation_is_current(4, 30));
    CHECK(gxmetal_ati_private_int(GXMETAL_ATI_CHIP_VERSION_TAG, &value));
    CHECK(value == GXMETAL_ATI_RAGE128_CHIP_VERSION);
    value = UINT32_C(0xdeadbeef);
    CHECK(!gxmetal_ati_private_int(1010, &value));
    CHECK(value == UINT32_C(0xdeadbeef));
}

int main(void)
{
    test_carmageddon_minimap();
    test_physical_context_bitmap();
    test_native_logical_canvas();
    test_larger_four_by_three_context();
    test_later_ati_game_identity();
    if (failures != 0) {
        fprintf(stderr, "GXMetal ATI compatibility: %u failures\n",
                failures);
        return 1;
    }
    puts("GXMetal ATI compatibility: all tests passed");
    return 0;
}
