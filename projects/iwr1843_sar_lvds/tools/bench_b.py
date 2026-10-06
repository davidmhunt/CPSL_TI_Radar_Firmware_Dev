"""Set B bench commands (firmware-10 Steps 2.3 to 2.6; a corner reflector at a MEASURED range is needed). Registered by
bench_run.py:

    ./bench tune --range M          sweep gain x HPF with sar_tune_sweep's flow; prints clip counts, sat flags, SNR, a chosen point
    ./bench sat --range M --gain G  2.6.6 saturation vs rxGain, >= 5 steps up to clipping, >= 30 s each, lag alignment
    ./bench endurance --range M --gain G --hpf A:B   10 min run: G1-G4, G6, G7 numbers (streaming analysis, bounded memory)
    ./bench tb --add-us N --range M --gain G --hpf A:B   repeat the boundary check with Tb + N us (the 2.5 follow-up)

--adc-bits N: the ADC full scale (12 -> 2048, 16 -> 32768) from `./bench adc`; without it the value `./bench adc` left in
/tmp/bench_run/adc_bits.txt is used, else 12 with a warning. All of them end with RESULT lines. HARDWARE TOOLS.
numpy and matplotlib needed (the `tools` uv group; the ./bench wrapper adds it).
"""
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_a as A  # noqa: E402
import bench_run as R  # noqa: E402

bc, common, sar_parse = R.bc, R.common, R.sar_parse
WORKDIR = R.WORKDIR
ADC_BITS_FILE = os.path.join(WORKDIR, "adc_bits.txt")
C = 299792458.0


def resolve_adc_bits(arg, out=print):
    if arg:
        return int(arg)
    try:
        with open(ADC_BITS_FILE) as fh:
            v = int(fh.read().strip())
        out("ADC bits %d (from ./bench adc, %s)" % (v, ADC_BITS_FILE))
        return v
    except (OSError, ValueError):
        out("WARNING: --adc-bits not given and ./bench adc has not decided it: assuming 12 (2048 counts full scale). "
            "Clip counts and dBFS are only trustworthy once you pass --adc-bits N.")
        return 12


def hpf_arg(text):
    import sar_tune_sweep as sw
    return text, sw.hpf_codes(text)


def tuned_lines(a):
    """Base cfg lines (no sensorStart) with the tuned gain / HPF."""
    import sar_tune_sweep as sw
    sw.check_gain(a.gain)
    _, (c1, c2) = hpf_arg(a.hpf)
    return R.with_profile(bc.cfg_lines(a.cfg, drop_last=True), a.gain, c1, c2)


def expected_bin(cfg, range_m):
    slope = abs(cfg["slope_mhz_us"]) * 1e12
    return range_m * 2 * slope / C * cfg["ns"] / (cfg["fs_ksps"] * 1e3)


# --- tune -------------------------------------------------------------------------------------------------------------
def tune_row(g, pair, r, verdict, note=""):
    row = dict(gain=g, hpf=pair, verdict=verdict, note=note, adc_clip=None, fw_sat=None, peak=None, noise=None,
               snr=None, rng=None)
    if r is not None:
        refl = r["reflector"]
        row.update(adc_clip=len(r["adc_clip"]), fw_sat=len(r["fw_sat"]), union=len(r["clipped"]), peak=r["peak_max_db"],
                   noise=r["noise_db"], snr=refl["snr_db"] if refl else None, rng=refl["range_m"] if refl else None)
    return row


def tune_table(rows):
    fmt = "%4s %-8s %-9s %7s %6s %9s %9s %7s %7s  %s"
    L = [fmt % ("gain", "HPF kHz", "verdict", "ADCclip", "fwSat", "peak dBFS", "noise dBFS", "SNR dB", "at m", "note")]
    f = lambda v, p: "-" if v is None else p % v      # noqa: E731
    for x in rows:
        L.append(fmt % (x["gain"], x["hpf"], x["verdict"], f(x["adc_clip"], "%d"), f(x["fw_sat"], "%d"),
                        f(x["peak"], "%.1f"), f(x["noise"], "%.1f"), f(x["snr"], "%.1f"), f(x["rng"], "%.2f"), x["note"]))
    return "\n".join(L)


def choose(rows):
    """Clean points (verdict ok, 0 clipped by ADC or firmware flag), best reflector SNR first."""
    clean = [x for x in rows if x["verdict"] == "ok" and x.get("union") == 0]
    return sorted(clean, key=lambda x: (-(x["snr"] if x["snr"] is not None else -999), -x["gain"]))


