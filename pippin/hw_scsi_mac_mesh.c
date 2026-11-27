/*
 * Apple MESH (Macintosh Enhanced SCSI Hardware) emulation for Pippin
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/scsi/mac_mesh.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/dma.h"

/* MESH register indices (offset >> 4) */
enum {
    MESH_REG_XFER_COUNT0 = 0x0,
    MESH_REG_XFER_COUNT1 = 0x1,
    MESH_REG_FIFO        = 0x2,
    MESH_REG_SEQUENCE    = 0x3,
    MESH_REG_BUS_STAT0   = 0x4,
    MESH_REG_BUS_STAT1   = 0x5,
    MESH_REG_FIFO_COUNT  = 0x6,
    MESH_REG_EXCEPTION   = 0x7,
    MESH_REG_ERROR       = 0x8,
    MESH_REG_INT_MASK    = 0x9,
    MESH_REG_INTERRUPT   = 0xA,
    MESH_REG_SOURCE_ID   = 0xB,
    MESH_REG_DEST_ID     = 0xC,
    MESH_REG_SYNC_PARAMS = 0xD,
    MESH_REG_MESH_ID     = 0xE,
    MESH_REG_SEL_TIMEOUT = 0xF,
};

/* MESH sequencer opcodes (cmd & 0x0F) */
enum {
    MESH_CMD_NOP           = 0x0,
    MESH_CMD_ARBITRATE     = 0x1,
    MESH_CMD_SELECT        = 0x2,
    MESH_CMD_COMMAND       = 0x3,
    MESH_CMD_STATUS        = 0x4,
    MESH_CMD_DATA_OUT      = 0x5,
    MESH_CMD_DATA_IN       = 0x6,
    MESH_CMD_MESSAGE_OUT   = 0x7,
    MESH_CMD_MESSAGE_IN    = 0x8,
    MESH_CMD_BUS_FREE      = 0x9,
    MESH_CMD_ENA_PARITY    = 0xA,
    MESH_CMD_DIS_PARITY    = 0xB,
    MESH_CMD_ENA_RESEL     = 0xC,
    MESH_CMD_DIS_RESEL     = 0xD,
    MESH_CMD_RESET_MESH    = 0xE,
    MESH_CMD_FLUSH_FIFO    = 0xF,
};

#define MESH_CMD_DMA           0x80
#define MESH_CMD_ATN           0x20

/* BusStatus0 bits */
#define MESH_BS0_IO            (1 << 0)
#define MESH_BS0_CD            (1 << 1)
#define MESH_BS0_MSG           (1 << 2)
#define MESH_BS0_ATN           (1 << 3)
#define MESH_BS0_ACK           (1 << 4)
#define MESH_BS0_REQ           (1 << 5)
#define MESH_BS0_PHASE_MASK    (MESH_BS0_IO | MESH_BS0_CD | MESH_BS0_MSG)

/* BusStatus1 bits */
#define MESH_BS1_SEL           (1 << 5)
#define MESH_BS1_BSY           (1 << 6)
#define MESH_BS1_RST           (1 << 7)

/* SCSI phase combinations in BusStatus0 */
#define MESH_PHASE_DATA_OUT    0
#define MESH_PHASE_DATA_IN     MESH_BS0_IO
#define MESH_PHASE_COMMAND     MESH_BS0_CD
#define MESH_PHASE_STATUS      (MESH_BS0_CD | MESH_BS0_IO)
#define MESH_PHASE_MESSAGE_OUT (MESH_BS0_MSG | MESH_BS0_CD)
#define MESH_PHASE_MESSAGE_IN  (MESH_BS0_MSG | MESH_BS0_CD | MESH_BS0_IO)

/* Exception register bits */
#define MESH_EXC_SEL_TIMEOUT   (1 << 0)
#define MESH_EXC_PHASE_MM      (1 << 1)
#define MESH_EXC_ARB_LOST      (1 << 2)

