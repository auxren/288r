# 288r community firmware — build.
#
#   make test      build + run all host unit tests (no hardware)
#   make engine    cross-compile the DSP engine objects for STM32F429 (proves it builds)
#   make firmware  full flashable image  --  BLOCKED: needs CubeMX HAL + startup + pinout
#   make clean
#
# The engine (src/) is hardware-independent and fully host-testable. The firmware
# image additionally needs the HAL/startup/linker bring-up, which depends on the
# board pinout and clock tree (see README "Blocked on hardware").

CROSS   ?= arm-none-eabi-
CC_HOST ?= cc

MCU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS  := -std=c11 -Wall -Wextra -O2 -ffreestanding -ffunction-sections -fdata-sections
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

## ---- full firmware image (not yet buildable) -----------------------------
firmware:
	@echo "BLOCKED: needs cube/ (CubeMX HAL+startup), a confirmed pinout, and the"
	@echo "clock/SDRAM/codec config. See firmware/README.md 'Blocked on hardware'."
	@echo "The engine itself builds today: run 'make engine'."
	@exit 1

clean:
	rm -rf build
