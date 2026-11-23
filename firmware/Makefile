# SPDX-License-Identifier: GPL-3.0-only
# STM32F103C8 PWM fan controller firmware (libopencm3).
#
#   make              build build/fanny.elf/.bin (inits and builds the
#                     libopencm3 submodule on first run)
#   make flash        flash build/fanny.bin with OpenOCD + ST-Link
#   make test         host-side unit tests (native gcc)
#   make analyze      clang static analyzer + cppcheck over every firmware
#                     source, cross-targeted at the real cpu/ABI. Needs
#                     clang-23 specifically (checker defaults drift between
#                     major versions): on CI, apt.llvm.org's llvm.sh installs
#                     it as clang-23; on Arch, the clang package's own
#                     clang-23 symlink already puts it on PATH.

OPENCM3_DIR ?= Drivers/libopencm3

PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size
CLANG    ?= clang-23
CPPCHECK ?= cppcheck

BUILD := build
TARGET := $(BUILD)/fanny

SRCS := Core/main.c \
	App/fan.c App/pwm.c App/tach.c App/thermal.c App/safety.c App/protocol.c \
	App/autocurve.c App/curve_store.c \
	USB_DEVICE/usb_cdc.c
OBJS := $(SRCS:%.c=$(BUILD)/%.o)

ARCH    := -mcpu=cortex-m3 -mthumb -msoft-float
CFLAGS  := $(ARCH) -Os -g -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	   -DSTM32F1 -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE -MMD -MP
LDFLAGS := $(ARCH) -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
	   -L$(OPENCM3_DIR)/lib -TCore/stm32f103x8.ld -Wl,-Map=$(TARGET).map
LDLIBS  := -lopencm3_stm32f1 -lm

all: $(TARGET).bin
	$(SIZE) $(TARGET).elf

$(TARGET).elf: $(OBJS) $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Sources need the headers, which only exist once the submodule is checked out.
$(OBJS): | $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a

$(OPENCM3_DIR)/lib/libopencm3_stm32f1.a:
	test -f $(OPENCM3_DIR)/Makefile || git submodule update --init $(OPENCM3_DIR)
	$(MAKE) -C $(OPENCM3_DIR) TARGETS=stm32/f1

flash: $(TARGET).bin
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "program $< verify reset exit 0x08000000"

test:
	@mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -ICore -IApp tests/test_protocol.c App/protocol.c \
		-o $(BUILD)/test_protocol
	$(BUILD)/test_protocol
	cc -std=c11 -Wall -Wextra -Werror -ICore -IApp -DAUTOCURVE_TEST_HOOKS \
		tests/test_autocurve.c App/autocurve.c -o $(BUILD)/test_autocurve
	$(BUILD)/test_autocurve

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
# the vendored libopencm3 tree is suppressed entirely since its style is not
# ours to fix. A finding that's a false positive or conflicts with an
# external API's required signature gets a `// cppcheck-suppress <id>`
# comment at the spot instead of widening these build-wide suppressions.
analyze: $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	@incdirs="$$($(CC) $(ARCH) -E -Wp,-v -xc /dev/null 2>&1 \
		| sed -n '/#include <\.\.\.> search starts here:/,/End of search list\./p' \
		| sed -e '1d;$$d' -e 's/^ //')"; \
	isystem=""; \
	for d in $$incdirs; do isystem="$$isystem -isystem $$d"; done; \
	status=0; \
	for src in $(SRCS); do \
		out="$$($(CLANG) --target=arm-none-eabi $(ARCH) $$isystem \
			-DSTM32F1 -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE \
			--analyze -Xclang -analyzer-output=text \
			"$$src" 2>&1)"; \
		if [ -n "$$out" ]; then echo "$$out"; status=1; fi; \
	done; \
	$(CPPCHECK) --enable=warning,style,performance,portability --inline-suppr \
		--suppress=missingIncludeSystem --suppress='*:$(OPENCM3_DIR)/*' \
		--error-exitcode=1 --std=c11 \
		-DSTM32F1 -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE \
		$(SRCS) -q || status=1; \
	exit $$status

.PHONY: all flash test clean analyze

-include $(OBJS:.o=.d)
