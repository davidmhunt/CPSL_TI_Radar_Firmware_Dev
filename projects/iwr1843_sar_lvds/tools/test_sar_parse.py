"""Unit tests for sar_parse.py, dca_capture.py and sar_common.py: synthetic captures only, stdlib, no hardware.

    cd firmware_dev && uv run python -m unittest projects/iwr1843_sar_lvds/tools/test_sar_parse.py

Each capture-requirement case asserts the printed per-check result and the verdict (docs/lvds_data_format.md
section 1). The synthetic run (sar_synth.py) follows the format doc: slot k mod 2 holds chirp k's own record, the other
slot still holds chirp k-1's, saturation lags one chirp.
"""
import csv
import io
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dca_capture  # noqa: E402
import sar_common as common  # noqa: E402
import sar_parse as P  # noqa: E402
import sar_synth as S  # noqa: E402

def slurp(path, mode="r"):
    with open(path, mode) as fh:
        return fh.read()


CFG_TEXT = S.make_cfg()
CFG = common.cfg_params(CFG_TEXT)
N = 200
SAT = (37,)


def run(name, n=N, **kw):
    dg, stats = S.scenario(name, CFG, n, sat_chirps=SAT, **kw)
    res = P.analyze(dg, CFG, stats)
    return res, P.render(res)


def status(res, num):
    return res["checks"][num - 1].status


class Geometry(unittest.TestCase):
    def test_packet_size_matches_cfg_checker(self):
        # H 64 + 4 x 1 x 1024 + 64 = 4224; the checker's bchirp_bytes uses the same formula
        self.assertEqual((CFG["H"], CFG["M"], CFG["B"]), (64, 4160, 4224))
        self.assertEqual(CFG["bchirp_bytes"], CFG["B"])
        ex = common.cfg_params(slurp(os.path.join(HERE, "..", "configs", "sar_example_2ms.cfg")))
        self.assertEqual(ex["B"], 13328)                      # lvds_data_format.md: R=1, Ns=3300, header on


