CC := powerpc-apple-macos-gcc
M68K_CC := m68k-apple-macos-gcc
MAKEPEF := MakePEF
REZ := Rez

ROOT_DIR := ../..
RINCLUDES ?= $(ROOT_DIR)/vendor/Retro68-build/toolchain/universal/RIncludes
RETRO68_RINCLUDES ?= $(RINCLUDES)/../../RIncludes

CFLAGS := -std=gnu99 -O2 -fPIC -Wall -Wextra -Werror \
          -Wno-multichar -ffunction-sections -fdata-sections \
          -Isrc -Iinclude -I../protocol
GXMETAL_INPUT_CFLAGS :=
ifeq ($(GXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC),1)
GXMETAL_INPUT_CFLAGS += -DGXMETAL_INPUT_TICKLE_ONLY_DIAGNOSTIC=1
endif
LDFLAGS := -shared -Wl,-e,GXMetalCFMInitialize \
           -Wl,-bE:src/GXMetal.exp -Wl,--gc-sections \
           -Wl,--gc-keep-exported
LIBS := -lQuickDraw3DRAVELib -lNameRegistryLib -lInterfaceLib

APP_CFLAGS := -std=gnu99 -Os -Wall -Wextra -Werror -Wno-multichar \
              -ffunction-sections -fdata-sections -Iinclude
APP_LDFLAGS := -Wl,--gc-sections
# The startup row is coordinated through the classic low-memory globals at
# 0x0928-0x092e; GCC 16 otherwise diagnoses those intentional absolute
# accesses as zero-based array bounds errors.
INIT_CFLAGS := -std=gnu99 -Os -Wall -Wextra -Werror -Wno-multichar \
               -Wno-array-bounds
INIT_LDFLAGS := -Wl,--mac-flat

all: bin/GXMetal.bin bin/GXMetalInput.bin bin/GXMetalStartup.bin bin/GXMetalInstaller.bin \
     bin/GXMetalTest.bin bin/GXMetalAGLProbe.bin

bin/GXMetal.xcoff: src/GXMetalEngine.c src/GXMetalTransport.c \
                   src/GXMetalTransport.h src/GXMetalATICompatibility.h \
                   src/GXMetalDrawPolicy.h \
                   src/GXMetalGenerationPolicy.h \
                   src/GXMetalRAVECompatibility.h \
                   include/GXMetalDiagnostics.h \
                   include/GXMetalRegistry.h include/GXMetalVersion.h \
                   src/GXMetal.exp \
                   ../protocol/gxmetal_protocol.h GNUmakefile
	@mkdir -p bin
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		src/GXMetalEngine.c src/GXMetalTransport.c $(LIBS)

bin/GXMetal.pef: bin/GXMetal.xcoff
	$(MAKEPEF) -o $@ $<
	python3 ../tools/pef_set_init.py --set-init $@
	python3 ../tools/pef_set_init.py --verify $@

bin/GXMetalStartupIcon.flt: src/GXMetalStartupIcon.c GNUmakefile
	@mkdir -p bin
	$(M68K_CC) $(INIT_CFLAGS) $(INIT_LDFLAGS) -o $@ $<

bin/GXMetal.bin: bin/GXMetal.pef src/GXMetal.r src/GXMetalIcon.r
	$(REZ) src/GXMetal.r -I$(RINCLUDES) -o $@ \
		--cc bin/GXMetal.dsk --cc bin/GXMetal --cc bin/%GXMetal.ad \
		-t shlb -c tnsl --data bin/GXMetal.pef
	python3 ../tools/set_macbinary_flags.py $@ --set 0x0400 --clear 0x0180

bin/GXMetalInput.xcoff: src/GXMetalInput.c src/GXMetalInput.exp \
                        include/GXMetalInputDiagnostics.h \
                        ../protocol/gxmetal_protocol.h GNUmakefile
	@mkdir -p bin
	$(CC) $(CFLAGS) $(GXMETAL_INPUT_CFLAGS) \
		-shared -Wl,-e,GXMetalInputCFMInitialize \
		-Wl,-bE:src/GXMetalInput.exp -Wl,--gc-sections \
		-Wl,--gc-keep-exported -o $@ $< \
		-lInputSprocketLib -lInterfaceLib

bin/GXMetalInput.pef: bin/GXMetalInput.xcoff
	$(MAKEPEF) -o $@ $<
	python3 ../tools/pef_set_init.py --set-init $@
	python3 ../tools/pef_set_init.py --verify $@

