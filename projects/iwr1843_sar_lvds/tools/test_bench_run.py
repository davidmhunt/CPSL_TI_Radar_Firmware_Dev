"""Unit tests for bench_run.py with fakes: no serial port, no DCA1000, no hardware.

    cd firmware_dev && uv run pytest projects/iwr1843_sar_lvds/tools/test_bench_run.py
"""
import argparse
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_run as R  # noqa: E402

CFG = R.bc.cfg_lines(R.DEFAULT_CFG)


def dg(seq, count, n=8):
    return struct.pack("<I", seq) + count.to_bytes(6, "little") + bytes(n)


def ev(**kw):
    cfg = {"tc_s": 2e-3, "tb_s": 3e-4}
    e = dict(accepted=True, failing=[], avail=7650, B=13328, end=7650 * 13328, datagrams=100, gaps=0, n=7650,
             valid=7650, absent=0, partial=0, sat=0, dt_in=2000.0, dt_bd=2300.5, frames=117.0, frame_s=0.5103,
             cfg=cfg)
    e.update(kw)
    return e


class Tests(unittest.TestCase):
    def test_with_profile_changes_only_gain_hpf(self):
        new = R.with_profile(CFG, 36, 1, 2)
        diff = [(a, b) for a, b in zip(CFG, new) if a != b]
        self.assertEqual(len(diff), 1)
        t = diff[0][1].split()
        self.assertEqual(t[0], "profileCfg")
        self.assertEqual(t[12:15], ["1", "2", "36"])
        for cmd in ("channelCfg", "adcCfg", "lowPower"):          # re-sent identically
            self.assertEqual([l for l in new if l.startswith(cmd)], [l for l in CFG if l.startswith(cmd)])
        self.assertEqual(new[-1], CFG[-1])

    def test_with_profile_rejects_bad_cfg(self):
        with self.assertRaises(ValueError):
            R.with_profile(["sensorStop"], 30, 0, 0)

    def test_with_command(self):
        new = R.with_command(CFG, "channelCfg", "15 1 0")
        self.assertIn("channelCfg 15 1 0", new)
        with self.assertRaises(ValueError):
            R.with_command(CFG, "nonesuch", "1")

    def test_parse_points(self):
        self.assertEqual(R.parse_points("36:0:0,24:0:1"), [(36, 0, 0), (24, 0, 1)])

    def test_seq_gaps(self):
        self.assertEqual(R.seq_gaps([dg(1, 0), dg(2, 8), dg(5, 16)]), 2)
        self.assertEqual(R.seq_gaps([dg(1, 0), dg(2, 8), dg(2, 8)]), 0)
        self.assertEqual(R.seq_gaps([]), 0)

    def test_judge_long_pass_and_fail(self):
        self.assertTrue(all(ok for _, ok in R.judge_long(ev(), 30)))
        cut = ev(frames=58.0, n=14790)                       # 29.6 s: the 30 s timer cut it off
        self.assertFalse(all(ok for _, ok in R.judge_long(cut, 30)))
        self.assertFalse(all(ok for _, ok in R.judge_long(ev(gaps=3), 30)))
        short = ev(end=7650 * 13328 - 5000)                  # lost more than one datagram at the end
        self.assertFalse(all(ok for _, ok in R.judge_long(short, 30)))
        tail = ev(end=7650 * 13328 - 1000)                   # one lost final datagram is tolerated
        self.assertTrue(all(ok for _, ok in R.judge_long(tail, 30)))

    def test_capture_ok(self):
        self.assertTrue(R.capture_ok(ev()))
        self.assertFalse(R.capture_ok(ev(accepted=False, failing=[3])))
        self.assertFalse(R.capture_ok(ev(absent=1)))
        self.assertFalse(R.capture_ok(None))

    def test_block_prints(self):
        lines = []
        R.block("x", ev(), lines.append)
        self.assertIn("ACCEPTED", lines[0])
        R.block("x", ev(accepted=False, failing=[2, 4]), lines.append)
        self.assertIn("REJECTED(check2,check4)", lines[-3])

    def run_restart(self, ok_cycles, min_cycles=3):
        sent = []
        R.configure = lambda dev, lines, out=print: sent.append(lines) or True
        caps = iter(ok_cycles)
        R.run_capture = lambda dev, cap, cfg, dur, extra=(): (0, next(caps))
        a = argparse.Namespace(cfg=R.DEFAULT_CFG, duration=30, points=R.DEFAULT_POINTS, min_cycles=min_cycles,
                               verbose=False)
        out = []
        return R.cmd_restart("dev", a, out.append), sent, out

    def test_restart_pass(self):
        ok, sent, _ = self.run_restart([ev()] * 4)
        self.assertTrue(ok)
        self.assertEqual(len(sent), 4)
        for lines in sent:                                    # every cycle starts with sensorStop + flushCfg, no start
            self.assertEqual(lines[:2], ["sensorStop", "flushCfg"])
            self.assertNotIn("sensorStart", lines)
        gains = [[l.split()[-1] for l in ls if l.startswith("profileCfg")][0] for ls in sent]
        self.assertEqual(gains, ["36", "24", "30", "30"])

    def test_restart_fail_one_cycle(self):
        ok, _, out = self.run_restart([ev(), ev(accepted=False, failing=[2]), ev(), ev()])
        self.assertFalse(ok)
        self.assertTrue(any("FAIL" in l and "cycle 2" in l for l in out))

    def test_restart_cfg_refused_stops(self):
        R.configure = lambda dev, lines, out=print: False
        a = argparse.Namespace(cfg=R.DEFAULT_CFG, duration=30, points="30:0:0,30:0:0", min_cycles=3, verbose=False)
        out = []
        self.assertFalse(R.cmd_restart("dev", a, out.append))


if __name__ == "__main__":
    unittest.main()