class CaptureRequirement(unittest.TestCase):
    def test_clean_accepted(self):
        res, text = run("clean")
        self.assertTrue(res["accepted"])
        for i in (1, 2, 3, 4):
            self.assertEqual(status(res, i), "PASS")
            self.assertIn("check %d (" % i, text)
        self.assertIn("VERDICT: ACCEPTED", text)
        self.assertIn("200 of 200 records validate", text)

    def test_one_packet_late_rejected_by_checks_3_and_4(self):
        # stream starts at packet 1; the other slot holds chirp k-1, so every record validates (check 2 passes)
        res, text = run("late_one_packet")
        self.assertEqual(status(res, 2), "PASS")
        self.assertEqual(status(res, 3), "FAIL")
        self.assertEqual(status(res, 4), "FAIL")
        self.assertEqual(res["failing"], [3, 4])
        self.assertIn("check 3 (no chirp follows the last packet): FAIL", text)
        self.assertIn("VERDICT: REJECTED: capture requirement not met (failing: check 3, check 4)", text)
        self.assertIn("Capture requirement", text)

    def test_mid_packet_start_rejected_by_check_2(self):
        res, text = run("mid_packet_start")
        self.assertEqual(status(res, 2), "FAIL")
        self.assertIn("0 of 199 records validate", text)
        self.assertFalse(res["accepted"])
        self.assertIn("REJECTED", text)

    def test_one_lost_datagram_accepted_chirp_marked_no_shift(self):
        res, text = run("lost_datagram")
        self.assertTrue(res["accepted"], text)
        lost = [k for k, ok in enumerate(res["adc_ok"]) if not ok]
        self.assertEqual(len(lost), 1)
        self.assertTrue(all(res["valid"]))                      # records elsewhere still validate: nothing shifted
        self.assertIn("ADC partly lost 1", text)
        k = lost[0]
        self.assertEqual(res["recs"][k][6], k)                  # the affected chirp's record is still its own

    def test_final_datagram_lost_tail_hole_accepted_when_check_3_evaluable(self):
        # a tiny final datagram that lies after the other slot (packet N-1 odd -> other slot is slot 0)
        n = next(n for n in range(100, 400, 2) if 1 <= (n * CFG["B"]) % 1462 <= 32)
        res, text = run("lost_final_datagram", n)
        self.assertEqual(status(res, 3), "PASS", text)
        self.assertEqual(status(res, 4), "PASS")
        self.assertIn("tail hole", text)
        self.assertTrue(res["accepted"], text)

    def test_final_datagram_covering_record_slots_defers_to_check_4_and_is_accepted(self):
        res, text = run("lost_final_datagram")                  # the lost datagram holds the last 1226 B
        self.assertEqual(status(res, 4), "PASS")                # F2: a tail hole does not false-reject check 4
        self.assertEqual(status(res, 3), "NOT EVALUABLE (tail hole)")
        self.assertTrue(res["accepted"], text)                  # F3: rely on check 4
        self.assertIn("check 3 (no chirp follows the last packet): NOT EVALUABLE (tail hole)", text)
        self.assertIn("VERDICT: ACCEPTED", text)

    def test_check_3_not_evaluable_with_check_4_failing_rejects(self):
        dg, stats = S.scenario("lost_final_datagram", CFG, N)
        res = P.analyze(dg[:-3], CFG, stats)                    # three datagrams lost: beyond the tail tolerance
        self.assertEqual(status(res, 3), "NOT EVALUABLE (tail hole)")
        self.assertEqual(status(res, 4), "FAIL")
        self.assertFalse(res["accepted"])
        self.assertIn("failing: check 3, check 4", P.render(res))

    def test_typed_chirp_avail_never_defers_check_3_triple_fault_rejects(self):
        # one packet late + lost final datagram + chirpAvail typed one too small: with the deferral this was accepted
        # one chirp off (other-slot regime k-1). Typed counts are not proof: not evaluable rejects.
        pk = S.build_run(CFG, N, sat_chirps=SAT)
        dg = S.to_datagrams(pk[1:])[:-1]
        res = P.analyze(dg, CFG, {"chirpAvail": N - 1, "runIdx": 1, "source": "manual"})
        self.assertEqual(status(res, 3), "NOT EVALUABLE (tail hole)")
        self.assertFalse(res["accepted"])
        # the same typed value on a clean tail-hole recording also rejects; machine-read sarStats still defers
        dg2, stats = S.scenario("lost_final_datagram", CFG, N)
        self.assertFalse(P.analyze(dg2, CFG, dict(stats, source="manual"))["accepted"])
        self.assertTrue(P.analyze(dg2, CFG, dict(stats, source="cli"))["accepted"])

    def test_long_run_plus_short_second_run_rejected_by_runidx_check_2(self):
        res, text = run("two_runs")
        self.assertEqual(status(res, 2), "FAIL")
        self.assertIn("another runIdx", text)
        self.assertFalse(res["accepted"])

    def test_two_runs_with_sarstats_of_the_second_run(self):
        dg, _ = S.scenario("two_runs", CFG, N)
        res = P.analyze(dg, CFG, {"chirpAvail": 5, "runIdx": 2})
        self.assertFalse(res["accepted"])
        self.assertEqual(status(res, 2), "FAIL")

    def test_no_sarstats_check_4_fails(self):
        res, text = run("no_sarstats")
        self.assertEqual(status(res, 4), "FAIL")
        self.assertIn("no sarStats reading", text)
        self.assertFalse(res["accepted"])
        self.assertEqual(res["failing"], [4])

    def test_exact_duplicate_datagram_accepted(self):
        res, text = run("duplicate_datagram")
        self.assertTrue(res["accepted"], text)
        self.assertIn("1 exact duplicate", text)

    def test_overlapping_datagrams_fail_check_1(self):
        dg, stats = S.scenario("clean", CFG, N)
        bad = bytearray(dg[10])
        bad[4:10] = (int.from_bytes(dg[10][4:10], "little") + 100).to_bytes(6, "little")   # overlaps its neighbour
        dg[10] = bytes(bad)
        res = P.analyze(dg, CFG, stats)
        self.assertEqual(status(res, 1), "FAIL")
        self.assertFalse(res["accepted"])

    def test_joined_recordings_restart_at_zero_fail_check_1(self):
        a, stats = S.scenario("clean", CFG, N)
        b = S.to_datagrams(S.build_run(CFG, 50, run_idx=2, seed=3))
        res = P.analyze(a + b, CFG, stats)
        self.assertEqual(status(res, 1), "FAIL")
        self.assertIn("restart", res["checks"][0].detail)

    def test_reordered_datagrams_are_fine(self):
        dg, stats = S.scenario("clean", CFG, N)
        dg[20], dg[21] = dg[21], dg[20]
        dg[0], dg[1] = dg[1], dg[0]
        res = P.analyze(dg, CFG, stats)
        self.assertTrue(res["accepted"], P.render(res))

    def test_firmware_frame_end_timeout_is_a_failed_recording(self):
        dg, stats = S.scenario("clean", CFG, N)
        stats = dict(stats, frameEndTimeout=True)
        res = P.analyze(dg, CFG, stats)
        self.assertFalse(res["accepted"])
        self.assertIn("recording flow: FAIL", P.render(res))

    def test_a_few_failed_records_are_tolerated_many_are_not(self):
        pk = S.build_run(CFG, N, sat_chirps=SAT)
        B, M = CFG["B"], CFG["M"]
        bad = list(pk)
        for k in (10, 11):                                      # corrupt two records: allowed max(1, 1 %) = 2
            b = bytearray(bad[k]); b[M + 32 * (k % 2):M + 32 * (k % 2) + 4] = b"\0\0\0\0"; bad[k] = bytes(b)
        res = P.analyze(S.to_datagrams(bad), CFG, {"chirpAvail": N, "runIdx": 1})
        self.assertEqual(status(res, 2), "PASS")
        self.assertFalse(res["valid"][10])
        for k in (30, 31, 32):
            b = bytearray(bad[k]); b[M + 32 * (k % 2):M + 32 * (k % 2) + 4] = b"\0\0\0\0"; bad[k] = bytes(b)
        res = P.analyze(S.to_datagrams(bad), CFG, {"chirpAvail": N, "runIdx": 1})
        self.assertEqual(status(res, 2), "FAIL")
        self.assertIn("firmware lost count", res["checks"][1].detail)

    def test_no_resync_by_magic_in_parser_source(self):
        src = slurp(os.path.join(HERE, "sar_parse.py"))
        code = "\n".join(ln for ln in src.splitlines() if not ln.lstrip().startswith("#"))
        for needle in (".find(", ".index(", "re.search", "re.finditer", "memmem"):
            self.assertNotIn(needle, code)


