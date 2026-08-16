CC := powerpc-apple-macos-gcc
MAKEPEF := MakePEF
REZ := Rez

ROOT_DIR := ../..
RINCLUDES ?= $(ROOT_DIR)/vendor/Retro68-build/toolchain/universal/RIncludes

CFLAGS := -std=gnu99 -Os -fPIC -Wall -Wextra -Werror \
          -Wno-multichar -ffunction-sections -fdata-sections
LDFLAGS := -shared -Wl,-e,GXMetalCFMInitialize \
           -Wl,-bE:src/GXMetal.exp -Wl,--gc-sections \
           -Wl,--gc-keep-exported
LIBS := -lQuickDraw3DRAVELib

all: bin/GXMetal.bin

bin/GXMetal.xcoff: src/GXMetalEngine.c src/GXMetal.exp \
                   ../protocol/gxmetal_protocol.h GNUmakefile
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/GXMetalEngine.c $(LIBS)

bin/GXMetal.pef: bin/GXMetal.xcoff
	$(MAKEPEF) -o $@ $<
	python3 ../tools/pef_set_init.py --set-init $@
	python3 ../tools/pef_set_init.py --verify $@

bin/GXMetal.bin: bin/GXMetal.pef src/GXMetal.r
	$(REZ) src/GXMetal.r -I$(RINCLUDES) -o $@ \
		--cc bin/GXMetal.dsk --cc bin/GXMetal --cc bin/%GXMetal.ad \
		-t tnsl -c GXMT --data bin/GXMetal.pef

clean:
	rm -rf bin

.PHONY: all clean
