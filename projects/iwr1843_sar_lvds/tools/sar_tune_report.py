#!/usr/bin/env python3
"""Tuning report for an iwr1843_sar_lvds capture: peak level, clipped chirps, noise floor, range profile, HPF response.

    uv run --group tools python sar_tune_report.py PREFIX --cfg CFG [--reflector-range M] [--noise-range A B]
                                                  [--adc-bits 12] [--rx N] [--png FILE] [--no-png]

PREFIX is the sar_parse.py output prefix (reads PREFIX_adc.bin and PREFIX_meta.csv from an ACCEPTED capture; a
capture written with --force is refused unless --allow-forced). Needs numpy and matplotlib: the optional uv group
`tools` in firmware_dev/pyproject.toml (`uv sync --group tools`). No hardware access.

What it prints (docs/tuning_guide.md explains each line):
  * peak |I|,|Q| per chirp in dBFS (full scale = 2^(adc-bits-1) counts, default 12-bit ADC = 2048);
  * clipped chirps: ADC full-scale hits in the data, and the firmware saturation count (satSlices), whose result
    arrives one chirp late and is aligned here with its lag (docs/lvds_data_format.md section 3);
  * noise floor: median level of the mean range profile over a quiet far-range region (default: the last fifth of
    the usable bins, 0.8 x Ns);
  * the mean range profile (image) with the theoretical two-stage HPF response overlaid (two first-order
    high-pass filters, datasheet SWRS228B 7.7; same formula as sar_cfg_check.py);
  * SNR of a reflector at --reflector-range, if given.
"""
import argparse
import csv
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_common as common  # noqa: E402

C = 299792458.0


