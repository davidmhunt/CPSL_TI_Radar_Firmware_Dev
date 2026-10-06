#!/usr/bin/env python3
"""Split a DCA1000 capture of an iwr1843_sar_lvds run (dataFmt 2) into per-chirp ADC + metadata, after proving the
capture is aligned.

    uv run python sar_parse.py CAPTURE --cfg CFG [--sarstats FILE | --chirp-avail N [--run-idx N]]
                               [--out PREFIX] [--force]

CAPTURE is written by dca_capture.py (datagrams with their 10-byte DCA1000 headers). CFG is the cfg the run used
(sar_cfg_check.py reads it: Ns, RX count, HSI header, SampleSwap, Tc, Tb). --sarstats defaults to
CAPTURE.sarstats.json, which dca_capture.py writes from the CLI `sarStats` read after `sensorStop`.

Packet k is placed at byte k*B of the run, from the DCA1000 byte count in each datagram header, never by searching
for the HSI id or "SARM". The tool therefore needs the recording to start at the run's first byte, and runs the four
capture-requirement checks of docs/lvds_data_format.md (section 1) to prove it did. It is the reference
implementation of those checks. Any check that fails or cannot be evaluated rejects the capture: it cannot be proven
aligned, so discard it and capture again. A rejected capture writes no aligned output unless --force is given
(debugging only; the files then sit beside a *_FORCED_REJECTED.txt marker).

Outputs on an accepted capture:
    PREFIX_adc.bin   int16 I,Q pairs, chirp-major: [chirp][rx][sample][I,Q]; lost bytes are zeros
    PREFIX_meta.csv  one row per chirp: every record field, validity, time, saturation with its lag applied
Exit code: 0 accepted, 1 rejected, 2 usage or unreadable input. Stdlib only. No hardware access.
"""
import argparse
import array
import csv
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_common as common  # noqa: E402

MAGIC = 0x4D524153          # "SARM"
REC = struct.Struct("<IHHIHHIHBBQ")   # 32 bytes, docs/lvds_data_format.md section 2
FLAG_SAT_VALID, FLAG_LATE = 0x1, 0x4
TS_HZ = 100e6               # 10 ns ticks (format doc section 2, Timestamp)

REQUIREMENT = ("Capture requirement (docs/lvds_data_format.md section 1): one run per recording; arm the DCA1000 "
               "before sensorStart; stop it only after sensorStop; keep the DCA1000 UDP headers; read sarStats "
               "after sensorStop.")


# --- reassembly ----------------------------------------------------------------------------------------------------
def unscramble(buf):
    """Step 1 of the byte order: swap bytes 2-3 with bytes 4-5 in every 8-byte group (in place; len % 8 == 0)."""
    a, b = buf[2::8], buf[4::8]
    buf[2::8], buf[4::8] = b, a
    a, b = buf[3::8], buf[5::8]
    buf[3::8], buf[5::8] = b, a


def reassemble(datagrams):
    """Place datagram payloads at their byte counts. Returns (stream, mask, info) where stream is the
    *unscrambled* reassembled run (F7: whole stream from byte 0), mask[i] = 1 where byte i was received, and info
    carries what check 1 needs. Exact duplicate datagrams (same sequence number, count and bytes) are dropped (F5)."""
    seen, items, dups = set(), [], 0
    for idx, d in enumerate(datagrams):
        if len(d) <= common.UDP_HDR:
            continue
        seq, count, payload = common.split_header(d)
        key = (seq, count, payload)
        if key in seen:
            dups += 1
            continue
        seen.add(key)
        items.append((idx, seq, count, payload))
    info = {"datagrams": len(items), "duplicates": dups, "overlaps": [], "restarts": 0, "end": 0,
            "max_payload": max((len(p) for _, _, _, p in items), default=0)}
    by_count = sorted(items, key=lambda t: (t[2], t[0]))
    for a, b in zip(by_count, by_count[1:]):
        if a[2] + len(a[3]) > b[2]:
            info["overlaps"].append((a[1], b[1]))
    # A datagram at byte count 0 that arrives long after higher counts is a restart (two recordings joined);
    # plain reordering moves a datagram by a few positions only.
    slack = 100
    for pos, (idx, seq, count, payload) in enumerate(items):
        if count == 0 and any(c >= slack * info["max_payload"] for _, _, c, _ in items[:pos]):
            info["restarts"] += 1
    end = max((c + len(p) for _, _, c, p in items), default=0)
    info["end"] = end
    size = (end + 7) // 8 * 8
    stream, mask = bytearray(size), bytearray(size)
    for _, _, count, payload in items:
        stream[count:count + len(payload)] = payload
        mask[count:count + len(payload)] = b"\x01" * len(payload)
    unscramble(stream)
    unscramble(mask)                            # the same permutation, so a hole stays a hole
    return stream, mask, info


