/*
 * Apple Taos video controller emulation for Pippin
 * (with Athens Q233 clock generator & Bt856 encoder I2C hooks)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/display/pippin_taos.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"

#define TAOS_CHIP_VERSION   0xA5000000U
#define TAOS_ENABLE_VOUT    (1U << 24)
#define TAOS_INT_VBL_BIT    (1U << 29)

#define ATHENS_I2C_ADDR     0x29
#define BT856_I2C_ADDR      0x44
#define ATHENS_XTAL_HZ      20000000.0

enum {
    TAOS_FB_BASE       = 0x00, /* 0x00 */
    TAOS_ROW_WORDS     = 0x01, /* 0x04 */
    TAOS_COLOR_MODE    = 0x02, /* 0x08 */
    TAOS_VIDEO_MODE    = 0x03, /* 0x0C */
    TAOS_VEFS          = 0x04, /* 0x10 */
    TAOS_VOFS          = 0x05, /* 0x14 */
    TAOS_HSTART        = 0x06, /* 0x18 */
    TAOS_VCONV_END     = 0x07, /* 0x1C */
    TAOS_CRT_CTRL      = 0x08, /* 0x20 */
    TAOS_CRT_TEST      = 0x09, /* 0x24 */
    TAOS_HEQ           = 0x0B, /* 0x2C */
    TAOS_HBWAY         = 0x0C, /* 0x30 */
    TAOS_HAL           = 0x0D, /* 0x34 */
    TAOS_HSERR         = 0x0E, /* 0x38 */
    TAOS_HFP           = 0x0F, /* 0x3C */
    TAOS_HPIX          = 0x10, /* 0x40 */
    TAOS_HSP           = 0x11, /* 0x44 */
    TAOS_HLFLN         = 0x12, /* 0x48 */
    TAOS_VBPEQ         = 0x13, /* 0x4C */
    TAOS_VBP           = 0x14, /* 0x50 */
    TAOS_VAL           = 0x15, /* 0x54 */
    TAOS_VFP           = 0x16, /* 0x58 */
    TAOS_VFPEQ         = 0x17, /* 0x5C */
    TAOS_VSYNC         = 0x18, /* 0x60 */
    TAOS_VHLINE        = 0x19, /* 0x64 */
    TAOS_GPIO_IN       = 0x1A, /* 0x68 */
    TAOS_GPIO_CONFIG   = 0x1B, /* 0x6C */
    TAOS_GPIO_OUT      = 0x1C, /* 0x70 */
    TAOS_GPIO_INT_CTRL = 0x1D, /* 0x74 */
    TAOS_INT_LEVELS    = 0x1E, /* 0x78 */
    TAOS_INT_ENABLES   = 0x1F, /* 0x7C */
    TAOS_INT_CLEAR     = 0x20, /* 0x80 */
    TAOS_VERSION       = 0x23, /* 0x8C */
    TAOS_INT_SET       = 0x24, /* 0x90 */
    TAOS_HEB           = 0x25, /* 0x94 */
};

static inline uint32_t taos_swatch(const PippinTaosState *s, int reg)
{
    return s->swatch_regs[reg - TAOS_HEQ] >> 20;
}

static void taos_update_irq(PippinTaosState *s)
{
    qemu_set_irq(s->irq, !!(s->int_levels & s->int_enables));
}

static double taos_athens_dot_clock(const PippinTaosState *s)
{
    uint8_t mux2 = s->athens_regs[3];
    uint8_t d2 = s->athens_regs[1];
    uint8_t n2 = s->athens_regs[2];
    int post_div;
    int mux;

    if (mux2 & 0x80) {
        return 0.0;
    }

    post_div = 1 << (3 - (mux2 & 3));
    mux = (mux2 >> 4) & 3;
    switch (mux) {
    case 0:
        if (d2 == 0) {
            return 25175000.0;
        }
        return ATHENS_XTAL_HZ * ((double)n2 / ((double)d2 * (double)post_div));
    case 2:
        return ATHENS_XTAL_HZ / (double)post_div;
    default:
        return 25175000.0;
    }
}

