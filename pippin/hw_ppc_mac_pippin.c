/*
 * Apple / Bandai Pippin machine emulation (Aspen + GrandCentral + Taos + MESH)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "hw/core/boards.h"
#include "hw/core/irq.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/sysbus.h"
#include "hw/display/pippin_taos.h"
#include "hw/input/adb.h"
#include "hw/misc/macio/grandcentral.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/mac_pippin.h"
#include "hw/ppc/ppc.h"
#include "hw/scsi/scsi.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "system/address-spaces.h"
#include "system/blockdev.h"
#include "system/ioport.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/ppc/cpu.h"

#define PIPPIN_TB_FREQ      16500000UL
#define PIPPIN_BUS_FREQ     66000000UL

static uint8_t bitrev8(uint8_t v)
{
    v = ((v & 0xF0) >> 4) | ((v & 0x0F) << 4);
    v = ((v & 0xCC) >> 2) | ((v & 0x33) << 2);
    v = ((v & 0xAA) >> 1) | ((v & 0x55) << 1);
    return v;
}

/* --------------------------------------------------------------------------
 * Aspen Memory Controller (0xF8000000)
 * -------------------------------------------------------------------------- */

static uint64_t aspen_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PippinMachineState *pms = PIPPIN_MACHINE(opaque);
    uint32_t reg = (addr >> 2) & 0x1F;

    switch (reg) {
    case 4: /* SYSTEM_ID (0x10) */
        return 0x40010000U;
    case 5: /* CHIP_REV (0x14) */
        return 0x01000000U;
    case 6: /* MEM_CONFIG (0x18) */
        return (uint32_t)pms->mem_config << 24;
    case 12: /* GPIO_IN (0x30) */
    case 14: /* GPIO_OUT (0x38) */
        return 0;
    case 13: /* GPIO_ENABLE (0x34) */
        return (uint32_t)pms->gpio_enable << 24;
    default:
        return 0;
    }
}

