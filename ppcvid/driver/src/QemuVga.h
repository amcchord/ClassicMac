#ifndef __QEMU_VGA_H__
#define __QEMU_VGA_H__

/* This must be enabled for the MacOS X version of the timer otherwise
 * we don't know if the call failed and don't back off to non-VBL ops
 */
#define USE_DSL_TIMER

/* Pseudo VBL timer duration in ms */
#define TIMER_DURATION	30

/* Enable use of the PCI IRQ as VBL using non-upstream QEMU VGA
 * extensions
 */
#undef USE_PCI_IRQ

/* --- Qemu/Bochs special registers --- */

#define VBE_DISPI_IOPORT_INDEX           0x01CE
#define VBE_DISPI_IOPORT_DATA            0x01CF

#define VBE_DISPI_INDEX_ID               0x0
#define VBE_DISPI_INDEX_XRES             0x1
#define VBE_DISPI_INDEX_YRES             0x2
#define VBE_DISPI_INDEX_BPP              0x3
#define VBE_DISPI_INDEX_ENABLE           0x4
#define VBE_DISPI_INDEX_BANK             0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH       0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT      0x7
#define VBE_DISPI_INDEX_X_OFFSET         0x8
#define VBE_DISPI_INDEX_Y_OFFSET         0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_ID0                    0xB0C0
#define VBE_DISPI_ID1                    0xB0C1
#define VBE_DISPI_ID2                    0xB0C2
#define VBE_DISPI_ID3                    0xB0C3
#define VBE_DISPI_ID4                    0xB0C4
#define VBE_DISPI_ID5                    0xB0C5

#define VBE_DISPI_DISABLED               0x00
#define VBE_DISPI_ENABLED                0x01
#define VBE_DISPI_GETCAPS                0x02
#define VBE_DISPI_8BIT_DAC               0x20
#define VBE_DISPI_LFB_ENABLED            0x40
#define VBE_DISPI_NOCLEARMEM             0x80

/* --- QEMU extended registers (BAR2 + 0x600, 32-bit LE each) ---
 * Registers 2..4 are the host-window-resize request channel added by the
 * ClassicMac vga-host-resize QEMU patch. They exist when register 0 (the
 * region size) reads back at least QEMU_EXT_SIZE_HOST_RESIZE bytes. QEMU
 * latches the desired guest resolution there when the user resizes the
 * window and bumps the serial; they are read-only to us. */
#define QEMU_EXT_REG_SIZE                0x0
#define QEMU_EXT_REG_BYTEORDER           0x1
#define QEMU_EXT_REG_REQ_WIDTH           0x2
#define QEMU_EXT_REG_REQ_HEIGHT          0x3
#define QEMU_EXT_REG_REQ_SERIAL          0x4

#define QEMU_EXT_SIZE_HOST_RESIZE        (5 * 4)

/* Smallest resolution we will follow the window down to; matches the QEMU
 * Cocoa window's minimum content size. */
#define HOST_RESIZE_MIN_WIDTH            512
#define HOST_RESIZE_MIN_HEIGHT           384

/* Ticks of the pseudo-VBL timer a request must hold steady before we act
 * (debounce, mirroring the 68k qfb driver's accRun debounce). */
#define HOST_RESIZE_DEBOUNCE_TICKS       2

/* --- VModes */

struct _vMode {
    UInt32 width;
    UInt32 height;
};

struct vMode {
    struct vMode *next;
    struct _vMode *mode;
};

extern struct vMode *vModes;
extern struct _vMode *getVMode(UInt16 idx);
extern void appendVModeToList(struct _vMode *vMode);

/* --- Internal APIs */

extern OSStatus	QemuVga_Init();
extern OSStatus	QemuVga_Exit();

extern OSStatus	QemuVga_Open();
extern OSStatus	QemuVga_Close();

extern void QemuVga_EnableInterrupts(void);
extern void QemuVga_DisableInterrupts(void);

extern UInt16 QemuVga_ReadEdidModes(void);

extern OSStatus	QemuVga_SetDepth(UInt32 bpp);

extern OSStatus	QemuVga_SetColorEntry(UInt32 index, RGBColor *color);
extern OSStatus	QemuVga_GetColorEntry(UInt32 index, RGBColor *color);

extern OSStatus QemuVga_GetModePages(UInt32 index, UInt32 depth,
									 UInt32 *pageSize, UInt32 *pageCount);
extern OSStatus QemuVga_GetModeInfo(UInt32 index, UInt32 *width, UInt32 *height);
extern OSStatus QemuVga_SetMode(UInt32 modeIndex, UInt32 depth, UInt32 page);

extern OSStatus QemuVga_Blank(Boolean blank);

/* --- Host-window-driven live resizing --- */

/* Retarget a dynamic mode to width x height (clamped) and return its 1-based
 * display mode ID; returns 0 when the request cannot be satisfied or already
 * matches the current mode. Safe at interrupt time (no allocation). */
extern UInt32 QemuVga_PrepareHostModeSwitch(UInt32 width, UInt32 height);

/* Read a QEMU extended register (QEMU_EXT_REG_*). */
extern UInt32 QemuVga_ReadExt(UInt32 reg);

/* Private status selector for a task-time guest agent (Strategy B): fills a
 * QemuVgaHostResizeRec so the agent can drive DMSetDisplayMode itself. */
#define cscQemuVgaGetHostResize 0x5192

typedef struct QemuVgaHostResizeRec {
    UInt32 available;   /* 1 when the QEMU host-resize channel is present */
    UInt32 serial;      /* current request serial (changes on window resize) */
    UInt32 width;       /* requested size, clamped to hardware limits */
    UInt32 height;
    UInt32 modeID;      /* 1-based mode ID to switch to, 0 = nothing to do */
    UInt32 depthMode;   /* current depth mode, for DMSetDisplayMode */
    UInt32 curWidth;    /* current mode size */
    UInt32 curHeight;
} QemuVgaHostResizeRec;

#endif
