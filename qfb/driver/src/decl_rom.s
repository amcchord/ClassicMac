        .global _DeclHeader,qfb_interrupt_service_routine
        .global _GammaTableMac, _GammaTableMac_Name
        .global _GammaTableSrgb, _GammaTableSrgb_Name
        .global _GammaTableLinear, _GammaTableLinear_Name
        .global _GammaTableNTSC, _GammaTableNTSC_Name
        .global _GammaTableSGI, _GammaTableSGI_Name
        .global _GammaTablePAL, _GammaTablePAL_Name
        .global _GammaTablePlato, _GammaTablePlato_Name
        .global _GammaTableOtalp, _GammaTableOtalp_Name

        .include "src/defs.s"

        /* defines that have to do with our ROM */
        .set ourBoardID, 0x9545 /* hope no real board used this! */
        /* ID of the board resource, must be in the range 0-127 */
        .set sResource_Board_ID, 1
        /* ID of the video resource, must be in the range 128-254 */
        .set sResource_Video_ID, 128

        .section .text.begin
        /* sResource directory, the list of all the sResources on the ROM */
        /* Note: our ROM header assumes this is the first thing in the ROM! */
_sResourceDirectory:
        OSLstEntry sResource_Board_ID, _sRsrc_Board
        OSLstEntry sResource_Video_ID, _sRsrc_Video
        DatLstEntry endOfList, 0
_sRsrc_Board:
        OSLstEntry sRsrcType, _BoardTypeRec
        OSLstEntry sRsrcName, _BoardName
        OSLstEntry sRsrcIcon, _Icon
        OSLstEntry sRsrcCicn, _Cicn
        DatLstEntry boardId, ourBoardID
        OSLstEntry primaryInit, _PrimaryInitRec
        OSLstEntry vendorInfo, _VendorInfoRec
        DatLstEntry endOfList, 0

_BoardTypeRec:
        .short catBoard /* Category: Board! */
        .short typeBoard /* Type: Board! */
        .short 0 /* DrvrSw: 0, because it's a board */
        .short 0 /* DrvrHw: 0, because... it's a board */
_BoardName:
        .asciz "Qemu FrameBuffer video card"

_PrimaryInitRec:
        .long _PrimaryInitRecEnd-_PrimaryInitRec
        .byte 2 /* code revision? */
        .byte sCPU_68020 /* CPU type */
        .short 0 /* reserved */
        .long 4 /* offset to code */
        .include "src/primary_init.s"
_PrimaryInitRecEnd:

_VendorInfoRec:
        OSLstEntry vendorId, _VendorId
        OSLstEntry revLevel, _RevLevel
        OSLstEntry partNum, _PartNum
        DatLstEntry endOfList, 0

_VendorId:
        .asciz "QEMU"
_RevLevel:
        .asciz "1.0"
_PartNum:
        .asciz "QFB0"

_sRsrc_Video:
        OSLstEntry sRsrcType, _VideoTypeRec
        OSLstEntry sRsrcName, _VideoName
        OSLstEntry sRsrcDrvrDir, _VideoDriverDirectory
        DatLstEntry sRsrcFlags, 6 /* open at start, use 32-bit addressing */
        DatLstEntry sRsrcHWDevId, 1
        OSLstEntry minorBase, _MinorBaseRec
        OSLstEntry minorLength, _MinorLengthRec
        OSLstEntry majorBase, _MajorBaseRec
        OSLstEntry majorLength, _MajorLengthRec
        OSLstEntry sGammaDir, _GammaDirectory
        /* Now we need sResource records for every bit depth */
        OSLstEntry oneBitMode, _OneBitRec
        OSLstEntry twoBitMode, _TwoBitRec
        OSLstEntry fourBitMode, _FourBitRec
        OSLstEntry eightBitMode, _EightBitRec
        OSLstEntry sixteenBitMode, _SixteenBitRec
        OSLstEntry thirtyTwoBitMode, _ThirtyTwoBitRec
        DatLstEntry endOfList, 0

_VideoTypeRec:
        .short catDisplay /* Category: Display */
        .short typeVideo /* Type: Video */
        .short drSwApple /* Software interface: QuickDraw */
        .short ourBoardID /* Hardware interface: unique (reuse our board ID) */

_VideoName:
        /* the video name is derived from the above, and must take this form */
        /* special note, the third element is the driver software interface,
           NOT the vendor of the board */
        .asciz "Display_Video_Apple_QFB"

_MinorBaseRec:
        .long 0 /* offset of video device within our slot space */