static void aspen_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PippinMachineState *pms = PIPPIN_MACHINE(opaque);
    uint32_t reg = (addr >> 2) & 0x1F;

    switch (reg) {
    case 6: /* MEM_CONFIG */
        pms->mem_config = (val >> 24) & 0xFF;
        break;
    case 13: /* GPIO_ENABLE */
        pms->gpio_enable = (val >> 24) & 0xFF;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps aspen_ctrl_ops = {
    .read = aspen_ctrl_read,
    .write = aspen_ctrl_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

/* --------------------------------------------------------------------------
 * Pippin Save Flash (AMD Am29F010, 128 KiB @ 0x40000000, bit-reversed bus)
 * -------------------------------------------------------------------------- */

static uint64_t pippin_flash_read(void *opaque, hwaddr addr, unsigned size)
{
    PippinFlashState *s = PIPPIN_FLASH(opaque);
    uint32_t faddr = (addr >> 2) & (PIPPIN_FLASH_SIZE - 1);
    uint8_t val = 0xFF;

    if (s->state == FLASH_STATE_AUTOSELECT) {
        switch (faddr & 0x03) {
        case 0:
            val = 0x01; /* AMD manufacturer ID */
            break;
        case 1:
            val = 0x20; /* Am29F010 device ID */
            break;
        case 2:
            val = 0x00; /* sector unprotected */
            break;
        default:
            val = 0xFF;
            break;
        }
    } else {
        val = s->data[faddr];
    }

    val = bitrev8(val);
    if (size == 4) {
        return (uint32_t)val << 24;
    } else if (size == 2) {
        return (uint32_t)val << 8;
    }
    return val;
}

static void pippin_flash_sync_sector(PippinFlashState *s, uint32_t sector_base)
{
    if (s->blk) {
        if (blk_pwrite(s->blk, sector_base, 0x4000,
                       &s->data[sector_base], 0) < 0) {
            error_report("pippin: failed erasing save flash sector 0x%x", sector_base);
        }
    }
}

static void pippin_flash_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    PippinFlashState *s = PIPPIN_FLASH(opaque);
    uint32_t faddr = (addr >> 2) & (PIPPIN_FLASH_SIZE - 1);
    uint8_t raw = (size == 4) ? ((val64 >> 24) & 0xFF)
                : (size == 2) ? ((val64 >> 8) & 0xFF)
                              : (val64 & 0xFF);
    uint8_t data = bitrev8(raw);

    if (s->state == FLASH_STATE_PROGRAM) {
        s->data[faddr] &= data;
        if (s->blk) {
            if (blk_pwrite(s->blk, faddr, 1, &s->data[faddr], 0) < 0) {
                error_report("pippin: failed programming save flash byte 0x%x", faddr);
            }
        }
        s->state = FLASH_STATE_READ;
        return;
    }

    if (data == 0xF0) {
        s->state = FLASH_STATE_READ;
        return;
    }

    switch (s->state) {
    case FLASH_STATE_READ:
    case FLASH_STATE_AUTOSELECT:
        if ((faddr & 0x7FFF) == 0x5555 && data == 0xAA) {
            s->state = FLASH_STATE_UNLOCK1;
        }
        break;
    case FLASH_STATE_UNLOCK1:
        if ((faddr & 0x7FFF) == 0x2AAA && data == 0x55) {
            s->state = FLASH_STATE_UNLOCK2;
        } else {
            s->state = FLASH_STATE_READ;
        }
        break;
    case FLASH_STATE_UNLOCK2:
        if ((faddr & 0x7FFF) == 0x5555) {
            switch (data) {
            case 0x90:
                s->state = FLASH_STATE_AUTOSELECT;
                break;
            case 0xA0:
                s->state = FLASH_STATE_PROGRAM;
                break;
            case 0x80:
                s->state = FLASH_STATE_ERASE_SETUP;
                break;
            default:
                s->state = FLASH_STATE_READ;
                break;
            }
        } else {
            s->state = FLASH_STATE_READ;
        }
        break;
    case FLASH_STATE_ERASE_SETUP:
        if ((faddr & 0x7FFF) == 0x5555 && data == 0xAA) {
            s->state = FLASH_STATE_ERASE_UNLOCK1;
        } else {
            s->state = FLASH_STATE_READ;
        }
        break;
    case FLASH_STATE_ERASE_UNLOCK1:
        if ((faddr & 0x7FFF) == 0x2AAA && data == 0x55) {
            s->state = FLASH_STATE_ERASE_UNLOCK2;
        } else {
            s->state = FLASH_STATE_READ;
        }
        break;
    case FLASH_STATE_ERASE_UNLOCK2:
        if ((faddr & 0x7FFF) == 0x5555 && data == 0x10) {
            memset(s->data, 0xFF, PIPPIN_FLASH_SIZE);
            if (s->blk) {
                if (blk_pwrite(s->blk, 0, PIPPIN_FLASH_SIZE,
                               s->data, 0) < 0) {
                    error_report("pippin: failed erasing save flash chip");
                }
            }
        } else if (data == 0x30) {
            uint32_t sector_base = faddr & ~0x3FFFU;
            memset(&s->data[sector_base], 0xFF, 0x4000);
            pippin_flash_sync_sector(s, sector_base);
        }
        s->state = FLASH_STATE_READ;
        break;
    default:
        s->state = FLASH_STATE_READ;
        break;
    }
}

static const MemoryRegionOps pippin_flash_ops = {
    .read = pippin_flash_read,
    .write = pippin_flash_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pippin_flash_realize(DeviceState *dev, Error **errp)
{
    PippinFlashState *s = PIPPIN_FLASH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memset(s->data, 0xFF, PIPPIN_FLASH_SIZE);
    if (s->blk) {
        int64_t len = blk_getlength(s->blk);
        if (len < PIPPIN_FLASH_SIZE) {
            error_setg(errp, "pippin: save flash image too small (expected %d bytes)",
                       PIPPIN_FLASH_SIZE);
            return;
        }
        if (blk_set_perm(s->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_ALL, errp) < 0) {
            return;
        }
        if (blk_pread(s->blk, 0, PIPPIN_FLASH_SIZE, s->data, 0) < 0) {
            error_setg(errp, "pippin: failed reading save flash image");
            return;
        }
    }

    memory_region_init_io(&s->mr, OBJECT(dev), &pippin_flash_ops, s,
                          "pippin-save-flash", PIPPIN_FLASH_WIN);
    sysbus_init_mmio(sbd, &s->mr);
}

static void pippin_flash_reset(DeviceState *dev)
{
    PippinFlashState *s = PIPPIN_FLASH(dev);
    s->state = FLASH_STATE_READ;
}

static const Property pippin_flash_properties[] = {
    DEFINE_PROP_DRIVE("drive", PippinFlashState, blk),
};

static void pippin_flash_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = pippin_flash_realize;
    device_class_set_legacy_reset(dc, pippin_flash_reset);
    device_class_set_props(dc, pippin_flash_properties);
}

static const TypeInfo pippin_flash_info = {
    .name          = TYPE_PIPPIN_FLASH,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PippinFlashState),
    .class_init    = pippin_flash_class_init,
};