def load_meta(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def hpf_gain_db(f_hz, hpf1_khz, hpf2_khz):
    """Two cascaded first-order high-pass filters: sum of -10 log10(1 + (fc/f)^2), in dB (<= 0)."""
    return -sum(10 * math.log10(1 + (fc * 1e3 / f_hz) ** 2) for fc in (hpf1_khz, hpf2_khz))


def analyze_run(prefix, cfg, adc_bits=12, rx=0, reflector_range=None, reflector_halfwidth=0.25, noise_range=None,
                allow_forced=False):
    """Compute the report values. Returns a dict (see render())."""
    import numpy as np
    if os.path.exists(prefix + "_FORCED_REJECTED.txt") and not allow_forced:
        raise ValueError("%s was written with --force from a REJECTED capture; re-capture, or pass --allow-forced "
                         "to look at it anyway" % prefix)
    nrx, ns = cfg["nrx"], cfg["ns"]
    meta = load_meta(prefix + "_meta.csv")
    n = len(meta)
    adc = np.fromfile(prefix + "_adc.bin", dtype=np.int16)
    if adc.size != n * nrx * ns * 2:
        raise ValueError("%s_adc.bin has %d samples, expected %d chirps x %d RX x %d x 2 (wrong --cfg?)" % (
            prefix, adc.size, n, nrx, ns))
    adc = adc.reshape(n, nrx, ns, 2)[:, rx, :, :].astype(np.float64)
    complete = np.array([r["adc_complete"] == "1" for r in meta])
    sat = np.array([int(r["sat_slices_this_chirp"]) if r["sat_slices_this_chirp"] != "" else -1 for r in meta])
    fs_counts = float(2 ** (adc_bits - 1))

    peak = np.abs(adc).max(axis=(1, 2))
    peak_db = 20 * np.log10(np.maximum(peak, 1) / fs_counts)
    ok = complete
    adc_clip = ok & (peak >= fs_counts - 1)
    fw_sat = ok & (sat > 0)
    fw_unknown = ok & (sat < 0)
    union = adc_clip | fw_sat

    # range profile: Hann window, complex FFT of I + jQ, amplitude normalised to full scale (a full-scale complex
    # tone reads 0 dB), mean over chirps in power
    w = np.hanning(ns)
    z = (adc[ok, :, 0] + 1j * adc[ok, :, 1]) * w
    spec = np.fft.fft(z, axis=1) / (w.sum() * fs_counts)
    power = (np.abs(spec) ** 2).mean(axis=0)
    prof_db = 10 * np.log10(np.maximum(power, 1e-30))
    slope = abs(cfg["slope_mhz_us"]) * 1e12                      # Hz/s, realized slope
    fs = cfg["fs_ksps"] * 1e3
    nuse = int(0.8 * ns)                                        # usable IF: 0.8 fs, as sar_cfg_check.py
    f_bin = np.arange(ns) * fs / ns
    rng = f_bin * C / (2 * slope)
    if noise_range:
        sel = (rng >= noise_range[0]) & (rng <= noise_range[1])
        sel[nuse:] = False
    else:
        sel = np.zeros(ns, bool)
        sel[int(0.8 * nuse):nuse] = True
    noise_db = float(np.median(prof_db[sel])) if sel.any() else float("nan")
    out = dict(prefix=prefix, n=n, n_complete=int(ok.sum()), fs_counts=fs_counts, adc_bits=adc_bits,
               peak_db=peak_db, peak_max_db=float(peak_db[ok].max()) if ok.any() else float("nan"),
               peak_median_db=float(np.median(peak_db[ok])) if ok.any() else float("nan"),
               peak_p99_db=float(np.percentile(peak_db[ok], 99)) if ok.any() else float("nan"),
               over_fs=int((peak[ok] > fs_counts).sum()),
               adc_clip=np.flatnonzero(adc_clip).tolist(), fw_sat=np.flatnonzero(fw_sat).tolist(),
               clipped=np.flatnonzero(union).tolist(), fw_unknown=int(fw_unknown.sum()),
               noise_db=noise_db, noise_region_m=(float(rng[sel].min()), float(rng[sel].max())) if sel.any() else None,
               range_m=rng[:nuse], profile_db=prof_db[:nuse], cfg=cfg, reflector=None, sat=sat, complete=complete)
    out["reflector_asked"] = reflector_range is not None
    if reflector_range is not None:
        win = (rng[:nuse] >= reflector_range - reflector_halfwidth) & (rng[:nuse] <= reflector_range + reflector_halfwidth)
        if win.any():
            i = int(np.argmax(np.where(win, prof_db[:nuse], -1e9)))
            out["reflector"] = dict(range_m=float(rng[i]), level_db=float(prof_db[i]),
                                    snr_db=float(prof_db[i] - noise_db), target_m=reflector_range)
    return out


def render(r):
    cfg = r["cfg"]
    L = ["tuning report: %s (%d chirps, %d with complete ADC data; full scale = %d counts = %d-bit ADC)" % (
        r["prefix"], r["n"], r["n_complete"], r["fs_counts"], r["adc_bits"])]
    L.append("setting: gain %s dB, HPF %s + %s kHz, fs %s ksps, Ns %d" % (cfg["rx_gain"], cfg["hpf1_khz"],
                                                                         cfg["hpf2_khz"], cfg["fs_ksps"], cfg["ns"]))
    L.append("peak |I|,|Q| per chirp (dBFS): max %.1f, p99 %.1f, median %.1f" % (
        r["peak_max_db"], r["peak_p99_db"], r["peak_median_db"]))
    if r["over_fs"]:
        L.append("WARNING: %d chirp(s) exceed full scale %d: wrong --adc-bits? (clip detection assumes it)" % (
            r["over_fs"], r["fs_counts"]))
    L.append("clipped chirps: %d  (ADC full-scale hit: %d; firmware saturation satSlices > 0: %d; both counted once)" % (
        len(r["clipped"]), len(r["adc_clip"]), len(r["fw_sat"])))
    L.append("  firmware result unknown for %d chirp(s) (the run's last chirp and any whose record was lost: the "
             "saturation field arrives one chirp late, lag applied)" % r["fw_unknown"])
    if r["clipped"]:
        L.append("  first clipped chirps: %s" % ", ".join(str(k) for k in r["clipped"][:10]))
    nz = r["noise_region_m"]
    L.append("noise floor: %.1f dBFS per range bin (median over %s)" % (
        r["noise_db"], "%.1f-%.1f m" % nz if nz else "no bins"))
    import numpy as np
    i = int(np.argmax(r["profile_db"][1:])) + 1
    L.append("mean range profile: strongest bin %.1f dBFS at %.2f m (%.1f dB above the noise floor)" % (
        r["profile_db"][i], r["range_m"][i], r["profile_db"][i] - r["noise_db"]))
    if cfg.get("hpf"):
        h = cfg["hpf"]
        L.append("HPF: corners at %.1f m (%s kHz) and %.1f m (%s kHz); attenuation at 1/2/5/10/20/50 m: %s dB" % (
            h["corner_range_m"][str(cfg["hpf1_khz"])], cfg["hpf1_khz"], h["corner_range_m"][str(cfg["hpf2_khz"])],
            cfg["hpf2_khz"], " / ".join("%.0f" % h["attenuation_db"][k] for k in ("1", "2", "5", "10", "20", "50"))))
    if r["reflector"]:
        f = r["reflector"]
        L.append("reflector near %.2f m: peak %.1f dBFS at %.2f m, SNR %.1f dB over the noise floor" % (
            f["target_m"], f["level_db"], f["range_m"], f["snr_db"]))
    elif r.get("reflector_asked"):
        L.append("reflector SNR: no bins within the search window")
    else:
        L.append("reflector SNR: not requested (--reflector-range M)")
    return "\n".join(L)


def plot(r, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
    cfg = r["cfg"]
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 7))
    rng = r["range_m"][1:]
    a1.plot(rng, r["profile_db"][1:], color="#1f77b4", label="mean range profile (dBFS)")
    a1.axhline(r["noise_db"], color="#555", ls=":", label="noise floor %.1f dBFS" % r["noise_db"])
    a1.set_xlabel("range (m)")
    a1.set_ylabel("level (dBFS per bin)")
    a1.grid(alpha=0.3)
    a3 = a1.twinx()
    slope = abs(cfg["slope_mhz_us"]) * 1e12
    f = 2 * slope * rng / C
    a3.plot(rng, [hpf_gain_db(x, cfg["hpf1_khz"], cfg["hpf2_khz"]) for x in f], color="#d62728", ls="--",
            label="HPF response %s+%s kHz (dB, right axis)" % (cfg["hpf1_khz"], cfg["hpf2_khz"]))
    a3.set_ylabel("HPF gain (dB)")
    h1, l1 = a1.get_legend_handles_labels()
    h2, l2 = a3.get_legend_handles_labels()
    a1.legend(h1 + h2, l1 + l2, loc="lower right", fontsize=8)
    a1.set_title("range profile and HPF response (gain %s dB)" % cfg["rx_gain"])
    k = np.arange(r["n"])
    a2.plot(k, r["peak_db"], lw=0.8, color="#1f77b4", label="peak |I|,|Q| per chirp")
    if r["clipped"]:
        a2.plot(r["clipped"], r["peak_db"][r["clipped"]], "rv", label="clipped chirps (%d)" % len(r["clipped"]))
    a2.axhline(0, color="#888", lw=0.5)
    a2.set_xlabel("chirp index")
    a2.set_ylabel("peak (dBFS)")
    a2.legend(fontsize=8)
    a2.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog="Read docs/tuning_guide.md.")
    ap.add_argument("prefix", help="sar_parse.py output prefix (PREFIX_adc.bin, PREFIX_meta.csv)")
    ap.add_argument("--cfg", required=True, help="the cfg the run used")
    ap.add_argument("--reflector-range", type=float, help="range of a known reflector, m (prints its SNR)")
    ap.add_argument("--reflector-halfwidth", type=float, default=0.25, help="search +- this many m (default 0.25)")
    ap.add_argument("--noise-range", type=float, nargs=2, metavar=("A", "B"),
                    help="quiet range region for the noise floor, m (default: last fifth of the usable bins)")
    ap.add_argument("--adc-bits", type=int, default=12, help="ADC bits for full scale (default 12: 2048 counts)")
    ap.add_argument("--rx", type=int, default=0, help="RX channel index to analyse (default 0)")
    ap.add_argument("--png", help="image path (default PREFIX_tune.png)")
    ap.add_argument("--no-png", action="store_true", help="text only")
    ap.add_argument("--allow-forced", action="store_true", help="analyse a --force output anyway (debugging)")
    return ap


def main(argv=None):
    opt = build_parser().parse_args(argv)
    try:
        import numpy  # noqa: F401
        if not opt.no_png:
            import matplotlib  # noqa: F401
    except ImportError as exc:
        print("error: %s. Install the optional group: uv sync --group tools (or uv run --group tools ...)" % exc,
              file=sys.stderr)
        return 2
    try:
        with open(opt.cfg) as fh:
            cfg = common.cfg_params(fh.read())
        r = analyze_run(opt.prefix, cfg, opt.adc_bits, opt.rx, opt.reflector_range, opt.reflector_halfwidth,
                        opt.noise_range, opt.allow_forced)
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    print(render(r))
    if not opt.no_png:
        png = opt.png or opt.prefix + "_tune.png"
        plot(r, png)
        print("image: %s" % png)
    return 0


if __name__ == "__main__":
    sys.exit(main())
