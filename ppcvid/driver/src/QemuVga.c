#include "VideoDriverPrivate.h"
#include "VideoDriverPrototypes.h"
#include "DriverQDCalls.h"
#include "GammaContract.h"
#include "ModeContract.h"
#include "QemuVga.h"
#include <Timer.h>

typedef char QemuVgaNativeGammaDataOffsetMustMatch[
	(offsetof(GammaTbl, gFormulaData) == QEMU_VGA_GAMMA_DATA_OFFSET) ? 1 : -1];
typedef char QemuVgaNativeGammaHeaderSizeMustMatch[
	(sizeof(GammaTbl) == QEMU_VGA_GAMMA_COMPAT_HEADER_BYTES) ? 1 : -1];

/*
static struct _vMode defaultVModes[] =  {
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 1600, 1200 },
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 0, 0 }
};
*/
static void VgaWriteB(UInt16 port, UInt8 val)
{
	UInt8 *ptr;
	
	ptr = (UInt8 *)((UInt32)GLOBAL.boardRegAddress + port + 0x400 - 0x3c0);
	*ptr = val;
	SynchronizeIO();
}

static UInt8 VgaReadB(UInt16 port)
{
	UInt8 *ptr, val;
	
	ptr = (UInt8 *)((UInt32)GLOBAL.boardRegAddress + port + 0x400 - 0x3c0);
	val = *ptr;
	SynchronizeIO();
	return val;
}

static void DispiWriteW(UInt16 reg, UInt16 val)
{
	UInt16 *ptr;
	
	ptr = (UInt16 *)((UInt32)GLOBAL.boardRegAddress + (reg << 1) + 0x500);
	*ptr = EndianSwap16Bit(val);
	SynchronizeIO();
}

static UInt16 DispiReadW(UInt16 reg)
{
	UInt16 *ptr, val;
	
	ptr = (UInt16 *)((UInt32)GLOBAL.boardRegAddress + (reg << 1) + 0x500);
	val = EndianSwap16Bit(*ptr);
	SynchronizeIO();
	return val;
}

static void ExtWriteL(UInt16 reg, UInt32 val)
{
	UInt32 *ptr;
	
	ptr = (UInt32 *)((UInt32)GLOBAL.boardRegAddress + (reg << 2) + 0x600);
	*ptr = EndianSwap32Bit(val);
	SynchronizeIO();
}

static UInt32 ExtReadL(UInt32 reg)
{
	UInt32 *ptr, val;
	
	ptr = (UInt32 *)((UInt32)GLOBAL.boardRegAddress + (reg << 2) + 0x600);
	val = EndianSwap32Bit(*ptr);
	SynchronizeIO();
	return val;
}

UInt32 QemuVga_ReadExt(UInt32 reg)
{
	return ExtReadL(reg);
}

static UInt32 QemuVga_ReadGammaValue(const UInt8 *source, UInt32 bytes)
{
	UInt32 value = 0;
	UInt32 i;

	for (i = 0; i < bytes; i++)
		value = (value << 8) | source[i];
	return value;
}

static UInt8 QemuVga_ScaleGammaValue(UInt32 value, UInt32 bits)
{
	UInt32 maximum;

	/* Gamma entries wider than one byte are full-range fixed values. Keep
	 * their most significant eight bits; this is exact for the common 16-bit
	 * tables and avoids pulling 64-bit division into the small NDRV. */
	if (bits > 8)
		return (UInt8)(value >> (bits - 8));
	maximum = (1UL << bits) - 1UL;
	if (value > maximum)
		value = maximum;
	return (UInt8)((value * 255UL + maximum / 2UL) / maximum);
}

static QemuVgaGammaStorage gCurrentGamma;
static Boolean gCurrentGammaInitialized;

static void QemuVga_InitializeGamma(void)
{
	if (gCurrentGammaInitialized)
		return;
	QemuVgaGammaInitializeIdentity(&gCurrentGamma);
	gCurrentGammaInitialized = TRUE;
}

GammaTbl *QemuVga_GetGamma(void)
{
	QemuVga_InitializeGamma();
	return (GammaTbl *)&gCurrentGamma;
}

