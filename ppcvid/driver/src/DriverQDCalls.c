#include "VideoDriverPrivate.h"
#include "VideoDriverPrototypes.h"
#include "DriverQDCalls.h"
#include "QemuVga.h"

/* Depth mode <-> bits-per-pixel mapping.
 *
 * When QEMU advertises the packed low-bpp feature (GLOBAL.lowDepthAvail)
 * we expose the full classic Mac ladder: B&W, 4, 16, 256, thousands and
 * millions of colors (kDepthMode1..6). On an older QEMU without the
 * feature we keep the historical 3-mode mapping (8/15/32) so saved depth
 * modes keep meaning what they used to. */

UInt8 DepthToDepthMode(UInt8 depth)
{
	if (GLOBAL.lowDepthAvail) {
		switch (depth) {
		case 1:
			return kDepthMode1;
		case 2:
			return kDepthMode2;
		case 4:
			return kDepthMode3;
		case 8:
			return kDepthMode4;
		case 15:
		case 16:
			return kDepthMode5;
		case 24:
		case 32:
			return kDepthMode6;
		default:
			return kDepthMode4;
		}
	}
	switch (depth) {
	case 8:
		return kDepthMode1;
	case 15:
	case 16:
		return kDepthMode2;
	case 24:
	case 32:
		return kDepthMode3;
	default:
		return kDepthMode1;
	}
}

UInt8 DepthModeToDepth(UInt8 mode)
{
	if (GLOBAL.lowDepthAvail) {
		switch (mode) {
		case kDepthMode1:
			return 1;
		case kDepthMode2:
			return 2;
		case kDepthMode3:
			return 4;
		case kDepthMode4:
			return 8;
		case kDepthMode5:
			return 15;
		case kDepthMode6:
			return 32;
		default:
			return 8;
		}
	}
	switch (mode) {
	case kDepthMode1:
		return 8;
	case kDepthMode2:
		return 15;
	case kDepthMode3:
		return 32;
	default:
		return 8;
	}
}

UInt8 MaxDepthMode(void)
{
	if (GLOBAL.lowDepthAvail)
		return kDepthMode6;
	return kDepthMode3;
}

/************************ Color Table Stuff ****************************/

static OSStatus
GraphicsCoreDoSetEntries(VDSetEntryRecord *entryRecord, Boolean directDevice, UInt32 start, UInt32 stop, Boolean useValue)
{
	UInt32 i;
	
	CHECK_OPEN( controlErr );
	if (GLOBAL.depth > 8)
		return controlErr;
	if (NULL == entryRecord->csTable)
		return controlErr;
	
	/* Note that stop value is included in the range */
	for(i=start;i<=stop;i++) {
		UInt32	colorIndex = useValue ? entryRecord->csTable[i].value : i;
		QemuVga_SetColorEntry(colorIndex, &entryRecord->csTable[i].rgb);
	}
	
	return noErr;
}

OSStatus
GraphicsCoreSetEntries(VDSetEntryRecord *entryRecord)
{
	Boolean useValue	= (entryRecord->csStart < 0);
	UInt32	start		= useValue ? 0UL : (UInt32)entryRecord->csStart;
	UInt32	stop		= start + entryRecord->csCount;

	Trace(GraphicsCoreSetEntries);

	return GraphicsCoreDoSetEntries(entryRecord, false, start, stop, useValue);
}
						
OSStatus
GraphicsCoreDirectSetEntries(VDSetEntryRecord *entryRecord)
{
	Boolean useValue	= (entryRecord->csStart < 0);
	UInt32	start		= useValue ? 0 : entryRecord->csStart;
	UInt32	stop		= start + entryRecord->csCount;

	Trace(GraphicsCoreDirectSetEntries);
	
	return GraphicsCoreDoSetEntries(entryRecord, true, start, stop, useValue);
}

OSStatus
GraphicsCoreGetEntries(VDSetEntryRecord *entryRecord)
{
	Boolean useValue	= (entryRecord->csStart < 0);
	UInt32	start		= useValue ? 0UL : (UInt32)entryRecord->csStart;
	UInt32	stop		= start + entryRecord->csCount;
	UInt32	i;
	
	Trace(GraphicsCoreGetEntries);

	if (GLOBAL.depth > 8)
		return controlErr;
	for(i=start;i<=stop;i++) {
		UInt32	colorIndex = useValue ? entryRecord->csTable[i].value : i;
		QemuVga_GetColorEntry(colorIndex, &entryRecord->csTable[i].rgb);
	}

	return noErr;
}

