"""
Binary Ninja rename script for the Buchla 288r "Time Domain Processor" firmware
(STM32F429 target).

Usage (from Binary Ninja's Python console, with the .bndb already open as `bv`):

    import rename_288r
    rename_288r.apply(bv)

Or headless:

    import binaryninja as bn
    bv = bn.load("firmware.bndb")
    import rename_288r
    rename_288r.apply(bv)
    bv.save_auto_snapshot()

This only renames functions/data variables and adds comments — it does not
change any code, types, or control flow. Everything here reflects the
panel-to-code mapping worked out from disassembly + the confirmed panel
photo (short/full cycle, write/recirc, pulse inputs, cal/preset A-D,
summed outputs, output mixer sliders + phase select, auto control,
input mixer, time multiplier). Some mappings are confident (register
addresses matching GPIO/RCC/I2C/SAI peripherals, and the 44140/11035
cycle-length divisor), others are best-effort based on code structure
and are marked as such in the comments.
"""

import binaryninja as bn


# ---------------------------------------------------------------------------
# Function renames: sub_XXXX -> descriptive name
# key = original address as it appeared in the decompiler output (offset from
# base 0x08000000 is NOT added here -- these are the raw sub_XXXX offsets you
# saw in the pseudocode, i.e. add your flash base if your addresses differ).
# ---------------------------------------------------------------------------

