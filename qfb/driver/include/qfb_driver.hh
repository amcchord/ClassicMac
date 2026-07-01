#ifndef QFB_DRIVER_HH
#define QFB_DRIVER_HH

#include <Multiverse.h>
#include "extraverse.h"
#ifdef DEBUG_QFB
#include "printf.h"
#endif

#include "HLocker.hh"

struct QFB {
  /* Reading this returns 'qfb1', writing it resets the device. */
  uint32_t version;
  /* Current width, height, depth, and base */
  uint32_t width, height, depth, base;
  /* Rowbytes for the current mode (R/O) */
  uint32_t rowbytes;
  /* Unused register, reserved */
  uint32_t reserved;
  /* Palette index and RW port */
  uint32_t pal_index, pal_color;
  /* Gamma LUT index and RW port */
  uint32_t lut_index, lut_color;
  /* IRQ mask, list of interrupts that are ENABLED */
  uint32_t irq_mask;
  /* IRQ status. Reading returns outstanding IRQs. Write an IRQ to ack it. */
  uint32_t irq;
  /* Width, height, and depth specified on the QEMU command line. (R/O) */
  uint32_t user_width, user_height, user_depth;
  /* Host-requested resolution from window resizing. host_req_serial is bumped
     by QEMU on each new request; the driver polls it and switches to the mode
     nearest host_req_width x host_req_height. (R/O) */
  uint32_t host_req_width, host_req_height, host_req_serial;
  /* Writing here makes QEMU re-patch the declaration ROM's video parameter
     blocks to the current width/height/depth. Write after changing the mode,
     then call SUpdateSRT so the OS rebuilds the screen GDevice. (W) */
  uint32_t repatch;
};
#define QFB_IRQ_VBL 1

/* A single resolution advertised to the Display Manager. The color depth is a
   separate axis (see the *_BIT_MODE constants below), so each resolution can be
   used at any supported depth. */
struct QfbResolution {
  uint32_t id;     /* DisplayModeID reported to the Display Manager */
  uint16_t width;
  uint16_t height;
};

/* The boot/user resolution (taken from the QEMU command line) is always
   advertised under this DisplayModeID and is reported as the default mode. */
#define QFB_USER_MODE_ID 128
/* A second dynamic DisplayModeID paired with QFB_USER_MODE_ID for arbitrary
   host-driven resizes. Successive drag-resizes alternate between the two IDs so
   the Display Manager always sees a genuinely different mode and re-lays-out the
   screen (it may short-circuit a "switch" to the mode it thinks is already
   current). Both entries are rewritten to the exact requested geometry on the
   fly; the standard advertised list starts after these two. */
#define QFB_USER_MODE_ID_ALT 129
/* sResource ID of the video sResource in the declaration ROM (see decl_rom.s).
   Used to tell the Slot Manager (SUpdateSRT) which sResource changed. */
#define QFB_VIDEO_SRSRC_ID 128
/* Upper bound on advertised resolutions (2 dynamic modes + standard list). */
#define QFB_MAX_RESOLUTIONS 24
/* Hardware limits, mirroring QFB_MAX_WIDTH/HEIGHT in mac_qfb.c. */
#define QFB_DEV_MAX_WIDTH 3840
#define QFB_DEV_MAX_HEIGHT 2160
/* Smallest resolution we will switch to (matches the Cocoa window min size). */
#define QFB_DEV_MIN_WIDTH 512
#define QFB_DEV_MIN_HEIGHT 384
/* accRun ticks a host-resize request must stay unchanged before we apply it, so
   we switch once per resize instead of storming the Display Manager mid-drag.
   dCtlDelay is ~6 ticks (~0.1s), so this is roughly a fifth of a second. */
#define QFB_RESIZE_DEBOUNCE_TICKS 2

struct Locals {
  volatile QFB* qfb;
  uint8_t* vram;
  SlotIntQElement* slot_queue_element;
  uint8_t cur_mode, slot;
  bool gray_mode_enabled;
  bool irq_enabled;
  /* Resolution support. cur_resolution is the active DisplayModeID; the
     resolutions array is built once at driver open and never changes, so the
     Display Manager can safely cache the list it enumerates. */
  uint32_t cur_resolution;
  uint8_t num_resolutions;
  QfbResolution resolutions[QFB_MAX_RESOLUTIONS];
  /* Last host-resize request serial we acted on (see qfb_periodic). */
  uint32_t last_req_serial;
  /* Debounce state for host-resize requests: pending_req_serial is the serial
     we are currently waiting to settle, pending_req_ticks counts how many
     accRun ticks it has been stable. See qfb_periodic. */
  uint32_t pending_req_serial;
  uint8_t pending_req_ticks;
};

