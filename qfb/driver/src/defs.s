        .macro MacsbugSymbol Name
        .byte 0x7F + _macsbugSymbol\@ - .
        .ascii "\Name"
_macsbugSymbol\@:
        .align 2
        .word 0 /* length of constants */
        .endm
        /* random defines and macros that we would normally get from MPW */
        .macro OSLstEntry Id,Offset
        .long (\Id<<24)+((\Offset-.)&0xFFFFFF)
        .endm
        .macro DatLstEntry Id,Data
        .long (\Id<<24)+(\Data&0xFFFFFF)
        .endm
        .set sCPU_68020, 2
        .set endOfList, 255
        .set sRsrcType, 1
        .set sRsrcName, 2
        .set sRsrcIcon, 3
        .set sRsrcDrvrDir, 4
        .set sRsrcFlags, 7
        .set sRsrcHWDevId, 8
        .set minorBase, 10
        .set minorLength, 11
        .set majorBase, 12
        .set majorLength, 13
        .set sRsrcCicn, 15
        .set boardId, 32
        .set primaryInit, 34
        .set vendorInfo, 36
        .set sGammaDir, 64
        .set vendorId, 1
        .set revLevel, 3
        .set partNum, 4
        .set mVidParams, 1
        .set mPageCnt, 3
        .set mDevType, 4
        .set oneBitMode, 128
        .set twoBitMode, 129
        .set fourBitMode, 130
        .set eightBitMode, 131
        .set sixteenBitMode, 132
        .set thirtyTwoBitMode, 133
        .set catBoard, 1
        .set catDisplay, 3
        .set typeBoard, 0
        .set typeVideo, 1
        .set drSwApple, 1
        /* SpBlock; parameter block for Slot Manager routines */
        .struct 0
spResult: .space 4
spsPointer: .space 4
spSize: .space 4
spOffsetData: .space 4
spIOFileName: .space 4
spsExecPBlk: .space 4
spParamData: .space 4
spMisc: .space 4
spReserved: .space 4
spIOReserved: .space 2
spRefNum: .space 2
spCategory: .space 2
spCType: .space 2
spDrvrSW: .space 2
spDrvrHW: .space 2
spTBMask: .space 1
spSlot: .space 1
spID: .space 1
spExtDev: .space 1
spHwDev: .space 1
spByteLanes: .space 1
spFlags: .space 1
spKey: .space 1
spBlock.size:
        /* SEBlock; parameter block for SExec blocks (like PrimaryInit) */
        .struct 0
seSlot: .space 1
sesRsrcId: .space 1
seStatus: .space 2
seFlags: .space 1
seFiller0: .space 1
seFiller1: .space 1
seFiller2: .space 1
seResult: .space 4
seIOFileName: .space 4
seDevice: .space 1
sePartition: .space 1
seOSType: .space 1
seReserved: .space 1
seRefNum: .space 1
seNumDevices: .space 1
seBootState: .space 1
filler: .space 1
SEBlock.size:
        .set JIODone, 0x08FC
        .set JVBLTask, 0x0D28
        .set ioTrap, 6
        .set ioResult, 16
        .set noQueueBit, 9
        .set dCtlSlot, 40
