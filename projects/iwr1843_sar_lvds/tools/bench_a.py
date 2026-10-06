"""Set A bench commands (firmware-10 Step 2.2 and 2.6; NO corner reflector needed). Registered by bench_run.py:

    ./bench fmt4    2.2 restart with dataFmt 4 (CQ monitors on, then off), then back to dataFmt 2 clean
    ./bench fmt1    2.6.7 dataFmt 1 regression, >= 30 s, then back to dataFmt 2
    ./bench bsize   2.6.3 packet size B: header on (13328), header off (13264), and R*Ns = 2 (mod 4) (H 56, B 13328)
    ./bench bytes   2.6.2 raw byte order + 2.6.4 other-slot regime on a short capture
    ./bench late    2.6.9 late-armed capture rejected; offline first-packet cut rejected; offline dropped datagram kept
    ./bench irq     2.6.1 sarStats after sensorStop: chirpStartIsr == chirpAvail, nothing moves afterwards

Each prints PASS/FAIL lines and ends with RESULT. Captures and cfgs go to /tmp/bench_run/. HARDWARE TOOLS (except
`bytes --capture FILE` and `late --capture FILE`, which still arm a late capture on hardware). Python stdlib + numpy.
"""
import json
import os
import re
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_run as R  # noqa: E402

bc = R.bc
common = R.common
sar_parse = R.sar_parse
WORKDIR = R.WORKDIR

STAT_KEYS = ("chirps", "frames", "chirpStartIsr", "chirpAvail", "saturatedChirps", "lateIsr", "missedChirpIsr",
             "frameResync", "availResync", "cbuffErrIrq", "cbuffChirpErr", "cbuffFrameStartErr", "lvdsFramesDone")
HSI_DEV = bytes.fromhex("DC0ADA0CDC0ADA0C")


def parse_stats(text):
    """Every counter of the `sarStats` text (mmw_sar_meta.c MmwDemo_sarMetaPrintStats) as ints; {} if absent."""
    d = {}
    m = re.search(r"\brun (\d+) \(sensor state (\d+)\), dataFmt (\d+), satMon (\d+)", text or "")
    if m:
        d.update(runIdx=int(m.group(1)), state=int(m.group(2)), dataFmt=int(m.group(3)), satMon=int(m.group(4)))
    for key in STAT_KEYS:
        m = re.search(r"\b%s (-?\d+)" % key, text or "")
        if m:
            d[key] = int(m.group(1))
    return d


def read_stats(dev):
    port = common.CliPort(dev)
    try:
        return port.command("sarStats", timeout=3.0)
    finally:
        port.close()


# --- cfg variants ---------------------------------------------------------------------------------------------------
def set_lvds(lines, hdr, fmt):
    return R.with_command(lines, "lvdsStreamCfg", "-1 %d %d 0" % (hdr, fmt))


def insert_after(lines, cmd, new):
    out, hit = [], 0
    for l in lines:
        out.append(l)
        if l.split()[:1] == [cmd]:
            out.append(new)
            hit += 1
    if hit != 1:
        raise ValueError("expected exactly one %s line, found %d" % (cmd, hit))
    return out


def cq_on(lines, sigimg="0 111 4"):
    """analogMonitor 1 1 + a CQSigImgMonitor line (the saturation monitor stays on)."""
    return insert_after(R.with_command(lines, "analogMonitor", "1 1"), "CQRxSatMonitor", "CQSigImgMonitor " + sigimg)


def cq_off(lines):
    """Signal/image monitor off, saturation monitor (CQ2) kept on. NOT 'analogMonitor 0 0': dataFmt 4 (CBUFF_DataFmt_CP_ADC_CQ)
    needs >= 1 CQ block, and CBUFF_createSession returns CBUFF_EINVAL with none (cbuff.c, 'Sanity Check: Do we have at least
    1 CQ'), which the firmware reports as 'ADCBUF/LVDS HW session setup failed' (firmware-10 Log, 2026-10-06)."""
    return R.with_command(lines, "analogMonitor", "1 0")


def with_ns(lines, ns):
    """profileCfg with numAdcSamples replaced (token 10)."""
    out, hit = [], 0
    for l in lines:
        t = l.split()
        if t[:1] == ["profileCfg"] and len(t) == 15:
            t[10] = str(ns)
            l, hit = " ".join(t), hit + 1
        out.append(l)
    if hit != 1:
        raise ValueError("expected exactly one 15-token profileCfg line, found %d" % hit)
    return out


def with_frame_period(lines, add_us):
    """frameCfg with framePeriodicity (ms) increased by add_us microseconds (= Tb + add_us)."""
    out, hit = [], 0
    for l in lines:
        t = l.split()
        if t[:1] == ["frameCfg"] and len(t) == 8:
            t[5] = ("%.4f" % (float(t[5]) + add_us / 1000.0)).rstrip("0").rstrip(".")
            l, hit = " ".join(t), hit + 1
        out.append(l)
    if hit != 1:
        raise ValueError("expected exactly one 8-token frameCfg line, found %d" % hit)
    return out


