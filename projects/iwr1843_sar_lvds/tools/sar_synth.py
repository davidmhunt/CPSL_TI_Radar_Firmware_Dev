#!/usr/bin/env python3
"""Synthetic dataFmt 2 captures for testing the SAR host tools (no hardware, stdlib only).

    uv run python sar_synth.py OUT [--chirps N] [--scenario NAME]

Builds a run exactly as docs/lvds_data_format.md describes the device sending it (packet k = optional HSI header,
ADC block, two 32-byte record slots; slot k mod 2 = chirp k's own record, the other slot still holds chirp k-1's;
the saturation result lags one chirp), scrambles the byte order as the DCA1000 does, splits it into DCA1000
datagrams with 10-byte headers, and writes OUT (capture) and OUT.sarstats.json. Used by test_sar_parse.py and
test_sar_tune.py; also handy to try the tools before the bench.

Scenarios: clean, late_one_packet, mid_packet_start, lost_datagram, lost_final_datagram, two_runs,
duplicate_datagram, no_sarstats.
"""
import argparse
import json
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_common as common  # noqa: E402
import sar_parse  # noqa: E402

CFG_TEMPLATE = """\
sensorStop
flushCfg
dfeDataOutputMode 1
channelCfg 1 1 0
adcCfg 2 1
adcbufCfg -1 0 1 1 1
lowPower 0 0
profileCfg 0 77.25 480 10 1520 0 0 2.333 1 {ns} 2200 {hpf1} {hpf2} {gain}
chirpCfg 0 0 0 0 0 0 0 1
frameCfg 0 0 {nc} 0 {period} 1 0
analogMonitor 1 0
CQRxSatMonitor 0 3 146 {slices} 0
calibData 0 0 0
lvdsStreamCfg -1 1 2 0
sensorStart
"""


def make_cfg(ns=1024, nc=20, gain=30, hpf1=0, hpf2=0):
    """A cfg like configs/sar_example_2ms.cfg with fewer samples/chirps (Tc 2000 us, Tb 300 us)."""
    return CFG_TEMPLATE.format(ns=ns, nc=nc, period="%.1f" % (nc * 2.0 + 0.3), gain=gain, hpf1=hpf1, hpf2=hpf2,
                               slices=127)


def adc_block(k, cfg, sat_chirps, rng, sigma, amp, tone_bin):
    """int16 I/Q bytes (device memory order, SampleSwap per cfg) of chirp k: a complex tone plus white noise;
    chirps in sat_chirps are driven to the 12-bit rails."""
    ns, swap = cfg["ns"], cfg["swap"]
    out = bytearray()
    scale = 8.0 if k in sat_chirps else 1.0
    for n in range(ns):
        ph = 2 * math.pi * tone_bin * n / ns
        i = amp * scale * math.cos(ph) + rng.gauss(0, sigma)
        q = amp * scale * math.sin(ph) + rng.gauss(0, sigma)
        i = max(-2048, min(2047, int(round(i))))
        q = max(-2048, min(2047, int(round(q))))
        out += struct.pack("<hh", q, i) if swap == 1 else struct.pack("<hh", i, q)
    return bytes(out)


def record(j, run_idx, nc, sat_slices_of, t0_ticks, tc_ticks, tb_ticks, late=False):
    """The 32-byte record chirp j writes at its chirp-start interrupt: reports chirp j-1's saturation (lag 1)."""
    flags = 0x2 | (0x1 if j >= 1 else 0) | (0x4 if late else 0)
    frame, cif = divmod(j, nc)
    ts = t0_ticks + j * tc_ticks + frame * tb_ticks
    sat = sat_slices_of(j - 1) if j >= 1 else 0
    return sar_parse.REC.pack(sar_parse.MAGIC, 1, flags, frame, cif, nc, j, run_idx, sat, 1 if j >= 1 else 0, ts)


_CACHE = {}


def build_run(cfg, n_chirps, *args, **kw):
    """Cached _build_run (tests build the same run many times); the returned list must not be modified."""
    key = (cfg["B"], cfg["swap"], cfg["nchirps"], n_chirps, args, tuple(sorted((k, tuple(v) if isinstance(v, (list, tuple, set)) else v) for k, v in kw.items())))
    if key not in _CACHE:
        _CACHE[key] = _build_run(cfg, n_chirps, *args, **kw)
    return _CACHE[key]


