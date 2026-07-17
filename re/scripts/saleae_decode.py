#!/usr/bin/env python3
"""
Saleae capture decoders for the 288r bench labelling session.

Turns a Logic 2 digital CSV export into the SAME words the firmware sees, so a
capture self-labels the [BENCH] maps instead of us guessing:

  165 switch chain (PA4 latch / PA5 clk / PA6 data)
      -> the 13-bit panel_switch_bits word per scan frame, MSB-first (matches
         bsp_panel_switches_read + panel_ctl bit indices). Toggle one switch while
         capturing and `--changes` prints exactly which bit moved.

  SPI2 control ADC (PB12 CS / PB13 SCK / PB14 MISO, optional MOSI)
      -> the 3 bytes + the 12-bit value ((b1&0x0F)<<8)|b2 per frame; if MOSI is
         probed, the command byte (0xA0/0xE0) labels the channel (0=CV, 1=knob).

CSV format: Logic 2 "Export Data" digital CSV — first column is time, the rest are
one column per channel (a transition list: each row holds the full level of every
channel at that time). Channels are selected by 0-based column index or header name.

    python saleae_decode.py 165 cap.csv --latch 0 --clk 1 --data 2 --changes
    python saleae_decode.py spi cap.csv --cs 0 --sck 1 --miso 2 --mosi 3
    python saleae_decode.py --selftest      # no capture needed; validates the decoders
"""
import csv, sys, argparse


def load_csv(path):
    """-> (channel_names, rows) where rows = [(time_float, [int levels...])]."""
    with open(path, newline="") as f:
        r = csv.reader(f)
        header = next(r)
        names = [h.strip() for h in header[1:]]
        rows = []
        for line in r:
            if not line or not line[0].strip():
                continue
            t = float(line[0])
            levels = [1 if x.strip() not in ("0", "", "0.0") else 0 for x in line[1:]]
            rows.append((t, levels))
    return names, rows


def _resolve(names, sel):
    """Channel selector: 0-based index, or a header name (exact or 'Channel N')."""
    try:
        return int(sel)
    except (TypeError, ValueError):
        pass
    for i, n in enumerate(names):
        if n == sel:
            return i
    raise SystemExit(f"channel {sel!r} not found in {names}")


def decode_165(rows, li, ci, di, nbits=13, edge="falling"):
    """Reconstruct one word per latch frame. Samples DATA on the given CLK edge
    (firmware drives CLK low then reads -> 'falling'), MSB-first like the firmware."""
    frames, in_frame, bits, ft, prev = [], False, [], 0.0, None
    for (t, lv) in rows:
        if prev is not None:
            latch_rise = prev[li] == 0 and lv[li] == 1
            clk_edge = ((edge == "falling" and prev[ci] == 1 and lv[ci] == 0) or
                        (edge == "rising"  and prev[ci] == 0 and lv[ci] == 1))
            if latch_rise:
                in_frame, bits, ft = True, [], t
            elif in_frame and clk_edge:
                bits.append(lv[di])
                if len(bits) == nbits:
                    word = 0
                    for b in bits:            # first sampled bit -> MSB
                        word = (word << 1) | b
                    frames.append((ft, word))
                    in_frame = False
        prev = lv
    return frames


def _bits_to_bytes(bits):
    out = []
    for i in range(0, len(bits) - 7, 8):
        b = 0
        for k in range(8):
            b = (b << 1) | bits[i + k]
        out.append(b)
    return out


def decode_spi(rows, csi, scki, misoi, mosii=None, edge="rising"):
    """Reconstruct MISO (and optional MOSI) bytes per CS-active frame + the 12-bit
    ADC value. Samples on the given SCK edge (MCP320x-style: 'rising')."""
    frames, active, mi, mo, ft, prev = [], False, [], [], 0.0, None
    for (t, lv) in rows:
        if prev is not None:
            cs_fall = prev[csi] == 1 and lv[csi] == 0
            cs_rise = prev[csi] == 0 and lv[csi] == 1
            sck_edge = ((edge == "rising"  and prev[scki] == 0 and lv[scki] == 1) or
                        (edge == "falling" and prev[scki] == 1 and lv[scki] == 0))
            if cs_fall:
                active, mi, mo, ft = True, [], [], t
            elif active and sck_edge:
                mi.append(lv[misoi])
                if mosii is not None:
                    mo.append(lv[mosii])
            elif active and cs_rise:
                miso_b = _bits_to_bytes(mi)
                mosi_b = _bits_to_bytes(mo) if mosii is not None else []
                adc = (((miso_b[1] & 0x0F) << 8) | miso_b[2]) if len(miso_b) >= 3 else None
                ch = None
                if len(mosi_b) >= 2:
                    ch = 0 if mosi_b[1] == 0xA0 else 1 if mosi_b[1] == 0xE0 else None
                frames.append((ft, miso_b, mosi_b, adc, ch))
                active = False
        prev = lv
    return frames


def changed_bits(words, nbits=13):
    """Bit positions that took both 0 and 1 across `words` (= the toggled switch)."""
    seen0 = seen1 = 0
    for w in words:
        for b in range(nbits):
            if (w >> b) & 1:
                seen1 |= (1 << b)
            else:
                seen0 |= (1 << b)
    both = seen0 & seen1
    return [b for b in range(nbits) if (both >> b) & 1]


