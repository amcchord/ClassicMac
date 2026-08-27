HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic \
               -Iprotocol -Ihost

BUILD_DIR := build
PROTOCOL_TEST := $(BUILD_DIR)/test_protocol
QUEUE_TEST := $(BUILD_DIR)/test_queue
GUEST_TRANSPORT_TEST := $(BUILD_DIR)/test_guest_transport
RENDERER_TEST := $(BUILD_DIR)/test_renderer
DIRTY_TEST := $(BUILD_DIR)/test_dirty
PIPELINE_TEST := $(BUILD_DIR)/test_pipeline
ATI_COMPATIBILITY_TEST := $(BUILD_DIR)/test_ati_compatibility
RAVE_COMPATIBILITY_TEST := $(BUILD_DIR)/test_rave_compatibility
AGL_PROBE_TEST := $(BUILD_DIR)/test_agl_probe
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
METAL_TEST := $(BUILD_DIR)/test_metal
endif

all: test

$(PROTOCOL_TEST): tests/test_protocol.c host/gxmetal_decode.c \
                  host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_protocol.c host/gxmetal_decode.c

$(QUEUE_TEST): tests/test_queue.c host/gxmetal_queue.c host/gxmetal_queue.h \
               host/gxmetal_decode.c host/gxmetal_decode.h \
               protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_queue.c \
		host/gxmetal_queue.c host/gxmetal_decode.c

$(GUEST_TRANSPORT_TEST): tests/test_guest_transport.c \
                         guest/src/GXMetalTransport.c \
                         guest/src/GXMetalTransport.h \
                         guest/src/GXMetalDrawPolicy.h \
                         protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/src -Iguest/include -o $@ \
		tests/test_guest_transport.c guest/src/GXMetalTransport.c

$(RENDERER_TEST): tests/test_renderer.c host/gxmetal_renderer.c \
                  host/gxmetal_renderer.h host/gxmetal_decode.c \
                  host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_renderer.c \
		host/gxmetal_renderer.c host/gxmetal_decode.c -lm

$(DIRTY_TEST): tests/test_dirty.c host/gxmetal_dirty.c \
               host/gxmetal_dirty.h host/gxmetal_decode.c \
               host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_dirty.c \
		host/gxmetal_dirty.c host/gxmetal_decode.c

$(PIPELINE_TEST): tests/test_pipeline.c guest/src/GXMetalTransport.c \
                  guest/src/GXMetalTransport.h host/gxmetal_queue.c \
                  host/gxmetal_queue.h host/gxmetal_renderer.c \
                  host/gxmetal_renderer.h host/gxmetal_decode.c \
                  protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/src -o $@ tests/test_pipeline.c \
		guest/src/GXMetalTransport.c host/gxmetal_queue.c \
		host/gxmetal_renderer.c host/gxmetal_decode.c -lm

$(ATI_COMPATIBILITY_TEST): tests/test_ati_compatibility.c \
                           guest/src/GXMetalATICompatibility.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/src -o $@ \
		tests/test_ati_compatibility.c -lm

$(RAVE_COMPATIBILITY_TEST): tests/test_rave_compatibility.c \
                            guest/src/GXMetalRAVECompatibility.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/src -o $@ \
		tests/test_rave_compatibility.c

$(AGL_PROBE_TEST): tests/test_agl_probe.c \
                   guest/apps/GXMetalAGLProbeLogic.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/apps -o $@ tests/test_agl_probe.c

$(BUILD_DIR)/test_metal: tests/test_metal.m host/gxmetal_metal.m \
                         host/gxmetal_metal.h host/gxmetal_decode.c \
                         host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -DGXMETAL_TESTING -fno-objc-arc -o $@ tests/test_metal.m \
		host/gxmetal_metal.m host/gxmetal_decode.c \
		-framework Foundation -framework Metal

test: $(PROTOCOL_TEST) $(QUEUE_TEST) $(GUEST_TRANSPORT_TEST) \
      $(RENDERER_TEST) $(DIRTY_TEST) $(PIPELINE_TEST) \
      $(ATI_COMPATIBILITY_TEST) $(RAVE_COMPATIBILITY_TEST) \
      $(AGL_PROBE_TEST) $(METAL_TEST)
	$(PROTOCOL_TEST)
	$(QUEUE_TEST)
	$(GUEST_TRANSPORT_TEST)
	$(RENDERER_TEST)
	$(DIRTY_TEST)
	$(PIPELINE_TEST)
	$(ATI_COMPATIBILITY_TEST)
	$(RAVE_COMPATIBILITY_TEST)
	$(AGL_PROBE_TEST)
	$(if $(METAL_TEST),$(METAL_TEST),@true)
	python3 tests/test_gxmetal_vnc.py
	python3 tests/test_decode_diagnostics.py
	python3 tests/test_pef_set_init.py

guest:
	../scripts/build-gxmetal.sh

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C guest clean

.PHONY: all test guest clean