/* --------------------------------------------------------------------------
 * Aspen PCI Host Bridge (0xF2000000)
 * -------------------------------------------------------------------------- */

static PCIDevice *aspen_pci_resolve(PCIHostState *phb, uint32_t *out_reg)
{
    uint32_t cfg = phb->config_reg;

    if (cfg & 1U) {
        /* Type-1 configuration cycle */
        *out_reg = cfg & 0xFFU;
        return pci_find_device(phb->bus, (cfg >> 16) & 0xFF, (cfg >> 8) & 0xFF);
    }

    /* Type-0 configuration cycle: Aspen IDSEL maps dev_num = (cfg >> 11) + 11 */
    uint32_t dev_slot = ((cfg >> 11) & 0x1FU) + 11U;
    if (dev_slot > 31U) {
        return NULL;
    }
    *out_reg = cfg & 0xFFU;
    return pci_find_device(phb->bus, 0, PCI_DEVFN(dev_slot, (cfg >> 8) & 7U));
}

static uint64_t aspen_pci_data_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(opaque);
    uint32_t reg = 0;
    PCIDevice *d = aspen_pci_resolve(phb, &reg);

    if (!d) {
        return MAKE_64BIT_MASK(0, size * 8);
    }
    return pci_host_config_read_common(d, (reg & ~3U) | (addr & 3U),
                                       pci_config_size(d), size);
}

static void aspen_pci_data_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(opaque);
    uint32_t reg = 0;
    PCIDevice *d = aspen_pci_resolve(phb, &reg);

    if (!d) {
        return;
    }
    pci_host_config_write_common(d, (reg & ~3U) | (addr & 3U),
                                  pci_config_size(d), val, size);
}

static const MemoryRegionOps aspen_pci_data_ops = {
    .read = aspen_pci_data_read,
    .write = aspen_pci_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int aspen_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int slot = PCI_SLOT(pci_dev->devfn);
    if (slot == 0x0E) {
        return 0;
    }
    return 1;
}

static void aspen_pci_set_irq(void *opaque, int irq_num, int level)
{
    AspenPciState *s = ASPEN_PCI_HOST(opaque);
    if (irq_num >= 0 && irq_num < ARRAY_SIZE(s->irqs) && s->irqs[irq_num]) {
        qemu_set_irq(s->irqs[irq_num], level);
    }
}

