#include "qfb_driver.hh"

void qfb_gray_clut(HLocker<Locals>& locals) {
  volatile QFB* qfb = locals->qfb;
  for(uint32_t i = 0; i < 256; ++i) {
    qfb->pal_index = i;
    qfb->pal_color = 0xAAAAAA;
  }
  qfb->depth = 1;
}

void qfb_gray_pixels(HLocker<Locals>& locals, uint32_t page) {
  volatile QFB* qfb = locals->qfb;
  uint32_t a, b;
  switch(qfb->depth){
  default:
  case 1: b = a = 0xAAAAAAAA; break;
  case 2: b = a = 0xCCCCCCCC; break;
  case 4: b = a = 0xF0F0F0F0; break;
  case 8: b = a = 0xFF00FF00; break;
  case 16: b = a = 0xFFFF0000; break;
  case 24:
  case 32: a = 0xFFFFFFFF; b = 0x00000000; break;
  }
  uint32_t* row_pointer = reinterpret_cast<uint32_t*>(locals->vram + QFB_VRAM_SLOT_BASE);
  row_pointer += page * qfb->height * (qfb->rowbytes / 4);
  for(uint32_t y = 0; y < qfb->height; ++y) {
    uint32_t rowbytes_left = qfb->rowbytes;
    uint32_t* p = row_pointer;
    while(rowbytes_left > 0) {
      p[0] = a;
      p[1] = b;
      p += 2;
      rowbytes_left -= 8;
    }
    uint32_t new_a = ~b, new_b = ~a;
    a = new_a; b = new_b;
    row_pointer += (qfb->rowbytes / 4);
  }
  if(qfb->depth <= 8) {
    qfb->pal_index = 0;
    qfb->pal_color = 0xFFFFFFFF;
    qfb->pal_index = (1 << qfb->depth) - 1;
    qfb->pal_color = 0;
  }
}

extern "C" void set_entry(volatile QFB* qfb, ColorSpec* entry, bool gray_mode){
  uint32_t pixel;
  if (gray_mode) {
    uint32_t gray = uint16_t((uint32_t(entry->rgb.red)
                              + uint32_t(entry->rgb.green)
                              + uint32_t(entry->rgb.blue)) >> 8) / 3;
    pixel = (gray << 16) | (gray << 8) | gray;
  }
  else {
    pixel = uint32_t((entry->rgb.red >> 8) << 16)
      | uint32_t((entry->rgb.green >> 8) << 8)
      | uint32_t((entry->rgb.blue >> 8));
  }
  qfb->pal_color = pixel;
}

int qfb_common_set_entries(CntrlParam* params, HLocker<Locals>& locals) {
  VDSetEntryRecord* si
    = *reinterpret_cast<VDSetEntryRecord**>(params->csParam);
  volatile QFB* qfb = locals->qfb;
  ColorSpec *table = (ColorSpec *)StripAddress((Ptr)(si->csTable));
  bool gray_mode = locals->gray_mode_enabled;
  /* note: si->csCount is "zero-based", so <= is correct below */
  if(si->csStart < 0) {
    /* Set entries by their index fields. */
    for(int32_t array_index = 0; array_index <= si->csCount; ++array_index) {
      ColorSpec* entry = table + array_index;
      if(entry->value >= 256 || entry->value < 0)
        continue;
      qfb->pal_index = entry->value;
      set_entry(qfb, entry, gray_mode);
    }
  }
  else {
    /* Set entries starting from the given index. */
    for(int32_t array_index = 0, entry_index = si->csStart;
        array_index <= si->csCount && entry_index < 256;
        ++array_index, ++entry_index) {
      ColorSpec* entry = table + array_index;
      qfb->pal_index = entry_index;
      set_entry(qfb, entry, gray_mode);
    }
  }
  return noErr;
}

/* Reset to default state. (Apparently only used by A/UX.) */
int qfb_reset(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  qfb_gray_clut(locals);
  locals->cur_mode = ONE_BIT_MODE;
  locals->cur_resolution = QFB_USER_MODE_ID;
  si->csMode = ONE_BIT_MODE;
  si->csData = QFB_USER_MODE_ID;
  si->csPage = 0;
  si->csBaseAddr = reinterpret_cast<Ptr>(locals->vram) + QFB_VRAM_SLOT_BASE;
  locals->qfb->width = locals->qfb->user_width;
  locals->qfb->height = locals->qfb->user_height;
  locals->qfb->depth = 1;
  qfb_gray_pixels(locals, 0);
  return noErr;
}

