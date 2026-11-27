/*
 * Apple GrandCentral I/O controller emulation for Pippin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/misc/macio/grandcentral.h"
#include "hw/nvram/mac_nvram.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define GC_ESCC_CLOCK    3686400
#define MACIO_INT_CLR    0x80000000U
#define MACIO_INT_MODE   0x80000000U

enum {
    GC_INT_EVENTS1 = 0x20,
    GC_INT_MASK1   = 0x24,
    GC_INT_CLEAR1  = 0x28,
    GC_INT_LEVELS1 = 0x2C,
};

static const uint8_t gc_mac_address[8] = {
    0x08, 0x00, 0x07, 0x44, 0x55, 0x66, 0x00, 0x00
};

static void gc_update_cpu_irq(GrandCentralState *s)
{
    qemu_set_irq(s->cpu_irq, !!(s->int_events & s->int_mask));
}

static void gc_pic_set_irq(void *opaque, int n, int level)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    uint32_t mask = (n >= 0 && n < 32) ? (1U << n) : 0;

    if (!mask) {
        return;
    }

    /* Native mode: 0->1 latches event; 68k mode (MACIO_INT_MODE): both edges latch */
    if ((s->int_mask & MACIO_INT_MODE) || (level && !(s->int_levels & mask))) {
        s->int_events |= mask;
    } else {
        s->int_events &= ~mask;
    }

    if (level) {
        s->int_levels |= mask;
    } else {
        s->int_levels &= ~mask;
    }

    gc_update_cpu_irq(s);
}

static uint64_t gc_pic_read(void *opaque, hwaddr addr, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);

    switch (addr) {
    case GC_INT_EVENTS1:
        return s->int_events & s->int_mask;
    case GC_INT_MASK1:
        return s->int_mask;
    case GC_INT_CLEAR1:
        return 0;
    case GC_INT_LEVELS1:
        return s->int_levels | (1U << 0x0B);
    default:
        return 0;
    }
}