static void aspen_pci_realize(DeviceState *dev, Error **errp)
{
    AspenPciState *s = ASPEN_PCI_HOST(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->pci_mmio, OBJECT(dev), "aspen-pci-mmio", 0x100000000ULL);
    memory_region_init_io(&s->pci_io, OBJECT(dev), &unassigned_io_ops,
                          dev, "aspen-pci-io", 64 * KiB);

    phb->bus = pci_register_root_bus(dev, NULL, aspen_pci_set_irq,
                                     aspen_pci_map_irq, s,
                                     &s->pci_mmio, &s->pci_io,
                                     0, 2, TYPE_PCI_BUS);

    memory_region_init_io(&phb->conf_mem, OBJECT(dev), &pci_host_conf_be_ops,
                          dev, "aspen-pci-conf", 4 * KiB);
    memory_region_init_io(&phb->data_mem, OBJECT(dev), &aspen_pci_data_ops,
                          dev, "aspen-pci-data", 4 * KiB);

    sysbus_init_mmio(sbd, &phb->conf_mem);
    sysbus_init_mmio(sbd, &phb->data_mem);
    qdev_init_gpio_out_named(dev, s->irqs, "aspen-pci-irq", 2);

    /* Expose PCI MMIO windows while leaving Taos (0xF0xxxxxx) & Aspen (0xF2xxxxxx) clear */
    memory_region_init_alias(&s->mmio_alias_lo, OBJECT(dev), "aspen-pci-mmio-lo",
                             &s->pci_mmio, 0x80000000ULL, 0x70000000ULL);
    memory_region_add_subregion(get_system_memory(), 0x80000000ULL, &s->mmio_alias_lo);

    memory_region_init_alias(&s->mmio_alias_hi, OBJECT(dev), "aspen-pci-mmio-hi",
                             &s->pci_mmio, 0xF3000000ULL, 0x05000000ULL);
    memory_region_add_subregion(get_system_memory(), 0xF3000000ULL, &s->mmio_alias_hi);
}

static void aspen_pci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspen_pci_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo aspen_pci_info = {
    .name          = TYPE_ASPEN_PCI_HOST,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(AspenPciState),
    .class_init    = aspen_pci_class_init,
};

/* --------------------------------------------------------------------------
 * Pippin Machine Init
 * -------------------------------------------------------------------------- */

static void pippin_cpu_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;
    CPUPPCState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    cpu_ppc_tb_reset(env);
    env->nip = 0xFFF00100UL;
}

typedef struct AspenProbeAlias {
    MemoryRegion mr;
    uint8_t *base_ptr;
    uint32_t bank_mask;
} AspenProbeAlias;

static uint64_t aspen_probe_read(void *opaque, hwaddr addr, unsigned size)
{
    AspenProbeAlias *a = opaque;
    uint8_t *p = a->base_ptr + (addr & a->bank_mask);
    switch (size) {
    case 1: return ldub_p(p);
    case 2: return lduw_be_p(p);
    case 4: return ldl_be_p(p);
    case 8: return ldq_be_p(p);
    default: return 0;
    }
}

static void aspen_probe_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    AspenProbeAlias *a = opaque;
    uint8_t *p = a->base_ptr + (addr & a->bank_mask);
    switch (size) {
    case 1: stb_p(p, val); break;
    case 2: stw_be_p(p, val); break;
    case 4: stl_be_p(p, val); break;
    case 8: stq_be_p(p, val); break;
    }
}

