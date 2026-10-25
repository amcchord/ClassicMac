/* ClassicMac display service. MIT; uses classicvirtio's NDRV runtime.
 * Loaded from firmware on a dedicated PCI mailbox, with no guest disk writes.
 * GNEFilter runs in application context; Display Manager is never called from
 * an interrupt or from a video-driver control request. Wait for the first
 * Finder event loop, then handle subsequent real application event loops.
 */
#include "callout68k.h"
#include "device.h"
#include <CodeFragments.h>
#include <Devices.h>
#include <Displays.h>
#include <DriverServices.h>
#include <Events.h>
#include <LowMem.h>
#include <MixedMode.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Processes.h>
#include <Traps.h>
#include <string.h>
static CFragConnectionID displayLibrary, interfaceLibrary;
static __typeof__(&DMGetDisplayIDByGDevice) fnDMGetDisplayIDByGDevice;
static __typeof__(&DMNewDisplayModeList) fnDMNewDisplayModeList;
static __typeof__(&DMGetIndexedDisplayModeFromList)
    fnDMGetIndexedDisplayModeFromList;
static __typeof__(&DMDisposeList) fnDMDisposeList;
static __typeof__(&DMBeginConfigureDisplays) fnDMBeginConfigureDisplays;
static __typeof__(&DMSetDisplayMode) fnDMSetDisplayMode;
static __typeof__(&DMEndConfigureDisplays) fnDMEndConfigureDisplays;
static void closeLibraries(void) {
  if (displayLibrary)
    CloseConnection(&displayLibrary);
  if (interfaceLibrary)
    CloseConnection(&interfaceLibrary);
  displayLibrary = interfaceLibrary = 0;
}
static Boolean bindDisplay(void) {
  Ptr entry;
  Str255 error;
  CFragSymbolClass cls;
  if (displayLibrary)
    return true;
  if (GetSharedLibrary((ConstStr255Param) "\x0a"
                                          "DisplayLib",
                       kPowerPCCFragArch, kFindCFrag, &displayLibrary, &entry,
                       error) != noErr)
    return false;
  GetSharedLibrary((ConstStr255Param) "\x0c"
                                      "InterfaceLib",
                   kPowerPCCFragArch, kFindCFrag, &interfaceLibrary, &entry,
                   error);
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x17"
                                    "DMGetDisplayIDByGDevice",
                 (Ptr *)&fnDMGetDisplayIDByGDevice, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x17"
                                    "DMGetDisplayIDByGDevice",
                 (Ptr *)&fnDMGetDisplayIDByGDevice, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x14"
                                    "DMNewDisplayModeList",
                 (Ptr *)&fnDMNewDisplayModeList, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x14"
                                    "DMNewDisplayModeList",
                 (Ptr *)&fnDMNewDisplayModeList, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x1f"
                                    "DMGetIndexedDisplayModeFromList",
                 (Ptr *)&fnDMGetIndexedDisplayModeFromList, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x1f"
                                    "DMGetIndexedDisplayModeFromList",
                 (Ptr *)&fnDMGetIndexedDisplayModeFromList, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x0d"
                                    "DMDisposeList",
                 (Ptr *)&fnDMDisposeList, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x0d"
                                    "DMDisposeList",
                 (Ptr *)&fnDMDisposeList, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x18"
                                    "DMBeginConfigureDisplays",
                 (Ptr *)&fnDMBeginConfigureDisplays, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x18"
                                    "DMBeginConfigureDisplays",
                 (Ptr *)&fnDMBeginConfigureDisplays, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x10"
                                    "DMSetDisplayMode",
                 (Ptr *)&fnDMSetDisplayMode, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x10"
                                    "DMSetDisplayMode",
                 (Ptr *)&fnDMSetDisplayMode, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  if (FindSymbol(displayLibrary,
                 (ConstStr255Param) "\x16"
                                    "DMEndConfigureDisplays",
                 (Ptr *)&fnDMEndConfigureDisplays, &cls) != noErr &&
      FindSymbol(interfaceLibrary,
                 (ConstStr255Param) "\x16"
                                    "DMEndConfigureDisplays",
                 (Ptr *)&fnDMEndConfigureDisplays, &cls) != noErr) {
    closeLibraries();
    return false;
  }
  return true;
}
static volatile unsigned long *mailbox;
static unsigned long lastSerial, pendingSerial, pendingTick;
static Boolean busy, finderReady;
static unsigned attempts;
static unsigned long attemptTick;
DriverDescription TheDriverDescription = {
    kTheDescriptionSignature,
    kInitialDriverDescriptor,
    {"\x0c"
     "pci1234,1112",
     {0x00, 0x10, 0x80, 0x00}},
    {kDriverIsLoadedUponDiscovery | kDriverIsOpenedUponLoad, "\x0b"
                                                             ".MacDisplay"},
    {1,
     {{kServiceCategoryNdrvDriver,
       kNdrvTypeIsGeneric,
       {0x00, 0x10, 0x80, 0x00}}}}};