/* Interrupt / IntMask bits */
#define MESH_INT_CMD_DONE      (1 << 0)
#define MESH_INT_EXCEPTION     (1 << 1)
#define MESH_INT_ERROR         (1 << 2)
#define MESH_INT_ALL           (MESH_INT_CMD_DONE | MESH_INT_EXCEPTION | MESH_INT_ERROR)

static void mesh_update_irq(MacMeshState *s)
{
    qemu_set_irq(s->irq, !!(s->int_stat & s->int_mask));
}

static void mesh_set_phase(MacMeshState *s, uint8_t phase, bool req)
{
    s->bus_stat0 = (s->bus_stat0 & ~(MESH_BS0_PHASE_MASK | MESH_BS0_REQ)) |
                   (phase & MESH_BS0_PHASE_MASK) |
                   (req ? MESH_BS0_REQ : 0);
}

static void mesh_fifo_flush(MacMeshState *s)
{
    s->fifo_pos = 0;
    s->fifo_len = 0;
}

static void mesh_fifo_push(MacMeshState *s, uint8_t val)
{
    if (s->fifo_len < MESH_FIFO_SIZE) {
        int tail = (s->fifo_pos + s->fifo_len) % MESH_FIFO_SIZE;
        s->fifo[tail] = val;
        s->fifo_len++;
    }
}

static uint8_t mesh_fifo_pop(MacMeshState *s)
{
    uint8_t val = 0;
    if (s->fifo_len > 0) {
        val = s->fifo[s->fifo_pos];
        s->fifo_pos = (s->fifo_pos + 1) % MESH_FIFO_SIZE;
        s->fifo_len--;
    }
    return val;
}

static void mesh_release_request(MacMeshState *s)
{
    if (s->current_req) {
        SCSIRequest *req = s->current_req;
        s->current_req = NULL;
        req->hba_private = NULL;
        scsi_req_cancel(req);
        scsi_req_unref(req);
    }
    s->scsi_buf = NULL;
    s->scsi_buf_len = 0;
    s->scsi_buf_pos = 0;
    s->in_data_phase = false;
    s->req_completed = false;
    s->req_in_flight = false;
}

static void mesh_bus_reset(MacMeshState *s)
{
    mesh_release_request(s);
    if (s->pending_io) {
        DBDMA_io *io = s->pending_io;
        s->pending_io = NULL;
        io->len = 0;
        io->dma_end(io);
    }
    mesh_fifo_flush(s);
    s->current_dev = NULL;
    s->bus_stat0 = 0;
    s->bus_stat1 &= MESH_BS1_RST;
    s->data_remaining = 0;
    s->cdb_len = 0;
    s->cdb_expected = 0;
    s->msg_out_len = 0;
    s->lun = 0;
    s->dma_active = false;
    s->in_xfer = false;
    s->req_in_flight = false;
}

static void mesh_start_cdb(MacMeshState *s)
{
    int32_t n;

    if (!s->current_dev) {
        s->exception |= MESH_EXC_PHASE_MM;
        s->int_stat |= MESH_INT_EXCEPTION | MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        return;
    }

    mesh_release_request(s);
    s->status_byte = 0;
    s->req_completed = false;
    s->scsi_buf = NULL;
    s->scsi_buf_len = 0;
    s->scsi_buf_pos = 0;

    s->current_req = scsi_req_new(s->current_dev, 0, s->lun,
                                  s->cdb, s->cdb_len, s);
    n = scsi_req_enqueue(s->current_req);
    if (n) {
        s->req_in_flight = true;
        scsi_req_continue(s->current_req);
    }
}

static void mesh_finish_data_cmd(MacMeshState *s)
{
    s->dma_active = false;
    if (s->req_completed) {
        mesh_set_phase(s, MESH_PHASE_STATUS, true);
    }
    s->int_stat |= MESH_INT_CMD_DONE;
    mesh_update_irq(s);
}

