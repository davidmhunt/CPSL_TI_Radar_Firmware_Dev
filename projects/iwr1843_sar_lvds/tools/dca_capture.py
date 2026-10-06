#!/usr/bin/env python3
"""Record one iwr1843_sar_lvds run from the DCA1000, with the sarStats reading that proves it aligned.

    uv run python dca_capture.py OUT --cli-port /dev/ttyACM0 --duration 5     # fully driven
    uv run python dca_capture.py OUT                                          # manual: you type sensorStart etc.

HARDWARE TOOL: talks to the DCA1000 (UDP) and, with --cli-port, to the radar CLI serial port. Run it only when you
mean to. The radar must already hold its cfg (sent by you, or by sar_tune_sweep.py): this tool never sends it.

The capture flow is the capture requirement of docs/lvds_data_format.md section 1, in this order:
  1. configure the DCA1000 (raw mode, LVDS 2-lane, 16-bit, ethernet stream) and open the data socket;
  2. arm it: RECORD_START          (before sensorStart, so the recording holds the run's first byte)
  3. CLI: sensorStart, wait --duration, sensorStop
  4. CLI: sarStats                 (after sensorStop, before any next sensorStart: it holds this run's chirpAvail)
  5. stop recording: RECORD_STOP   (only after sensorStop)
Outputs: OUT (a SARCAP1 file: every datagram with its 10-byte DCA1000 header, 2-byte length prefix added) and
OUT.sarstats.json (chirpAvail, runIdx, the raw sarStats text, and frameEndTimeout = true if the firmware printed
"no BSS frame-end event after sensorStop", which makes the recording a failed one). Parse with sar_parse.py.

DCA1000 command set: TI DCA1000EVM CLI Software Developer Guide (DCA_Programming/Docs/), command codes and the
CONFIG_FPGA_GEN / CONFIG_PACKET_DATA payloads as in CPSL_TI_Radar_cpp/src/DCA1000/DCA1000Commands.cpp and
PacketSource.cpp. Defaults are the DCA1000EVM factory addresses (FPGA 192.168.33.180, host 192.168.33.30, ports
4096 / 4098); this repo's driver configs use others (e.g. 192.168.1.182, 4088 / 4090): pass what your setup uses.
"""
import argparse
import json
import os
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_common as common  # noqa: E402


class Receiver(threading.Thread):
    """Writes every datagram arriving on `sock` to `fh` until stopped. `count` = datagrams written."""

    def __init__(self, sock, fh):
        super().__init__(daemon=True)
        self.sock, self.fh, self.count, self._stop_evt = sock, fh, 0, threading.Event()

    def run(self):
        self.sock.settimeout(0.2)
        while not self._stop_evt.is_set():
            try:
                d, _ = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            common.write_datagram(self.fh, d)
            self.count += 1

    def stop(self):
        self._stop_evt.set()
        self.join(timeout=2.0)


def open_data_socket(host_ip, data_port, rcvbuf_bytes=64 << 20):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf_bytes)
    s.bind((host_ip, data_port))
    return s


def configure_dca(dca, lanes=2, timer_s=30, packet_bytes=1472, delay_us=100, log=print):
    """SYSTEM_CONNECT, RESET_FPGA, CONFIG_PACKET_DATA, CONFIG_FPGA_GEN (raw, 16-bit, ethernet), READ_FPGA_VERSION."""
    dca.send("SYSTEM_CONNECT")
    dca.send("RESET_FPGA")
    dca.send("CONFIG_PACKET_DATA", common.dca_config_packet_data(packet_bytes, delay_us))
    dca.send("CONFIG_FPGA_GEN", common.dca_config_fpga_gen(lanes, timer_s))
    status = dca.send("READ_FPGA_VERSION")
    log("DCA1000 configured (FPGA version word %d)" % status)


