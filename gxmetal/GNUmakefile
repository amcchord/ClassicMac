HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic

BUILD_DIR := build
PROTOCOL_TEST := $(BUILD_DIR)/test_protocol

all: test

$(PROTOCOL_TEST): tests/test_protocol.c host/gxmetal_decode.c \
                  host/gxmetal_decode.h protocol/gxmetal_protocol.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ tests/test_protocol.c host/gxmetal_decode.c

test: $(PROTOCOL_TEST)
	$(PROTOCOL_TEST)
	python3 tests/test_pef_set_init.py

guest:
	../scripts/build-gxmetal.sh

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C guest clean

.PHONY: all test guest clean