_MinorLengthRec:
        .long 0x10000000 /* size of our video device */
        /* it's a slight fib, since the declaration ROM is at the end of that,
           but the Slot Manager won't care :) */

_MajorBaseRec:
        .long 0 /* offset of video RAM within our super slot space */
_MajorLengthRec:
        .long 32 << 20 /* size of our video RAM, 32MiB */

_VideoDriverDirectory:
        OSLstEntry sCPU_68020, _DRVRBlock
        DatLstEntry endOfList, 0

_GammaDirectory:
        OSLstEntry 128, _GammaTableMac_SBlock
        OSLstEntry 129, _GammaTableLinear_SBlock
        OSLstEntry 130, _GammaTableNTSC_SBlock
        OSLstEntry 131, _GammaTableSGI_SBlock
        OSLstEntry 132, _GammaTablePAL_SBlock
        DatLstEntry endOfList, 0

        .macro ModeResource name, paramLink, type
\name\():
        OSLstEntry mVidParams, \paramLink
        DatLstEntry mPageCnt, 0x4545 /* will be patched by QEMU */
        DatLstEntry mDevType, \type /* 0 = clut, 1 = fixed CLUT, 2 = direct */
        DatLstEntry endOfList, 0
        .endm
        .macro ModeParams type, bpp, cpp, bpc, name
\name\():
        .long \name\()End-\name\() /* size of block */
        .long 0x10000 /* offset within device memory */
        .short 0x4545 /* bytes per row, will be patched by QEMU */
        /* bounds (top, left, bottom, right) */
        .short 0
        .short 0
        .short 0x4545 /* height, will be patched by QEMU */
        .short 0x4545 /* width, will be patched by QEMU */
        .short 1 /* version (always 1) */
        .short 0 /* packType (not used) */
        .long 0 /* packSize (not used) */
        .long 72 << 16 /* 72 dots per inch horizontally */
        .long 72 << 16 /* again vertically */
        .short \type /* 0 = chunky indexed, 16 = chunky direct */
        .short \bpp /* bits per pixel */
        .short \cpp /* components per pixel */
        .short \bpc /* bits per component */
        .long 0 /* "plane bytes" (reserved) */
\name\()End:
        .endm
        ModeResource _OneBitRec, _OneBitParams, 0
        ModeResource _TwoBitRec, _TwoBitParams, 0
        ModeResource _FourBitRec, _FourBitParams, 0
        ModeResource _EightBitRec, _EightBitParams, 0
        ModeResource _SixteenBitRec, _SixteenBitParams, 2
        ModeResource _ThirtyTwoBitRec, _ThirtyTwoBitParams, 2
        ModeParams 0, 1, 1, 1, _OneBitParams
        ModeParams 0, 2, 1, 2, _TwoBitParams
        ModeParams 0, 4, 1, 4, _FourBitParams
        ModeParams 0, 8, 1, 8, _EightBitParams
        ModeParams 16, 16, 3, 5, _SixteenBitParams
        ModeParams 16, 32, 3, 8, _ThirtyTwoBitParams

        /* The icon! */
_Icon:  .long 0x000FF000,0x007FFE00,0x01FFFF80,0x03E3FFC0,0x07C01FE0,0x0FC00FF0
        .long 0x1FC1CFF8,0x3F81C7FC,0x3F0001FC,0x7F03C07E,0x7F003F3E,0x7E0000BE
        .long 0xFE003FFF,0xFE007FFF,0xFE01FFFF,0xFE01FFFF,0xFF81FFFF,0xFFC0FFFF
        .long 0xFFF0FFFF,0xFFF87FFF,0x7FFC7FFE,0x7FFE7FFE,0x7FFE3FFE,0x3FFF3FFC
        .long 0x3FFF3FFC,0x1FFFBFF8,0x0FFFBFF0,0x07FFFFF8,0x03FFFFF8,0x01FFFFFC
        .long 0x007FFE7C,0x000FF03E
        /* Now in color! */
