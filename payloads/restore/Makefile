LIBSRCS := ../lib/proc.c ../lib/debug.c ../lib/kobj.c
ELF := restore_cpu.elf
include ../common.mk

VARIANTS := restore_all.elf restore_device.elf restore_trace.elf

all: $(VARIANTS)

restore_all.elf: main.c $(LIBSRCS)
	$(CC) $(CFLAGS) -DGROUPS=15 -o $@ $^ $(LDLIBS)

restore_trace.elf: main.c $(LIBSRCS)
	$(CC) $(CFLAGS) -DGROUPS=15 -DTRACE -o $@ $^ $(LDLIBS)

restore_device.elf: main.c $(LIBSRCS)
	$(CC) $(CFLAGS) -DGROUPS=4 -o $@ $^ $(LDLIBS)

clean: clean_variants

clean_variants:
	rm -f $(VARIANTS)
