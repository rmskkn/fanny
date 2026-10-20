# Top-level entry point; the firmware build lives in firmware/Makefile.
#
#   make          build firmware (firmware/build/fanctl.elf/.bin)
#   make flash    flash with OpenOCD + ST-Link
#   make test     host-side unit tests
#   make clean

all flash test clean:
	$(MAKE) -C firmware $@

.PHONY: all flash test clean
