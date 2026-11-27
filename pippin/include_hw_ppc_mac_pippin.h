/*
 * Apple / Bandai Pippin machine definitions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PPC_MAC_PIPPIN_H
#define HW_PPC_MAC_PIPPIN_H

#include "hw/core/boards.h"
#include "hw/display/pippin_taos.h"
#include "hw/pci/pci_host.h"
#include "qom/object.h"
#include "system/block-backend.h"

#define TYPE_ASPEN_PCI_HOST "aspen-pci-host"
OBJECT_DECLARE_SIMPLE_TYPE(AspenPciState, ASPEN_PCI_HOST)

#define TYPE_PIPPIN_FLASH "pippin-flash"
OBJECT_DECLARE_SIMPLE_TYPE(PippinFlashState, PIPPIN_FLASH)

#define TYPE_PIPPIN_MACHINE MACHINE_TYPE_NAME("pippin")
OBJECT_DECLARE_SIMPLE_TYPE(PippinMachineState, PIPPIN_MACHINE)

#define PIPPIN_FLASH_BASE   0x40000000U
#define PIPPIN_FLASH_SIZE   0x00020000U /* 128 KiB Am29F010 */
#define PIPPIN_FLASH_WIN    0x00080000U /* CPU A2->A0 stride window */

#define PIPPIN_PCI_BASE     0xF2000000U
#define PIPPIN_ASPEN_BASE   0xF8000000U
#define PIPPIN_ROM_BASE     0xFFC00000U
#define PIPPIN_ROM_SIZE     0x00400000U /* 4 MiB */

struct AspenPciState {
    /*< private >*/
    PCIHostState parent_obj;

    /*< public >*/
    MemoryRegion pci_mmio;
    MemoryRegion pci_io;
    MemoryRegion mmio_alias_lo;
    MemoryRegion mmio_alias_hi;
    qemu_irq irqs[2]; /* PIPPIN_E, PIPPIN_F */
};

typedef enum {
    FLASH_STATE_READ = 0,
    FLASH_STATE_UNLOCK1,
    FLASH_STATE_UNLOCK2,
    FLASH_STATE_AUTOSELECT,
    FLASH_STATE_PROGRAM,
    FLASH_STATE_ERASE_UNLOCK1,
    FLASH_STATE_ERASE_UNLOCK2,
    FLASH_STATE_ERASE_SETUP,
} PippinFlashFsm;

struct PippinFlashState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mr;
    uint8_t data[PIPPIN_FLASH_SIZE];
    BlockBackend *blk;
    PippinFlashFsm state;
};

struct PippinMachineState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    char *video_out;

    /* Aspen memory controller state */
    MemoryRegion aspen_mr;
    uint8_t mem_config;
    uint8_t gpio_enable;
};

#endif /* HW_PPC_MAC_PIPPIN_H */