/************************ Gamma ****************************/

OSStatus
GraphicsCoreSetGamma(VDGammaRecord *gammaRec)
{
	CHECK_OPEN( controlErr );
		
	return noErr;
}

OSStatus
GraphicsCoreGetGammaInfoList(VDGetGammaListRec *gammaList)
{
	Trace(GraphicsCoreGammaInfoList);

	return statusErr;
}

OSStatus
GraphicsCoreRetrieveGammaTable(VDRetrieveGammaRec *gammaRec)
{
	Trace(GraphicsCoreRetrieveGammaTable);

	return statusErr;
}

OSStatus
GraphicsCoreGetGamma(VDGammaRecord *gammaRecord)
{
	CHECK_OPEN( statusErr );
		
	Trace(GraphicsCoreGetGamma);

	gammaRecord->csGTable = NULL;

	return noErr;
}


/************************ Gray pages ****************************/
			
OSStatus
GraphicsCoreGrayPage(VDPageInfo *pageInfo)
{
	UInt32 pageCount;

	CHECK_OPEN( controlErr );
		
	Trace(GraphicsCoreGrayPage);

	QemuVga_GetModePages(GLOBAL.curMode, GLOBAL.depth, NULL, &pageCount);
	if (pageInfo->csPage >= pageCount)
		return paramErr;
	
	/* XXX Make it gray ! */
	return noErr;
}
			
OSStatus
GraphicsCoreSetGray(VDGrayRecord *grayRecord)
{
	CHECK_OPEN( controlErr );
	
	Trace(GraphicsCoreSetGray);

	GLOBAL.qdLuminanceMapping	= grayRecord->csMode;
	return noErr;
}


OSStatus
GraphicsCoreGetPages(VDPageInfo *pageInfo)
{
	UInt32 pageCount, depth;

	CHECK_OPEN( statusErr );

	Trace(GraphicsCoreGetPages);

	depth = DepthModeToDepth(pageInfo->csMode);
	QemuVga_GetModePages(GLOBAL.curMode, depth, NULL, &pageCount);
	pageInfo->csPage = pageCount;

	return noErr;
}

			
OSStatus
GraphicsCoreGetGray(VDGrayRecord *grayRecord)
{
	CHECK_OPEN( statusErr );
		
	Trace(GraphicsCoreGetGray);
		
	grayRecord->csMode = (GLOBAL.qdLuminanceMapping);
	
	return noErr;
}

/************************ Hardware Cursor ****************************/

OSStatus
GraphicsCoreSupportsHardwareCursor(VDSupportsHardwareCursorRec *hwCursRec)
{
	CHECK_OPEN( statusErr );
		
	Trace(GraphicsCoreSupportsHardwareCursor);

	hwCursRec->csReserved1 = 0;
	hwCursRec->csReserved2 = 0;

	hwCursRec->csSupportsHardwareCursor = false;

	return noErr;
}

OSStatus
GraphicsCoreSetHardwareCursor(VDSetHardwareCursorRec *setHwCursRec)
{
	Trace(GraphicsCoreSetHardwareCursor);

	return controlErr;
}

OSStatus
GraphicsCoreDrawHardwareCursor(VDDrawHardwareCursorRec *drawHwCursRec)
{
	Trace(GraphicsCoreDrawHardwareCursor);

	return controlErr;
}

OSStatus
GraphicsCoreGetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *hwCursDStateRec)
{
	Trace(GraphicsCoreGetHardwareCursorDrawState);

	return statusErr;
}

/************************ Misc ****************************/

OSStatus
GraphicsCoreSetInterrupt(VDFlagRecord *flagRecord)
{
	CHECK_OPEN( controlErr );

	Trace(GraphicsCoreSetInterrupt);

	if (!flagRecord->csMode)
	    QemuVga_EnableInterrupts();
	else
	    QemuVga_DisableInterrupts();

	return noErr;
}

OSStatus
GraphicsCoreGetInterrupt(VDFlagRecord *flagRecord)
{
	Trace(GraphicsCoreGetInterrupt);

	CHECK_OPEN( statusErr );
		
	flagRecord->csMode = !GLOBAL.qdInterruptsEnable;
	return noErr;
}