def _build_run(cfg, n_chirps, run_idx=1, sat_chirps=(), seed=1, sigma=3.0, amp=400.0, tone_bin=200, t0=10_000_000,
              late_chirps=()):
    """Device-order packets (list of bytes, each B long) of one run."""
    rng = random.Random(seed)
    nc, B, H = cfg["nchirps"], cfg["B"], cfg["H"]
    tc, tb = int(round(cfg["tc_s"] * 1e8)), int(round(cfg["tb_s"] * 1e8))
    sat_set = set(sat_chirps)
    sat_of = lambda c: 5 if c in sat_set else 0     # noqa: E731
    recs = [record(j, run_idx, nc, sat_of, t0, tc, tb, j in late_chirps) for j in range(n_chirps)]
    pk = []
    for k in range(n_chirps):
        hdr = bytes(H)
        own, other = recs[k], (recs[k - 1] if k >= 1 else bytes(32))
        slots = (own + other) if k % 2 == 0 else (other + own)
        pk.append(hdr + adc_block(k, cfg, sat_set, rng, sigma, amp, tone_bin) + slots)
        assert len(pk[-1]) == B
    return pk


def to_datagrams(packets, payload=1462, start_count=0, first_seq=1):
    """Scramble a device-order byte stream as the DCA1000 delivers it and cut it into datagrams."""
    stream = bytearray(b"".join(packets))
    pad = (-len(stream)) % 8
    stream += bytes(pad)
    sar_parse.unscramble(stream)                    # the swap is its own inverse
    if pad:
        del stream[-pad:]
    out, pos, seq = [], 0, first_seq
    while pos < len(stream):
        chunk = bytes(stream[pos:pos + payload])
        out.append(struct.pack("<I", seq) + (start_count + pos).to_bytes(6, "little") + chunk)
        pos += len(chunk)
        seq += 1
    return out


def stream_from_bytes(raw, payload=1462, start_count=0, first_seq=1):
    """Like to_datagrams for a byte stream that does not start on a packet boundary."""
    return to_datagrams([raw], payload, start_count, first_seq)


def scenario(name, cfg, n_chirps=200, **kw):
    """Return (datagrams, sarstats dict or None) for a named case."""
    B, M = cfg["B"], cfg["M"]
    sat_chirps = kw.get("sat_chirps", ())
    pk = build_run(cfg, n_chirps, sat_chirps=sat_chirps, late_chirps=kw.get("late_chirps", ()))
    stats = {"chirpAvail": n_chirps, "runIdx": 1}
    if name == "clean":
        return to_datagrams(pk), stats
    if name == "late_one_packet":                   # arming one packet late: the DCA1000 restarts its count at 0
        return to_datagrams(pk[1:]), stats
    if name == "mid_packet_start":
        return stream_from_bytes(b"".join(pk)[100:]), stats
    if name == "lost_datagram":
        dg = to_datagrams(pk)
        # drop a datagram that lies inside one packet's ADC block (no record slot, no header)
        for i, d in enumerate(dg):
            lo = int.from_bytes(d[4:10], "little")
            hi = lo + len(d) - 10
            if lo // B == (hi - 1) // B and 50 < lo // B < 150 and (lo % B) >= cfg["H"] and (hi % B or B) <= M:
                del dg[i]
                return dg, stats
        raise AssertionError("no suitable datagram")
    if name == "lost_final_datagram":
        dg = to_datagrams(pk)
        return dg[:-1], stats
    if name == "two_runs":
        second = build_run(cfg, 5, run_idx=2, seed=2)
        return to_datagrams(pk + second), stats       # stats still those read after run 1
    if name == "duplicate_datagram":
        dg = to_datagrams(pk)
        dg.insert(60, dg[59])
        return dg, stats
    if name == "no_sarstats":
        return to_datagrams(pk), None
    raise ValueError(name)


def write_capture(path, datagrams, sarstats):
    with open(path, "wb") as fh:
        common.write_capture_header(fh)
        for d in datagrams:
            common.write_datagram(fh, d)
    if sarstats is not None:
        with open(path + ".sarstats.json", "w") as fh:
            json.dump(sarstats, fh)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("out", help="capture file to write (also OUT.sarstats.json, and OUT.cfg)")
    ap.add_argument("--chirps", type=int, default=200)
    ap.add_argument("--scenario", default="clean")
    opt = ap.parse_args(argv)
    text = make_cfg()
    cfg = common.cfg_params(text)
    dg, stats = scenario(opt.scenario, cfg, opt.chirps)
    write_capture(opt.out, dg, stats)
    with open(opt.out + ".cfg", "w") as fh:
        fh.write(text)
    print("wrote %s (%d datagrams), %s.cfg" % (opt.out, len(dg), opt.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
