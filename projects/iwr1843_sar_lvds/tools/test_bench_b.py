"""Unit tests for bench_b.py (Set B bench commands) and the streaming analyzer bench_stream.py: synthetic captures,
fake serial / DCA1000, no hardware.

    cd firmware_dev && uv run --group tools pytest projects/iwr1843_sar_lvds/tools/test_bench_b.py
"""
import argparse
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_a as A  # noqa: E402
import bench_b as B  # noqa: E402
import bench_run as R  # noqa: E402
import bench_stream as BS  # noqa: E402
import sar_parse as P  # noqa: E402
import sar_synth as S  # noqa: E402
from test_bench_a import Base, build_packets, stats_text, write_cap  # noqa: E402
from test_bench_run import ev  # noqa: E402

common = R.common
CFG_TEXT = S.make_cfg()                       # ns 1024, 20 chirps per frame
CFG = common.cfg_params(CFG_TEXT)
N = 1200                                      # >= 1000 packets, the other-slot regime gate


def range_of_bin(cfg, b):
    slope = abs(cfg["slope_mhz_us"]) * 1e12
    return b * (cfg["fs_ksps"] * 1e3) / cfg["ns"] * B.C / (2 * slope)


class StreamParity(Base):
    """The streaming analyzer gives the parser's verdict and numbers on every synthetic scenario."""

    def check(self, name, n=200, batch=8, slack=4, **kw):
        dg, stats = S.scenario(name, CFG, n, sat_chirps=(37,), **kw)
        cap = os.path.join(self.tmp, name + ".cap")
        S.write_capture(cap, dg, stats)
        res = P.analyze(dg, CFG, stats)
        r = BS.analyze_stream(cap, CFG, stats, batch=batch, slack=slack)
        self.assertEqual(r["accepted"], res["accepted"], name)
        self.assertEqual(r["failing"], res["failing"], name)
        self.assertEqual([c.status.split(" (")[0] for c in r["checks"]], [c.status.split(" (")[0] for c in res["checks"]], name)
        self.assertEqual(list(r["valid"]), list(res["valid"]), name)
        self.assertEqual(list(r["adc_ok"]), list(res["adc_ok"]), name)
        self.assertEqual(r["absent"], res["absent"], name)
        self.assertEqual(int((r["sat_lag"] > 0).sum()), P.sat_summary(res)[0], name)
        self.assertEqual(sorted(r["d_in"].round(9)), sorted(round(x, 9) for x in res["d_in"]), name)
        return r, res

    def test_scenarios(self):
        for name in ("clean", "late_one_packet", "mid_packet_start", "lost_datagram", "lost_final_datagram", "two_runs",
                     "duplicate_datagram", "no_sarstats"):
            if name == "no_sarstats":
                continue                                  # the bench always has sarStats
            self.check(name)

    def test_default_window_sizes(self):
        self.check("clean", n=300, batch=64, slack=32)

    def test_seq_gap_counts_dropped_datagram(self):
        r, _ = self.check("lost_datagram")
        self.assertEqual(r["seq_gaps"], 1)
        r, _ = self.check("clean")
        self.assertEqual(r["seq_gaps"], 0)

    def test_regime_and_byte_order_counts(self):
        r = BS.analyze_stream(self.write(build_packets(CFG, 300, regime=-1)), CFG, {"chirpAvail": 300, "runIdx": 1})
        self.assertEqual(set(r["regime_in"]), {-1})
        self.assertEqual(r["counts"]["hsi_raw_ok"], r["counts"]["hsi_raw_eval"])
        r = BS.analyze_stream(self.write(build_packets(CFG, 300, regime=1)), CFG, {"chirpAvail": 300, "runIdx": 1})
        self.assertEqual(set(r["regime_in"]), {1})
        self.assertEqual(set(r["regime_last"]), {-1})

    def write(self, packets, name="x.cap"):
        cap = os.path.join(self.tmp, name)
        write_cap(cap, packets, "")
        return cap


