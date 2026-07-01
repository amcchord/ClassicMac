#ifndef EXTRAVERSE_H
#define EXTRAVERSE_H

#include <Multiverse.h>

/* Some random definitions not found in the Multiversal Interfaces. */

struct SlotIntQElement {
  Ptr sqLink;
  short sqType;
  short sqPrio;
  short(*sqAddr)(uint32_t);
  uint32_t sqParm;
};

#pragma parameter __D0 SIntInstall(__A0, __D0)
OSErr SIntInstall(SlotIntQElement*, short) M68K_INLINE(0xA075);
#pragma parameter __D0 SIntRemove(__A0, __D0)
OSErr SIntRemove(SlotIntQElement*, short) M68K_INLINE(0xA076);

/* Display Manager calls, dispatched through _DisplayDispatch (0xABEB). These
   are missing from the Multiversal Interfaces; the dispatch selectors come from
   Apple's Displays.h (0x0A11 for DMSetDisplayMode, 0x0C12 for
   DMCheckDisplayMode) and follow the same M68K_INLINE form the interfaces use
   for SetDepth/HasDepth. We use them to switch the screen resolution from the
   driver when the host window is resized. */
pascal OSErr DMSetDisplayMode(GDHandle theDevice, unsigned long mode,
                              unsigned long *depthMode, unsigned long reserved,
                              Handle displayState)
    M68K_INLINE(0x303C, 0x0A11, 0xABEB);
pascal OSErr DMCheckDisplayMode(GDHandle theDevice, unsigned long mode,
                                unsigned long depthMode,
                                unsigned long *switchFlags,
                                unsigned long reserved, Boolean *modeOk)
    M68K_INLINE(0x303C, 0x0C12, 0xABEB);

struct VDSwitchInfoRec {
  uint16_t csMode; /* mode ID */
  /* In the old driver scheme, this is a pointer to an sResource describing the
     target mode (I think). In the new driver scheme, this is a mode ID. */
  uint32_t csData;
  uint16_t csPage; /* target page */
  Ptr csBaseAddr; /* base address of resultant page */
  uint32_t csReserved;
};

struct VDDisplayConnectInfoRec {
  uint16_t csDisplayType; /* what kind of display is connected */
  uint8_t csConnectTaggedType; /* what kind of tagging data */
  uint8_t csConnectTaggedData; /* tagging data */
  uint32_t csConnectFlags; /* flags about the connection */
  uint32_t csDisplayComponent; /* reserved? */
  uint32_t csConnectReserved; /* reserved */
};

struct VPBlock {
  /* Offset of page, relative to the start of VRAM */
  uint32_t vpBaseOffset;
  /* All other fields are as in PixMap */
  uint16_t vpRowBytes;
  Rect vpBounds;
  int16_t vpVersion;
  int16_t vpPackType;
  uint32_t vpPackSize;
  Fixed vpHRes, vpVRes;
  int16_t vpPixelType;
  int16_t vpPixelSize;
  int16_t vpCmpCount;
  int16_t vpCmpSize;
  uint32_t vpPlaneBytes;
};

struct VDVideoParametersInfoRec {
  uint32_t csDisplayModeID;
  uint16_t csDepthMode;
  VPBlock* csVPBlockPtr;
  uint32_t csPageCount;
  uint32_t csDeviceType;
  uint32_t csReserved;
};

struct VDResolutionInfoRec {
  uint32_t csPreviousDisplayModeID;
  uint32_t csDisplayModeID;
  uint32_t csHorizontalPixels;
  uint32_t csVerticalPixels;
  Fixed csRefreshRate;
  uint16_t csMaxDepthMode;
  uint32_t csResolutionFlags;
  uint32_t csReserved;
};

struct VDTimingInfoRec {
  uint32_t csTimingMode;
  uint32_t csTimingReserved;
  uint32_t csTimingFormat;
  uint32_t csTimingData;
  uint32_t csTimingFlags;
};

struct VDSetEntryRecord {
  ColorSpec* csTable;
  int16_t csStart;
  int16_t csCount;
};

struct VDGrayRecord {
  uint8_t csMode;
  uint8_t filler;
};

struct VDFlagRecord {
  uint8_t csMode;
  uint8_t filler;
};

struct GammaTbl {
  uint16_t gVersion; /* zero is the only version */
  uint16_t gType; /* non-zero iff this table is weird-device-specific */
  uint16_t gFormulaSize; /* must be zero if gType is zero */
  uint16_t gChanCnt; /* either 1 or 3 */
  uint16_t gDataCnt; /* hopefully 256 */
  uint16_t gDataWidth; /* bits/datum, storage uses next larger byte size
                          (should always be 8 though) */
  uint16_t gFormulaData[1]; /* not a real field, actually the beginning of the
                               variable-length data */
};

struct VDGetGammaListRec {
  uint32_t csPreviousGammaTableID; /* candidate for longest field name */
  uint32_t csGammaTableID;
  uint32_t csGammaTableSize;
  char* csGammaTableName; /* yes, that is a C-string */
};

struct VDRetrieveGammaRec {
  uint32_t csGammaTableID;
  GammaTbl* csGammaTablePtr;
};

#endif