static void mesh_pump_pio_datain(MacMeshState *s)
{
    if (!s->in_data_phase || s->dma_active || s->in_xfer) {
        return;
    }

    s->in_xfer = true;
    while (s->data_remaining > 0 && s->fifo_len < MESH_FIFO_SIZE) {
        if (s->scsi_buf_pos >= s->scsi_buf_len) {
            if (s->req_completed || !s->current_req || s->req_in_flight) {
                break;
            }
            s->req_in_flight = true;
            scsi_req_continue(s->current_req);
            if (s->scsi_buf_pos >= s->scsi_buf_len) {
                break;
            }
        }
        mesh_fifo_push(s, s->scsi_buf[s->scsi_buf_pos++]);
        s->data_remaining--;
        if (s->xfer_count > 0) {
            s->xfer_count--;
        }
    }
    if (s->scsi_buf_pos >= s->scsi_buf_len && !s->req_completed &&
        s->current_req && !s->req_in_flight) {
        s->req_in_flight = true;
        scsi_req_continue(s->current_req);
    }
    s->in_xfer = false;

    if (s->fifo_len == 0 && (s->data_remaining == 0 || s->req_completed)) {
        mesh_finish_data_cmd(s);
    }
}

static void mesh_try_dma_xfer(MacMeshState *s)
{
    if (s->in_xfer) {
        return;
    }

    s->in_xfer = true;
    while (s->pending_io && s->in_data_phase && s->dma_active) {
        DBDMA_io *io = s->pending_io;
        bool is_out = ((s->cur_cmd & 0x0F) == MESH_CMD_DATA_OUT);
        int xfer;

        if (is_out != (bool)io->is_dma_out) {
            /* Direction mismatch between DBDMA descriptor and MESH phase */
            break;
        }

        if (s->data_remaining == 0 ||
            (s->req_completed && s->scsi_buf_pos >= s->scsi_buf_len)) {
            if (io->len == 0 || s->req_completed) {
                s->pending_io = NULL;
                io->dma_end(io);
            }
            s->in_xfer = false;
            mesh_finish_data_cmd(s);
            return;
        }

        if (s->scsi_buf_pos >= s->scsi_buf_len) {
            if (!s->req_completed && s->current_req && !s->req_in_flight) {
                s->req_in_flight = true;
                scsi_req_continue(s->current_req);
            }
            if (s->scsi_buf_pos >= s->scsi_buf_len) {
                /* Wait for async SCSI completion callback */
                break;
            }
        }

        xfer = MIN(io->len, (int)s->data_remaining);
        xfer = MIN(xfer, s->scsi_buf_len - s->scsi_buf_pos);
        if (xfer <= 0) {
            if (io->len == 0) {
                s->pending_io = NULL;
                io->dma_end(io);
                continue;
            }
            break;
        }

        if (is_out) {
            dma_memory_read(&address_space_memory, io->addr,
                            s->scsi_buf + s->scsi_buf_pos, xfer,
                            MEMTXATTRS_UNSPECIFIED);
        } else {
            dma_memory_write(&address_space_memory, io->addr,
                             s->scsi_buf + s->scsi_buf_pos, xfer,
                             MEMTXATTRS_UNSPECIFIED);
        }

        s->scsi_buf_pos += xfer;
        io->addr += xfer;
        io->len -= xfer;
        s->data_remaining -= xfer;
        if (s->xfer_count > 0) {
            s->xfer_count = (s->xfer_count > xfer) ? (s->xfer_count - xfer) : 0;
        }

        if (s->scsi_buf_pos >= s->scsi_buf_len && !s->req_completed &&
            s->current_req && !s->req_in_flight) {
            s->req_in_flight = true;
            scsi_req_continue(s->current_req);
        }

        if (io->len == 0) {
            s->pending_io = NULL;
            io->dma_end(io);
        }

        if (s->data_remaining == 0 ||
            (s->req_completed && s->scsi_buf_pos >= s->scsi_buf_len)) {
            if (s->pending_io && s->req_completed) {
                DBDMA_io *rem = s->pending_io;
                s->pending_io = NULL;
                rem->dma_end(rem);
            }
            s->in_xfer = false;
            mesh_finish_data_cmd(s);
            return;
        }
    }
    s->in_xfer = false;
}