# --- the analysis ---------------------------------------------------------------------------------------------------
class Check:
    def __init__(self, num, name):
        self.num, self.name, self.status, self.detail = num, name, "NOT EVALUABLE", ""

    def set(self, status, detail):
        self.status, self.detail = status, detail
        return self


def _rec_at(stream, mask, off):
    """Unpack the 32-byte record at `off`; None if any of its bytes was not received."""
    if off + 32 > len(stream) or mask[off:off + 32].count(1) != 32:
        return None
    return REC.unpack_from(stream, off)


def _r(t):
    return dict(zip(("magic", "version", "flags", "frameIdx", "chirpInFrame", "numChirps", "gidx", "runIdx",
                     "satSlices", "satRefLag", "ts"), t))


def analyze(datagrams, cfg, sarstats=None, flow_error=None):
    """Run checks 1-4 and split the run. `cfg` = common.cfg_params(); `sarstats` = dict with chirpAvail (and
    runIdx, frameEndTimeout) or None. Returns a result dict (see render())."""
    B, M, H = cfg["B"], cfg["M"], cfg["H"]
    nc = cfg["nchirps"]
    stream, mask, info = reassemble(datagrams)
    end = info["end"]
    avail = sarstats["chirpAvail"] if sarstats else None
    n_chirps = avail if avail is not None else -(-end // B)
    need = n_chirps * B
    n_scan = max(n_chirps, -(-end // B))        # check 2 looks at every packet recorded, even beyond chirpAvail
    if len(stream) < n_scan * B:
        stream.extend(bytes(n_scan * B - len(stream)))
        mask.extend(bytes(n_scan * B - len(mask)))

    # per packet: record (slot k mod 2) if all 32 bytes are present
    recs = [_rec_at(stream, mask, k * B + M + 32 * (k % 2)) for k in range(n_scan)]

    # the run's runIdx: sarStats when known, else the first record that matches magic, version and position
    run_idx = sarstats.get("runIdx") if sarstats else None
    first_valid_run = None
    for k, t in enumerate(recs):
        if t is not None and t[0] == MAGIC and t[1] == 1 and t[6] == k & 0xFFFFFFFF:
            first_valid_run = t[7]
            break
    if run_idx is None:
        run_idx = first_valid_run

    checks = [Check(1, "byte counts consistent"), Check(2, "records validate at their positions"),
              Check(3, "no chirp follows the last packet"), Check(4, "bytes recorded = chirpAvail x B")]

    # check 1 ---------------------------------------------------------------------------------------------------
    c1 = checks[0]
    if info["overlaps"] or info["restarts"]:
        bits = []
        if info["overlaps"]:
            bits.append("%d overlapping datagram pair(s), e.g. seq %d and %d" % ((len(info["overlaps"]),)
                                                                                 + info["overlaps"][0]))
        if info["restarts"]:
            bits.append("byte count restarts at 0 %d time(s) (two recordings joined)" % info["restarts"])
        c1.set("FAIL", "; ".join(bits))
    elif info["datagrams"] == 0:
        c1.set("FAIL", "no datagrams")
    else:
        c1.set("PASS", "%d datagrams, %d exact duplicate(s) ignored, no overlap, no restart" % (
            info["datagrams"], info["duplicates"]))

    # check 2 ---------------------------------------------------------------------------------------------------
    c2 = checks[1]
    n_eval = n_valid = n_other_run = 0
    for k, t in enumerate(recs):
        if t is None:
            continue
        n_eval += 1
        if t[0] == MAGIC and run_idx is not None and t[7] != run_idx:
            n_other_run += 1
        elif t[0] == MAGIC and t[1] == 1 and t[6] == k & 0xFFFFFFFF and t[7] == run_idx:
            n_valid += 1
    n_fail = n_eval - n_valid
    allowed = max(1, n_eval // 100)
    if n_eval == 0:
        c2.set("FAIL", "no packet has all its record bytes")
    elif n_other_run:
        c2.set("FAIL", "%d record(s) carry another runIdx than the run's (%s): a second run in the recording" % (
            n_other_run, run_idx))
    elif sarstats and sarstats.get("runIdx") is not None and first_valid_run is not None \
            and first_valid_run != sarstats["runIdx"]:
        c2.set("FAIL", "first valid record has runIdx %d, sarStats says %d" % (first_valid_run, sarstats["runIdx"]))
    elif n_valid == 0:
        c2.set("FAIL", "0 of %d records validate: the recording starts mid-packet or at least 2 packets late" % n_eval)
    elif n_fail > allowed:
        c2.set("FAIL", "%d of %d records fail (allowed %d): records keep failing, firmware lost count" % (
            n_fail, n_eval, allowed))
    else:
        c2.set("PASS", "%d of %d records validate (%d failure(s) allowed); failed records get interpolated times" % (
            n_valid, n_eval, allowed))

    # check 3 ---------------------------------------------------------------------------------------------------
    c3 = checks[2]
    if end == 0:
        c3.set("NOT EVALUABLE", "no data")
    else:
        cand = (end - 1) // B
        if avail is not None:
            cand = min(cand, avail - 1)
        off = cand * B + M + 32 * ((cand + 1) % 2)
        t = _rec_at(stream, mask, off)
        if t is None:
            c3.set("NOT EVALUABLE", "packet %d: the other slot's bytes are not all present (a lost final "
                                    "datagram covers the record slots), so the start cannot be proven" % cand)
        elif t[0] == MAGIC and (run_idx is None or t[7] == run_idx) and t[6] == (cand + 1) & 0xFFFFFFFF:
            c3.set("FAIL", "packet %d's other slot holds chirp %d of the run: a chirp follows the last packet, so "
                           "the recording starts one packet late" % (cand, cand + 1))
        else:
            c3.set("PASS", "packet %d's other slot does not hold chirp %d" % (cand, cand + 1))

    # check 4 ---------------------------------------------------------------------------------------------------
    c4 = checks[3]
    if avail is None:
        c4.set("FAIL", "no sarStats reading: chirpAvail unknown")
    else:
        short = need - end
        tol = min(B - 1, info["max_payload"])   # a lost final datagram is a tail hole (F2); a whole packet is not
        if short == 0:
            c4.set("PASS", "%d B recorded = %d x %d" % (end, avail, B))
        elif 0 < short <= tol:
            c4.set("PASS", "%d B recorded, %d B short of %d x %d: tail hole (lost final datagram(s)), tolerated" % (
                end, short, avail, B))
        elif short > 0:
            c4.set("FAIL", "%d B recorded, expected %d x %d = %d: short by %d B (%.2f packets)" % (
                end, avail, B, need, short, short / B))
        else:
            c4.set("FAIL", "%d B recorded, expected %d x %d = %d: %d B too many (a second run, or recording "
                           "continued)" % (end, avail, B, need, -short))

    flow_ok = True
    if sarstats and sarstats.get("frameEndTimeout"):
        flow_ok = False
    if flow_error:
        flow_ok = False
    failing = [c.num for c in checks if c.status != "PASS"]
    accepted = not failing and flow_ok

    # per chirp metadata -------------------------------------------------------------------------------------------
    valid = [t is not None and t[0] == MAGIC and t[1] == 1 and t[6] == k & 0xFFFFFFFF and t[7] == run_idx
             for k, t in enumerate(recs)][:n_chirps]
    sat = {}
    for k, t in enumerate(recs[:n_chirps]):
        if valid[k] and t[2] & FLAG_SAT_VALID:
            sat[t[6] - t[9]] = t[8]
    valid_ks = [k for k in range(n_chirps) if valid[k]]
    t0 = recs[valid_ks[0]][10] if valid_ks else 0
    times = [None] * n_chirps          # (seconds from the first valid record's chirp, interpolated?)
    tc, tb = cfg["tc_s"], cfg["tb_s"]
    j_prev = None
    for k in range(n_chirps):
        if valid[k]:
            j_prev = k
            times[k] = ((recs[k][10] - t0) / TS_HZ, False)
        elif valid_ks:
            j = j_prev if j_prev is not None else valid_ks[0]
            tj = (recs[j][10] - t0) / TS_HZ
            times[k] = (tj + (k - j) * tc + (k // nc - j // nc) * tb, True)
    adc_ok = []
    for k in range(n_chirps):
        a = k * B + H
        adc_ok.append(mask[a:a + 4 * cfg["nrx"] * cfg["ns"]].count(1) == 4 * cfg["nrx"] * cfg["ns"])
    absent = sum(1 for k in range(n_chirps) if mask[k * B:(k + 1) * B].count(1) == 0)

    # time deltas between consecutive valid records
    d_in, d_bd = [], []
    for k in range(1, n_chirps):
        if valid[k] and valid[k - 1]:
            dt = (recs[k][10] - recs[k - 1][10]) / TS_HZ
            (d_bd if k % nc == 0 else d_in).append(dt)

    return dict(checks=checks, accepted=accepted, failing=failing, flow_ok=flow_ok, flow_error=flow_error,
                sarstats=sarstats, cfg=cfg, info=info, run_idx=run_idx, n_chirps=n_chirps, recs=recs[:n_chirps], valid=valid,
                sat=sat, times=times, adc_ok=adc_ok, absent=absent, stream=stream, d_in=d_in, d_bd=d_bd,
                n_valid=n_valid, n_eval=n_eval)


# --- reporting ------------------------------------------------------------------------------------------------------
def _stats(vals, ref):
    if not vals:
        return "no pairs"
    s = sorted(vals)
    p999 = s[min(len(s) - 1, math.ceil(0.999 * len(s)) - 1)]
    mean = sum(vals) / len(vals)
    return "n=%d mean %.2f us (%+.2f vs cfg %.2f), p99.9 %.2f us" % (len(vals), mean * 1e6, (mean - ref) * 1e6,
                                                                      ref * 1e6, p999 * 1e6)


def sat_summary(res):
    """(saturated chirps, chirps with a known result, chirps with unknown result)."""
    n = res["n_chirps"]
    known = sum(1 for k in range(n) if k in res["sat"])
    return sum(1 for k in range(n) if res["sat"].get(k, 0) > 0), known, n - known


def render(res):
    cfg = res["cfg"]
    L = ["capture: %d datagrams (%d exact duplicates ignored), %d B recorded; cfg: B = %d B/packet "
         "(H %d + 4x%dx%d ADC + 64), Nc %d" % (res["info"]["datagrams"], res["info"]["duplicates"],
                                              res["info"]["end"], cfg["B"], cfg["H"], cfg["nrx"], cfg["ns"],
                                              cfg["nchirps"])]
    st = res["sarstats"]
    L.append("sarStats: %s" % ("chirpAvail %d, runIdx %s" % (st["chirpAvail"], st.get("runIdx")) if st
                              else "none (check 4 cannot pass)"))
    for c in res["checks"]:
        L.append("check %d (%s): %s - %s" % (c.num, c.name, c.status, c.detail))
    if res["flow_error"] or (st and st.get("frameEndTimeout")):
        L.append("recording flow: FAIL - %s" % (res["flow_error"] or "firmware printed '%s'" % common.FRAME_END_MSG))
    if res["accepted"]:
        L.append("VERDICT: ACCEPTED")
    else:
        L.append("VERDICT: REJECTED: capture requirement not met (failing: %s)" % (
            ", ".join(["check %d" % n for n in res["failing"]] + (["recording flow"] if not res["flow_ok"] else [])))
        )
        L.append(REQUIREMENT)
        L.append("Any failed or not-evaluable check means the capture cannot be proven aligned: discard it and "
                 "capture again.")
    n = res["n_chirps"]
    nsat, known, unknown = sat_summary(res)
    partial = sum(1 for ok in res["adc_ok"] if not ok)
    late = sum(1 for k in range(n) if res["valid"][k] and res["recs"][k][2] & FLAG_LATE)
    L.append("summary: %d chirps (%d frames of %d); packets wholly absent %d (missing globalChirpIdx), ADC partly "
             "lost %d (marked adc_complete=0, never shifted)" % (n, n // cfg["nchirps"], cfg["nchirps"],
                                                                res["absent"], partial))
    L.append("         records valid %d / %d chirps; duplicate globalChirpIdx 0 (a record must equal its position); "
             "LATE-flagged %d" % (sum(res["valid"]), n, late))
    L.append("         saturated chirps %d (known result for %d chirps, unknown for %d; results are lag-aligned: "
             "docs/lvds_data_format.md section 3)" % (nsat, known, unknown))
    L.append("         in-frame dt: %s" % _stats(res["d_in"], cfg["tc_s"]))
    L.append("         boundary dt: %s (Tc+Tb)" % _stats(res["d_bd"], cfg["tc_s"] + cfg["tb_s"]))
    return "\n".join(L)


# --- output files --------------------------------------------------------------------------------------------------
META_COLS = ["chirp", "frame", "chirpInFrame", "record_valid", "adc_complete", "version", "flags", "frameIdx",
             "rec_chirpInFrame", "numChirpsPerFrame", "globalChirpIdx", "runIdx", "satSlices", "satRefLag",
             "tsTicks", "t_s", "t_interpolated", "late", "sat_slices_this_chirp"]


def write_outputs(res, prefix):
    cfg, B, H, nrx, ns = res["cfg"], res["cfg"]["B"], res["cfg"]["H"], res["cfg"]["nrx"], res["cfg"]["ns"]
    stream = res["stream"]
    n = res["n_chirps"]
    with open(prefix + "_adc.bin", "wb") as fh:
        for k in range(n):
            blk = array.array("h")
            blk.frombytes(bytes(stream[k * B + H:k * B + H + 4 * nrx * ns]))
            if sys.byteorder == "big":
                blk.byteswap()
            if cfg["swap"] == 1:                     # Q in the low half-word: swap to I,Q (format doc section 1)
                blk[0::2], blk[1::2] = blk[1::2], blk[0::2]
            fh.write(blk.tobytes())
    with open(prefix + "_meta.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(META_COLS)
        for k in range(n):
            t = res["recs"][k]
            ok = res["valid"][k]
            tm = res["times"][k]
            row = [k, k // cfg["nchirps"], k % cfg["nchirps"], int(ok), int(res["adc_ok"][k])]
            if ok:
                r = _r(t)
                row += [r["version"], r["flags"], r["frameIdx"], r["chirpInFrame"], r["numChirps"], r["gidx"],
                        r["runIdx"], r["satSlices"], r["satRefLag"], r["ts"]]
            else:
                row += [""] * 10
            row += ["%.9f" % tm[0] if tm else "", int(tm[1]) if tm else "",
                    int(bool(ok and t[2] & FLAG_LATE)), res["sat"].get(k, "")]
            w.writerow(row)


# --- command line --------------------------------------------------------------------------------------------------
def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 epilog="Exit 0 accepted, 1 rejected, 2 usage/input error. Read docs/tuning_guide.md.")
    ap.add_argument("capture", help="capture file written by dca_capture.py")
    ap.add_argument("--cfg", required=True, help="the cfg the run used")
    ap.add_argument("--sarstats", help="sarStats JSON (default CAPTURE.sarstats.json)")
    ap.add_argument("--chirp-avail", type=int, help="chirpAvail from the sarStats read, instead of a JSON file")
    ap.add_argument("--run-idx", type=int, help="run number from the sarStats read (optional)")
    ap.add_argument("--out", help="output prefix (default: CAPTURE without extension)")
    ap.add_argument("--force", action="store_true",
                    help="DEBUGGING ONLY: write aligned output even when the capture is rejected")
    return ap


def load_sarstats(opt):
    if opt.chirp_avail is not None:
        return {"chirpAvail": opt.chirp_avail, "runIdx": opt.run_idx}
    path = opt.sarstats or opt.capture + ".sarstats.json"
    if os.path.exists(path):
        with open(path) as fh:
            return json.load(fh)
    if opt.sarstats:
        raise OSError("sarstats file %s not found" % path)
    return None


def main(argv=None):
    opt = build_parser().parse_args(argv)
    try:
        with open(opt.cfg) as fh:
            cfg = common.cfg_params(fh.read())
        datagrams = common.read_capture(opt.capture)
        sarstats = load_sarstats(opt)
    except (OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    res = analyze(datagrams, cfg, sarstats)
    print(render(res))
    prefix = opt.out or os.path.splitext(opt.capture)[0]
    if res["accepted"]:
        write_outputs(res, prefix)
        print("wrote %s_adc.bin and %s_meta.csv" % (prefix, prefix))
        return 0
    if opt.force:
        write_outputs(res, prefix)
        with open(prefix + "_FORCED_REJECTED.txt", "w") as fh:
            fh.write("These files were written with --force from a REJECTED capture. They may be misaligned by "
                     "one or more chirps. Do not use them for results.\n" + render(res) + "\n")
        print("!" * 78)
        print("WARNING: --force: %s_adc.bin / _meta.csv written from a REJECTED capture. Alignment is NOT "
              "proven; debugging only." % prefix)
        print("!" * 78)
    else:
        print("no aligned output written (rejected; --force writes it for debugging only)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
