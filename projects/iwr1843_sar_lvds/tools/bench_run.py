#!/usr/bin/env python3
"""One-command bench runs for firmware-10 Step 2.2 / 2.6.0 (run from firmware_dev/; `./bench` is a short wrapper):

    ./bench long       60 s capture: FPGA timer does not cut it off, bytes = chirpAvail x B, 0 sequence gaps
                       (= CONFIG_PACKET_DATA delay unit check)
    ./bench restart    >= 3 stop / re-cfg (changed rxGain, HPF) / start cycles in one boot, a capture + parse each

Each prints PASS/FAIL lines and a short block to paste back. HARDWARE TOOL: configures the radar over the CLI port
and records from the DCA1000 (via dca_capture.py, then sar_parse's analysis in-process). The board must be in run mode,
the DCA1000 on its network. A cfg is accepted once per power-up for the first configure; reconfigure after sensorStop +
flushCfg is what `restart` tests. Python stdlib + sar_common / sar_parse only.
"""
import argparse
import json
import os
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
    a = ap.parse_args(argv)
    dev = bc.find_cli_port(a.cli_port)
    fn = {"long": cmd_long, "restart": cmd_restart}[a.cmd]
    return 0 if fn(dev, a) else 1


if __name__ == "__main__":
    sys.exit(main())
