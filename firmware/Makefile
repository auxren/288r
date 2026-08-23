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

# OPTIMISATION LEVEL FOR THE TARGET IMAGE — a budget decision, not a taste one.
#
# The F429 executes from flash at FIVE wait states (168 MHz, clock.c sets
# LATENCY_5WS + prefetch + the ART's 1 KB instruction cache). Sequential code is
# fed 128 bits per flash access, so a stream of 32-bit Thumb-2/VFP instructions
# cannot go faster than ~1.5 cycles each however simple they are, and anything
# that does not fit the 1 KB cache re-fetches it every block. The audio ISR is
# ~14 KB of fully inlined code executed 2999 times a second: its instruction
# FETCH is a first-order cost, and the field has already measured that directly
# — an edit that only shifted inlining topology (-4 KB of text) moved the ISR
# load from 93% to 76% of budget, ~4 points per KB.
#
#   -O3  bsp_audio_isr 13936 B (pre-restructure), total text 46500 B
#   -O2  bsp_audio_isr  8824 B, total text 35460 B     <- default
#   -Os  bsp_audio_isr  5876 B, total text 28664 B
#
# -O2 keeps every optimisation that matters here and drops the aggressive
# unrolling/peeling that -O3 spends code size on. It CANNOT change behaviour:
# no -ffast-math is used anywhere, so float order and rounding are fixed by the
# source at every level, and `make cachecheck` + the golden fixture pin the
# audio independently.
#
# THIS IS THE ONE CHANGE ON THIS BRANCH THAT IS INFERRED RATHER THAN MEASURED.
# The bench A/B is one flag and two SWD reads:
#     make firmware FW_OPT=-O3   # flash, reset g_isr_pk, play WCC-TIME, read it
#     make firmware              # (-O2) same programme, same read
# If -O3 wins, change the default back here and say so.
FW_OPT  ?= -O2
CFLAGS  := -std=c11 -Wall -Wextra $(FW_OPT) -flto -ffreestanding -ffunction-sections -fdata-sections
INC     := -Isrc

# LIB_SRC = the hardware-independent, host-testable engine (excludes the app main).
LIB_SRC    := $(filter-out src/main.c,$(wildcard src/*.c))
ALL_SRC    := $(wildcard src/*.c)
ENGINE_OBJ := $(patsubst src/%.c,build/%.o,$(ALL_SRC))
TESTS      := $(wildcard test/test_*.c)

.PHONY: test engine firmware clean cachecheck budget-strict
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

## ---- dl_cache equivalence gate (QA, budget contract clause C-1) ----------
# The CCM window cache is only allowed to change WHERE the delay-line words
# came from, never WHICH words they are. test_dl_cache proves that per stencil;
# this proves it for the whole instrument, by running the 200k-frame golden
# scenario (both loop-window orientations, varispeed 0.25..4.0, seam-adjacent
# reads, sub-block tap delays, overdub, FM, crush) with the cache compiled IN
# and compiled OUT and requiring the output and the recorded buffer to hash
# identically. Runs in CI: it is the only check that can catch a coherence rule
# being weakened, because a stale word changes the audio and nothing else.
cachecheck:
	@mkdir -p build
	@$(CC_HOST) -std=c11 -Wall $(INC) test/test_golden.c $(LIB_SRC) -lm \
	    -o build/golden_cache_on
	@$(CC_HOST) -std=c11 -Wall $(INC) -DDL_CACHE_ENABLE=0 test/test_golden.c \
	    $(LIB_SRC) -lm -o build/golden_cache_off
	@$(CC_HOST) -std=c11 -Wall $(INC) test/test_isr_budget.c $(LIB_SRC) -lm \
	    -o build/wc_cache_on
	@$(CC_HOST) -std=c11 -Wall $(INC) -DDL_CACHE_ENABLE=0 test/test_isr_budget.c \
	    $(LIB_SRC) -lm -o build/wc_cache_off
	@./build/golden_cache_on  | grep 'out hash' >  build/hash_on.txt
	@./build/wc_cache_on      | grep 'wc hash'  >> build/hash_on.txt
	@./build/golden_cache_off | grep 'out hash' >  build/hash_off.txt
	@./build/wc_cache_off     | grep 'wc hash'  >> build/hash_off.txt
	@diff -u build/hash_off.txt build/hash_on.txt \
	  && echo "dl_cache: audio bit-identical with the cache compiled in and out" \
	  || (echo "dl_cache CHANGES THE AUDIO — a coherence rule is broken"; exit 1)

## ---- the budget contract as a hard gate (QA) -----------------------------
# test_isr_budget reports the unmet contract items as GAPs so the normal gate
# stays usable; this target makes them failures. Flip CI over to it once the
# governor has a real floor level and dl_cache's rail behaviour is fixed.
budget-strict:
	@mkdir -p build
	$(CC_HOST) -std=c11 -Wall -Wextra $(INC) -DISR_BUDGET_STRICT \
	    test/test_isr_budget.c $(LIB_SRC) -lm -o build/test_isr_budget_strict
	./build/test_isr_budget_strict

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

FW_LDFLAGS := $(MCU) $(FW_OPT) -flto -nostdlib -T STM32F429.ld -Wl,--gc-sections -Wl,-Map=$(FWDIR)/$(TARGET).map

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

## ---- WCET / budget contract check ----------------------------------------
# Static half of budget contract clause C-3: every loop in the audio ISR's
# call graph bounded, no indirect dispatch, no soft-double helper, and the
# worst-case path costed against the per-block budget. Reports the projection
# vs target; add --strict to make that a build failure once it is under the
# line. See tools/wcet.py for what the numbers mean and do not mean.
.PHONY: wcet
wcet: $(FWDIR)/$(TARGET).elf
	python3 tools/wcet.py --elf $< --objdump $(CROSS)objdump

clean:
	rm -rf build
