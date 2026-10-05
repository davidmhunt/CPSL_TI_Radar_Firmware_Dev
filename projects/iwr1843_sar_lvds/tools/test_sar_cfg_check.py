"""Unit tests for sar_cfg_check.py (stdlib unittest, no hardware).

    cd firmware_dev && uv run python -m unittest discover -s projects/iwr1843_sar_lvds/tools -p 'test_*.py'
"""
import argparse
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_cfg_check as chk  # noqa: E402

EXAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "configs", "sar_example_2ms.cfg")

TEMPLATE = """\
% test cfg
dfeDataOutputMode 1
channelCfg 1 1 0
adcCfg 2 1
adcbufCfg -1 0 1 1 1
lowPower 0 0
profileCfg 0 {start} {idle} {adc0} {ramp} 0 0 {slope} 1 {ns} {rate} 0 0 {gain}
chirpCfg 0 0 0 0 0 0 0 1
frameCfg 0 0 {loops} 0 {period} 1 0
analogMonitor 1 0
CQRxSatMonitor 0 3 146 127 0
lvdsStreamCfg -1 1 2 0
calibData 0 0 0
{extra}
"""
GOOD = dict(start="77.25", idle="480", adc0="10", ramp="1520", slope="2.333", ns="3300", rate="2200", gain="30",
            loops="255", period="510.3", extra="")


def make(**kw):
    d = dict(GOOD)
    d.update(kw)
    return TEMPLATE.format(**d)


def run(text, **kw):
    opt = argparse.Namespace(max_range=None, speed=None, dmax=None, tb_min=300.0, if_margin=0.8,
                             lvds_margin=10.0)
    for k, v in kw.items():
        setattr(opt, k, v)
    return chk.analyze(text, opt)


def codes(rep):
    return {e[1:e.index("]")] for e in rep.errors}


class TestPassing(unittest.TestCase):
    def test_template_passes(self):
        rep = run(make(), max_range=100, speed=0.75, dmax=0.00711)
        self.assertEqual(rep.errors, [])

    def test_example_file_values(self):
        with open(EXAMPLE) as fh:
            rep = run(fh.read(), max_range=100, speed=0.75, dmax=0.00711)
        self.assertEqual(rep.errors, [])
        v = rep.values
        self.assertEqual(v["slope_code"], 48)
        self.assertAlmostEqual(v["slope_mhz_us"], 2.3174, places=4)
        self.assertAlmostEqual(v["tc_us"], 2000.0)
        self.assertAlmostEqual(v["tb_us"], 300.0)
        self.assertEqual(v["nchirps"], 255)
        self.assertAlmostEqual(v["frame_period_ms"], 510.3)
        self.assertAlmostEqual(v["boundary_step_us"], 2300.0)
        self.assertAlmostEqual(v["spacing_boundary_m"] * 1e3, 1.725, places=3)
        self.assertEqual(v["bchirp_bytes"], 13328)
        self.assertAlmostEqual(v["mbytes_per_s"], 6.664, places=3)
        self.assertAlmostEqual(v["range_res_m"] * 100, 4.31, places=2)
        self.assertAlmostEqual(v["hpf"]["corner_range_m"]["175"], 11.3, places=1)

    def test_example_file_cli_exit_codes(self):
        import contextlib, io
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            self._cli_exit_codes()

    def _cli_exit_codes(self):
        self.assertEqual(chk.main([EXAMPLE, "--max-range", "100", "--speed", "0.75", "--dmax", "0.00711"]), 0)
        self.assertEqual(chk.main([EXAMPLE, "--speed", "0.75", "--dmax", "0.0015"]), 1)
        self.assertEqual(chk.main([EXAMPLE, "--dmax", "0.0015"]), 2)

    def test_second_derived_cfg(self):
        # Guide section 4 sanity: 50 m, 10 cm, 2 m/s, d_max 5 mm. 1.8 MHz/us -> code 37, 2 Msps x 2000 samples
        # (1000 us window), Tc 2000 us, 3 chirp indices x 200 loops, period 1200.3 ms (Tb 300 us).
        text = make(start="77.0", idle="980", adc0="10", ramp="1020", slope="1.8", ns="2000", rate="2000",
                    loops="200", period="1200.3")
        text = text.replace("chirpCfg 0 0 0 0 0 0 0 1", "chirpCfg 0 2 0 0 0 0 0 1").replace("frameCfg 0 0", "frameCfg 0 2")
        rep = run(text, max_range=50, speed=2.0, dmax=0.005)
        self.assertEqual(rep.errors, [])
        self.assertEqual(rep.values["slope_code"], 37)
        self.assertLess(rep.values["range_res_m"], 0.10)
        self.assertAlmostEqual(rep.values["spacing_boundary_m"] * 1e3, 4.6, places=3)