class Outputs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.cfgpath = os.path.join(self.tmp.name, "run.cfg")
        with open(self.cfgpath, "w") as fh:
            fh.write(CFG_TEXT)

    def capture(self, name, n=N):
        path = os.path.join(self.tmp.name, name + ".cap")
        dg, stats = S.scenario(name, CFG, n, sat_chirps=SAT)
        S.write_capture(path, dg, stats)
        return path

    def main(self, *args):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = P.main(list(args))
        return code, buf.getvalue()

    def test_accepted_writes_aligned_adc_and_meta(self):
        path = self.capture("clean")
        code, out = self.main(path, "--cfg", self.cfgpath)
        self.assertEqual(code, 0, out)
        prefix = os.path.splitext(path)[0]
        raw = slurp(prefix + "_adc.bin", "rb")
        self.assertEqual(len(raw), N * 4 * CFG["ns"])
        # chirp 5 matches what the synthetic device sent (I,Q order after the SampleSwap fix)
        pk = S.build_run(CFG, N, sat_chirps=SAT)
        want = pk[5][CFG["H"]:CFG["H"] + 4 * CFG["ns"]]
        q_i = struct.unpack("<%dh" % (2 * CFG["ns"]), want)    # device order is Q, I (SampleSwap 1)
        got = struct.unpack("<%dh" % (2 * CFG["ns"]), raw[5 * 4 * CFG["ns"]:6 * 4 * CFG["ns"]])
        self.assertEqual(list(got[0::2]), list(q_i[1::2]))     # I
        self.assertEqual(list(got[1::2]), list(q_i[0::2]))     # Q
        rows = list(csv.DictReader(io.StringIO(slurp(prefix + "_meta.csv"))))
        self.assertEqual(len(rows), N)
        self.assertEqual(rows[37]["sat_slices_this_chirp"], "5")   # reported by chirp 38's record, lag 1
        self.assertEqual(rows[36]["sat_slices_this_chirp"], "0")
        self.assertEqual(rows[N - 1]["sat_slices_this_chirp"], "")  # the last chirp's result is never delivered
        self.assertEqual(rows[0]["sat_slices_this_chirp"], "0")   # chirp 0 is reported by chirp 1
        self.assertEqual(rows[39]["t_interpolated"], "0")
        # saturated chirp counted once, delta-t statistics match the cfg
        self.assertIn("saturated chirps 1", out)
        self.assertIn("in-frame dt: n=190 mean 2000.00 us (+0.00 vs cfg 2000.00)", out)
        self.assertIn("boundary dt: n=9 mean 2300.00 us (+0.00 vs cfg 2300.00)", out)

    def test_rejected_writes_nothing_without_force(self):
        path = self.capture("late_one_packet")
        code, out = self.main(path, "--cfg", self.cfgpath)
        prefix = os.path.splitext(path)[0]
        self.assertEqual(code, 1)
        self.assertFalse(os.path.exists(prefix + "_adc.bin"))
        self.assertFalse(os.path.exists(prefix + "_meta.csv"))
        self.assertIn("no aligned output written", out)
        self.assertIn("Capture requirement", out)

    def test_force_writes_with_loud_warning(self):
        path = self.capture("late_one_packet")
        code, out = self.main(path, "--cfg", self.cfgpath, "--force")
        prefix = os.path.splitext(path)[0]
        self.assertEqual(code, 1)
        self.assertTrue(os.path.exists(prefix + "_adc.bin"))
        self.assertTrue(os.path.exists(prefix + "_FORCED_REJECTED.txt"))
        self.assertIn("WARNING: --force", out)

    def test_missing_capture_is_a_usage_error(self):
        code, _ = self.main(os.path.join(self.tmp.name, "nope.cap"), "--cfg", self.cfgpath)
        self.assertEqual(code, 2)

    def test_chirp_avail_flag_replaces_sidecar(self):
        path = self.capture("no_sarstats")
        code, out = self.main(path, "--cfg", self.cfgpath, "--chirp-avail", str(N), "--run-idx", "1")
        self.assertEqual(code, 0, out)


