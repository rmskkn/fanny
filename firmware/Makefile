# SPDX-License-Identifier: GPL-3.0-only
# STM32F103C8 PWM fan controller firmware (libopencm3), with a USB-CDC
# bootloader for field updates (single app partition -- see
# Core/flash_layout.h; no A/B, no trial-boot: an interrupted update needs
# a re-flash over the same CDC link, not an automatic revert).
#
#   make fw           build the whole firmware as one image, no bootloader,
#                     no OTA: build/fanny.elf/.bin, same as before any OTA
#                     work -- bench/dev use, or a board that never updates
#   make flash_fw     flash that image with OpenOCD + ST-Link
#   make fw_ota       build bootloader.elf/.bin and fanny_app.elf/.bin
#                     (inits and builds the libopencm3 submodule on first
#                     run)
#   make flash_fw_ota flash both with OpenOCD + ST-Link (a fresh board;
#                     once the bootloader is on it, update fanny_app.bin
#                     with the "update" command instead, over CDC)
#   make test         host-side unit tests (native gcc)
#   make analyze      clang static analyzer + cppcheck over every source this
#                     project owns, cross-targeted at the real cpu/ABI. Needs
#                     clang-23 specifically (checker defaults drift between
#                     major versions): on CI, apt.llvm.org's llvm.sh installs
#                     it as clang-23; on Arch, the clang package's own
#                     clang-23 symlink already puts it on PATH.
#
# The bootloader reuses chunky_bootloader's (Bootloader/chunky, git
# submodule) format2 wire format for the update blob (SPEC.md 4.1/4.2:
# magic/version/image-SHA-256/seal) and its sha256/crc32 utilities, but
# none of its A/B flash.h/bootloader.c/device.c machinery, which this
# single-partition layout has no use for -- see Bootloader/fw_image.c's
# header comment.

OPENCM3_DIR ?= Drivers/libopencm3
CHUNKY_DIR  ?= Bootloader/chunky

PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size
CLANG    ?= clang-23
CPPCHECK ?= cppcheck

BUILD      := build
CHUNKY_LIB := $(CHUNKY_DIR)/lib
FMT_C      := $(CHUNKY_DIR)/rust/chunky_format/c

ARCH    := -mcpu=cortex-m3 -mthumb -msoft-float
CFLAGS  := $(ARCH) -Os -g -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	   -DSTM32F1 -I$(OPENCM3_DIR)/include -MMD -MP
LDARCH  := $(ARCH) -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
	   -L$(OPENCM3_DIR)/lib
LDLIBS  := -lopencm3_stm32f1 -lm

# --- app (Core/, App/, USB_DEVICE/): fan control + the ASCII line
# protocol, unchanged by the OTA work except for App/fw_update.c's
# "update" command (resets into the bootloader) -- no chunky code at all. ---
APP_TARGET := $(BUILD)/fanny_app
APP_SRCS := Core/main.c \
	App/fan.c App/pwm.c App/tach.c App/thermal.c App/safety.c App/protocol.c \
	App/autocurve.c App/curve_store.c App/fw_update.c \
	USB_DEVICE/usb_cdc.c
APP_OBJS := $(APP_SRCS:%.c=$(BUILD)/%.o)
APP_CFLAGS := $(CFLAGS) -ICore -IApp -IUSB_DEVICE

# --- bootloader (Bootloader/, plus the two chunky lib/ files it needs for
# format2's image descriptor and frame CRCs; chunky_format.h is header-only) ---
BOOT_TARGET := $(BUILD)/bootloader
BOOT_OWN_SRCS := Bootloader/boot_main.c Bootloader/fw_image.c Bootloader/update_recv.c \
	Core/flash_port_stm32.c USB_DEVICE/usb_cdc.c