/* Kill any pending IOs. We don't pend IOs, so we have nothing to do here. */
int qfb_kill_io(CntrlParam* params, DCtlPtr dce) {
  (void)params; (void)dce;
  return noErr;
}

/* Change the mode and page. */
int qfb_set_mode(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  dprintf("\x1B[1mSET MODE: %i @ %i\x1B[0m\n", si->csMode,
          si->csPage);
  if(si->csMode < FIRST_VALID_MODE
     || si->csMode > LAST_VALID_MODE) {
    DebugStr("\pset_mode bad mode");
    dprintf("(bad mode)\n");
    return controlErr; /* bad target mode */
  }
  int target_depth;
  switch(si->csMode) {
  case ONE_BIT_MODE: target_depth = 1; break;
  case TWO_BIT_MODE: target_depth = 2; break;
  case FOUR_BIT_MODE: target_depth = 4; break;
  case EIGHT_BIT_MODE: target_depth = 8; break;
  case SIXTEEN_BIT_MODE: target_depth = 16; break;
  case THIRTY_TWO_BIT_MODE: target_depth = 24; break;
  /* other cases ruled out above */
  }
  if(si->csPage >= qfb_calculate_num_pages(locals->qfb->width, locals->qfb->height, target_depth)) {
    dprintf("(bad page)\n");
    return controlErr;
  }
  if(si->csMode != locals->cur_mode) {
    /* Actually change the mode */
    qfb_gray_clut(locals);
    locals->qfb->depth = target_depth;
    locals->cur_mode = si->csMode;
    /* (QuickDraw will make other calls as needed to create the trademark gray
       pattern and set a valid CLUT) */
  }
  uint32_t page_offset = QFB_VRAM_SLOT_BASE + (locals->qfb->height * locals->qfb->rowbytes) * si->csPage;
  locals->qfb->base = page_offset;
  si->csBaseAddr = reinterpret_cast<Ptr>(locals->vram + page_offset);
  return noErr;
}

/* Tell the Slot Manager that our video sResource changed, so it re-reads the
   (just re-patched) declaration ROM into the Slot Resource Table. This is the
   piece that lets the OS rebuild the screen GDevice with the new geometry,
   mirroring BasiliskII's use of SUpdateSRT in switch_mode. _SlotManager is trap
   0xA06E with the routine selector in D0.w and an SpBlock pointer in A0. */
static OSErr qfb_update_srt(uint8_t slot, uint8_t sRsrcId) {
  uint32_t spb[14]; /* a 56-byte SpBlock, long-aligned */
  uint8_t* b = reinterpret_cast<uint8_t*>(spb);
  for(int i = 0; i < 56; i++) {
    b[i] = 0;
  }
  b[49] = slot;     /* spSlot */
  b[50] = sRsrcId;  /* spID */
  b[51] = 0;        /* spExtDev */
  register void* a0 asm("%a0") = b;
  register int32_t d0 asm("%d0") = 0x002B; /* SUpdateSRT */
  __asm__ volatile(".short 0xA06E"
                   : "+d"(d0), "+a"(a0)
                   :
                   : "memory", "%d1", "%d2", "%a1");
  return (OSErr)(d0 & 0xFFFF);
}

/* Switch both resolution and depth at once (cscSwitchMode). The Display Manager
   uses this to change resolution: csData is the target DisplayModeID and csMode
   is the target depth mode. cscSetMode (above) only changes depth and keeps the
   resolution. */