def geom_cfg(lines):
    """cfg_params of `lines` read as dataFmt 2 header on (geometry only: Ns, R, Tc, Tb, Nc), for dataFmt 1/4 cfgs."""
    return common.cfg_params("\n".join(set_lvds(lines, 1, 2) + ["sensorStart"]) + "\n")


def cq_extra_bytes(sigimg, sat_args):
    """Bytes per chirp the firmware adds for the CQ blocks (mss_main.c:571-587, 16 B alignment)."""
    up = lambda n: (n + 15) // 16 * 16  # noqa: E731
    sig_slices = int(sigimg.split()[1])
    sat_slices = int(sat_args.split()[4])
    return up((sig_slices + 1) * 2) + up((sat_slices + 1) * 1)


# --- running ---------------------------------------------------------------------------------------------------------
def configure_show(dev, lines, out=print):
    """Send cfg lines (no sensorStart). On failure print the last lines of the dialogue. Returns True if every line
    got Done."""
    log = []
    port = common.CliPort(dev)
    try:
        ok = bc.send_cfg(port, lines, log.append)
    finally:
        port.close()
    if not ok:
        for l in log[-8:]:
            out("    " + l)
    return ok


def capture_only(dev, cap, cfg_path, duration, extra=(), out=print):
    """dca_capture.py without parsing (the capture is not dataFmt 2). Returns the sidecar dict or None."""
    cmd = [sys.executable, os.path.join(HERE, "dca_capture.py"), cap, "--cli-port", dev, "--duration", str(duration),
           *extra]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        out(p.stdout.strip()[-600:])
        return None
    with open(cap + ".sarstats.json") as fh:
        return json.load(fh)


def raw_info(cap, side):
    """What can be said about a capture of any dataFmt without a record parser."""
    datagrams = common.read_capture(cap)
    stream, mask, info = sar_parse.reassemble(datagrams)
    return dict(datagrams=len(datagrams), gaps=R.seq_gaps(datagrams), end=info["end"], max_payload=info["max_payload"],
                overlaps=len(info["overlaps"]), restarts=info["restarts"], stream=stream, mask=mask,
                stats=parse_stats(side.get("raw", "")), avail=side.get("chirpAvail"),
                frame_end=bool(side.get("frameEndTimeout")))