extern "C" { // so MacsBug symbols are valid

int qfb_drvr_open(ParmBlkPtr params, DCtlPtr dce, uint32_t slot);
int qfb_drvr_close(ParmBlkPtr params, DCtlPtr dce);
int qfb_drvr_control(ParmBlkPtr params, DCtlPtr dce);
int qfb_drvr_status(ParmBlkPtr params, DCtlPtr dce);
void qfb_gray_clut(HLocker<Locals>& locals);
void qfb_gray_pixels(HLocker<Locals>& locals, uint32_t page);
int qfb_common_set_entries(CntrlParam* params, HLocker<Locals>& locals);
uint32_t qfb_calculate_stride(uint32_t width, uint32_t depth);
uint16_t qfb_calculate_num_pages(uint32_t width, uint32_t height, uint32_t depth);

/* Resolution table helpers (defined in main.cc). */
void qfb_init_resolutions(Locals* locals);
int qfb_find_resolution_index(Locals* locals, uint32_t id); /* -1 if absent */
const QfbResolution* qfb_resolution_for_id(Locals* locals, uint32_t id);
uint32_t qfb_nearest_resolution_id(Locals* locals, uint32_t width,
                                   uint32_t height);
/* Id of a *standard* (non-dynamic) advertised resolution exactly matching the
   given size, or 0 if none. Lets an exact host resize pick a stable mode. */
uint32_t qfb_exact_standard_id(Locals* locals, uint32_t width,
                               uint32_t height);
/* The dynamic DisplayModeID (QFB_USER_MODE_ID / _ALT) not currently active, so
   a new arbitrary resize always targets a genuinely different mode. */
uint32_t qfb_pick_dynamic_id(Locals* locals);
/* Rewrite the geometry advertised for a given DisplayModeID (used to retarget a
   dynamic mode to an arbitrary requested size before switching to it). */
void qfb_set_resolution_geometry(Locals* locals, uint32_t id,
                                 uint32_t width, uint32_t height);
int qfb_depth_for_mode(uint16_t mode); /* 1/2/4/8/16/24, or 0 if invalid */
int qfb_enable_interrupts(DCtlPtr dce);
int qfb_disable_interrupts(DCtlPtr dce);
short qfb_interrupt_service_routine(uint32_t);
void mystrcpy(char* dst, const char* src);
void mymemcpy(void* dst, const void* src, size_t size);

/* Status routines */
int qfb_get_mode(CntrlParam* params, DCtlPtr dce);
int qfb_get_entries(CntrlParam* params, DCtlPtr dce);
int qfb_get_page_count(CntrlParam* params, DCtlPtr dce);
int qfb_get_page_base(CntrlParam* params, DCtlPtr dce);
int qfb_get_gray(CntrlParam* params, DCtlPtr dce);
int qfb_get_interrupt(CntrlParam* params, DCtlPtr dce);
int qfb_get_gamma(CntrlParam* params, DCtlPtr dce);
int qfb_get_default_mode(CntrlParam* params, DCtlPtr dce);
int qfb_get_connection(CntrlParam* params, DCtlPtr dce);
int qfb_get_video_parameters(CntrlParam* params, DCtlPtr dce);
int qfb_get_next_resolution(CntrlParam* params, DCtlPtr dce);
int qfb_get_mode_timing(CntrlParam* params, DCtlPtr dce);
int qfb_get_gamma_info_list(CntrlParam* params, DCtlPtr dce);
int qfb_retrieve_gamma_table(CntrlParam* params, DCtlPtr dce);

/* Control routines */
int qfb_reset(CntrlParam* params, DCtlPtr dce);
int qfb_kill_io(CntrlParam* params, DCtlPtr dce);
int qfb_set_mode(CntrlParam* params, DCtlPtr dce);
int qfb_switch_mode(CntrlParam* params, DCtlPtr dce);
int qfb_set_entries(CntrlParam* params, DCtlPtr dce);
int qfb_set_gamma(CntrlParam* params, DCtlPtr dce);
int qfb_gray_page(CntrlParam* params, DCtlPtr dce);
int qfb_set_gray(CntrlParam* params, DCtlPtr dce);
int qfb_set_interrupt(CntrlParam* params, DCtlPtr dce);
int qfb_direct_set_entries(CntrlParam* params, DCtlPtr dce);
int qfb_set_default_mode(CntrlParam* params, DCtlPtr dce);
int qfb_periodic(CntrlParam* params, DCtlPtr dce); /* accRun: apply host resize */

}

#ifdef DEBUG_QFB
#define dprintf(format, ...) printf(format ,##__VA_ARGS__)
#else
#define dprintf(...)
#endif

#define ONE_BIT_MODE 0x80
#define TWO_BIT_MODE 0x81
#define FOUR_BIT_MODE 0x82
#define EIGHT_BIT_MODE 0x83
#define SIXTEEN_BIT_MODE 0x84
#define THIRTY_TWO_BIT_MODE 0x85
#define FIRST_VALID_MODE ONE_BIT_MODE
#define LAST_VALID_MODE THIRTY_TWO_BIT_MODE

#define QFB_VRAM_SIZE 0x2000000 /* 32MiB */
/* the lowest address of VRAM accessible in the regular slot space */
#define QFB_VRAM_SLOT_BASE 0x10000

#endif