OSStatus QemuVga_SetGamma(const GammaTbl *table)
{
	const UInt8 *data;
	UInt8 *current;
	UInt32 channels;
	UInt32 count;
	UInt32 bits;
	UInt32 bytes;
	UInt32 i;

	if (!GLOBAL.gammaLUTAvail)
		return controlErr;
	if (table == NULL || table->gFormulaSize < 0 || table->gChanCnt < 1 ||
		table->gChanCnt > 3 || (table->gChanCnt != 1 && table->gChanCnt != 3) ||
		table->gDataCnt < 2 || table->gDataCnt > 4096 ||
		table->gDataWidth < 1 || table->gDataWidth > 32)
		return paramErr;

	channels = (UInt32)table->gChanCnt;
	count = (UInt32)table->gDataCnt;
	bits = (UInt32)table->gDataWidth;
	bytes = bits <= 8 ? 1 : (bits <= 16 ? 2 : 4);
	data = (const UInt8 *)table->gFormulaData +
		(UInt32)table->gFormulaSize;
	QemuVga_InitializeGamma();
	current = (UInt8 *)gCurrentGamma.gFormulaData;

	ExtWriteL(QEMU_EXT_REG_GAMMA_INDEX, 0);
	for (i = 0; i < 256; i++) {
		UInt32 sourceIndex = (i * (count - 1) + 127) / 255;
		UInt32 redOffset = sourceIndex * bytes;
		UInt32 greenOffset = channels == 3 ?
			(count + sourceIndex) * bytes : redOffset;
		UInt32 blueOffset = channels == 3 ?
			(2 * count + sourceIndex) * bytes : redOffset;
		UInt32 red = QemuVga_ScaleGammaValue(
			QemuVga_ReadGammaValue(data + redOffset, bytes), bits);
		UInt32 green = QemuVga_ScaleGammaValue(
			QemuVga_ReadGammaValue(data + greenOffset, bytes), bits);
		UInt32 blue = QemuVga_ScaleGammaValue(
			QemuVga_ReadGammaValue(data + blueOffset, bytes), bits);

		current[i] = (UInt8)red;
		current[256 + i] = (UInt8)green;
		current[512 + i] = (UInt8)blue;
		ExtWriteL(QEMU_EXT_REG_GAMMA_VALUE,
			(red << 16) | (green << 8) | blue);
	}
	ExtWriteL(QEMU_EXT_REG_GAMMA_COMMAND, QEMU_EXT_GAMMA_APPLY);
	lprintf("QEMU display gamma table applied (%lu channels, %lu entries, %lu-bit)\n",
		channels, count, bits);
	return noErr;
}

void QemuVga_SetCursor(UInt32 *argb)
{
	UInt32 i;

	if (!GLOBAL.hardwareCursorAvail)
		return;

	for (i = 0; i < QEMU_EXT_CURSOR_PIXELS; i++)
		ExtWriteL(QEMU_EXT_CURSOR_DATA_REG + i, argb[i]);
	ExtWriteL(QEMU_EXT_REG_CURSOR_WIDTH, QEMU_EXT_CURSOR_WIDTH);
	ExtWriteL(QEMU_EXT_REG_CURSOR_HEIGHT, QEMU_EXT_CURSOR_HEIGHT);
	ExtWriteL(QEMU_EXT_REG_CURSOR_HOT_X, 0);
	ExtWriteL(QEMU_EXT_REG_CURSOR_HOT_Y, 0);
	ExtWriteL(QEMU_EXT_REG_CURSOR_COMMAND, QEMU_EXT_CURSOR_DEFINE);
}

void QemuVga_DrawCursor(SInt32 x, SInt32 y, Boolean visible)
{
	if (!GLOBAL.hardwareCursorAvail)
		return;

	ExtWriteL(QEMU_EXT_REG_CURSOR_X, (UInt32)x);
	ExtWriteL(QEMU_EXT_REG_CURSOR_Y, (UInt32)y);
	ExtWriteL(QEMU_EXT_REG_CURSOR_VISIBLE, visible ? 1 : 0);
	ExtWriteL(QEMU_EXT_REG_CURSOR_COMMAND, QEMU_EXT_CURSOR_MOVE);
}

static OSStatus VBLTimerProc(void *p1, void *p2);
static void QemuVga_PollHostResize(void);

