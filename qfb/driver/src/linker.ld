OUTPUT_FORMAT("elf32-m68k");
ENTRY(_DeclHeader);

SECTIONS {
  .text : {
    *(.text.begin)
    *(.text .text.* .sdata .sdata.* .data .data.* .rodata .rodata.*)
    PROVIDE(_DRVREnd = .);
    /* Place the 56-byte trailer (.fblock: patch list + format header) at the end
       of the ROM, which is rounded up to a 4KiB boundary. Reserve the trailer's
       56 bytes before aligning so a driver that fills past (boundary - 56) grows
       the ROM to the next 4KiB boundary instead of overflowing the trailer. */
    . = ALIGN(. + 56, 4096) - 56;
    *(.fblock)
    PROVIDE(ROMSize = .);
    PROVIDE(DirectoryOffset = ((0-.)+20) & 0xFFFFFF);
  }
  /DISCARD/ : { *(*) }
}
