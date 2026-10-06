#!/usr/bin/env python3
"""Bounded-memory analysis of a long dataFmt 2 capture (the 10 min endurance run is ~4 GB; sar_parse.analyze holds
the whole run, the stream and its mask in RAM, and sar_tune_report converts the ADC to float64: ~20+ GB).

    from bench_stream import analyze_stream
    r = analyze_stream(capture_path, cfg, sarstats, adc_bits=12, reflector_range=3.0)

Reads the SARCAP1 file once, sequentially, keeping a window of ~100 packets. Per packet it does what sar_parse does
(place by DCA1000 byte count, unscramble, validate slot k mod 2, checks 1-4 of docs/lvds_data_format.md section 1) plus
what the bench needs: raw byte-order counts, the other-slot regime, the satRefLag histogram, timestamp deltas, and
from the ADC data (rx 0) the per-chirp peak, the mean range profile and the complex value of the reflector's range bin
(phase / amplitude steps at frame boundaries). tests (test_bench_b.py) compare it with sar_parse.analyze. numpy needed.
No hardware access.
"""
import os
import struct
import sys
from collections import Counter, deque

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sar_common as common  # noqa: E402
import sar_parse as sp  # noqa: E402

C = 299792458.0
HSI_RAW = bytes.fromhex("DC0ADC0ADA0CDA0C")      # lvds_data_format.md [1]: raw (DCA1000) order
HSI_DEV = bytes.fromhex("DC0ADA0CDC0ADA0C")      # after the u0 u2 u1 u3 unscramble
MAGIC_RAW6 = b"SA\x01\x00RM"                       # bytes 0-5 of a record, raw order: "SA" 01 00 "RM"
MAGIC_DEV = b"SARM"


def iter_datagrams(path):
    """Yield every datagram (10-byte DCA1000 header + payload) of a SARCAP1 file, reading sequentially."""
    with open(path, "rb", buffering=1 << 20) as fh:
        if fh.read(len(common.CAP_MAGIC)) != common.CAP_MAGIC:
            raise ValueError("%s: not a SARCAP1 capture" % path)
        while True:
            h = fh.read(2)
            if len(h) < 2:
                return
            n = struct.unpack("<H", h)[0]
            d = fh.read(n)
            if len(d) < n:
                return
            yield d


def _rec(ub, um, off):
    """Record tuple at `off` in the unscrambled window, None if any of its 32 bytes is missing."""
    if off + 32 > len(ub) or um[off:off + 32].count(1) != 32:
        return None
    return sp.REC.unpack_from(ub, off)


