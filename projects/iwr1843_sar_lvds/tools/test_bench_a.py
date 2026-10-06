"""Unit tests for bench_a.py (Set A bench commands) and bench_stream.py: fake serial port, synthetic captures, no hardware.

    cd firmware_dev && uv run --group tools pytest projects/iwr1843_sar_lvds/tools/test_bench_a.py
"""
import argparse
import json
import math
import os
import random
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_a as A  # noqa: E402
import bench_run as R  # noqa: E402
import bench_stream as BS  # noqa: E402
import sar_parse as P  # noqa: E402
import sar_synth as S  # noqa: E402
from test_bench_run import ev  # noqa: E402

common = R.common
HSI = A.HSI_DEV
EXAMPLE = common.cfg_params(open(R.DEFAULT_CFG).read())
_CACHE = {}


def adc_bytes(k, cfg, rng, sigma, amp, tone_bin, scale, phase):
    out = bytearray()
    for n in range(cfg["ns"]):
        ph = 2 * math.pi * tone_bin * n / cfg["ns"] + phase
        i = max(-2048, min(2047, int(round(amp * scale * math.cos(ph) + rng.gauss(0, sigma)))))
        q = max(-2048, min(2047, int(round(amp * scale * math.sin(ph) + rng.gauss(0, sigma)))))
        out += struct.pack("<hh", q, i) if cfg["swap"] == 1 else struct.pack("<hh", i, q)
    return bytes(out)