int qfb_switch_mode(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  dprintf("\x1B[1mSWITCH MODE: depth=%i res=%i @ %i\x1B[0m\n", si->csMode,
          (int)si->csData, si->csPage);
  if(si->csMode < FIRST_VALID_MODE || si->csMode > LAST_VALID_MODE) {
    DebugStr("\pswitch_mode bad depth");
    return controlErr;
  }
  int target_depth = qfb_depth_for_mode(si->csMode);
  if(target_depth == 0) {
    return controlErr;
  }
  const QfbResolution* r = qfb_resolution_for_id(*locals, si->csData);
  if(r == nullptr) {
    dprintf("(unknown resolution id)\n");
    return paramErr;
  }
  uint32_t num_pages = qfb_calculate_num_pages(r->width, r->height, target_depth);
  if(si->csPage >= num_pages) {
    dprintf("(bad page)\n");
    return controlErr;
  }
  /* Reset to the trademark gray CLUT on a depth change, exactly like cscSetMode;
     QuickDraw repaints the screen afterward. */
  if(si->csMode != locals->cur_mode) {
    qfb_gray_clut(locals);
  }
  /* Program the device. Writing width/height/depth makes QEMU recompute the
     stride register and resize the host display surface. */
  locals->qfb->width = r->width;
  locals->qfb->height = r->height;
  locals->qfb->depth = target_depth;
  locals->cur_mode = si->csMode;
  locals->cur_resolution = si->csData;
  /* Re-patch the declaration ROM to the new geometry and tell the Slot Manager,
     so the OS rebuilds the screen GDevice with the new width/height/rowBytes.
     Without this the hardware changes but QuickDraw keeps the old rowBytes and
     the screen shears. (Mirrors BasiliskII's switch_mode + SUpdateSRT.) */
  locals->qfb->repatch = 1;
  qfb_update_srt(locals->slot, QFB_VIDEO_SRSRC_ID);
  uint32_t page_offset = QFB_VRAM_SLOT_BASE
    + (locals->qfb->height * locals->qfb->rowbytes) * si->csPage;
  locals->qfb->base = page_offset;
  si->csBaseAddr = reinterpret_cast<Ptr>(locals->vram + page_offset);
  return noErr;
}

int qfb_set_entries(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  if(locals->cur_mode < ONE_BIT_MODE
     || locals->cur_mode > EIGHT_BIT_MODE)
    return controlErr;
  else
    return qfb_common_set_entries(params, locals);
}

int qfb_set_gamma(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGammaRecord* g
    = *reinterpret_cast<VDGammaRecord**>(params->csParam);
  if(!g->csGTable) {
    // I don't think this is documented anywhere, but apparently we're supposed
    // to load a linear table if we get a null table?
    for(uint32_t n = 0; n < 256; ++n) {
      locals->qfb->lut_index = n;
      locals->qfb->lut_color = (n<<16)|(n<<8)|n;
    }
    return noErr;
  }
  GammaTbl* tab = reinterpret_cast<GammaTbl*>(g->csGTable);
  if(tab->gVersion != 0) {
    dprintf("%s: %s (%i)\n", "rejecting table", "not version 0", tab->gVersion);
    return controlErr;
  }
  if(tab->gType != 0) {
    dprintf("%s: %s (%i)\n", "rejecting table", "not type 0", tab->gType);
    return controlErr;
  }
  if(tab->gFormulaSize != 0) {
    dprintf("%s: %s (%i)\n", "rejecting table", "non-zero formula", tab->gFormulaSize);
    return controlErr;
  }
  if(tab->gChanCnt != 1 && tab->gChanCnt != 3) {
    dprintf("%s: %s (%i)\n", "rejecting table", "not 1/3 channels", tab->gChanCnt);
    return controlErr;
  }
  if(tab->gDataCnt != 256) {
    dprintf("%s: %s (%i)\n", "rejecting table", "not 256 data", tab->gDataCnt);
    return controlErr;
  }
  if(tab->gDataWidth != 8) {
    dprintf("%s: %s (%i)\n", "rejecting table", "not 8-bit", tab->gDataWidth);
    return controlErr;
  }
  const uint8_t* inp = reinterpret_cast<const uint8_t*>(tab->gFormulaData);
  if(tab->gChanCnt == 3) {
    for(uint32_t n = 0; n < 256; ++n) {
      locals->qfb->lut_index = n;
      uint32_t packed = uint32_t(inp[0]) << 16;
      packed |= uint32_t(inp[1]) << 8;
      packed |= uint32_t(inp[2]);
      inp += 3;
      // dprintf("gamma[%u] = %06X\n", n, packed);
      locals->qfb->lut_color = packed;
    }
  }
  else {
    // tab->gChanCnt == 1
    for(uint32_t n = 0; n < 256; ++n) {
      locals->qfb->lut_index = n;
      uint32_t p = *inp++;
      uint32_t packed = (p << 16) | (p << 8) | p;
      // dprintf("gamma[%u] = %02X\n", n, p);
      locals->qfb->lut_color = packed;
    }
  }
  return noErr;
}

/* put the gray dither pattern in the given page */
int qfb_gray_page(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  qfb_gray_pixels(locals, si->csPage);
  return noErr;
}

/* csMode = 1 -> enable grayscale mapping for SetEntries. csMode = 0 -> disable
   the mapping. */