_Cicn:  .long _CicnEnd-_Cicn
        .long 0x00000000,0x80100000,0x00000020,0x00200000,0x00000000,0x00000048
        .long 0x00000048,0x00000000,0x00040001,0x00040000,0x00000000,0x00000000
        .long 0x00000000,0x00000004,0x00000000,0x00200020,0x00000000,0x00040000
        .long 0x00000020,0x00200000,0x0000000F,0xF000007F,0xFE0001FF,0xFF8003FF
        .long 0xFFC007FF,0xFFE00FFF,0xFFF01FFF,0xFFF83FFF,0xFFFC3FFF,0xFFFC7FFF
        .long 0xFFFE7FFF,0xFFFE7FFF,0xFFFEFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF7FFF,0xFFFE7FFF
        .long 0xFFFE7FFF,0xFFFE3FFF,0xFFFC3FFF,0xFFFC1FFF,0xFFF80FFF,0xFFF007FF
        .long 0xFFF803FF,0xFFF801FF,0xFFFC007F,0xFE7C000F,0xF03EFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFE3,0xFFFFFFC0,0x1FFFFFC0,0x0FFFFFC1,0xCFFFFF81
        .long 0xC7FFFF00,0x01FFFF03,0xC07FFF00,0x3F3FFE00,0x00BFFE00,0x3FFFFE00
        .long 0x7FFFFE01,0xFFFFFE01,0xFFFFFF81,0xFFFFFFC0,0xFFFFFFF0,0xFFFFFFF8
        .long 0x7FFFFFFC,0x7FFFFFFE,0x7FFFFFFE,0x3FFFFFFF,0x3FFFFFFF,0x3FFFFFFF
        .long 0xBFFFFFFF,0xBFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFF0000,0x00000000,0x000A0000,0xFFFF6666,0x33330001,0xCCCCCCCC
        .long 0xCCCC0002,0xBBBBBBBB,0xBBBB0003,0xAAAAAAAA,0xAAAA0004,0x88888888
        .long 0x88880005,0x77777777,0x77770006,0x55555555,0x55550007,0x44444444
        .long 0x44440008,0x22222222,0x22220009,0x11111111,0x1111000F,0x00000000
        .long 0x0000FFFF,0xFFFFFFFF,0x11111111,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF12
        .long 0x12121212,0x12FFFFFF,0xFFFFFFFF,0xFFFF2222,0x22222222,0x2222FFFF
        .long 0xFFFFFFFF,0xFFF23230,0x00323232,0x32323FFF,0xFFFFFFFF,0xFF333300
        .long 0x00000003,0x333333FF,0xFFFFFFFF,0xFF434300,0x00000000,0x434343FF
        .long 0xFFFFFFFF,0xF4444400,0x000FFF00,0x4444444F,0xFFFFFFFF,0xF4545000
        .long 0x000FFF00,0x0454545F,0xFFFFFFFF,0xF5550000,0x00000000,0x0005555F
        .long 0xFFFFFFFF,0xF5650000,0x00FFFF00,0x0000056F,0xFFFFFFFF,0xFF660000
        .long 0x000000FF,0xFFFF00FF,0xFFFFFFFF,0xFF700000,0x00000000,0x0000F0FF
        .long 0xFFFFFFFF,0xFFF00000,0x00000077,0x77777FFF,0xFFFFFFFF,0xFFF00000
        .long 0x00000787,0x8787FFFF,0xFFFFFFFF,0xFFF00000,0x00088888,0x88FFFFFF
        .long 0xFFFFFFFF,0xFFF00000,0x00089898,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF000
        .long 0x000FFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFF00,0x0000FFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0x0000FFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xF0000FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFF000FFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFF00FFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFF000FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFF00FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFF0FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFF0FF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF
        .long 0xFFFFFFFF,0xFFFFFFFF
        .byte 0xFF
        .align 2
_CicnEnd:

        /* Now for the DRIVER! */
        /* THIS MUST BE THE LAST THING IN THE `.text.begin` SECTION! */
_DRVRBlock:
        .long _DRVREnd-_DRVRBlock
_DRVR:
        /* drvrFlags: dNeedLock|dNeedTime|dStatEnable|dCtlEnable. dNeedTime
           (0x2000) gives us periodic accRun calls so we can poll the
           host-resize request; the rest are as the original driver had. */
        .short 0x6C00 /* drvrFlags */
        .short 0 /* drvrDelay, for drivers that need to poll (we don't) */
        .short 0 /* drvrEMask; used for desk accessories, not drivers */
        .short 0 /* drvrMenu; desk accessories again */
        .short _drvr_open_glue-_DRVR /* offset to Open routine */
        .short 0 /* offset to Prime routine */
        .short _drvr_ctl_glue-_DRVR /* offset to Ctl routine */
        .short _drvr_status_glue-_DRVR /* offset to Status routine */
        .short _drvr_close_glue-_DRVR /* offset to Close routine */
        .byte 24 /* length of name */
        .ascii ".Display_Video_Apple_QFB"
        .align 2
        .short 0x0100 /* version number of ROM driver (1.0.0) */
        /* (the System Software will look for a later version of this driver,
           and use the latest one it can find) */
        /* bog standard glue */
