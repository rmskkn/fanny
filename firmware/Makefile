# SPDX-License-Identifier: GPL-3.0-only
# STM32F103C8 PWM fan controller firmware (libopencm3).
#
#   make              build build/fanny.elf/.bin (inits and builds the
#                     libopencm3 submodule on first run)
#   make flash        flash build/fanny.bin with OpenOCD + ST-Link
#   make test         host-side unit tests (native gcc)
#   make analyze      clang static analyzer over every firmware source,
#                     cross-targeted at the real cpu/ABI. Needs clang-23
#                     specifically (checker defaults drift between major
#                     versions): on CI, apt.llvm.org's llvm.sh installs it
#                     as clang-23; on Arch, the clang package's own
#                     clang-23 symlink already puts it on PATH.

OPENCM3_DIR ?= Drivers/libopencm3

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
CLANG   ?= clang-23

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

# clang's own ARM sysroot lacks libopencm3 and the newlib headers the real
# toolchain carries, so point it at arm-none-eabi-gcc's sysroot instead.
# -isysroot alone found newlib's own headers (string.h etc.) with one clang
# build but not another, so the sysroot's include dir is also added
# explicitly rather than relied on implicitly. CLANG is pinned to clang-18
# specifically (see the help text above): an untested other major version
# can have a different default checker set, which bit us once already (a
# checker that flags every MMIO register access as a "fixed address
# dereference" even though that's the normal, intended way to touch
# hardware here; clang-18 doesn't carry that checker at all).
# --analyze always exits 0 even on a warning, so grep the output for one
# and fail the build on a hit instead of trusting the exit code.
analyze: $(OPENCM3_DIR)/lib/libopencm3_stm32f1.a
	@sysroot="$$($(CC) -print-sysroot)"; \
	status=0; \
	for src in $(SRCS); do \
		out="$$($(CLANG) --target=arm-none-eabi $(ARCH) -isysroot "$$sysroot" -I"$$sysroot/include" \
			-DSTM32F1 -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE \
			--analyze -Xclang -analyzer-output=text \
			"$$src" 2>&1)"; \
		if [ -n "$$out" ]; then echo "$$out"; status=1; fi; \
	done; \
	exit $$status

.PHONY: all flash test clean analyze

-include $(OBJS:.o=.d)
