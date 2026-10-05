#!/usr/bin/env python3
"""Check an iwr1843_sar_lvds cfg against the IWR1843 / SDK 3.6 limits and the SAR frame-boundary rule.

    uv run python sar_cfg_check.py CFG [--max-range M] [--speed V] [--dmax D]
                                       [--tb-min US] [--if-margin F] [--lvds-margin X] [--json]

Stdlib only, no hardware. Exit 0 = no violation, 1 = at least one violation, 2 = usage / unreadable file.
Read docs/sar_cfg_guide.md first: it explains every message and every limit. Limits and their sources are
tabulated in docs/sar_feasibility.md (a); SDK paths are relative to
mmwave_sdk_03_06_02_00-LTS/packages/ti and rl_sensor.h is control/mmwavelink/include/rl_sensor.h.

The profile/chirp/frame arithmetic copies what the SDK CLI does with the typed numbers
(utils/cli/src/cli_mmwave.c): float32 conversion, truncation toward zero, LSB units. The checker therefore
reports the *realized* slope, start frequency, times and frame period, not the typed ones.
"""
import argparse
import json
import math
import struct
import sys

C = 299792458.0                      # m/s
FREQ_SCALE = 3.6                     # xWR1xxx 76-81 GHz: 1 LSB start = 3.6e9/2^26 Hz (rl_sensor.h:640-645)
LVDS_BYTES_PER_US = 2 * 600 / 8      # 2 lanes x 600 Mbps (DDR rate) = 150 MB/s (src/mss/mmw_lvds_stream.c:142)
ADCBUF_HALF = 16384                  # B: one ping/pong half of the 32 KB ADC buffer (sar_feasibility.md (a))
MAX_PERIOD_TICKS = (1 << 28) - 1     # frame period 5 ns LSB, max 1.342 s (rl_sensor.h:986-987)
HPF1_KHZ = (175, 235, 350, 700)      # rl_sensor.h:773-791
HPF2_KHZ = (350, 700, 1400, 2800)

# Commands this firmware accepts: name -> allowed argument counts (src/mss/mmw_cli.c table, SDK CLI extension).
ACCEPTED = {
    "flushCfg": (0,), "dfeDataOutputMode": (1,), "channelCfg": (3,), "adcCfg": (2,), "profileCfg": (14,),
    "chirpCfg": (8,), "frameCfg": (7,), "lowPower": (2,), "version": (0,), "sensorStart": (0, 1),
    "sensorStop": (0,), "adcbufCfg": (5,), "CQRxSatMonitor": (5,), "CQSigImgMonitor": (3,),
    "analogMonitor": (2,), "lvdsStreamCfg": (4,), "queryDemoStatus": (0,), "sarStats": (0,),
    "calibData": (3,),
}
# Stock-demo commands removed in firmware-07 (README "What changed vs TI"): the CLI answers "not a valid command"
# and a cfg stops there.
REMOVED = ("guiMonitor", "cfarCfg", "multiObjBeamForming", "calibDcRangeSig", "clutterRemoval",
           "compRangeBiasAndRxChanPhase", "measureRangeBiasAndRxChanPhase", "aoaFovCfg", "cfarFovCfg",
           "extendedMaxVelocity", "configDataPort")
# In the CLI table but unusable here: frame mode only (mmw_cli.c:144).
UNSUPPORTED = ("advFrameCfg", "subFrameCfg", "contModeCfg", "bpmCfgAdvanced")


def f32(x):
    """Round to IEEE float32, as the C `(float)` casts in the SDK CLI do."""
    return struct.unpack("f", struct.pack("f", x))[0]


def to_int(tok):
    """C atoi: leading integer, 0 if none."""
    s = tok.strip()
    sign = -1 if s.startswith("-") else 1
    s = s.lstrip("+-")
    n = 0
    while n < len(s) and s[n].isdigit():
        n += 1
    return sign * int(s[:n]) if n else 0


def to_float(tok):
    """C atof: longest leading float, 0.0 if none."""
    s = tok.strip()
    for end in range(len(s), 0, -1):
        try:
            return float(s[:end])
        except ValueError:
            continue
    return 0.0


def ticks(tok, mult, div):
    """`(uint32_t)((float)atof(x) * mult / div)`: the CLI's us/ms -> LSB conversions, in float32."""
    return int(f32(f32(f32(to_float(tok)) * mult) / div))