FUNCTION_RENAMES = {
    # --- soft-float / libgcc runtime (safe to leave, included for completeness) ---
    0x744:  "sf_extract_sign_or_call",       # helper feeding soft-float compare
    0x1f0:  "sf_dsub",                       # double subtract wrapper (flips sign, tailcalls dadd)
    0x1fc:  "sf_dadd_dsub_core",             # __aeabi_dadd / __adddf3 core
    0x4b8:  "sf_dmul",                       # __aeabi_dmul / __muldf3
    0x4fc:  "sf_normalize_helper",
    0x568:  "sf_ddiv",                       # __aeabi_ddiv / __divdf3
    0x7bc:  "sf_ddiv_longdiv",                # long-division fallback used by ddiv
    0x92a:  "sf_dcmp_core",
    0x99c:  "sf_dcmp_le_ge",                  # __aeabi_cdcmple / cdcmpge core
    0xa18:  "sf_dcmp_swap_args",
    0xa28:  "sf_dcmp_dispatch",
    0xa38:  "sf_deq",                         # __aeabi_cdeq
    0xa4c:  "sf_dge",                         # __aeabi_cdge (via carry)
    0xa60:  "sf_dgt",
    0xa74:  "sf_dle",
    0xa88:  "sf_dlt",
    0xa9c:  "sf_d2i_trunc",                   # __aeabi_d2iz
    0xaec:  "sf_d2i_trunc_unsigned",
    0xb2c:  "sf_i2d_or_f2d",                  # int/float -> double
    0xbcc:  "sf_udiv64_entry",
    0xbfc:  "sf_udiv64_core",
    0xec8:  "sf_stub_zero",

    # --- ADC / panel-switch readback ---
    0xecc:  "read_panel_i2c_sliders",         # 8x 3-byte I2C xfers into 0x2000035c[] -- OUTPUT MIXER sliders 0-8
    0xf58:  "get_slider_raw",                 # returns 0x2000035c[idx<<1] raw slider value
    0xf64:  "read_mode_switches",             # reads 4 GPIO bits -> packs into 0x20000360/361/362
    0xfbc:  "get_switch_bit_A",               # *0x20000362 -- likely WRITE/RECIRC or CAL/PRESET related bit
    0xfc8:  "get_cycle_length_switch",        # *0x20000361 -- SHORT CYCLE / FULL CYCLE toggle (confirmed 4:1 ratio)
    0xfd4:  "get_mode_2bit",                  # *0x20000360 -- 2-bit combined mode (which buffer/preset feeds output)
    0xfe0:  "gpio_clear_bit_by_index",
    0x102c: "gpio_set_bit_by_index",

    # --- delay engine core ---
    0x1078: "delay_dma_stream_reconfigure",    # sets up 4 successive DMA-stream-select params, then commits
    0x1110: "get_mode_from_switches",          # combines sub_4310(9)/(10) -> 0,1,2 mode value
    0x112c: "compute_output_scale_reg",        # RCC/DAC scaling calc feeding *0x40023888 (SAI/PLL cfg)
    0x1180: "init_sai_stream",                 # SAI + DMA + RCC bring-up for codec I/O
    0x1250: "delay_tap_service_A",             # per-sample tap output pointer/pos update, path A
    0x15dc: "delay_tap_service_B",             # twin of above, path B (stereo? or preset-pair)
    0x1968: "auto_control_ramp_update_A",      # envelope-follower/sawtooth ramp update (AUTO CONTROL, path A)
    0x1c98: "auto_control_ramp_update_B",      # twin of above, path B; also updates per-tap trim table 0x2000000c
    0x2030: "time_multiplier_and_envfollow",   # computes smoothed TIME MULTIPLIER value + dual envelope followers
    0x2508: "main_init_and_run_loop",          # top-level init (RCC/GPIO/I2C/SAI/USART) + superloop / mode state machine

    # --- misc peripheral glue ---
    0x3214: "on_usart6_something",
    0x3228: "envfollow_insert_ringbuf_A",      # ring-buffer insert/running-avg, envelope follower channel A
    0x32c0: "envfollow_insert_ringbuf_B",      # same, channel B
    0x3358: "envfollow_insert_ringbuf_C",      # same, channel C (3rd follower -- possibly INPUT MIXER monitor)
    0x3408: "scan_preset_dipswitch_column",    # bit-bang GPIO scan of one PRESET A/B/C/D dip-switch column
    0x3488: "scan_all_preset_dipswitches",     # loops scan_preset_dipswitch_column x8, builds phase/tap table
    0x3c64: "lookup_preset_tap_position",      # *0x200020cc[...] lookup, values 20,40,...,160 (confirmed from panel: PHASE SELECT scale 0-160)

    # --- clock / RCC ---
    0x43dc: "clock_init_pll",
    0x4394: "systick_reconfigure",
    0x4410: "systick_tick_accum",
    0x4428: "systick_get_ticks",
    0x4434: "delay_ticks_blocking",
    0x4910: "nvic_set_priority_group",
    0x4934: "nvic_set_priority",
    0x49a0: "nvic_enable_irq",
    0x49bc: "systick_config",
    0x4d4c: "gpio_pin_configure",              # AF/mode/speed/pupd config for one pin, used heavily during init
    0x51b0: "rcc_pll_wait_and_switch",
    0x55d8: "rcc_configure_prescalers",
    0x556c: "rcc_get_pclk",
    0x5714: "rcc_get_apb1_timclk",
    0x5734: "rcc_get_apb2_timclk",
    0x5754: "rcc_configure_peripheral_clocks",

    # --- I2S/SAI codec driver ---
    0x59a8: "sai_block_init",
    0x5b64: "sai_block_a_transmit",
    0x5c24: "sai_block_b_transmit",
    0x5ce8: "sai_dma_half_complete_cb",         # calls auto_control_ramp_update_A
    0x5d48: "sai_dma_complete_cb_B",            # calls auto_control_ramp_update_B
    0x5d54: "sai_dma_half_complete_cb_2",       # calls delay_tap_service_A
    0x5db4: "sai_dma_complete_cb_2",            # calls delay_tap_service_B

    # --- I2C (panel slider bank) ---
    0x5f04: "i2c_init",
    0x5f48: "i2c_write_blocking",
    0x5f7c: "i2c_stop",
    0x6024: "i2c_transfer",

    # --- GPIO scan helpers ---
    0x6930: "usart_init",
    0x6a74: "sai_block_cr1_apply",
    0x6af4: "sai_block_slot_apply",
    0x6b84: "i2c_start_and_addr",
}


# ---------------------------------------------------------------------------
# Data-variable renames: address -> (name, optional type_string, comment)
# type_string is a C-style Binary Ninja type string; leave None to keep
# whatever type Binary Ninja inferred.
# ---------------------------------------------------------------------------