class TestFailing(unittest.TestCase):
    def test_rate_below_minimum(self):
        self.assertIn("RATE", codes(run(make(rate="1557", ns="2335"))))

    def test_window_past_ramp(self):
        # 2335 samples at 2000 ksps = 1167.5 us + 10 us start > rampEnd 1100
        self.assertIn("ADCWIN", codes(run(make(ns="2335", rate="2000", ramp="1100"))))
        self.assertIn("ADCWIN", codes(run(make(ramp="1100"))))

    def test_adcbuf_overflow(self):
        self.assertIn("ADCBUF", codes(run(make(ns="4100", rate="3000", ramp="1500"))))

    def test_sweep_out_of_band(self):
        self.assertIn("SWEEP", codes(run(make(start="79.5"))))
        self.assertIn("SWEEP", codes(run(make(start="76.5"))))

    def test_period_too_short(self):
        rep = run(make(period="510.1"))
        self.assertIn("TB", codes(rep))
        self.assertAlmostEqual(rep.values["tb_us"], 100.0)

    def test_period_too_long(self):
        self.assertIn("PERIOD", codes(run(make(idle="4000", ramp="1520", period="1400"))))

    def test_spacing(self):
        self.assertIn("SPACING", codes(run(make(), speed=0.75, dmax=0.0015)))

    def test_if_margin(self):
        self.assertIn("IF", codes(run(make(), max_range=150)))

    def test_loops(self):
        self.assertIn("LOOPS", codes(run(make(loops="256"))))

    def test_lvds_margin(self):
        # Tc 50 us (capacity 7500 B/chirp); 800 samples = 3264 B is 43 % of the link: over capacity/10, under capacity
        text = make(idle="10", ramp="40", adc0="0", ns="80", rate="2000", period="20", loops="100")
        self.assertEqual(codes(run(text)) & {"LVDS"}, set())   # 80 samples: 448 B, 6 %: fine
        text = make(idle="10", ramp="40", adc0="0", ns="800", rate="18000", period="20", loops="100")
        rep = run(text)
        self.assertIn("LVDS", codes(rep))
        self.assertNotIn("LVDS", codes(run(text, lvds_margin=1.0)))

    def test_dataformat2_needs_complex_even(self):
        self.assertIn("LVDSCFG", codes(run(make(ns="3301"))))
        text = make().replace("adcbufCfg -1 0 1 1 1", "adcbufCfg -1 1 1 1 1")
        self.assertIn("LVDSCFG", codes(run(text)))
        text = make().replace("adcbufCfg -1 0 1 1 1", "adcbufCfg -1 0 1 0 1")      # interleaved
        self.assertIn("LVDSCFG", codes(run(text)))

    def test_gain_odd(self):
        self.assertIn("GAIN", codes(run(make(gain="31"))))

    def test_stock_demo_commands(self):
        rep = run(make(extra="guiMonitor -1 1 1 0 0 0 1\ncfarCfg -1 0 2 8 4 3 0 15 1"))
        self.assertEqual(sum(1 for e in rep.errors if e.startswith("[CMD]")), 2)

    def test_sw_session_and_threshold(self):
        self.assertIn("LVDSCFG", codes(run(make().replace("lvdsStreamCfg -1 1 2 0", "lvdsStreamCfg -1 1 2 1"))))
        self.assertIn("ADCBUF", codes(run(make().replace("adcbufCfg -1 0 1 1 1", "adcbufCfg -1 0 1 1 2"))))

    def test_advanced_frame_rejected(self):
        self.assertIn("CMD", codes(run(make(extra="advFrameCfg 2 0 0 1 0"))))

    def test_full_cfg_requires_analog_and_calib(self):
        self.assertIn("FULLCFG", codes(run(make().replace("calibData 0 0 0\n", ""))))
        self.assertIn("FULLCFG", codes(run(make().replace("analogMonitor 1 0\n", ""))))

    def test_wrong_argument_count(self):
        self.assertIn("CMD", codes(run(make(extra="channelCfg 1 1"))))


class TestArithmetic(unittest.TestCase):
    def test_slope_quantization_truncates(self):
        rep = run(make(slope="2.333"))
        self.assertEqual(rep.values["slope_code"], 48)
        rep = run(make(slope="2.36"))      # 48.88 -> 48, not 49
        self.assertEqual(rep.values["slope_code"], 48)

    def test_idle_and_ramp_units(self):
        rep = run(make(idle="500", ramp="1500"))
        self.assertAlmostEqual(rep.values["tc_us"], 2000.0)

    def test_max_chirps_per_frame_with_indices(self):
        text = make(loops="223").replace("chirpCfg 0 0 0 0 0 0 0 1",
                                         "chirpCfg 0 2 0 0 0 0 0 1").replace("frameCfg 0 0", "frameCfg 0 2")
        rep = run(text.replace("510.3", "1338.3"))
        self.assertEqual(rep.values["nchirps"], 669)
        self.assertAlmostEqual(rep.values["tb_us"], 300.0)
        self.assertEqual(rep.errors, [])

    def test_tb_open_item_warning(self):
        self.assertTrue(any("TB-OPEN" in w for w in run(make()).warnings))
        self.assertFalse(any("TB-OPEN" in w for w in run(make(period="510.9")).warnings))


if __name__ == "__main__":
    unittest.main()