/* The two retargetable "dynamic" modes used to follow the host window. They
 * sit at the end of the mode list so their 1-based display mode IDs stay
 * stable (numBaseModes+1 and numBaseModes+2); we alternate between them so
 * the Display Manager always sees a mode *change*. */
static struct _vMode dynVModes[2];

#ifndef USE_DSL_TIMER
static TMTask gLegacyTimer;

static pascal void legacyTimerCB(TMTaskPtr *inTask)
{
	VBLTimerProc(NULL, NULL);
}

static const RoutineDescriptor	gLegacyTimerDesc	= BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, legacyTimerCB);
static const TimerUPP			gLegacyTimerProc	= (TimerUPP) &gLegacyTimerDesc;
static int gTimerInstalled;

static OSStatus ScheduleVBLTimer(void)
{
	if (!gTimerInstalled) {
		BlockZero(&gLegacyTimer, sizeof(gLegacyTimer));
		gLegacyTimer.tmAddr = gLegacyTimerProc;
		gLegacyTimer.qLink = (QElemPtr)'eada';
		InsXTime((QElemPtr)&gLegacyTimer);
		gTimerInstalled = true;
	}
	PrimeTime((QElemPtr)&gLegacyTimer, TIMER_DURATION);
	return noErr;
}

#else

static OSStatus ScheduleVBLTimer(void)
{
	AbsoluteTime target = AddDurationToAbsolute(TIMER_DURATION, UpTime());
	return SetInterruptTimer(&target, VBLTimerProc, NULL, &GLOBAL.VBLTimerID);
}

#endif

static OSStatus VBLTimerProc(void *p1, void *p2)
{
	GLOBAL.inInterrupt = 1;

	/* This can be called before the service is ready */
	if (GLOBAL.qdVBLInterrupt && GLOBAL.qdInterruptsEnable)
		VSLDoInterruptService(GLOBAL.qdVBLInterrupt);

	/* Follow the host window: poll the resize-request serial and, once a
	 * request settles, nudge the Display Manager through the connect
	 * interrupt service. Everything here is interrupt-safe. */
	QemuVga_PollHostResize();

	/* Reschedule */
	ScheduleVBLTimer();

	GLOBAL.inInterrupt = 0;
	return noErr;
}

/* Retarget one of the dynamic modes to the requested size and return its
 * 1-based display mode ID. Returns 0 when there is nothing to do (request
 * matches the current mode) picking an exact standard mode when one exists.
 * Interrupt-safe: no allocations, only writes to preallocated structures. */
UInt32 QemuVga_PrepareHostModeSwitch(UInt32 width, UInt32 height)
{
	struct _vMode *cur;
	UInt32 slot, id, i;

	if (!GLOBAL.hostResizeAvail || !GLOBAL.isOpen)
		return 0;

	/* Clamp to the hardware envelope: never below the window's minimum
	 * content size, never larger than the framebuffer BAR can hold at
	 * 32bpp (so any depth the user picks later still fits). The width is
	 * rounded down to a multiple of 8 because the DISPI interface does the
	 * same; without this the rowbytes we report and the stride QEMU scans
	 * out would disagree (visibly so at the packed sub-byte depths). */
	width &= ~(UInt32)7;
	if (width < HOST_RESIZE_MIN_WIDTH)
		width = HOST_RESIZE_MIN_WIDTH;
	if (height < HOST_RESIZE_MIN_HEIGHT)
		height = HOST_RESIZE_MIN_HEIGHT;
	while (width * height * 4 > GLOBAL.boardFBMappedSize && height > HOST_RESIZE_MIN_HEIGHT)
		height--;
	if (width * height * 4 > GLOBAL.boardFBMappedSize)
		return 0;

	/* Already showing exactly the requested size? Nothing to do. */
	cur = getVMode(GLOBAL.curMode);
	if (cur && cur->width == width && cur->height == height)
		return 0;

	/* Prefer an exact standard mode so Monitors shows a familiar entry. */
	for (i = 0; i < GLOBAL.numBaseModes; i++) {
		struct _vMode *m = getVMode(i);
		if (m && m->width == width && m->height == height)
			return i + 1;
	}

	/* Otherwise retarget the spare dynamic mode and alternate slots so the
	 * Display Manager treats each request as a real mode change. */
	slot = GLOBAL.dynToggle & 1;
	if (GLOBAL.curMode == GLOBAL.numBaseModes + slot)
		slot ^= 1;
	GLOBAL.dynToggle = slot ^ 1;
	dynVModes[slot].width = width;
	dynVModes[slot].height = height;
	id = GLOBAL.numBaseModes + slot + 1;
	return id;
}