def cmd_tune(dev, a, out=print):
    import dca_capture
    import sar_tune_report as rep
    import sar_tune_sweep as sw
    adc_bits = resolve_adc_bits(a.adc_bits, out)
    outdir = os.path.join(WORKDIR, "tune")
    os.makedirs(outdir, exist_ok=True)
    with open(a.cfg) as fh:
        base_text = fh.read()
    gains = [int(x) for x in a.gains.split(",")]
    pairs = [p.strip() for p in a.hpf.split(",")]
    for g in gains:
        sw.check_gain(g)
    for p in pairs:
        sw.hpf_codes(p)
    out("reflector at %.2f m (--range); %d points x %g s; files in %s" % (a.range, len(gains) * len(pairs), a.duration, outdir))
    d = dca_capture.build_parser().parse_args(["x"])
    cli = common.CliPort(dev)
    dca = common.Dca1000(d.fpga_ip, d.host_ip, d.cmd_port)
    sock = dca_capture.open_data_socket(d.host_ip, d.data_port, d.rcvbuf_mb << 20)
    rows = []
    try:
        dca_capture.configure_dca(dca, 2, 30, log=lambda *_: None)
        for g in gains:
            for p in pairs:
                rows.append(tune_point(g, p, base_text, cli, dca, sock, outdir, a.duration, a.range, adc_bits, rep, sw,
                                       dca_capture))
                out("  gain %d HPF %s: %s" % (g, p, rows[-1]["verdict"] + (" " + rows[-1]["note"] if rows[-1]["note"] else "")))
    except (RuntimeError, OSError) as exc:
        out("error: %s" % exc)
    finally:
        sock.close()
        dca.close()
        cli.close()
    out(tune_table(rows))
    best = choose(rows)
    ok = bool(rows) and bool(best) and all(x["verdict"] in ("ok", "SKIPPED") for x in rows)
    if best:
        out("CANDIDATES (0 clipped chirps, best reflector SNR first): " + "; ".join(
            "%d dB %s (SNR %s)" % (x["gain"], x["hpf"], "%.1f" % x["snr"] if x["snr"] is not None else "n/a") for x in best[:5]))
        b = best[0]
        out("CHOSEN: gain %d dB, HPF %s. Next: ./bench endurance --range M --gain %d --hpf %s --adc-bits %d "
            "(M = the reflector's measured 3-10 m)" % (b["gain"], b["hpf"], b["gain"], b["hpf"], adc_bits))
    else:
        out("no point without clipping: raise the HPF pair, lower the gain, or move the reflector farther (docs/tuning_guide.md section 6)")
    out("NOT checked: far-scene SNR needs a real far target; SNR here is the reflector's over the far-bin noise floor.")
    return bc.summarize([("at least one clean point (0 clipped chirps) and every point captured", ok)], out)


def tune_point(g, pair, base_text, cli, dca, sock, outdir, duration, range_m, adc_bits, rep, sw, dca_capture):
    tag = "point_%d_%s" % (g, pair.replace(":", "-"))
    base = os.path.join(outdir, tag)
    h1, h2 = sw.hpf_codes(pair)
    text = sw.edit_cfg(base_text, g, h1, h2)
    with open(base + ".cfg", "w") as fh:
        fh.write(text)
    errs = sw.chk.analyze(text, sw.chk_opts()).errors
    if errs:
        return tune_row(g, pair, None, "SKIPPED", errs[0])
    sw.send_cfg(cli, text)
    side = dca_capture.capture_run(dca, sock, cli, base + ".cap", duration, log=lambda *_: None)
    cfg = common.cfg_params(text)
    res = sar_parse.analyze(common.read_capture(base + ".cap"), cfg, side)
    if not res["accepted"]:
        return tune_row(g, pair, None, "REJECTED", "failing " + ",".join(str(n) for n in res["failing"]))
    sar_parse.write_outputs(res, base)
    r = rep.analyze_run(base, cfg, adc_bits=adc_bits, reflector_range=range_m)
    rep.plot(r, base + "_tune.png")
    return tune_row(g, pair, r, "ok")


# --- sat --------------------------------------------------------------------------------------------------------------
def gain_ladder(tuned, n=5, step=4, lo=24, hi=48):
    """>= n even gains: the tuned one, then up in `step` to `hi`; filled downwards if the top is reached too soon."""
    g = [tuned]
    x = tuned + step
    while x <= hi:
        g.append(x)
        x += step
    x = tuned - step
    while len(g) < n and x >= lo:
        g.insert(0, x)
        x -= step
    return sorted(g)


def sat_point_stats(r, side, cfg):
    """Counts for one gain point from an analyze_stream result and its sarStats sidecar."""
    fw = A.parse_stats(side.get("raw", "")).get("saturatedChirps")
    known = r["adc_ok"] & (r["sat_lag"] >= 0)
    return dict(fw=fw, parser=int((r["sat_lag"] > 0).sum()), adc=int(r["adc_clip"].sum()), known=int(known.sum()),
                n=r["n_chirps"], accepted=r["accepted"], failing=r["failing"])


