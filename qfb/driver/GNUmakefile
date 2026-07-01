# Uncomment this for WAY too many messages and the ROM is huge now
#DEBUG=1

LD:=m68k-apple-macos-ld
LDFLAGS:=
LIBS:=`m68k-apple-macos-gcc -print-file-name=libgcc.a`

AS:=m68k-apple-macos-as
ASFLAGS:=

CXX:=m68k-apple-macos-g++
CXXFLAGS:=-Iinclude/ -Wall -Wextra -Wno-strict-aliasing -mcpu=68020 -nostartfiles -ffreestanding -fno-exceptions -DSUPPORT_AUX -O3 -mpcrel

OBJCOPY:=m68k-apple-macos-objcopy

OBJS:=obj/decl_rom.o obj/main.o obj/control.o obj/status.o

ifdef DEBUG
CXXFLAGS+=-DDEBUG_QFB
OBJS+=obj/printf.o
else
endif

all: bin/mac_qfb.rom

clean:
	rm -rf bin obj

bin/mac_qfb.rom: src/calculate_crc.lua bin/mac_qfb.bin
	@mkdir -p bin
	lua $^ $@

bin/mac_qfb.elf: src/linker.ld $(OBJS)
	@mkdir -p bin
	$(LD) $(LDFLAGS) -o $@ -T $^ $(LIBS)

bin/mac_qfb.bin: bin/mac_qfb.elf
	$(OBJCOPY) $< -O binary $@

obj/decl_rom.o: src/decl_rom.s src/gamma_tables.s $(wildcard src/*.s) GNUmakefile
	@mkdir -p obj
# this one asm file will include all the others it needs
	$(AS) -o $@ src/decl_rom.s

src/gamma_tables.s: src/gen_gamma_tables.lua
	lua $< || (rm -f $@; false)

obj/%.o: src/%.cc $(wildcard include/*) GNUmakefile
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

obj/%.o: src/%.c $(wildcard include/*) GNUmakefile
	@mkdir -p obj
	$(CXX) $(CXXFLAGS) -c -o $@ $<

.PHONY: all clean
.SECONDARY: # keep all secondary files
MAKEFLAGS += --no-builtin-rules