OSStatus
GraphicsCoreSetSync(VDSyncInfoRec *syncInfo)
{
	UInt8 sync, mask;

	Trace(GraphicsCoreSetSync);

	CHECK_OPEN( controlErr );

	sync = syncInfo->csMode;
	mask = syncInfo->csFlags;	

	/* Unblank shortcut */
	if (sync == 0 && mask == 0) {
		sync = 0;
		mask = kDPMSSyncMask;
	}
	/* Blank shortcut */
	if (sync == 0xff && mask == 0xff) {
		sync = 0x7;
		mask = kDPMSSyncMask;
	}
	
	lprintf("SetSync req: sync=%x mask=%x\n", sync, mask);
	
	/* Only care about the DPMS mode */
	if ((mask & kDPMSSyncMask) == 0)
		return noErr;
	
	/* If any sync is disabled, blank */
	if (sync & kDPMSSyncMask)
		QemuVga_Blank(true);
	else
		QemuVga_Blank(false);

	return noErr;
}

OSStatus
GraphicsCoreGetSync(VDSyncInfoRec *syncInfo)
{
	Trace(GraphicsCoreGetSync);

	if (syncInfo->csMode == 0xff) {
		/* Return HW caps */
		syncInfo->csMode = (1 << kDisableHorizontalSyncBit) |
						   (1 << kDisableVerticalSyncBit) |
						   (1 << kDisableCompositeSyncBit) |
						   (1 << kNoSeparateSyncControlBit);
	} else if (syncInfo->csMode == 0x00){
		syncInfo->csMode = GLOBAL.blanked ? kDPMSSyncMask : 0;
	} else
		return statusErr;

	syncInfo->csFlags = 0;

	return noErr;
}

OSStatus
GraphicsCoreSetPowerState(VDPowerStateRec *powerStateRec)
{
	Trace(GraphicsCoreSetPowerState);

	return paramErr;
}

OSStatus
GraphicsCoreGetPowerState(VDPowerStateRec *powerStateRec)
{
	Trace(GraphicsCoreGetPowerState);

	return paramErr;
}
		
OSStatus
GraphicsCoreSetPreferredConfiguration(VDSwitchInfoRec *switchInfo)
{
	Trace(GraphicsCoreSetPreferredConfiguration);

	CHECK_OPEN( controlErr );
	
	return noErr;
}


OSStatus
GraphicsCoreGetPreferredConfiguration(VDSwitchInfoRec *switchInfo)
{
	Trace(GraphicsCoreGetPreferredConfiguration);

	CHECK_OPEN( statusErr );

	/* While a host window resize is pending, report the retargeted dynamic
	 * mode as our preferred configuration so the Display Manager's re-probe
	 * (triggered by the VSL connect service) lands on it. */
	if (GLOBAL.hostPendingMode != 0) {
		switchInfo->csMode		= DepthToDepthMode(GLOBAL.depth);
		switchInfo->csData		= GLOBAL.hostPendingMode;
		switchInfo->csPage		= 0;
		switchInfo->csBaseAddr	= FB_START;
		return noErr;
	}

	switchInfo->csMode 	 	= DepthToDepthMode(GLOBAL.bootDepth);
	switchInfo->csData		= GLOBAL.bootMode + 1; /* Modes are 1 based */
	switchInfo->csPage		= 0;
	switchInfo->csBaseAddr	= FB_START;

	return noErr;
}

// ?***************** Misc status calls *********************/

OSStatus
GraphicsCoreGetBaseAddress(VDPageInfo *pageInfo)
{
	UInt32 pageCount, pageSize;

	Trace(GraphicsCoreGetBaseAddress);

	CHECK_OPEN( statusErr );

	QemuVga_GetModePages(GLOBAL.curMode, GLOBAL.depth, &pageSize, &pageCount);
	if (pageInfo->csPage >= pageCount)
		return paramErr;
		
	pageInfo->csBaseAddr = FB_START + pageInfo->csPage * pageSize;

	return noErr;
}
			
OSStatus
GraphicsCoreGetConnection(VDDisplayConnectInfoRec *connectInfo)
{
	Trace(GraphicsCoreGetConnection);

	CHECK_OPEN( statusErr );
		
	connectInfo->csDisplayType			= kVGAConnect;
	connectInfo->csConnectTaggedType	= 0;
	connectInfo->csConnectTaggedData	= 0;

	connectInfo->csConnectFlags		=
		(1 << kTaggingInfoNonStandard) | (1 << kUncertainConnection);

	/* Host-resize: we report connection changes through the VSL connect
	 * service so the Display Manager re-probes us when the QEMU window is
	 * resized (the same path used for real monitor hot-plugging). */
	if (GLOBAL.hostResizeAvail)
		connectInfo->csConnectFlags |= (1 << kReportsHotPlugging);

	connectInfo->csDisplayComponent		= 0;
	
	return noErr;
}