static void taos_recompute_mode(PippinTaosState *s)
{
    uint32_t new_w = 0, new_h = 0;
    uint32_t hfp = taos_swatch(s, TAOS_HFP);
    uint32_t hal = taos_swatch(s, TAOS_HAL);
    uint32_t vfp = taos_swatch(s, TAOS_VFP);
    uint32_t val = taos_swatch(s, TAOS_VAL);
    uint32_t hsp = taos_swatch(s, TAOS_HSP);
    uint32_t vsync = taos_swatch(s, TAOS_VSYNC);
    double dot = taos_athens_dot_clock(s);
    uint32_t htotal, vtotal;

    if (hfp > hal) {
        new_w = hfp - hal;
    }
    if (vfp > val) {
        new_h = vfp - val;
    }

    if (new_w < 64 || new_w > 1280 || new_h < 64 || new_h > 1024) {
        new_w = 640;
        new_h = 480;
    }

    s->width = new_w;
    s->height = new_h;
    s->depth = (s->color_mode & (1U << 31)) ? 16 : 8;
    s->pitch = s->row_words >> 20;
    if (s->pitch == 0) {
        s->pitch = (s->depth == 16) ? (s->width * 2) : s->width;
    }

    htotal = hal + (hsp > hfp ? (hsp - hfp) : 0) + new_w;
    vtotal = val + (vsync > vfp ? (vsync - vfp) : 0) + new_h;
    if (dot > 1000000.0 && htotal > 0 && vtotal > 0) {
        double hz = dot / ((double)htotal * (double)vtotal);
        if (hz < 25.0 || hz > 75.0) {
            hz = (s->mon_id == TAOS_MON_ID_PAL) ? 50.0 : 60.0;
        }
        s->vbl_period_ns = (int64_t)(NANOSECONDS_PER_SECOND / hz);
    } else {
        s->vbl_period_ns = NANOSECONDS_PER_SECOND / 60;
    }

    if (s->con) {
        DisplaySurface *surface = qemu_console_surface(s->con);
        if (!surface || surface_width(surface) != (int)s->width ||
            surface_height(surface) != (int)s->height ||
            surface_bits_per_pixel(surface) != 32) {
            qemu_console_resize(s->con, s->width, s->height);
        }
    }
    s->full_update = true;
}

static void taos_rebuild_luts(PippinTaosState *s)
{
    for (int i = 0; i < 256; i++) {
        uint32_t shifted = (s->palette[i] >> 8) & 0x00FFFFFFU;
        s->pal32[i] = 0xFF000000U | shifted;
        if (i < 32) {
            s->rgb555_r[i] = shifted & 0x00FF0000U;
            s->rgb555_g[i] = shifted & 0x0000FF00U;
            s->rgb555_b[i] = shifted & 0x000000FFU;
        }
    }
}

static bool taos_range_dirty(PippinTaosState *s, DirtyBitmapSnapshot *snap,
                             uint32_t offset, uint32_t len)
{
    uint32_t first = MIN(len, TAOS_VRAM_SIZE - offset);

    if (memory_region_snapshot_get_dirty(&s->vram_mr, snap, offset, first)) {
        return true;
    }
    if (first < len) {
        return memory_region_snapshot_get_dirty(&s->vram_mr, snap, 0,
                                                len - first);
    }
    return false;
}

