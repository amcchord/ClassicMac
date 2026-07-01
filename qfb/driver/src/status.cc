#include "qfb_driver.hh"

/* Return the current mode. */
int qfb_get_mode(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  si->csMode = locals->cur_mode;
  si->csData = locals->cur_resolution; /* current DisplayModeID */
  si->csPage = (locals->qfb->base - QFB_VRAM_SLOT_BASE) / (locals->qfb->width * locals->qfb->rowbytes);
  si->csBaseAddr = reinterpret_cast<Ptr>(locals->vram + locals->qfb->base);
  return noErr;
}

extern "C" void get_entry(volatile QFB* qfb, ColorSpec* entry) {
  uint32_t pix = qfb->pal_color;
  entry->rgb.red = (pix >> 16) & 255;
  entry->rgb.red |= entry->rgb.red << 8;
  entry->rgb.green = (pix >> 8) & 255;
  entry->rgb.green |= entry->rgb.green << 8;
  entry->rgb.blue = pix & 255;
  entry->rgb.blue |= entry->rgb.blue << 8;
}

/* Read the color table. */
int qfb_get_entries(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSetEntryRecord* si
    = *reinterpret_cast<VDSetEntryRecord**>(params->csParam);
  volatile QFB* qfb = locals->qfb;
  /* note: si->csCount is "zero-based", so <= is correct below */
  if(si->csStart < 0) {
    /* Set entries by their index fields. */
    for(int32_t array_index = 0; array_index <= si->csCount; ++array_index) {
      ColorSpec* entry = si->csTable + array_index;
      if(entry->value >= 256 || entry->value < 0)
        continue;
      qfb->pal_index = entry->value;
      get_entry(qfb, entry);
    }
  }
  else {
    /* Set entries starting from the given index. */
    for(int32_t array_index = 0, entry_index = si->csStart;
        array_index <= si->csCount && entry_index < 256;
        ++array_index, ++entry_index) {
      ColorSpec* entry = si->csTable + array_index;
      qfb->pal_index = entry_index;
      get_entry(qfb, entry);
    }
  }
  return noErr;
}

/* Return the number of pages available in the current mode. */
int qfb_get_page_count(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  si->csPage = qfb_calculate_num_pages(locals->qfb->width, locals->qfb->height, locals->qfb->depth);
  return noErr;
}

/* Returns the base address of the given page in the current mode. */
int qfb_get_page_base(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  uint32_t target_page = si->csPage;
  if(target_page >= qfb_calculate_num_pages(locals->qfb->width, locals->qfb->height, locals->qfb->depth))
    return statusErr;
  si->csBaseAddr = reinterpret_cast<Ptr>(locals->vram + QFB_VRAM_SLOT_BASE + target_page * locals->qfb->rowbytes * locals->qfb->height);
  return noErr;
}

/* Returns whether grayscale mapping is in effect. */
int qfb_get_gray(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGrayRecord* g
    = *reinterpret_cast<VDGrayRecord**>(params->csParam);
  g->csMode = locals->gray_mode_enabled ? 1 : 0;
  return noErr;
}

int qfb_get_interrupt(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGrayRecord* g
    = *reinterpret_cast<VDGrayRecord**>(params->csParam);
  g->csMode = locals->irq_enabled ? 0 : 1;
  return noErr;
}

int qfb_get_gamma(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGammaRecord* g
    = *reinterpret_cast<VDGammaRecord**>(params->csParam);
  int channel_count = 1;
  for(uint32_t n = 0; n < 256; ++n) {
    locals->qfb->lut_index = n;
    uint32_t packed = locals->qfb->lut_color;
    uint8_t r = (packed >> 16);
    uint8_t g = (packed >> 8);
    uint8_t b = packed;
    if(r != g || r != b) {
      channel_count = 3;
      break;
    }
  }
  g->csGTable = NewPtrSysClear(sizeof(GammaTbl) - 2 + (256 * channel_count));
  if(!g->csGTable) return MemError();
  GammaTbl* tab = reinterpret_cast<GammaTbl*>(g->csGTable);
  tab->gChanCnt = channel_count;
  tab->gDataCnt = 256;
  tab->gDataWidth = 8;
  uint8_t* outp = reinterpret_cast<uint8_t*>(tab->gFormulaData);
  if(tab->gChanCnt == 3) {
    for(uint32_t n = 0; n < 256; ++n) {
      locals->qfb->lut_index = n;
      uint32_t packed = locals->qfb->lut_color;
      *outp++ = (packed >> 16);
      *outp++ = (packed >> 8);
      *outp++ = packed;
    }
  }
  else {
    for(uint32_t n = 0; n < 256; ++n) {
      locals->qfb->lut_index = n;
      uint32_t packed = locals->qfb->lut_color;
      *outp++ = packed;
    }
  }
  /* Our caller will have to dispose of that pointer. */
  return noErr;
}