OSStatus
GraphicsCoreGetMode(VDPageInfo *pageInfo)
{
	Trace(GraphicsCoreGetMode);

	CHECK_OPEN( statusErr );
	
	pageInfo->csMode		= DepthToDepthMode(GLOBAL.depth);
	pageInfo->csPage		= GLOBAL.curPage;
	pageInfo->csBaseAddr	= GLOBAL.curBaseAddress;

	return noErr;
}

OSStatus
GraphicsCoreGetCurrentMode(VDSwitchInfoRec *switchInfo)
{
	Trace(GraphicsCoreGetCurrentMode);

	CHECK_OPEN( statusErr );
	
	//lprintf("GetCurrentMode\n");
	switchInfo->csMode		= DepthToDepthMode(GLOBAL.depth);
	switchInfo->csData		= GLOBAL.curMode + 1;
	switchInfo->csPage		= GLOBAL.curPage;
	switchInfo->csBaseAddr	= GLOBAL.curBaseAddress;

	return noErr;
}

/********************** Video mode *****************************/
						
OSStatus
GraphicsCoreGetModeTiming(VDTimingInfoRec *timingInfo)
{
	Trace(GraphicsCoreGetModeTiming);

	CHECK_OPEN( statusErr );

	if (timingInfo->csTimingMode < 1 || timingInfo->csTimingMode > GLOBAL.numModes )
		return paramErr;

	if (GLOBAL.hostResizeAvail && timingInfo->csTimingMode > GLOBAL.numBaseModes) {
		/* Dynamic (host-resize) modes: valid and safe, but hidden from the
		 * Monitors panel; flagged as the default while their switch is
		 * pending so a Display Manager re-probe prefers them. */
		timingInfo->csTimingFlags = (1 << kModeValid) | (1 << kModeSafe);
		if (GLOBAL.hostPendingMode == timingInfo->csTimingMode)
			timingInfo->csTimingFlags |= (1 << kModeDefault) | (1 << kModeShowNow);
		else
			timingInfo->csTimingFlags |= (1 << kModeShowNever);
	} else {
		timingInfo->csTimingFlags =
			(1 << kModeValid) | (1 << kModeDefault) | (1 <<kModeSafe);
		/* While a host window resize is pending, report every other mode
		 * as not valid. The Display Manager's connect-change re-probe
		 * otherwise revalidates its *saved* preference (usually the mode
		 * we are already in) and stops without ever switching, so the
		 * window drag would be ignored on any system that has a saved
		 * Display Preferences entry. With the old mode invalid, the
		 * re-probe falls through to GetPreferredConfiguration, which
		 * names the pending window-sized mode. The flags return to
		 * normal as soon as the switch lands (cscSwitchMode clears the
		 * pending mode), so the Monitors panel is unaffected. */
		if (GLOBAL.hostPendingMode != 0 &&
		    GLOBAL.hostPendingMode != timingInfo->csTimingMode)
			timingInfo->csTimingFlags = 0;
	}

	timingInfo->csTimingFormat	= kDeclROMtables;
	timingInfo->csTimingData	= timingVESA_640x480_60hz;

	return noErr;
}


OSStatus
GraphicsCoreSetMode(VDPageInfo *pageInfo)
{
	UInt32 newDepth, newPage, pageCount;

	Trace(GraphicsCoreSetMode);

	CHECK_OPEN(controlErr);

	newDepth = DepthModeToDepth(pageInfo->csMode);
	newPage = pageInfo->csPage;
	QemuVga_GetModePages(GLOBAL.curMode, newDepth, NULL, &pageCount);

	lprintf("Requested depth=%d page=%d\n", newDepth, newPage);
	if (pageInfo->csPage >= pageCount)
		return paramErr;
	
	if (newDepth != GLOBAL.depth || newPage != GLOBAL.curPage)
		QemuVga_SetMode(GLOBAL.curMode, newDepth, newPage);
	
	pageInfo->csBaseAddr = GLOBAL.curBaseAddress;
	lprintf("Returning BA: %lx\n", pageInfo->csBaseAddr);

	return noErr;
}			