BOOT_LIB_SRCS := $(CHUNKY_LIB)/format2.c $(CHUNKY_LIB)/sha256.c $(CHUNKY_LIB)/util.c
BOOT_OBJS := $(BOOT_OWN_SRCS:%.c=$(BUILD)/boot/%.o) $(BOOT_LIB_SRCS:$(CHUNKY_LIB)/%.c=$(BUILD)/boot/%.o)
BOOT_CFLAGS := $(CFLAGS) -I$(CHUNKY_LIB) -I$(FMT_C) -ICore -IBootloader -IUSB_DEVICE

fw_ota: $(APP_TARGET).bin $(BOOT_TARGET).bin
	$(SIZE) $(BOOT_TARGET).elf $(APP_TARGET).elf

# --- app objects and link ---
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(APP_CFLAGS) -c $< -o $@

$(APP_TARGET).elf: $(APP_OBJS) Core/stm32f103x8_app.ld $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	$(CC) $(LDARCH) -TCore/stm32f103x8_app.ld -Wl,-Map=$(APP_TARGET).map $(APP_OBJS) $(LDLIBS) -o $@

$(APP_TARGET).bin: $(APP_TARGET).elf
	$(OBJCOPY) -O binary $< $@

# --- bootloader objects and link ---
$(BUILD)/boot/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BUILD)/boot/%.o: $(CHUNKY_LIB)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_TARGET).elf: $(BOOT_OBJS) Core/stm32f103x8_bootloader.ld $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	$(CC) $(LDARCH) -TCore/stm32f103x8_bootloader.ld -Wl,-Map=$(BOOT_TARGET).map $(BOOT_OBJS) $(LDLIBS) -o $@

$(BOOT_TARGET).bin: $(BOOT_TARGET).elf
	$(OBJCOPY) -O binary $< $@

# Sources need the headers, which only exist once the submodule is checked out.
$(APP_OBJS) $(BOOT_OBJS): | $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a

$(OPENCM3_DIR)/lib/libopencm3_stm32f1.a:
	test -f $(OPENCM3_DIR)/Makefile || git submodule update --init $(OPENCM3_DIR)
	$(MAKE) -C $(OPENCM3_DIR) TARGETS=stm32/f1

# Bootloader then app, at their fixed addresses -- the two-binary flash a
# fresh board needs. Re-flashing just the app once a bootloader is already
# on the board should go through an update instead (the "update" command,
# then streaming fanny_app.bin's matching full-image blob over CDC), not
# this target.
flash_fw_ota: $(BOOT_TARGET).bin $(APP_TARGET).bin
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "program $(BOOT_TARGET).bin verify 0x08000000" \
		-c "program $(APP_TARGET).bin verify reset exit 0x08005000"

# --- fw: the app alone, no bootloader, no OTA (see
# Core/stm32f103x8_monolithic.ld). For bench/dev use or a board that will
# never run an update. ---
$(BUILD)/fanny.elf: $(APP_OBJS) Core/stm32f103x8_monolithic.ld $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	$(CC) $(LDARCH) -TCore/stm32f103x8_monolithic.ld -Wl,-Map=$(BUILD)/fanny.map $(APP_OBJS) $(LDLIBS) -o $@

$(BUILD)/fanny.bin: $(BUILD)/fanny.elf
	$(OBJCOPY) -O binary $< $@

fw: $(BUILD)/fanny.bin
	$(SIZE) $(BUILD)/fanny.elf

flash_fw: $(BUILD)/fanny.bin
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "program $(BUILD)/fanny.bin verify reset exit 0x08000000"

all: fw fw_ota