DATA_RENAMES = {
    0x20000000: ("delay_ram_length",        "uint32_t", "Total length of the delay RAM circular buffer (in samples)."),
    0x20000004: ("delay_ram_bank_A",        "uint32_t*", "Primary delay memory bank (write head at delay_write_ptr)."),
    0x20000008: ("delay_ram_bank_B",        "uint32_t*", "Secondary delay memory bank / recirc path."),
    0x20000358: ("panel_switch_bits",       "uint32_t", "Packed panel switch bits, read by get_mode_from_switches (sub_4310 bit tests)."),
    0x20000360: ("mode_2bit",               "uint8_t", "0/1/2 -- combined WRITE/RECIRC + CAL/PRESET state, from get_mode_2bit."),
    0x20000361: ("cycle_length_switch",     "uint8_t", "SHORT CYCLE (0) / FULL CYCLE (1) front-panel toggle."),
    0x20000362: ("switch_bit_a",            "uint8_t", "Secondary front-panel switch bit (write/recirc candidate)."),
    0x2000035c: ("slider_raw_table",        "int16_t[8]", "Raw values for OUTPUT MIXER sliders 0-8, read via I2C in read_panel_i2c_sliders."),
    0x20000000: ("delay_ram_length",        "uint32_t", "Total length of delay RAM circular buffer."),
    0x200000c4: ("delay_write_ptr",         "uint32_t", "Live record/write head into delay_ram_bank_A/B."),
    0x200000c8: ("delay_read_ptr_preset",   "uint32_t", "Read pointer used when following a stored PRESET tap pattern."),
    0x200000cc: ("delay_loop_start_ptr",    "uint32_t", "Loop/recirc start pointer, set on WRITE->RECIRC transition."),
    0x200000d0: ("transport_mode",          "uint8_t", "1=write, 2=recirc/loop-full, 3=recirc/loop-short(?), 5/6=transitional states."),
    0x200000d4: ("delay_read_offset",       "int32_t", "Computed read offset = write_ptr - time_multiplier_samples."),
    0x200000dc: ("time_multiplier_prev",    "float", "Previous smoothed TIME MULTIPLIER value, for slew limiting."),
    0x200013c0: ("delay_loop_end_ptr",      "uint32_t", "Loop/recirc end pointer (paired with delay_loop_start_ptr)."),
    0x20001428: ("time_multiplier_quantized_prev", "int32_t", "Hysteresis-compared quantized TIME MULTIPLIER reading."),
    0x20001430: ("envfollow_accum_ch_signal", "int32_t", "Running envelope-follower accumulator, 'signal in' path."),
    0x20001434: ("envfollow_accum_ch_cv",     "int32_t", "Running envelope-follower accumulator, 'c.v. in' path (or 2nd tap)."),
    0x20001480: ("auto_ramp_accum_A",       "int64_t", "AUTO CONTROL sawtooth ramp running sum, path A."),
    0x200014d0: ("auto_control_mode_A",     "uint8_t", "AUTO CONTROL resolution/mode selector, path A."),
    0x20001700: ("auto_ramp_accum_B",       "int64_t", "AUTO CONTROL sawtooth ramp running sum, path B."),
    0x200016b8: ("sai_dma_out_buf_B",       "int32_t[]", "Output samples staged for SAI DMA, path B."),
    0x200016dc: ("sai_dma_out_buf_A",       "int32_t[]", "Output samples staged for SAI DMA, path A."),
    0x200017a0: ("envfollow_ring_signal",   "int32_t[128]", "128-sample ring buffer backing envfollow_accum_ch_signal running average."),
    0x20001750: ("time_multiplier_smoothed","float", "Slew-limited TIME MULTIPLIER value actually applied to the delay engine."),
    0x20001aa0: ("time_multiplier_delta",   "int32_t", "Signed change in raw TIME MULTIPLIER ADC/CV reading this sample."),
    0x20001aa4: ("auto_ramp_pos_A",         "uint8_t", "0-127 ring position for auto_control_ramp_update_A's sawtooth."),
    0x20001aa6: ("auto_ramp_pos_B",         "uint8_t", "0-127 ring position, secondary follower channel."),
    0x20001aa8: ("auto_ramp_pos_C",         "uint8_t", "0-127 ring position, third follower channel (combined ramp)."),
    0x20001aac: ("codec_sai_block_a",       "void*", "SAI peripheral instance struct, block A (audio codec channel A)."),
    0x20001eec: ("time_multiplier_raw",     "int32_t", "Raw ADC/CV reading for the TIME MULTIPLIER control before smoothing."),
    0x20001f98: ("preset_tap_target_table", "int32_t[8]", "Target per-tap positions loaded from lookup_preset_tap_position (values 20..160)."),
    0x2000000c: ("preset_tap_current_table","int32_t[8]", "Current (slewing) per-tap positions, chasing preset_tap_target_table."),
    0x20002080: ("time_multiplier_committed","int32_t", "Time-multiplier value committed after hysteresis check (+/-1)."),
    0x200020c0: ("time_multiplier_at_last_commit", "int32_t", "Snapshot of time_multiplier_raw at the last committed change."),
    0x200020c4: ("preset_dipswitch_col_0",  "uint16_t", "PRESET dip-switch scan result, column 0, all 8 banks (A-D x2?)."),
    0x200020c6: ("preset_dipswitch_col_1",  "uint16_t", "PRESET dip-switch scan result, column 1."),
    0x200020c8: ("preset_dipswitch_col_2",  "uint16_t", "PRESET dip-switch scan result, column 2."),
    0x200020cc: ("preset_phase_table",      "uint8_t[48]", "Decoded PHASE SELECT / preset tap-position values (20,40,...,160), 8 per preset row."),
    0x20002084: ("output_sample_scratch",   "int32_t", "Per-tap output sample scaled/shifted just before writing to SAI DMA buffer."),
    0x20002088: ("auto_control_correction", "int32_t", "Correction term applied by auto_control_ramp_update_A/B each sample."),
    0x2000208a: ("output_resolution_mode",  "uint8_t", "0/1/2 resolution/format mode for SAI output word width."),
    0x2000208c: ("sai_peripheral_struct",   "void*", "SAI/DMA config struct passed to delay_dma_stream_reconfigure."),
}