/* Return the default mode. Since we always respect the value on the QEMU
   command line, our job here is simple. */
int qfb_get_default_mode(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDSwitchInfoRec* si
    = *reinterpret_cast<VDSwitchInfoRec**>(params->csParam);
  si->csData = 128; // preferred mode ID
  switch(locals->qfb->user_depth) {
  case 1: si->csMode = ONE_BIT_MODE; return noErr;
  case 2: si->csMode = TWO_BIT_MODE; return noErr;
  case 4: si->csMode = FOUR_BIT_MODE; return noErr;
  case 8: si->csMode = EIGHT_BIT_MODE; return noErr;
  case 16: si->csMode = SIXTEEN_BIT_MODE; return noErr;
  case 24:
  case 32: si->csMode = THIRTY_TWO_BIT_MODE; return noErr;
  default:
    DebugStr("\puser_mode invalid");
    return statusErr;
  }
}

/* Return information about the connection to the monitor. */
int qfb_get_connection(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDDisplayConnectInfoRec* dci
    = *reinterpret_cast<VDDisplayConnectInfoRec**>(params->csParam);
  dci->csDisplayType = 20; // generic LCD
  dci->csConnectTaggedType = 0;
  dci->csConnectTaggedData = 0;
  dci->csConnectFlags = 0x000C; // all modes safe, direct connection
  dci->csDisplayComponent = 0;
  return noErr;
}

/* Return information about the connection to the monitor. */
int qfb_get_mode_timing(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDTimingInfoRec* ti
    = *reinterpret_cast<VDTimingInfoRec**>(params->csParam);
  ti->csTimingFormat = 'decl';
  /* kModeValid (bit 0) | kModeSafe (bit 1); only the boot/user mode is also
     flagged kModeDefault (bit 2) so the Display Manager picks it as default. */
  uint32_t flags = 0x00000003;
  if(ti->csTimingMode == QFB_USER_MODE_ID) {
    flags = 0x00000007;
  }
  ti->csTimingFlags = flags;
  ti->csTimingData = 159; // "Apple Fixed Rate LCD"
  return noErr;
}

/* This is used to query the resolutions supported by the device. The Display
   Manager walks the list by passing the previously returned DisplayModeID; we
   return 0xFFFFFFFD once it has seen them all. */
int qfb_get_next_resolution(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDResolutionInfoRec* ri
    = *reinterpret_cast<VDResolutionInfoRec**>(params->csParam);
  uint32_t prev = ri->csPreviousDisplayModeID;
  int idx;
  if(prev == 0 || prev == 0xFFFFFFFE) {
    /* Start of iteration (or "current"): return the first resolution. */
    idx = 0;
  } else {
    int found = qfb_find_resolution_index(*locals, prev);
    if(found < 0) {
      return paramErr;
    }
    idx = found + 1;
    if(idx >= locals->num_resolutions) {
      ri->csDisplayModeID = 0xFFFFFFFD; /* no more resolutions */
      return noErr;
    }
  }
  const QfbResolution* r = &locals->resolutions[idx];
  ri->csDisplayModeID = r->id;
  ri->csHorizontalPixels = r->width;
  ri->csVerticalPixels = r->height;
  ri->csRefreshRate = 60 << 16;
  ri->csMaxDepthMode = LAST_VALID_MODE;
  ri->csResolutionFlags = 0;
  return noErr;
}