def parse_cfg(text):
    """Return [(lineno, cmd, [args])]. A line starting with '%' is a comment (SDK cli.c:157); blanks skipped."""
    out = []
    for no, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("%"):
            continue
        tok = line.split()
        out.append((no, tok[0], tok[1:]))
    return out


class Report:
    def __init__(self):
        self.errors, self.warnings, self.values = [], [], {}

    def err(self, code, msg):
        self.errors.append("[%s] %s" % (code, msg))

    def warn(self, code, msg):
        self.warnings.append("[%s] %s" % (code, msg))


def popcount(n):
    return bin(n & 0xFF).count("1")


def check_commands(cmds, rep):
    """Only commands this firmware accepts, with the right argument count; collect the cfg state."""
    st = {"profiles": {}, "chirps": [], "frame": None, "channel": None, "adccfg": None, "adcbuf": None,
          "lvds": None, "dfe": None, "analog": None, "satmon": {}, "calib": None}
    for no, cmd, args in cmds:
        where = "line %d `%s`" % (no, cmd)
        if cmd in REMOVED:
            rep.err("CMD", "%s: stock-demo command removed from this firmware (the CLI rejects it and the cfg "
                           "stops here); delete it" % where)
            continue
        if cmd in UNSUPPORTED:
            rep.err("CMD", "%s: advanced frame / continuous mode / BPM are not supported (frame mode only); "
                           "delete it" % where)
            continue
        if cmd not in ACCEPTED:
            rep.err("CMD", "%s: unknown command (not in this firmware's CLI table)" % where)
            continue
        if len(args) not in ACCEPTED[cmd]:
            want = " or ".join(str(n) for n in ACCEPTED[cmd])
            rep.err("CMD", "%s: %d arguments, expected %s" % (where, len(args), want))
            continue
        ints = tuple(to_int(a) for a in args)
        if cmd == "dfeDataOutputMode":
            st["dfe"] = ints[0]
        elif cmd == "channelCfg":
            st["channel"] = ints
        elif cmd == "adcCfg":
            st["adccfg"] = ints
        elif cmd == "profileCfg":
            st["profiles"][ints[0]] = args
        elif cmd == "chirpCfg":
            st["chirps"].append(args)
        elif cmd == "frameCfg":
            st["frame"] = args
        elif cmd == "adcbufCfg":
            st["adcbuf"] = ints
        elif cmd == "lvdsStreamCfg":
            st["lvds"] = ints
        elif cmd == "analogMonitor":
            st["analog"] = ints
        elif cmd == "calibData":
            st["calib"] = ints
        elif cmd == "CQRxSatMonitor":
            st["satmon"][ints[0]] = ints
    return st


def realize_profile(args):
    """Typed profileCfg -> realized values, following CLI_MMWaveProfileCfg (cli_mmwave.c:613-680)."""
    p = {}
    p["id"] = to_int(args[0])
    p["start_code"] = int(to_float(args[1]) * (1 << 26) / f32(FREQ_SCALE))          # uint32 truncation
    p["start_ghz"] = p["start_code"] * FREQ_SCALE / (1 << 26)                        # 3.6e9 Hz per 2^26
    p["idle_t"] = ticks(args[2], 1000, 10)                                          # 10 ns units
    p["adc_start_t"] = ticks(args[3], 1000, 10)
    p["ramp_end_t"] = ticks(args[4], 1000, 10)
    p["slope_typed"] = to_float(args[7])
    p["slope_code"] = int(to_float(args[7]) * (1 << 26) / ((f32(FREQ_SCALE) * 1e3) * 900.0))  # int16, trunc to 0
    p["slope_mhz_us"] = p["slope_code"] * FREQ_SCALE * 1e3 * 900 / (1 << 26)            # 48.279 kHz/us per LSB
    p["tx_start_t"] = ticks(args[8], 1000, 10)
    p["ns"] = to_int(args[9])
    p["rate_ksps"] = to_int(args[10])
    p["hpf1"], p["hpf2"], p["gain"] = to_int(args[11]), to_int(args[12]), to_int(args[13])
    return p