static void taos_draw_display(void *opaque)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t *dst_base;
    int dst_stride;
    uint32_t fb_off;
    uint32_t row_bytes;
    DirtyBitmapSnapshot *snap;
    int y_start = -1;

    if (!surface || surface_bits_per_pixel(surface) != 32) {
        return;
    }

    dst_base = surface_data(surface);
    dst_stride = surface_stride(surface);

    if (!s->crtc_on) {
        if (s->full_update || s->crtc_was_on) {
            for (uint32_t y = 0; y < s->height; y++) {
                memset(dst_base + y * dst_stride, 0, s->width * 4);
            }
            s->crtc_was_on = false;
            s->full_update = false;
            dpy_gfx_update(s->con, 0, 0, s->width, s->height);
        }
        return;
    }

    if (!s->crtc_was_on) {
        s->crtc_was_on = true;
        s->full_update = true;
    }

    fb_off = (s->fb_base >> 20) & (TAOS_VRAM_SIZE - 1);
    row_bytes = (s->depth == 16) ? (s->width * 2) : s->width;
    snap = memory_region_snapshot_and_clear_dirty(&s->vram_mr, 0, TAOS_VRAM_SIZE,
                                                  DIRTY_MEMORY_VGA);

    for (uint32_t y = 0; y < s->height; y++) {
        uint32_t row_off = (fb_off + y * s->pitch) & (TAOS_VRAM_SIZE - 1);
        bool dirty = s->full_update || taos_range_dirty(s, snap, row_off, row_bytes);

        if (!dirty) {
            if (y_start >= 0) {
                dpy_gfx_update(s->con, 0, y_start, s->width, y - y_start);
                y_start = -1;
            }
            continue;
        }

        if (y_start < 0) {
            y_start = y;
        }

        uint32_t *dst = (uint32_t *)(dst_base + y * dst_stride);
        if (row_off + row_bytes <= TAOS_VRAM_SIZE) {
            const uint8_t *src = s->vram + row_off;
            if (s->depth == 16) {
                for (uint32_t x = 0; x < s->width; x++) {
                    uint16_t c = lduw_be_p(src + x * 2);
                    dst[x] = 0xFF000000U |
                             s->rgb555_r[(c >> 10) & 0x1F] |
                             s->rgb555_g[(c >> 5) & 0x1F] |
                             s->rgb555_b[c & 0x1F];
                }
            } else {
                for (uint32_t x = 0; x < s->width; x++) {
                    dst[x] = s->pal32[src[x]];
                }
            }
        } else {
            if (s->depth == 16) {
                for (uint32_t x = 0; x < s->width; x++) {
                    uint32_t addr = (row_off + x * 2) & (TAOS_VRAM_SIZE - 2);
                    uint16_t c = lduw_be_p(s->vram + addr);
                    dst[x] = 0xFF000000U |
                             s->rgb555_r[(c >> 10) & 0x1F] |
                             s->rgb555_g[(c >> 5) & 0x1F] |
                             s->rgb555_b[c & 0x1F];
                }
            } else {
                for (uint32_t x = 0; x < s->width; x++) {
                    uint32_t addr = (row_off + x) & (TAOS_VRAM_SIZE - 1);
                    dst[x] = s->pal32[s->vram[addr]];
                }
            }
        }
    }

    if (y_start >= 0) {
        dpy_gfx_update(s->con, 0, y_start, s->width, s->height - y_start);
    }

    g_free(snap);
    s->full_update = false;
}

static void taos_invalidate_display(void *opaque)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    s->full_update = true;
}

static const GraphicHwOps taos_gfx_ops = {
    .invalidate = taos_invalidate_display,
    .gfx_update = taos_draw_display,
};

static void taos_vbl_tick(void *opaque)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);

    s->vsync_active ^= 1;
    if (s->vsync_active) {
        s->int_levels |= TAOS_INT_VBL_BIT;
        taos_update_irq(s);
    }
    timer_mod(s->vbl_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                            (s->vbl_period_ns / 2));
}

bool pippin_taos_i2c_cmd(void *opaque, uint8_t dev_addr, bool has_subaddr,
                         uint8_t subaddr, const uint8_t *tx, int tx_len,
                         bool is_read, uint8_t *rx, int *rx_len)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);

    switch (dev_addr) {
    case ATHENS_I2C_ADDR:
        if (has_subaddr) {
            s->athens_reg_idx = subaddr;
        }
        if (is_read) {
            if (tx_len >= 1 && !has_subaddr) {
                s->athens_reg_idx = tx[0];
            }
            rx[0] = 0x41; /* Athens clockgen ID */
            *rx_len = 1;
            return true;
        }
        if (has_subaddr) {
            for (int i = 0; i < tx_len; i++) {
                uint8_t r = s->athens_reg_idx + i;
                if (r >= ARRAY_SIZE(s->athens_regs)) {
                    return false;
                }
                s->athens_regs[r] = tx[i];
            }
        } else if (tx_len >= 2) {
            if (tx[0] >= ARRAY_SIZE(s->athens_regs)) {
                return false;
            }
            s->athens_regs[tx[0]] = tx[1];
        } else if (tx_len == 1) {
            s->athens_reg_idx = tx[0];
        }
        return true;

    case BT856_I2C_ADDR:
        if (has_subaddr) {
            s->bt856_reg_idx = subaddr;
        }
        if (is_read) {
            rx[0] = 0x60; /* Bt856 encoder ID */
            *rx_len = 1;
            return true;
        }
        if (!has_subaddr && tx_len >= 1) {
            s->bt856_reg_idx = tx[0];
        }
        return true;

    case 0x45: /* Screamer / TDA7433 I2C audio processor */
        if (is_read) {
            rx[0] = 0x00;
            *rx_len = 1;
        }
        return true;

    default:
        return false;
    }
}

