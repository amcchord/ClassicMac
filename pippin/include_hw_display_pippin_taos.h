/*
 * Apple Taos video controller emulation for Pippin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DISPLAY_PIPPIN_TAOS_H
#define HW_DISPLAY_PIPPIN_TAOS_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "ui/console.h"

#define TYPE_PIPPIN_TAOS "pippin-taos"
OBJECT_DECLARE_SIMPLE_TYPE(PippinTaosState, PIPPIN_TAOS)

#define TAOS_VRAM_BASE   0xF0000000U
#define TAOS_VRAM_SIZE   0x00100000U /* 1 MiB */
#define TAOS_IOREG_BASE  0xF0800000U
#define TAOS_IOREG_SIZE  0x00000800U
#define TAOS_CLUT_BASE   0xF1000000U
#define TAOS_CLUT_SIZE   0x00000400U

#define TAOS_MON_ID_PAL  3
#define TAOS_MON_ID_NTSC 5
#define TAOS_MON_ID_VGA  6

struct PippinTaosState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion vram_mr;
    MemoryRegion ctrl_mr;
    MemoryRegion clut_mr;
    qemu_irq irq;
    QemuConsole *con;
    QEMUTimer *vbl_timer;

    uint8_t *vram;
    uint32_t palette[256]; /* stored as 0xRRGGBB00 */
    uint32_t pal32[256];   /* predecoded 0xFFRRGGBB for 8-bit */
    uint32_t rgb555_r[32]; /* predecoded channel LUTs for 16-bit */
    uint32_t rgb555_g[32];
    uint32_t rgb555_b[32];

    char *video_out;
    uint8_t mon_id;
    uint8_t gpio_cfg;
    uint8_t gpio_out;
    uint8_t vsync_active;

    uint32_t fb_base;
    uint32_t row_words;
    uint32_t color_mode;
    uint32_t video_mode;
    uint32_t crt_ctrl;
    uint32_t crt_test;
    uint32_t int_levels;
    uint32_t int_enables;
    uint32_t swatch_regs[16];

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    int depth;
    bool crtc_on;
    bool crtc_was_on;
    bool full_update;
    int64_t vbl_period_ns;

    /* Embedded I2C targets behind Cuda */
    uint8_t athens_regs[8];
    uint8_t athens_reg_idx;
    uint8_t bt856_reg_idx;
};

bool pippin_taos_i2c_cmd(void *opaque, uint8_t dev_addr, bool has_subaddr,
                         uint8_t subaddr, const uint8_t *tx, int tx_len,
                         bool is_read, uint8_t *rx, int *rx_len);

#endif /* HW_DISPLAY_PIPPIN_TAOS_H */