def apply(bv: "bn.BinaryView", base: int = 0x08000000):
    """
    Apply all function and data renames.

    `base` is added to every FUNCTION_RENAMES key, since those were captured
    as raw sub_XXXX offsets from the decompiler (which is typically relative
    to the flash load address). DATA_RENAMES addresses are already absolute
    RAM addresses (0x2000xxxx) and are used as-is.
    """
    renamed_funcs = 0
    missed_funcs = []
    for offset, name in FUNCTION_RENAMES.items():
        addr = base + offset
        func = bv.get_function_at(addr)
        if func is None:
            missed_funcs.append((hex(addr), name))
            continue
        func.name = name
        renamed_funcs += 1

    renamed_vars = 0
    missed_vars = []
    for addr, (name, typestr, comment) in DATA_RENAMES.items():
        try:
            if typestr is not None:
                t = bv.parse_type_string(typestr)[0]
                bv.define_user_data_var(addr, t)
            var = bv.get_data_var_at(addr)
            if var is not None:
                var.name = name
            else:
                bv.define_user_symbol(bn.Symbol(bn.SymbolType.DataSymbol, addr, name))
            if comment:
                bv.set_comment_at(addr, comment)
            renamed_vars += 1
        except Exception as e:
            missed_vars.append((hex(addr), name, str(e)))

    print(f"[288r rename] functions renamed: {renamed_funcs}/{len(FUNCTION_RENAMES)}")
    if missed_funcs:
        print(f"[288r rename] functions NOT found (create them first / check base): {missed_funcs}")
    print(f"[288r rename] data vars renamed: {renamed_vars}/{len(DATA_RENAMES)}")
    if missed_vars:
        print(f"[288r rename] data vars with issues: {missed_vars}")

    bv.update_analysis_and_wait()
    return renamed_funcs, renamed_vars


if __name__ == "__main__":
    # Allow running as: binaryninja --script rename_288r.py firmware.bndb
    import sys
    if len(sys.argv) > 1:
        bv = bn.load(sys.argv[1])
        apply(bv)
        bv.save_auto_snapshot()
    else:
        print("Import this module and call apply(bv) from Binary Ninja's console instead.")