static uint64_t taos_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    uint32_t reg = (addr >> 2) & 0xFF;

    switch (reg) {
    case TAOS_FB_BASE:
        return s->fb_base;
    case TAOS_ROW_WORDS:
        return s->row_words;
    case TAOS_COLOR_MODE:
        return s->color_mode;
    case TAOS_VIDEO_MODE:
        return s->video_mode << 30;
    case TAOS_CRT_CTRL:
        return s->crt_ctrl;
    case TAOS_CRT_TEST:
        return s->crt_test;
    case TAOS_HEQ ... TAOS_VHLINE:
        return s->swatch_regs[reg - TAOS_HEQ];
    case TAOS_GPIO_IN:
        return (((uint32_t)s->mon_id << 29) | (1U << 27) |
                ((uint32_t)s->vsync_active << 25)) & ~((uint32_t)s->gpio_cfg << 24);
    case TAOS_GPIO_CONFIG:
        return (uint32_t)s->gpio_cfg << 24;
    case TAOS_GPIO_OUT:
        return (uint32_t)s->gpio_out << 24;
    case TAOS_INT_LEVELS:
        return s->int_levels;
    case TAOS_INT_ENABLES:
        return s->int_enables;
    case TAOS_VERSION:
        return TAOS_CHIP_VERSION;
    case TAOS_INT_CLEAR:
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "taos: unimplemented read reg 0x%" HWADDR_PRIx "\n", addr);
        return 0;
    }
}

static void taos_ctrl_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    uint32_t reg = (addr >> 2) & 0xFF;
    uint32_t val = (uint32_t)val64;

    switch (reg) {
    case TAOS_FB_BASE:
        if (s->fb_base != val) {
            s->fb_base = val;
            if (s->crtc_on) {
                taos_recompute_mode(s);
            }
            s->full_update = true;
        }
        break;
    case TAOS_ROW_WORDS:
        if (s->row_words != val) {
            s->row_words = val;
            if (s->crtc_on) {
                taos_recompute_mode(s);
            }
            s->full_update = true;
        }
        break;
    case TAOS_COLOR_MODE:
        if (s->color_mode != val || !s->crtc_on) {
            s->color_mode = val;
            s->crtc_on = true;
            taos_recompute_mode(s);
        }
        break;
    case TAOS_VIDEO_MODE:
        s->video_mode = val >> 30;
        break;
    case TAOS_VEFS:
    case TAOS_VOFS:
    case TAOS_HSTART:
    case TAOS_VCONV_END:
    case TAOS_GPIO_INT_CTRL:
    case TAOS_HEB:
        break;
    case TAOS_CRT_CTRL: {
        bool was_on = !!(s->crt_ctrl & TAOS_ENABLE_VOUT);
        bool now_on = !!(val & TAOS_ENABLE_VOUT);
        s->crt_ctrl = val;
        if (now_on && !was_on) {
            s->crtc_on = true;
            taos_recompute_mode(s);
        } else if (!now_on && was_on) {
            s->crtc_on = false;
            s->full_update = true;
        }
        break;
    }
    case TAOS_CRT_TEST:
        s->crt_test = val;
        break;
    case TAOS_HEQ ... TAOS_VHLINE:
        s->swatch_regs[reg - TAOS_HEQ] = val;
        break;
    case TAOS_GPIO_CONFIG:
        s->gpio_cfg = (val >> 24) & 0xFF;
        break;
    case TAOS_GPIO_OUT:
        s->gpio_out = (val >> 24) & 0xFF;
        break;
    case TAOS_INT_ENABLES:
        s->int_enables = val;
        taos_update_irq(s);
        break;
    case TAOS_INT_CLEAR:
        s->int_levels &= ~val;
        taos_update_irq(s);
        break;
    case TAOS_INT_SET:
        s->int_levels |= val;
        taos_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "taos: write reg 0x%" HWADDR_PRIx " = 0x%x\n", addr, val);
        break;
    }
}

static const MemoryRegionOps taos_ctrl_ops = {
    .read = taos_ctrl_read,
    .write = taos_ctrl_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t taos_clut_read(void *opaque, hwaddr addr, unsigned size)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    return s->palette[(addr >> 2) & 0xFF] & 0xFFFFFF00U;
}

static void taos_clut_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    uint32_t idx = (addr >> 2) & 0xFF;
    uint32_t entry = (uint32_t)val & 0xFFFFFF00U;

    if (s->palette[idx] == entry) {
        return;
    }
    s->palette[idx] = entry;
    uint32_t shifted = entry >> 8;
    s->pal32[idx] = 0xFF000000U | shifted;
    if (idx < 32) {
        s->rgb555_r[idx] = shifted & 0x00FF0000U;
        s->rgb555_g[idx] = shifted & 0x0000FF00U;
        s->rgb555_b[idx] = shifted & 0x000000FFU;
    }
    s->full_update = true;
}