def build_packets(cfg, n, regime=-1, hsi=True, sat_chirps=(), seed=1, sigma=3.0, amp=400.0, tone_bin=200, run_idx=1,
                  phase_step=0.0, late_chirps=()):
    """Device-order packets like sar_synth, plus: the HSI id in the header, the other slot holding chirp k+1
    (regime=+1, except at a frame's last chirp) or k-1 (regime=-1), a phase step at every frame boundary."""
    key = (cfg["B"], n, regime, hsi, tuple(sat_chirps), seed, sigma, amp, tone_bin, run_idx, phase_step, tuple(late_chirps))
    if key in _CACHE:
        return _CACHE[key]
    rng = random.Random(seed)
    nc, H = cfg["nchirps"], cfg["H"]
    tc, tb = int(round(cfg["tc_s"] * 1e8)), int(round(cfg["tb_s"] * 1e8))
    sat_set = set(sat_chirps)
    recs = [S.record(j, run_idx, nc, lambda c: 5 if c in sat_set else 0, 10_000_000, tc, tb, j in late_chirps)
            for j in range(n)]
    pk = []
    for k in range(n):
        hdr = (HSI + bytes(H - 8)) if hsi else bytes(H)
        if regime == 1 and k + 1 < n and k % nc != nc - 1:
            other = recs[k + 1]
        else:
            other = recs[k - 1] if k >= 1 else bytes(32)
        slots = (recs[k] + other) if k % 2 == 0 else (other + recs[k])
        pk.append(hdr + adc_bytes(k, cfg, rng, sigma, amp, tone_bin, 8.0 if k in sat_set else 1.0, phase_step * (k // nc))
                  + slots)
        assert len(pk[-1]) == cfg["B"]
    _CACHE[key] = pk
    return pk


def stats_text(chirps, nc, fmt=2, runidx=3, isr=None, avail=None, lvds=None, late=0, sat=0):
    isr = chirps if isr is None else isr
    avail = chirps if avail is None else avail
    lvds = chirps // nc if lvds is None else lvds
    return ("sarStats\nrun %d (sensor state 3), dataFmt %d, satMon 1\n\rchirps %d frames %d chirpStartIsr %d chirpAvail %d\n\r"
            "saturatedChirps %d\n\rlateIsr %d missedChirpIsr 0 frameResync 0 availResync 0\n\r"
            "cbuffErrIrq 0 cbuffChirpErr 0 cbuffFrameStartErr 0 (error bits sticky since boot), lvdsFramesDone %d\n\r"
            "tsTicks 0x0000001000000000 (100000000 Hz)\n\rDone\n\rmmwDemo:/>" % (
                runidx, fmt, chirps, chirps // nc, isr, avail, sat, late, lvds))


def write_cap(path, packets, text, extra=None):
    dg = S.to_datagrams(packets)
    side = {"chirpAvail": len(packets), "runIdx": 1, "raw": text, "frameEndTimeout": False, "source": "cli"}
    side.update(extra or {})
    S.write_capture(path, dg, side)
    return dg, side


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.saved = []
        for mod, name, val in ((A, "WORKDIR", self.tmp), (R, "WORKDIR", self.tmp), (A.time, "sleep", lambda s: None)):
            self.patch(mod, name, val)

    def patch(self, obj, name, val):
        old = getattr(obj, name)
        setattr(obj, name, val)
        self.addCleanup(setattr, obj, name, old)


class Parsing(unittest.TestCase):
    def test_parse_stats(self):
        s = A.parse_stats(stats_text(2550, 255))
        self.assertEqual((s["runIdx"], s["dataFmt"], s["chirps"], s["chirpStartIsr"], s["chirpAvail"], s["lvdsFramesDone"],
                          s["frames"], s["lateIsr"], s["saturatedChirps"]), (3, 2, 2550, 2550, 2550, 10, 10, 0, 0))
        self.assertEqual(A.parse_stats("garbage"), {})

    def test_real_sidecar_text(self):
        real = "sarStats\nrun 12 (sensor state 3), dataFmt 2, satMon 1\n\rchirps 2550 frames 10 chirpStartIsr 2550 chirpAvail 2550\n\rsaturatedChirps 0\n\r"
        self.assertEqual(A.parse_stats(real)["chirps"], 2550)

    def test_cfg_variants(self):
        base = R.bc.cfg_lines(R.DEFAULT_CFG, drop_last=True)
        on = A.cq_on(A.set_lvds(base, 1, 4), "0 111 4")
        self.assertIn("analogMonitor 1 1", on)
        self.assertIn("CQSigImgMonitor 0 111 4", on)
        self.assertEqual(on[-1], "lvdsStreamCfg -1 1 4 0")
        self.assertIn("analogMonitor 0 0", A.cq_off(A.set_lvds(base, 1, 4)))
        g = A.geom_cfg(on)
        self.assertEqual((g["B"], g["nchirps"]), (13328, 255))              # geometry read as dataFmt 2
        odd = common.cfg_params("\n".join(A.with_ns(base, 3302) + ["sensorStart"]) + "\n")
        self.assertEqual((odd["H"], odd["B"], odd["cfg_errors"]), (56, 13328, []))
        off = common.cfg_params("\n".join(A.set_lvds(base, 0, 2) + ["sensorStart"]) + "\n")
        self.assertEqual((off["H"], off["B"]), (0, 13264))
        self.assertEqual(A.cq_extra_bytes("0 111 4", "CQRxSatMonitor 0 3 146 127 0"), 224 + 128)

    def test_with_frame_period(self):
        base = R.bc.cfg_lines(R.DEFAULT_CFG, drop_last=True)
        self.assertIn("frameCfg 0 0 255 0 510.8 1 0", A.with_frame_period(base, 500))
        self.assertIn("frameCfg 0 0 255 0 510.35 1 0", A.with_frame_period(base, 50))

    def test_packet_size_and_frames(self):
        self.assertEqual(A.packet_size(2550 * 13328 - 336, 2550, 1456), (13328, 336))
        self.assertEqual(A.packet_size(100, 50, 1456), (None, None))                   # not unique: too few chirps
        self.assertTrue(A.frames_ok(10, 2550, 255))
        self.assertTrue(A.frames_ok(6, 1600, 255) and A.frames_ok(7, 1600, 255))
        self.assertFalse(A.frames_ok(5, 1600, 255))

    def test_decode_pairing_matches_unscramble(self):
        rng = random.Random(5)
        dev = bytes(rng.getrandbits(8) for _ in range(64))                              # device order
        raw = bytearray(dev)
        P.unscramble(raw)                                                                # to the DCA1000 order
        import numpy as np
        d = np.frombuffer(dev, dtype="<i2")
        v = A.decode_two_lane_iq_pairs(raw, q_first=False)
        self.assertTrue(np.array_equal(v[0::2], d[0::2]) and np.array_equal(v[1::2], d[1::2]))
        v = A.decode_two_lane_iq_pairs(raw, q_first=True)
        self.assertTrue(np.array_equal(v[0::2], d[1::2]) and np.array_equal(v[1::2], d[0::2]))


class Judges(unittest.TestCase):
    def test_irq(self):
        st = A.parse_stats(stats_text(2550, 255))
        reads = [("a", st), ("b", dict(st)), ("c", dict(st))]
        self.assertTrue(all(ok for _, ok in A.judge_irq(reads)))
        moved = [("a", st), ("b", dict(st, chirps=2551, chirpStartIsr=2551, chirpAvail=2551))]
        self.assertFalse(all(ok for _, ok in A.judge_irq(moved)))
        dead = [("a", dict(st, chirpStartIsr=0))]
        self.assertFalse(all(ok for _, ok in A.judge_irq(dead)))

    def test_dropped_helpers_geometry(self):
        adc, rec = A.affected(33985952, 33987408, EXAMPLE, 5100)
        self.assertEqual((sorted(adc), sorted(rec)), ([2549, 2550], [2549]))


class Offline(Base):
    """Offline cases of `late`, on a synthetic good capture."""

    def setUp(self):
        super().setUp()
        self.cfg = common.cfg_params(S.make_cfg())
        self.pk = build_packets(self.cfg, 200)
        self.cap = os.path.join(self.tmp, "good.cap")
        self.dg, self.side = write_cap(self.cap, self.pk, "")
        self.res = P.analyze(self.dg, self.cfg, self.side)

    def test_good(self):
        self.assertTrue(self.res["accepted"])

    def test_cut_first_packet_is_rejected_and_check3_alone_catches_it(self):
        B = self.cfg["B"]
        cut = A.cut_first_packet(self.dg, B)
        r1 = P.analyze(cut, self.cfg, self.side)
        self.assertFalse(r1["accepted"])
        n = (self.res["info"]["end"] - B) // B
        whole = A.keep_first_bytes(cut, n * B)
        r2 = P.analyze(whole, self.cfg, {"chirpAvail": n, "runIdx": 1})
        self.assertEqual(r2["checks"][2].status, "FAIL")                                # k-1 regime: only check 3 sees it
        self.assertEqual(r2["checks"][1].status, "PASS")
        self.assertFalse(r2["accepted"])

    def test_dropped_datagram_not_rejected_nothing_shifted(self):
        idx = len(self.dg) // 2
        seq, count, pl = common.split_header(self.dg[idx])
        d = A.drop_one(self.dg, idx)
        rd = P.analyze(d, self.cfg, self.side)
        checks = A.judge_dropped(self.res, rd, d, (count, count + len(pl)), self.cfg)
        self.assertTrue(all(ok for _, ok in checks), checks)

    def test_dropped_judge_fails_when_a_chirp_shifted(self):
        idx = len(self.dg) // 2
        seq, count, pl = common.split_header(self.dg[idx])
        d = A.drop_one(self.dg, idx)
        rd = P.analyze(d, self.cfg, self.side)
        rd["valid"][5] = False                                                           # pretend the parser lost a record
        checks = A.judge_dropped(self.res, rd, d, (count, count + len(pl)), self.cfg)
        self.assertFalse(all(ok for _, ok in checks))


class Flows(Base):
    """The command flows with a scripted board: patched configure / capture / stats readers."""

    def args(self, **kw):
        d = dict(cfg=R.DEFAULT_CFG, duration=10, sigimg="0 111 4", tb_add_us=0, capture=None, ns2=3302, delay=2.0,
                 late_duration=5.0, reads=2, gap=4.0)
        d.update(kw)
        return argparse.Namespace(**d)

    def fmt_capture(self, sizes, nc=255, chirps=1600, fmt=4, drop=False, text=None):
        """patch capture_only to write dataFmt-4-like captures of the given packet sizes in order."""
        sizes = iter(sizes)
        self.sent = []

        def cap_only(dev, cap, cfgp, duration, extra=(), out=print):
            B = next(sizes)
            pk = [HSI + bytes(B - 8) for _ in range(chirps)]
            dg = S.to_datagrams(pk)
            if drop:
                del dg[len(dg) // 2]
            side = {"chirpAvail": chirps, "runIdx": 1, "raw": text or stats_text(chirps, nc, fmt), "frameEndTimeout": False,
                    "source": "cli"}
            S.write_capture(cap, dg, side)
            self.sent.append(cfgp)
            return side
        self.patch(A, "capture_only", cap_only)

    def common_patches(self):
        self.lines = []
        self.patch(A, "configure_show", lambda dev, lines, out=print: self.lines.append(lines) or True)
        self.patch(R, "run_capture", lambda dev, cap, cfgp, dur, extra=(): (0, ev(side={})))

    def test_fmt4_pass(self):
        self.common_patches()
        self.fmt_capture([13200 + 64 + 16 + 352, 13200 + 64 + 16])
        out = []
        self.assertTrue(A.cmd_fmt4("dev", self.args(), out.append))
        self.assertEqual(len(self.lines), 3)
        self.assertIn("lvdsStreamCfg -1 1 4 0", self.lines[0])
        self.assertIn("analogMonitor 1 1", self.lines[0])
        self.assertIn("analogMonitor 0 0", self.lines[1])
        self.assertIn("lvdsStreamCfg -1 1 2 0", self.lines[2])
        self.assertTrue(any("CQ on - off = 352 B" in l for l in out))
        self.assertTrue(any("NOT verified" in l for l in out))

    def test_fmt4_gap_and_missing_back_capture_fail(self):
        self.common_patches()
        self.fmt_capture([13200 + 64 + 16] * 2, drop=True)
        self.assertFalse(A.cmd_fmt4("dev", self.args(), lambda *_: None))
        self.common_patches()
        self.fmt_capture([13200 + 64 + 16] * 2)
        self.patch(R, "run_capture", lambda dev, cap, cfgp, dur, extra=(): (0, ev(accepted=False, failing=[2], side={})))
        self.assertFalse(A.cmd_fmt4("dev", self.args(), lambda *_: None))

    def test_fmt4_cfg_error_still_restores(self):
        sent = []

        def conf(dev, lines, out=print):
            sent.append(lines)
            return not any("lvdsStreamCfg -1 1 4 0" in l for l in lines)
        self.patch(A, "configure_show", conf)
        self.patch(R, "run_capture", lambda dev, cap, cfgp, dur, extra=(): (0, ev(side={})))
        self.fmt_capture([])
        self.assertFalse(A.cmd_fmt4("dev", self.args(), lambda *_: None))
        self.assertIn("lvdsStreamCfg -1 1 2 0", sent[-1])                                # dataFmt 2 restored anyway

    def test_fmt1_pass_and_wrong_size(self):
        self.common_patches()
        rng = random.Random(2)
        blob = bytes(rng.getrandbits(8) for _ in range(13200))

        def cap_only(dev, cap, cfgp, duration, extra=(), out=print, B=13264):
            pk = [HSI + bytes(56) + blob + bytes(B - 13264) for _ in range(1600)]
            side = {"chirpAvail": 1600, "runIdx": 1, "raw": stats_text(1600, 255, 1), "frameEndTimeout": False, "source": "cli"}
            S.write_capture(cap, S.to_datagrams(pk), side)
            return side
        self.patch(A, "capture_only", cap_only)
        out = []
        self.assertTrue(A.cmd_fmt1("dev", self.args(duration=30), out.append))
        self.assertIn("lvdsStreamCfg -1 1 1 0", self.lines[0])
        self.assertIn("lvdsStreamCfg -1 1 2 0", self.lines[1])
        self.patch(A, "capture_only", lambda *a, **k: cap_only(*a, B=13264 + 16, **k))
        self.assertFalse(A.cmd_fmt1("dev", self.args(duration=30), lambda *_: None))

    def test_bsize(self):
        got = []

        def conf(dev, lines, out=print):
            return True
        self.patch(A, "configure_show", conf)
        Bs = {}

        def run_capture(dev, cap, cfgp, dur, extra=()):
            cfg = common.cfg_params(open(cfgp).read())
            B = Bs.get(cfg["H"], cfg["B"])
            got.append((cfg["H"], B))
            return 0, ev(B=B, avail=2550, end=2550 * B - 300, cfg={"tc_s": 2e-3, "tb_s": 3e-4})
        self.patch(R, "run_capture", run_capture)
        out = []
        self.assertTrue(A.cmd_bsize("dev", self.args(), out.append))
        self.assertEqual(got, [(64, 13328), (0, 13264), (56, 13328)])
        Bs[56] = 13336                                                                   # a packet that is not H + 4RNs + 64
        got.clear()
        self.assertFalse(A.cmd_bsize("dev", self.args(), lambda *_: None))

    def test_irq(self):
        self.common_patches()
        text = stats_text(2550, 255)
        self.patch(A, "capture_only", lambda dev, cap, cfgp, dur, extra=(), out=print: {"raw": text})
        self.patch(A, "read_stats", lambda dev: text)
        out = []
        self.assertTrue(A.cmd_irq("dev", self.args(), out.append))
        self.assertEqual(len(self.lines), 2)                                             # cfg, then re-sent before the last read
        self.assertEqual(sum(1 for l in out if l.startswith(("after sensorStop", "+", "before next")) and "chirpStartIsr 2550" in l), 4)
        self.patch(A, "read_stats", lambda dev: stats_text(2551, 255))                   # moves after the stop
        self.assertFalse(A.cmd_irq("dev", self.args(), lambda *_: None))

    def test_late_flow(self):
        cfg_path = os.path.join(self.tmp, "syn.cfg")
        with open(cfg_path, "w") as fh:
            fh.write(S.make_cfg())
        cfg = common.cfg_params(S.make_cfg())
        pk = build_packets(cfg, 200)
        self.common_patches()

        def cap_only(dev, cap, cfgp, duration, extra=(), out=print):
            return write_cap(cap, pk, "")[1]
        self.patch(A, "capture_only", cap_only)

        def late(dev, cap, duration, delay_s, timer_s=30, out=print):
            raw = b"".join(pk)[50 * cfg["B"] + 100:]                                       # 50 packets late, mid-packet: the late arming
            side = {"chirpAvail": 200, "runIdx": 1, "raw": "", "frameEndTimeout": False, "source": "cli"}
            S.write_capture(cap, S.stream_from_bytes(raw), side)
            return side
        self.patch(A, "late_armed_capture", late)
        out = []
        self.assertTrue(A.cmd_late("dev", self.args(cfg=cfg_path), out.append), "\n".join(out))
        self.assertTrue(any("check 2" in l for l in out))
        # a late capture that the parser would ACCEPT (armed in time) must make the command FAIL

        def not_late(dev, cap, duration, delay_s, timer_s=30, out=print):
            return write_cap(cap, pk, "")[1]
        self.patch(A, "late_armed_capture", not_late)
        self.assertFalse(A.cmd_late("dev", self.args(cfg=cfg_path), lambda *_: None))


class Bytes(Base):
    def run_bytes(self, packets, **kw):
        cfg_path = os.path.join(self.tmp, "syn.cfg")
        with open(cfg_path, "w") as fh:
            fh.write(S.make_cfg())
        cap = os.path.join(self.tmp, "b.cap")
        write_cap(cap, packets, "")
        out = []
        ok = A.cmd_bytes(None, argparse.Namespace(capture=cap, cfg=cfg_path, duration=10), out.append)
        return ok, out

    def test_both_regimes_pass_and_are_named(self):
        cfg = common.cfg_params(S.make_cfg())
        ok, out = self.run_bytes(build_packets(cfg, 1200, regime=-1))
        self.assertTrue(ok, "\n".join(out))
        self.assertTrue(any(l.startswith("REGIME: k-1") for l in out))
        ok, out = self.run_bytes(build_packets(cfg, 1200, regime=1))
        self.assertTrue(ok, "\n".join(out))
        self.assertTrue(any(l.startswith("REGIME: k+1") for l in out))

    def test_too_few_packets_or_no_header_fail(self):
        cfg = common.cfg_params(S.make_cfg())
        ok, out = self.run_bytes(build_packets(cfg, 300, regime=-1))
        self.assertFalse(ok)                                                             # < 1000 in-frame packets
        ok, out = self.run_bytes(build_packets(cfg, 1200, regime=-1, hsi=False))
        self.assertFalse(ok)                                                             # no HSI id in the header


if __name__ == "__main__":
    unittest.main()