def agreement(r):
    """ADC full-scale vs firmware flag agreement with the lag applied, with no lag, and with a fixed lag of 1."""
    import numpy as np
    n = r["n_chirps"]
    clip = r["adc_clip"]
    out = {}
    sets = {"lag applied": r["sat_lag"], "no lag": r["sat_nolag"]}
    la1 = np.full(n, -1, dtype=np.int64)                   # as if every record had satRefLag 1
    valid = r["valid"] & ((r["flags"] & 1) != 0)
    idx = np.flatnonzero(valid)
    # the slices the record carries are not kept per record in the result; rebuild from the no-lag array
    la1[idx[idx >= 1] - 1] = r["sat_nolag"][idx[idx >= 1]]
    sets["lag fixed at 1"] = la1
    for name, s in sets.items():
        known = r["adc_ok"] & (s >= 0)
        flag = s > 0
        nk = int(known.sum())
        agree = int((flag[known] == clip[known]).sum())
        nclip = int(clip[known].sum())
        hit = int((flag & clip)[known].sum())
        out[name] = dict(known=nk, agree=agree, acc=(agree / nk if nk else None), clip=nclip, hit=hit,
                         recall=(hit / nclip if nclip else None), flagged=int(flag[known].sum()))
    return out


def judge_sat(points, tuned_gain, tol=0):
    """points = [(gain, stats dict)] sorted by gain; agreement dict for the top point in points[-1][1]['agree']."""
    checks = []
    for name in ("fw", "parser", "adc"):
        vals = [p[name] for _, p in points]
        mono = all(b + tol >= a for a, b in zip(vals, vals[1:]))
        label = {"fw": "firmware sarStats saturatedChirps", "parser": "parser (lag applied) saturated chirps",
                 "adc": "report ADC-clipped chirps"}[name]
        checks.append(("%s non-decreasing with gain: %s" % (label, vals), mono))
    top_g, top = points[-1]
    t = dict(points)[tuned_gain] if tuned_gain in dict(points) else None
    checks.append(("0 at the tuned point (%d dB): fw %s, parser %s, ADC %s" % (
        tuned_gain, t and t["fw"], t and t["parser"], t and t["adc"]),
        t is not None and t["fw"] == 0 and t["parser"] == 0 and t["adc"] == 0))
    checks.append(("> 0 at the top point (%d dB): fw %s, parser %s, ADC %s" % (top_g, top["fw"], top["parser"], top["adc"]),
                   bool(top["fw"]) and top["parser"] > 0 and top["adc"] > 0))
    checks.append(("firmware count vs parser count agree within max(2, 1%%) at every point: %s" % (
        [(p["fw"], p["parser"]) for _, p in points]),
        all(p["fw"] is not None and abs(p["fw"] - p["parser"]) <= max(2, 0.01 * p["fw"]) for _, p in points)))
    ag = top.get("agree")
    if ag:
        la, nl = ag["lag applied"], ag["no lag"]
        checks.append(("top point, lag applied: ADC-full-scale vs flag agree on %s of %d known chirps (>= 99%%); "
                       "ADC-clipped chirps flagged: %s of %d" % (
                           "%.2f%%" % (100 * la["acc"]) if la["acc"] is not None else "n/a", la["known"], la["hit"], la["clip"]),
                       la["acc"] is not None and la["acc"] >= 0.99))
        checks.append(("top point: no lag agrees no better (%s vs %s with lag; fixed lag 1: %s)" % (
            "%.2f%%" % (100 * nl["acc"]) if nl["acc"] is not None else "n/a",
            "%.2f%%" % (100 * la["acc"]) if la["acc"] is not None else "n/a",
            "%.2f%%" % (100 * ag["lag fixed at 1"]["acc"]) if ag["lag fixed at 1"]["acc"] is not None else "n/a"),
            la["acc"] is not None and nl["acc"] is not None and la["acc"] >= nl["acc"]))
    return checks


def cmd_sat(dev, a, out=print):
    import bench_stream as bs
    adc_bits = resolve_adc_bits(a.adc_bits, out)
    outdir = os.path.join(WORKDIR, "sat")
    os.makedirs(outdir, exist_ok=True)
    A_gains = [int(x) for x in a.gains.split(",")] if a.gains else gain_ladder(a.gain, a.points)
    if a.gain not in A_gains:
        out("RESULT: FAIL (--gain %d is not in --gains %s)" % (a.gain, A_gains))
        return False
    import sar_tune_sweep as sw
    _, (c1, c2) = hpf_arg(a.hpf)
    base = bc.cfg_lines(a.cfg, drop_last=True)
    out("gains %s dB, HPF %s, %g s each, reflector %.2f m; the top gain must clip (move the reflector closer if it does not)" % (
        A_gains, a.hpf, a.duration, a.range))
    points = []
    for g in A_gains:
        sw.check_gain(g)
        lines = R.with_profile(base, g, c1, c2)
        cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(outdir, "g%d.cfg" % g))
        if not A.configure_show(dev, lines, out):
            out("gain %d: cfg not accepted" % g)
            return bc.summarize([("cfg accepted at gain %d" % g, False)], out)
        cap = os.path.join(outdir, "g%d.cap" % g)
        side = A.capture_only(dev, cap, cfgp, a.duration, out=out)
        if side is None:
            return bc.summarize([("capture at gain %d" % g, False)], out)
        with open(cfgp) as fh:
            cfg = common.cfg_params(fh.read())
        r = bs.analyze_stream(cap, cfg, side, adc_bits=adc_bits, reflector_range=a.range)
        st = sat_point_stats(r, side, cfg)
        st["agree"] = agreement(r) if g == max(A_gains) else None
        points.append((g, st))
        out("  gain %2d dB: fw saturatedChirps %s, parser %d, ADC-clipped %d (%d chirps, checks 1-4 %s)" % (
            g, st["fw"], st["parser"], st["adc"], st["n"], "ok" if st["accepted"] else "REJECTED %s" % st["failing"]))
    points.sort(key=lambda p: p[0])
    checks = [("every capture accepted by checks 1-4", all(p["accepted"] for _, p in points))]
    checks += judge_sat(points, a.gain)
    return bc.summarize(checks, out)