def check_profile(p, st, rep, opt):
    r = rep.values
    ramp_us, idle_us, adc0_us = p["ramp_end_t"] / 100.0, p["idle_t"] / 100.0, p["adc_start_t"] / 100.0
    fs = p["rate_ksps"] * 1e3
    nrx = popcount(st["channel"][0]) if st["channel"] else 1
    complex_out = not (st["adcbuf"] and st["adcbuf"][1] != 0)
    bps = 4 if complex_out else 2

    # ADC rate: rl_sensor.h:742 (2000-37500 ksps); complex 1x max 18.75 Msps: rl_sensor.h:750.
    if not 2000 <= p["rate_ksps"] <= 37500:
        rep.err("RATE", "digOutSampleRate %d ksps is outside 2000-37500 ksps (rl_sensor.h:742); raise it to at "
                        "least 2000 and keep the ADC window and numAdcSamples consistent" % p["rate_ksps"])
    elif complex_out and p["rate_ksps"] > 18750:
        rep.err("RATE", "digOutSampleRate %d ksps exceeds 18750 ksps, the complex 1x maximum (rl_sensor.h:750)"
                % p["rate_ksps"])
    # Samples: 2 to MAX; one chirp must fit a 16 KB ADCBUF half (sar_feasibility.md (a)).
    if p["ns"] < 2:
        rep.err("ADCBUF", "numAdcSamples %d is below 2" % p["ns"])
    if p["ns"] * bps * nrx > ADCBUF_HALF:
        rep.err("ADCBUF", "numAdcSamples %d x %d B x %d RX = %d B exceeds the 16384 B ADC buffer half (max %d "
                          "samples); lower the ADC rate or shorten the ADC window" % (
                    p["ns"], bps, nrx, p["ns"] * bps * nrx, ADCBUF_HALF // (bps * nrx)))
    # Time fields: rl_sensor.h:653 (idle), :660 (adcStart 0-4095), :665 (rampEnd 0-500000), 10 ns LSB.
    if p["idle_t"] > 524287:
        rep.err("IDLE", "idleTime %.2f us exceeds 5242.87 us (rl_sensor.h:653)" % idle_us)
    if p["adc_start_t"] > 4095:
        rep.err("ADCWIN", "adcStartTime %.2f us exceeds 40.95 us (rl_sensor.h:659)" % adc0_us)
    if p["ramp_end_t"] > 500000:
        rep.err("RAMP", "rampEndTime %.2f us exceeds 5000 us (rl_sensor.h:665)" % ramp_us)
    # ADC window must end inside the ramp.
    window_us = p["ns"] * 1e6 / fs if fs else float("inf")
    r["adc_window_us"] = window_us
    if adc0_us + window_us > ramp_us + 1e-6:
        rep.err("ADCWIN", "ADC window ends at adcStart %.2f + %d/%.3f Msps = %.2f us, after rampEnd %.2f us; "
                          "shorten the window (fewer samples or a higher rate) or lengthen the ramp" % (
                    adc0_us, p["ns"], fs / 1e6, adc0_us + window_us, ramp_us))
    # Slope: LSB 48.279 kHz/us, +-2072 (rl_sensor.h:709-710).
    if p["slope_code"] == 0:
        rep.err("SLOPE", "slope %.4g MHz/us quantizes to code 0 (LSB 48.279 kHz/us); the chirp would not sweep"
                % p["slope_typed"])
    if abs(p["slope_code"]) > 2072:
        rep.err("SLOPE", "slope code %d is beyond +-2072 (100 MHz/us)" % p["slope_code"])
    # Sweep band, using the realized slope: rl_sensor.h:666-667 (76-78 or 77-81 GHz).
    f0 = p["start_ghz"]
    f1 = f0 + p["slope_mhz_us"] * ramp_us / 1e3
    lo, hi = min(f0, f1), max(f0, f1)
    r["sweep_ghz"] = (f0, f1)
    if not ((77.0 <= lo and hi <= 81.0) or (76.0 <= lo and hi <= 78.0)):
        rep.err("SWEEP", "sweep %.4f-%.4f GHz (start + realized slope x rampEnd) is not inside 77-81 or 76-78 GHz "
                         "(rl_sensor.h:666-667); lower startFreq, the slope or rampEnd" % (lo, hi))
    # Gain: even 24-52 in the API (rl_sensor.h:832); the datasheet specifies 24-48.
    if p["gain"] % 2 or not 24 <= p["gain"] <= 52:
        rep.err("GAIN", "rxGain %d dB must be an even value 24-52 (rl_sensor.h:832)" % p["gain"])
    elif p["gain"] > 48:
        rep.warn("GAIN", "rxGain %d dB is above the 48 dB the datasheet specifies" % p["gain"])
    hpf_ok = 0 <= p["hpf1"] <= 3 and 0 <= p["hpf2"] <= 3
    if not hpf_ok:
        rep.err("HPF", "hpfCornerFreq1/2 codes must be 0-3 (rl_sensor.h:773-791)")

    # Derived numbers from the realized slope and the sampled bandwidth.
    slope = abs(p["slope_mhz_us"]) * 1e12
    bw = slope * window_us * 1e-6
    r.update(slope_mhz_us=p["slope_mhz_us"], slope_typed=p["slope_typed"], slope_code=p["slope_code"],
             start_ghz=f0, bw_sampled_mhz=bw / 1e6, fs_ksps=p["rate_ksps"], ns=p["ns"], nrx=nrx,
             range_res_m=C / (2 * bw) if bw else None,
             max_if_range_m=opt.if_margin * fs * C / (2 * slope) if slope else None,
             rx_gain=p["gain"], hpf1_khz=HPF1_KHZ[p["hpf1"]] if hpf_ok else None,
             hpf2_khz=HPF2_KHZ[p["hpf2"]] if hpf_ok else None)
    if opt.max_range is not None and slope:
        fb = 2 * slope * opt.max_range / C
        r["beat_at_max_range_mhz"] = fb / 1e6
        if fb > opt.if_margin * fs:
            rep.err("IF", "beat frequency at %.1f m is %.3f MHz, above %.2f x fs = %.3f MHz; raise the ADC rate, "
                          "lower the slope or reduce the range" % (opt.max_range, fb / 1e6, opt.if_margin,
                                                                   opt.if_margin * fs / 1e6))
    return nrx, bps


def hpf_table(slope_hz_per_s, hpf1_khz, hpf2_khz):
    """Corner ranges and the two-HPF attenuation at reference ranges (first-order highpass, datasheet 7.7)."""
    out = {"corner_range_m": {}, "attenuation_db": {}}
    for fc in sorted(set(HPF1_KHZ + HPF2_KHZ)):
        out["corner_range_m"][str(fc)] = fc * 1e3 * C / (2 * slope_hz_per_s)
    for rng in (1, 2, 5, 10, 20, 50, 100):
        fb = 2 * slope_hz_per_s * rng / C
        out["attenuation_db"][str(rng)] = sum(10 * math.log10(1 + (fc * 1e3 / fb) ** 2)
                                              for fc in (hpf1_khz, hpf2_khz))
    return out


def check_frame(st, profiles, rep, opt, nrx, bps):
    r = rep.values
    if not st["frame"]:
        rep.err("FRAME", "no frameCfg")
        return
    fa = st["frame"]
    start, end, loops = to_int(fa[0]), to_int(fa[1]), to_int(fa[2])
    period_t = ticks(fa[4], 1000000, 5)                                  # 5 ns units (cli_mmwave.c:807-835)
    trig = to_int(fa[5])
    # rl_sensor.h:949-958: indices 0-511 with end >= start, loops 1-255.
    if not (0 <= start <= end <= 511):
        rep.err("LOOPS", "chirp indices %d..%d must satisfy 0 <= start <= end <= 511 (rl_sensor.h:949-953)"
                % (start, end))
        return
    if not 1 <= loops <= 255:
        rep.err("LOOPS", "numLoops %d must be 1-255 (rl_sensor.h:958); for more chirps per frame use several "
                         "identical chirp indices" % loops)
    if trig not in (1, 2):
        rep.err("FRAME", "triggerSelect %d must be 1 (software) or 2 (hardware)" % trig)
    elif trig == 2:
        rep.warn("FRAME", "triggerSelect 2 is hardware trigger: frames start only on an external SYNC_IN pulse")
    if to_float(fa[6]) != 0:
        rep.warn("FRAME", "frameTriggerDelay %s ms is non-zero and delays the first frame only" % fa[6])

    # Chirps used by the frame.
    chirp = {}
    for a in st["chirps"]:
        s, e = to_int(a[0]), to_int(a[1])
        if not (0 <= s <= e <= 511):
            rep.err("LOOPS", "chirpCfg indices %d..%d must be within 0-511 (rl_sensor.h:949-953)" % (s, e))
            continue
        for i in range(s, e + 1):
            chirp[i] = a
    used = [chirp.get(i) for i in range(start, end + 1)]
    if any(c is None for c in used):
        rep.err("LOOPS", "frameCfg uses chirp indices %d..%d but a chirpCfg does not define all of them"
                % (start, end))
        return
    pids = {to_int(c[2]) for c in used}
    if len(pids) != 1:
        rep.err("PROFILE", "frame chirps use profiles %s; this firmware reads one profile per run (one Tc, one "
                           "ADC window): use a single profile" % sorted(pids))
        return
    pid = next(iter(pids))
    if pid not in profiles:
        rep.err("PROFILE", "chirpCfg refers to profile %d which has no profileCfg" % pid)
        return
    p = profiles[pid]
    # Per-chirp variations: the idle variation adds to the period (10 ns units).
    tcs, nonzero_var = set(), False
    for c in used:
        nonzero_var = nonzero_var or any(to_float(c[i]) != 0 for i in (3, 4, 5, 6))
        tcs.add(p["idle_t"] + ticks(c[5], 1000, 10) + p["ramp_end_t"])
    if nonzero_var:
        rep.warn("CHIRP", "chirpCfg start/slope/idle/adcStart variations are non-zero: SAR needs identical "
                          "chirps; the checker only accounts for the idle variation")
    if len(tcs) != 1:
        rep.err("PERIOD", "chirps of the frame have different periods %s us; SAR needs a uniform Tc"
                % sorted(t / 100.0 for t in tcs))
        return
    tc_t = next(iter(tcs))
    tx_en = {to_int(c[7]) for c in used}
    if st["channel"] and any(t & ~st["channel"][1] for t in tx_en):
        rep.err("TX", "chirpCfg txEnable %s is not enabled in channelCfg txChannelEn %d" % (
            sorted(tx_en), st["channel"][1]))
    if st["channel"] and popcount(st["channel"][1]) != 1:
        rep.warn("TX", "channelCfg enables %d TX; this guide and the checker assume 1TX" % popcount(st["channel"][1]))

    nc = (end - start + 1) * loops
    tc_us = tc_t / 100.0
    period_us = period_t * 5 / 1000.0
    tb_us = period_us - nc * tc_us
    r.update(tc_us=tc_us, idle_us=p["idle_t"] / 100.0, ramp_end_us=p["ramp_end_t"] / 100.0, nchirps=nc,
             loops=loops, chirp_indices=end - start + 1, frame_period_ms=period_us / 1000.0, tb_us=tb_us,
             dead_time_pct=100.0 * tb_us / period_us if period_us else None, boundary_step_us=tc_us + tb_us)
    if tc_us < 15:
        rep.err("PERIOD", "chirp period %.2f us is below the 15 us minimum chirp cycle (rl_sensor.h:4570)" % tc_us)
    # Frame period: 300 us to 1.342 s, LSB 5 ns (rl_sensor.h:986-987).
    if period_t > MAX_PERIOD_TICKS:
        rep.err("PERIOD", "framePeriodicity %.3f ms exceeds the 1342.177 ms maximum (rl_sensor.h:986-987); use "
                          "fewer chirps per frame (Nc <= (1342.177 ms - Tb)/Tc = %d here)" % (
                    period_us / 1000.0, int((MAX_PERIOD_TICKS * 5 / 1000.0 - opt.tb_min) // tc_us)))
    # Blank rule: Tb = period - Nc x Tc >= tb-min (sar_feasibility.md (c)).
    if tb_us < opt.tb_min - 1e-6:
        rep.err("TB", "inter-frame blank Tb = framePeriodicity %.4f ms - %d x %.2f us = %.2f us is below the %.0f us "
                      "minimum; framePeriodicity must be at least %.4f ms" % (
                    period_us / 1000.0, nc, tc_us, tb_us, opt.tb_min, (nc * tc_us + opt.tb_min) / 1000.0))
    elif tb_us < opt.tb_min + 500:
        rep.warn("TB-OPEN", "Tb %.0f us leaves no time for the APLL (150 us) + synth VCO (350 us) calibrations "
                            "TI runs internally; open pending bench: raise Tb by up to +500 us only if boundary "
                            "steps show (sar_feasibility.md (c))" % tb_us)

    # LVDS bytes per chirp (docs/lvds_data_format.md section 1; sar_feasibility.md (b)).
    lv = st["lvds"]
    fmt = lv[2] if lv else 0
    hdr = lv[1] if lv else 0
    adc_b = p["ns"] * bps * nrx
    if fmt == 0:
        rep.warn("LVDS", "lvdsStreamCfg is missing or dataFmt 0: nothing is streamed over LVDS")
        bchirp = 0
    elif fmt == 2:
        h = 0 if not hdr else (64 if (p["ns"] * nrx) % 4 == 0 else 56)
        bchirp = h + adc_b + 64
    else:
        bchirp = (adc_b + 52 + 255) // 256 * 256 if hdr else adc_b
        if fmt == 4:
            rep.warn("LVDS", "dataFmt 4 (CP+ADC+CQ) adds profile/CQ blocks the checker does not size")
    cap = tc_us * LVDS_BYTES_PER_US
    r.update(bchirp_bytes=bchirp, lvds_capacity_bytes=cap, mbytes_per_s=bchirp / tc_us if tc_us else None,
             lvds_pct=100.0 * bchirp / cap if cap else None)
    if bchirp > cap:
        rep.err("LVDS", "%d B per chirp exceeds the %.0f B the 2 x 600 Mbps LVDS link carries in one Tc "
                        "(%.2f us)" % (bchirp, cap, tc_us))
    elif bchirp * opt.lvds_margin > cap:
        rep.err("LVDS", "%d B per chirp uses %.1f%% of the LVDS link, above 1/%.0f of capacity (go/no-go margin, "
                        "sar_feasibility.md (f)); lengthen Tc or cut samples, or pass --lvds-margin 1 to waive" % (
                    bchirp, 100.0 * bchirp / cap, opt.lvds_margin))

    # Along-track spacing: boundary step = Tc + Tb, in-frame step = Tc.
    if opt.speed is not None:
        v = opt.speed
        r.update(speed_mps=v, spacing_in_frame_m=v * tc_us * 1e-6, spacing_boundary_m=v * (tc_us + tb_us) * 1e-6)
        if opt.dmax is not None:
            r["dmax_m"] = opt.dmax
            if v * (tc_us + tb_us) * 1e-6 > opt.dmax * (1 + 1e-9):
                rep.err("SPACING", "boundary step v x (Tc + Tb) = %.3f m/s x %.1f us = %.3f mm exceeds d_max %.3f "
                                   "mm; need Tc + Tb <= %.1f us (shorten Tc or Tb)" % (
                            v, tc_us + tb_us, v * (tc_us + tb_us) * 1e-3, opt.dmax * 1e3, opt.dmax / v * 1e6))

    # Monitors lengthen the blank; the saturation monitor must fit the ADC window.
    an = st["analog"]
    if an and an[1]:
        rep.warn("CAL", "analogMonitor sigImgBand=1 enables the signal/image monitor; TI adds monitor durations "
                        "to the inter-frame blank (rl_sensor.h:4549-4558): add them to Tb")
    if an and an[0]:
        sm = st["satmon"].get(pid)
        if sm is None:
            rep.err("SAT", "analogMonitor rxSaturation=1 needs a CQRxSatMonitor line for profile %d" % pid)
        else:
            prim = (sm[3] + 1) // 2
            dur_us = prim * sm[2] * 0.16
            r["satmon_window_us"] = dur_us
            if sm[3] > 127 or sm[3] < 1 or sm[2] < 4:
                rep.err("SAT", "CQRxSatMonitor numSlices must be 1-127 and priSliceDuration >= 4 "
                               "(rl_monitoring.h:1913-1937)")
            elif dur_us > r["adc_window_us"] + 1e-6:
                rep.warn("SAT", "CQRxSatMonitor covers %.1f us (%d primary slices x %d x 0.16 us), longer than "
                                "the %.1f us ADC window (rl_monitoring.h:1913-1937)" % (
                            dur_us, prim, sm[2], r["adc_window_us"]))
    # Periodic calibration is off in this firmware (src/mss/mss_main.c, enablePeriodicity = false); no cfg command re-enables it.


def check_modes(st, rep):
    """dfeDataOutputMode, adcCfg, adcbufCfg, lvdsStreamCfg requirements of this firmware."""
    if st["dfe"] is None:
        rep.err("MODE", "dfeDataOutputMode is missing (must be 1, frame)")
    elif st["dfe"] != 1:
        rep.err("MODE", "dfeDataOutputMode %d: only 1 (frame) is supported (mmw_cli.c:144)" % st["dfe"])
    if not st["channel"]:
        rep.err("MODE", "channelCfg is missing")
    ac = st["adccfg"]
    if not ac:
        rep.err("MODE", "adcCfg is missing")
    else:
        if ac[0] != 2:
            rep.err("MODE", "adcCfg numADCBits %d: use 2 (16 bit)" % ac[0])
        if ac[1] != 1:
            rep.warn("MODE", "adcCfg adcOutputFmt %d: the SAR path is written for 1 (complex 1x)" % ac[1])
    ab = st["adcbuf"]
    if not ab:
        rep.err("MODE", "adcbufCfg is missing")
    else:
        if ab[0] != -1:
            rep.err("MODE", "adcbufCfg subFrameIdx %d: use -1" % ab[0])
        if ab[4] != 1:
            rep.err("ADCBUF", "adcbufCfg chirpThreshold %d: must be 1 (mmw_cli.c:420)" % ab[4])
        if ab[3] != 1:
            rep.warn("MODE", "adcbufCfg chInterleave %d: the DCA1000 parser assumes non-interleaved (1)" % ab[3])
    # sensorStart refuses a start unless adcbufCfg, lvdsStreamCfg, analogMonitor and calibData were all sent
    # (mmw_cli.c:160-168, mss_main.c MmwDemo_isAllCfgInPendingState); analogMonitor 0 0 and calibData 0 0 0 are the neutral values.
    if st["analog"] is None:
        rep.err("FULLCFG", "analogMonitor is missing; sensorStart needs it even when unused (analogMonitor 0 0)")
    if st["calib"] is None:
        rep.err("FULLCFG", "calibData is missing; sensorStart needs it even when unused (calibData 0 0 0)")
    elif st["calib"][0] == 1 and st["calib"][1] == 1:
        rep.err("FULLCFG", "calibData save and restore cannot both be 1 (mmw_cli.c:709)")
    lv = st["lvds"]
    if not lv:
        rep.err("LVDSCFG", "lvdsStreamCfg is missing")
    else:
        if lv[0] != -1:
            rep.err("LVDSCFG", "lvdsStreamCfg subFrameIdx %d: use -1" % lv[0])
        if lv[3] != 0:
            rep.err("LVDSCFG", "lvdsStreamCfg enableSW %d: must be 0, there is no SW session (mmw_cli.c:622)" % lv[3])
        if lv[2] not in (0, 1, 2, 4):
            rep.err("LVDSCFG", "lvdsStreamCfg dataFmt %d: must be 0, 1, 2 or 4 (mmw_cli.c:631)" % lv[2])
        if lv[2] == 2 and ab and ab[3] != 1:
            rep.err("LVDSCFG", "lvdsStreamCfg dataFmt 2 needs non-interleaved ADC data: adcbufCfg ChanInterleave "
                               "must be 1 (mss_main.c MmwDemo_configSensor)")
        if lv[2] == 2 and ab and ab[1] != 0:
            rep.err("LVDSCFG", "lvdsStreamCfg dataFmt 2 needs complex ADC output: adcbufCfg adcOutputFmt must be "
                               "0 (mss_main.c MmwDemo_configSensor)")


def analyze(text, opt):
    rep = Report()
    st = check_commands(parse_cfg(text), rep)
    check_modes(st, rep)
    if not st["profiles"]:
        rep.err("PROFILE", "no profileCfg")
        return rep
    profiles = {pid: realize_profile(args) for pid, args in st["profiles"].items()}
    used_pid = None
    if st["chirps"]:
        used_pid = to_int(st["chirps"][0][2])
    p = profiles.get(used_pid) or next(iter(profiles.values()))
    nrx, bps = check_profile(p, st, rep, opt)
    # dataFmt 2: numAdcSamples x RX even (mss_main.c MmwDemo_configSensor).
    if st["lvds"] and st["lvds"][2] == 2 and (p["ns"] * nrx) % 2:
        rep.err("LVDSCFG", "lvdsStreamCfg dataFmt 2 needs numAdcSamples x RX channels even (%d x %d)" % (p["ns"], nrx))
    check_frame(st, profiles, rep, opt, nrx, bps)
    v = rep.values
    if v.get("slope_mhz_us"):
        v["hpf"] = hpf_table(abs(v["slope_mhz_us"]) * 1e12, v.get("hpf1_khz") or 175, v.get("hpf2_khz") or 350)
    return rep


def render(rep, path):
    r = rep.values
    L = ["cfg: %s" % path]
    if "slope_code" in r:
        L.append("profile: start %.4f GHz, slope code %d = %.4f MHz/us (typed %.4g), sweep %.4f-%.4f GHz" % (
            r["start_ghz"], r["slope_code"], r["slope_mhz_us"], r["slope_typed"], r["sweep_ghz"][0], r["sweep_ghz"][1]))
        L.append("ADC: %d ksps, %d samples x %d RX, window %.1f us; gain %d dB, HPF %s + %s kHz" % (
            r["fs_ksps"], r["ns"], r["nrx"], r["adc_window_us"], r["rx_gain"], r.get("hpf1_khz"), r.get("hpf2_khz")))
        if r["range_res_m"]:
            L.append("range resolution %.2f cm (sampled bandwidth %.0f MHz); max IF range %.1f m" % (
                100 * r["range_res_m"], r["bw_sampled_mhz"], r["max_if_range_m"] or 0))
        if "beat_at_max_range_mhz" in r:
            L.append("beat at max range %.3f MHz" % r["beat_at_max_range_mhz"])
    if "tc_us" in r:
        L.append("Tc %.2f us (idle %.2f + rampEnd %.2f); %d chirps/frame (%d idx x %d loops); period %.4f ms" % (
            r["tc_us"], r["idle_us"], r["ramp_end_us"], r["nchirps"], r["chirp_indices"], r["loops"],
            r["frame_period_ms"]))
        L.append("Tb %.2f us; boundary step Tc+Tb %.2f us; dead time %.4f %%" % (
            r["tb_us"], r["boundary_step_us"], r["dead_time_pct"]))
        L.append("LVDS: %d B/chirp, %.2f MB/s, %.2f %% of %.0f B capacity per Tc" % (
            r["bchirp_bytes"], r["mbytes_per_s"], r["lvds_pct"], r["lvds_capacity_bytes"]))
        if "spacing_in_frame_m" in r:
            L.append("along-track spacing at %.3f m/s: in-frame %.3f mm, boundary %.3f mm%s" % (
                r["speed_mps"], r["spacing_in_frame_m"] * 1e3, r["spacing_boundary_m"] * 1e3,
                "" if "dmax_m" not in r else " (d_max %.3f mm)" % (r["dmax_m"] * 1e3)))
    if "hpf" in r:
        h = r["hpf"]
        L.append("HPF corner ranges (m): " + ", ".join("%s kHz %.1f" % (k, v) for k, v in h["corner_range_m"].items()))
        L.append("HPF attenuation dB at 1/2/5/10/20/50/100 m: " + " / ".join(
            "%.0f" % v for v in h["attenuation_db"].values()))
    for w in rep.warnings:
        L.append("WARN  " + w)
    for e in rep.errors:
        L.append("ERROR " + e)
    L.append("%s: %d violation(s), %d warning(s)" % ("FAIL" if rep.errors else "PASS", len(rep.errors),
                                                     len(rep.warnings)))
    return "\n".join(L)


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cfg", help="cfg file to check")
    ap.add_argument("--max-range", type=float, help="maximum range of interest, m (checks the beat frequency)")
    ap.add_argument("--speed", type=float, help="platform speed v, m/s (prints along-track spacing)")
    ap.add_argument("--dmax", type=float, help="along-track spacing limit d_max, m; needs --speed")
    ap.add_argument("--tb-min", type=float, default=300.0, help="minimum inter-frame blank, us (default 300)")
    ap.add_argument("--if-margin", type=float, default=0.8, help="usable IF as a fraction of fs (default 0.8)")
    ap.add_argument("--lvds-margin", type=float, default=10.0,
                    help="required LVDS capacity / bytes-per-chirp ratio (default 10; 1 = only forbid overflow)")
    ap.add_argument("--json", action="store_true", help="print machine-readable JSON instead of text")
    return ap


def main(argv=None):
    opt = build_parser().parse_args(argv)
    if opt.dmax is not None and opt.speed is None:
        print("error: --dmax needs --speed", file=sys.stderr)
        return 2
    try:
        with open(opt.cfg) as fh:
            text = fh.read()
    except OSError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    rep = analyze(text, opt)
    if opt.json:
        print(json.dumps({"cfg": opt.cfg, "ok": not rep.errors, "errors": rep.errors, "warnings": rep.warnings,
                          "values": rep.values}, indent=2, default=str))
    else:
        print(render(rep, opt.cfg))
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
