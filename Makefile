# SPDX-License-Identifier: GPL-3.0-only
# Top-level entry point; the firmware build lives in firmware/Makefile.
#
#   make          build firmware (firmware/build/fanny.elf/.bin)
#   make flash    flash with OpenOCD + ST-Link
#   make test     unit tests: firmware logic (native) + host/ daemon and CLI
#   make clean

all flash clean:
	$(MAKE) -C firmware $@

test:
	$(MAKE) -C firmware test
	$(MAKE) -C host test

.PHONY: all flash test clean