static void mesh_transfer_data(SCSIRequest *req, uint32_t len)
{
    MacMeshState *s = req->hba_private;

    if (!s || req != s->current_req) {
        return;
    }

    s->req_in_flight = false;
    s->scsi_buf = scsi_req_get_buf(req);
    s->scsi_buf_len = len;
    s->scsi_buf_pos = 0;
    s->in_data_phase = true;

    if (req->cmd.mode == SCSI_XFER_TO_DEV) {
        mesh_set_phase(s, MESH_PHASE_DATA_OUT, true);
    } else {
        mesh_set_phase(s, MESH_PHASE_DATA_IN, true);
    }

    if (s->in_xfer) {
        return;
    }

    if ((s->cur_cmd & 0x0F) == MESH_CMD_COMMAND) {
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
    } else if (s->dma_active) {
        mesh_try_dma_xfer(s);
    } else {
        mesh_pump_pio_datain(s);
    }
}

static void mesh_command_complete(SCSIRequest *req, size_t resid)
{
    MacMeshState *s = req->hba_private;

    if (!s || req != s->current_req) {
        return;
    }

    s->req_in_flight = false;
    s->status_byte = req->status;
    s->req_completed = true;
    s->scsi_buf = NULL;
    s->scsi_buf_len = 0;
    s->scsi_buf_pos = 0;
    s->current_req = NULL;
    req->hba_private = NULL;
    scsi_req_unref(req);

    if (s->in_xfer) {
        return;
    }

    if (s->dma_active) {
        if (s->pending_io) {
            DBDMA_io *io = s->pending_io;
            s->pending_io = NULL;
            io->dma_end(io);
        }
        mesh_finish_data_cmd(s);
        return;
    }

    if ((s->cur_cmd & 0x0F) == MESH_CMD_DATA_IN && s->fifo_len > 0) {
        return;
    }

    if ((s->cur_cmd & 0x0F) == MESH_CMD_DATA_IN ||
        (s->cur_cmd & 0x0F) == MESH_CMD_DATA_OUT) {
        if (s->fifo_len == 0) {
            mesh_finish_data_cmd(s);
        }
        return;
    }

    mesh_set_phase(s, MESH_PHASE_STATUS, true);
    if ((s->cur_cmd & 0x0F) == MESH_CMD_COMMAND) {
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
    }
}

static void mesh_request_cancelled(SCSIRequest *req)
{
    MacMeshState *s = req->hba_private;
    if (s && s->current_req == req) {
        s->req_in_flight = false;
        s->current_req = NULL;
        req->hba_private = NULL;
    }
}

static const SCSIBusInfo mesh_scsi_info = {
    .tcq = false,
    .max_target = 7,
    .max_lun = 7,
    .transfer_data = mesh_transfer_data,
    .complete = mesh_command_complete,
    .cancel = mesh_request_cancelled,
};

static void mesh_process_command_fifo(MacMeshState *s)
{
    while (s->fifo_len > 0 && s->cdb_len < sizeof(s->cdb)) {
        s->cdb[s->cdb_len++] = mesh_fifo_pop(s);
        if (s->xfer_count > 0) {
            s->xfer_count--;
        }
    }

    if ((s->cdb_expected > 0 && s->cdb_len >= s->cdb_expected) ||
        (s->cdb_expected == 0 && s->cdb_len > 0)) {
        mesh_start_cdb(s);
    }
}

static void mesh_process_msg_out_fifo(MacMeshState *s)
{
    while (s->fifo_len > 0 && s->msg_out_len < sizeof(s->msg_out)) {
        s->msg_out[s->msg_out_len++] = mesh_fifo_pop(s);
        if (s->xfer_count > 0) {
            s->xfer_count--;
        }
    }

    if (s->xfer_count == 0 && s->msg_out_len > 0) {
        uint8_t msg = s->msg_out[0];
        if (msg & 0x80) {
            s->lun = msg & 0x07;
        }
        s->bus_stat0 &= ~MESH_BS0_ATN;
        mesh_set_phase(s, MESH_PHASE_COMMAND, true);
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
    }
}