typedef struct {
  unsigned long w, h, mode, depthMode;
  short depth;
  Boolean found;
} Wanted;
static pascal void consider(void *data, DMListIndexType idx,
                            DMDisplayModeListEntryPtr e) {
  Wanted *w = data;
  unsigned long i;
  VDResolutionInfoPtr r = e->displayModeResolutionInfo;
  if (!r || r->csHorizontalPixels != w->w || r->csVerticalLines != w->h ||
      !e->displayModeDepthBlockInfo)
    return;
  for (i = 0; i < e->displayModeDepthBlockInfo->depthBlockCount; i++) {
    DMDepthInfoPtr d = &e->displayModeDepthBlockInfo->depthVPBlock[i];
    if (d->depthVPBlock && d->depthSwitchInfo &&
        d->depthVPBlock->vpPixelSize == w->depth) {
      w->mode = r->csDisplayModeID;
      w->depthMode = d->depthSwitchInfo->csMode;
      w->found = true;
      return;
    }
  }
}
static OSErr videoCall(GDHandle gd, short selector, void *record,
                       Boolean control) {
  CntrlParam pb;
  memset(&pb, 0, sizeof(pb));
  pb.ioCRefNum = (**gd).gdRefNum;
  pb.csCode = selector;
  memcpy(pb.csParam, &record, sizeof(record));
  return control ? PBControlSync((ParmBlkPtr)&pb)
                 : PBStatusSync((ParmBlkPtr)&pb);
}
static unsigned long programmedModes[2], programmedAliases[2];
static Boolean programMode(GDHandle gd, Wanted *w) {
  VDSwitchInfoRec current = {0};
  VDResolutionInfoRec r = {0};
  VDDetailedTimingRec t = {0};
  unsigned index;
  OSErr err;
  if (videoCall(gd, cscGetCurMode, &current, false) != noErr)
    return false;
  /* Never retarget the active slot. Radeon enumeration can return an in-use
   * programmable ID first, so explicitly skip the slot we already own. */
  index =
      programmedAliases[0] && programmedAliases[0] == current.csData ? 1 : 0;
  if (!programmedModes[index]) {
    r.csPreviousDisplayModeID = kDisplayModeIDFindFirstProgrammable;
    for (unsigned n = 0; n < 128; n++) {
      err = videoCall(gd, cscGetNextResolution, &r, false);
      if (err || r.csDisplayModeID == kDisplayModeIDNoMoreResolutions)
        return false;
      if (r.csDisplayModeID != programmedModes[1 - index]) {
        programmedModes[index] = r.csDisplayModeID;
        break;
      }
      r.csPreviousDisplayModeID = r.csDisplayModeID;
    }
    if (!programmedModes[index])
      return false;
  }
  t.csTimingSize = sizeof(t);
  t.csDisplayModeID = programmedModes[index];
  err = videoCall(gd, cscGetDetailedTiming, &t, false);
  if (err) {
    return false;
  }
  t.csDisplayModeState = kDMSModeReady;
  t.csDisplayModeAlias = t.csDisplayModeID;
  t.csHorizontalActive = w->w;
  t.csHorizontalBlanking = 160;
  t.csHorizontalSyncOffset = 48;
  t.csHorizontalSyncPulseWidth = 32;
  t.csVerticalActive = w->h;
  t.csVerticalBlanking = 30;
  t.csVerticalSyncOffset = 3;
  t.csVerticalSyncPulseWidth = 6;
  t.csSignalConfig = 0;
  t.csSignalLevels = kAnalogSignalLevel_0700_0300;
  t.csHorizontalSyncConfig = kSyncPositivePolarityMask;
  t.csVerticalSyncConfig = 0;
  unsigned long clock = (w->w + 160) * (w->h + 30) * 60;
  if (clock > 162000000)
    clock = (w->w + 160) * (w->h + 30) * 50;
  t.csPixelClock = (UInt64)clock;
  t.csMinPixelClock = (UInt64)(clock - clock / 50);
  t.csMaxPixelClock = (UInt64)(clock + clock / 50);
  err = videoCall(gd, cscSetDetailedTiming, &t, true);
  if (err)
    return false;
  w->mode = t.csDisplayModeAlias ? t.csDisplayModeAlias : t.csDisplayModeID;
  programmedAliases[index] = w->mode;
  w->depthMode = current.csMode;
  w->found = true;
  return true;
}
static void complete(OSErr result) {
  mailbox[4] = (unsigned long)(long)result;
  SynchronizeIO();
  mailbox[5] = pendingSerial;
  lastSerial = pendingSerial;
}
static void apply(void) {
  ProcessSerialNumber psn;
  ProcessInfoRec pi;
  GDHandle gd;
  DisplayIDType id;
  DMListType list = 0;
  DMListIndexType count = 0, i;
  Handle state = 0;
  Wanted w = {0};
  OSErr err;
  if (!mailbox || busy || CurrentExecutionLevel() != kTaskLevel)
    return;
  /* Observe Finder even when no resize is pending. A user can launch a game
   * before the first host resize; later application event loops must then be
   * allowed to service the request without waiting for Finder again. */
  if (!finderReady) {
    memset(&pi, 0, sizeof(pi));
    pi.processInfoLength = sizeof(pi);
    if (GetCurrentProcess(&psn) != noErr || psn.lowLongOfPSN <= 2 ||
        GetProcessInformation(&psn, &pi) != noErr || pi.processSignature != 'MACS')
      return;
    finderReady = true;
  }
  if (!mailbox[3] || mailbox[3] == lastSerial)
    return;
  if (mailbox[3] != pendingSerial) {
    pendingSerial = mailbox[3];
    pendingTick = TickCount();
    attempts = 0;
    attemptTick = pendingTick;
    return;
  }
  if ((unsigned long)(TickCount() - pendingTick) < 30 ||
      (unsigned long)(TickCount() - attemptTick) < 30)
    return;
  memset(&pi, 0, sizeof(pi));
  pi.processInfoLength = sizeof(pi);
  if (GetCurrentProcess(&psn) != noErr || psn.lowLongOfPSN <= 2 ||
      GetProcessInformation(&psn, &pi) != noErr)
    return;
  if (pi.processSignature == 'MACS')
    finderReady = true;
  if (!finderReady)
    return;
  if (!bindDisplay()) {
    attemptTick = TickCount();
    if (++attempts >= 12)
      complete(unimpErr);
    return;
  }
  busy = true;
  attemptTick = TickCount();
  w.w = mailbox[1];
  w.h = mailbox[2];
  if (mailbox[3] != pendingSerial) {
    busy = false;
    return;
  }
  err = paramErr;
  if (w.w < 640 || w.w > 1920 || w.h < 480 || w.h > 1440 || (w.w & 7)) {
    complete(err);
    busy = false;
    return;
  }
  gd = GetMainDevice();
  if (!gd)
    goto done;
  if ((**gd).gdRect.right - (**gd).gdRect.left == w.w &&
      (**gd).gdRect.bottom - (**gd).gdRect.top == w.h) {
    complete(noErr);
    busy = false;
    return;
  }
  w.depth = (**(**gd).gdPMap).pixelSize;
  if (programMode(gd, &w))
    goto switch_mode;
  err = fnDMGetDisplayIDByGDevice(gd, &id, false);
  if (err)
    goto done;
  err = fnDMNewDisplayModeList(id, 0, 0, &count, &list);
  if (err)
    goto done;
  DMDisplayModeListIteratorUPP cb = NewDMDisplayModeListIteratorUPP(consider);
  for (i = 0; i < count; i++)
    fnDMGetIndexedDisplayModeFromList(list, i, 0, cb, &w);
  DisposeDMDisplayModeListIteratorUPP(cb);
  fnDMDisposeList(list);
  if (!w.found) {
    err = paramErr;
    goto done;
  }
switch_mode:
  err = fnDMBeginConfigureDisplays(&state);
  if (!err) {
    err = fnDMSetDisplayMode(gd, w.mode, &w.depthMode, 0, state);
    OSErr endErr = fnDMEndConfigureDisplays(state);
    if (!err)
      err = endErr;
  }
  gd = GetMainDevice();
  if (!err && (!gd || (**gd).gdRect.right - (**gd).gdRect.left != w.w ||
               (**gd).gdRect.bottom - (**gd).gdRect.top != w.h))
    err = paramErr;
done:
  if (!err || ++attempts >= 12)
    complete(err);
  busy = false;
}
static GetNextEventFilterUPP previousFilter, replacementFilter;
static void eventFilter(EventRecord *event, Boolean *result) {
  if (previousFilter)
    InvokeGetNextEventFilterUPP(event, result, previousFilter);
  apply();
}
int DriverStart(short refNum) {
  if (replacementFilter)
    return noErr;
  RegEntryID dev;
  RegPropertyValueSize len;
  void *bars[6] = {0};
  GetDriverInformation(
      refNum, &(UnitNumber){0}, &(DriverFlags){0}, &(DriverOpenCount){0},
      (Str255){0}, &dev,
      &(CFragSystem7Locator){.u = {.onDisk = {.fileSpec = &(FSSpec){0}}}},
      &(CFragConnectionID){0}, &(DriverEntryPointPtr){0},
      &(DriverDescription){0});
  len = sizeof(bars);
  if (RegistryPropertyGet(&dev, "AAPL,address", bars, &len) != noErr ||
      !bars[0])
    return -1;
  unsigned short command = 0;
  ExpMgrConfigReadWord(&dev, (LogicalAddress)4, &command);
  ExpMgrConfigWriteWord(&dev, (LogicalAddress)4, command | 2);
  SynchronizeIO();
  mailbox = bars[0];
  if (mailbox[0] != 0x434d7273) {
    mailbox = 0;
    return -1;
  }
  previousFilter = LMGetGNEFilter();
  replacementFilter = NewGetNextEventFilterUPP(eventFilter);
  if (!replacementFilter)
    return -1;
  LMSetGNEFilter(replacementFilter);
  return noErr;
}
int DriverStop(void) {
  if (busy || LMGetGNEFilter() != replacementFilter)
    return fBsyErr;
  LMSetGNEFilter(previousFilter);
  DisposeGetNextEventFilterUPP(replacementFilter);
  replacementFilter = 0;
  closeLibraries();
  mailbox = 0;
  return noErr;
}

int DriverRead(IOParam *p) { return -19; }
int DriverWrite(IOParam *p) { return -20; }
int DriverCtl(CntrlParam *p) { return -17; }
int DriverStatus(CntrlParam *p) { return -18; }