/* Poll the host resize request from the pseudo-VBL tick. When a request has
 * held steady for HOST_RESIZE_DEBOUNCE_TICKS we publish the target mode in
 * GLOBAL.hostPendingMode and fire the VSL connect-change service, which asks
 * the Display Manager to re-probe this display; the re-probe finds the
 * pending mode as our preferred/default configuration and switches to it. */
static void QemuVga_PollHostResize(void)
{
	UInt32 serial, width, height, id;

	if (!GLOBAL.hostResizeAvail || !GLOBAL.isOpen)
		return;

	serial = ExtReadL(QEMU_EXT_REG_REQ_SERIAL);
	if (serial == GLOBAL.lastReqSerial) {
		/* Nothing new since the request we last acted on. If a pending
		 * mode was never adopted (the Display Manager re-probe didn't
		 * land, e.g. no Display Manager running), stop advertising it -
		 * and stop hiding the other modes in GetModeTiming - after a
		 * few seconds, so the Monitors panel returns to normal. */
		if (GLOBAL.hostPendingMode != 0) {
			if (GLOBAL.hostPendingTicks < HOST_RESIZE_PENDING_TIMEOUT_TICKS)
				GLOBAL.hostPendingTicks++;
			else
				GLOBAL.hostPendingMode = 0;
		}
		return;
	}

	/* Debounce: act only once the serial has held steady for a couple of
	 * ticks, so a drag that publishes several sizes results in a single
	 * Display Manager switch at the end. */
	if (serial != GLOBAL.pendingReqSerial) {
		GLOBAL.pendingReqSerial = serial;
		GLOBAL.pendingReqTicks = 0;
		return;
	}
	if (GLOBAL.pendingReqTicks < HOST_RESIZE_DEBOUNCE_TICKS) {
		GLOBAL.pendingReqTicks++;
		return;
	}

	GLOBAL.lastReqSerial = serial; /* consider this request handled */

	width = ExtReadL(QEMU_EXT_REG_REQ_WIDTH);
	height = ExtReadL(QEMU_EXT_REG_REQ_HEIGHT);
	if (width == 0 || height == 0)
		return;

	id = QemuVga_PrepareHostModeSwitch(width, height);
	if (id == 0)
		return;

	GLOBAL.hostPendingMode = id;
	GLOBAL.hostPendingTicks = 0;
	if (GLOBAL.qdConnectInterrupt)
		VSLDoInterruptService(GLOBAL.qdConnectInterrupt);
}

#ifdef USE_PCI_IRQ
static InterruptMemberNumber PCIInterruptHandler(InterruptSetMember ISTmember,
												 void *refCon, UInt32 theIntCount)
{
	UInt32 reg;
	
	reg = ExtReadL(2);
	if (!(reg & 1))
		return kIsrIsNotComplete;
	if (GLOBAL.qdVBLInterrupt && GLOBAL.qdInterruptsEnable)
		VSLDoInterruptService(GLOBAL.qdVBLInterrupt);
	ExtWriteL(2, 3);
	return kIsrIsComplete;
}
#endif


