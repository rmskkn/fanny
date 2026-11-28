# SPDX-License-Identifier: GPL-3.0-only
# Top-level entry point; the firmware build lives in firmware/Makefile.

.DEFAULT_GOAL := help

help:
	@echo "make full_fw     build firmware/build/fanny.bin (bootloader + app combined)"
	@echo "make flash_fw    flash it with OpenOCD + ST-Link"
	@echo "make ota_fw      build firmware/build/bootloader.elf/.bin + fanny_app.elf/.bin"
	@echo "make flash_ota   flash fanny_app.bin alone (bootloader must already be on the board)"
	@echo "make test        unit tests: firmware logic (native) + host/ daemon and CLI"
	@echo "make analyze     clang static analyzer over the firmware sources"
	@echo "make clean"

full_fw flash_fw ota_fw flash_ota clean analyze:
	$(MAKE) -C firmware $@

test:
	$(MAKE) -C firmware test
	$(MAKE) -C host test

.PHONY: help full_fw flash_fw ota_fw flash_ota test clean analyze