# --------------------------------------------------------------------------- #
# synthetic-capture self-test (no hardware): generate a known frame, decode it #
# --------------------------------------------------------------------------- #
def _gen_165(word, nbits=13):
    """latch idles high, clk idles high; pulse latch low->high, then nbits clocks
    (clk high->low = sample point) presenting `word` MSB-first on data."""
    rows, t = [], 0.0
    def push(la, cl, da):
        nonlocal t
        rows.append((t, [la, cl, da])); t += 1e-6
    push(1, 1, 0)                       # idle
    push(0, 1, 0); push(1, 1, 0)        # latch load pulse -> rising edge = frame start
    for i in range(nbits):
        bit = (word >> (nbits - 1 - i)) & 1     # MSB first
        push(1, 0, bit)                 # clk falling: data present at sample
        push(1, 1, bit)                 # clk rising
    push(1, 1, 0)
    return rows


def _gen_spi(adc12, channel):
    """CS low, 24 SCK rising edges. MISO bytes = [0x00, hi, lo] with the 12-bit adc
    in ((hi&0x0F)<<8)|lo; MOSI bytes = [0x01, cmd, 0x00], cmd=0xA0(ch0)/0xE0(ch1)."""
    miso = [0x00, (adc12 >> 8) & 0x0F, adc12 & 0xFF]
    cmd = 0xA0 if channel == 0 else 0xE0
    mosi = [0x01, cmd, 0x00]
    mbits = [(byte >> (7 - k)) & 1 for byte in miso for k in range(8)]
    obits = [(byte >> (7 - k)) & 1 for byte in mosi for k in range(8)]
    rows, t = [], 0.0
    def push(cs, sck, mi, mo):
        nonlocal t
        rows.append((t, [cs, sck, mi, mo])); t += 1e-6
    push(1, 0, 0, 0)                    # idle, CS high
    push(0, 0, mbits[0], obits[0])      # CS falling
    for i in range(24):
        push(0, 1, mbits[i], obits[i])  # SCK rising: sample
        nxt = mbits[i + 1] if i + 1 < 24 else 0
        nxo = obits[i + 1] if i + 1 < 24 else 0
        push(0, 0, nxt, nxo)            # SCK falling: present next
    push(1, 0, 0, 0)                    # CS rising: frame end
    return rows


def _selftest():
    fails = 0
    def ck(name, cond):
        nonlocal fails
        print(f"  {name:<44} {'ok' if cond else 'FAIL'}")
        if not cond:
            fails += 1

    for w in (0x0000, 0x1555, 0x0AAA, 0x1FFF, 0x0041):
        got = decode_165(_gen_165(w), 0, 1, 2)
        ck(f"165 word 0x{w:04X} round-trips", len(got) == 1 and got[0][1] == w)

    # two frames differing only in bit 3 -> changed_bits reports exactly [3]
    two = decode_165(_gen_165(0x0000) + _gen_165(0x0008), 0, 1, 2)
    ck("165 changed-bit finder isolates bit 3",
       changed_bits([w for _, w in two]) == [3])

    for adc, ch in ((0, 0), (2048, 1), (4095, 0), (1234, 1)):
        got = decode_spi(_gen_spi(adc, ch), 0, 1, 2, 3)
        ok = len(got) == 1 and got[0][3] == adc and got[0][4] == ch
        ck(f"spi adc={adc:4d} ch={ch} round-trips", ok)

    print("\nALL PASS" if fails == 0 else f"\nFAILED ({fails})")
    return fails


def main():
    ap = argparse.ArgumentParser(description="288r Saleae capture decoders")
    ap.add_argument("mode", nargs="?", choices=["165", "spi"])
    ap.add_argument("csv", nargs="?")
    ap.add_argument("--latch"); ap.add_argument("--clk"); ap.add_argument("--data")
    ap.add_argument("--cs"); ap.add_argument("--sck"); ap.add_argument("--miso"); ap.add_argument("--mosi")
    ap.add_argument("--changes", action="store_true", help="print which bits toggled across frames")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        sys.exit(1 if _selftest() else 0)
    if not a.mode or not a.csv:
        ap.error("need MODE and CSV (or --selftest)")

    names, rows = load_csv(a.csv)
    if a.mode == "165":
        li, ci, di = (_resolve(names, x) for x in (a.latch, a.clk, a.data))
        frames = decode_165(rows, li, ci, di)
        for t, w in frames:
            print(f"  {t:.6f}  0x{w:04X}  {w:013b}")
        print(f"  {len(frames)} frames")
        if a.changes:
            print("  toggled bit(s):", changed_bits([w for _, w in frames]))
    else:
        csi, scki, misoi = (_resolve(names, x) for x in (a.cs, a.sck, a.miso))
        mosii = _resolve(names, a.mosi) if a.mosi is not None else None
        for t, mi, mo, adc, ch in decode_spi(rows, csi, scki, misoi, mosii):
            chs = f" ch{ch}({'CV' if ch == 0 else 'knob'})" if ch is not None else ""
            print(f"  {t:.6f}  miso={[hex(b) for b in mi]}  adc={adc}{chs}")


if __name__ == "__main__":
    main()