bin/GXMetalInput.bin: bin/GXMetalInput.pef src/GXMetalInput.r \
                      src/GXMetalIcon.r
	$(REZ) src/GXMetalInput.r -I$(RINCLUDES) -o $@ \
		--cc 'bin/GXMetal Input.dsk' --cc 'bin/GXMetal Input' \
		--cc 'bin/%GXMetalInput.ad' -t shlb -c insp \
		--data bin/GXMetalInput.pef
	python3 ../tools/set_macbinary_flags.py $@ --set 0x0400 --clear 0x0180

bin/GXMetalStartup.bin: bin/GXMetalStartupIcon.flt src/GXMetalStartup.r \
                        src/GXMetalIcon.r
	$(REZ) src/GXMetalStartup.r -I$(RINCLUDES) \
		-I$(RETRO68_RINCLUDES) -o $@ \
		--cc bin/GXMetalStartup.dsk --cc 'bin/GXMetal Startup' \
		--cc 'bin/%GXMetalStartup.ad' -t INIT -c GXMT
	python3 ../tools/set_macbinary_flags.py $@ --set 0x0400 --clear 0x0180

bin/GXMetalInstaller.xcoff: apps/GXMetalInstaller.c \
                            include/GXMetalVersion.h GNUmakefile
	@mkdir -p bin
	$(CC) $(APP_CFLAGS) $(APP_LDFLAGS) -o $@ $< -lInterfaceLib

bin/GXMetalInstaller.pef: bin/GXMetalInstaller.xcoff
	$(MAKEPEF) -o $@ $<

bin/GXMetalInstaller.bin: bin/GXMetalInstaller.pef apps/GXMetalInstaller.r \
                          src/GXMetalIcon.r
	$(REZ) apps/GXMetalInstaller.r -I$(RINCLUDES) -Isrc -o $@ \
		--cc bin/GXMetalInstaller.dsk --cc bin/GXMetalInstaller \
		--cc bin/%GXMetalInstaller.ad -t APPL -c GXMT \
		--data bin/GXMetalInstaller.pef
	python3 ../tools/set_macbinary_flags.py $@ --set 0x2000 --clear 0x0100

bin/GXMetalTest.xcoff: apps/GXMetalTest.c include/GXMetalDiagnostics.h \
                       include/GXMetalVersion.h \
                       src/GXMetalATICompatibility.h GNUmakefile
	@mkdir -p bin
	$(CC) $(APP_CFLAGS) -Isrc $(APP_LDFLAGS) -o $@ $< \
		-lQuickDraw3DRAVELib -lNameRegistryLib -lInterfaceLib

bin/GXMetalTest.pef: bin/GXMetalTest.xcoff
	$(MAKEPEF) -o $@ $<

bin/GXMetalTest.bin: bin/GXMetalTest.pef apps/GXMetalTest.r \
                     src/GXMetalIcon.r
	$(REZ) apps/GXMetalTest.r -I$(RINCLUDES) -Isrc -o $@ \
		--cc bin/GXMetalTest.dsk --cc bin/GXMetalTest \
		--cc bin/%GXMetalTest.ad -t APPL -c GXMT \
		--data bin/GXMetalTest.pef
	python3 ../tools/set_macbinary_flags.py $@ --set 0x2000 --clear 0x0100

bin/GXMetalAGLProbe.xcoff: apps/GXMetalAGLProbe.c \
                           apps/GXMetalAGLProbeLogic.h \
                           include/GXMetalVersion.h GNUmakefile
	@mkdir -p bin
	$(CC) $(APP_CFLAGS) -Iapps $(APP_LDFLAGS) -o $@ $< -lInterfaceLib

bin/GXMetalAGLProbe.pef: bin/GXMetalAGLProbe.xcoff
	$(MAKEPEF) -o $@ $<

bin/GXMetalAGLProbe.bin: bin/GXMetalAGLProbe.pef apps/GXMetalAGLProbe.r \
                         src/GXMetalIcon.r
	$(REZ) apps/GXMetalAGLProbe.r -I$(RINCLUDES) -Isrc -o $@ \
		--cc bin/GXMetalAGLProbe.dsk --cc 'bin/GXMetal AGL Probe' \
		--cc bin/%GXMetalAGLProbe.ad -t APPL -c GXMA \
		--data bin/GXMetalAGLProbe.pef
	python3 ../tools/set_macbinary_flags.py $@ --set 0x2000 --clear 0x0100

clean:
	rm -rf bin

.PHONY: all clean
