# 288r community firmware — build.
#
#   make test      build + run all host unit tests (no hardware)
#   make engine    cross-compile the DSP engine objects for STM32F429 (proves it builds)
#   make firmware  full flashable image: build/fw/b288-community.{elf,hex,bin}
#   make clean
#
# The engine (src/*.c) is hardware-independent and host-testable. The firmware image
# adds the bare-metal BSP (src/bsp/*, direct-register F429 bring-up) + the vendored
# CMSIS device header/startup (Libraries/CMSIS). This toolchain ships libgcc but not
# newlib, so the image links -nostdlib + a tiny freestanding runtime (bsp/freestanding.c).
#
# Bring-up status: the image builds and links; the [BENCH]-tagged constants in
# src/bsp/board.h (codec regs, SAI clock chain, exact pins, TDM slot order) must be
# confirmed on the SWD/logic-analyzer session before it is trusted to make sound.
# See docs/bench-bringup.md. Recovery is always SWD: reflash the golden stock hex.

CROSS   ?= arm-none-eabi-
CC_HOST ?= cc

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := -std=c11 -Wall -Wextra -O3 -flto -ffreestanding -ffunction-sections -fdata-sections
INC     := -Isrc

# LIB_SRC = the hardware-independent, host-testable engine (excludes the app main).
LIB_SRC    := $(filter-out src/main.c,$(wildcard src/*.c))
ALL_SRC    := $(wildcard src/*.c)
ENGINE_OBJ := $(patsubst src/%.c,build/%.o,$(ALL_SRC))
TESTS      := $(wildcard test/test_*.c)

.PHONY: test engine firmware clean
.DEFAULT_GOAL := test

## ---- host unit tests -----------------------------------------------------
test:
	@mkdir -p build
	@fail=0; \
	for t in $(TESTS); do \
	  out=build/$$(basename $$t .c); \
	  $(CC_HOST) -std=c11 -Wall -Wextra $(INC) $$t $(LIB_SRC) -lm -o $$out || exit 1; \
	  echo "== $$t =="; ./$$out || fail=1; \
	done; \
	exit $$fail

## ---- cross-compile engine for the target (compile-only proof) ------------
engine: $(ENGINE_OBJ)
	@$(CROSS)size $(ENGINE_OBJ) | awk 'NR==1{print} NR>1{t+=$$1; print} END{print "  total text:", t, "bytes"}'

build/%.o: src/%.c
	@mkdir -p build
	$(CROSS)gcc $(MCU) $(CFLAGS) $(INC) -c $< -o $@

## ---- full firmware image -------------------------------------------------
TARGET    := b288-community
FWDIR     := build/fw
CMSIS_DEV := Libraries/CMSIS/Device/ST/STM32F4xx

FW_INC := -Isrc -Isrc/bsp \
          -ILibraries/CMSIS/Include \
          -I$(CMSIS_DEV)/Include
FW_DEF := -DSTM32F429xx -DHSE_VALUE=8000000

FW_CSRC := $(wildcard src/*.c) $(wildcard src/bsp/*.c) $(CMSIS_DEV)/Source/system_stm32f4xx.c
FW_ASRC := $(CMSIS_DEV)/Source/startup_stm32f429xx.s
FW_OBJ  := $(addprefix $(FWDIR)/,$(notdir $(FW_CSRC:.c=.o))) \
           $(addprefix $(FWDIR)/,$(notdir $(FW_ASRC:.s=.o)))

vpath %.c src src/bsp $(CMSIS_DEV)/Source
vpath %.s $(CMSIS_DEV)/Source

FW_LDFLAGS := $(MCU) -O3 -flto -nostdlib -T STM32F429.ld -Wl,--gc-sections -Wl,-Map=$(FWDIR)/$(TARGET).map

firmware: $(FWDIR)/$(TARGET).hex $(FWDIR)/$(TARGET).bin
	@$(CROSS)size $(FWDIR)/$(TARGET).elf

$(FWDIR)/%.o: %.c
	@mkdir -p $(FWDIR)
	$(CROSS)gcc $(MCU) $(CFLAGS) $(FW_INC) $(FW_DEF) -MMD -MP -c $< -o $@

-include $(FW_OBJ:.o=.d)

$(FWDIR)/%.o: %.s
	@mkdir -p $(FWDIR)
	$(CROSS)gcc $(MCU) -c $< -o $@

$(FWDIR)/$(TARGET).elf: $(FW_OBJ)
	$(CROSS)gcc $(FW_LDFLAGS) $(FW_OBJ) -lgcc -o $@

$(FWDIR)/%.hex: $(FWDIR)/%.elf
	$(CROSS)objcopy -O ihex $< $@
$(FWDIR)/%.bin: $(FWDIR)/%.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -rf build
