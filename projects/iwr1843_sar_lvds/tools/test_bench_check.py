"""Unit tests for bench_check.py with a fake CLI port: no serial port, no hardware.

    cd firmware_dev && uv run pytest projects/iwr1843_sar_lvds/tools/test_bench_check.py
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_check as B  # noqa: E402

P = "\nmmwDemo:/>"


def stats(chirps, frames, isr):
    return ("sarStats\r\nrun 1 (sensor state 2), dataFmt 2, satMon 1\r\nchirps %d frames %d chirpStartIsr %d "
            "chirpAvail 5\r\nlateIsr 0 missedChirpIsr 0\r\n" % (chirps, frames, isr)) + P


class Fake:
    def __init__(self, replies):
        self.replies, self.sent = list(replies), []

    def command(self, line, timeout=2.0):
        self.sent.append(line)
        r = self.replies.pop(0)
        return r(line) if callable(r) else r


def cfgfile(text):
    f = tempfile.NamedTemporaryFile("w", suffix=".cfg", delete=False)
    f.write(text)
    f.close()
    return f.name


class Tests(unittest.TestCase):
    def run_cfg(self, replies, no_start=False):
        path = cfgfile("% c\nsensorStop\nflushCfg\n\nsensorStart\n")
        self.addCleanup(os.unlink, path)
        port, out = Fake(replies), []
        return B.run_cfg(port, path, no_start, out.append), port, "\n".join(out)

    def test_cfg_pass_and_no_start(self):
        ok, port, _ = self.run_cfg([lambda l: l + "\r\nDone" + P] * 2, no_start=True)
        self.assertTrue(ok)
        self.assertEqual(port.sent, ["sensorStop", "flushCfg"])

    def test_cfg_stops_at_error(self):
        ok, port, text = self.run_cfg([lambda l: l + "\r\nDone" + P, "flushCfg\r\nError -3" + P, "x"])
        self.assertFalse(ok)
        self.assertEqual(port.sent, ["sensorStop", "flushCfg"])
        self.assertIn("FAIL", text)

    def test_status_pass_and_fail(self):
        run = lambda s2: B.run_status(Fake(["Sensor State: 2" + P, stats(10, 1, 10), s2]), 0, lambda *_: None)
        self.assertTrue(run(stats(500, 3, 500)))
        self.assertFalse(run(stats(10, 1, 10)))
        self.assertFalse(run(stats(500, 3, 499)))

    def test_status_not_running(self):
        self.assertFalse(B.run_status(Fake(["Sensor State: 1" + P, stats(1, 1, 1), stats(2, 2, 2)]), 0,
                                      lambda *_: None))

    def test_stop(self):
        self.assertTrue(B.run_stop(Fake(["sensorStop\r\nDone" + P]), lambda *_: None))
        bad = "sensorStop\r\n" + B.common.FRAME_END_MSG + P
        self.assertFalse(B.run_stop(Fake([bad]), lambda *_: None))

    def test_port_detect_explicit(self):
        self.assertEqual(B.find_cli_port("/dev/x"), "/dev/x")


if __name__ == "__main__":
    unittest.main()