static const MemoryRegionOps aspen_probe_ops = {
    .read = aspen_probe_read,
    .write = aspen_probe_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pippin_map_ram_banks(PippinMachineState *pms, MemoryRegion *ram_mr)
{
    /*
     * Aspen has 4 DRAM banks at 16 MiB stride (0x00, 0x01, 0x02, 0x03 * 16 MiB):
     *   Bank 0 = 4 MiB soldered (0x00000000)
     *   Bank 1 = 1 MiB soldered (0x01000000)
     *   Bank 2 = 0/1/4/8/16 MiB expansion (0x02000000)
     *   Bank 3 = 0/1/4/8/16 MiB expansion (0x03000000)
     */
    static const uint32_t valid_exp[] = { 16 * MiB, 8 * MiB, 4 * MiB, 1 * MiB };
    uint32_t bank_sizes[4] = { 4 * MiB, 1 * MiB, 0, 0 };
    uint64_t total = pms->parent_obj.ram_size;
    uint64_t rem = (total > 5 * MiB) ? (total - 5 * MiB) : 0;
    uint64_t ram_off = 0;

    for (int b = 2; b < 4 && rem > 0; b++) {
        for (int i = 0; i < ARRAY_SIZE(valid_exp); i++) {
            if (rem >= valid_exp[i]) {
                bank_sizes[b] = valid_exp[i];
                rem -= valid_exp[i];
                break;
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        uint32_t sz = bank_sizes[i];
        hwaddr base = (hwaddr)i * (16 * MiB);
        uint32_t row_mask, col_mask, probe_off;
        g_autofree char *name = NULL;
        MemoryRegion *bank_mr;

        if (!sz) {
            continue;
        }

        name = g_strdup_printf("pippin.ram.bank%d", i);
        bank_mr = g_new(MemoryRegion, 1);
        memory_region_init_alias(bank_mr, OBJECT(pms), name, ram_mr, ram_off, sz);
        memory_region_add_subregion(get_system_memory(), base, bank_mr);

        switch (sz) {
        case 1 * MiB:
            row_mask = (1 << 9) - 1;
            col_mask = (1 << 9) - 1;
            break;
        case 4 * MiB:
            row_mask = (1 << 10) - 1;
            col_mask = (1 << 10) - 1;
            break;
        case 8 * MiB:
            row_mask = (1 << 11) - 1;
            col_mask = (1 << 10) - 1;
            break;
        default:
            row_mask = (1 << 11) - 1;
            col_mask = (1 << 11) - 1;
            break;
        }

        probe_off = ((0xC01000 >> 13) & row_mask) | ((0xC01000 >> 2) & col_mask);

        static const struct {
            hwaddr alias_off;
            uint32_t max_sz;
        } mirrors[] = {
            { 0xC01000, UINT32_MAX },
            { 0xC00000, 16 * MiB },
            { 0x400000, 8 * MiB },
        };

        for (int m = 0; m < ARRAY_SIZE(mirrors); m++) {
            if (sz < mirrors[m].max_sz) {
                g_autofree char *mname = g_strdup_printf("pippin.ram.bank%d.m%d", i, m);
                AspenProbeAlias *alias = g_new0(AspenProbeAlias, 1);
                alias->base_ptr = (uint8_t *)memory_region_get_ram_ptr(ram_mr) + ram_off + probe_off;
                alias->bank_mask = 0xFFF;
                memory_region_init_io(&alias->mr, OBJECT(pms), &aspen_probe_ops,
                                      alias, mname, 0x1000);
                memory_region_add_subregion(get_system_memory(),
                                            base + mirrors[m].alias_off, &alias->mr);
            }
        }

        ram_off += sz;
    }
}

static void pippin_init(MachineState *machine)
{
    PippinMachineState *pms = PIPPIN_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    PowerPCCPU *cpu;
    CPUPPCState *env;
    DeviceState *pci_dev, *taos_dev, *gc_dev, *mesh_dev;
    PCIDevice *gc_pci;
    GrandCentralState *gc;
    ADBBusState *adb_bus;
    DriveInfo *dinfo;
    MemoryRegion *rom_mr;
    g_autofree char *bios_path = NULL;
    int rom_size;

    /* CPU: PPC603 @ 66 MHz */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("pippin: incompatible CPU bus model");
        exit(1);
    }
    cpu_ppc_tb_init(env, PIPPIN_TB_FREQ);
    qemu_register_reset(pippin_cpu_reset, cpu);

    /* Allocate RAM and install Aspen bank layout + probe mirrors */
    pippin_map_ram_banks(pms, machine->ram);

    /* Load 4 MiB Pippin Kinka ROM at 0xFFC00000 */
    rom_mr = g_new(MemoryRegion, 1);
    memory_region_init_rom(rom_mr, NULL, "pippin.rom", PIPPIN_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, PIPPIN_ROM_BASE, rom_mr);

    if (!machine->firmware) {
        error_report("pippin: -bios <kinka-rom.bin> is required");
        exit(1);
    }
    bios_path = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (!bios_path) {
        error_report("pippin: could not find ROM image '%s'", machine->firmware);
        exit(1);
    }
    rom_size = load_image_targphys(bios_path, PIPPIN_ROM_BASE, PIPPIN_ROM_SIZE,
                                   &error_fatal);
    if (rom_size < 0 || rom_size > PIPPIN_ROM_SIZE) {
        error_report("pippin: invalid ROM image '%s'", bios_path);
        exit(1);
    }

    /* Aspen Memory Controller registers @ 0xF8000000 */
    memory_region_init_io(&pms->aspen_mr, OBJECT(pms), &aspen_ctrl_ops, pms,
                          "aspen-ctrl", 0x800);
    memory_region_add_subregion(sysmem, PIPPIN_ASPEN_BASE, &pms->aspen_mr);

    /* Am29F010 Save Flash @ 0x40000000 */
    {
        DeviceState *flash_dev = qdev_new(TYPE_PIPPIN_FLASH);
        dinfo = drive_get(IF_PFLASH, 0, 0);
        if (dinfo) {
            qdev_prop_set_drive(flash_dev, "drive", blk_by_legacy_dinfo(dinfo));
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(flash_dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(flash_dev), 0, PIPPIN_FLASH_BASE);
    }

    /* Aspen PCI Host Bridge @ 0xF2800000 / 0xF2C00000 */
    pci_dev = qdev_new(TYPE_ASPEN_PCI_HOST);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(pci_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(pci_dev), 0, PIPPIN_PCI_BASE + 0x800000U);
    sysbus_mmio_map(SYS_BUS_DEVICE(pci_dev), 1, PIPPIN_PCI_BASE + 0xC00000U);

    /* GrandCentral I/O Controller at PCI dev 0x10 fn 0 */
    gc_pci = pci_new(PCI_DEVFN(0x10, 0), TYPE_GRANDCENTRAL);
    gc_dev = DEVICE(gc_pci);
    gc = GRANDCENTRAL(gc_pci);
    qdev_prop_set_uint64(gc_dev, "frequency", PIPPIN_TB_FREQ);

    dinfo = drive_get(IF_MTD, 0, 0);
    if (dinfo) {
        qdev_prop_set_drive(gc_dev, "nvram-drive", blk_by_legacy_dinfo(dinfo));
    }
    if (machine->audiodev) {
        DeviceState *sc = DEVICE(object_resolve_path_component(OBJECT(gc), "screamer"));
        if (sc) {
            qdev_prop_set_string(sc, "audiodev", machine->audiodev);
        }
    }
    pci_realize_and_unref(gc_pci, PCI_HOST_BRIDGE(pci_dev)->bus, &error_fatal);

    qdev_connect_gpio_out(gc_dev, 0,
                          qdev_get_gpio_in(DEVICE(cpu), PPC6xx_INPUT_INT));
    qdev_connect_gpio_out_named(pci_dev, "aspen-pci-irq", 0,
                                qdev_get_gpio_in(gc_dev, GC_IRQ_PIPPIN_E));
    qdev_connect_gpio_out_named(pci_dev, "aspen-pci-irq", 1,
                                qdev_get_gpio_in(gc_dev, GC_IRQ_PIPPIN_F));

    /* Taos Video Controller (VRAM, IO regs, CLUT, Athens/Bt856 I2C) */
    taos_dev = qdev_new(TYPE_PIPPIN_TAOS);
    if (pms->video_out) {
        qdev_prop_set_string(taos_dev, "video-out", pms->video_out);
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(taos_dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(taos_dev), 0, TAOS_VRAM_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(taos_dev), 1, TAOS_IOREG_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(taos_dev), 2, TAOS_CLUT_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(taos_dev), 0,
                       qdev_get_gpio_in(gc_dev, GC_IRQ_PIPPIN_E));

    /* Connect Taos I2C slaves (Athens & Bt856) + ADB devices onto Cuda */
    gc->cuda.i2c_cmd = pippin_taos_i2c_cmd;
    gc->cuda.i2c_opaque = taos_dev;
    adb_bus = &gc->cuda.adb_bus;
    {
        DeviceState *aj_dev = qdev_new("adb-applejack");
        object_property_add_child(OBJECT(pms), "applejack", OBJECT(aj_dev));
        qdev_realize_and_unref(aj_dev, BUS(adb_bus), &error_fatal);
    }
    qdev_realize_and_unref(qdev_new("adb-keyboard"), BUS(adb_bus), &error_fatal);

    /* Optional CUDA 256-byte PRAM persistence on IF_MTD index 1 */
    dinfo = drive_get(IF_MTD, 0, 1);
    if (dinfo) {
        gc->cuda.pram_blk = blk_by_legacy_dinfo(dinfo);
        if (gc->cuda.pram_blk && blk_getlength(gc->cuda.pram_blk) >= 256) {
            if (blk_attach_dev(gc->cuda.pram_blk, DEVICE(&gc->cuda)) == 0 &&
                blk_set_perm(gc->cuda.pram_blk,
                             BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                             BLK_PERM_ALL, &error_fatal) == 0) {
                if (blk_pread(gc->cuda.pram_blk, 0, 256, gc->cuda.pram, 0) < 0) {
                    error_report("pippin: failed reading CUDA PRAM");
                }
            }
        }
    }

    /* Attach CD-ROM / SCSI devices onto MESH */
    mesh_dev = DEVICE(object_resolve_path_component(OBJECT(gc), "mesh"));
    if (mesh_dev) {
        SCSIBus *scsi_bus = SCSI_BUS(qdev_get_child_bus(mesh_dev, "mesh-scsi.0"));
        if (scsi_bus) {
            scsi_bus_legacy_handle_cmdline(scsi_bus);
        }
    }
}

static char *pippin_get_video_out(Object *obj, Error **errp)
{
    PippinMachineState *pms = PIPPIN_MACHINE(obj);
    return g_strdup(pms->video_out ? pms->video_out : "ntsc");
}

static void pippin_set_video_out(Object *obj, const char *value, Error **errp)
{
    PippinMachineState *pms = PIPPIN_MACHINE(obj);
    g_free(pms->video_out);
    pms->video_out = g_strdup(value);
}

static void pippin_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Apple / Bandai Pippin";
    mc->init = pippin_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("603");
    mc->default_ram_size = 13 * MiB;
    mc->default_ram_id = "pippin.ram";
    mc->block_default_type = IF_SCSI;
    mc->ignore_boot_device_suffixes = true;

    machine_add_audiodev_property(mc);

    object_class_property_add_str(oc, "video-out",
                                  pippin_get_video_out,
                                  pippin_set_video_out);
    object_class_property_set_description(oc, "video-out",
        "Video output mode: ntsc (default), pal, or vga");
}

static const TypeInfo pippin_machine_info = {
    .name          = TYPE_PIPPIN_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(PippinMachineState),
    .class_init    = pippin_machine_class_init,
};

static void pippin_register_types(void)
{
    type_register_static(&aspen_pci_info);
    type_register_static(&pippin_flash_info);
    type_register_static(&pippin_machine_info);
}

type_init(pippin_register_types)
