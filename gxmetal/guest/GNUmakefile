CC := powerpc-apple-macos-gcc
MAKEPEF := MakePEF
REZ := Rez

ROOT_DIR := ../..
RINCLUDES ?= $(ROOT_DIR)/vendor/Retro68-build/toolchain/universal/RIncludes

CFLAGS := -std=gnu99 -Os -fPIC -Wall -Wextra -Werror \
          -Wno-multichar -ffunction-sections -fdata-sections \
          -Isrc -Iinclude -I../protocol
LDFLAGS := -shared -Wl,-e,GXMetalCFMInitialize \
           -Wl,-bE:src/GXMetal.exp -Wl,--gc-sections \
           -Wl,--gc-keep-exported
LIBS := -lQuickDraw3DRAVELib -lNameRegistryLib -lInterfaceLib

APP_CFLAGS := -std=gnu99 -Os -Wall -Wextra -Werror -Wno-multichar \
              -ffunction-sections -fdata-sections
APP_LDFLAGS := -Wl,--gc-sections

all: bin/GXMetal.bin bin/GXMetalInstaller.bin bin/GXMetalTest.bin

bin/GXMetal.xcoff: src/GXMetalEngine.c src/GXMetalTransport.c \
                   src/GXMetalTransport.h include/GXMetalRegistry.h src/GXMetal.exp \
                   ../protocol/gxmetal_protocol.h GNUmakefile
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		src/GXMetalEngine.c src/GXMetalTransport.c $(LIBS)

bin/GXMetal.pef: bin/GXMetal.xcoff
	$(MAKEPEF) -o $@ $<
	python3 ../tools/pef_set_init.py --set-init $@
	python3 ../tools/pef_set_init.py --verify $@

bin/GXMetal.bin: bin/GXMetal.pef src/GXMetal.r
	$(REZ) src/GXMetal.r -I$(RINCLUDES) -o $@ \
		--cc bin/GXMetal.dsk --cc bin/GXMetal --cc bin/%GXMetal.ad \
		-t tnsl -c GXMT --data bin/GXMetal.pef

bin/GXMetalInstaller.xcoff: apps/GXMetalInstaller.c GNUmakefile
	@mkdir -p bin
	$(CC) $(APP_CFLAGS) $(APP_LDFLAGS) -o $@ $< -lInterfaceLib

bin/GXMetalInstaller.pef: bin/GXMetalInstaller.xcoff
	$(MAKEPEF) -o $@ $<

bin/GXMetalInstaller.bin: bin/GXMetalInstaller.pef apps/GXMetalInstaller.r
	$(REZ) apps/GXMetalInstaller.r -I$(RINCLUDES) -o $@ \
		--cc bin/GXMetalInstaller.dsk --cc bin/GXMetalInstaller \
		--cc bin/%GXMetalInstaller.ad -t APPL -c GXMT \
		--data bin/GXMetalInstaller.pef

bin/GXMetalTest.xcoff: apps/GXMetalTest.c GNUmakefile
	@mkdir -p bin
	$(CC) $(APP_CFLAGS) $(APP_LDFLAGS) -o $@ $< \
		-lQuickDraw3DRAVELib -lInterfaceLib

bin/GXMetalTest.pef: bin/GXMetalTest.xcoff
	$(MAKEPEF) -o $@ $<

bin/GXMetalTest.bin: bin/GXMetalTest.pef apps/GXMetalTest.r
	$(REZ) apps/GXMetalTest.r -I$(RINCLUDES) -o $@ \
		--cc bin/GXMetalTest.dsk --cc bin/GXMetalTest \
		--cc bin/%GXMetalTest.ad -t APPL -c GXMT \
		--data bin/GXMetalTest.pef

clean:
	rm -rf bin

.PHONY: all clean
