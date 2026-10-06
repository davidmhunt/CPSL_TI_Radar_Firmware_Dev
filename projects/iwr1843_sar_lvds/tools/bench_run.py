#!/usr/bin/env python3
"""One-command bench runs for firmware-10 Step 2.2 / 2.6.0 (run from firmware_dev/; `./bench` is a short wrapper):

    ./bench long       60 s capture: FPGA timer does not cut it off, bytes = chirpAvail x B, 0 sequence gaps
                       (= CONFIG_PACKET_DATA delay unit check)
    ./bench restart    >= 3 stop / re-cfg (changed rxGain, HPF) / start cycles in one boot, a capture + parse each
    ./bench chan       full cfg with a changed channelCfg + sensorStart: rejected at sensorStart, then a valid cfg + capture still works
    ./bench finite     numFrames 5 run ends by itself; sensorStop still works; chirpAvail = 5 x 255; LVDS frame count
    ./bench start0     `sensorStart 0` restart with no new cfg, capture + parse
    ./bench adc        peak |I|,|Q| of a capture (clipped on purpose) -> which --adc-bits (12: 2048, 16: 32768)

Each prints PASS/FAIL lines and a short block to paste back. HARDWARE TOOL: configures the radar over the CLI port
and records from the DCA1000 (via dca_capture.py, then sar_parse's analysis in-process). The board must be in run mode,
the DCA1000 on its network. A cfg is accepted once per power-up for the first configure; reconfigure after sensorStop +
flushCfg is what `restart` tests. Python stdlib + sar_common / sar_parse only.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_check as bc  # noqa: E402
import sar_common as common  # noqa: E402
import sar_parse  # noqa: E402

DEFAULT_CFG = bc.DEFAULT_CFG
WORKDIR = "/tmp/bench_run"
# profileCfg argument indices (profileCfg <id> startGHz idle adcStart rampEnd txPwr txPhase slope txStart Ns rate hpf1 hpf2 gain)
I_HPF1, I_HPF2, I_GAIN = 11, 12, 13
DEFAULT_POINTS = "36:0:0,24:0:1,30:1:0,30:0:0"      # rxGain_dB:hpf1:hpf2 per cycle


# --- cfg editing ----------------------------------------------------------------------------------------------------
def with_profile(lines, gain, hpf1, hpf2):
    """Copy of the cfg lines with profileCfg's HPF1/HPF2/rxGain replaced (everything else identical, so channelCfg,
    lowPower and adcCfg are re-sent exactly as they were)."""
    out, hit = [], 0
    for l in lines:
        t = l.split()
        if t and t[0] == "profileCfg" and len(t) == 15:
            t[1 + I_HPF1], t[1 + I_HPF2], t[1 + I_GAIN] = str(hpf1), str(hpf2), str(gain)
            l, hit = " ".join(t), hit + 1
        out.append(l)
    if hit != 1:
        raise ValueError("expected exactly one 15-token profileCfg line, found %d" % hit)
    return out


def with_command(lines, cmd, args):
    """Copy with the (single) `cmd` line replaced by `cmd args`."""
    out, hit = [], 0
    for l in lines:
        if l.split()[:1] == [cmd]:
            l, hit = (cmd + " " + args).strip(), hit + 1
        out.append(l)
    if hit != 1:
        raise ValueError("expected exactly one %s line, found %d" % (cmd, hit))
    return out


def write_cfg(lines, path):
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    return path


def parse_points(text):
    pts = []
    for item in text.split(","):
        g, h1, h2 = (int(x) for x in item.split(":"))
        pts.append((g, h1, h2))
    return pts


# --- one recording --------------------------------------------------------------------------------------------------
def seq_gaps(datagrams):
    """Missing UDP sequence numbers between the first and last datagram received (exact duplicates ignored)."""
    seqs = {common.split_header(d)[0] for d in datagrams if len(d) > common.UDP_HDR}
    return (max(seqs) - min(seqs) + 1 - len(seqs)) if seqs else 0


def evaluate(cap, cfg_path):
    """Parse + analyse a finished capture. Returns a flat dict of the numbers the bench blocks print."""
    with open(cfg_path) as fh:
        cfg = common.cfg_params(fh.read())
    datagrams = common.read_capture(cap)
    with open(cap + ".sarstats.json") as fh:
        side = json.load(fh)
    res = sar_parse.analyze(datagrams, cfg, side)
    n, nc = res["n_chirps"], cfg["nchirps"]
    d_in, d_bd = res["d_in"], res["d_bd"]
    nsat, known, _ = sar_parse.sat_summary(res)
    return dict(res=res, cfg=cfg, side=side, accepted=res["accepted"], failing=list(res["failing"]),
                avail=side.get("chirpAvail"), B=cfg["B"], end=res["info"]["end"], datagrams=len(datagrams),
                gaps=seq_gaps(datagrams), n=n, valid=sum(res["valid"]), absent=res["absent"],
                partial=sum(1 for ok in res["adc_ok"] if not ok), sat=nsat,
                dt_in=(sum(d_in) / len(d_in) * 1e6) if d_in else None,
                dt_bd=(sum(d_bd) / len(d_bd) * 1e6) if d_bd else None,
                frames=n / nc, frame_s=nc * cfg["tc_s"] + cfg["tb_s"])


def run_capture(dev, cap, cfg_path, duration, extra=()):
    """dca_capture.py (sends sensorStart, waits, sensorStop, sarStats) then evaluate(). Returns (rc, eval or None)."""
    cmd = [sys.executable, os.path.join(HERE, "dca_capture.py"), cap, "--cli-port", dev, "--duration", str(duration),
           *extra]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode != 0:
        print(p.stdout.strip()[-800:])
        return p.returncode, None
    return 0, evaluate(cap, cfg_path)


def capture_ok(e):
    """G1-style pass for one capture: accepted by checks 1-4, no sequence gap, no wholly missing packet."""
    return bool(e) and e["accepted"] and e["gaps"] == 0 and e["absent"] == 0


def block(name, e, out=print):
    """Short paste-back block for one capture."""
    if e is None:
        out("%s: capture tool failed (no result)" % name)
        return
    out("%s: %s  datagrams %d, seq gaps %d, bytes %d vs chirpAvail %s x B %d = %s" % (
        name, "ACCEPTED" if e["accepted"] else "REJECTED(" + ",".join("check%d" % n for n in e["failing"]) + ")",
        e["datagrams"], e["gaps"], e["end"], e["avail"], e["B"], (e["avail"] or 0) * e["B"]))
    out("    chirps %d (%.1f frames), records valid %d, wholly missing %d, ADC partly lost %d, saturated %d" % (
        e["n"], e["frames"], e["valid"], e["absent"], e["partial"], e["sat"]))
    out("    dt in-frame %s us, boundary %s us (cfg %.2f / %.2f)" % (
        "%.2f" % e["dt_in"] if e["dt_in"] is not None else "n/a",
        "%.2f" % e["dt_bd"] if e["dt_bd"] is not None else "n/a", e["cfg"]["tc_s"] * 1e6,
        (e["cfg"]["tc_s"] + e["cfg"]["tb_s"]) * 1e6))


def configure(dev, lines, out=print):
    """Send cfg lines (no sensorStart) on a fresh CLI handle, then release the port for dca_capture."""
    port = common.CliPort(dev)
    try:
        return bc.send_cfg(port, lines, out)
    finally:
        port.close()


# --- long -----------------------------------------------------------------------------------------------------------
def judge_long(e, timer_s):
    """List of (name, ok) for the 60 s capture."""
    recorded = e["frames"] * e["frame_s"]
    tail = e["datagrams"] and (e["avail"] or 0) * e["B"] - e["end"]
    return [
        ("capture accepted by sar_parse checks 1-4", e["accepted"]),
        ("recorded %.1f s of run > timer %d s (not cut off)" % (recorded, timer_s), recorded > timer_s + 2),
        ("bytes %d = chirpAvail x B %d (tail short by %d B; <= 1 datagram tolerated)" % (
            e["end"], (e["avail"] or 0) * e["B"], tail), 0 <= tail <= 1472),
        ("0 UDP sequence gaps (CONFIG_PACKET_DATA delay 100 raw: no loss at this rate)", e["gaps"] == 0),
        ("0 wholly missing packets", e["absent"] == 0),
    ]


def cmd_long(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    cfg = a.cfg
    if not configure(dev, bc.cfg_lines(cfg, drop_last=True), out):
        out("RESULT: FAIL (cfg not accepted; keep the output above)")
        return False
    rc, e = run_capture(dev, os.path.join(WORKDIR, "long.cap"), cfg, a.duration, ["--timer-s", str(a.timer_s)])
    if e is None:
        out("RESULT: FAIL (capture tool failed)")
        return False
    block("long %gs, --timer-s %d" % (a.duration, a.timer_s), e, out)
    return bc.summarize(judge_long(e, a.timer_s), out)


# --- restart --------------------------------------------------------------------------------------------------------
def cmd_restart(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    base = bc.cfg_lines(a.cfg, drop_last=True)
    results = []
    for i, (g, h1, h2) in enumerate(parse_points(a.points), 1):
        out("=== cycle %d: rxGain %d dB, hpf1 %d, hpf2 %d (sensorStop, flushCfg, cfg, start, %g s) ===" % (
            i, g, h1, h2, a.duration))
        cfg = write_cfg(with_profile(base, g, h1, h2), os.path.join(WORKDIR, "restart%d.cfg" % i))
        ok_cfg = configure(dev, bc.cfg_lines(cfg), out if a.verbose else (lambda *_: None))
        if not ok_cfg:
            out("cycle %d: cfg NOT accepted (rerun with --verbose to see replies)" % i)
            results.append(("cycle %d" % i, None, False))
            break
        _, e = run_capture(dev, os.path.join(WORKDIR, "restart%d.cap" % i), cfg, a.duration)
        block("cycle %d" % i, e, out)
        results.append(("cycle %d" % i, e, capture_ok(e)))
    checks = [("%s capture clean (accepted, 0 gaps, 0 missing)" % n, ok) for n, e, ok in results]
    checks.append((">= %d cycles run" % a.min_cycles, len(results) >= a.min_cycles))
    return bc.summarize(checks, out)


# --- chan -----------------------------------------------------------------------------------------------------------
BAD_WORDS = ("exception", "assert", "halt", "abort")


def probe_lines(port, lines, out=print):
    """Send lines, stop at the first Error. Returns (replies, errored) where replies = [(line, body_lines, raw)]."""
    replies = []
    for l in lines:
        raw = port.command(l, timeout=5.0)
        body = bc.reply_body(l, raw)
        replies.append((l, body, raw))
        out("> " + l)
        for b in body:
            out("    " + b)
        if any("Error" in b for b in body):
            return replies, True
    return replies, False


def judge_chan(replies, errored, follow, alive, state=None):
    """The firmware validates channelCfg at sensorStart (mmw_cli.c, "Error: channelCfg differs from the first
    sensorStart"), not at the channelCfg CLI line, which only stores it and answers Done."""
    last_line, last_body, _ = replies[-1]
    raw_all = " ".join(r[2] for r in replies).lower()
    return [
        ("sensorStart with the changed channelCfg answered with 'Error: channelCfg differs' (reply: %s)" % (
            " / ".join(last_body) or "none"),
         errored and last_line.startswith("sensorStart") and any("channelCfg differs" in b for b in last_body)),
        ("sensor not started after the rejected sensorStart (Sensor State %s, not 2)" % state, state != 2),
        ("no Exception/assert/halt text in any reply", not any(w in raw_all for w in BAD_WORDS)),
        ("CLI still answers after the rejection (prompt returned)", alive),
        ("following valid cfg + sensorStart: clean capture", capture_ok(follow)),
    ]


def cmd_chan(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    full = bc.cfg_lines(a.cfg)                       # includes the final sensorStart
    bad = with_command(full, "channelCfg", a.channel_cfg)
    port = common.CliPort(dev)
    try:
        replies, errored = probe_lines(port, bad, out)
        status = port.command("queryDemoStatus", timeout=5.0)
        alive = common.CliPort.PROMPT in status
        state = bc.sensor_state(status)
        if state == 2:                               # accepted (firmware gap): stop it so the follow-up can reconfigure
            out("sensor STARTED with the changed channelCfg: sending sensorStop")
            port.command("sensorStop", timeout=5.0)
    finally:
        port.close()
    out("--- now a valid cfg (flushCfg, original channelCfg) and a capture ---")
    follow = None
    if configure(dev, bc.cfg_lines(a.cfg, drop_last=True), lambda *_: None):
        _, follow = run_capture(dev, os.path.join(WORKDIR, "chan.cap"), a.cfg, a.duration)
        block("follow-up", follow, out)
    else:
        out("follow-up cfg NOT accepted")
    return bc.summarize(judge_chan(replies, errored, follow, alive, state), out)


# --- finite ---------------------------------------------------------------------------------------------------------
def with_num_frames(lines, n):
    """frameCfg <start> <end> <loops> <numFrames> <period> <trig> <delay> with numFrames replaced."""
    out, hit = [], 0
    for l in lines:
        t = l.split()
        if t[:1] == ["frameCfg"] and len(t) == 8:
            t[4] = str(n)
            l, hit = " ".join(t), hit + 1
        out.append(l)
    if hit != 1:
        raise ValueError("expected exactly one 8-token frameCfg line, found %d" % hit)
    return out


def lvds_frames(dev):
    """'LVDS HW frames done' from queryDemoStatus (None if absent)."""
    port = common.CliPort(dev)
    try:
        text = port.command("queryDemoStatus", timeout=5.0)
    finally:
        port.close()
    m = re.search(r"LVDS HW frames done:\s*(\d+)", text)
    return (int(m.group(1)) if m else None), bc.reply_body("queryDemoStatus", text)


def judge_finite(e, n_frames, nchirps, lvds):
    return [
        ("capture accepted, 0 gaps, 0 missing", capture_ok(e)),
        ("chirpAvail %s = %d frames x %d" % (e["avail"], n_frames, nchirps), e["avail"] == n_frames * nchirps),
        ("LVDS HW frames done %s = %d frames sent" % (lvds, n_frames), lvds == n_frames),
    ]


def cmd_finite(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    lines = with_num_frames(bc.cfg_lines(a.cfg, drop_last=True), a.frames)
    cfg = write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "finite.cfg"))
    if not configure(dev, lines, lambda *_: None):
        out("RESULT: FAIL (cfg not accepted)")
        return False
    nchirps = common.cfg_params(open(cfg).read())["nchirps"]
    dur = a.frames * 0.5103 + a.extra_s                 # the run ends by itself before dca_capture's sensorStop
    _, e = run_capture(dev, os.path.join(WORKDIR, "finite.cap"), cfg, dur)
    block("finite numFrames %d (%.1f s)" % (a.frames, dur), e, out)
    if e is None:
        out("RESULT: FAIL (capture tool failed)")
        return False
    lvds, body = lvds_frames(dev)
    out("queryDemoStatus: " + " | ".join(body))
    return bc.summarize(judge_finite(e, a.frames, nchirps, lvds), out)


# --- start0 ---------------------------------------------------------------------------------------------------------
def cmd_start0(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    out("(no cfg sent: the board keeps the last one; geometry is read from %s)" % os.path.basename(a.cfg))
    _, e = run_capture(dev, os.path.join(WORKDIR, "start0.cap"), a.cfg, a.duration,
                       ["--start-cmd", "sensorStart 0"])
    block("sensorStart 0", e, out)
    if e is None:
        out("RESULT: FAIL (capture tool failed or sensorStart 0 refused)")
        return False
    return bc.summarize([("capture clean after `sensorStart 0` restart", capture_ok(e)),
                         ("chirpAvail %s > 0" % e["avail"], bool(e["avail"]))], out)


# --- adc ------------------------------------------------------------------------------------------------------------
def adc_peaks(res):
    """Peak I and Q (counts, signed min/max each) over every chirp whose ADC bytes are all present."""
    import array
    cfg, B, H = res["cfg"], res["cfg"]["B"], res["cfg"]["H"]
    n_h = 2 * cfg["nrx"] * cfg["ns"]
    lo = {"I": 0, "Q": 0}
    hi = {"I": 0, "Q": 0}
    cnt = {}
    nused = 0
    for k in range(res["n_chirps"]):
        if not res["adc_ok"][k]:
            continue
        blk = array.array("h")
        blk.frombytes(bytes(res["stream"][k * B + H:k * B + H + 2 * n_h]))
        if sys.byteorder == "big":
            blk.byteswap()
        halves = (blk[0::2], blk[1::2])
        names = ("Q", "I") if cfg["swap"] == 1 else ("I", "Q")        # SampleSwap 1: low half-word = Q
        for nm, h in zip(names, halves):
            lo[nm], hi[nm] = min(lo[nm], min(h)), max(hi[nm], max(h))
        nused += 1
    return lo, hi, nused


def count_at(res, value):
    import array
    cfg, B, H = res["cfg"], res["cfg"]["B"], res["cfg"]["H"]
    n = 0
    for k in range(res["n_chirps"]):
        if res["adc_ok"][k]:
            blk = array.array("h")
            blk.frombytes(bytes(res["stream"][k * B + H:k * B + H + 4 * cfg["nrx"] * cfg["ns"]]))
            n += blk.count(value)
    return n


def decide_adc_bits(peak, n_at_pos, n_at_neg):
    """(adc_bits or None, reason). Signed full scale: 12-bit -> -2048..2047, 16-bit -> -32768..32767."""
    if peak > 2048:
        return 16, "peak %d exceeds 12-bit range (2048): 16-bit full scale" % peak
    if (n_at_pos >= 10 and n_at_neg + n_at_pos >= 10) and peak >= 2047:
        return 12, "samples pile up at the 12-bit rails (%d at +2047, %d at -2048)" % (n_at_pos, n_at_neg)
    return None, "peak %d is below 2047 with no rail pile-up: not clipped, cannot tell 12 from 16 bit" % peak


def judge_adc(res, out=print):
    lo, hi, nused = adc_peaks(res)
    peak = max(-lo["I"], -lo["Q"], hi["I"], hi["Q"])
    out("ADC peaks over %d complete chirps: I %d..%d, Q %d..%d (peak |.| %d)" % (
        nused, lo["I"], hi["I"], lo["Q"], hi["Q"], peak))
    bits = None
    if peak > 2048:
        bits, why = decide_adc_bits(peak, 0, 0)
        for rail in (32767, -32768):
            out("    samples at %d: %d" % (rail, count_at(res, rail)))
    else:
        bits, why = decide_adc_bits(peak, count_at(res, 2047), count_at(res, -2048))
    out("--adc-bits decision: %s" % (("%d  (%s)" % (bits, why)) if bits else "UNDECIDED  (%s)" % why))
    return [("--adc-bits determined from data: %s" % (bits if bits else "not clipped, rerun closer/higher --gain"),
             bits is not None)]


def cmd_adc(dev, a, out=print):
    os.makedirs(WORKDIR, exist_ok=True)
    cfg = a.cfg
    if a.capture:
        cap = a.capture
    else:
        lines = with_profile(bc.cfg_lines(cfg, drop_last=True), a.gain, a.hpf1, a.hpf2)
        cfg = write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "adc.cfg"))
        if not configure(dev, lines, lambda *_: None):
            out("RESULT: FAIL (cfg not accepted)")
            return False
        out("gain %d dB: put the reflector close / raise --gain until it clips" % a.gain)
        cap = os.path.join(WORKDIR, "adc.cap")
        rc, e = run_capture(dev, cap, cfg, a.duration)
        if e is None:
            out("RESULT: FAIL (capture tool failed)")
            return False
    e = evaluate(cap, cfg)
    block("adc capture", e, out)
    return bc.summarize(judge_adc(e["res"], out), out)