int qfb_set_gray(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGrayRecord* gr
    = *reinterpret_cast<VDGrayRecord**>(params->csParam);
  locals->gray_mode_enabled = (gr->csMode != 0);
#ifdef DEBUG_QFB
  if(locals->gray_mode_enabled)
    dprintf("Gray mode is now \x1B[1mON\x1B[0m\n");
  else
    dprintf("Gray mode is now \x1B[1;31mO\x1B[32mF\x1B[34mF\x1B[0m\n");
#endif
  return noErr;
}

/* csMode = 0 -> ENABLE interrupt. csMode = 1 -> DISABLE interrupt. The
   interrupt is expected to be ENABLED after open. I think. */
int qfb_set_interrupt(CntrlParam* params, DCtlPtr dce) {
  int8_t* flag
    = *reinterpret_cast<int8_t**>(params->csParam);
  if(*flag)
    return qfb_disable_interrupts(dce);
  else
    return qfb_enable_interrupts(dce);
}

/* Exactly like set_entries, except that it will throw an error unless we're
   in a direct-color mode. (???) */
int qfb_direct_set_entries(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  if(locals->cur_mode < SIXTEEN_BIT_MODE
     || locals->cur_mode > THIRTY_TWO_BIT_MODE)
    return controlErr;
  else
    return qfb_common_set_entries(params, locals);
}

/* Change the default mode. We always set up the mode on the QEMU command line,
   so we don't care about MacOS's concept of default modes.*/
int qfb_set_default_mode(CntrlParam*, DCtlPtr) {
  return noErr;
}

/* True if the Display Manager dispatcher trap is implemented. On a bare System
   7.1 without the Display Manager, _DisplayDispatch is unimplemented and calling
   it would crash, so the host-resize applier becomes a no-op there. */
static bool qfb_display_mgr_available(void) {
  /* 0xABEB = _DisplayDispatch, 0xA89F = _Unimplemented */
  return GetToolboxTrapAddress(0xABEB) != GetToolboxTrapAddress(0xA89F);
}

/* Periodic task, driven by the Device Manager via accRun (we set dNeedTime in
   the ROM driver flags and dCtlDelay at open). It polls the host-resize request
   that QEMU publishes when the user resizes the window and, once the request has
   settled, asks the Display Manager to switch the main screen to the *exact*
   requested size. When the size matches a standard advertised mode we switch to
   that stable mode; otherwise we retarget one of the two dynamic modes to the
   exact size and switch to it (alternating IDs so the Display Manager treats it
   as a real change). The Display Manager performs the QuickDraw and desktop
   relayout and calls back into our cscSwitchMode to program the hardware - which
   is why we must not poke the registers ourselves here.

   If the guest has no Display Manager (bare System 7.1, A/UX) the switch becomes
   a no-op and the window simply scales the current mode; and if the Display
   Manager rejects a dynamic mode, we likewise leave the current resolution in
   place (the host window keeps scaling it). */
