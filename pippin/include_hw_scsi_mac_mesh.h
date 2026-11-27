/*
 * Apple MESH (Macintosh Enhanced SCSI Hardware) emulation for Pippin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SCSI_MAC_MESH_H
#define HW_SCSI_MAC_MESH_H

#include "hw/core/sysbus.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/scsi/scsi.h"
#include "qom/object.h"

#define TYPE_MAC_MESH "mac-mesh"
OBJECT_DECLARE_SIMPLE_TYPE(MacMeshState, MAC_MESH)

#define MESH_FIFO_SIZE 16
#define MESH_TNT_ID    0xE2

struct MacMeshState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mem;
    qemu_irq irq;
    qemu_irq dma_irq;
    SCSIBus bus;
    void *dbdma;

    /* MESH registers */
    uint16_t xfer_count;
    uint8_t cur_cmd;
    uint8_t bus_stat0;
    uint8_t bus_stat1;
    uint8_t exception;
    uint8_t error;
    uint8_t int_mask;
    uint8_t int_stat;
    uint8_t src_id;
    uint8_t dst_id;
    uint8_t sync_params;
    uint8_t sel_timeout;
    bool check_parity;

    /* Hardware FIFO */
    uint8_t fifo[MESH_FIFO_SIZE];
    int fifo_pos;
    int fifo_len;

    /* Active SCSI transaction state */
    SCSIDevice *current_dev;
    SCSIRequest *current_req;
    uint8_t cdb[16];
    int cdb_len;
    int cdb_expected;
    uint8_t msg_out[16];
    int msg_out_len;
    uint8_t lun;
    uint8_t status_byte;
    bool req_completed;
    bool req_in_flight;
    bool in_data_phase;
    bool dma_active;
    bool in_xfer;
    uint32_t data_remaining; /* bytes remaining for current DataIn/DataOut command */
    uint8_t *scsi_buf;
    int scsi_buf_len;
    int scsi_buf_pos;

    /* Pending DBDMA request */
    DBDMA_io *pending_io;
};

void mac_mesh_register_dma(MacMeshState *s, void *dbdma, int channel);

#endif /* HW_SCSI_MAC_MESH_H */