def packet_size(end, avail, max_payload):
    """(B, tail) with B = bytes per chirp packet = ceil(end / chirpAvail), tail = avail*B - end (a lost final datagram
    leaves it positive but <= one datagram). B is only unique when chirpAvail > one datagram's payload."""
    if not avail or avail <= max_payload:
        return None, None
    b = -(-end // avail)
    return b, avail * b - end


def hsi_at_starts(info, B):
    """(packets with the HSI id after the unscramble at k*B, packets whose first 8 bytes were received)."""
    ok = tot = 0
    for k in range(info["end"] // B):
        o = k * B
        if info["mask"][o:o + 8].count(1) == 8:
            tot += 1
            ok += info["stream"][o:o + 8] == HSI_DEV
    return ok, tot


def frames_ok(done, avail, nc):
    """LVDS frame count vs frames sent: the run stops mid-frame, so floor or ceil of chirps / Nc."""
    return done is not None and avail is not None and avail // nc <= done <= -(-avail // nc)


def counter_checks(st, avail, nc):
    return [
        ("sarStats counters ran and agree: chirps %s = chirpStartIsr %s = chirpAvail %s" % (
            st.get("chirps"), st.get("chirpStartIsr"), st.get("chirpAvail")),
         bool(avail) and st.get("chirps") == st.get("chirpStartIsr") == st.get("chirpAvail") == avail),
        ("lateIsr %s, missedChirpIsr %s, frameResync %s, availResync %s all 0" % (
            st.get("lateIsr"), st.get("missedChirpIsr"), st.get("frameResync"), st.get("availResync")),
         all(st.get(k) == 0 for k in ("lateIsr", "missedChirpIsr", "frameResync", "availResync"))),
        ("LVDS frames done %s = frames sent (%s chirps / %d: %s or %s)" % (
            st.get("lvdsFramesDone"), avail, nc, (avail or 0) // nc, -(-(avail or 0) // nc)),
         frames_ok(st.get("lvdsFramesDone"), avail, nc)),
    ]


def judge_raw(info, nc, label, expect_b=None):
    """Checks common to the dataFmt 1 / 4 captures. Returns (checks, B)."""
    avail = info["avail"]
    B, tail = packet_size(info["end"], avail, info["max_payload"])
    ok_size = B is not None and 0 <= tail <= info["max_payload"]
    checks = [
        ("%s: 0 UDP sequence gaps (%d datagrams)" % (label, info["datagrams"]), info["gaps"] == 0),
        ("%s: byte counts consistent (overlaps %d, restarts %d)" % (label, info["overlaps"], info["restarts"]),
         info["overlaps"] == 0 and info["restarts"] == 0),
        ("%s: bytes %d = chirpAvail %s x B %s (tail short %s B, <= 1 datagram)" % (label, info["end"], avail, B, tail),
         ok_size),
        ("%s: no 'no BSS frame-end event' warning" % label, not info["frame_end"]),
    ]
    if B is not None:
        h_ok, h_tot = hsi_at_starts(info, B)
        checks.append(("%s: HSI id at every packet start (stride B = %d): %d / %d" % (label, B, h_ok, h_tot),
                       h_tot > 0 and h_ok == h_tot))
    checks += counter_checks(info["stats"], avail, nc)
    return checks, B


# --- fmt4 -------------------------------------------------------------------------------------------------------------
def cmd_fmt4(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    base = bc.cfg_lines(a.cfg, drop_last=True)
    g = geom_cfg(base)
    nc, nrx, ns = g["nchirps"], g["nrx"], g["ns"]
    sat_args = [l for l in base if l.startswith("CQRxSatMonitor")][0]
    cq_lines = cq_on(set_lvds(base, 1, 4), a.sigimg)
    if a.tb_add_us:
        cq_lines = with_frame_period(cq_lines, a.tb_add_us)
    cases = [("fmt4 CQ on", cq_lines), ("fmt4 CQ off", cq_off(set_lvds(base, 1, 4)))]
    checks, sizes = [], {}
    for i, (name, lines) in enumerate(cases):
        out("=== %s: dataFmt 4, header on, %s s ===" % (name, a.duration))
        cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "fmt4_%d.cfg" % i))
        if not configure_show(dev, lines, out):
            checks.append(("%s: cfg accepted (every line Done)" % name, False))
            continue
        cap = os.path.join(WORKDIR, "fmt4_%d.cap" % i)
        side = capture_only(dev, cap, cfgp, a.duration, out=out)
        if side is None:
            checks.append(("%s: sensorStart + capture ran (see the error above)" % name, False))
            continue
        info = raw_info(cap, side)
        c, B = judge_raw(info, nc, name)
        checks += c
        sizes[name] = B
        out("%s: %d datagrams, %d B, chirpAvail %s -> %s B per chirp packet (ADC alone %d B, overhead %s B)" % (
            name, info["datagrams"], info["end"], info["avail"], B, 4 * nrx * ns, (B - 4 * nrx * ns) if B else "?"))
    if len(sizes) == 2 and all(sizes.values()):
        extra = cq_extra_bytes(a.sigimg, sat_args)
        out("INFO: CQ on - off = %d B per chirp; firmware sizes the CQ blocks at %d B (sigImg + satMon, 16 B aligned, "
            "mss_main.c:571-587). Not gated." % (sizes["fmt4 CQ on"] - sizes["fmt4 CQ off"], extra))
    out("=== back to dataFmt 2 (example cfg), %s s ===" % a.duration)
    lines = base
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "fmt4_back.cfg"))
    if configure_show(dev, lines, out):
        _, e = R.run_capture(dev, os.path.join(WORKDIR, "fmt4_back.cap"), cfgp, a.duration)
        R.block("dataFmt 2 again", e, out)
        checks.append(("dataFmt 2 capture after dataFmt 4: accepted by checks 1-4, 0 gaps, 0 missing", R.capture_ok(e)))
        checks.append(("dataFmt 2 after dataFmt 4: no 'no BSS frame-end event' warning",
                       bool(e) and not e["side"].get("frameEndTimeout")))
    else:
        checks.append(("dataFmt 2 cfg accepted again", False))
    out("NOT verified: dataFmt 4's CP / CQ block contents (sar_parse has no dataFmt 4 path; only size, periodic HSI id, "
        "counters and gaps are checked).")
    return bc.summarize(checks, out)