/* Return information about the given video mode. */
int qfb_get_video_parameters(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDVideoParametersInfoRec* vpi
    = *reinterpret_cast<VDVideoParametersInfoRec**>(params->csParam);
  dprintf("get_video_parameters csDisplayModeID=%i csDepthMode=%i\n",
          vpi->csDisplayModeID, vpi->csDepthMode);
  uint32_t width;
  uint32_t height;
  if(vpi->csDisplayModeID == 0) {
    /* mode ID 0 means "the current mode" */
    width = locals->qfb->width;
    height = locals->qfb->height;
  } else {
    const QfbResolution* r = qfb_resolution_for_id(*locals, vpi->csDisplayModeID);
    if(r == nullptr) {
      return controlErr;
    }
    width = r->width;
    height = r->height;
  }
  VPBlock* vpb = vpi->csVPBlockPtr;
  vpb->vpBaseOffset = QFB_VRAM_SLOT_BASE;
  vpb->vpBounds.top = 0;
  vpb->vpBounds.left = 0;
  vpb->vpBounds.bottom = height;
  vpb->vpBounds.right = width;
  vpb->vpVersion = 1;
  vpb->vpPackType = 0;
  vpb->vpPackSize = 0;
  vpb->vpHRes = 72 << 16;
  vpb->vpVRes = 72 << 16;
  vpb->vpPlaneBytes = 0;
  int depth;
  switch(vpi->csDepthMode) {
  case ONE_BIT_MODE:
    depth = 1;
    vpb->vpCmpSize = vpb->vpPixelSize = 1;
    break;
  case TWO_BIT_MODE:
    depth = 2;
    vpb->vpCmpSize = vpb->vpPixelSize = 2;
    break;
  case FOUR_BIT_MODE:
    depth = 4;
    vpb->vpCmpSize = vpb->vpPixelSize = 4;
    break;
  case EIGHT_BIT_MODE:
    depth = 8;
    vpb->vpCmpSize = vpb->vpPixelSize = 8;
    break;
  case SIXTEEN_BIT_MODE:
    depth = 16;
    vpb->vpCmpSize = 5;
    vpb->vpPixelSize = 16;
    break;
  case THIRTY_TWO_BIT_MODE:
    depth = 24;
    vpb->vpCmpSize = 8;
    vpb->vpPixelSize = 32;
    break;
  default:
    return statusErr;
  }
  vpb->vpPixelType = depth >= 16 ? 16 : 0;
  vpb->vpCmpCount = depth >= 16 ? 3 : 1;
  vpb->vpRowBytes = qfb_calculate_stride(width, depth);
  vpi->csPageCount = qfb_calculate_num_pages(width, height, depth);
  vpi->csDeviceType = vpi->csDepthMode >= SIXTEEN_BIT_MODE ? 16 : 0;
  return noErr;
}

extern char _GammaTableMac_Name[32];
extern char _GammaTableLinear_Name[32];
extern char _GammaTableNTSC_Name[32];
extern char _GammaTableSGI_Name[32];
extern char _GammaTablePAL_Name[32];
extern char _GammaTablePlato_Name[32];
extern char _GammaTableOtalp_Name[32];

int qfb_get_gamma_info_list(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDGetGammaListRec* ggl
    = *reinterpret_cast<VDGetGammaListRec**>(params->csParam);
  switch(ggl->csPreviousGammaTableID) {
  case 0: /* get a specific ID */
    break;
  case 0xFFFFFFFE: /* get the first table */
    ggl->csGammaTableID = 128;
    break;
  case 134:
    ggl->csGammaTableID = 0xFFFFFFFD; /* no more tables */
    return noErr;
  default:
    if(ggl->csPreviousGammaTableID < 128) return paramErr;
    if(ggl->csPreviousGammaTableID > 134) return paramErr;
    ggl->csGammaTableID = ggl->csPreviousGammaTableID + 1;
    break;
  }
  switch(ggl->csGammaTableID) {
  case 128:
    mystrcpy(ggl->csGammaTableName, _GammaTableMac_Name);
    break;
  case 129:
    mystrcpy(ggl->csGammaTableName, _GammaTableLinear_Name);
    break;
  case 130:
    mystrcpy(ggl->csGammaTableName, _GammaTableNTSC_Name);
    break;
  case 131:
    mystrcpy(ggl->csGammaTableName, _GammaTableSGI_Name);
    break;
  case 132:
    mystrcpy(ggl->csGammaTableName, _GammaTablePAL_Name);
    break;
  case 133:
    mystrcpy(ggl->csGammaTableName, _GammaTablePlato_Name);
    break;
  case 134:
    mystrcpy(ggl->csGammaTableName, _GammaTableOtalp_Name);
    break;
  default:
    return paramErr;
  }
  ggl->csGammaTableSize = 12 + (ggl->csGammaTableID > 132 ? 768 : 256);
  return noErr;
}

extern GammaTbl _GammaTableMac;
extern GammaTbl _GammaTableLinear;
extern GammaTbl _GammaTableNTSC;
extern GammaTbl _GammaTableSGI;
extern GammaTbl _GammaTablePAL;
extern GammaTbl _GammaTablePlato;
extern GammaTbl _GammaTableOtalp;