# --- cli ------------------------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli-port", help="CLI serial port (default: the single /dev/serial/by-id/*XDS110*-if00)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("long", help="60 s capture: timer not cutting off, bytes = chirpAvail x B, 0 sequence gaps")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--duration", type=float, default=60)
    p.add_argument("--timer-s", type=int, default=30, help="CONFIG_FPGA_GEN timer byte under test (default 30)")
    p = sub.add_parser("restart", help=">= 3 reconfigure cycles in one boot, a clean capture each")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--duration", type=float, default=30)
    p.add_argument("--points", default=DEFAULT_POINTS, help="rxGain_dB:hpf1:hpf2 per cycle (default %(default)s)")
    p.add_argument("--min-cycles", type=int, default=3)
    p.add_argument("--verbose", action="store_true", help="print every CLI reply")
    p = sub.add_parser("chan", help="channelCfg change rejection + a following valid start")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--duration", type=float, default=10)
    p.add_argument("--channel-cfg", default="15 1 0", help="the changed channelCfg args (default '%(default)s': 4 RX)")
    p = sub.add_parser("finite", help="finite numFrames: run ends by itself, sensorStop still works")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--frames", type=int, default=5)
    p.add_argument("--extra-s", type=float, default=3.0, help="seconds to wait beyond the run's end")
    p = sub.add_parser("start0", help="`sensorStart 0` restart with no new cfg (run after another capture)")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--duration", type=float, default=10)
    p = sub.add_parser("adc", help="peak |I|,|Q| of a clipped capture: which --adc-bits")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--capture", help="analyse this existing capture instead of taking one")
    p.add_argument("--gain", type=int, default=48, help="rxGain dB for the capture (default 48)")
    p.add_argument("--hpf1", type=int, default=0)
    p.add_argument("--hpf2", type=int, default=0)
    p.add_argument("--duration", type=float, default=5)
    a = ap.parse_args(argv)
    dev = bc.find_cli_port(a.cli_port)
    fn = {"long": cmd_long, "restart": cmd_restart, "chan": cmd_chan, "finite": cmd_finite, "start0": cmd_start0,
          "adc": cmd_adc}[a.cmd]
    return 0 if fn(dev, a) else 1


if __name__ == "__main__":
    sys.exit(main())
