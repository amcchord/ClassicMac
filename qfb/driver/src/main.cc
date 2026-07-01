#include "qfb_driver.hh"

pascal void SwapMMUMode(Byte *bp){
    register Byte _bp __asm__("%d0");_bp = *bp;
    __asm__ volatile(".short 0xA05D"
        : "=r"(_bp)
        : "r"(_bp)                                                                      : "%d1", "%d2", "%a0", "%a1");
*bp = _bp;
}

int qfb_drvr_open(ParmBlkPtr params, DCtlPtr dce, uint32_t slot) {
  (void)params;
  dce = (DCtlPtr)StripAddress((Ptr)dce);
  Byte mode = 1;
  SwapMMUMode(&mode);
  dprintf("--- mac_qfb_driver opened! ---\n");
  dprintf("Allocating memory.\n");
  /* Allocate memory for our Locals */
  ReserveMemSys(sizeof(Locals)+sizeof(SlotIntQElement));
  Handle local_handle = NewHandleSysClear(sizeof(Locals));
  if(!local_handle) {
    SwapMMUMode(&mode);
    return MemError();
  }
#ifdef SUPPORT_AUX
  dprintf("Locking handle.\n");
  HLock(local_handle);
#else
  dprintf("Moving handle high.\n");
  MoveHHi(local_handle);
#endif
  dce->dCtlStorage = local_handle;
  HLocker<Locals> locals(local_handle);
  locals->qfb = reinterpret_cast<volatile QFB*>(0xF0000000 | (slot<<24));
  locals->vram = reinterpret_cast<uint8_t*>(slot << 28);
  locals->slot = slot;
  dprintf("VRAM: %p\tRegs: %p\nSlot: %X\n", locals->vram,
          locals->qfb, locals->slot);
  if(locals->qfb->version != 'qfb1') {
    DebugStr("\pWrong QFB version");
    locals.unlock();
    DisposeHandle(local_handle);
    SwapMMUMode(&mode);
    return openErr;
  }
  dprintf("Allocating Slot Interrupt Queue element.\n");
  locals->slot_queue_element = reinterpret_cast<SlotIntQElement*>(NewPtrSysClear(sizeof(SlotIntQElement)));
  if(locals->slot_queue_element == nullptr) {
    int ret = MemError();
    locals.unlock();
    DisposeHandle(local_handle);
    SwapMMUMode(&mode);
    return ret;
  }
  dprintf("Initializing Slot Interrupt Queue element.\n");
  locals->slot_queue_element->sqType = 6; // sIQType
  locals->slot_queue_element->sqAddr = qfb_interrupt_service_routine;
  locals->slot_queue_element->sqParm = reinterpret_cast<uint32_t>(locals->qfb);
  dprintf("Installing gray palette.\n");
  qfb_gray_clut(locals);
  /* Build the list of resolutions we advertise to the Display Manager. */
  qfb_init_resolutions(*locals);
  locals->cur_resolution = QFB_USER_MODE_ID;
  dprintf("Advertising %u resolutions.\n", locals->num_resolutions);
  dprintf("Setting mode: %u x %u x %u\n",
          locals->qfb->user_width, locals->qfb->user_height,
          locals->qfb->user_depth);
  /* set up user-specified width and height, but 1-bpp, because that's the mode
     A/UX and MacOS expect to be active on open */
  locals->qfb->width = locals->qfb->user_width;
  locals->qfb->height = locals->qfb->user_height;
  switch(locals->qfb->user_depth) {
  case 1:
    locals->cur_mode = ONE_BIT_MODE;
    locals->qfb->depth = 1;
    break;
  case 2:
    locals->cur_mode = TWO_BIT_MODE;
    locals->qfb->depth = 2;
    break;
  case 4:
    locals->cur_mode = FOUR_BIT_MODE;
    locals->qfb->depth = 4;
    break;
  case 8:
    locals->cur_mode = EIGHT_BIT_MODE;
    locals->qfb->depth = 8;
    break;
  case 16:
    locals->cur_mode = SIXTEEN_BIT_MODE;
    locals->qfb->depth = 16;
    break;
  case 24: case 32:
    locals->cur_mode = THIRTY_TWO_BIT_MODE;
    locals->qfb->depth = 32;
    break;
  }
  locals->qfb->base = QFB_VRAM_SLOT_BASE;
  dprintf("Splatting gray pattern.\n");
  qfb_gray_pixels(locals, 0);
  locals.unlock();
  /* Ask the Device Manager for periodic time (accRun) so we can poll the
     host-resize request published by QEMU. dNeedTime is set in the ROM driver
     flags; dCtlDelay is the period in ticks (~6 ticks is about 1/10 second). */
  dce->dCtlDelay = 6;
  auto ret = qfb_enable_interrupts(dce);
  dprintf("Open complete!\n");
  SwapMMUMode(&mode);
  return ret;
}