static const MemoryRegionOps taos_clut_ops = {
    .read = taos_clut_read,
    .write = taos_clut_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void taos_reset(DeviceState *dev)
{
    PippinTaosState *s = PIPPIN_TAOS(dev);

    s->gpio_cfg = 0;
    s->gpio_out = 0;
    s->vsync_active = 0;
    s->fb_base = 0;
    s->row_words = 0;
    s->color_mode = 0;
    s->video_mode = 0;
    s->crt_ctrl = 0;
    s->crt_test = 0;
    s->int_levels = 0;
    s->int_enables = 0;
    memset(s->swatch_regs, 0, sizeof(s->swatch_regs));
    memset(s->palette, 0, sizeof(s->palette));
    taos_rebuild_luts(s);
    s->width = 640;
    s->height = 480;
    s->pitch = 640;
    s->depth = 8;
    s->crtc_on = false;
    s->crtc_was_on = false;
    s->full_update = true;
    s->vbl_period_ns = NANOSECONDS_PER_SECOND / 60;

    memset(s->athens_regs, 0, sizeof(s->athens_regs));
    s->athens_regs[1] = 2;    /* D2 */
    s->athens_regs[2] = 2;    /* N2 */
    s->athens_regs[3] = 0x62; /* P2_MUX2 */
    taos_update_irq(s);
}

static void taos_realize(DeviceState *dev, Error **errp)
{
    PippinTaosState *s = PIPPIN_TAOS(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->video_out && g_ascii_strcasecmp(s->video_out, "vga") == 0) {
        s->mon_id = TAOS_MON_ID_VGA;
    } else if (s->video_out && g_ascii_strcasecmp(s->video_out, "pal") == 0) {
        s->mon_id = TAOS_MON_ID_PAL;
    } else {
        s->mon_id = TAOS_MON_ID_NTSC;
    }

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_ram(&s->vram_mr, OBJECT(dev), "pippin-taos.vram",
                           TAOS_VRAM_SIZE, errp);
    if (*errp) {
        return;
    }
    memory_region_set_log(&s->vram_mr, true, DIRTY_MEMORY_VGA);
    s->vram = memory_region_get_ram_ptr(&s->vram_mr);
    sysbus_init_mmio(sbd, &s->vram_mr);

    memory_region_init_io(&s->ctrl_mr, OBJECT(dev), &taos_ctrl_ops, s,
                          "pippin-taos.ctrl", TAOS_IOREG_SIZE);
    sysbus_init_mmio(sbd, &s->ctrl_mr);

    memory_region_init_io(&s->clut_mr, OBJECT(dev), &taos_clut_ops, s,
                          "pippin-taos.clut", TAOS_CLUT_SIZE);
    sysbus_init_mmio(sbd, &s->clut_mr);

    s->con = graphic_console_init(dev, 0, &taos_gfx_ops, s);
    s->width = 640;
    s->height = 480;
    s->full_update = true;
    qemu_console_resize(s->con, s->width, s->height);

    s->vbl_period_ns = NANOSECONDS_PER_SECOND / 60;
    s->vbl_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, taos_vbl_tick, s);
    timer_mod(s->vbl_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                            (s->vbl_period_ns / 2));
}

static const Property taos_properties[] = {
    DEFINE_PROP_STRING("video-out", PippinTaosState, video_out),
};

static int taos_post_load(void *opaque, int version_id)
{
    PippinTaosState *s = PIPPIN_TAOS(opaque);
    taos_rebuild_luts(s);
    s->full_update = true;
    return 0;
}

static const VMStateDescription vmstate_pippin_taos = {
    .name = "pippin-taos",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = taos_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(palette, PippinTaosState, 256),
        VMSTATE_UINT32(fb_base, PippinTaosState),
        VMSTATE_UINT32(row_words, PippinTaosState),
        VMSTATE_UINT32(color_mode, PippinTaosState),
        VMSTATE_UINT32(crt_ctrl, PippinTaosState),
        VMSTATE_UINT32_ARRAY(swatch_regs, PippinTaosState, 16),
        VMSTATE_END_OF_LIST()
    }
};

static void taos_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = taos_realize;
    device_class_set_legacy_reset(dc, taos_reset);
    dc->vmsd = &vmstate_pippin_taos;
    device_class_set_props(dc, taos_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo taos_info = {
    .name          = TYPE_PIPPIN_TAOS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PippinTaosState),
    .class_init    = taos_class_init,
};

static void taos_register_types(void)
{
    type_register_static(&taos_info);
}

type_init(taos_register_types)