class SarStatsText(unittest.TestCase):
    TEXT = ("run 3 (sensor state 2), dataFmt 2, satMon 1\n"
            "chirps 5100 frames 20 chirpStartIsr 5100 chirpAvail 5100\n"
            "saturatedChirps 4\nlateIsr 0 missedChirpIsr 0 frameResync 0 availResync 0\n"
            "cbuffErrIrq 0 cbuffChirpErr 0 cbuffFrameStartErr 0 (error bits sticky since boot), lvdsFramesDone 20\n"
            "tsTicks 0x0000000a1b2c3d4e (100000000 Hz)\nmmwDemo:/>")

    def test_parse(self):
        s = common.parse_sarstats(self.TEXT)
        self.assertEqual((s["chirpAvail"], s["runIdx"], s["saturatedChirps"]), (5100, 3, 4))
        self.assertIsNone(common.parse_sarstats("Error: nothing"))


class FakeDca:
    def __init__(self, events):
        self.events = events

    def send(self, name, data=b""):
        self.events.append("dca:" + name)
        return 0


class FakeCli:
    """Radar CLI stand-in: after sensorStart the 'DCA1000' (this object) sends the run's datagrams to the data socket."""

    def __init__(self, events, data_addr, datagrams, stats_text, stop_text="Done\nmmwDemo:/>"):
        self.events, self.addr, self.datagrams, self.stats_text, self.stop_text = (events, data_addr, datagrams,
                                                                                  stats_text, stop_text)

    def command(self, line, timeout=2.0):
        self.events.append("cli:" + line)
        if line == "sensorStart":
            tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            for i, d in enumerate(self.datagrams):
                tx.sendto(d, self.addr)
                if i % 20 == 0:
                    time.sleep(0.001)
            tx.close()
            return "Done\nmmwDemo:/>"
        if line == "sensorStop":
            return self.stop_text
        if line == "sarStats":
            return self.stats_text
        return "Done\nmmwDemo:/>"


