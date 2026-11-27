/*
 * Apple GrandCentral I/O controller emulation for Pippin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_MACIO_GRANDCENTRAL_H
#define HW_MISC_MACIO_GRANDCENTRAL_H

#include "hw/audio/screamer.h"
#include "hw/char/escc.h"
#include "hw/misc/macio/cuda.h"
#include "hw/pci/pci_device.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/scsi/mac_mesh.h"
#include "qom/object.h"
#include "system/block-backend.h"

#define TYPE_GRANDCENTRAL "grandcentral"
OBJECT_DECLARE_SIMPLE_TYPE(GrandCentralState, GRANDCENTRAL)

#define PCI_DEVICE_ID_APPLE_GRANDCENTRAL 0x0002

/* GrandCentral interrupt bit numbers */
enum {
    GC_IRQ_DMA_SCSI_CURIO = 0x00,
    GC_IRQ_DMA_SWIM3      = 0x01,
    GC_IRQ_DMA_ENET_TX    = 0x02,
    GC_IRQ_DMA_ENET_RX    = 0x03,
    GC_IRQ_DMA_ESCCA_TX   = 0x04,
    GC_IRQ_DMA_ESCCA_RX   = 0x05,
    GC_IRQ_DMA_ESCCB_TX   = 0x06,
    GC_IRQ_DMA_ESCCB_RX   = 0x07,
    GC_IRQ_DMA_AUDIO_OUT  = 0x08,
    GC_IRQ_DMA_AUDIO_IN   = 0x09,
    GC_IRQ_DMA_SCSI_MESH  = 0x0A,
    GC_IRQ_SCSI_CURIO     = 0x0C,
    GC_IRQ_SCSI_MESH      = 0x0D,
    GC_IRQ_ETHERNET       = 0x0E,
    GC_IRQ_SCCA           = 0x0F,
    GC_IRQ_SCCB           = 0x10,
    GC_IRQ_DAVBUS         = 0x11,
    GC_IRQ_VIA_CUDA       = 0x12,
    GC_IRQ_SWIM3          = 0x13,
    GC_IRQ_NMI            = 0x14,
    GC_IRQ_EXT1           = 0x15,
    GC_IRQ_BANDIT1        = 0x16,
    GC_IRQ_PCI_A          = 0x17,
    GC_IRQ_PCI_B          = 0x18,
    GC_IRQ_PCI_C          = 0x19,
    GC_IRQ_CONTROL        = 0x1A,
    GC_IRQ_SIXTY6         = 0x1B,
    GC_IRQ_PLANB          = 0x1C,
    GC_IRQ_PIPPIN_F       = 0x1D,
    GC_IRQ_PIPPIN_E       = 0x1E,
};

#define GC_NVRAM_SIZE 0x2000

struct GrandCentralState {
    /*< private >*/
    PCIDevice parent_obj;

    /*< public >*/
    MemoryRegion bar;
    MemoryRegion pic_mr;
    MemoryRegion curio_mr;
    MemoryRegion mace_mr;
    MemoryRegion swim3_mr;
    MemoryRegion enet_rom_mr;
    MemoryRegion nvram_addr_mr;
    MemoryRegion nvram_data_mr;

    BusState macio_bus;
    DBDMAState dbdma;
    ESCCState escc;
    CUDAState cuda;
    ScreamerState screamer;
    MacMeshState mesh;

    /* PIC state */
    uint32_t int_events;
    uint32_t int_mask;
    uint32_t int_levels;
    qemu_irq cpu_irq;

    /* NVRAM state (64 pages x 32 bytes = 8 KiB) */
    uint16_t nvram_addr_hi;
    uint8_t nvram_data[GC_NVRAM_SIZE];
    BlockBackend *nvram_blk;
    uint64_t frequency;
};

#endif /* HW_MISC_MACIO_GRANDCENTRAL_H */