static void gc_pic_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    uint32_t val = (uint32_t)val64;

    switch (addr) {
    case GC_INT_MASK1:
        s->int_mask = val;
        gc_update_cpu_irq(s);
        break;
    case GC_INT_CLEAR1:
        if ((s->int_mask & MACIO_INT_MODE) && (val & MACIO_INT_CLR)) {
            s->int_events = 0;
        } else {
            uint32_t clear_dma = val & 0x7FFU;
            s->int_events &= ~(val & 0x7FFFFFFFU);
            if (clear_dma) {
                if (s->int_mask & MACIO_INT_MODE) {
                    s->int_events |= s->int_levels & clear_dma;
                }
                s->int_levels &= ~clear_dma;
            }
        }
        gc_update_cpu_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps gc_pic_ops = {
    .read = gc_pic_read,
    .write = gc_pic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t gc_stub_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void gc_stub_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps gc_stub_ops = {
    .read = gc_stub_read,
    .write = gc_stub_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t gc_enet_rom_read(void *opaque, hwaddr addr, unsigned size)
{
    return gc_mac_address[(addr >> 4) & 7];
}

static const MemoryRegionOps gc_enet_rom_ops = {
    .read = gc_enet_rom_read,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t gc_nvram_addr_read(void *opaque, hwaddr addr, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    return s->nvram_addr_hi;
}

static void gc_nvram_addr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    s->nvram_addr_hi = val & 0xFF;
}

static const MemoryRegionOps gc_nvram_addr_ops = {
    .read = gc_nvram_addr_read,
    .write = gc_nvram_addr_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static uint64_t gc_nvram_data_read(void *opaque, hwaddr addr, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    uint32_t idx = (((uint32_t)s->nvram_addr_hi << 5) + ((addr >> 4) & 0x1F)) &
                   (GC_NVRAM_SIZE - 1);
    return s->nvram_data[idx];
}

static void gc_nvram_data_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    GrandCentralState *s = GRANDCENTRAL(opaque);
    uint32_t idx = (((uint32_t)s->nvram_addr_hi << 5) + ((addr >> 4) & 0x1F)) &
                   (GC_NVRAM_SIZE - 1);
    s->nvram_data[idx] = (uint8_t)val;
    if (s->nvram_blk) {
        if (blk_pwrite(s->nvram_blk, idx, 1, &s->nvram_data[idx], 0) < 0) {
            error_report("grandcentral: failed writing NVRAM byte 0x%x", idx);
        }
    }
}

static const MemoryRegionOps gc_nvram_data_ops = {
    .read = gc_nvram_data_read,
    .write = gc_nvram_data_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void gc_escc_legacy_setup(GrandCentralState *s)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(&s->escc);
    MemoryRegion *escc_legacy = g_new(MemoryRegion, 1);
    static const int maps[] = {
        0x00, 0x00, /* Command B */
        0x02, 0x20, /* Command A */
        0x04, 0x10, /* Data B */
        0x06, 0x30, /* Data A */
        0x08, 0x40, /* Enhancement B */
        0x0A, 0x50, /* Enhancement A */
        0x80, 0x80, /* Recovery count */
        0x90, 0x90, /* Start A */
        0xA0, 0xA0, /* Start B */
        0xB0, 0xB0, /* Detect AB */
    };

    memory_region_init(escc_legacy, OBJECT(s), "gc-escc-legacy", 256);
    for (int i = 0; i < ARRAY_SIZE(maps); i += 2) {
        MemoryRegion *port = g_new(MemoryRegion, 1);
        memory_region_init_alias(port, OBJECT(s), "gc-escc-legacy-port",
                                 sysbus_mmio_get_region(sbd, 0),
                                 maps[i + 1], 0x2);
        memory_region_add_subregion(escc_legacy, maps[i], port);
    }
    memory_region_add_subregion(&s->bar, 0x12000, escc_legacy);
}

static void grandcentral_realize(PCIDevice *d, Error **errp)
{
    GrandCentralState *s = GRANDCENTRAL(d);
    DeviceState *dev = DEVICE(d);
    SysBusDevice *sbd;

    qdev_init_gpio_in(dev, gc_pic_set_irq, 32);
    qdev_init_gpio_out(dev, &s->cpu_irq, 1);

    memory_region_init_io(&s->pic_mr, OBJECT(d), &gc_pic_ops, s,
                          "gc-pic", 0x40);
    memory_region_add_subregion(&s->bar, 0x00000, &s->pic_mr);

    if (!qdev_realize(DEVICE(&s->dbdma), BUS(&s->macio_bus), errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->dbdma);
    memory_region_add_subregion(&s->bar, 0x08000,
                                sysbus_mmio_get_region(sbd, 0));

    memory_region_init_io(&s->curio_mr, OBJECT(d), &gc_stub_ops, s,
                          "gc-curio-stub", 0x1000);
    memory_region_add_subregion(&s->bar, 0x10000, &s->curio_mr);

    memory_region_init_io(&s->mace_mr, OBJECT(d), &gc_stub_ops, s,
                          "gc-mace-stub", 0x1000);
    memory_region_add_subregion(&s->bar, 0x11000, &s->mace_mr);

    qdev_prop_set_uint32(DEVICE(&s->escc), "disabled", 0);
    qdev_prop_set_uint32(DEVICE(&s->escc), "frequency", GC_ESCC_CLOCK);
    qdev_prop_set_uint32(DEVICE(&s->escc), "it_shift", 4);
    qdev_prop_set_uint32(DEVICE(&s->escc), "chnBtype", escc_serial);
    qdev_prop_set_uint32(DEVICE(&s->escc), "chnAtype", escc_serial);
    if (!qdev_realize(DEVICE(&s->escc), BUS(&s->macio_bus), errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->escc);
    memory_region_add_subregion(&s->bar, 0x13000,
                                sysbus_mmio_get_region(sbd, 0));
    gc_escc_legacy_setup(s);
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(dev, GC_IRQ_SCCB));
    sysbus_connect_irq(sbd, 1, qdev_get_gpio_in(dev, GC_IRQ_SCCA));

    if (!qdev_realize(DEVICE(&s->screamer), BUS(&s->macio_bus), errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->screamer);
    memory_region_add_subregion(&s->bar, 0x14000,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(dev, GC_IRQ_DAVBUS));
    sysbus_connect_irq(sbd, 1, qdev_get_gpio_in(dev, GC_IRQ_DMA_AUDIO_OUT));
    sysbus_connect_irq(sbd, 2, qdev_get_gpio_in(dev, GC_IRQ_DMA_AUDIO_IN));
    macio_screamer_register_dma(&s->screamer, &s->dbdma, 0x10, 0x12);

    memory_region_init_io(&s->swim3_mr, OBJECT(d), &gc_stub_ops, s,
                          "gc-swim3-stub", 0x1000);
    memory_region_add_subregion(&s->bar, 0x15000, &s->swim3_mr);

    qdev_prop_set_uint64(DEVICE(&s->cuda), "timebase-frequency", s->frequency);
    if (!qdev_realize(DEVICE(&s->cuda), BUS(&s->macio_bus), errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->cuda);
    memory_region_add_subregion(&s->bar, 0x16000,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(dev, GC_IRQ_VIA_CUDA));

    if (!qdev_realize(DEVICE(&s->mesh), BUS(&s->macio_bus), errp)) {
        return;
    }
    sbd = SYS_BUS_DEVICE(&s->mesh);
    memory_region_add_subregion(&s->bar, 0x18000,
                                sysbus_mmio_get_region(sbd, 0));
    sysbus_connect_irq(sbd, 0, qdev_get_gpio_in(dev, GC_IRQ_SCSI_MESH));
    sysbus_connect_irq(sbd, 1, qdev_get_gpio_in(dev, GC_IRQ_DMA_SCSI_MESH));
    mac_mesh_register_dma(&s->mesh, &s->dbdma, 0x14);

    memory_region_init_io(&s->enet_rom_mr, OBJECT(d), &gc_enet_rom_ops, s,
                          "gc-enet-rom", 0x1000);
    memory_region_add_subregion(&s->bar, 0x19000, &s->enet_rom_mr);

    if (s->nvram_blk) {
        int64_t len = blk_getlength(s->nvram_blk);
        if (len != GC_NVRAM_SIZE) {
            error_setg(errp, "invalid GrandCentral NVRAM size %" PRId64 " (expected %d)",
                       len, GC_NVRAM_SIZE);
            return;
        }
        if (blk_set_perm(s->nvram_blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_ALL, errp) < 0) {
            return;
        }
        if (blk_pread(s->nvram_blk, 0, GC_NVRAM_SIZE, s->nvram_data, 0) < 0) {
            error_setg(errp, "failed reading GrandCentral NVRAM");
            return;
        }
    }

    memory_region_init_io(&s->nvram_addr_mr, OBJECT(d), &gc_nvram_addr_ops, s,
                          "gc-nvram-addr", 0x1000);
    memory_region_add_subregion(&s->bar, 0x1D000, &s->nvram_addr_mr);

    memory_region_init_io(&s->nvram_data_mr, OBJECT(d), &gc_nvram_data_ops, s,
                          "gc-nvram-data", 0x1000);
    memory_region_add_subregion(&s->bar, 0x1F000, &s->nvram_data_mr);

    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar);
}

static void grandcentral_reset(DeviceState *dev)
{
    GrandCentralState *s = GRANDCENTRAL(dev);

    s->int_events = 0;
    s->int_mask = 0;
    s->int_levels = 0;
    s->nvram_addr_hi = 0;
    gc_update_cpu_irq(s);
}

static void grandcentral_instance_init(Object *obj)
{
    GrandCentralState *s = GRANDCENTRAL(obj);

    memory_region_init(&s->bar, obj, "grandcentral", 0x20000);
    qbus_init(&s->macio_bus, sizeof(s->macio_bus), "macio-bus",
              DEVICE(obj), "gc.0");

    object_initialize_child(obj, "dbdma", &s->dbdma, TYPE_MAC_DBDMA);
    object_initialize_child(obj, "escc", &s->escc, TYPE_ESCC);
    object_initialize_child(obj, "cuda", &s->cuda, TYPE_CUDA);
    object_initialize_child(obj, "screamer", &s->screamer, TYPE_SCREAMER);
    object_initialize_child(obj, "mesh", &s->mesh, TYPE_MAC_MESH);
}

static const Property grandcentral_properties[] = {
    DEFINE_PROP_UINT64("frequency", GrandCentralState, frequency, 16500000),
    DEFINE_PROP_DRIVE("nvram-drive", GrandCentralState, nvram_blk),
};

static const VMStateDescription vmstate_grandcentral = {
    .name = "grandcentral",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, GrandCentralState),
        VMSTATE_UINT32(int_events, GrandCentralState),
        VMSTATE_UINT32(int_mask, GrandCentralState),
        VMSTATE_UINT32(int_levels, GrandCentralState),
        VMSTATE_UINT16(nvram_addr_hi, GrandCentralState),
        VMSTATE_UINT8_ARRAY(nvram_data, GrandCentralState, GC_NVRAM_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void grandcentral_class_init(ObjectClass *oc, const void *data)
{
    PCIDeviceClass *pdc = PCI_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    pdc->realize = grandcentral_realize;
    pdc->vendor_id = PCI_VENDOR_ID_APPLE;
    pdc->device_id = PCI_DEVICE_ID_APPLE_GRANDCENTRAL;
    pdc->class_id = PCI_CLASS_OTHERS << 8;
    pdc->revision = 0x02;
    device_class_set_legacy_reset(dc, grandcentral_reset);
    dc->vmsd = &vmstate_grandcentral;
    device_class_set_props(dc, grandcentral_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo grandcentral_info = {
    .name          = TYPE_GRANDCENTRAL,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GrandCentralState),
    .instance_init = grandcentral_instance_init,
    .class_init    = grandcentral_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void grandcentral_register_types(void)
{
    type_register_static(&grandcentral_info);
}

type_init(grandcentral_register_types)
