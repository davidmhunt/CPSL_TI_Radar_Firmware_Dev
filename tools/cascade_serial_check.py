#!/usr/bin/env python3
"""Bring-up check for a TI mmWave demo over UART (no TI visualizer needed).

Sends a chirp cfg over the CLI port, checks every command answers "Done", then reads the data
port and validates TLV frames (magic word, header, frame-number continuity, frame rate).

Board-tested on the AWR2243 2-chip cascade demo only. The TLV frame header it parses is the same
in the mmWave SDK 3.x single-chip demos, and the frameCfg period parse accepts both the cascade
(9-argument) and SDK 3.x (7-argument) forms, but it has not been run against an SDK 3.x board;
pass --data-baud to match the demo (SDK 3.x demos use 921600).

From firmware_dev/ (the `flash` service passes the host serial ports through):

    docker compose run --rm flash python3 /build_context/tools/cascade_serial_check.py \
        --cli /dev/ttyUSB0 --data /dev/ttyUSB1 \
        --cfg /build_context/projects/awr2243_cascade_ddm/configs/cascade_shortrange.cfg

or on the host (needs pyserial): tools/cascade_serial_check.py --cli ... --data ... --cfg ...

The demo cannot be reconfigured after sensorStart (TI known issue): power-cycle the EVM
between runs. Use --skip-config to only listen on the data port of an already-running board.
"""
import argparse
import struct
import sys
import time

import serial

MAGIC = b"\x02\x01\x04\x03\x06\x05\x08\x07"
HEADER_FMT = "<8sIIIIIIII"  # magic, version, totalPacketLen, platform, frameNumber,
HEADER_LEN = struct.calcsize(HEADER_FMT)  # timeCpuCycles, numDetectedObj, numTLVs, subFrameNumber
TLV_NAMES = {1: "points", 6: "stats", 7: "side_info", 9: "temperature", 10: "tracker", 104: "compact"}


def send_config(port, baud, cfg_path, timeout_s):
    with open(cfg_path) as f:
        lines = [l.strip() for l in f if l.strip() and not l.strip().startswith("%")]

    with serial.Serial(port, baud, timeout=0.1) as cli:
        cli.reset_input_buffer()
        for line in lines:
            cli.write((line + "\n").encode())
            resp = b""
            deadline = time.monotonic() + timeout_s
            while time.monotonic() < deadline:
                resp += cli.read(256)
                if b"Done" in resp or b"Error" in resp or b"not recognized" in resp:
                    break
            text = resp.decode(errors="replace")
            if "Done" not in text:
                print(f"FAIL  {line[:60]}\n      response: {text.strip()!r}")
                return False
            print(f"Done  {line[:60]}")
    return True


def read_frames(port, baud, duration_s, expected_period_ms):
    frames, errors, gaps = [], 0, 0
    buf = b""
    with serial.Serial(port, baud, timeout=0.1) as data:
        data.reset_input_buffer()
        end = time.monotonic() + duration_s
        while time.monotonic() < end:
            buf += data.read(4096)
            while True:
                idx = buf.find(MAGIC)
                if idx < 0:
                    buf = buf[-(len(MAGIC) - 1):]
                    break
                if idx > 0:
                    if frames:  # junk between frames (startup junk before the first one is expected)
                        errors += 1
                    buf = buf[idx:]
                if len(buf) < HEADER_LEN:
                    break
                (_, version, total_len, platform, frame_num, _, num_obj, num_tlvs,
                 _) = struct.unpack_from(HEADER_FMT, buf)
                if total_len < HEADER_LEN or total_len > 1 << 20:
                    errors += 1
                    buf = buf[len(MAGIC):]
                    continue
                if len(buf) < total_len:
                    break
                packet, buf = buf[:total_len], buf[total_len:]

                tlvs, off, ok = [], HEADER_LEN, True
                for _ in range(num_tlvs):
                    if off + 8 > total_len:
                        ok = False
                        break
                    t, length = struct.unpack_from("<II", packet, off)
                    tlvs.append((t, length))
                    off += 8 + length
                if not ok or off > total_len:
                    errors += 1
                    continue

                t_rx = time.monotonic()
                if frames and frame_num != frames[-1][1] + 1:
                    gaps += 1
                frames.append((t_rx, frame_num))
                desc = ", ".join(f"{TLV_NAMES.get(t, t)}:{l}B" for t, l in tlvs)
                print(f"frame {frame_num:6d}  platform=0x{platform:x}  len={total_len:5d}  "
                      f"points={num_obj:4d}  tlvs=[{desc}]")

    print()
    if not frames:
        print(f"FAIL  no frames received on {port} at {baud} baud")
        return False
    span = frames[-1][0] - frames[0][0]
    rate = (len(frames) - 1) / span if span > 0 else 0.0
    print(f"{len(frames)} frames, {rate:.2f} Hz measured, {gaps} frame-number gaps, {errors} framing errors")
    if expected_period_ms:
        print(f"expected {1000.0 / expected_period_ms:.2f} Hz from frameCfg")
    return gaps == 0 and errors == 0


def frame_period_ms(cfg_path):
    with open(cfg_path) as f:
        for line in f:
            parts = line.split()
            # SDK 3.x (7 args): frameCfg <start> <end> <loops> <frames> <periodMs> <trigSel> <trigDelay>
            #   e.g. xwr18xx/mmw/profiles/profile_2d.cfg: frameCfg 0 1 32 0 100 1 0
            # cascade (9 args): period is the 6th argument
            #   e.g. projects/awr2243_cascade_ddm/configs/*.cfg: frameCfg 0 7 32 0 192 50 1 0 2
            if parts and parts[0] == "frameCfg":
                if len(parts) >= 10:
                    return float(parts[6])
                if len(parts) > 5:
                    return float(parts[5])
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cli", help="CLI (Application/User UART) port")
    p.add_argument("--data", required=True, help="data UART port")
    p.add_argument("--cfg", help="chirp cfg to send")
    p.add_argument("--cli-baud", type=int, default=115200)
    p.add_argument("--data-baud", type=int, default=3125000)
    p.add_argument("--duration", type=float, default=10.0, help="seconds to read the data port")
    p.add_argument("--cmd-timeout", type=float, default=5.0, help="seconds to wait for each 'Done'")
    p.add_argument("--skip-config", action="store_true", help="don't send a cfg; only read frames")
    args = p.parse_args()

    if not args.skip_config:
        if not (args.cli and args.cfg):
            p.error("--cli and --cfg are required unless --skip-config is given")
        if not send_config(args.cli, args.cli_baud, args.cfg, args.cmd_timeout):
            return 1
        print()

    period = frame_period_ms(args.cfg) if args.cfg else None
    return 0 if read_frames(args.data, args.data_baud, args.duration, period) else 1


if __name__ == "__main__":
    sys.exit(main())
