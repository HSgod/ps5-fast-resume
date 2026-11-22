LIBSRCS := ../lib/proc.c ../lib/debug.c
include ../common.mk

all: spin_agcsub.elf

spin_agcsub.elf: main.c $(LIBSRCS)
	$(CC) $(CFLAGS) -DSPIN_THREAD=\"AgcSubmission\" -o $@ $^ $(LDLIBS)
