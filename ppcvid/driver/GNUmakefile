# Build the ClassicMac PowerPC video driver (qemu_vga.ndrv) with the Retro68
# powerpc-apple-macos toolchain. The driver is a fork of the QemuMacDrivers
# QemuVGADriver (GPL-2.0) extended with the ClassicMac host-window-resize
# channel; the ndrv link recipe follows elliotnunn/classicvirtio.
#
# Requires on PATH: powerpc-apple-macos-gcc, MakePEF (see
# scripts/build-ppcvid-ndrv.sh, which sets up the toolchain).

CC := powerpc-apple-macos-gcc

# TYPE_BOOL settles a dispute between MacTypes.h and stdbool.h; the driver
# sources predate C99 so build them as gnu99.
CDEFS := -DTYPE_BOOL -Dbool=_Bool -Dtrue=1 -Dfalse=0 -std=gnu99 \
         -Wall -Wno-multichar -Wno-unused-variable

# Uncomment to build with the debug logger (needs a debug console).
# CDEFS += -DDEBUG_OSI

SRCS := src/DriverDoDriverIO.c \
        src/DriverGestaltHandler.c \
        src/DriverQDCalls.c \
        src/QemuEdid.c \
        src/QemuVga.c \
        src/MacDriverUtils.c

LIBS := -lDriverServicesLib -lNameRegistryLib -lPCILib -lVideoServicesLib

all: bin/qemu_vga.ndrv

clean:
	rm -rf bin

bin/qemu_vga.so: $(SRCS) $(wildcard src/*.h) src/VideoDriver.exp ndrv.lds GNUmakefile
	@mkdir -p bin
	$(CC) -o $@ $(CDEFS) -Os -ffunction-sections -fdata-sections \
		-T ndrv.lds -Wl,-bE:src/VideoDriver.exp -Wl,--gc-sections -Wl,--gc-keep-exported \
		$(SRCS) $(LIBS)

bin/qemu_vga.ndrv: bin/qemu_vga.so
	MakePEF -o $@ $<

.PHONY: all clean