class Steps(Base):
    def run_steps(self, **kw):
        cap = os.path.join(self.tmp, "s.cap")
        write_cap(cap, build_packets(CFG, 400, **kw), "")
        r = BS.analyze_stream(cap, CFG, {"chirpAvail": 400, "runIdx": 1}, reflector_range=range_of_bin(CFG, 200))
        return r, BS.phase_steps(r)

    def test_static_reflector_has_no_step(self):
        r, ps = self.run_steps()
        self.assertEqual(r["bref"], 200)
        self.assertLess(abs(ps["phi"]["step"]), ps["phi"]["p99_in"])
        self.assertLess(ps["phi"]["frac_over"], 0.12)

    def test_phase_step_at_boundaries_is_seen(self):
        r, ps = self.run_steps(phase_step=0.3)            # +0.3 rad at every frame start
        self.assertGreater(ps["phi"]["step"], 0.2)
        self.assertGreater(ps["phi"]["frac_over"], 0.9)
        cfg = CFG
        G, _ = B.judge_long(r, {"chirpAvail": 400, "raw": stats_text(400, 20)}, cfg, argparse.Namespace(range=range_of_bin(CFG, 200)),
                            400 / 20 * 0.0403, [("after sensorStop", A.parse_stats(stats_text(400, 20)))], ps)
        self.assertFalse(all(ok for _, ok in G["G3 phase continuity"]))
        self.assertFalse(all(ok for _, ok in G["G6 APLL/SYNTH boundary"]))

    def test_low_snr_is_inconclusive_not_pass(self):
        r, ps = self.run_steps(amp=3.0, sigma=40.0)
        G, _ = B.judge_long(r, {"chirpAvail": 400, "raw": stats_text(400, 20)}, CFG, argparse.Namespace(range=None),
                            400 / 20 * 0.0403, [("after sensorStop", A.parse_stats(stats_text(400, 20)))], ps)
        self.assertFalse(all(ok for _, ok in G["G3 phase continuity"]))


class SatJudge(Base):
    def capture(self, sat_chirps):
        cap = os.path.join(self.tmp, "t.cap")
        write_cap(cap, build_packets(CFG, 300, sat_chirps=sat_chirps), "")
        return BS.analyze_stream(cap, CFG, {"chirpAvail": 300, "runIdx": 1})

    def test_agreement_with_and_without_lag(self):
        sats = tuple(range(40, 300, 7))
        r = self.capture(sats)
        ag = B.agreement(r)
        self.assertEqual(r["adc_clip"].sum(), len(sats))
        self.assertEqual(ag["lag applied"]["acc"], 1.0)
        self.assertEqual(ag["lag applied"]["recall"], 1.0)
        self.assertLess(ag["no lag"]["acc"], 0.9)
        self.assertEqual(ag["lag fixed at 1"]["acc"], 1.0)                 # the synthetic records have lag 1

    def points(self, vals, top_agree=True):
        pts = []
        for g, (fw, par, adc) in zip((36, 40, 44, 48), vals):
            pts.append((g, dict(fw=fw, parser=par, adc=adc, agree=None)))
        if top_agree:
            r = self.capture(tuple(range(40, 300, 7)))
            pts[-1][1]["agree"] = B.agreement(r)
        return pts

    def test_judge_sat_pass_and_fail_modes(self):
        good = self.points([(0, 0, 0), (0, 0, 0), (4, 4, 4), (37, 37, 37)])
        self.assertTrue(all(ok for _, ok in B.judge_sat(good, 36)), B.judge_sat(good, 36))
        decreasing = self.points([(0, 0, 0), (5, 5, 5), (4, 4, 4), (37, 37, 37)])
        self.assertFalse(all(ok for _, ok in B.judge_sat(decreasing, 36)))
        nonzero_tuned = self.points([(1, 1, 1), (2, 2, 2), (4, 4, 4), (37, 37, 37)])
        self.assertFalse(all(ok for _, ok in B.judge_sat(nonzero_tuned, 36)))
        no_clip_top = self.points([(0, 0, 0), (0, 0, 0), (0, 0, 0), (0, 0, 0)])
        self.assertFalse(all(ok for _, ok in B.judge_sat(no_clip_top, 36)))
        fw_mismatch = self.points([(0, 0, 0), (0, 0, 0), (4, 4, 4), (60, 37, 37)])
        self.assertFalse(all(ok for _, ok in B.judge_sat(fw_mismatch, 36)))

    def test_gain_ladder(self):
        self.assertEqual(B.gain_ladder(30), [30, 34, 38, 42, 46])
        self.assertEqual(B.gain_ladder(40), [32, 36, 40, 44, 48])
        self.assertEqual(B.gain_ladder(48), [32, 36, 40, 44, 48])
        self.assertEqual(len(B.gain_ladder(24)), 7)