static void mesh_perform_command(MacMeshState *s, uint8_t cmd)
{
    uint8_t op = cmd & 0x0F;

    s->cur_cmd = cmd;
    s->int_stat &= ~MESH_INT_CMD_DONE;
    s->dma_active = !!(cmd & MESH_CMD_DMA);

    switch (op) {
    case MESH_CMD_NOP:
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_ARBITRATE:
        s->exception &= ~MESH_EXC_ARB_LOST;
        s->bus_stat1 |= MESH_BS1_SEL;
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_SELECT:
        s->exception &= ~(MESH_EXC_SEL_TIMEOUT | MESH_EXC_PHASE_MM);
        s->current_dev = scsi_device_find(&s->bus, 0, s->dst_id, 0);
        if (!s->current_dev) {
            s->bus_stat1 &= ~(MESH_BS1_BSY | MESH_BS1_SEL);
            s->bus_stat0 = 0;
            s->exception |= MESH_EXC_SEL_TIMEOUT;
            s->int_stat |= MESH_INT_EXCEPTION | MESH_INT_CMD_DONE;
            mesh_update_irq(s);
            break;
        }
        s->bus_stat1 &= ~MESH_BS1_SEL;
        s->bus_stat1 |= MESH_BS1_BSY;
        s->cdb_len = 0;
        s->msg_out_len = 0;
        s->lun = 0;
        if (cmd & MESH_CMD_ATN) {
            s->bus_stat0 |= MESH_BS0_ATN;
            mesh_set_phase(s, MESH_PHASE_MESSAGE_OUT, true);
        } else {
            s->bus_stat0 &= ~MESH_BS0_ATN;
            mesh_set_phase(s, MESH_PHASE_COMMAND, true);
        }
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_MESSAGE_OUT:
        s->exception &= ~MESH_EXC_PHASE_MM;
        mesh_process_msg_out_fifo(s);
        break;

    case MESH_CMD_COMMAND:
        s->exception &= ~MESH_EXC_PHASE_MM;
        s->cdb_expected = s->xfer_count ? (s->cdb_len + s->xfer_count) : s->cdb_len;
        if (s->fifo_len > 0 || s->xfer_count == 0) {
            mesh_process_command_fifo(s);
        }
        break;

    case MESH_CMD_DATA_IN:
        s->exception &= ~MESH_EXC_PHASE_MM;
        s->data_remaining = s->xfer_count ? s->xfer_count : 65536;
        if (!s->in_data_phase || (s->bus_stat0 & MESH_BS0_PHASE_MASK) != MESH_PHASE_DATA_IN) {
            if (s->req_completed && s->scsi_buf_pos >= s->scsi_buf_len) {
                s->dma_active = false;
                mesh_set_phase(s, MESH_PHASE_STATUS, true);
                s->exception |= MESH_EXC_PHASE_MM;
                s->int_stat |= MESH_INT_EXCEPTION | MESH_INT_CMD_DONE;
                mesh_update_irq(s);
                break;
            }
        }
        if (s->dma_active) {
            mesh_try_dma_xfer(s);
        } else {
            mesh_pump_pio_datain(s);
        }
        break;

    case MESH_CMD_DATA_OUT:
        s->exception &= ~MESH_EXC_PHASE_MM;
        s->data_remaining = s->xfer_count ? s->xfer_count : 65536;
        if (!s->in_data_phase || (s->bus_stat0 & MESH_BS0_PHASE_MASK) != MESH_PHASE_DATA_OUT) {
            if (s->req_completed && s->scsi_buf_pos >= s->scsi_buf_len) {
                s->dma_active = false;
                mesh_set_phase(s, MESH_PHASE_STATUS, true);
                s->exception |= MESH_EXC_PHASE_MM;
                s->int_stat |= MESH_INT_EXCEPTION | MESH_INT_CMD_DONE;
                mesh_update_irq(s);
                break;
            }
        }
        if (s->dma_active) {
            mesh_try_dma_xfer(s);
        } else if (s->fifo_len > 0 && s->in_data_phase && s->scsi_buf) {
            s->in_xfer = true;
            while (s->fifo_len > 0 && s->data_remaining > 0 &&
                   s->scsi_buf_pos < s->scsi_buf_len) {
                s->scsi_buf[s->scsi_buf_pos++] = mesh_fifo_pop(s);
                s->data_remaining--;
                if (s->xfer_count > 0) {
                    s->xfer_count--;
                }
            }
            if (s->scsi_buf_pos >= s->scsi_buf_len && !s->req_completed &&
                s->current_req && !s->req_in_flight) {
                s->req_in_flight = true;
                scsi_req_continue(s->current_req);
            }
            s->in_xfer = false;
            if (s->data_remaining == 0 || s->req_completed) {
                mesh_finish_data_cmd(s);
            }
        }
        break;

    case MESH_CMD_STATUS:
        s->exception &= ~MESH_EXC_PHASE_MM;
        s->in_data_phase = false;
        mesh_fifo_flush(s);
        mesh_fifo_push(s, s->status_byte);
        if (s->xfer_count > 0) {
            s->xfer_count--;
        }
        mesh_set_phase(s, MESH_PHASE_MESSAGE_IN, false);
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_MESSAGE_IN:
        s->exception &= ~MESH_EXC_PHASE_MM;
        mesh_fifo_flush(s);
        mesh_fifo_push(s, 0x00); /* COMMAND COMPLETE */
        if (s->xfer_count > 0) {
            s->xfer_count--;
        }
        s->bus_stat0 |= MESH_BS0_ACK;
        s->bus_stat0 &= ~MESH_BS0_REQ;
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_BUS_FREE:
        s->exception &= ~MESH_EXC_PHASE_MM;
        if (!(s->bus_stat0 & MESH_BS0_ATN)) {
            s->bus_stat0 &= ~MESH_BS0_ACK;
        }
        if (s->bus_stat0 & MESH_BS0_ATN) {
            /* Initiator rejected last message; target remains connected */
            s->bus_stat0 &= ~MESH_BS0_ATN;
        } else if ((s->bus_stat0 & MESH_BS0_PHASE_MASK) == MESH_PHASE_MESSAGE_IN) {
            /* Message accepted: complete command and release the SCSI bus */
            mesh_release_request(s);
            s->bus_stat1 &= ~(MESH_BS1_BSY | MESH_BS1_SEL);
            s->bus_stat0 = 0;
        }
        if (s->bus_stat1 & MESH_BS1_BSY) {
            s->exception |= MESH_EXC_PHASE_MM;
            s->int_stat |= MESH_INT_EXCEPTION;
        } else {
            s->int_stat |= MESH_INT_CMD_DONE;
        }
        mesh_update_irq(s);
        break;

    case MESH_CMD_ENA_PARITY:
        s->check_parity = true;
        break;

    case MESH_CMD_DIS_PARITY:
        s->check_parity = false;
        break;

    case MESH_CMD_ENA_RESEL:
    case MESH_CMD_DIS_RESEL:
        s->int_stat |= MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_RESET_MESH:
        mesh_bus_reset(s);
        s->exception = 0;
        s->error = 0;
        s->int_mask = 0;
        s->int_stat = MESH_INT_CMD_DONE;
        mesh_update_irq(s);
        break;

    case MESH_CMD_FLUSH_FIFO:
        mesh_fifo_flush(s);
        break;
    }
}

