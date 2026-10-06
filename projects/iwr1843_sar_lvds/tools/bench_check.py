#!/usr/bin/env python3
"""Short bench commands for docs/bench_bringup.md section 5 (run from firmware_dev/):

    uv run python projects/iwr1843_sar_lvds/tools/bench_check.py {cfg|status|stop|capture} [options]

The CLI port is the single /dev/serial/by-id/*XDS110*-if00 unless --cli-port is given. A cfg is accepted once per
power-up. Sensor state 2 = STARTED (MmwDemo_SensorState enum: INIT 0, OPENED 1, STARTED 2).
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sar_common as common  # noqa: E402

DEFAULT_CFG = os.path.join(os.path.dirname(HERE), "configs", "sar_example_2ms.cfg")
STARTED = 2


def find_cli_port(explicit=None):
    if explicit:
        return explicit
    ports = sorted(glob.glob("/dev/serial/by-id/*XDS110*-if00"))
    if len(ports) != 1:
        raise SystemExit("ERROR: expected exactly one /dev/serial/by-id/*XDS110*-if00, found %d%s; use --cli-port" % (
            len(ports), (": " + ", ".join(ports)) if ports else ""))
    return ports[0]


def reply_body(line, text):
    """Reply with the echoed command line and the prompt removed."""
    out = []
    for l in text.replace("\r", "").split("\n"):
        s = l.strip()
        if not s or s == line.strip() or s.startswith(common.CliPort.PROMPT) and s == common.CliPort.PROMPT:
            continue
        out.append(s.replace(common.CliPort.PROMPT, "").strip() or "")
    return [o for o in out if o]


def cfg_lines(path, drop_last=False):
    with open(path) as fh:
        lines = [l.strip() for l in fh if l.strip() and not l.strip().startswith("%")]
    return lines[:-1] if drop_last else lines


def send_cfg(port, lines, out=print):
    """Send lines; stop at the first Error. Returns True when every line got Done."""
    ok = True
    for l in lines:
        body = reply_body(l, port.command(l, timeout=5.0))
        out("> " + l)
        for b in body:
            out("    " + b)
        if any("Error" in b for b in body):
            out("    ^^^ ERROR on this line, stopping")
            return False
        if not any(b == "Done" or b.endswith("Done") for b in body):
            ok = False
    return ok


def run_cfg(port, path, no_start, out=print):
    ok = send_cfg(port, cfg_lines(path, drop_last=no_start), out)
    out("cfg: %s" % ("PASS (Done on every line)" if ok else "FAIL (Error or missing Done; keep this output)"))
    return ok


def sensor_state(text):
    m = re.search(r"Sensor State:\s*(\d+)", text)
    return int(m.group(1)) if m else None


def run_status(port, wait, out=print):
    def ask(cmd):
        t = port.command(cmd, timeout=5.0)
        out("> " + cmd)
        for b in reply_body(cmd, t):
            out("    " + b)
        return t
    st = ask("queryDemoStatus")
    s1 = common.parse_sarstats(ask("sarStats"))
    out("... waiting %g s" % wait)
    time.sleep(wait)
    s2 = common.parse_sarstats(ask("sarStats"))
    checks = []
    state = sensor_state(st)
    checks.append(("sensor running (state %s, want %d)" % (state, STARTED), state == STARTED))
    if s1 and s2:
        checks.append(("chirps increased (%s -> %s)" % (s1.get("chirps"), s2.get("chirps")),
                       s2.get("chirps", 0) > s1.get("chirps", 0)))
        checks.append(("frames increased (%s -> %s)" % (s1.get("frames"), s2.get("frames")),
                       s2.get("frames", 0) > s1.get("frames", 0)))
        checks.append(("chirpStartIsr == chirps (%s vs %s)" % (s2.get("chirpStartIsr"), s2.get("chirps")),
                       s2.get("chirpStartIsr") is not None and s2.get("chirpStartIsr") == s2.get("chirps")))
    else:
        checks.append(("sarStats parsed both times", False))
    return summarize(checks, out)


def summarize(checks, out=print):
    for name, ok in checks:
        out("  %s  %s" % ("PASS" if ok else "FAIL", name))
    allok = all(ok for _, ok in checks)
    out("RESULT: %s" % ("PASS" if allok else "FAIL"))
    return allok


def run_stop(port, out=print):
    t = port.command("sensorStop", timeout=5.0)
    out("> sensorStop")
    for b in reply_body("sensorStop", t):
        out("    " + b)
    return summarize([("no '%s'" % common.FRAME_END_MSG, common.FRAME_END_MSG not in t)], out)


def run_capture(dev, cfg, duration, out=print):
    port = common.CliPort(dev)
    try:
        ok = run_cfg(port, cfg, True, out)
    finally:
        port.close()
    if not ok:
        return False
    cap = "/tmp/bringup.cap"
    rc1 = subprocess.call([sys.executable, os.path.join(HERE, "dca_capture.py"), cap, "--cli-port", dev,
                           "--duration", str(duration)])
    rc2 = subprocess.call([sys.executable, os.path.join(HERE, "sar_parse.py"), cap, "--cfg", cfg])
    try:
        import json
        with open(cap + ".sarstats.json") as fh:
            out("chirpAvail: %s (expect 2295 or 2550)" % json.load(fh).get("chirpAvail"))
    except (OSError, ValueError) as e:
        out("chirpAvail: unavailable (%s)" % e)
    out("capture: exit codes dca_capture %d, sar_parse %d (see VERDICT above)" % (rc1, rc2))
    return rc1 == 0 and rc2 == 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli-port", help="CLI serial port (default: the single /dev/serial/by-id/*XDS110*-if00)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("cfg", help="send the cfg line by line")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--no-start", action="store_true", help="drop the final sensorStart line")
    p = sub.add_parser("status", help="queryDemoStatus, sarStats, wait, sarStats")
    p.add_argument("--wait", type=float, default=5.0)
    sub.add_parser("stop", help="sensorStop")
    p = sub.add_parser("capture", help="cfg without sensorStart, then dca_capture.py and sar_parse.py")
    p.add_argument("--cfg", default=DEFAULT_CFG)
    p.add_argument("--duration", type=float, default=5)
    a = ap.parse_args(argv)
    dev = find_cli_port(a.cli_port)
    if a.cmd == "capture":
        return 0 if run_capture(dev, a.cfg, a.duration) else 1
    port = common.CliPort(dev)
    try:
        if a.cmd == "cfg":
            ok = run_cfg(port, a.cfg, a.no_start)
        elif a.cmd == "status":
            ok = run_status(port, a.wait)
        else:
            ok = run_stop(port)
    finally:
        port.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