def capture_run(dca, data_sock, cli, out_path, duration_s, log=print, drain_s=0.5, manual=None):
    """One recording, in the order the capture requirement demands. `dca` has .send(name, data); `cli` has
    .command(line, timeout) (None = manual: `manual()` is called after arming and must return (chirpAvail, runIdx)
    once the user has run sensorStart ... sensorStop and read sarStats). Returns the sidecar dict."""
    sidecar = {"chirpAvail": None, "runIdx": None, "raw": "", "frameEndTimeout": False, "source": "cli" if cli else "manual"}
    with open(out_path, "wb") as fh:
        common.write_capture_header(fh)
        rx = Receiver(data_sock, fh)
        armed = False
        started = False
        try:
            rx.start()
            dca.send("RECORD_START")                                   # 2. arm before sensorStart
            armed = True
            log("DCA1000 armed")
            if cli is not None:
                out = cli.command("sensorStart", timeout=5.0)          # 3.
                if "Done" not in out:
                    raise RuntimeError("sensorStart failed: %r" % out.strip()[-200:])
                started = True
                log("sensorStart ok; recording %.1f s" % duration_s)
                time.sleep(duration_s)
                stop_out = cli.command("sensorStop", timeout=10.0)
                started = False
                if common.FRAME_END_MSG in stop_out:
                    sidecar["frameEndTimeout"] = True
                    log("WARNING: firmware printed '%s': this recording is a failed one" % common.FRAME_END_MSG)
                time.sleep(drain_s)                                    # let the last datagrams arrive
                text = cli.command("sarStats", timeout=3.0)            # 4. after sensorStop
                stats = common.parse_sarstats(text)
                sidecar["raw"] = text
                if stats is None:
                    raise RuntimeError("could not read chirpAvail from sarStats output: %r" % text.strip()[-200:])
                sidecar.update(chirpAvail=stats["chirpAvail"], runIdx=stats["runIdx"])
            else:
                avail, run = manual()
                sidecar.update(chirpAvail=avail, runIdx=run)
        finally:
            if started and cli is not None:                            # best effort: do not leave the sensor running
                try:
                    cli.command("sensorStop", timeout=10.0)
                except Exception:                                      # noqa: BLE001
                    pass
            if armed:
                try:
                    dca.send("RECORD_STOP")                            # 5. only after sensorStop
                except RuntimeError as exc:
                    log("WARNING: %s" % exc)
            rx.stop()
    sidecar["datagrams"] = rx.count
    with open(out_path + ".sarstats.json", "w") as fh:
        json.dump(sidecar, fh, indent=2)
    log("wrote %s (%d datagrams) and %s.sarstats.json (chirpAvail %s)" % (out_path, rx.count, out_path,
                                                                        sidecar["chirpAvail"]))
    return sidecar


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], epilog="Read docs/tuning_guide.md first.")
    ap.add_argument("out", help="capture file to write (OUT.sarstats.json is written beside it)")
    ap.add_argument("--cli-port", help="radar CLI serial port, e.g. /dev/ttyACM0: the tool then sends sensorStart, "
                                       "sensorStop and sarStats itself (needs --duration)")
    ap.add_argument("--duration", type=float, help="seconds between sensorStart and sensorStop (with --cli-port)")
    ap.add_argument("--fpga-ip", default="192.168.33.180", help="DCA1000 address (default %(default)s)")
    ap.add_argument("--host-ip", default="192.168.33.30", help="this host's address (default %(default)s)")
    ap.add_argument("--cmd-port", type=int, default=4096, help="DCA1000 command port (default %(default)s)")
    ap.add_argument("--data-port", type=int, default=4098, help="data port (default %(default)s)")
    ap.add_argument("--lanes", type=int, choices=(2, 4), default=2, help="LVDS lanes (default 2: IWR1843)")
    ap.add_argument("--timer-s", type=int, default=30,
                    help="CONFIG_FPGA_GEN timer byte, seconds (default 30, as every shipped board descriptor)")
    ap.add_argument("--rcvbuf-mb", type=int, default=64, help="requested SO_RCVBUF in MiB (default 64)")
    return ap


def main(argv=None):
    opt = build_parser().parse_args(argv)
    if opt.cli_port and not opt.duration:
        print("error: --cli-port needs --duration", file=sys.stderr)
        return 2
    cli = common.CliPort(opt.cli_port) if opt.cli_port else None
    dca = common.Dca1000(opt.fpga_ip, opt.host_ip, opt.cmd_port)
    data_sock = open_data_socket(opt.host_ip, opt.data_port, opt.rcvbuf_mb << 20)

    def manual():
        print("\nDCA1000 is armed and recording. Now, on the radar CLI port, in this order:\n"
              "  sensorStart    (wait for the run you want)\n  sensorStop\n  sarStats\n"
              "Then type the numbers from the sarStats output here.")
        avail = int(input("chirpAvail: "))
        run = input("run (first number of the 'run N' line, Enter to skip): ").strip()
        return avail, int(run) if run else None

    try:
        configure_dca(dca, opt.lanes, opt.timer_s)
        capture_run(dca, data_sock, cli, opt.out, opt.duration, manual=None if cli else manual)
    except (RuntimeError, OSError, ValueError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    finally:
        data_sock.close()
        dca.close()
        if cli:
            cli.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
