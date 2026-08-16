HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic \
               -Iprotocol -Ihost

BUILD_DIR := build
PROTOCOL_TEST := $(BUILD_DIR)/test_protocol
QUEUE_TEST := $(BUILD_DIR)/test_queue
GUEST_TRANSPORT_TEST := $(BUILD_DIR)/test_guest_transport
RENDERER_TEST := $(BUILD_DIR)/test_renderer
PIPELINE_TEST := $(BUILD_DIR)/test_pipeline
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

$(PIPELINE_TEST): tests/test_pipeline.c guest/src/GXMetalTransport.c \
                  guest/src/GXMetalTransport.h host/gxmetal_queue.c \
                  host/gxmetal_queue.h host/gxmetal_renderer.c \
                  host/gxmetal_renderer.h host/gxmetal_decode.c \
                  protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -Iguest/src -o $@ tests/test_pipeline.c \
		guest/src/GXMetalTransport.c host/gxmetal_queue.c \
		host/gxmetal_renderer.c host/gxmetal_decode.c -lm

$(BUILD_DIR)/test_metal: tests/test_metal.m host/gxmetal_metal.m \
                         host/gxmetal_metal.h host/gxmetal_decode.c \
                         host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -fno-objc-arc -o $@ tests/test_metal.m \
		host/gxmetal_metal.m host/gxmetal_decode.c \
		-framework Foundation -framework Metal

test: $(PROTOCOL_TEST) $(QUEUE_TEST) $(GUEST_TRANSPORT_TEST) \
      $(RENDERER_TEST) $(PIPELINE_TEST) $(METAL_TEST)
	$(PROTOCOL_TEST)
	$(QUEUE_TEST)
	$(GUEST_TRANSPORT_TEST)
	$(RENDERER_TEST)
	$(PIPELINE_TEST)
	$(if $(METAL_TEST),$(METAL_TEST),@true)
	python3 tests/test_pef_set_init.py

guest:
	../scripts/build-gxmetal.sh

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C guest clean

.PHONY: all test guest clean