static uint64_t mac_mesh_read(void *opaque, hwaddr addr, unsigned size)
{
    MacMeshState *s = MAC_MESH(opaque);
    uint8_t reg = (addr >> 4) & 0x0F;
    uint8_t val = 0;

    switch (reg) {
    case MESH_REG_XFER_COUNT0:
        val = s->xfer_count & 0xFF;
        break;
    case MESH_REG_XFER_COUNT1:
        val = (s->xfer_count >> 8) & 0xFF;
        break;
    case MESH_REG_FIFO:
        val = mesh_fifo_pop(s);
        if ((s->cur_cmd & 0x0F) == MESH_CMD_DATA_IN && !s->dma_active) {
            mesh_pump_pio_datain(s);
        }
        break;
    case MESH_REG_SEQUENCE:
        val = s->cur_cmd;
        break;
    case MESH_REG_BUS_STAT0:
        val = s->bus_stat0;
        break;
    case MESH_REG_BUS_STAT1:
        val = s->bus_stat1;
        break;
    case MESH_REG_FIFO_COUNT:
        val = s->fifo_len;
        break;
    case MESH_REG_EXCEPTION:
        val = s->exception;
        break;
    case MESH_REG_ERROR:
        val = s->error;
        break;
    case MESH_REG_INT_MASK:
        val = s->int_mask;
        break;
    case MESH_REG_INTERRUPT:
        val = s->int_stat;
        break;
    case MESH_REG_SOURCE_ID:
        val = s->src_id;
        break;
    case MESH_REG_DEST_ID:
        val = s->dst_id;
        break;
    case MESH_REG_SYNC_PARAMS:
        val = s->sync_params;
        break;
    case MESH_REG_MESH_ID:
        val = MESH_TNT_ID;
        break;
    case MESH_REG_SEL_TIMEOUT:
        val = s->sel_timeout;
        break;
    }

    return val;
}