int qfb_retrieve_gamma_table(CntrlParam* params, DCtlPtr dce) {
  HLocker<Locals> locals(dce->dCtlStorage);
  VDRetrieveGammaRec* rg
    = *reinterpret_cast<VDRetrieveGammaRec**>(params->csParam);
  switch(rg->csGammaTableID) {
  case 128:
    mymemcpy(rg->csGammaTablePtr, &_GammaTableMac, 12 + 256);
    break;
  case 129:
    mymemcpy(rg->csGammaTablePtr, &_GammaTableLinear, 12 + 256);
    break;
  case 130:
    mymemcpy(rg->csGammaTablePtr, &_GammaTableNTSC, 12 + 256);
    break;
  case 131:
    mymemcpy(rg->csGammaTablePtr, &_GammaTableSGI, 12 + 256);
    break;
  case 132:
    mymemcpy(rg->csGammaTablePtr, &_GammaTablePAL, 12 + 256);
    break;
  case 133:
    mymemcpy(rg->csGammaTablePtr, &_GammaTablePlato, 12 + 768);
    break;
  case 134:
    mymemcpy(rg->csGammaTablePtr, &_GammaTableOtalp, 12 + 768);
    break;
  default:
    return paramErr;
  }
  return noErr;
}

#ifdef DEBUG_QFB
static const char* get_status_name(int csCode) {
  switch(csCode) {
  case 2:  return "cscGetMode";
  case 3:  return "cscGetEntries";
  case 4:  return "cscGetPageCnt";
  case 5:  return "cscGetPageBase";
  case 6:  return "cscGetGray";
  case 7:  return "cscGetInterrupt";
  case 8: return "cscGetGamma";
  case 9: return "cscGetDefaultMode";
  case 10: return "cscGetCurMode";
  case 11: return "cscGetSync";
  case 12: return "cscGetConnection";
  case 13: return "cscGetModeTiming";
  case 14: return "cscGetModeBaseAddress";
  case 15: return "cscGetScanProc";
  case 16: return "cscGetPreferredConfiguration";
  case 17: return "cscGetNextResolution";
  case 18: return "cscGetVideoParameters";
  case 20: return "cscGetGammaInfoList";
  case 21: return "cscRetrieveGammaTable";
  case 22: return "cscSupportsHardwareCursor";
  case 23: return "cscGetHardwareCursorDrawState";
  case 24: return "cscGetConvolution";
  case 25: return "cscGetPowerState";
  case 26: return "cscPrivateStatusCall";
  case 27: return "cscGetDDCBlock";
  case 28: return "cscGetMultiConnect";
  case 29: return "cscGetClutBehavior";
  case 30: return "cscGetTimingRanges";
  case 31: return "cscGetDetailedTiming";
  case 32: return "cscGetCommunicationInfo";
  default: return "???";
  }
}
#endif

int qfb_drvr_status(ParmBlkPtr params, DCtlPtr dce) {
  dprintf("\t\t\t\t\tstatus %s (%i)\n",
          get_status_name(params->cntrlParam.csCode),
          params->cntrlParam.csCode);
  dce = (DCtlPtr)StripAddress((Ptr)dce);
  Byte mode = 1;
  SwapMMUMode(&mode);
  int ret = statusErr;
  switch(params->cntrlParam.csCode) {
  case 10: // PCI flavor!
  case 2: ret = qfb_get_mode(&params->cntrlParam, dce); break;
  case 3: ret = qfb_get_entries(&params->cntrlParam, dce); break;
  case 4: ret = qfb_get_page_count(&params->cntrlParam, dce); break;
  case 5: ret = qfb_get_page_base(&params->cntrlParam, dce); break;
  case 6: ret = qfb_get_gray(&params->cntrlParam, dce); break;
  case 7: ret = qfb_get_interrupt(&params->cntrlParam, dce); break;
  case 8: ret = qfb_get_gamma(&params->cntrlParam, dce); break;
  case 16: // PCI flavor!
  case 9: ret = qfb_get_default_mode(&params->cntrlParam, dce); break;
    /* PCI-flavored routines, OS8 uses these even on NuBus */
  case 12: ret = qfb_get_connection(&params->cntrlParam, dce); break;
  case 13: ret = qfb_get_mode_timing(&params->cntrlParam, dce); break;
  case 17: ret = qfb_get_next_resolution(&params->cntrlParam, dce); break;
  case 18: ret = qfb_get_video_parameters(&params->cntrlParam, dce); break;
  case 20: ret = qfb_get_gamma_info_list(&params->cntrlParam, dce); break;
  case 21: ret = qfb_retrieve_gamma_table(&params->cntrlParam, dce); break;
  }
  dprintf("\t\t\t\t\t\t= %i\n", ret);
  SwapMMUMode(&mode);
  return ret;
}