# --- long runs (endurance, tb) ----------------------------------------------------------------------------------------
def polled_capture(dev, cap, duration, timer_s, poll_s, out=print):
    import dca_capture
    d = dca_capture.build_parser().parse_args(["x"])
    cli = common.CliPort(dev)
    dca = common.Dca1000(d.fpga_ip, d.host_ip, d.cmd_port)
    sock = dca_capture.open_data_socket(d.host_ip, d.data_port, d.rcvbuf_mb << 20)

    def on_poll(t, raw):
        s = A.parse_stats(raw)
        out("  t=%4.0fs chirps %s chirpStartIsr %s chirpAvail %s frames %s late %s missed %s resync %s/%s" % (
            t, s.get("chirps"), s.get("chirpStartIsr"), s.get("chirpAvail"), s.get("frames"), s.get("lateIsr"),
            s.get("missedChirpIsr"), s.get("frameResync"), s.get("availResync")))
    try:
        dca_capture.configure_dca(dca, 2, timer_s, log=lambda *_: None)
        side = dca_capture.capture_run(dca, sock, cli, cap, duration, log=lambda *_: None, poll_s=poll_s, on_poll=on_poll)
    finally:
        sock.close()
        dca.close()
        cli.close()
    return side


def judge_polls(side):
    """2.6.1 mid-run polls: chirps, chirpStartIsr and chirpAvail within 2 of each other, nothing missed or late."""
    polls = [A.parse_stats(p["raw"]) for p in side.get("polls", [])]
    if not polls:
        return [("sarStats polled during the run", False)]
    close = all(max(abs(p.get("chirps", 0) - p.get("chirpStartIsr", 0)), abs(p.get("chirpAvail", 0) - p.get("chirpStartIsr", 0)),
                    abs(p.get("chirps", 0) - p.get("chirpAvail", 0))) <= 2 for p in polls)
    rising = all(b.get("chirps", 0) > a_.get("chirps", 0) for a_, b in zip(polls, polls[1:]))
    zero = all(all(p.get(k) == 0 for k in ("lateIsr", "missedChirpIsr", "frameResync", "availResync")) for p in polls)
    return [("%d mid-run sarStats polls: chirps, chirpStartIsr, chirpAvail within 2 in flight" % len(polls), close),
            ("polls: chirps rising every minute", rising),
            ("polls: lateIsr, missedChirpIsr, frameResync, availResync all 0", zero)]


def post_stop_reads(dev, side, reads, gap, out=print):
    """After sensorStop: the capture's own reading, `reads` more `gap` s apart, one more before the next sensorStart."""
    seq = [("after sensorStop", A.parse_stats(side["raw"]))]
    for i in range(reads):
        time.sleep(gap)
        seq.append(("+%g s" % (gap * (i + 1)), A.parse_stats(A.read_stats(dev))))
    return seq


def lag_text(r):
    h = r["lag_hist"]
    tot = sum(h.values())
    return ", ".join("lag %d: %d (%.2f%%)" % (k, v, 100.0 * v / max(tot, 1)) for k, v in sorted(h.items())) or "none", tot