def analyze_stream(path, cfg, sarstats=None, adc_bits=12, reflector_range=None, batch=64, slack=32, progress=None):
    """Returns a dict; keys documented where used (bench_b.py). `sarstats` as in sar_parse.analyze."""
    B, M, H, ns, nrx, nc = cfg["B"], cfg["M"], cfg["H"], cfg["ns"], cfg["nrx"], cfg["nchirps"]
    avail = sarstats["chirpAvail"] if sarstats else None
    run_idx = sarstats.get("runIdx") if sarstats else None
    fs_counts = float(2 ** (adc_bits - 1))
    hdr_on = cfg["hdr_on"]
    adc_bytes = 4 * nrx * ns
    win = np.hanning(ns)
    slope = abs(cfg["slope_mhz_us"]) * 1e12
    fs = cfg["fs_ksps"] * 1e3
    nuse = int(0.8 * ns)
    rng_of_bin = np.arange(ns) * fs / ns * C / (2 * slope)

    buf, msk, base_k = bytearray(), bytearray(), 0
    seen = bytearray()
    seq_origin = [None]
    info = dict(datagrams=0, duplicates=0, overlaps=0, restarts=0, stale=0, max_payload=0, end=0, seq_min=None,
                seq_max=None, unique=0)
    # per-packet lists
    L = {n: [] for n in ("absent", "adc", "valid", "ts", "flags", "sat", "lag", "late_any")}
    peak_chunks, z_chunks = [], []
    cnt = dict(n_eval=0, n_valid=0, n_other_run=0, hsi_raw_eval=0, hsi_raw_ok=0, hsi_dev_ok=0, own_raw_eval=0,
               own_raw_ok=0, other_raw_eval=0, other_raw_ok=0, own_dev_ok=0, own_dev_eval=0)
    first_valid_run = [None]
    regime_in, regime_last = Counter(), Counter()
    other_unreadable = [0]
    tail = deque(maxlen=8)
    pow_sum = np.zeros(ns)
    n_pow = [0]
    blk = {"sum": np.zeros(ns), "n": 0}
    blocks = []                      # (first chirp index, summed power, n)
    blk_first = [None]
    state = {"bref": None}
    ref_win = None
    if reflector_range is not None:
        ref_win = np.abs(rng_of_bin - reflector_range) <= 0.5
        ref_win[:3] = False
        ref_win[nuse:] = False

    def process(n):
        nonlocal buf, msk, base_k, run_idx
        if len(buf) < n * B:
            buf.extend(bytes(n * B - len(buf)))
            msk.extend(bytes(n * B - len(msk)))
        ub, um = bytearray(buf[:n * B]), bytearray(msk[:n * B])
        raw = buf                      # raw byte-order counts use the window before the unscramble
        comp = np.zeros(n, bool)
        for j in range(n):
            k = base_k + j
            o = j * B
            s_own = k % 2
            if hdr_on:
                if msk[o:o + 8].count(1) == 8:
                    cnt["hsi_raw_eval"] += 1
                    cnt["hsi_raw_ok"] += raw[o:o + 8] == HSI_RAW
            oo = o + M + 32 * s_own
            if msk[oo:oo + 6].count(1) == 6:
                cnt["own_raw_eval"] += 1
                cnt["own_raw_ok"] += raw[oo:oo + 6] == MAGIC_RAW6
            ot = o + M + 32 * (1 - s_own)
            if k > 0 and msk[ot:ot + 6].count(1) == 6:
                cnt["other_raw_eval"] += 1
                cnt["other_raw_ok"] += raw[ot:ot + 6] == MAGIC_RAW6
        sp.unscramble(ub)
        sp.unscramble(um)
        for j in range(n):
            k = base_k + j
            o = j * B
            s_own = k % 2
            present = um[o:o + B].count(1)
            L["absent"].append(present == 0)
            adc_ok = um[o + H:o + H + adc_bytes].count(1) == adc_bytes
            L["adc"].append(adc_ok)
            comp[j] = adc_ok
            if hdr_on and um[o:o + 8].count(1) == 8:
                cnt["hsi_dev_ok"] += ub[o:o + 8] == HSI_DEV
            own = _rec(ub, um, o + M + 32 * s_own)
            other = _rec(ub, um, o + M + 32 * (1 - s_own))
            tail.append((k, other))
            if own is not None:
                cnt["own_dev_eval"] += 1
                cnt["own_dev_ok"] += own[0] == sp.MAGIC
            looks = own is not None and own[0] == sp.MAGIC and own[1] == 1 and own[6] == (k & 0xFFFFFFFF)
            if looks and first_valid_run[0] is None:
                first_valid_run[0] = own[7]
            if run_idx is None and first_valid_run[0] is not None:
                run_idx = first_valid_run[0]
            if own is not None:
                cnt["n_eval"] += 1
                if own[0] == sp.MAGIC and run_idx is not None and own[7] != run_idx:
                    cnt["n_other_run"] += 1
                elif looks and own[7] == run_idx:
                    cnt["n_valid"] += 1
            ok = looks and own[7] == run_idx
            L["valid"].append(ok)
            if ok:
                L["ts"].append(own[10])
                L["flags"].append(own[2])
                L["sat"].append(own[8])
                L["lag"].append(own[9])
            else:
                L["ts"].append(0)
                L["flags"].append(0)
                L["sat"].append(0)
                L["lag"].append(0)
            # other-slot regime (which chirp the other slot holds)
            if k > 0:
                if other is not None and other[0] == sp.MAGIC and run_idx is not None and other[7] == run_idx:
                    d = (other[6] - k + 2 ** 31) % 2 ** 32 - 2 ** 31
                    (regime_last if k % nc == nc - 1 else regime_in)[d] += 1
                else:
                    other_unreadable[0] += 1
        # ADC: peak, range profile, reflector bin
        arr = np.ndarray(shape=(n, 2 * ns * nrx), dtype="<i2", buffer=ub, offset=H, strides=(B, 2))[:, :2 * ns]
        x = arr.astype(np.float64)
        a, b = x[:, 0::2], x[:, 1::2]
        i_, q_ = (b, a) if cfg["swap"] == 1 else (a, b)
        peak = np.maximum(np.abs(i_).max(axis=1), np.abs(q_).max(axis=1))
        peak_chunks.append(np.where(comp, peak, -1.0))
        zc = np.full(n, np.nan + 0j)
        if comp.any():
            z = (i_[comp] + 1j * q_[comp]) * win
            spec = np.fft.fft(z, axis=1) / (win.sum() * fs_counts)
            pw = (spec.real ** 2 + spec.imag ** 2)
            if state["bref"] is None:
                mean = pw.mean(axis=0)
                if ref_win is not None and ref_win.any():
                    state["bref"] = int(np.argmax(np.where(ref_win, mean, -1.0)))
                else:
                    m2 = mean.copy()
                    m2[:3] = -1
                    m2[nuse:] = -1
                    state["bref"] = int(np.argmax(m2))
            zc[comp] = spec[:, state["bref"]]
            s = pw.sum(axis=0)
            pow_sum[:] += s
            n_pow[0] += int(comp.sum())
            blk["sum"] += s
            blk["n"] += int(comp.sum())
            if blk_first[0] is None:
                blk_first[0] = base_k
            if blk["n"] >= 1000:
                blocks.append((blk_first[0], blk["sum"].copy(), blk["n"]))
                blk["sum"][:] = 0
                blk["n"] = 0
                blk_first[0] = None
        z_chunks.append(zc)
        del buf[:n * B]
        del msk[:n * B]
        base_k += n

    # --- main loop -------------------------------------------------------------------------------------------
    max_end = 0
    next_prog = 0.1
    for d in iter_datagrams(path):
        if len(d) <= common.UDP_HDR:
            continue
        seq, count, payload = common.split_header(d)
        info["datagrams"] += 1
        info["max_payload"] = max(info["max_payload"], len(payload))
        if info["seq_min"] is None:
            info["seq_min"] = info["seq_max"] = seq
        else:
            info["seq_min"], info["seq_max"] = min(info["seq_min"], seq), max(info["seq_max"], seq)
        if seq_origin[0] is None:
            seq_origin[0] = max(0, seq - 100000)
        idx = seq - seq_origin[0]
        if idx < 0:                         # more than 100000 below the first datagram seen: not tracked
            idx = None
        if idx is not None:
            if idx >= len(seen):
                seen.extend(bytes(idx + 1 - len(seen) + 4096))
            if seen[idx]:
                info["duplicates"] += 1
                continue
            seen[idx] = 1
            info["unique"] += 1
        if count == 0 and info["datagrams"] > 1 and max_end >= 100 * info["max_payload"]:
            info["restarts"] += 1
            continue
        a = count - base_k * B
        if a < 0:
            info["stale"] += 1
            continue
        e = a + len(payload)
        if e > len(buf):
            buf.extend(bytes(e - len(buf)))
            msk.extend(bytes(e - len(msk)))
        if 1 in msk[a:e]:
            if bytes(buf[a:e]) == payload and msk[a:e].count(1) == len(payload):
                info["duplicates"] += 1
            else:
                info["overlaps"] += 1
            continue
        buf[a:e] = payload
        msk[a:e] = b"\x01" * len(payload)
        max_end = max(max_end, count + len(payload))
        while max_end - base_k * B >= (batch + slack) * B:
            process(batch)
        if progress and avail and max_end >= next_prog * avail * B:
            progress(next_prog)
            next_prog += 0.1
    info["end"] = max_end
    n_scan = max(avail if avail is not None else 0, -(-max_end // B))
    while base_k < n_scan:
        process(min(batch, n_scan - base_k))
    if blk["n"]:
        blocks.append((blk_first[0], blk["sum"].copy(), blk["n"]))

    # --- results ---------------------------------------------------------------------------------------------
    n_chirps = avail if avail is not None else -(-max_end // B)
    valid = np.array(L["valid"], bool)[:n_chirps]
    ts = np.array(L["ts"], dtype=np.int64)[:n_chirps]
    flags = np.array(L["flags"], dtype=np.int64)[:n_chirps]
    satv = np.array(L["sat"], dtype=np.int64)[:n_chirps]
    lag = np.array(L["lag"], dtype=np.int64)[:n_chirps]
    adc_ok = np.array(L["adc"], bool)[:n_chirps]
    absent = int(np.array(L["absent"], bool)[:n_chirps].sum())
    peak = np.concatenate(peak_chunks)[:n_chirps] if peak_chunks else np.zeros(0)
    zref = np.concatenate(z_chunks)[:n_chirps] if z_chunks else np.zeros(0, complex)
    kk = np.arange(n_chirps)
    sat_mask = valid & ((flags & sp.FLAG_SAT_VALID) != 0)
    sat_la = np.full(n_chirps, -1, dtype=np.int64)
    tgt = kk[sat_mask] - lag[sat_mask]
    okt = (tgt >= 0) & (tgt < n_chirps)
    sat_la[tgt[okt]] = satv[sat_mask][okt]
    sat_nolag = np.where(sat_mask, satv, -1)
    adc_clip = adc_ok & (peak >= fs_counts - 1)
    lag_hist = Counter(int(v) for v in lag[sat_mask])
    sat_valid0 = np.flatnonzero(valid & ~sat_mask)
    late = int(((flags & sp.FLAG_LATE) != 0)[valid].sum())
    skip = int(((flags & 0x8) != 0)[valid].sum())
    resync = int(((flags & 0x10) != 0)[valid].sum())

    # time deltas (consecutive valid records), parser semantics
    pair = valid[1:] & valid[:-1]
    dt = (ts[1:] - ts[:-1]).astype(np.float64) / sp.TS_HZ
    bd = (kk[1:] % nc == 0)
    d_in, d_bd = dt[pair & ~bd], dt[pair & bd]

    # checks 1-4
    checks = [sp.Check(1, "byte counts consistent"), sp.Check(2, "records validate at their positions"),
              sp.Check(3, "no chirp follows the last packet"), sp.Check(4, "bytes recorded = chirpAvail x B")]
    c1, c2, c3, c4 = checks
    if info["overlaps"] or info["restarts"]:
        c1.set("FAIL", "%d overlapping datagram(s), %d restart(s) at count 0" % (info["overlaps"], info["restarts"]))
    elif info["datagrams"] == 0:
        c1.set("FAIL", "no datagrams")
    else:
        c1.set("PASS", "%d datagrams, %d duplicate(s) ignored, no overlap, no restart" % (
            info["datagrams"], info["duplicates"]))
    n_eval, n_valid = cnt["n_eval"], cnt["n_valid"]
    allowed = max(1, n_eval // 100)
    if n_eval == 0:
        c2.set("FAIL", "no packet has all its record bytes")
    elif cnt["n_other_run"]:
        c2.set("FAIL", "%d record(s) carry another runIdx than the run's (%s)" % (cnt["n_other_run"], run_idx))
    elif sarstats and sarstats.get("runIdx") is not None and first_valid_run[0] is not None \
            and first_valid_run[0] != sarstats["runIdx"]:
        c2.set("FAIL", "first valid record has runIdx %d, sarStats says %d" % (first_valid_run[0], sarstats["runIdx"]))
    elif n_valid == 0:
        c2.set("FAIL", "0 of %d records validate" % n_eval)
    elif n_eval - n_valid > allowed:
        c2.set("FAIL", "%d of %d records fail (allowed %d)" % (n_eval - n_valid, n_eval, allowed))
    else:
        c2.set("PASS", "%d of %d records validate (%d failure(s) allowed)" % (n_valid, n_eval, allowed))
    if max_end == 0:
        c3.set("NOT EVALUABLE", "no data")
    else:
        cand = (max_end - 1) // B
        if avail is not None:
            cand = min(cand, avail - 1)
        other = next((r for k_, r in tail if k_ == cand), "missing")
        if other == "missing" or other is None:
            c3.set("NOT EVALUABLE (tail hole)", "packet %d: the other slot's bytes are not all present" % cand)
        elif other[0] == sp.MAGIC and (run_idx is None or other[7] == run_idx) and other[6] == (cand + 1) & 0xFFFFFFFF:
            c3.set("FAIL", "packet %d's other slot holds chirp %d of the run: the recording starts one packet late" % (
                cand, cand + 1))
        else:
            c3.set("PASS", "packet %d's other slot does not hold chirp %d" % (cand, cand + 1))
    if avail is None:
        c4.set("FAIL", "no sarStats reading: chirpAvail unknown")
    else:
        short = avail * B - max_end
        tol = min(B - 1, info["max_payload"])
        if short == 0:
            c4.set("PASS", "%d B recorded = %d x %d" % (max_end, avail, B))
        elif 0 < short <= tol:
            c4.set("PASS", "%d B recorded, %d B short of %d x %d: tail hole, tolerated" % (max_end, short, avail, B))
        elif short > 0:
            c4.set("FAIL", "%d B recorded, expected %d x %d = %d: short by %d B" % (max_end, avail, B, avail * B, short))
        else:
            c4.set("FAIL", "%d B recorded, %d B too many" % (max_end, -short))
    flow_ok = not (sarstats and sarstats.get("frameEndTimeout"))
    typed = bool(sarstats) and sarstats.get("source") == "manual"
    c3_def = c3.status.startswith("NOT EVALUABLE (tail hole)") and c4.status == "PASS" and not typed
    failing = [c.num for c in checks if c.status != "PASS" and not (c.num == 3 and c3_def)]

    # range profile and reflector
    prof_db = 10 * np.log10(np.maximum(pow_sum / max(n_pow[0], 1), 1e-30))
    sel = np.zeros(ns, bool)
    sel[int(0.8 * nuse):nuse] = True
    noise_db = float(np.median(prof_db[sel]))
    pk = prof_db[:nuse].copy()
    pk[:3] = -1e9
    peak_bin = int(np.argmax(pk))

    def blk_peak(b):
        if not b:
            return None
        m = (b[1] / b[2]).copy()
        m[:3] = 0
        m[nuse:] = 0
        return int(np.argmax(m))
    full = [b for b in blocks if b[2] >= 500]
    first_peak_bin = blk_peak(full[0]) if full else None
    last_peak_bin = blk_peak(full[-1]) if full else None
    bref = state["bref"]
    peak_db = 20 * np.log10(np.maximum(peak[adc_ok], 1) / fs_counts) if adc_ok.any() else np.zeros(1)
    out = dict(
        path=path, cfg=cfg, sarstats=sarstats, info=info, checks=checks, failing=failing, accepted=(not failing and flow_ok),
        flow_ok=flow_ok, n_chirps=n_chirps, n_scan=n_scan, n_eval=n_eval, n_valid=n_valid, absent=absent,
        seq_gaps=(info["seq_max"] - info["seq_min"] + 1 - info["unique"]) if info["seq_min"] is not None else 0,
        valid=valid, adc_ok=adc_ok, ts=ts, flags=flags, d_in=d_in, d_bd=d_bd, late=late, skip=skip, resync=resync,
        lag_hist=dict(lag_hist), sat_valid0=sat_valid0, sat_lag=sat_la, sat_nolag=sat_nolag, adc_clip=adc_clip,
        peak=peak, fs_counts=fs_counts, adc_bits=adc_bits, peak_db=peak_db,
        regime_in=dict(regime_in), regime_last=dict(regime_last), other_unreadable=other_unreadable[0],
        counts=cnt, zref=zref, bref=bref, noise_db=noise_db, profile_db=prof_db[:nuse], range_m=rng_of_bin[:nuse],
        peak_bin=peak_bin, first_peak_bin=first_peak_bin, last_peak_bin=last_peak_bin,
        bref_db=float(prof_db[bref]) if bref is not None else None,
        bref_range=float(rng_of_bin[bref]) if bref is not None else None, run_idx=run_idx)
    return out


def phase_steps(r):
    """Frame-boundary vs in-frame step statistics of the reflector bin (phase in rad, amplitude in dB)."""
    z = r["zref"]
    nc = r["cfg"]["nchirps"]
    n = len(z)
    if n < 3:
        return None
    ok = ~np.isnan(z[1:]) & ~np.isnan(z[:-1]) & (np.abs(z[1:]) > 0) & (np.abs(z[:-1]) > 0)
    k = np.arange(1, n)
    bd = (k % nc == 0)
    dphi = np.angle(z[1:] * np.conj(z[:-1]))
    da = 20 * np.log10(np.maximum(np.abs(z[1:]), 1e-12) / np.maximum(np.abs(z[:-1]), 1e-12))
    ins, bds = ok & ~bd, ok & bd
    if ins.sum() < 10 or bds.sum() < 3:
        return None
    res = dict(n_in=int(ins.sum()), n_bd=int(bds.sum()))
    for name, v in (("phi", dphi), ("amp", da)):
        a_in, a_bd = v[ins], v[bds]
        if name == "phi":
            c_in = float(np.angle(np.exp(1j * a_in).mean()))
            c_bd = float(np.angle(np.exp(1j * a_bd).mean()))
            step = float(np.angle(np.exp(1j * (c_bd - c_in))))
        else:
            c_in, c_bd = float(a_in.mean()), float(a_bd.mean())
            step = c_bd - c_in
        dev_in = np.abs(a_in - c_in) if name == "amp" else np.abs(np.angle(np.exp(1j * (a_in - c_in))))
        dev_bd = np.abs(a_bd - c_in) if name == "amp" else np.abs(np.angle(np.exp(1j * (a_bd - c_in))))
        p99 = float(np.percentile(dev_in, 99))
        res[name] = dict(mean_in=c_in, mean_bd=c_bd, step=step, p99_in=p99, p999_in=float(np.percentile(dev_in, 99.9)),
                         max_bd=float(dev_bd.max()), frac_over=float((dev_bd > p99).mean()),
                         p99_bd=float(np.percentile(dev_bd, 99)))
    return res