int qfb_periodic(CntrlParam* params, DCtlPtr dce) {
  (void)params;
  if(dce->dCtlStorage == nullptr) {
    return noErr;
  }
  HLocker<Locals> locals(dce->dCtlStorage);
  uint32_t serial = locals->qfb->host_req_serial;
  if(serial == locals->last_req_serial) {
    return noErr; /* nothing new since the request we last applied */
  }
  /* Debounce: only act once the request serial has held steady for a couple of
     accRun ticks, so a drag that publishes several sizes results in a single
     switch at the end rather than a storm of Display Manager calls. */
  if(serial != locals->pending_req_serial) {
    locals->pending_req_serial = serial;
    locals->pending_req_ticks = 0;
    return noErr;
  }
  if(locals->pending_req_ticks < QFB_RESIZE_DEBOUNCE_TICKS) {
    locals->pending_req_ticks++;
    return noErr;
  }

  uint32_t rw = locals->qfb->host_req_width;
  uint32_t rh = locals->qfb->host_req_height;
  locals->last_req_serial = serial; /* consider this request handled */
  if(rw == 0 || rh == 0) {
    return noErr;
  }
  /* Clamp to the hardware envelope (also the Cocoa window's min content size). */
  if(rw < QFB_DEV_MIN_WIDTH) rw = QFB_DEV_MIN_WIDTH;
  if(rh < QFB_DEV_MIN_HEIGHT) rh = QFB_DEV_MIN_HEIGHT;
  if(rw > QFB_DEV_MAX_WIDTH) rw = QFB_DEV_MAX_WIDTH;
  if(rh > QFB_DEV_MAX_HEIGHT) rh = QFB_DEV_MAX_HEIGHT;

  if(!qfb_display_mgr_available()) {
    return noErr; /* no Display Manager: leave resolution switching manual */
  }
  GDHandle gd = GetMainDevice();
  if(gd == nullptr) {
    return noErr;
  }

  /* Already showing exactly the requested size? Nothing to do. */
  const QfbResolution* cur = qfb_resolution_for_id(*locals, locals->cur_resolution);
  if(cur != nullptr && cur->width == rw && cur->height == rh) {
    return noErr;
  }

  /* Prefer an exact standard mode; otherwise retarget the spare dynamic mode to
     the exact requested size and switch to it. */
  uint32_t target = qfb_exact_standard_id(*locals, rw, rh);
  if(target == 0) {
    target = qfb_pick_dynamic_id(*locals);
    qfb_set_resolution_geometry(*locals, target, rw, rh);
  }
  if(target == locals->cur_resolution) {
    return noErr;
  }

  unsigned long depthMode = locals->cur_mode; /* keep the current color depth */
  unsigned long switchFlags = 0;
  Boolean modeOk = false;
  OSErr err = DMCheckDisplayMode(gd, target, depthMode, &switchFlags, 0, &modeOk);
  if(err != noErr || !modeOk) {
    return noErr; /* the Display Manager won't allow this switch */
  }
  dprintf("\x1B[1mHOST RESIZE -> mode %i (%ux%u)\x1B[0m\n", (int)target, rw, rh);
  DMSetDisplayMode(gd, target, &depthMode, 0, nullptr);
  return noErr;
}

#ifdef DEBUG_QFB
static const char* get_control_name(int csCode) {
  switch(csCode) {
  case 0:  return "cscReset";
  case 1:  return "cscKillIO";
  case 2:  return "cscSetMode";
  case 3:  return "cscSetEntries";
  case 4:  return "cscSetGamma";
  case 5:  return "cscGrayScreen";
  case 6:  return "cscSetGray";
  case 7:  return "cscSetInterrupt";
  case 8:  return "cscDirectSetEntries";
  case 9: return "cscSetDefaultMode";
  case 10: return "cscSwitchMode";
  case 11: return "cscSetSync";
  case 16: return "cscSavePreferredConfiguration";
  case 22: return "cscSetHardwareCursor";
  case 23: return "cscDrawHardwareCursor";
  case 24: return "cscSetConvolution";
  case 25: return "cscSetPowerState";
  case 26: return "cscPrivateControlCall";
  case 28: return "cscSetMultiConnect";
  case 29: return "cscSetClutBehavior";
  case 31: return "cscSetDetailedTiming";
  case 33: return "cscDoCommunication";
  case 34: return "cscProbeConnection";
  default: return "???";
  }
};
#endif

int qfb_drvr_control(ParmBlkPtr params, DCtlPtr dce) {
  dprintf("control %s (%i)\n",
          get_control_name(params->cntrlParam.csCode),
          params->cntrlParam.csCode);
  dce = (DCtlPtr)StripAddress((Ptr)dce);
  Byte mode = 1;
  SwapMMUMode(&mode);
  int ret = controlErr;
  switch(params->cntrlParam.csCode) {
  case 0: ret = qfb_reset(&params->cntrlParam, dce); break;
  case 1: ret = qfb_kill_io(&params->cntrlParam, dce); break;
  case 2: ret = qfb_set_mode(&params->cntrlParam, dce); break;
  case 3: ret = qfb_set_entries(&params->cntrlParam, dce); break;
  case 4: ret = qfb_set_gamma(&params->cntrlParam, dce); break;
  case 5: ret = qfb_gray_page(&params->cntrlParam, dce); break;
  case 6: ret = qfb_set_gray(&params->cntrlParam, dce); break;
  case 7: ret = qfb_set_interrupt(&params->cntrlParam, dce); break;
  case 8: ret = qfb_direct_set_entries(&params->cntrlParam, dce); break;
  case 16: // PCI flavor!
  case 9: ret = qfb_set_default_mode(&params->cntrlParam, dce); break;
  case 10: ret = qfb_switch_mode(&params->cntrlParam, dce); break;
  case accRun: ret = qfb_periodic(&params->cntrlParam, dce); break;
  }
  dprintf("\t= %i\n", ret);
  SwapMMUMode(&mode);
  return ret;
}
