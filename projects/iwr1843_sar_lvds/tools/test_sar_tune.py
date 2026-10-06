"""Unit tests for sar_tune_report.py and sar_tune_sweep.py: synthetic captures, no hardware.

    cd firmware_dev && uv run --group tools python -m unittest projects/iwr1843_sar_lvds/tools/test_sar_tune.py

Needs numpy and matplotlib (optional uv group `tools`); the tests skip without them.
"""
import io
import math
import os
import socket
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sar_common as common  # noqa: E402
import sar_parse as P  # noqa: E402
import sar_synth as S  # noqa: E402
import test_sar_parse as TP  # noqa: E402

try:
    import numpy as np
    import matplotlib  # noqa: F401
    import sar_tune_report as R
    import sar_tune_sweep as W
    HAVE = True
except ImportError:                       # pragma: no cover
    HAVE = False

SIGMA, AMP, TONE_BIN, NCH, SAT = 3.0, 400.0, 200, 200, 37


@unittest.skipUnless(HAVE, "numpy/matplotlib not installed (uv sync --group tools)")
class Report(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.cfg = TP.CFG
        dg, stats = S.scenario("clean", cls.cfg, NCH, sat_chirps=(SAT,))
        res = P.analyze(dg, cls.cfg, stats)
        assert res["accepted"]
        cls.prefix = os.path.join(cls.tmp.name, "syn")
        P.write_outputs(res, cls.prefix)
        cls.r = R.analyze_run(cls.prefix, cls.cfg, reflector_range=None)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_injected_clipped_chirp_is_reported_once_by_both_detectors(self):
        self.assertEqual(self.r["clipped"], [SAT])
        self.assertEqual(self.r["adc_clip"], [SAT])         # driven to the 12-bit rails
        self.assertEqual(self.r["fw_sat"], [SAT])           # lag-1 result, reported by chirp 38's record
        text = R.render(self.r)
        self.assertIn("clipped chirps: 1", text)
        self.assertIn("ADC full-scale hit: 1", text)
        self.assertIn("firmware saturation satSlices > 0: 1", text)

    def test_noise_floor_within_1_db_of_synthetic_value(self):
        w = np.hanning(self.cfg["ns"])
        var = SIGMA ** 2 + 1.0 / 12                          # int16 rounding adds 1/12 per component
        expect = 10 * np.log10(2 * var * (w ** 2).sum() / w.sum() ** 2 / 2048.0 ** 2)
        self.assertLess(abs(self.r["noise_db"] - expect), 1.0, (self.r["noise_db"], expect))

    def test_peak_and_reflector_snr(self):
        self.assertAlmostEqual(self.r["peak_max_db"], 20 * math.log10(2047 / 2048.0), delta=0.1)   # the clipped chirp
        slope = abs(self.cfg["slope_mhz_us"]) * 1e12
        rng = TONE_BIN * self.cfg["fs_ksps"] * 1e3 / self.cfg["ns"] * common.chk.C / (2 * slope)
        r = R.analyze_run(self.prefix, self.cfg, reflector_range=rng)
        w = np.hanning(self.cfg["ns"])
        var = SIGMA ** 2 + 1.0 / 12
        noise = 10 * np.log10(2 * var * (w ** 2).sum() / w.sum() ** 2 / 2048.0 ** 2)
        want = 20 * math.log10(AMP / 2048.0) - noise
        self.assertLess(abs(r["reflector"]["snr_db"] - want), 1.5, (r["reflector"], want))
        self.assertAlmostEqual(r["reflector"]["range_m"], rng, delta=0.1)

    def test_hpf_attenuation_line_and_png(self):
        text = R.render(self.r)
        self.assertIn("HPF: corners at", text)
        png = os.path.join(self.tmp.name, "t.png")
        R.plot(self.r, png)
        self.assertGreater(os.path.getsize(png), 5000)

    def test_forced_output_is_refused(self):
        d = tempfile.mkdtemp()
        dg, stats = S.scenario("late_one_packet", self.cfg, 60)
        res = P.analyze(dg, self.cfg, stats)
        prefix = os.path.join(d, "f")
        P.write_outputs(res, prefix)
        open(prefix + "_FORCED_REJECTED.txt", "w").close()
        with self.assertRaises(ValueError):
            R.analyze_run(prefix, self.cfg)

    def test_cli_main_prints_report(self):
        cfgp = os.path.join(self.tmp.name, "c.cfg")
        with open(cfgp, "w") as fh:
            fh.write(TP.CFG_TEXT)
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = R.main([self.prefix, "--cfg", cfgp, "--no-png", "--reflector-range", "27.8"])
        self.assertEqual(code, 0)
        for line in ("peak |I|,|Q| per chirp", "clipped chirps: 1", "noise floor:", "HPF: corners", "SNR"):
            self.assertIn(line, buf.getvalue())


class SweepCli(TP.FakeCli):
    """Fake radar: remembers the gain of the last profileCfg it was sent and 'streams' the matching synthetic run."""

    def __init__(self, events, addr, scenarios):
        super().__init__(events, addr, [], "")
        self.scenarios, self.gain, self.point = scenarios, None, -1

    def command(self, line, timeout=2.0):
        if line.startswith("profileCfg"):
            self.gain = int(line.split()[14])
        if line == "sensorStart":
            self.point += 1
            name, sat = self.scenarios[self.point]
            self.datagrams, stats = S.scenario(name, TP.CFG, 60, sat_chirps=sat)
            self.stats_text = "run 1 (sensor state 0), dataFmt 2, satMon 1\nchirps 60 frames 3 chirpStartIsr 60 chirpAvail 60\n"
        return super().command(line, timeout)


@unittest.skipUnless(HAVE, "numpy/matplotlib not installed (uv sync --group tools)")
class Sweep(unittest.TestCase):
    def test_edit_cfg_changes_only_gain_and_hpf(self):
        out = W.edit_cfg(TP.CFG_TEXT, 36, 2, 1)
        a = [l for l in TP.CFG_TEXT.splitlines() if l.startswith("profileCfg")][0].split()
        b = [l for l in out.splitlines() if l.startswith("profileCfg")][0].split()
        self.assertEqual([i for i in range(len(a)) if a[i] != b[i]], [12, 13, 14])
        self.assertEqual(b[12:], ["2", "1", "36"])
        self.assertEqual(W.hpf_codes("350:700"), (2, 1))

    def test_sweep_reads_sarstats_per_point_and_tabulates(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
        sock.bind(("127.0.0.1", 0))
        self.addCleanup(sock.close)
        events = []
        cli = SweepCli(events, sock.getsockname(), [("clean", ()), ("clean", (10, 11, 12)), ("late_one_packet", ())])
        dca = TP.FakeDca(events)
        rows = []
        for g in (30, 36, 42):
            rows.append(W.run_point(g, "175:350", TP.CFG_TEXT, cli, dca, sock, tmp.name, 0.2, log=lambda m: None))
        self.assertEqual([r["verdict"] for r in rows], ["ok", "ok", "REJECTED"], rows)
        self.assertEqual([r["clipped"] for r in rows[:2]], [0, 3])
        self.assertIn("check 3", rows[2]["note"])
        # per point: arm, sensorStart, sensorStop, sarStats, record stop; and cfg lines were sent before the arm
        starts = [i for i, e in enumerate(events) if e == "dca:RECORD_START"]
        self.assertEqual(len(starts), 3)
        for i in starts:                                     # arm, sensorStart, sensorStop, sarStats, record stop
            self.assertEqual(events[i:i + 5], ["dca:RECORD_START", "cli:sensorStart", "cli:sensorStop",
                                               "cli:sarStats", "dca:RECORD_STOP"])
        first_start = events.index("cli:sensorStart")
        self.assertTrue(any(e.startswith("cli:profileCfg") for e in events[:first_start]))
        self.assertNotIn("cli:sensorStart", events[:first_start])
        for g in (30, 36, 42):
            self.assertTrue(os.path.exists(os.path.join(tmp.name, "point_%d_175-350.cap.sarstats.json" % g)))
        text = W.table(rows)
        self.assertIn("REJECTED", text)
        self.assertIn("clipped", text)

    def test_bad_point_is_skipped_by_the_cfg_checker(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        row = W.run_point(31, "175:350", TP.CFG_TEXT, None, None, None, tmp.name, 1.0, log=lambda m: None)
        self.assertEqual(row["verdict"], "SKIPPED")          # odd gain: sar_cfg_check rejects it before any hardware use

    def test_dry_run_needs_no_hardware(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        cfgp = os.path.join(tmp.name, "b.cfg")
        with open(cfgp, "w") as fh:
            fh.write(TP.CFG_TEXT)
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = W.main([cfgp, os.path.join(tmp.name, "o"), "--gains", "24,30", "--hpf", "175:350,350:700",
                           "--dry-run"])
        self.assertEqual(code, 0)
        self.assertEqual(buf.getvalue().count("cfg ok"), 4)


if __name__ == "__main__":
    unittest.main()
