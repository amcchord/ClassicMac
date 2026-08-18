#include <assert.h>
#include <stdio.h>

#include "ModeContract.h"

#define FIRST_DEPTH_MODE 128UL
#define FRAMEBUFFER_BYTES (64UL * 1024UL * 1024UL)

static void test_depth_ladders(void)
{
    assert(QemuVgaDepthToDepthMode(1, 1, FIRST_DEPTH_MODE) == 128);
    assert(QemuVgaDepthToDepthMode(1, 8, FIRST_DEPTH_MODE) == 131);
    assert(QemuVgaDepthToDepthMode(1, 15, FIRST_DEPTH_MODE) == 132);
    assert(QemuVgaDepthToDepthMode(1, 32, FIRST_DEPTH_MODE) == 133);
    assert(QemuVgaDepthModeToDepth(1, 128, FIRST_DEPTH_MODE) == 1);
    assert(QemuVgaDepthModeToDepth(1, 131, FIRST_DEPTH_MODE) == 8);
    assert(QemuVgaDepthModeToDepth(1, 132, FIRST_DEPTH_MODE) == 15);
    assert(QemuVgaDepthModeToDepth(1, 133, FIRST_DEPTH_MODE) == 32);

    assert(QemuVgaDepthToDepthMode(0, 8, FIRST_DEPTH_MODE) == 128);
    assert(QemuVgaDepthToDepthMode(0, 15, FIRST_DEPTH_MODE) == 129);
    assert(QemuVgaDepthToDepthMode(0, 32, FIRST_DEPTH_MODE) == 130);
    assert(QemuVgaDepthModeToDepth(0, 128, FIRST_DEPTH_MODE) == 8);
    assert(QemuVgaDepthModeToDepth(0, 129, FIRST_DEPTH_MODE) == 15);
    assert(QemuVgaDepthModeToDepth(0, 130, FIRST_DEPTH_MODE) == 32);

    assert(QemuVgaDepthToDepthMode(1, 12, FIRST_DEPTH_MODE) == 0);
    assert(QemuVgaDepthModeToDepth(1, 0, FIRST_DEPTH_MODE) == 0);
    assert(QemuVgaDepthModeToDepth(1, 134, FIRST_DEPTH_MODE) == 0);
    assert(QemuVgaDepthModeToDepth(0, 131, FIRST_DEPTH_MODE) == 0);
}

static void test_runtime_resolution_switch_layout(void)
{
    unsigned long rowBytes;
    unsigned long pageSize;
    unsigned long pageCount;

    /* ClassicMac's normal launcher boot: 1024x768, Thousands. */
    assert(QemuVgaCalculatePageLayout(1024, 768, 15, FRAMEBUFFER_BYTES,
                                      &rowBytes, &pageSize, &pageCount));
    assert(rowBytes == 2048);
    assert(pageSize == 1572864);
    assert(pageCount == 2);

    /* Carmageddon II's hardware request must be valid independent of that
       source mode: 640x480, Thousands (RGB555 in 16-bit storage). */
    assert(QemuVgaCalculatePageLayout(640, 480, 15, FRAMEBUFFER_BYTES,
                                      &rowBytes, &pageSize, &pageCount));
    assert(rowBytes == 1280);
    assert(pageSize == 614400);
    assert(pageCount == 2);
}

static void test_layout_rejects_invalid_requests(void)
{
    unsigned long pageCount = 99;

    assert(!QemuVgaCalculatePageLayout(640, 480, 12, FRAMEBUFFER_BYTES,
                                       NULL, NULL, &pageCount));
    assert(!QemuVgaCalculatePageLayout(0, 480, 15, FRAMEBUFFER_BYTES,
                                       NULL, NULL, &pageCount));
    assert(!QemuVgaCalculatePageLayout(4096, 4096, 32, 1024 * 1024,
                                       NULL, NULL, &pageCount));
}

int main(void)
{
    test_depth_ladders();
    test_runtime_resolution_switch_layout();
    test_layout_rejects_invalid_requests();
    puts("ppcvid mode contract: PASS");
    return 0;
}
