# STM32F103C8 PWM fan controller firmware (libopencm3).
#
#   make              build build/fanctl.elf/.bin (inits and builds the
#                     libopencm3 submodule on first run)
#   make flash        flash with OpenOCD + ST-Link
#   make test         host-side unit tests (native gcc)

OPENCM3_DIR ?= Drivers/libopencm3

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

BUILD := build
TARGET := $(BUILD)/fanctl

SRCS := Core/main.c \
	App/fan.c App/pwm.c App/tach.c App/thermal.c App/safety.c App/protocol.c \
	USB_DEVICE/usb_cdc.c
OBJS := $(SRCS:%.c=$(BUILD)/%.o)

ARCH    := -mcpu=cortex-m3 -mthumb -msoft-float
CFLAGS  := $(ARCH) -Os -g -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	   -DSTM32F1 -I$(OPENCM3_DIR)/include -ICore -IApp -IUSB_DEVICE -MMD -MP
LDFLAGS := $(ARCH) -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
	   -L$(OPENCM3_DIR)/lib -TCore/stm32f103x8.ld -Wl,-Map=$(TARGET).map
LDLIBS  := -lopencm3_stm32f1

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

flash: $(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
		-c "program $< verify reset exit"

test:
	@mkdir -p $(BUILD)
	cc -std=c11 -Wall -Wextra -Werror -ICore -IApp tests/test_protocol.c App/protocol.c \
		-o $(BUILD)/test_protocol
	$(BUILD)/test_protocol

clean:
	rm -rf $(BUILD)

.PHONY: all flash test clean

-include $(OBJS:.o=.d)