class Small(unittest.TestCase):
    def test_judge_polls(self):
        good = {"polls": [{"t": 60, "raw": stats_text(30000, 255, isr=30001)}, {"t": 120, "raw": stats_text(60000, 255)}]}
        self.assertTrue(all(ok for _, ok in B.judge_polls(good)))
        dead = {"polls": [{"t": 60, "raw": stats_text(30000, 255, isr=0)}]}
        self.assertFalse(all(ok for _, ok in B.judge_polls(dead)))
        self.assertFalse(all(ok for _, ok in B.judge_polls({})))
        late = {"polls": [{"t": 60, "raw": stats_text(30000, 255, late=1)}]}
        self.assertFalse(all(ok for _, ok in B.judge_polls(late)))

    def test_choose_and_table(self):
        rows = [dict(gain=24, hpf="175:350", verdict="ok", union=0, snr=20.0, note="", adc_clip=0, fw_sat=0, peak=-20.0, noise=-80.0, rng=1.5),
                dict(gain=36, hpf="175:350", verdict="ok", union=3, snr=30.0, note="", adc_clip=3, fw_sat=2, peak=0.0, noise=-70.0, rng=1.5),
                dict(gain=30, hpf="350:700", verdict="ok", union=0, snr=25.0, note="", adc_clip=0, fw_sat=0, peak=-9.0, noise=-76.0, rng=1.5),
                dict(gain=42, hpf="350:700", verdict="REJECTED", union=None, snr=None, note="failing 2", adc_clip=None, fw_sat=None,
                     peak=None, noise=None, rng=None)]
        self.assertEqual([(x["gain"], x["hpf"]) for x in B.choose(rows)], [(30, "350:700"), (24, "175:350")])
        self.assertIn("REJECTED", B.tune_table(rows))

    def test_with_frame_period_in_tb(self):
        lines = A.with_frame_period(R.bc.cfg_lines(R.DEFAULT_CFG, drop_last=True), 200)
        cfg = common.cfg_params("\n".join(lines + ["sensorStart"]) + "\n")
        self.assertAlmostEqual(cfg["tb_us"], 500.0, places=3)

    def test_capture_run_polls_sarstats(self):
        import dca_capture

        class Cli:
            def __init__(self):
                self.sent = []

            def command(self, line, timeout=2.0):
                self.sent.append(line)
                return "chirps 1"
        cli, side, got = Cli(), {}, []
        dca_capture._wait(cli, 0.5, 0.2, lambda t, raw: got.append(round(t, 1)), side)
        self.assertEqual(len(side["polls"]), 2)
        self.assertEqual(cli.sent, ["sarStats", "sarStats"])
        self.assertEqual(got, [0.2, 0.4])
        side = {}
        dca_capture._wait(cli, 0.1, None, None, side)                                  # no polling: just waits
        self.assertEqual(side, {})

    def test_resolve_adc_bits(self):
        self.assertEqual(B.resolve_adc_bits(16, lambda *_: None), 16)