int qfb_drvr_close(ParmBlkPtr params, DCtlPtr dce) {
  (void)params;
  dprintf("Driver closing.\n");
  Byte mode = 1;
  SwapMMUMode(&mode);
  dce = (DCtlPtr)StripAddress((Ptr)dce);
  if(dce->dCtlStorage) {
    HLocker<Locals> locals(dce->dCtlStorage);
    if(locals->slot_queue_element) {
      if(locals->irq_enabled) {
        dprintf("Driver closed while interrupts still enabled.\n");
        locals->irq_enabled = false;
        locals->qfb->irq_mask = 0;
        SIntRemove(locals->slot_queue_element, locals->slot);
      }
      DisposePtr(reinterpret_cast<Ptr>(locals->slot_queue_element));
      locals->slot_queue_element = nullptr;
    }
    locals.unlock();
    DisposeHandle(dce->dCtlStorage);
    dce->dCtlStorage = nullptr;
  }
  SwapMMUMode(&mode);
  return noErr;
}

void _putchar(char c) {
  uint32_t ch = static_cast<uint32_t>(static_cast<unsigned char>(c));
  if(ch > 0 && ch <= 255) {
    Byte mode = 1;
    SwapMMUMode(&mode);
    *reinterpret_cast<volatile uint32_t*>(0xFC00003C) = ch;
    SwapMMUMode(&mode);
  }
}

uint32_t qfb_calculate_stride(uint32_t width, uint32_t depth) {
  /* this must mirror the calculation in mac_qfb.c */
  if(depth == 24) depth = 32;
  return ((width * depth + 31) / 8) & ~(uint32_t)3;
}

uint16_t qfb_calculate_num_pages(uint32_t width, uint32_t height, uint32_t depth) {
  uint32_t rowbytes = qfb_calculate_stride(width, depth);
  uint32_t modesize = rowbytes * height;
  uint32_t num_pages = (QFB_VRAM_SIZE - QFB_VRAM_SLOT_BASE) / modesize;
  if(num_pages > 32000) return 32000;
  else return num_pages;
}

/* Standard Macintosh-friendly resolutions offered in the Monitors control
   panel, in addition to the user's configured boot resolution. Entries that
   exceed the hardware limits or duplicate the boot resolution are skipped when
   the list is built. The id field is assigned dynamically in
   qfb_init_resolutions. */
static const QfbResolution kQfbStdResolutions[] = {
  { 0,  512,  384 },
  { 0,  640,  480 },
  { 0,  800,  600 },
  { 0,  832,  624 },
  { 0, 1024,  768 },
  { 0, 1152,  870 },
  { 0, 1280,  800 },
  { 0, 1280, 1024 },
  { 0, 1440,  900 },
  { 0, 1600, 1000 },
  { 0, 1680, 1050 },
  { 0, 1920, 1080 },
  { 0, 1920, 1200 },
};

void qfb_init_resolutions(Locals* locals) {
  uint8_t n = 0;
  uint32_t uw = locals->qfb->user_width;
  uint32_t uh = locals->qfb->user_height;
  /* Two dynamic entries come first. QFB_USER_MODE_ID holds the boot/user
     resolution (and is reported as the default mode); QFB_USER_MODE_ID_ALT
     starts as a copy of it. Host-driven resizes rewrite one of these two to an
     arbitrary size and switch to it, alternating between the two IDs so the
     Display Manager always relayouts. */
  locals->resolutions[n].id = QFB_USER_MODE_ID;
  locals->resolutions[n].width = uw;
  locals->resolutions[n].height = uh;
  n++;
  locals->resolutions[n].id = QFB_USER_MODE_ID_ALT;
  locals->resolutions[n].width = uw;
  locals->resolutions[n].height = uh;
  n++;
  uint32_t next_id = QFB_USER_MODE_ID_ALT + 1;
  uint32_t count = sizeof(kQfbStdResolutions) / sizeof(kQfbStdResolutions[0]);
  for(uint32_t i = 0; i < count; i++) {
    uint32_t w = kQfbStdResolutions[i].width;
    uint32_t h = kQfbStdResolutions[i].height;
    if(w > QFB_DEV_MAX_WIDTH || h > QFB_DEV_MAX_HEIGHT) continue;
    if(w == uw && h == uh) continue; /* avoid a duplicate of the boot mode */
    if(n >= QFB_MAX_RESOLUTIONS) break;
    locals->resolutions[n].id = next_id;
    locals->resolutions[n].width = w;
    locals->resolutions[n].height = h;
    next_id++;
    n++;
  }
  locals->num_resolutions = n;
}

int qfb_find_resolution_index(Locals* locals, uint32_t id) {
  for(uint8_t i = 0; i < locals->num_resolutions; i++) {
    if(locals->resolutions[i].id == id) return i;
  }
  return -1;
}

const QfbResolution* qfb_resolution_for_id(Locals* locals, uint32_t id) {
  int idx = qfb_find_resolution_index(locals, id);
  if(idx < 0) return nullptr;
  return &locals->resolutions[idx];
}