OSStatus QemuVga_Init(void)
{
	UInt16 id, i;
	UInt32 mem, width, height, depth;
	Boolean modeFound = false;
	struct vMode *v;

	lprintf("First MMIO read...\n");
	id = DispiReadW(VBE_DISPI_INDEX_ID);
	mem = DispiReadW(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
	mem <<= 16;
	lprintf("DISPI_ID=%04x VMEM=%d Mb\n", id, mem >> 20);
	if ((id & 0xfff0) != VBE_DISPI_ID0) {
		lprintf("Unsupported ID !\n");
		return controlErr;
	}
	if (mem > GLOBAL.boardFBMappedSize)
		mem = GLOBAL.boardFBMappedSize;
	GLOBAL.vramSize = mem;
	
	// XXX Add endian control regs

	width = DispiReadW(VBE_DISPI_INDEX_XRES);
	height = DispiReadW(VBE_DISPI_INDEX_YRES);
	depth = DispiReadW(VBE_DISPI_INDEX_BPP);
	lprintf("Current setting: %dx%dx%d\n", width, height, depth);

	GLOBAL.depth = GLOBAL.bootDepth = depth;
	GLOBAL.numBaseModes = QemuVga_ReadEdidModes();
	lprintf("Number of modes: %d\n", GLOBAL.numBaseModes);

	/* Host-window-driven live resizing: probe for the request channel in
	 * the QEMU extended registers and, when present, append the two
	 * retargetable dynamic modes at stable IDs past the standard list. */
	GLOBAL.hostResizeAvail =
		(GLOBAL.boardRegMappedSize >= 0x600 + QEMU_EXT_SIZE_HOST_RESIZE) &&
		(ExtReadL(QEMU_EXT_REG_SIZE) >= QEMU_EXT_SIZE_HOST_RESIZE);

	/* Packed 1/2/4-bpp indexed modes: probe the feature bitmap register
	 * (only read it once the reported region size says it exists). */
	GLOBAL.lowDepthAvail =
		(GLOBAL.boardRegMappedSize >= 0x600 + QEMU_EXT_SIZE_FEATURES) &&
		(ExtReadL(QEMU_EXT_REG_SIZE) >= QEMU_EXT_SIZE_FEATURES) &&
		((ExtReadL(QEMU_EXT_REG_FEATURES) & QEMU_EXT_FEATURE_PACKED_LOWBPP) != 0);
	if (GLOBAL.lowDepthAvail)
		lprintf("QEMU packed low-depth modes detected\n");
	GLOBAL.hardwareCursorAvail =
		(GLOBAL.boardRegMappedSize >= 0x600 + QEMU_EXT_SIZE_CURSOR) &&
		(ExtReadL(QEMU_EXT_REG_SIZE) >= QEMU_EXT_SIZE_CURSOR) &&
		((ExtReadL(QEMU_EXT_REG_FEATURES) & QEMU_EXT_FEATURE_HARDWARE_CURSOR) != 0);
	GLOBAL.gammaLUTAvail =
		(GLOBAL.boardRegMappedSize >= 0x600 + QEMU_EXT_SIZE_CURSOR) &&
		(ExtReadL(QEMU_EXT_REG_SIZE) >= QEMU_EXT_SIZE_CURSOR) &&
		((ExtReadL(QEMU_EXT_REG_FEATURES) & QEMU_EXT_FEATURE_GAMMA_LUT) != 0);
	GLOBAL.cursorSet = false;
	GLOBAL.cursorVisible = false;
	GLOBAL.cursorX = 0;
	GLOBAL.cursorY = 0;
	if (GLOBAL.hardwareCursorAvail)
		lprintf("QEMU hardware cursor detected\n");
	if (GLOBAL.gammaLUTAvail)
		lprintf("QEMU display gamma table detected\n");
	GLOBAL.numModes = GLOBAL.numBaseModes;
	if (GLOBAL.hostResizeAvail) {
		lprintf("QEMU host-resize channel detected\n");
		dynVModes[0].width = width;
		dynVModes[0].height = height;
		dynVModes[1].width = width;
		dynVModes[1].height = height;
		appendVModeToList(&dynVModes[0]);
		appendVModeToList(&dynVModes[1]);
		GLOBAL.numModes += 2;
		GLOBAL.lastReqSerial = ExtReadL(QEMU_EXT_REG_REQ_SERIAL);
		GLOBAL.pendingReqSerial = GLOBAL.lastReqSerial;
		GLOBAL.pendingReqTicks = 0;
		GLOBAL.dynToggle = 0;
		GLOBAL.hostPendingMode = 0;
		GLOBAL.hostPendingTicks = 0;
	}

	for (i = 0, v = vModes; v != NULL; v = v->next, i++) {
		if (width == v->mode->width && height == v->mode->height) {
		    modeFound = true;
			break;
		}
	}

	if (!modeFound) {
		lprintf("Not found in list ! using default.\n");
		i = 0;
	} else {
	    lprintf("Using mode: %d\n", i);
	}
	GLOBAL.bootMode = i;

	QemuVga_SetMode(GLOBAL.bootMode, depth, 0);

#ifdef USE_PCI_IRQ
	if (SetupPCIInterrupt(&GLOBAL.deviceEntry, &GLOBAL.irqInfo,
					   	  PCIInterruptHandler, NULL) == noErr)
		GLOBAL.hasPCIInterrupt = true;
	else
#else
	GLOBAL.hasPCIInterrupt = false;
#endif
	return noErr;
}

OSStatus QemuVga_Open(void)
{
	lprintf("QemuVga v1.00\n");

	GLOBAL.isOpen = true;

	if (GLOBAL.hasPCIInterrupt) {
		QemuVga_EnableInterrupts();
		lprintf("VBL registered using PCI interrupts\n");	
	} else {
		/* Schedule the timer now if timers are supported. They aren't on OS X
		 * in which case we must not create the VSL service, otherwise OS X will expect
		 * a VBL and fail to update the cursor when not getting one.
	 	*/
		lprintf("Testing using timer to simulate VBL..\n");	
		GLOBAL.hasTimer = (ScheduleVBLTimer() == noErr);
		GLOBAL.qdInterruptsEnable = GLOBAL.hasTimer;

		if (GLOBAL.hasTimer)
			lprintf("Using timer to simulate VBL.\n");	
		else
			lprintf("No timer service (OS X ?), VBL not registered.\n");	

	}

	/* Create VBL if we have a PCI interrupt or timer works */
	if (GLOBAL.hasPCIInterrupt || GLOBAL.hasTimer)
		VSLNewInterruptService(&GLOBAL.deviceEntry, kVBLInterruptServiceType, &GLOBAL.qdVBLInterrupt);

	/* Connect-change service used to make the Display Manager re-probe us
	 * when the host window is resized (needs the timer/IRQ to poll). */
	if (GLOBAL.hostResizeAvail && (GLOBAL.hasPCIInterrupt || GLOBAL.hasTimer))
		VSLNewInterruptService(&GLOBAL.deviceEntry, kFBConnectInterruptServiceType,
							   &GLOBAL.qdConnectInterrupt);

	return noErr;
}

OSStatus QemuVga_Close(void)
{
	lprintf("Closing Driver...\n");

	if (GLOBAL.hardwareCursorAvail)
		QemuVga_DrawCursor(GLOBAL.cursorX, GLOBAL.cursorY, false);
	GLOBAL.isOpen = false;
	
	QemuVga_DisableInterrupts();
	if (GLOBAL.qdVBLInterrupt)
		VSLDisposeInterruptService( GLOBAL.qdVBLInterrupt );
	GLOBAL.qdVBLInterrupt = 0;
	if (GLOBAL.qdConnectInterrupt)
		VSLDisposeInterruptService( GLOBAL.qdConnectInterrupt );
	GLOBAL.qdConnectInterrupt = 0;

	return noErr;
}

OSStatus QemuVga_Exit(void)
{
	QemuVga_Close();

	return noErr;
}

void QemuVga_EnableInterrupts(void)
{
	GLOBAL.qdInterruptsEnable = true;
	if (GLOBAL.hasTimer)
		ScheduleVBLTimer();
	else if (GLOBAL.hasPCIInterrupt) {
		GLOBAL.irqInfo.enableFunction(GLOBAL.irqInfo.interruptSetMember, GLOBAL.irqInfo.refCon);
		ExtWriteL(2, 3);
	}
}

void QemuVga_DisableInterrupts(void)
{
	AbsoluteTime remaining;

	GLOBAL.qdInterruptsEnable = false;
	if (GLOBAL.hasTimer)
		CancelTimer(GLOBAL.VBLTimerID, &remaining);
	else if (GLOBAL.hasPCIInterrupt) {
		ExtWriteL(2, 1);
		GLOBAL.irqInfo.disableFunction(GLOBAL.irqInfo.interruptSetMember, GLOBAL.irqInfo.refCon);
	}
}

OSStatus QemuVga_SetColorEntry(UInt32 index, RGBColor *color)
{
	//lprintf("SetColorEntry %d, %x %x %x\n", index, color->red, color->green, color->blue);
	VgaWriteB(0x3c8, index);
	VgaWriteB(0x3c9, color->red >> 8);
	VgaWriteB(0x3c9, color->green >> 8);
	VgaWriteB(0x3c9, color->blue >> 8);
	return noErr;
}

OSStatus QemuVga_GetColorEntry(UInt32 index, RGBColor *color)
{
	UInt32 r,g,b;
	
	VgaWriteB(0x3c7, index);
	r = VgaReadB(0x3c9);
	g = VgaReadB(0x3c9);
	b = VgaReadB(0x3c9);
	color->red = (r << 8) | r;
	color->green = (g << 8) | g;
	color->blue = (b << 8) | b;

	return noErr;
}

OSStatus QemuVga_GetModeInfo(UInt32 index, UInt32 *width, UInt32 *height)
{
	if (index >= GLOBAL.numModes)
		return paramErr;
	if (width)
		*width = getVMode(index)->width;
	if (height)
		*height = getVMode(index)->height;
	return noErr;
}

UInt32 QemuVga_GetRowBytes(UInt32 width, UInt32 depth)
{
	UInt32 rowBytes;

	if (!QemuVgaCalculatePageLayout(width, 1, depth, ~0UL,
									 &rowBytes, NULL, NULL))
		return 0;
	return rowBytes;
}

OSStatus QemuVga_GetModePages(UInt32 index, UInt32 depth,
							  UInt32 *pageSize, UInt32 *pageCount)
{
	UInt32 width, height, rowBytes, pBytes, capacity;
	struct _vMode *vMode;

	if (index >= GLOBAL.numModes)
		return paramErr;
	vMode = getVMode(index);
	if (vMode == NULL)
		return paramErr;
	width = vMode->width;
	height = vMode->height;
	capacity = GLOBAL.vramSize;
	if (capacity == 0 || capacity > GLOBAL.boardFBMappedSize)
		capacity = GLOBAL.boardFBMappedSize;
	if (!QemuVgaCalculatePageLayout(width, height, depth, capacity,
									 &rowBytes, &pBytes, pageCount))
		return paramErr;
	if (pageSize)
		*pageSize = pBytes;
	return noErr;
}

OSStatus QemuVga_SetMode(UInt32 mode, UInt32 depth, UInt32 page)
{
	UInt32 width, height;
	UInt32 pageSize, numPages;
	OSStatus err;

	if (mode >= GLOBAL.numModes)
		return paramErr;
	
	width = getVMode(mode)->width;
	height = getVMode(mode)->height;
	err = QemuVga_GetModePages(mode, depth, &pageSize, &numPages);
	if (err != noErr)
		return err;
	lprintf("Set Mode: %dx%dx%d has %d pages\n", width, height, depth, numPages);
	if (page >= numPages)
		return paramErr;

	DispiWriteW(VBE_DISPI_INDEX_ENABLE,      0);
	DispiWriteW(VBE_DISPI_INDEX_BPP,         depth);
	DispiWriteW(VBE_DISPI_INDEX_XRES,        width);
	DispiWriteW(VBE_DISPI_INDEX_YRES,        height);
	DispiWriteW(VBE_DISPI_INDEX_BANK,        0);
	DispiWriteW(VBE_DISPI_INDEX_VIRT_WIDTH,  width);
	DispiWriteW(VBE_DISPI_INDEX_VIRT_HEIGHT, height * numPages);
	DispiWriteW(VBE_DISPI_INDEX_X_OFFSET,    0);
	DispiWriteW(VBE_DISPI_INDEX_Y_OFFSET,    height * page);
	DispiWriteW(VBE_DISPI_INDEX_ENABLE,      VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_8BIT_DAC);	
	GLOBAL.curMode = mode;
	GLOBAL.depth = depth;
	GLOBAL.curPage = page;
	GLOBAL.curBaseAddress = FB_START + page * pageSize;
	
	return noErr;
}

OSStatus QemuVga_Blank(Boolean blank)
{
	/* We use the AR Index VGA register which is a flip flop
	 * so we need to ensure we write twice. We use a non-existing
	 * index so that the second write is dropped.
	 */
	if (blank) {
		VgaWriteB(0x3c0, 0x1f);
		VgaWriteB(0x3c0, 0x1f);
	} else {
		VgaWriteB(0x3c0, 0x3f);
		VgaWriteB(0x3c0, 0x3f);
	}
	GLOBAL.blanked = blank;
	return noErr;
}
