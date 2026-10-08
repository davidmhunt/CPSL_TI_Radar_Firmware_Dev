"""R16: verify against a pty fake board; 0 bytes written when skipped."""
import json
import os
import select
import threading
import time

from conftest import DESCRIPTOR


class Board(threading.Thread):
    """Reads commands from the pty master and answers like a CLI; counts bytes received."""

    def __init__(self, master, replies):
        super().__init__(daemon=True)
        self.master, self.replies, self.received, self.stop = master, replies, b"", False

    def run(self):
        buf = b""
        while not self.stop:
            r, _, _ = select.select([self.master], [], [], 0.1)
            if not r:
                continue
            try:
                d = os.read(self.master, 4096)
            except OSError:
                return
            self.received += d
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                reply = self.replies.get(line.strip().decode())
                if reply is not None:
                    os.write(self.master, f"{line.strip().decode()}\r\n{reply}\r\nmmwDemo:/> ".encode())


def start(t, replies):
    port, master = t.port()
    b = Board(master, replies)
    b.start()
    return port, b


def test_verify_pass(built):
    port, b = start(built, {"version": "Platform : xWR99xx\nSDK"})
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    b.stop = True
    assert res["code"] == 0 and res["data"]["outcome"] == "pass"
    pr = res["data"]["probes"][0]
    assert pr["cmd"] == "version" and pr["ok"] and pr["show"]["platform"] == "xWR99xx" and pr["matched"]
    assert b.received == b"version\n"                   # only the descriptor probe, nothing else


def test_verify_fail_on_reject_pattern(built):
    port, b = start(built, {"version": "Platform : xWR99xx BAD"})
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    b.stop = True
    assert res["code"] == 1 and res["data"]["outcome"] == "fail"


def test_verify_no_response_fails_with_1(built):
    port, b = start(built, {})
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    b.stop = True
    assert res["code"] == 1 and res["data"]["outcome"] == "fail" and not res["data"]["probes"][0]["ok"]


def test_verify_picks_the_only_matching_port(built):
    port, b = start(built, {"version": "Platform : xWR99xx"})
    res = built.result(built.run("verify", "proj", "--json"))
    b.stop = True
    assert res["code"] == 0 and res["data"]["port"] == port


def test_verify_held_port_exits_4(built):
    port, b = start(built, {"version": "Platform : xWR99xx"})
    built.hold(port)
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    b.stop = True
    assert res["code"] == 4 and b.received == b""


def test_verify_non_byid_port_exits_4(built):
    other = built.top / "ttyACM0"
    other.write_text("")
    res = built.result(built.run("verify", "proj", "--port", str(other), "--json"))
    assert res["code"] == 4
    port, b = start(built, {})
    res = built.result(built.run("verify", "proj", "--port", str(built.serial / ".." / "by-id" / "usb-board-if00"),
                                 "--json"))
    b.stop = True
    assert res["code"] == 4                              # not the canonical by-id path


def test_verify_no_descriptor_or_no_identify_exits_3(built):
    d = json.loads(json.dumps(DESCRIPTOR))
    d["identify"] = {}
    built.descriptor("dummy", d)
    port, b = start(built, {})
    assert built.result(built.run("verify", "proj", "--port", port, "--json"))["code"] == 3
    (built.top / "CPSL_TI_Radar_cpp" / "config" / "firmware" / "dummy.json").unlink()
    assert built.result(built.run("verify", "proj", "--port", port, "--json"))["code"] == 3
    b.stop = True
    assert b.received == b""


def test_verify_once_safe_false_skips_and_writes_nothing(built):
    d = json.loads(json.dumps(DESCRIPTOR))
    d["identify"]["IWR9999"]["once_safe"] = False
    built.descriptor("dummy", d)
    port, b = start(built, {"version": "x"})
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    time.sleep(0.3)
    b.stop = True
    assert res["code"] == 0 and res["data"]["outcome"] == "skipped" and b.received == b""


def test_verify_never_sends_cfg_commands(built):
    d = json.loads(json.dumps(DESCRIPTOR))
    d["identify"]["IWR9999"]["probes"].append({"cmd": "sensorStart", "require": [], "reject": []})
    built.descriptor("dummy", d)
    port, b = start(built, {})
    res = built.result(built.run("verify", "proj", "--port", port, "--json"))
    b.stop = True
    assert res["code"] == 4 and b.received == b""