/* Returns the DisplayModeID of the advertised resolution whose dimensions are
   closest to the requested width/height (Manhattan distance). Used by the
   host-driven resize path to snap an arbitrary window size onto a real mode. */
uint32_t qfb_nearest_resolution_id(Locals* locals, uint32_t width,
                                   uint32_t height) {
  uint32_t best_id = locals->resolutions[0].id;
  uint32_t best_cost = 0xFFFFFFFF;
  for(uint8_t i = 0; i < locals->num_resolutions; i++) {
    uint32_t w = locals->resolutions[i].width;
    uint32_t h = locals->resolutions[i].height;
    uint32_t dw;
    uint32_t dh;
    if(w > width) dw = w - width;
    else dw = width - w;
    if(h > height) dh = h - height;
    else dh = height - h;
    uint32_t cost = dw + dh;
    if(cost < best_cost) {
      best_cost = cost;
      best_id = locals->resolutions[i].id;
    }
  }
  return best_id;
}

/* Returns the DisplayModeID of a *standard* advertised resolution (i.e. not one
   of the two dynamic IDs) whose dimensions exactly match the request, or 0 if
   none. A host resize that lands exactly on a standard size selects that stable
   mode instead of a dynamic one, keeping the Monitors list tidy. */
uint32_t qfb_exact_standard_id(Locals* locals, uint32_t width,
                               uint32_t height) {
  for(uint8_t i = 0; i < locals->num_resolutions; i++) {
    uint32_t id = locals->resolutions[i].id;
    if(id == QFB_USER_MODE_ID || id == QFB_USER_MODE_ID_ALT) continue;
    if(locals->resolutions[i].width == width
       && locals->resolutions[i].height == height) {
      return id;
    }
  }
  return 0;
}

/* Picks the dynamic DisplayModeID to use for the next arbitrary resize: the one
   of the two that is not currently active, so the Display Manager always sees a
   genuinely different mode. If neither dynamic mode is current (we're on a
   standard mode), start with QFB_USER_MODE_ID. */
uint32_t qfb_pick_dynamic_id(Locals* locals) {
  if(locals->cur_resolution == QFB_USER_MODE_ID) {
    return QFB_USER_MODE_ID_ALT;
  }
  return QFB_USER_MODE_ID;
}

/* Rewrites the geometry advertised for a given DisplayModeID. Used to retarget a
   dynamic mode to an arbitrary requested size before switching to it;
   qfb_switch_mode and qfb_get_video_parameters then read the new geometry. */
void qfb_set_resolution_geometry(Locals* locals, uint32_t id,
                                 uint32_t width, uint32_t height) {
  int idx = qfb_find_resolution_index(locals, id);
  if(idx < 0) return;
  locals->resolutions[idx].width = width;
  locals->resolutions[idx].height = height;
}

int qfb_depth_for_mode(uint16_t mode) {
  switch(mode) {
  case ONE_BIT_MODE: return 1;
  case TWO_BIT_MODE: return 2;
  case FOUR_BIT_MODE: return 4;
  case EIGHT_BIT_MODE: return 8;
  case SIXTEEN_BIT_MODE: return 16;
  case THIRTY_TWO_BIT_MODE: return 24;
  default: return 0;
  }
}

int qfb_enable_interrupts(DCtlPtr dce) {
  dprintf("Enabling vertical blank interrupt.\n");
  HLocker<Locals> locals(dce->dCtlStorage);
  Byte mode = 1;
  if(locals->irq_enabled) {
    dprintf("(but it was already enabled!)\n");
    return noErr; /* nothing to do */
  }
  SwapMMUMode(&mode);
  SIntInstall(locals->slot_queue_element, locals->slot);
  locals->qfb->irq_mask = QFB_IRQ_VBL;
  locals->irq_enabled = true;
  SwapMMUMode(&mode);
  return noErr;
}

int qfb_disable_interrupts(DCtlPtr dce) {
  dprintf("Disabling vertical blank interrupt.\n");
  HLocker<Locals> locals(dce->dCtlStorage);
  Byte mode = 1;
  if(!locals->irq_enabled) {
    dprintf("(but it was already disabled!)\n");
    return noErr; /* nothing to do */
  }
  SwapMMUMode(&mode);
  locals->irq_enabled = false;
  locals->qfb->irq_mask = 0;
  SIntRemove(locals->slot_queue_element, locals->slot);
  SwapMMUMode(&mode);
  return noErr;
}

void mystrcpy(char* dstp, const char* srcp) {
  while((*dstp++ = *srcp++)) {}
}

void mymemcpy(void* dst, const void* src, size_t len) {
  uint8_t* dstp = reinterpret_cast<uint8_t*>(dst);
  const uint8_t* srcp = reinterpret_cast<const uint8_t*>(src);
  while(len-- > 0) *dstp++ = *srcp++;
}