OSStatus
GraphicsCoreSwitchMode(VDSwitchInfoRec *switchInfo)
{
	UInt32 newMode, newDepth, newPage, pageCount;

	Trace(GraphicsCoreSwitchMode);

	CHECK_OPEN(controlErr);
	
	newMode = switchInfo->csData - 1;
	newDepth = DepthModeToDepth(switchInfo->csMode);
	newPage = switchInfo->csPage;
	QemuVga_GetModePages(GLOBAL.curMode, newDepth, NULL, &pageCount);

	if (newPage >= pageCount)
		return paramErr;

	if (newMode != GLOBAL.curMode || newDepth != GLOBAL.depth ||
	    newPage != GLOBAL.curPage) {
		if (QemuVga_SetMode(newMode, newDepth, newPage))
			return controlErr;
	}
	switchInfo->csBaseAddr = GLOBAL.curBaseAddress;

	/* A pending host-resize mode has been applied (or the user switched to
	 * something else entirely): stop advertising it as preferred. */
	GLOBAL.hostPendingMode = 0;

	return noErr;
}

OSStatus
GraphicsCoreGetNextResolution(VDResolutionInfoRec *resInfo)
{
	UInt32 width, height;
	int id = resInfo->csPreviousDisplayModeID;

	Trace(GraphicsCoreGetNextResolution);

	CHECK_OPEN(statusErr);

	if (id == kDisplayModeIDFindFirstResolution)
		id = 0;
	else if (id == kDisplayModeIDCurrent)
		id = GLOBAL.curMode;
	id++;
	
	if (id == GLOBAL.numModes + 1) {
		resInfo->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
		return noErr;
	}
	if (id < 1 || id > GLOBAL.numModes)
		return paramErr;
	
	if (QemuVga_GetModeInfo(id - 1, &width, &height))
		return paramErr;

	resInfo->csDisplayModeID	= id;
	resInfo->csHorizontalPixels	= width;
	resInfo->csVerticalLines	= height;
	resInfo->csRefreshRate		= 60;
	resInfo->csMaxDepthMode		= MaxDepthMode(); /* XXX Calculate if it fits ! */

	return noErr;
}

// Looks quite a bit hard-coded, isn't it ?
OSStatus
GraphicsCoreGetVideoParams(VDVideoParametersInfoRec *videoParams)
{
	UInt32 width, height, depth, rowBytes, pageCount;
	OSStatus err = noErr;
	
	Trace(GraphicsCoreGetVideoParams);

	CHECK_OPEN(statusErr);
 		
	if (videoParams->csDisplayModeID < 1 || videoParams->csDisplayModeID > GLOBAL.numModes)
		return paramErr;
	if (videoParams->csDepthMode > MaxDepthMode())
		return paramErr;
	if (QemuVga_GetModeInfo(videoParams->csDisplayModeID - 1, &width, &height))
		return paramErr;
	
	depth = DepthModeToDepth(videoParams->csDepthMode);
	QemuVga_GetModePages(videoParams->csDisplayModeID - 1, depth, NULL, &pageCount);
	videoParams->csPageCount = pageCount;
	lprintf("Video Params says %d pages\n", pageCount);
	
	rowBytes = (width * depth + 7) / 8;
	(videoParams->csVPBlockPtr)->vpBaseOffset 		= 0;			// For us, it's always 0
	(videoParams->csVPBlockPtr)->vpBounds.top 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpBounds.left 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpVersion 			= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpPackType 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpPackSize 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpHRes 			= 0x00480000;	// Hard coded to 72 dpi
	(videoParams->csVPBlockPtr)->vpVRes 			= 0x00480000;	// Hard coded to 72 dpi
	(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpBounds.bottom	= height;
	(videoParams->csVPBlockPtr)->vpBounds.right		= width;
	(videoParams->csVPBlockPtr)->vpRowBytes			= rowBytes;

	switch (depth) {
	case 1:
	case 2:
	case 4:
		videoParams->csDeviceType 						= clutType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 0;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= depth;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 1;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= depth;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	case 8:
		videoParams->csDeviceType 						= clutType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 0;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 8;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 1;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 8;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	case 15:
	case 16:
		videoParams->csDeviceType 						= directType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 16;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 16;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 3;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 5;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	case 32:
		videoParams->csDeviceType 						= directType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 16;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 32;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 3;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 8;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	default:
		err = paramErr;
		break;
	}

	return err;
}
