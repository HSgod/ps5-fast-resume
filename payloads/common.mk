PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined, run: source env.sh)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS ?= -Wall -Werror -O2 -g
ELF ?= $(notdir $(CURDIR)).elf
SRCS ?= $(wildcard *.c)
LIBSRCS ?=
CFLAGS += -I../lib

all: $(ELF)

$(ELF): $(SRCS) $(LIBSRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(ELF)

test: $(ELF)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

.PHONY: all clean test