def judge_long(r, side, cfg, a, duration, post_reads, ps, out=print):
    """Groups of (name, ok) per goal / criterion from an analyze_stream result."""
    import numpy as np
    G = {}
    nc, B = cfg["nchirps"], cfg["B"]
    tc_us, tb_us = cfg["tc_s"] * 1e6, cfg["tb_s"] * 1e6
    stats = A.parse_stats(side.get("raw", ""))
    n = r["n_chirps"]
    expect = duration / (nc * cfg["tc_s"] + cfg["tb_s"]) * nc
    span = (r["ts"][r["valid"]].max() - r["ts"][r["valid"]].min()) / 1e8 if r["valid"].any() else 0
    mbs = r["info"]["end"] / span / 1e6 if span else 0
    bad_rec = int((~r["valid"]).sum())
    g = G.setdefault("G1 throughput", [])
    g += [("0 UDP sequence gaps over %d datagrams" % r["info"]["datagrams"], r["seq_gaps"] == 0),
          ("0 wholly missing packets, %d invalid record(s) of %d chirps (<= 1: the tail hole)" % (bad_rec, n),
           r["absent"] == 0 and bad_rec <= 1),
          ("bytes recorded %d vs chirpAvail %s x B %d: check 4 %s (%s)" % (
              r["info"]["end"], side.get("chirpAvail"), B, r["checks"][3].status, r["checks"][3].detail[:60]),
           r["checks"][3].status == "PASS"),
          ("%d chirps recorded vs ~%d expected for %g s (>= 99%%); %.2f MB/s (expect ~6.66)" % (n, expect, duration, mbs),
           n >= 0.99 * expect)]
    din, dbd = r["d_in"] * 1e6, r["d_bd"] * 1e6
    g = G.setdefault("G2 timing", [])
    dev_in = np.abs(din - tc_us)
    p999 = float(np.percentile(dev_in, 99.9)) if len(din) else float("nan")
    dev_bd = np.abs(dbd - (tc_us + tb_us))
    g += [("in-frame dt mean %.2f us, p99.9 |dev| %.2f us (<= 5 us of %.0f)" % (din.mean() if len(din) else float("nan"), p999, tc_us),
           len(din) > 0 and p999 <= 5.0),
          ("boundary dt mean %.2f us (%+.2f vs cfg %.0f), max |dev| %.2f us over %d boundaries (every one <= 5 us)" % (
              dbd.mean() if len(dbd) else float("nan"), (dbd.mean() - tc_us - tb_us) if len(dbd) else float("nan"),
              tc_us + tb_us, dev_bd.max() if len(dbd) else float("nan"), len(dbd)), len(dbd) > 0 and dev_bd.max() <= 5.0),
          ("0 LATE-flagged records (%d) and sarStats lateIsr %s" % (r["late"], stats.get("lateIsr")),
           r["late"] == 0 and stats.get("lateIsr") == 0),
          ("sarStats agrees with the parser: chirps %s = chirpAvail %s = %d parsed; frames %s ~ %d" % (
              stats.get("chirps"), stats.get("chirpAvail"), n, stats.get("frames"), n // nc),
           stats.get("chirps") == stats.get("chirpAvail") == n and abs((stats.get("frames") or 0) - n // nc) <= 1),
          ("sarStats saturatedChirps %s vs parser %d (within max(2, 1%%))" % (stats.get("saturatedChirps"), int((r["sat_lag"] > 0).sum())),
           stats.get("saturatedChirps") is not None and abs(stats["saturatedChirps"] - int((r["sat_lag"] > 0).sum())) <= max(2, 0.01 * stats["saturatedChirps"]))]
    snr = (r["bref_db"] - r["noise_db"]) if r["bref_db"] is not None else float("nan")
    for key, label in (("G3 phase continuity", False), ("G6 APLL/SYNTH boundary", True)):
        g = G.setdefault(key, [])
        if ps is None:
            g.append(("step statistics available (enough chirp pairs)", False))
            continue
        g.append(("reflector bin %s: SNR %.1f dB >= 20 (below that a step is not visible)" % (r["bref"], snr), snr >= 20))
        p = ps["phi"]
        g.append(("phase: boundary step %+.4f rad vs in-frame p99 %.4f rad (|step| <= p99); max boundary dev %.3f" % (
            p["step"], p["p99_in"], p["max_bd"]), abs(p["step"]) <= p["p99_in"]))
        g.append(("phase: %.1f%% of %d boundaries beyond the in-frame p99 (<= 5%%; 1%% expected with no step)" % (
            100 * p["frac_over"], ps["n_bd"]), p["frac_over"] <= 0.05))
        if label:
            q = ps["amp"]
            g.append(("amplitude: boundary step %+.3f dB vs in-frame p99 %.3f dB (|step| <= p99)" % (q["step"], q["p99_in"]),
                      abs(q["step"]) <= q["p99_in"]))
            g.append(("amplitude: %.1f%% of boundaries beyond the in-frame p99 (<= 5%%)" % (100 * q["frac_over"]),
                      q["frac_over"] <= 0.05))
            g.append(("boundary dt within 5 us of Tc+Tb on every boundary (max %.2f us)" % (dev_bd.max() if len(dbd) else float("nan")),
                      len(dbd) > 0 and dev_bd.max() <= 5.0))
    g = G.setdefault("G4 data sanity", [])
    eb = expected_bin(cfg, a.range) if getattr(a, "range", None) else None
    if eb is not None:
        g.append(("reflector range-bin: mean-profile peak bin %d, tracked bin %s, first/last 1000 chirps %s/%s; expected %.1f +- 2" % (
            r["peak_bin"], r["bref"], r["first_peak_bin"], r["last_peak_bin"], eb),
            all(b is not None and abs(b - eb) <= 2 for b in (r["peak_bin"], r["bref"], r["first_peak_bin"], r["last_peak_bin"]))))
    nclip = int(r["adc_clip"].sum())
    nfw = int((r["sat_lag"] > 0).sum())
    g.append(("0 clipped chirps at the tuned point: ADC full scale %d (--adc-bits %d), firmware flag %d" % (nclip, r["adc_bits"], nfw),
              nclip == 0 and nfw == 0))
    g.append(("recorded: noise floor %.1f dBFS, reflector %.1f dBFS at %.2f m, SNR %.1f dB, peak max %.1f dBFS" % (
        r["noise_db"], r["bref_db"] or float("nan"), r["bref_range"] or float("nan"), snr, float(r["peak_db"].max())), True))
    g = G.setdefault("G7 per-chirp metadata", [])
    g += judge_polls(side)
    g += A.judge_irq(post_reads)
    h, tot = lag_text(r)
    g.append(("2.6.5 satRefLag over %d SAT_VALID records: %s; all in {0,1,2}" % (tot, h), set(r["lag_hist"]) <= {0, 1, 2} and tot > 0))
    sv0 = r["sat_valid0"]
    g.append(("2.6.5 records with SAT_VALID = 0: %d (first at k = %s; expected only k = 0, maybe 1)" % (
        len(sv0), [int(x) for x in sv0[:5]]), True))
    g.append(("2.6.8 checks 1-4: %s" % "; ".join("%d %s" % (c.num, c.status.split(" (")[0]) for c in r["checks"]),
              r["accepted"]))
    bchecks, regime, rtext = A.judge_bytes(r)
    g += bchecks
    out("2.6.4 " + rtext)
    return G, regime


def emit_groups(G, out=print, final=True):
    allok = True
    for name, checks in G.items():
        ok = all(c[1] for c in checks)
        allok &= ok
        for text, good in checks:
            if not good:
                out("  FAIL  " + text)
        out("RESULT %s: %s  (%d/%d)" % (name, "PASS" if ok else "FAIL", sum(1 for c in checks if c[1]), len(checks)))
    if final:
        out("RESULT: %s" % ("PASS" if allok else "FAIL"))
    return allok


def report_block(r, ps, cfg, out=print):
    din, dbd = r["d_in"] * 1e6, r["d_bd"] * 1e6
    out("setting: gain %s dB, HPF %s + %s kHz; %d chirps (%.1f frames), %d datagrams, %.1f MB" % (
        cfg["rx_gain"], cfg["hpf1_khz"], cfg["hpf2_khz"], r["n_chirps"], r["n_chirps"] / cfg["nchirps"],
        r["info"]["datagrams"], r["info"]["end"] / 1e6))
    if len(din) and len(dbd):
        out("dt in-frame %.2f us, boundary %.2f us (cfg %.0f / %.0f)" % (din.mean(), dbd.mean(), cfg["tc_s"] * 1e6,
                                                                       (cfg["tc_s"] + cfg["tb_s"]) * 1e6))
    if ps:
        out("boundary steps: phase %+.4f rad (in-frame p99 %.4f), amplitude %+.3f dB (in-frame p99 %.3f)" % (
            ps["phi"]["step"], ps["phi"]["p99_in"], ps["amp"]["step"], ps["amp"]["p99_in"]))
    h, _ = lag_text(r)
    out("satRefLag: %s; other slot (in-frame): %s" % (h, {"k%+d" % d: v for d, v in sorted(r["regime_in"].items())}))


def self_check(e, r):
    """The streaming analyzer against sar_parse.analyze on the same capture (n chirps, valid, clipped, dt, noise)."""
    import numpy as np
    res = e["res"]
    pairs = [("chirps", e["n"], r["n_chirps"]), ("valid records", sum(res["valid"]), int(r["valid"].sum())),
             ("ADC partly lost", e["partial"], int((~r["adc_ok"]).sum())), ("saturated", e["sat"], int((r["sat_lag"] > 0).sum()))]
    bad = [("%s: parser %s vs stream %s" % x) for x in pairs if x[1] != x[2]]
    if res["d_in"] and len(r["d_in"]) and abs(np.mean(res["d_in"]) - r["d_in"].mean()) > 1e-9:
        bad.append("in-frame dt mean differs")
    if res["d_bd"] and len(r["d_bd"]) and abs(np.mean(res["d_bd"]) - r["d_bd"].mean()) > 1e-9:
        bad.append("boundary dt mean differs")
    return bad


def precheck(dev, lines, cfgp, cfg, a, adc_bits, out=print):
    """30 s capture with the real tools (sar_parse + sar_tune_report) and the streaming analyzer on the same file."""
    import bench_stream as bs
    import sar_tune_report as rep
    out("=== pre-check: %g s capture through sar_parse + sar_tune_report, then the streaming analyzer on the same file ===" % a.precheck_s)
    if not A.configure_show(dev, lines, out):
        return [("pre-check: cfg accepted", False)]
    cap = os.path.join(WORKDIR, "endurance_pre.cap")
    _, e = R.run_capture(dev, cap, cfgp, a.precheck_s, ["--timer-s", str(a.timer_s)])
    if e is None:
        return [("pre-check: capture ran", False)]
    R.block("pre-check", e, out)
    checks = [("pre-check capture accepted by sar_parse checks 1-4, 0 gaps, 0 missing", R.capture_ok(e))]
    if e["accepted"]:
        prefix = os.path.join(WORKDIR, "endurance_pre")
        sar_parse.write_outputs(e["res"], prefix)
        rr = rep.analyze_run(prefix, cfg, adc_bits=adc_bits, reflector_range=a.range)
        rep.plot(rr, prefix + "_tune.png")
        for l in rep.render(rr).splitlines()[1:]:
            out("  " + l)
        out("  image: %s_tune.png" % prefix)
        r = bs.analyze_stream(cap, cfg, e["side"], adc_bits=adc_bits, reflector_range=a.range)
        bad = self_check(e, r)
        if abs(r["noise_db"] - rr["noise_db"]) > 0.05:
            bad.append("noise floor %.2f vs %.2f dB" % (r["noise_db"], rr["noise_db"]))
        if len(r["adc_clip"].nonzero()[0]) != len(rr["adc_clip"]):
            bad.append("ADC-clipped %d vs %d" % (len(r["adc_clip"].nonzero()[0]), len(rr["clipped"])))
        checks.append(("streaming analyzer = sar_parse + sar_tune_report on the pre-check%s" % ((": " + "; ".join(bad)) if bad else ""), not bad))
    return checks


def cmd_endurance(dev, a, out=print):
    adc_bits = resolve_adc_bits(a.adc_bits, out)
    os.makedirs(WORKDIR, exist_ok=True)
    lines = tuned_lines(a)
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "endurance.cfg"))
    with open(cfgp) as fh:
        text = fh.read()
    cfg = common.cfg_params(text)
    errs = cfg_errors(text)
    if errs:
        out("RESULT: FAIL (cfg check: %s)" % errs[0])
        return False
    out("endurance: %g s, gain %d dB, HPF %s, reflector %.2f m (expected bin %.1f), timer-s %d, ADC bits %d, DCA1000 %s" % (
        a.duration, a.gain, a.hpf, a.range, expected_bin(cfg, a.range), a.timer_s, adc_bits, "factory 192.168.33.x"))
    pre = []
    if a.precheck_s > 0:
        pre = precheck(dev, lines, cfgp, cfg, a, adc_bits, out)
        if not all(ok for _, ok in pre):
            return bc.summarize(pre, out)
    return run_long_and_judge(dev, lines, cfgp, cfg, a, adc_bits, "endurance", pre, out)


def cfg_errors(text):
    """sar_cfg_check errors of a cfg text (the same options sar_tune_sweep uses)."""
    import sar_cfg_check as chk
    import sar_tune_sweep as sw
    return chk.analyze(text, sw.chk_opts()).errors


def run_long_and_judge(dev, lines, cfgp, cfg, a, adc_bits, name, pre, out):
    import bench_stream as bs
    if not A.configure_show(dev, lines, out):
        out("RESULT: FAIL (cfg not accepted)")
        return False
    cap = os.path.join(WORKDIR, name + ".cap")
    out("=== %s: capture %g s, sarStats every %g s (the DCA1000 timer and the disk are exercised: ~%.1f GB) ===" % (
        name, a.duration, a.poll_s, a.duration * 6.66e6 / 1e9))
    t0 = time.time()
    side = polled_capture(dev, cap, a.duration, a.timer_s, a.poll_s, out)
    out("capture done in %.0f s; analysing (streaming, ~1-2 min)" % (time.time() - t0))
    prog = lambda f: out("  analysed %d%%" % round(100 * f))      # noqa: E731
    r = bs.analyze_stream(cap, cfg, side, adc_bits=adc_bits, reflector_range=a.range, progress=prog)
    ps = bs.phase_steps(r)
    post = post_stop_reads(dev, side, a.reads, a.gap, out)
    A.configure_show(dev, lines, out)                                          # cfg for the next start: still no sensorStart
    post.append(("before next sensorStart (cfg re-sent)", A.parse_stats(A.read_stats(dev))))
    report_block(r, ps, cfg, out)
    for label, s in post:
        out("  sarStats %-38s chirps %s chirpStartIsr %s chirpAvail %s late %s missed %s fResync %s aResync %s sat %s" % (
            label, s.get("chirps"), s.get("chirpStartIsr"), s.get("chirpAvail"), s.get("lateIsr"), s.get("missedChirpIsr"),
            s.get("frameResync"), s.get("availResync"), s.get("saturatedChirps")))
    G, regime = judge_long(r, side, cfg, a, a.duration, post, ps, out)
    if pre:
        G["pre-check"] = pre
    ok = emit_groups(G, out, final=False)
    steps = [c for key in ("G3 phase continuity", "G6 APLL/SYNTH boundary") for c in G.get(key, []) if not c[1]]
    if G.get("G6 APLL/SYNTH boundary") and not all(c[1] for c in G["G6 APLL/SYNTH boundary"]):
        out("G6 follow-up (APLL/SYNTH, Step 2.5): steps or boundary anomalies at Tb = 300 us. Repeat with a longer blank: "
            "./bench tb --add-us 100 --range %s --gain %s --hpf %s [--adc-bits %d], then 200 ... up to 500; record the minimum clean Tb." % (
                a.range, a.gain, a.hpf, adc_bits))
    elif G.get("G6 APLL/SYNTH boundary"):
        out("G6: no boundary step beyond the in-frame band: Tb = %.0f us is clean (record it)." % (cfg["tb_s"] * 1e6))
    out("capture kept: %s (%.1f GB); regime %s" % (cap, r["info"]["end"] / 1e9, regime))
    out("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return ok


# --- tb ---------------------------------------------------------------------------------------------------------------
def cmd_tb(dev, a, out=print):
    adc_bits = resolve_adc_bits(a.adc_bits, out)
    os.makedirs(WORKDIR, exist_ok=True)
    lines = A.with_frame_period(tuned_lines(a), a.add_us)
    cfgp = R.write_cfg(lines + ["sensorStart"], os.path.join(WORKDIR, "tb_%d.cfg" % a.add_us))
    with open(cfgp) as fh:
        text = fh.read()
    cfg = common.cfg_params(text)
    errs = cfg_errors(text)
    out("Tb %.0f us (= 300 + %d), frame period %.1f ms; sar_cfg_check: %s" % (
        cfg["tb_us"], a.add_us, (cfg["nchirps"] * cfg["tc_s"] + cfg["tb_s"]) * 1e3,
        ("ERROR " + errs[0]) if errs else "no errors"))
    if errs:
        out("RESULT: FAIL (cfg check)")
        return False
    return run_tb(dev, lines, cfgp, cfg, a, adc_bits, out)


def run_tb(dev, lines, cfgp, cfg, a, adc_bits, out):
    import bench_stream as bs
    if not A.configure_show(dev, lines, out):
        out("RESULT: FAIL (cfg not accepted)")
        return False
    cap = os.path.join(WORKDIR, "tb_%d.cap" % a.add_us)
    side = A.capture_only(dev, cap, cfgp, a.duration, extra=["--timer-s", str(a.timer_s)], out=out)
    if side is None:
        out("RESULT: FAIL (capture tool failed)")
        return False
    r = bs.analyze_stream(cap, cfg, side, adc_bits=adc_bits, reflector_range=a.range)
    ps = bs.phase_steps(r)
    report_block(r, ps, cfg, out)
    G, _ = judge_long(r, side, cfg, a, a.duration, [("after sensorStop", A.parse_stats(side["raw"]))] * 1, ps, out)
    keep = {k: G[k] for k in ("G2 timing", "G3 phase continuity", "G6 APLL/SYNTH boundary")}
    keep["G2 timing"] = keep["G2 timing"][:3]
    ok = emit_groups(keep, out, final=False)
    out("Tb %.0f us: %s" % (cfg["tb_us"], "CLEAN (no boundary step or dt anomaly)" if ok else "STEPS SEEN (this Tb is not clean)"))
    out("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return ok


# --- registration ---------------------------------------------------------------------------------------------------
def register(sub):
    cfg = R.DEFAULT_CFG
    p = sub.add_parser("tune", help="2.3 gain x HPF sweep with the reflector at 1-2 m")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--range", type=float, default=1.5, help="reflector range, m (default 1.5)")
    p.add_argument("--gains", default="24,30,36,42", help="rxGain list, dB, even 24-48 (default %(default)s)")
    p.add_argument("--hpf", default="175:350,350:700", help="HPF1:HPF2 pairs, kHz (default %(default)s)")
    p.add_argument("--duration", type=float, default=5.0)
    p.add_argument("--adc-bits", type=int)
    p = sub.add_parser("sat", help="2.6.6 saturation vs rxGain, >= 5 steps up to clipping, >= 30 s each")
    p.add_argument("--cfg", default=cfg)
    p.add_argument("--range", type=float, default=1.5, help="reflector range, m, fixed for the sweep (default 1.5)")
    p.add_argument("--gain", type=int, required=True, help="the tuned rxGain, dB")
    p.add_argument("--hpf", default="175:350", help="the tuned HPF1:HPF2, kHz (default %(default)s)")
    p.add_argument("--gains", help="explicit gain list instead of the ladder (must contain --gain)")
    p.add_argument("--points", type=int, default=5)
    p.add_argument("--duration", type=float, default=30.0)
    p.add_argument("--adc-bits", type=int)
    for name, hlp in (("endurance", "10 min run: G1-G4, G6, G7"), ("tb", "boundary check with Tb + N us")):
        p = sub.add_parser(name, help=hlp)
        p.add_argument("--cfg", default=cfg)
        p.add_argument("--range", type=float, required=True, help="reflector range, m (measured)")
        p.add_argument("--gain", type=int, required=True, help="the tuned rxGain, dB")
        p.add_argument("--hpf", default="175:350", help="the tuned HPF1:HPF2, kHz (default %(default)s)")
        p.add_argument("--timer-s", type=int, default=30)
        p.add_argument("--adc-bits", type=int)
        if name == "endurance":
            p.add_argument("--duration", type=float, default=600.0)
            p.add_argument("--poll-s", type=float, default=60.0)
            p.add_argument("--precheck-s", type=float, default=30.0, help="0 skips the 30 s tool cross-check")
            p.add_argument("--reads", type=int, default=2)
            p.add_argument("--gap", type=float, default=4.0)
        else:
            p.add_argument("--add-us", type=int, required=True, help="increase Tb by this many microseconds (100..500)")
            p.add_argument("--duration", type=float, default=60.0)


COMMANDS = {"tune": cmd_tune, "sat": cmd_sat, "endurance": cmd_endurance, "tb": cmd_tb}