_drvr_open_glue:
        /* back up parameters */
        MOVEM.L %A0/%A1, -(%SP)
        /* C routine parameters, in reverse order: */
        /* Our slot number */
        /* (Multiversal interfaces are missing the NuBus-specific DCE stuff,
            so it's not easy to retrieve from C) */
        CLR.L %D0
        MOVE.B dCtlSlot(%A1), %D0
        MOVE.L %D0, -(%SP)
        /* Our own parameters: */
        MOVEM.L %A0/%A1, -(%SP)
        /* call! */
        BSR qfb_drvr_open
        /* clean up the stack */
        ADDA #12, %SP
        /* restore parameters */
        MOVEM.L (%SP)+, %A0/%A1
        RTS
        MacsbugSymbol "_drvr_open_glue"
_drvr_close_glue:
        MOVEM.L %A0/%A1, -(%SP)
        MOVEM.L %A0/%A1, -(%SP)
        BSR qfb_drvr_close
        ADDQ #8, %SP
        MOVEM.L (%SP)+, %A0/%A1
        RTS
        MacsbugSymbol "_drvr_close_glue"
        /* Status and Control are... different. We may have to jump to JIODone
           at the end, so that the device manager removes our IO request from
           the queue.

           If we were writing a driver capable of asynchronous IO, this glue
           would have to account for returning while an operation was still in
           progress. But we're not. */
_drvr_ctl_glue:
        MOVEM.L %A0/%A1, -(%SP)
        MOVEM.L %A0/%A1, -(%SP)
        BSR qfb_drvr_control
        BRA _drvr_return_glue
_drvr_status_glue:
        MOVEM.L %A0/%A1, -(%SP)
        MOVEM.L %A0/%A1, -(%SP)
        BSR qfb_drvr_status
        BRA _drvr_return_glue
_drvr_return_glue:
        ADDQ #8, %SP
        MOVEM.L (%SP)+, %A0/%A1
        /* check the ioTrap to see if it was an unqueued IO trap */
        MOVE.W ioTrap(%A0), %D1
        BTST #noQueueBit, %D1
        BEQ _goIODone
        /* it was not queued */
        MOVE.L %D0, ioResult(%A0)
        RTS
        MacsbugSymbol "_drvr_mixed_glue"
_goIODone:
        /* it was queued, jump to JIODone */
        /* (this is the easiest way to follow a jump vector on m68k) */
        MOVE.L JIODone, -(%SP)
        RTS
        MacsbugSymbol "_goIODone"
qfb_interrupt_service_routine:
        /* set addressing mode to 32bit for superslot access */
        MOVE.L #1, %D0
        .short 0xA05D
        MOVEM.L %D0, -(%SP)
        /* end of addressing mode change */
        /* acknowledge the interrupt */
        MOVEQ.L #1, %D0
        MOVE.L %D0, 0x30(%A1)
        /* restore addressing mode */
        MOVEM.L (%SP)+, %D0
        .short 0xA05D
        /* end of restore addressing mode */
        /* convert register pointer to slot number */
        MOVE.L %A1, %D0
        ROL.L #8, %D0
        AND #15, %D0
        /* call the VBL task routine, with our slot number in D0 */
        MOVE.L JVBLTask,%A0
        JSR (%A0)
        /* return 1, indicating that the interrupt was serviced */
        MOVEQ.L #1, %D0
        RTS
        MacsbugSymbol "qfb_interrupt_service_routine"
        /* note: this has to go inside the DRVR so we can respond to requests
           like cscGetGammaInfoList */
        .include "src/gamma_tables.s"

        .section .fblock
_QemuPatchHelper:
        .long 0 /* end of patch list (it's backwards) */
        .long _ThirtyTwoBitRec
        .long _SixteenBitRec
        .long _EightBitRec
        .long _FourBitRec
        .long _TwoBitRec
        .long _OneBitRec
        .ascii "PatchMe!"
_DeclHeader:
        .long DirectoryOffset
        .long ROMSize
        .long 0 /* Checksum goes here */
        .byte 1 /* ROM format revision */
        .byte 1 /* Apple format */
        .long 0x5A932BC7 /* Magic number */
        .byte 0 /* Must be zero */
        .byte 0x0F /* use all four byte lanes */

        .end
