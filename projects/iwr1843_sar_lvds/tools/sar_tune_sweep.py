#!/usr/bin/env python3
"""Sweep rxGain and HPF corners on the bench without a power cycle; tabulate the tuning report per point.

    uv run --group tools python sar_tune_sweep.py BASE.cfg OUTDIR --cli-port /dev/ttyACM0 --duration 3
        --gains 24,30,36 --hpf 175:350,350:700 [--reflector-range 1.5] [DCA options as dca_capture.py]
    uv run --group tools python sar_tune_sweep.py BASE.cfg OUTDIR --gains 24,30 --hpf 175:350 --dry-run

HARDWARE TOOL (unless --dry-run): drives the radar CLI and the DCA1000. The radar must have been powered up with the
base cfg's channelCfg / adcCfg / lowPower (those three keep their first-start values, docs/sar_cfg_guide.md section 1).

Per point (every gain x HPF pair):
  1. edit the base cfg's profileCfg (rxGain, hpfCornerFreq1/2: nothing else depends on them);
  2. check the edited cfg with sar_cfg_check.py; a point that fails is skipped and listed;
  3. send the cfg (sensorStop, flushCfg, ... everything except the final sensorStart);
  4. capture_run() of dca_capture.py: arm the DCA1000, sensorStart, wait, sensorStop, read sarStats, stop recording
     (the capture requirement, docs/lvds_data_format.md section 1: the same flow as dca_capture.py);
  5. sar_parse checks 1-4. A rejected capture is NOT tuned on: its row says REJECTED, re-run that point;
  6. sar_tune_report values: peak dBFS, clipped chirps, noise floor, reflector SNR.
Files per point in OUTDIR: point_<gain>_<hpf1>-<hpf2>.cfg / .cap / .cap.sarstats.json (+ parse outputs and image).
The table is printed and written to OUTDIR/sweep.txt. HPF pairs are kHz as in the datasheet (HPF1 175/235/350/700,
HPF2 350/700/1400/2800). Restart without a power cycle relies on the firmware's sensorStop + flushCfg path.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dca_capture  # noqa: E402
import sar_cfg_check as chk  # noqa: E402
import sar_common as common  # noqa: E402
import sar_parse  # noqa: E402


def hpf_codes(pair):
    h1, h2 = pair.split(":")
    try:
        return chk.HPF1_KHZ.index(int(h1)), chk.HPF2_KHZ.index(int(h2))
    except ValueError:
        raise ValueError("HPF pair %r: HPF1 must be one of %s kHz, HPF2 one of %s kHz" % (pair, chk.HPF1_KHZ,
                                                                                         chk.HPF2_KHZ))


def edit_cfg(text, gain, h1, h2):
    """Return cfg text with the profileCfg rxGain / hpfCornerFreq1 / hpfCornerFreq2 replaced (codes 0-3 for HPF)."""
    out, done = [], False
    for line in text.splitlines():
        tok = line.split()
        if tok and tok[0] == "profileCfg" and len(tok) == 15 and not done:
            tok[12], tok[13], tok[14] = str(h1), str(h2), str(gain)
            line = " ".join(tok)
            done = True
        out.append(line)
    if not done:
        raise ValueError("base cfg has no 14-argument profileCfg line")
    return "\n".join(out) + "\n"


def cfg_lines_before_start(text):
    """The commands to send: every non-comment line except the final sensorStart (the capture flow sends that)."""
    lines = [ln.strip() for ln in text.splitlines() if ln.strip() and not ln.strip().startswith("%")]
    if lines and lines[-1].split()[0] == "sensorStart":
        lines = lines[:-1]
    return lines


def send_cfg(cli, text, log=print):
    for ln in cfg_lines_before_start(text):
        resp = cli.command(ln, timeout=3.0)
        if "Error" in resp or "not recognized" in resp or "not a valid" in resp:
            raise RuntimeError("radar rejected %r: %s" % (ln, resp.strip()[-200:]))


def run_point(gain, pair, base_text, cli, dca, data_sock, outdir, duration, reflector_range=None, log=print,
              adc_bits=12):
    """One sweep point. Returns a dict row (verdict, peak_db, clipped, noise_db, snr_db or skip reason)."""
    import sar_tune_report as rep
    h1, h2 = hpf_codes(pair)
    tag = "point_%d_%s" % (gain, pair.replace(":", "-"))
    base = os.path.join(outdir, tag)
    text = edit_cfg(base_text, gain, h1, h2)
    with open(base + ".cfg", "w") as fh:
        fh.write(text)
    row = {"gain": gain, "hpf": pair, "verdict": "", "peak_db": None, "clipped": None, "noise_db": None,
           "snr_db": None, "note": ""}
    chk_rep = chk.analyze(text, chk_opts())
    if chk_rep.errors:
        row.update(verdict="SKIPPED", note="sar_cfg_check: " + chk_rep.errors[0])
        return row
    send_cfg(cli, text, log)
    sidecar = dca_capture.capture_run(dca, data_sock, cli, base + ".cap", duration, log=log)
    cfg = common.cfg_params(text)
    res = sar_parse.analyze(common.read_capture(base + ".cap"), cfg, sidecar)
    log(sar_parse.render(res))
    if not res["accepted"]:
        row.update(verdict="REJECTED", note="failing: " + ", ".join(
            ["check %d" % n for n in res["failing"]] + (["recording flow"] if not res["flow_ok"] else [])))
        return row
    sar_parse.write_outputs(res, base)
    r = rep.analyze_run(base, cfg, adc_bits=adc_bits, reflector_range=reflector_range)
    rep.plot(r, base + "_tune.png")
    row.update(verdict="ok", peak_db=r["peak_max_db"], clipped=len(r["clipped"]), noise_db=r["noise_db"],
               snr_db=r["reflector"]["snr_db"] if r["reflector"] else None,
               note="fw result unknown for %d chirp(s)" % r["fw_unknown"])
    return row


def chk_opts():
    import types
    return types.SimpleNamespace(max_range=None, speed=None, dmax=None, tb_min=300.0, if_margin=0.8, lvds_margin=10.0)


def table(rows):
    fmt = "%5s %-9s %-9s %9s %8s %9s %8s  %s"
    L = [fmt % ("gain", "HPF kHz", "verdict", "peak dBFS", "clipped", "noise dBFS", "SNR dB", "note")]
    for r in rows:
        f = lambda v, p: "-" if v is None else p % v      # noqa: E731
        L.append(fmt % (r["gain"], r["hpf"], r["verdict"], f(r["peak_db"], "%.1f"), f(r["clipped"], "%d"),
                        f(r["noise_db"], "%.1f"), f(r["snr_db"], "%.1f"), r["note"]))
    return "\n".join(L)


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog="Read docs/tuning_guide.md.")
    ap.add_argument("cfg", help="base cfg (sensorStop ... sensorStart), e.g. configs/sar_example_2ms.cfg")
    ap.add_argument("outdir", help="directory for the per-point files and sweep.txt")
    ap.add_argument("--gains", default="30", help="comma list of rxGain, dB (even, 24-48) (default 30)")
    ap.add_argument("--hpf", default="175:350", help="comma list of HPF1:HPF2 pairs in kHz (default 175:350)")
    ap.add_argument("--duration", type=float, default=3.0, help="seconds per point (default 3)")
    ap.add_argument("--reflector-range", type=float, help="range of a known reflector, m (adds SNR)")
    ap.add_argument("--adc-bits", type=int, default=12)
    ap.add_argument("--dry-run", action="store_true", help="no hardware: list the points and check their cfgs")
    ap.add_argument("--cli-port", help="radar CLI serial port (needed unless --dry-run)")
    ap.add_argument("--fpga-ip", default="192.168.33.180")
    ap.add_argument("--host-ip", default="192.168.33.30")
    ap.add_argument("--cmd-port", type=int, default=4096)
    ap.add_argument("--data-port", type=int, default=4098)
    ap.add_argument("--lanes", type=int, choices=(2, 4), default=2)
    ap.add_argument("--timer-s", type=int, default=30)
    return ap


def main(argv=None):
    opt = build_parser().parse_args(argv)
    try:
        gains = [int(x) for x in opt.gains.split(",")]
        pairs = [p.strip() for p in opt.hpf.split(",")]
        for p in pairs:
            hpf_codes(p)
        with open(opt.cfg) as fh:
            base_text = fh.read()
        edit_cfg(base_text, 30, 0, 0)
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    os.makedirs(opt.outdir, exist_ok=True)
    points = [(g, p) for g in gains for p in pairs]
    if opt.dry_run:
        rows = []
        for g, p in points:
            h1, h2 = hpf_codes(p)
            rep = chk.analyze(edit_cfg(base_text, g, h1, h2), chk_opts())
            rows.append({"gain": g, "hpf": p, "verdict": "cfg ok" if not rep.errors else "SKIPPED", "peak_db": None,
                         "clipped": None, "noise_db": None, "snr_db": None,
                         "note": rep.errors[0] if rep.errors else ""})
        print(table(rows))
        return 0
    if not opt.cli_port:
        print("error: --cli-port is required unless --dry-run", file=sys.stderr)
        return 2
    try:
        import numpy  # noqa: F401
        import matplotlib  # noqa: F401
    except ImportError as exc:
        print("error: %s. Use: uv run --group tools python sar_tune_sweep.py ..." % exc, file=sys.stderr)
        return 2
    cli = common.CliPort(opt.cli_port)
    dca = common.Dca1000(opt.fpga_ip, opt.host_ip, opt.cmd_port)
    data_sock = dca_capture.open_data_socket(opt.host_ip, opt.data_port)
    rows = []
    try:
        dca_capture.configure_dca(dca, opt.lanes, opt.timer_s)
        for g, p in points:
            print("--- point: gain %d dB, HPF %s kHz" % (g, p))
            rows.append(run_point(g, p, base_text, cli, dca, data_sock, opt.outdir, opt.duration,
                                  opt.reflector_range, adc_bits=opt.adc_bits))
    except (RuntimeError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
    finally:
        data_sock.close()
        dca.close()
        cli.close()
    text = table(rows)
    print(text)
    with open(os.path.join(opt.outdir, "sweep.txt"), "w") as fh:
        fh.write(text + "\n")
    return 0 if rows and all(r["verdict"] == "ok" for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
