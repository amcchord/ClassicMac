#ifndef __VideoDriverPrivate_H__
#define __VideoDriverPrivate_H__

#pragma internal off

#include <VideoServices.h>
#include <Video.h>
#include <Displays.h>
#include <DriverGestalt.h>
#include <DriverServices.h>
#include <PCI.h>

#include "MacDriverUtils.h"

#ifndef FALSE
#define TRUE	1
#define FALSE	0
#endif

#define QEMU_PCI_VIDEO_VENDOR_ID		0x1234
#define QEMU_PCI_VIDEO_DEVICE_ID		0x1111
#define QEMU_PCI_VIDEO_NAME				"\pQEMU,VGA"
#define QEMU_PCI_VIDEO_PNAME			"\p.Display_Video_QemuVGA"

#define QEMU_PCI_VIDEO_BASE_REG			0x10
#define QEMU_PCI_VIDEO_MMIO_REG			0x18

#define kDriverGlobalsPropertyName	"GLOBALS"
#define kDriverFailTextPropertyName	"FAILURE"
#define kDriverFailCodePropertyName	"FAIL-CODE"


/*
 * Our global storage is defined by this structure. This is not a requirement of the
 * driver environment, but it collects globals into a coherent structure for debugging.
 */
struct DriverGlobal {
	DriverRefNum		refNum;			/* Driver refNum for PB... */
	RegEntryID			deviceEntry;		/* Name Registry Entry ID */
	LogicalAddress		boardFBAddress;
	ByteCount			boardFBMappedSize;
	LogicalAddress		boardRegAddress;
	ByteCount			boardRegMappedSize;

	volatile Boolean	inInterrupt;

	/* Common globals */
	UInt32				openCount;
	
	/* Frame buffer configuration */
	Boolean				qdInterruptsEnable;	/* Enable VBLs for qd */
	Boolean				qdLuminanceMapping;

	Boolean				hasPCIInterrupt;
	IRQInfo				irqInfo;

	Boolean				hasTimer;
	InterruptServiceIDType	qdVBLInterrupt;
	TimerID				VBLTimerID;

	Boolean				isOpen;
	
	UInt32				vramSize;
	UInt32				depth;
	UInt32				bootDepth;
	UInt32				bootMode;
	UInt32				curMode;
	UInt32				numModes;
	UInt32				curPage;
	LogicalAddress		curBaseAddress;
	Boolean				blanked;

	/* Host-window-driven live resizing (ClassicMac vga-host-resize):
	 * QEMU publishes the desired resolution in extended registers when the
	 * user resizes the window; the pseudo-VBL timer polls the serial,
	 * debounces it, retargets a dynamic mode and asks the Display Manager
	 * to re-probe us through the VSL connect interrupt service. */
	Boolean				hostResizeAvail;	/* QEMU channel detected */
	UInt32				numBaseModes;		/* modes before the dynamic pair */
	UInt32				lastReqSerial;		/* last serial we acted upon */
	UInt32				pendingReqSerial;	/* serial being debounced */
	UInt32				pendingReqTicks;	/* how long it has held steady */
	UInt32				dynToggle;			/* alternates the dynamic pair */
	UInt32				hostPendingMode;	/* 1-based mode DM should adopt, 0=none */
	InterruptServiceIDType	qdConnectInterrupt;	/* VSL connect-change service */
};
typedef struct DriverGlobal DriverGlobal, *DriverGlobalPtr;

/*
 * Globals and functions
 */
extern DriverGlobal		gDriverGlobal;		/* All interesting globals */
#define GLOBAL			(gDriverGlobal)		/* GLOBAL.field for references */
extern DriverDescription	TheDriverDescription;	/* Exported to the universe */

#define FB_START			((char*)GLOBAL.boardFBAddress)
#define CHECK_OPEN( error )	if( !GLOBAL.isOpen ) return (error)

#endif