class Flows(Base):
    def setUp(self):
        super().setUp()
        self.patch(B, "WORKDIR", self.tmp)
        self.patch(B, "ADC_BITS_FILE", os.path.join(self.tmp, "adc_bits.txt"))
        self.cfg_path = os.path.join(self.tmp, "syn.cfg")
        with open(self.cfg_path, "w") as fh:
            fh.write(CFG_TEXT)
        self.lines = []
        self.patch(A, "configure_show", lambda dev, lines, out=print: self.lines.append(lines) or True)
        self.patch(B, "cfg_errors", lambda text: [])
        self.final = stats_text(N, 20)
        self.patch(A, "read_stats", lambda dev: self.final)

    def ns(self, **kw):
        d = dict(cfg=self.cfg_path, range=range_of_bin(CFG, 200), gain=30, hpf="175:350", timer_s=30, adc_bits=12,
                 duration=N / 20 * 0.0403, poll_s=60.0, precheck_s=0, reads=2, gap=4.0, add_us=100)
        d.update(kw)
        return argparse.Namespace(**d)

    def fake_polled(self, packets):
        def polled(dev, cap, duration, timer_s, poll_s, out=print):
            polls = [{"t": 1.0, "raw": stats_text(600, 20, isr=601)}, {"t": 2.0, "raw": stats_text(1000, 20, isr=1000)}]
            side = write_cap(cap, packets, self.final, {"polls": polls})[1]
            side["raw"] = self.final
            return side
        self.patch(B, "polled_capture", polled)

    def test_endurance_pass(self):
        self.fake_polled(build_packets(CFG, N, regime=-1))
        out = []
        ok = B.cmd_endurance("dev", self.ns(), out.append)
        self.assertTrue(ok, "\n".join(out))
        for g in ("G1 throughput", "G2 timing", "G3 phase continuity", "G4 data sanity", "G6 APLL/SYNTH boundary", "G7 per-chirp metadata"):
            self.assertTrue(any(l.startswith("RESULT %s: PASS" % g) for l in out), g)
        self.assertTrue(any("is clean" in l for l in out))
        self.assertEqual(out[-1], "RESULT: PASS")
        self.assertEqual(len(self.lines), 2)                                  # cfg for the run + cfg re-sent before the last read

    def test_endurance_step_gives_followup_and_fail(self):
        self.fake_polled(build_packets(CFG, N, regime=-1, phase_step=0.3))
        out = []
        self.assertFalse(B.cmd_endurance("dev", self.ns(), out.append))
        self.assertTrue(any("./bench tb --add-us 100" in l for l in out))
        self.assertEqual(out[-1], "RESULT: FAIL")

    def test_endurance_udp_gap_fails_g1(self):
        packets = build_packets(CFG, N, regime=-1)

        def polled(dev, cap, duration, timer_s, poll_s, out=print):
            dg = S.to_datagrams(packets)
            del dg[len(dg) // 2]
            side = {"chirpAvail": N, "runIdx": 1, "raw": self.final, "frameEndTimeout": False, "source": "cli",
                    "polls": [{"t": 1.0, "raw": stats_text(600, 20)}, {"t": 2.0, "raw": stats_text(1000, 20)}]}
            S.write_capture(cap, dg, side)
            return side
        self.patch(B, "polled_capture", polled)
        out = []
        self.assertFalse(B.cmd_endurance("dev", self.ns(), out.append))
        self.assertTrue(any(l.startswith("RESULT G1 throughput: FAIL") for l in out))

    def test_endurance_isr_moves_after_stop_fails_g7(self):
        self.fake_polled(build_packets(CFG, N, regime=-1))
        self.patch(A, "read_stats", lambda dev: stats_text(N + 1, 20))
        out = []
        self.assertFalse(B.cmd_endurance("dev", self.ns(), out.append))
        self.assertTrue(any(l.startswith("RESULT G7") and "FAIL" in l for l in out))

    def soak_ns(self, **kw):
        d = dict(cfg=self.cfg_path, timer_s=30, duration=N / 20 * 0.0403, poll_s=60.0, precheck_s=0, reads=2, gap=4.0)
        d.update(kw)
        return argparse.Namespace(**d)

    def test_soak_pass_marks_reflector_items_not_evaluated(self):
        self.fake_polled(build_packets(CFG, N, regime=-1))
        self.patch(B, "free_gb", lambda p: 100.0)
        out = []
        ok = B.cmd_soak("dev", self.soak_ns(), out.append)
        self.assertTrue(ok, "\n".join(out))
        for g in ("G1 throughput", "G2 timing", "G7 per-chirp metadata"):
            self.assertTrue(any(l.startswith("RESULT %s: PASS" % g) for l in out), g)
        text = "\n".join(out)
        for g in ("G3", "G4", "G6"):
            self.assertIn("%s" % g, text)
            self.assertFalse(any(l.startswith("RESULT " + g) for l in out), g)
        self.assertEqual(sum(1 for l in out if "NOT EVALUATED (no reflector) -> firmware-18" in l), len(B.NOT_EVALUATED))
        self.assertTrue(out[-1].startswith("RESULT: PASS"))
        with open(os.path.join(self.tmp, "soak_summary.json")) as fh:
            d = json.load(fh)
        self.assertEqual(d["result"], "PASS")
        self.assertEqual(d["udp_seq_gaps"], 0)
        self.assertEqual(d["chirps"], N)
        self.assertIn("G3 phase continuity", d["not_evaluated"])
        with open(os.path.join(self.tmp, "soak_summary.txt")) as fh:
            self.assertLessEqual(len(fh.read().splitlines()), 40)
        self.assertEqual(len(self.lines), 2)

    def test_soak_udp_gap_fails_g1(self):
        packets = build_packets(CFG, N, regime=-1)

        def polled(dev, cap, duration, timer_s, poll_s, out=print):
            dg = S.to_datagrams(packets)
            del dg[len(dg) // 2]
            side = {"chirpAvail": N, "runIdx": 1, "raw": self.final, "frameEndTimeout": False, "source": "cli",
                    "polls": [{"t": 1.0, "raw": stats_text(600, 20)}, {"t": 2.0, "raw": stats_text(1000, 20)}]}
            S.write_capture(cap, dg, side)
            return side
        self.patch(B, "polled_capture", polled)
        self.patch(B, "free_gb", lambda p: 100.0)
        out = []
        self.assertFalse(B.cmd_soak("dev", self.soak_ns(), out.append))
        self.assertTrue(any(l.startswith("RESULT G1 throughput: FAIL") for l in out))
        self.assertTrue(out[-1].startswith("RESULT: FAIL"))

    def test_soak_late_isr_in_a_poll_fails_g2(self):
        packets = build_packets(CFG, N, regime=-1)

        def polled(dev, cap, duration, timer_s, poll_s, out=print):
            polls = [{"t": 1.0, "raw": stats_text(600, 20, isr=601).replace("lateIsr 0", "lateIsr 3")},
                     {"t": 2.0, "raw": stats_text(1000, 20, isr=1000)}]
            side = write_cap(cap, packets, self.final, {"polls": polls})[1]
            side["raw"] = self.final
            return side
        self.patch(B, "polled_capture", polled)
        self.patch(B, "free_gb", lambda p: 100.0)
        out = []
        self.assertFalse(B.cmd_soak("dev", self.soak_ns(), out.append))
        self.assertTrue(any(l.startswith("RESULT G2 timing: FAIL") for l in out))

    def test_soak_isr_moves_after_stop_fails_g7(self):
        self.fake_polled(build_packets(CFG, N, regime=-1))
        self.patch(B, "free_gb", lambda p: 100.0)
        self.patch(A, "read_stats", lambda dev: stats_text(N + 1, 20))
        out = []
        self.assertFalse(B.cmd_soak("dev", self.soak_ns(), out.append))
        self.assertTrue(any(l.startswith("RESULT G7") and "FAIL" in l for l in out))

    def test_soak_aborts_cleanly_when_disk_short(self):
        self.patch(B, "free_gb", lambda p: 3.0)
        self.patch(B, "polled_capture", lambda *a, **k: self.fail("must not capture"))
        out = []
        self.assertFalse(B.cmd_soak("dev", self.soak_ns(), out.append))
        self.assertEqual(self.lines, [])
        self.assertTrue(out[-1].startswith("RESULT: ABORTED"))

    def test_soak_needs_no_range_gain_hpf(self):
        import bench_run
        a = bench_run.argparse.ArgumentParser()
        sub = a.add_subparsers(dest="cmd")
        B.register(sub)
        ns = a.parse_args(["soak"])
        self.assertEqual((ns.duration, ns.poll_s, ns.precheck_s), (600.0, 60.0, 30.0))
        self.assertFalse(hasattr(ns, "range") or hasattr(ns, "gain"))

    def test_tb_clean_and_steps(self):
        def cap_only(dev, cap, cfgp, duration, extra=(), out=print):
            cfg = common.cfg_params(open(cfgp).read())
            self.assertAlmostEqual(cfg["tb_us"], 400.0, places=3)             # 300 + 100
            return write_cap(cap, build_packets(CFG, 400, regime=-1), self.final)[1]
        self.patch(A, "capture_only", cap_only)
        out = []
        self.assertTrue(B.cmd_tb("dev", self.ns(duration=1.0), out.append) is not None)
        self.assertTrue(any("frame period" in l for l in out))

    def test_sat_pass(self):
        sat_by_gain = {32: (), 36: (), 40: (), 44: tuple(range(50, 300, 40)), 48: tuple(range(40, 300, 7))}
        caps = {}

        def cap_only(dev, cap, cfgp, duration, extra=(), out=print):
            g = int(os.path.basename(cfgp)[1:-4])
            sats = sat_by_gain[g]
            n_fw = len([c for c in sats if c < 299])
            text = stats_text(300, 20, sat=n_fw)
            return write_cap(cap, build_packets(CFG, 300, sat_chirps=sats), text)[1]
        self.patch(A, "capture_only", cap_only)
        out = []
        ok = B.cmd_sat("dev", self.ns(gain=40, gains=None, points=5, duration=30.0), out.append)
        self.assertTrue(ok, "\n".join(out))
        self.assertTrue(any("gain 48 dB: fw saturatedChirps 37, parser 37, ADC-clipped 38" in l for l in out))

    def test_sat_top_point_not_clipping_fails(self):
        def cap_only(dev, cap, cfgp, duration, extra=(), out=print):
            return write_cap(cap, build_packets(CFG, 300), stats_text(300, 20))[1]
        self.patch(A, "capture_only", cap_only)
        self.assertFalse(B.cmd_sat("dev", self.ns(gain=40, gains=None, points=5, duration=30.0), lambda *_: None))

    def test_sat_gain_must_be_in_list(self):
        self.assertFalse(B.cmd_sat("dev", self.ns(gain=40, gains="30,44", points=5, duration=30.0), lambda *_: None))

    def test_self_check_matches_parser(self):
        cap = os.path.join(self.tmp, "p.cap")
        write_cap(cap, build_packets(CFG, 300, sat_chirps=(50, 120)), "")
        e = R.evaluate(cap, self.cfg_path)
        r = BS.analyze_stream(cap, CFG, e["side"])
        self.assertEqual(B.self_check(e, r), [])

    def test_adc_bits_file_written_by_adc_command(self):
        got = []
        res = {"cfg": {"B": 4 * 8 + 64, "H": 0, "nrx": 1, "ns": 8, "swap": 0}, "n_chirps": 1, "adc_ok": [True],
               "stream": bytearray(__import__("array").array("h", [30000, -32768] * 8).tobytes() + bytes(64))}
        R.judge_adc(res, lambda *_: None, got)
        self.assertEqual(got, [16])


class TuneFlow(Base):
    def test_tune_with_fake_hardware(self):
        import dca_capture
        import sar_tune_sweep as sw
        cfg_text = S.make_cfg(ns=3300, nc=20, gain=30)
        cfg = common.cfg_params(cfg_text)
        cfg_path = os.path.join(self.tmp, "t.cfg")
        with open(cfg_path, "w") as fh:
            fh.write(cfg_text)
        self.patch(B, "WORKDIR", self.tmp)
        packets = build_packets(cfg, 60, tone_bin=200)

        class Fake:
            def close(self):
                pass

            def command(self, line, timeout=2.0):
                return "Done\nmmwDemo:/>"
        self.patch(common, "CliPort", lambda dev: Fake())
        self.patch(common, "Dca1000", lambda *a: Fake())
        self.patch(dca_capture, "open_data_socket", lambda *a, **k: Fake())
        self.patch(dca_capture, "configure_dca", lambda *a, **k: None)

        def capture_run(dca, sock, cli, out_path, duration, log=print, **kw):
            return write_cap(out_path, packets, "")[1]
        self.patch(dca_capture, "capture_run", capture_run)
        out = []
        a = argparse.Namespace(cfg=cfg_path, range=1.5, gains="30,36", hpf="175:350", duration=3.0, adc_bits=12)
        ok = B.cmd_tune("dev", a, out.append)
        text = "\n".join(out)
        self.assertTrue(ok, text)
        self.assertIn("CHOSEN: gain", text)
        self.assertIn("./bench endurance --range M --gain", text)


if __name__ == "__main__":
    unittest.main()
