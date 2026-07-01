        .set QFB_VERSION, 0x0
        .set QFB_MODE_WIDTH, 0x4
        .set QFB_MODE_HEIGHT, 0x8
        .set QFB_MODE_DEPTH, 0xC
        .set QFB_MODE_BASE, 0x10
        .set QFB_MODE_STRIDE, 0x14
        .set QFB_PAL_INDEX, 0x1C
        .set QFB_PAL_COLOR, 0x20
        .set QFB_LUT_INDEX, 0x24
        .set QFB_LUT_COLOR, 0x28
        .set QFB_IRQ_MASK, 0x2C
        .set QFB_IRQ, 0x30
        .set QFB_CUSTOM_WIDTH, 0x34
        .set QFB_CUSTOM_HEIGHT, 0x38
        .set QFB_CUSTOM_DEPTH, 0x3C
        .set QFB_IRQ_VBL, 1
        /* A0: Address of an SEBlock, our parameters */
        CLR.W seStatus(%A0) /* presume success */
        /* save non-volatile registers */
        MOVEM.L %A2/%A3/%D2, -(%SP)
        /* A3 := address of control registers */
        /* (we should use the SResources to determine this, but it's really
           inconvenient to do that in a primary init) */
        CLR.L %D0
        /* set addressing mode to 32bit for superslot access */
        MOVE.B #1, %D0
        .short 0xA05D
        MOVEM.L %D0, -(%SP)
        CLR.L %D0
        /* end of addressing mode change */
        MOVE.B seSlot(%A0), %D0
        ORI.L #0xF0, %D0
        MOVEQ.L #24, %D1
        LSL.L %D1, %D0
        MOVEA.L %D0, %A3
        /* Check that a QFB device is present */
        MOVE.L #0x71666231, %D0
        CMP.L QFB_VERSION(%A3), %D0
        BNE returnErr
        /* Reset */
        MOVE.L #0, QFB_VERSION(%A3)
        /* Disable interrupts (which reset should have already done)*/
        CLR.L QFB_IRQ_MASK(%A3)
        MOVE.L #QFB_IRQ_VBL, QFB_IRQ(%A3)
        /* Set up the requested custom mode, at 1-bpp */
        MOVE.L QFB_CUSTOM_WIDTH(%A3), QFB_MODE_WIDTH(%A3)
        MOVE.L QFB_CUSTOM_HEIGHT(%A3), QFB_MODE_HEIGHT(%A3)
        MOVE.L #1, QFB_MODE_DEPTH(%A3)
        MOVE.L #0, QFB_MODE_BASE(%A3)
        /* Put in colors */
        CLR.L QFB_PAL_INDEX(%A3)
        MOVE.L #0xFFFFFF, QFB_PAL_COLOR(%A3)
        MOVE.L #1, QFB_PAL_INDEX(%A3)
        CLR.L QFB_PAL_COLOR(%A3)
        /* We don't need to do anything to the gamma ramp, resetting the card
           makes it linear */
        /* A2 := address of video RAM */
        CLR.L %D0
        MOVE.B seSlot(%A0), %D0
        MOVEQ.L #28, %D1
        LSL.L %D1, %D0
        MOVEA.L %D0, %A2
        /* Let's fill the screen with gray! */
        MOVE.L #0xAAAAAAAA, %D2
        MOVE.L QFB_MODE_HEIGHT(%A3), %D1
        CMP.L #2160, %D1
        BLS 1f
        MOVE.L #2160, %D1
1:
_screenFillLoopOuter:
        MOVE.L QFB_MODE_STRIDE(%A3), %D0
        CMP.L #16384, %D0
        BLS 1f
        MOVE.L #16384, %D0
1:      MOVE.L %A2, %A1
_screenFillLoopInner:
        MOVE.L %D2, (%A1)+
        SUBQ.L #4, %D0
        BGT _screenFillLoopInner
        ADD.L QFB_MODE_STRIDE(%A3), %A2
        NOT.L %D2
        SUBQ.L #1, %D1
        BGT _screenFillLoopOuter
        /* restore addressing mode */
        MOVEM.L (%SP)+, %D0
        .short 0xA05D
        /* end of restore addressing mode */
        /* now it gets real... now it's time to patch our sResources */
        /* ...or it would be if QEMU hadn't done it for us. */
        /* make room on the stack for the spBlock */
        SUBA #spBlock.size, %SP
        /* put a pointer to our brand new Slot Manager parameter block in A0 */
        MOVE %SP, %A0
        /* deallocate stack-allocated spBlock */
        ADDA #spBlock.size, %SP
        /* restore registers */
1:      MOVEM.L (%SP)+, %A2/%A3/%D2
        RTS
returnErr:
        MOVE.W #-1, seStatus(%A0)
        BRA 1b
        /* note that this code will only be correct with a fairly recent ROM
           (ha ha), so if you add support for older Macs to QEMU, or want to
           use this driver in another emulator, you'll need to pay attention to
           some gnarly issues. see "Designing Cards & Drivers for the
           Macintosh Family" and, as always, the various Inside Macintoshes. */