# --- fmt1 -------------------------------------------------------------------------------------------------------------
def decode_two_lane_iq_pairs(raw, q_first=False):
    """ADCCubeConverter::file_order, layout two_lane_iq_pairs (CPSL_TI_Radar_cpp/src/DCA1000/ADCCubeConverter.cpp:
    75-93) on raw DCA1000 bytes: words [A0 A1 B0 B1] -> samples (re, im) = (A0, B0), (A1, B1) (i_first) or
    (B0, A0), (B1, A1) (q_first). Returns an int16 array of re, im pairs."""
    import numpy as np
    w = np.frombuffer(bytes(raw[:len(raw) // 8 * 8]), dtype="<i2").reshape(-1, 4)
    a0, a1, b0, b1 = w[:, 0], w[:, 1], w[:, 2], w[:, 3]
    out = np.empty((w.shape[0], 4), dtype=np.int16)
    if q_first:
        out[:, 0], out[:, 1], out[:, 2], out[:, 3] = b0, a0, b1, a1
    else:
        out[:, 0], out[:, 1], out[:, 2], out[:, 3] = a0, b0, a1, b1
    return out.reshape(-1)


def fmt1_decode(info, B, nrx, ns, nsample_chirps=200, swap=0):
    """Decode the ADC block (after the 64 B HSI header) of the first chirps with the C++ converter's pairing, on the
    raw (re-scrambled) bytes. Returns (n decoded, n all-zero chirps, peak, mean |dI|/rms smoothness, agrees with the
    unscramble path)."""
    import numpy as np
    n = min(nsample_chirps, info["end"] // B)
    zero = 0
    peak = 0
    sm = []
    agree = True
    for k in range(n):
        o = k * B + 64
        blk = bytearray(info["stream"][o:o + 4 * nrx * ns])
        raw = bytearray(blk)
        sar_parse.unscramble(raw)                                    # back to the DCA1000 order
        v = decode_two_lane_iq_pairs(raw, q_first=(swap == 1))
        dev = np.frombuffer(bytes(blk), dtype="<i2")                  # device order after the unscramble: pairs
        i_, q_ = (dev[1::2], dev[0::2]) if swap == 1 else (dev[0::2], dev[1::2])
        if not (np.array_equal(v[0::2], i_) and np.array_equal(v[1::2], q_)):
            agree = False
        if not v.any():
            zero += 1
        peak = max(peak, int(np.abs(v.astype(np.int32)).max()))
        x = i_.astype(np.float64)
        rms = float(np.sqrt((x ** 2).mean())) or 1.0
        sm.append(float(np.abs(np.diff(x)).mean()) / rms)
    return n, zero, peak, (sum(sm) / len(sm) if sm else None), agree


def cmd_fmt1(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    base = bc.cfg_lines(a.cfg, drop_last=True)
    g = geom_cfg(base)
    nc, nrx, ns = g["nchirps"], g["nrx"], g["ns"]
    lines = set_lvds(base, 1, 1)
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "fmt1.cfg"))
    out("=== dataFmt 1 (stock framing), header on, %g s ===" % a.duration)
    checks = []
    if not configure_show(dev, lines, out):
        checks.append(("dataFmt 1 cfg accepted (every line Done)", False))
    else:
        cap = os.path.join(WORKDIR, "fmt1.cap")
        side = capture_only(dev, cap, cfgp, a.duration, out=out)
        if side is None:
            checks.append(("dataFmt 1 sensorStart + capture ran (see the error above)", False))
        else:
            info = raw_info(cap, side)
            c, B = judge_raw(info, nc, "dataFmt 1")
            checks += c
            adc = 4 * nrx * ns
            out("dataFmt 1: %d B per chirp packet (ADC %d B + HSI header 64 B = %d; the stock tools round up to 256 B = %d)" % (
                B or 0, adc, adc + 64, (adc + 52 + 255) // 256 * 256))
            checks.append(("dataFmt 1 stock frame size: B %s is one of the two stock sizes (%d = 16 B aligned, %d = 256 B "
                           "aligned)" % (B, adc + 64, (adc + 52 + 255) // 256 * 256),
                           B in (adc + 64, (adc + 52 + 255) // 256 * 256)))
            if B:
                n, zero, peak, smooth, agree = fmt1_decode(info, B, nrx, ns, swap=g["swap"])
                out("decode (ADCCubeConverter two_lane_iq_pairs pairing, %d chirps): all-zero chirps %d, peak |x| %d, "
                    "mean |dI|/rms %s (white noise reads ~1.1; INFO only)" % (
                        n, zero, peak, "%.2f" % smooth if smooth is not None else "n/a"))
                checks.append(("dataFmt 1 offline decode: C++ pairing = unscramble path, %d chirps, none all-zero" % n,
                               agree and n > 0 and zero == 0))
    out("=== back to dataFmt 2 (example cfg), 10 s ===")
    cfg2 = R.write_cfg(base + ["sensorStart"], os.path.join(WORKDIR, "fmt1_back.cfg"))
    if configure_show(dev, base, out):
        _, e = R.run_capture(dev, os.path.join(WORKDIR, "fmt1_back.cap"), cfg2, 10)
        R.block("dataFmt 2 again", e, out)
        checks.append(("restart back to dataFmt 2: capture accepted, 0 gaps, 0 missing", R.capture_ok(e)))
    else:
        checks.append(("dataFmt 2 cfg accepted again", False))
    out("NOT verified: decoding with the real C++ ADCCubeConverter (its pairing is reproduced in Python and compared with "
        "the unscramble path; signal sanity needs a target).")
    return bc.summarize(checks, out)


# --- bsize ------------------------------------------------------------------------------------------------------------
def cmd_bsize(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    base = bc.cfg_lines(a.cfg, drop_last=True)
    g0 = common.cfg_params("\n".join(base + ["sensorStart"]) + "\n")
    r, ns = g0["nrx"], g0["ns"]
    cases = [("header on, Ns %d" % ns, set_lvds(base, 1, 2), 64, 64 + 4 * r * ns + 64),
             ("header off, Ns %d" % ns, set_lvds(base, 0, 2), 0, 4 * r * ns + 64)]
    ns2 = a.ns2
    odd = with_ns(base, ns2)
    cases.append(("header on, Ns %d (R*Ns = %d, 2 mod 4)" % (ns2, r * ns2), odd, 56, 56 + 4 * r * ns2 + 64))
    checks = []
    for i, (name, lines, H, Bexp) in enumerate(cases):
        out("=== %s: expect H %d, B %d, %g s ===" % (name, H, Bexp, a.duration))
        text = "\n".join(lines + ["sensorStart"]) + "\n"
        try:
            gp = common.cfg_params(text)
        except ValueError as exc:
            checks.append(("%s: cfg usable (sar_cfg_check): %s" % (name, exc), False))
            continue
        errs = gp["cfg_errors"]
        checks.append(("%s: sar_cfg_check finds no error%s" % (name, (" (" + errs[0] + ")") if errs else ""), not errs))
        checks.append(("%s: parser H %d, B %d = H + 4*R*Ns + 64 = %d" % (name, gp["H"], gp["B"], Bexp),
                       gp["H"] == H and gp["B"] == Bexp))
        cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "bsize%d.cfg" % i))
        if not configure_show(dev, lines, out):
            checks.append(("%s: cfg accepted by the board" % name, False))
            continue
        _, e = R.run_capture(dev, os.path.join(WORKDIR, "bsize%d.cap" % i), cfgp, a.duration)
        R.block(name, e, out)
        if e is None:
            checks.append(("%s: capture ran" % name, False))
            continue
        est, tail = packet_size(e["end"], e["avail"], 1456)
        checks.append(("%s: capture accepted (records validate at k*B: B proven), 0 gaps, 0 missing" % name,
                       R.capture_ok(e)))
        checks.append(("%s: bytes / chirpAvail -> B %s = %d (tail short %s B)" % (name, est, Bexp, tail),
                       est == Bexp and tail is not None and 0 <= tail <= 1472))
    out("(the board now holds the last cfg; every ./bench command re-sends its own cfg first)")
    return bc.summarize(checks, out)


# --- bytes ------------------------------------------------------------------------------------------------------------
def judge_bytes(r, min_packets=1000):
    """2.6.2 and 2.6.4 from a bench_stream.analyze_stream result. Returns (checks, regime text)."""
    c = r["counts"]
    n_in = sum(r["regime_in"].values())
    plus, minus = r["regime_in"].get(1, 0), r["regime_in"].get(-1, 0)
    regime = None
    if n_in:
        if plus / n_in >= 0.99:
            regime = "k+1"
        elif minus / n_in >= 0.99:
            regime = "k-1"
    last = r["regime_last"]
    checks = [
        ("HSI id raw (DC 0A DC 0A DA 0C DA 0C) at packet start: %d / %d" % (c["hsi_raw_ok"], c["hsi_raw_eval"]),
         c["hsi_raw_eval"] > 0 and c["hsi_raw_ok"] == c["hsi_raw_eval"]),
        ("HSI id after unscramble (DC 0A DA 0C DC 0A DA 0C): %d / %d" % (c["hsi_dev_ok"], c["hsi_raw_eval"]),
         c["hsi_raw_eval"] > 0 and c["hsi_dev_ok"] == c["hsi_raw_eval"]),
        ("record magic raw \"SA\" 01 00 \"RM\" at M + 32*(k mod 2) (own slot): %d / %d" % (
            c["own_raw_ok"], c["own_raw_eval"]), c["own_raw_eval"] > 0 and c["own_raw_ok"] == c["own_raw_eval"]),
        ("record magic raw at the other slot, k > 0: %d / %d" % (c["other_raw_ok"], c["other_raw_eval"]),
         c["other_raw_eval"] > 0 and c["other_raw_ok"] == c["other_raw_eval"]),
        ("unscramble gives SARM at the own slot: %d / %d" % (c["own_dev_ok"], c["own_dev_eval"]),
         c["own_dev_eval"] > 0 and c["own_dev_ok"] == c["own_dev_eval"]),
        ("every packet's slot k mod 2 validates (magic SARM, version 1, globalChirpIdx = k, run's runIdx): %d / %d" % (
            c["n_valid"], c["n_eval"]), c["n_eval"] > 0 and c["n_valid"] == c["n_eval"]),
        ("other-slot regime over %d in-frame packets (>= %d): k+1 %d (%.2f%%), k-1 %d (%.2f%%) -> %s" % (
            n_in, min_packets, plus, 100.0 * plus / max(n_in, 1), minus, 100.0 * minus / max(n_in, 1),
            regime or "NEITHER >= 99%"), n_in >= min_packets and regime is not None),
    ]
    text = "other slot at the last packet of each frame: %s (expected always k-1)" % (
        {("k%+d" % d): v for d, v in sorted(last.items())} or "none")
    return checks, regime, text


def cmd_bytes(dev, a, out=print):
    import bench_stream as bs
    os.makedirs(WORKDIR, exist_ok=True)
    if a.capture:
        cap, cfgp = a.capture, a.cfg
    else:
        lines = bc.cfg_lines(a.cfg, drop_last=True)
        cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "bytes.cfg"))
        if not configure_show(dev, lines, out):
            out("RESULT: FAIL (cfg not accepted)")
            return False
        cap = os.path.join(WORKDIR, "bytes.cap")
        if capture_only(dev, cap, cfgp, a.duration, out=out) is None:
            out("RESULT: FAIL (capture tool failed)")
            return False
    with open(cfgp) as fh:
        cfg = common.cfg_params(fh.read())
    with open(cap + ".sarstats.json") as fh:
        side = json.load(fh)
    r = bs.analyze_stream(cap, cfg, side)
    out("capture %s: %d packets, checks 1-4 %s" % (os.path.basename(cap), r["n_chirps"],
                                                    "ACCEPTED" if r["accepted"] else "REJECTED %s" % r["failing"]))
    checks, regime, text = judge_bytes(r)
    out(text)
    if regime:
        out("REGIME: %s (k+1: the chirp-start interrupt sits near the idle start; k-1: later, near the ramp start)" % regime)
    return bc.summarize(checks, out)


# --- late -------------------------------------------------------------------------------------------------------------
def cut_first_packet(datagrams, B):
    """The silent one-packet-late case: drop the first B bytes and renumber the byte counts from 0 (B % 8 == 0 keeps
    the u0 u2 u1 u3 groups aligned)."""
    out = []
    for d in datagrams:
        seq, count, pl = common.split_header(d)
        if count + len(pl) <= B:
            continue
        if count < B:
            pl, count = pl[B - count:], B
        out.append(struct.pack("<I", seq) + (count - B).to_bytes(6, "little") + pl)
    return out


def keep_first_bytes(datagrams, nbytes):
    out = []
    for d in datagrams:
        seq, count, pl = common.split_header(d)
        if count >= nbytes:
            continue
        out.append(struct.pack("<I", seq) + count.to_bytes(6, "little") + pl[:nbytes - count])
    return out


def drop_one(datagrams, idx):
    return datagrams[:idx] + datagrams[idx + 1:]


def affected(lo, hi, cfg, n):
    """(chirps whose ADC bytes, chirps whose own record slot) intersect the lost byte range [lo, hi)."""
    B, M, H, nrx, ns = cfg["B"], cfg["M"], cfg["H"], cfg["nrx"], cfg["ns"]
    adc, rec = set(), set()
    for k in range(max(0, lo // B - 1), min(n, hi // B + 2)):
        a0 = k * B + H
        if a0 < hi and a0 + 4 * nrx * ns > lo:
            adc.add(k)
        r0 = k * B + M + 32 * (k % 2)
        if r0 < hi and r0 + 32 > lo:
            rec.add(k)
    return adc, rec


def judge_dropped(res_good, res_drop, dgs_drop, lost, cfg):
    """Dropping one datagram: accepted, gap reported, only the overlapped chirps marked, nothing shifted."""
    n = res_good["n_chirps"]
    adc_aff, rec_aff = affected(lost[0], lost[1], cfg, n)
    base_bad_adc = {k for k in range(n) if not res_good["adc_ok"][k]}
    base_bad_rec = {k for k in range(n) if not res_good["valid"][k]}
    now_bad_adc = {k for k in range(n) if not res_drop["adc_ok"][k]}
    now_bad_rec = {k for k in range(n) if not res_drop["valid"][k]}
    same_rec = all(res_drop["recs"][k] == res_good["recs"][k] for k in range(n)
                   if res_drop["valid"][k] and res_good["valid"][k])
    B, H, nrx, ns = cfg["B"], cfg["H"], cfg["nrx"], cfg["ns"]
    same_adc = all(res_drop["stream"][k * B + H:k * B + H + 4 * nrx * ns] == res_good["stream"][k * B + H:k * B + H + 4 * nrx * ns]
                   for k in range(0, n, max(1, n // 400)) if k not in adc_aff and res_good["adc_ok"][k])
    return [
        ("dropped datagram (bytes %d-%d, seq gap %d): capture still ACCEPTED (not a requirement violation)" % (
            lost[0], lost[1], R.seq_gaps(dgs_drop)), res_drop["accepted"] and R.seq_gaps(dgs_drop) == 1),
        ("ADC marked incomplete for exactly the overlapped chirps %s (marked now: %s)" % (
            sorted(adc_aff), sorted(now_bad_adc - base_bad_adc)), now_bad_adc - base_bad_adc == adc_aff - base_bad_adc),
        ("record invalid for exactly the chirps whose slot was hit %s (invalid now: %s)" % (
            sorted(rec_aff), sorted(now_bad_rec - base_bad_rec)), now_bad_rec - base_bad_rec == rec_aff - base_bad_rec),
        ("nothing shifted: every other chirp's record and (sampled) ADC bytes identical to the good capture",
         same_rec and same_adc),
        ("no packet wholly absent (a gap inside one packet leaves its neighbours' alignment alone)",
         res_drop["absent"] == res_good["absent"]),
    ]


def late_armed_capture(dev, cap, duration, delay_s, timer_s=30, out=print):
    """sensorStart FIRST, then arm the DCA1000 (the forbidden order), record `duration`, sensorStop, sarStats."""
    import dca_capture
    d = dca_capture.build_parser().parse_args(["x"])
    cli = common.CliPort(dev)
    dca = common.Dca1000(d.fpga_ip, d.host_ip, d.cmd_port)
    sock = dca_capture.open_data_socket(d.host_ip, d.data_port, d.rcvbuf_mb << 20)
    side = {"chirpAvail": None, "runIdx": None, "raw": "", "frameEndTimeout": False, "source": "cli", "late_armed_s": delay_s}
    started = False
    armed = False
    with open(cap, "wb") as fh:
        common.write_capture_header(fh)
        rx = dca_capture.Receiver(sock, fh)
        try:
            rx.start()
            dca_capture.configure_dca(dca, 2, timer_s, log=lambda *_: None)
            resp = cli.command("sensorStart", timeout=5.0)
            if "Done" not in resp:
                raise RuntimeError("sensorStart failed: %r" % resp.strip()[-200:])
            started = True
            time.sleep(delay_s)
            dca.send("RECORD_START")                                    # armed mid-run: the violation under test
            armed = True
            time.sleep(duration)
            stop = cli.command("sensorStop", timeout=10.0)
            started = False
            side["frameEndTimeout"] = common.FRAME_END_MSG in stop
            time.sleep(0.5)
            text = cli.command("sarStats", timeout=3.0)
            side["raw"] = text
            st = common.parse_sarstats(text)
            if st is None:
                raise RuntimeError("could not read sarStats: %r" % text.strip()[-200:])
            side.update(chirpAvail=st["chirpAvail"], runIdx=st["runIdx"])
        finally:
            if started:
                try:
                    cli.command("sensorStop", timeout=10.0)
                except Exception:                                       # noqa: BLE001
                    pass
            if armed:
                try:
                    dca.send("RECORD_STOP")
                except RuntimeError:
                    pass
            rx.stop()
            sock.close()
            dca.close()
            cli.close()
    side["datagrams"] = rx.count
    with open(cap + ".sarstats.json", "w") as fh:
        json.dump(side, fh, indent=2)
    return side


def cmd_late(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    lines = bc.cfg_lines(a.cfg, drop_last=True)
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "late.cfg"))
    with open(a.cfg) as fh:
        cfg = common.cfg_params(fh.read())
    checks = []
    # a good capture to copy
    if a.capture:
        good = a.capture
    else:
        out("=== good %g s capture (to copy offline) ===" % a.duration)
        if not configure_show(dev, lines, out):
            out("RESULT: FAIL (cfg not accepted)")
            return False
        good = os.path.join(WORKDIR, "late_good.cap")
        if capture_only(dev, good, cfgp, a.duration, out=out) is None:
            out("RESULT: FAIL (capture tool failed)")
            return False
    with open(good + ".sarstats.json") as fh:
        side = json.load(fh)
    dgs = common.read_capture(good)
    res_good = sar_parse.analyze(dgs, cfg, side)
    checks.append(("good capture is ACCEPTED (checks 1-4), %d chirps" % res_good["n_chirps"], res_good["accepted"]))
    B = cfg["B"]
    # (b) first packet cut
    cut = cut_first_packet(dgs, B)
    n_cut = (res_good["info"]["end"] - B) // B
    r_b1 = sar_parse.analyze(cut, cfg, side)
    out("(b1) first packet cut, original sarStats: REJECTED on checks %s" % r_b1["failing"] if not r_b1["accepted"]
        else "(b1) first packet cut, original sarStats: ACCEPTED (!)")
    checks.append(("(b1) first packet cut + renumbered: rejected", not r_b1["accepted"]))
    whole = keep_first_bytes(cut, n_cut * B)
    r_b2 = sar_parse.analyze(whole, cfg, {"chirpAvail": n_cut, "runIdx": side.get("runIdx")})
    c3 = r_b2["checks"][2]
    out("(b2) cut + trimmed to whole packets + matching chirpAvail %d (so check 4 cannot object): checks failing %s; "
        "check 3: %s - %s" % (n_cut, r_b2["failing"], c3.status, c3.detail))
    checks.append(("(b2) check 3 alone rejects the one-packet-late recording (check 3 FAIL, verdict REJECTED)",
                   (not r_b2["accepted"]) and c3.status == "FAIL"))
    # (c) one dropped datagram
    idx = len(dgs) // 2
    seq, count, pl = common.split_header(dgs[idx])
    lost = (count, count + len(pl))
    dropped = drop_one(dgs, idx)
    res_drop = sar_parse.analyze(dropped, cfg, side)
    checks += judge_dropped(res_good, res_drop, dropped, lost, cfg)
    # (a) late armed on hardware
    out("=== (a) late-armed capture: sensorStart, %g s later arm the DCA1000, record %g s ===" % (a.delay, a.late_duration))
    if configure_show(dev, lines, out):
        lcap = os.path.join(WORKDIR, "late_a.cap")
        try:
            lside = late_armed_capture(dev, lcap, a.late_duration, a.delay, out=out)
        except (RuntimeError, OSError) as exc:
            out("late capture failed: %s" % exc)
            checks.append(("(a) late-armed capture ran", False))
        else:
            ldgs = common.read_capture(lcap)
            res_a = sar_parse.analyze(ldgs, cfg, lside)
            out("(a) late-armed: %d datagrams, %d B recorded, chirpAvail %s; " % (
                len(ldgs), res_a["info"]["end"], lside["chirpAvail"]) +
                "; ".join("check %d %s" % (c.num, c.status) for c in res_a["checks"]))
            prefix = os.path.join(WORKDIR, "late_a_out")
            for suf in ("_adc.bin", "_meta.csv"):
                if os.path.exists(prefix + suf):
                    os.remove(prefix + suf)
            p = subprocess.run([sys.executable, os.path.join(HERE, "sar_parse.py"), lcap, "--cfg", cfgp, "--out", prefix],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            fails = {c.num for c in res_a["checks"] if c.status != "PASS"}
            checks.append(("(a) checks 2 and 4 fail (check 3: %s)" % res_a["checks"][2].status, {2, 4} <= fails))
            checks.append(("(a) sar_parse exits 1, names the capture requirement, writes no aligned output",
                           p.returncode == 1 and "Capture requirement" in p.stdout and "REJECTED" in p.stdout and
                           not os.path.exists(prefix + "_adc.bin") and not os.path.exists(prefix + "_meta.csv")))
    else:
        checks.append(("(a) cfg accepted for the late capture", False))
    return bc.summarize(checks, out)


# --- irq --------------------------------------------------------------------------------------------------------------
def judge_irq(reads):
    """reads = [(label, stats dict)]: the first is the reading after sensorStop."""
    first = reads[0][1]
    keys = ("chirps", "chirpStartIsr", "chirpAvail", "frames")
    checks = [
        ("after sensorStop: chirpStartIsr %s = chirpAvail %s = chirps %s" % (
            first.get("chirpStartIsr"), first.get("chirpAvail"), first.get("chirps")),
         first.get("chirpStartIsr") is not None and first.get("chirpStartIsr") == first.get("chirpAvail") == first.get("chirps")
         and bool(first.get("chirps"))),
        ("no counter moves across %d later reads (chirps, chirpStartIsr, chirpAvail, frames)" % (len(reads) - 1),
         all(all(s.get(k) == first.get(k) for k in keys) for _, s in reads[1:])),
        ("lateIsr, missedChirpIsr, frameResync, availResync all 0 in every reading",
         all(all(s.get(k) == 0 for k in ("lateIsr", "missedChirpIsr", "frameResync", "availResync")) for _, s in reads)),
    ]
    return checks


def cmd_irq(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    lines = bc.cfg_lines(a.cfg, drop_last=True)
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "irq.cfg"))
    if not configure_show(dev, lines, out):
        out("RESULT: FAIL (cfg not accepted)")
        return False
    side = capture_only(dev, os.path.join(WORKDIR, "irq.cap"), cfgp, a.duration, out=out)
    if side is None:
        out("RESULT: FAIL (capture tool failed)")
        return False
    reads = [("after sensorStop", parse_stats(side["raw"]))]
    for i in range(a.reads):
        time.sleep(a.gap)
        reads.append(("+%g s" % (a.gap * (i + 1)), parse_stats(read_stats(dev))))
    time.sleep(a.gap)
    if configure_show(dev, lines, out):                                   # the cfg for the next start is in; no sensorStart
        reads.append(("before next sensorStart (cfg re-sent)", parse_stats(read_stats(dev))))
    for label, s in reads:
        out("%-38s chirps %s chirpStartIsr %s chirpAvail %s frames %s late %s missed %s fResync %s aResync %s" % (
            label, s.get("chirps"), s.get("chirpStartIsr"), s.get("chirpAvail"), s.get("frames"), s.get("lateIsr"),
            s.get("missedChirpIsr"), s.get("frameResync"), s.get("availResync")))
    return bc.summarize(judge_irq(reads), out)


# --- registration ---------------------------------------------------------------------------------------------------
def register(sub):
    cfg = R.DEFAULT_CFG
    p = sub.add_parser("fmt4", help="2.2 dataFmt 4 with CQ monitors on, then off, then back to dataFmt 2")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=10)
    p.add_argument("--sigimg", default="0 111 4", help="CQSigImgMonitor args: profile numSlices samplesPerSlice (default '%(default)s')")
    p.add_argument("--tb-add-us", type=int, default=0, help="lengthen the blank in the CQ-on cfg (the signal/image monitor adds time; try 500 if CQ-on shows frame errors)")
    p = sub.add_parser("fmt1", help="2.6.7 dataFmt 1 regression (>= 30 s), then back to dataFmt 2")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=30)
    p = sub.add_parser("bsize", help="2.6.3 packet size B: header on, off, and R*Ns = 2 mod 4")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=10)
    p.add_argument("--ns2", type=int, default=3302, help="numAdcSamples for the R*Ns = 2 (mod 4) case (default 3302)")
    p = sub.add_parser("bytes", help="2.6.2 raw byte order and 2.6.4 other-slot regime")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=10)
    p.add_argument("--capture", help="analyse this existing capture (with its .sarstats.json) instead of taking one")
    p = sub.add_parser("late", help="2.6.9 late-armed capture, first-packet cut, dropped datagram")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=10, help="the good capture to copy (default 10 s)")
    p.add_argument("--capture", help="use this existing good capture (with its .sarstats.json) for the offline cases")
    p.add_argument("--delay", type=float, default=2.0, help="seconds after sensorStart before arming (default 2)")
    p.add_argument("--late-duration", type=float, default=5.0)
    p = sub.add_parser("irq", help="2.6.1 sarStats after sensorStop: nothing moves")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--duration", type=float, default=10)
    p.add_argument("--reads", type=int, default=2, help="extra reads after the one in the capture (default 2)")
    p.add_argument("--gap", type=float, default=4.0, help="seconds between reads (default 4)")


COMMANDS = {"fmt4": cmd_fmt4, "fmt1": cmd_fmt1, "bsize": cmd_bsize, "bytes": cmd_bytes, "late": cmd_late,
            "irq": cmd_irq}
