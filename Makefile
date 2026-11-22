PAYLOADS := $(patsubst payloads/%/Makefile,%,$(wildcard payloads/*/Makefile))

.PHONY: all clean $(PAYLOADS)

all: $(PAYLOADS)

$(PAYLOADS):
	$(MAKE) -C payloads/$@

clean:
	for p in $(PAYLOADS); do $(MAKE) -C payloads/$$p clean; done