class CaptureFlow(unittest.TestCase):
    def flow(self, stop_text="Done\nmmwDemo:/>", scenario="clean"):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        dg, stats = S.scenario(scenario, CFG, N)
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
        sock.bind(("127.0.0.1", 0))
        self.addCleanup(sock.close)
        text = "run 1 (sensor state 0), dataFmt 2, satMon 1\nchirps %d frames 10 chirpStartIsr %d chirpAvail %d\n" % (
            N, N, N)
        events = []
        cli = FakeCli(events, sock.getsockname(), dg, text, stop_text)
        out = os.path.join(tmp.name, "run.cap")
        side = dca_capture.capture_run(FakeDca(events), sock, cli, out, 0.3, log=lambda m: None, drain_s=0.3)
        return events, side, out

    def test_order_arm_before_start_stop_after_stop_stats_between(self):
        events, side, out = self.flow()
        self.assertEqual(events, ["dca:RECORD_START", "cli:sensorStart", "cli:sensorStop", "cli:sarStats",
                                  "dca:RECORD_STOP"])
        self.assertEqual(side["chirpAvail"], N)
        stored = json.loads(slurp(out + ".sarstats.json"))
        self.assertEqual((stored["chirpAvail"], stored["runIdx"]), (N, 1))
        res = P.analyze(common.read_capture(out), CFG, stored)
        self.assertTrue(res["accepted"], P.render(res))

    def test_frame_end_message_marks_recording_failed(self):
        events, side, out = self.flow("no BSS frame-end event after sensorStop\nDone\nmmwDemo:/>")
        stored = json.loads(slurp(out + ".sarstats.json"))
        self.assertTrue(stored["frameEndTimeout"])
        res = P.analyze(common.read_capture(out), CFG, stored)
        self.assertFalse(res["accepted"])

    def test_dca_command_bytes(self):
        # header 0xA55A, code, length, data, footer 0xEEAA, little-endian (DCA1000 developer guide; DCA1000Commands.cpp)
        self.assertEqual(common.dca_command(0x9), bytes.fromhex("5aa509000000aaee"))
        self.assertEqual(common.dca_config_fpga_gen(2, 30), bytes([1, 2, 1, 2, 3, 30]))
        self.assertEqual(common.dca_command(0x3, common.dca_config_fpga_gen(2, 30)),
                         bytes.fromhex("5aa503000600" "01020102031e" "aaee"))


class Help(unittest.TestCase):
    def test_every_tool_has_help(self):
        import subprocess
        for tool in ("dca_capture.py", "sar_parse.py", "sar_tune_report.py", "sar_tune_sweep.py", "sar_synth.py"):
            p = subprocess.run([sys.executable, os.path.join(HERE, tool), "--help"], capture_output=True, text=True)
            self.assertEqual(p.returncode, 0, tool + p.stderr)
            self.assertIn("usage", p.stdout.lower())


if __name__ == "__main__":
    unittest.main()