static void mac_mesh_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    MacMeshState *s = MAC_MESH(opaque);
    uint8_t reg = (addr >> 4) & 0x0F;
    uint8_t val = val64 & 0xFF;

    switch (reg) {
    case MESH_REG_XFER_COUNT0:
        s->xfer_count = (s->xfer_count & 0xFF00U) | val;
        break;
    case MESH_REG_XFER_COUNT1:
        s->xfer_count = (s->xfer_count & 0x00FFU) | ((uint16_t)val << 8);
        break;
    case MESH_REG_FIFO:
        mesh_fifo_push(s, val);
        if ((s->cur_cmd & 0x0F) == MESH_CMD_COMMAND) {
            mesh_process_command_fifo(s);
        } else if ((s->cur_cmd & 0x0F) == MESH_CMD_MESSAGE_OUT) {
            mesh_process_msg_out_fifo(s);
        } else if ((s->cur_cmd & 0x0F) == MESH_CMD_DATA_OUT && !s->dma_active) {
            if (s->in_data_phase && s->scsi_buf && s->scsi_buf_pos < s->scsi_buf_len) {
                s->in_xfer = true;
                s->scsi_buf[s->scsi_buf_pos++] = mesh_fifo_pop(s);
                if (s->data_remaining > 0) {
                    s->data_remaining--;
                }
                if (s->xfer_count > 0) {
                    s->xfer_count--;
                }
                if (s->scsi_buf_pos >= s->scsi_buf_len && !s->req_completed &&
                    s->current_req && !s->req_in_flight) {
                    s->req_in_flight = true;
                    scsi_req_continue(s->current_req);
                }
                s->in_xfer = false;
                if (s->data_remaining == 0 || s->req_completed) {
                    mesh_finish_data_cmd(s);
                }
            }
        }
        break;
    case MESH_REG_SEQUENCE:
        mesh_perform_command(s, val);
        break;
    case MESH_REG_BUS_STAT0:
        s->bus_stat0 = val;
        break;
    case MESH_REG_BUS_STAT1:
        if ((val & MESH_BS1_RST) && !(s->bus_stat1 & MESH_BS1_RST)) {
            s->bus_stat1 |= MESH_BS1_RST;
            mesh_bus_reset(s);
            bus_cold_reset(BUS(&s->bus));
        } else if (!(val & MESH_BS1_RST)) {
            s->bus_stat1 = (val & ~MESH_BS1_RST);
        }
        break;
    case MESH_REG_INT_MASK:
        s->int_mask = val & MESH_INT_ALL;
        mesh_update_irq(s);
        break;
    case MESH_REG_INTERRUPT:
        s->int_stat &= ~(val & MESH_INT_ALL);
        mesh_update_irq(s);
        break;
    case MESH_REG_SOURCE_ID:
        s->src_id = val & 0x07;
        break;
    case MESH_REG_DEST_ID:
        s->dst_id = val & 0x07;
        break;
    case MESH_REG_SYNC_PARAMS:
        s->sync_params = val;
        break;
    case MESH_REG_SEL_TIMEOUT:
        s->sel_timeout = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps mac_mesh_ops = {
    .read = mac_mesh_read,
    .write = mac_mesh_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void mac_mesh_dma_rw(DBDMA_io *io)
{
    MacMeshState *s = MAC_MESH(io->opaque);

    if (s->pending_io && s->pending_io != io) {
        s->pending_io->dma_end(s->pending_io);
    }
    s->pending_io = io;

    if (s->dma_active && s->in_data_phase) {
        mesh_try_dma_xfer(s);
    }
}

static void mac_mesh_dma_flush(DBDMA_io *io)
{
    MacMeshState *s = MAC_MESH(io->opaque);
    if (s->dma_active && s->in_data_phase) {
        mesh_try_dma_xfer(s);
    }
}

void mac_mesh_register_dma(MacMeshState *s, void *dbdma, int channel)
{
    s->dbdma = dbdma;
    DBDMA_register_channel(dbdma, channel, s->dma_irq,
                           mac_mesh_dma_rw, mac_mesh_dma_flush, s);
}

static void mac_mesh_reset(DeviceState *dev)
{
    MacMeshState *s = MAC_MESH(dev);

    mesh_bus_reset(s);
    s->cur_cmd = 0;
    s->xfer_count = 0;
    s->bus_stat0 = 0;
    s->bus_stat1 = 0;
    s->exception = 0;
    s->error = 0;
    s->int_mask = 0;
    s->int_stat = 0;
    s->src_id = 7;
    s->dst_id = 0;
    s->sync_params = 2;
    s->sel_timeout = 0;
    s->check_parity = true;
    mesh_update_irq(s);
}

static void mac_mesh_realize(DeviceState *dev, Error **errp)
{
    MacMeshState *s = MAC_MESH(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->dma_irq);
    memory_region_init_io(&s->mem, OBJECT(dev), &mac_mesh_ops, s,
                          "mac-mesh", 0x1000);
    sysbus_init_mmio(sbd, &s->mem);

    scsi_bus_init_named(&s->bus, sizeof(s->bus), dev,
                        &mesh_scsi_info, "mesh-scsi.0");
}

static const VMStateDescription vmstate_mac_mesh = {
    .name = "mac-mesh",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(xfer_count, MacMeshState),
        VMSTATE_UINT8(cur_cmd, MacMeshState),
        VMSTATE_UINT8(bus_stat0, MacMeshState),
        VMSTATE_UINT8(bus_stat1, MacMeshState),
        VMSTATE_UINT8(exception, MacMeshState),
        VMSTATE_UINT8(int_mask, MacMeshState),
        VMSTATE_UINT8(int_stat, MacMeshState),
        VMSTATE_UINT8(src_id, MacMeshState),
        VMSTATE_UINT8(dst_id, MacMeshState),
        VMSTATE_UINT8_ARRAY(fifo, MacMeshState, MESH_FIFO_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void mac_mesh_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mac_mesh_realize;
    device_class_set_legacy_reset(dc, mac_mesh_reset);
    dc->vmsd = &vmstate_mac_mesh;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo mac_mesh_info = {
    .name = TYPE_MAC_MESH,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MacMeshState),
    .class_init = mac_mesh_class_init,
};

static void mac_mesh_register_types(void)
{
    type_register_static(&mac_mesh_info);
}

type_init(mac_mesh_register_types)
