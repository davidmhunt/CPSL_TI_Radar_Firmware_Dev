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

P = "\nmmwDemo:/>"


class FakePort:
    """Stands in for common.CliPort: scripted replies by command prefix."""
    PROMPT = "mmwDemo:/>"

    def __init__(self, rules):
        self.rules, self.sent = rules, []

    def command(self, line, timeout=2.0):
        self.sent.append(line)
        for pre, rep in self.rules:
            if line.startswith(pre):
                return line + "\r\n" + rep + P
        return line + "\r\nDone" + P

    def close(self):
        pass


def factory(port):
    f = lambda dev: port  # noqa: E731
    f.PROMPT = FakePort.PROMPT
    return f


class Tests2(unittest.TestCase):
    def setUp(self):
        self.saved = (R.common.CliPort, R.configure, R.run_capture)
        self.addCleanup(lambda: (setattr(R.common, "CliPort", self.saved[0]), setattr(R, "configure", self.saved[1]),
                                 setattr(R, "run_capture", self.saved[2])))

    def test_with_num_frames(self):
        new = R.with_num_frames(CFG, 5)
        self.assertIn("frameCfg 0 0 255 5 510.3 1 0", new)
        self.assertEqual(len(new), len(CFG))

    def test_chan_pass(self):
        port = FakePort([("sensorStart", "Error: channelCfg differs from the first sensorStart; re-send"),
                         ("queryDemoStatus", "Sensor State: 3")])
        R.common.CliPort = factory(port)
        R.configure = lambda dev, lines, out=print: True
        R.run_capture = lambda dev, cap, cfg, dur, extra=(): (0, ev())
        a = argparse.Namespace(cfg=R.DEFAULT_CFG, duration=10, channel_cfg="15 1 0")
        out = []
        self.assertTrue(R.cmd_chan("dev", a, out.append))
        self.assertEqual(port.sent[-2], "sensorStart")                 # full cfg sent, rejected at sensorStart
        self.assertIn("channelCfg 15 1 0", port.sent)
        self.assertTrue(any("channelCfg differs" in l for l in out))

    def test_chan_not_rejected_or_exception_fails(self):
        for rule in ([("queryDemoStatus", "Sensor State: 2")],
                     [("sensorStart", "Error: channelCfg differs\nException in MSS"), ("queryDemoStatus", "Sensor State: 3")]):
            R.common.CliPort = factory(FakePort(rule))
            R.configure = lambda dev, lines, out=print: True
            R.run_capture = lambda dev, cap, cfg, dur, extra=(): (0, ev())
            a = argparse.Namespace(cfg=R.DEFAULT_CFG, duration=10, channel_cfg="15 1 0")
            self.assertFalse(R.cmd_chan("dev", a, lambda *_: None))

    def test_finite(self):
        R.common.CliPort = factory(FakePort([("queryDemoStatus", "Sensor State: 0\nLVDS HW frames done: 5")]))
        R.configure = lambda dev, lines, out=print: True
        R.run_capture = lambda dev, cap, cfg, dur, extra=(): (0, ev(avail=1275, frames=5.0, n=1275))
        a = argparse.Namespace(cfg=R.DEFAULT_CFG, frames=5, extra_s=3.0)
        self.assertTrue(R.cmd_finite("dev", a, lambda *_: None))
        R.run_capture = lambda dev, cap, cfg, dur, extra=(): (0, ev(avail=1020, frames=4.0, n=1020))
        self.assertFalse(R.cmd_finite("dev", a, lambda *_: None))

    def test_start0_passes_start_cmd(self):
        seen = []
        R.run_capture = lambda dev, cap, cfg, dur, extra=(): seen.append(list(extra)) or (0, ev())
        a = argparse.Namespace(cfg=R.DEFAULT_CFG, duration=10)
        self.assertTrue(R.cmd_start0("dev", a, lambda *_: None))
        self.assertEqual(seen, [["--start-cmd", "sensorStart 0"]])

    def test_decide_adc_bits(self):
        self.assertEqual(R.decide_adc_bits(5000, 0, 0)[0], 16)
        self.assertEqual(R.decide_adc_bits(2047, 40, 12)[0], 12)
        self.assertIsNone(R.decide_adc_bits(900, 0, 0)[0])

    def fake_res(self, samples, swap=0):
        import array
        a = array.array("h", samples)
        B, H, ns = 4 * len(samples) // 2 // 1, 0, len(samples) // 2
        cfg = {"B": 2 * len(samples) + 64, "H": 0, "nrx": 1, "ns": ns, "swap": swap}
        return {"cfg": cfg, "n_chirps": 1, "adc_ok": [True], "stream": bytearray(a.tobytes() + bytes(64))}

    def test_adc_peaks_swap(self):
        # interleaved half-words [w0, w1, ...]: SampleSwap 0 -> w0 = I; 1 -> w0 = Q
        res = self.fake_res([100, -300, 50, 20], swap=0)
        lo, hi, n = R.adc_peaks(res)
        self.assertEqual((lo["I"], hi["I"], lo["Q"], hi["Q"], n), (0, 100, -300, 20, 1))
        lo, hi, _ = R.adc_peaks(self.fake_res([100, -300, 50, 20], swap=1))
        self.assertEqual((hi["Q"], lo["I"]), (100, -300))

    def test_judge_adc_12_and_16_and_undecided(self):
        out = []
        r12 = self.fake_res([2047] * 20 + [-2048] * 20)
        self.assertTrue(R.judge_adc(r12, out.append)[0][1])
        self.assertIn("12", out[-1])
        r16 = self.fake_res([30000, -32768, 5, 7])
        self.assertTrue(R.judge_adc(r16, out.append)[0][1])
        self.assertIn("16", out[-1])
        self.assertFalse(R.judge_adc(self.fake_res([100, -90, 3, 4]), out.append)[0][1])


if __name__ == "__main__":
    unittest.main()