test:
	@mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -ICore -IApp tests/test_protocol.c App/protocol.c \
		-o $(BUILD)/test_protocol
	$(BUILD)/test_protocol
	cc -std=c11 -Wall -Wextra -Werror -ICore -IApp -DAUTOCURVE_TEST_HOOKS \
		tests/test_autocurve.c App/autocurve.c -o $(BUILD)/test_autocurve
	$(BUILD)/test_autocurve
	cc -std=c11 -Wall -Wextra -Werror -ICore -IBootloader -Itests -I$(CHUNKY_LIB) -I$(FMT_C) \
		tests/test_fw_image.c Bootloader/fw_image.c $(CHUNKY_LIB)/format2.c $(CHUNKY_LIB)/sha256.c \
		-o $(BUILD)/test_fw_image
	$(BUILD)/test_fw_image
	cc -std=c11 -Wall -Wextra -Werror -DSTM32F1 -ICore -IBootloader -IUSB_DEVICE -Itests \
		-I$(CHUNKY_LIB) -I$(FMT_C) -I$(OPENCM3_DIR)/include \
		tests/test_update_recv.c Bootloader/update_recv.c Bootloader/fw_image.c \
		$(CHUNKY_LIB)/format2.c $(CHUNKY_LIB)/sha256.c $(CHUNKY_LIB)/util.c \
		-o $(BUILD)/test_update_recv
	$(BUILD)/test_update_recv

clean:
	rm -rf $(BUILD)

# clang's own ARM target has no newlib headers (string.h etc.) of its own,
# and guessing the right sysroot layout for them broke twice already: once
# across clang builds, once across how two different distros package
# gcc-arm-none-eabi's sysroot. So instead of guessing, ask arm-none-eabi-gcc
# itself (the same compiler the real build uses) where it finds its own
# headers, with the same cpu/ABI flags, and hand clang that exact list via
# -isystem. CLANG is pinned to clang-23 (see the help text above): an
# untested other major version can have a different default checker set,
# which bit us once already (a checker that flags every MMIO register
# access as a "fixed address dereference" even though that's the normal,
# intended way to touch hardware here).
# --analyze always exits 0 even on a warning, so grep the output for one
# and fail the build on a hit instead of trusting the exit code.
#
# cppcheck runs second, as a different engine over the same sources: it
# catches classes of bug clang's analyzer doesn't model (e.g. this project's
# protocol.c constVariablePointer fix). missingIncludeSystem is suppressed
# because, unlike clang above, cppcheck is never given libc's real headers;
# the vendored libopencm3 and chunky_bootloader trees are suppressed
# entirely since their style is not ours to fix. A finding that's a false
# positive or conflicts with an external API's required signature gets a
# `// cppcheck-suppress <id>` comment at the spot instead of widening these
# build-wide suppressions.
ANALYZE_SRCS := $(APP_SRCS) $(BOOT_OWN_SRCS)
ANALYZE_INC  := -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE -IBootloader \
	-I$(CHUNKY_LIB) -I$(FMT_C)
analyze: $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	@incdirs="$$($(CC) $(ARCH) -E -Wp,-v -xc /dev/null 2>&1 \
		| sed -n '/#include <\.\.\.> search starts here:/,/End of search list\./p' \
		| sed -e '1d;$$d' -e 's/^ //')"; \
	isystem=""; \
	for d in $$incdirs; do isystem="$$isystem -isystem $$d"; done; \
	status=0; \
	for src in $(ANALYZE_SRCS); do \
		out="$$($(CLANG) --target=arm-none-eabi $(ARCH) $$isystem \
			-DSTM32F1 $(ANALYZE_INC) \
			--analyze -Xclang -analyzer-output=text \
			"$$src" 2>&1)"; \
		if [ -n "$$out" ]; then echo "$$out"; status=1; fi; \
	done; \
	$(CPPCHECK) --enable=warning,style,performance,portability --inline-suppr \
		--suppress=missingIncludeSystem --suppress='*:$(OPENCM3_DIR)/*' --suppress='*:$(CHUNKY_DIR)/*' \
		--error-exitcode=1 --std=c11 \
		-DSTM32F1 $(ANALYZE_INC) \
		$(ANALYZE_SRCS) -q || status=1; \
	exit $$status

.PHONY: all fw flash_fw fw_ota flash_fw_ota test clean analyze

-include $(APP_OBJS:.o=.d)
-include $(BOOT_OBJS:.o=.d)
