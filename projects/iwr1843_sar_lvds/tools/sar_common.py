"""Shared helpers for the iwr1843_sar_lvds host tools (stdlib only).

Used by dca_capture.py, sar_parse.py, sar_tune_report.py and sar_tune_sweep.py:

  * the capture file format (raw DCA1000 datagrams, UDP headers kept, each prefixed by a 2-byte length);
  * `cfg_params`: the numbers a parser needs from a cfg (via sar_cfg_check.py);
  * `parse_sarstats`: the firmware's `sarStats` text -> chirpAvail, runIdx;
  * `CliPort`: the radar CLI serial port (termios, no pyserial);
  * `Dca1000`: the DCA1000 command channel (UDP).

Nothing here runs at import time: no port is opened and no packet is sent until a tool constructs CliPort/Dca1000.
"""
import os
import re
import socket
import struct
import sys
import time
import types

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sar_cfg_check as chk  # noqa: E402

# --- capture file --------------------------------------------------------------------------------------------------
# File = 8-byte magic, then per datagram: u16 little-endian length, then the datagram exactly as received (10-byte
# DCA1000 header + payload). The length prefix is the only thing added; it lets the parser find datagram boundaries.
CAP_MAGIC = b"SARCAP1\n"
UDP_HDR = 10                       # u32 sequence number + u48 byte count, little-endian (ARCHITECTURE.md)


def write_capture_header(fh):
    fh.write(CAP_MAGIC)


def write_datagram(fh, dgram):
    fh.write(struct.pack("<H", len(dgram)) + dgram)


def read_capture(path):
    """Return the list of datagrams (bytes) in a capture file."""
    with open(path, "rb") as fh:
        data = fh.read()
    if not data.startswith(CAP_MAGIC):
        raise ValueError("%s: not a SARCAP1 capture (written by dca_capture.py; datagram headers kept)" % path)
    out, pos = [], len(CAP_MAGIC)
    while pos + 2 <= len(data):
        (n,) = struct.unpack_from("<H", data, pos)
        pos += 2
        if pos + n > len(data):
            break                                   # truncated file tail (capture killed mid-write): dropped
        out.append(data[pos:pos + n])
        pos += n
    return out


def split_header(dgram):
    """(sequence number, byte count, payload) of one DCA1000 datagram."""
    seq = struct.unpack_from("<I", dgram, 0)[0]
    count = int.from_bytes(dgram[4:10], "little")
    return seq, count, dgram[UDP_HDR:]


# --- cfg -----------------------------------------------------------------------------------------------------------
def cfg_params(text):
    """What a parser needs from a cfg. Raises ValueError if the cfg is not a usable dataFmt 2 SAR cfg."""
    opt = types.SimpleNamespace(max_range=None, speed=None, dmax=None, tb_min=300.0, if_margin=0.8, lvds_margin=10.0)
    rep = chk.analyze(text, opt)
    v = dict(rep.values)
    for key in ("ns", "nrx", "tc_us", "tb_us", "nchirps"):
        if key not in v:
            raise ValueError("cfg has no usable profile/chirp/frame (sar_cfg_check: %s)" % "; ".join(rep.errors[:2]))
    hdr, swap, fmt = 1, 0, None
    for _, cmd, args in chk.parse_cfg(text):
        if cmd == "lvdsStreamCfg" and len(args) == 4:
            hdr, fmt = chk.to_int(args[1]), chk.to_int(args[2])
        if cmd == "adcbufCfg" and len(args) == 5:
            swap = chk.to_int(args[2])
    if fmt != 2:
        raise ValueError("cfg lvdsStreamCfg dataFmt is %r, this tool needs 2 (ADC + metadata)" % fmt)
    r, ns = v["nrx"], v["ns"]
    m = (0 if not hdr else (64 if (r * ns) % 4 == 0 else 56)) + 4 * r * ns
    v.update(hdr_on=bool(hdr), swap=swap, H=m - 4 * r * ns, M=m, B=m + 64, cfg_errors=list(rep.errors),
             cfg_warnings=list(rep.warnings), tc_s=v["tc_us"] * 1e-6, tb_s=v["tb_us"] * 1e-6)
    return v


# --- sarStats ------------------------------------------------------------------------------------------------------
FRAME_END_MSG = "no BSS frame-end event after sensorStop"


def parse_sarstats(text):
    """Parse the CLI `sarStats` output (mmw_sar_meta.c MmwDemo_sarMetaPrintStats). None if chirpAvail is absent."""
    m_run = re.search(r"\brun (\d+)", text)
    m_avail = re.search(r"\bchirpAvail (\d+)", text)
    if not m_avail:
        return None
    out = {"chirpAvail": int(m_avail.group(1)), "runIdx": int(m_run.group(1)) if m_run else None}
    for key in ("chirps", "frames", "chirpStartIsr", "lateIsr", "missedChirpIsr", "saturatedChirps"):
        m = re.search(r"\b%s (\d+)" % key, text)
        if m:
            out[key] = int(m.group(1))
    return out


# --- radar CLI serial port -----------------------------------------------------------------------------------------
class CliPort:
    """Radar CLI over a serial device, 115200 8N1, raw, via termios (Linux). Opens the device when constructed."""

    PROMPT = "mmwDemo:/>"

    def __init__(self, dev, baud=115200):
        import termios
        self._termios = termios
        self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0                                            # iflag
        attrs[1] = 0                                            # oflag
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
        attrs[3] = 0                                            # lflag: raw
        speed = getattr(termios, "B%d" % baud)
        attrs[4] = attrs[5] = speed
        attrs[6][termios.VMIN], attrs[6][termios.VTIME] = 0, 1
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        termios.tcflush(self.fd, termios.TCIOFLUSH)

    def close(self):
        os.close(self.fd)

    def command(self, line, timeout=2.0):
        """Send one CLI line, return everything printed until the prompt (or timeout)."""
        self._termios.tcflush(self.fd, self._termios.TCIFLUSH)
        os.write(self.fd, (line.strip() + "\n").encode())
        buf, end = "", time.time() + timeout
        while time.time() < end:
            chunk = os.read(self.fd, 4096)
            if chunk:
                buf += chunk.decode(errors="replace")
                if self.PROMPT in buf:
                    break
        return buf


# --- DCA1000 command channel ---------------------------------------------------------------------------------------
DCA_HEADER, DCA_FOOTER = 0xA55A, 0xEEAA
DCA_CMD = dict(RESET_FPGA=0x1, RECORD_START=0x5, RECORD_STOP=0x6, SYSTEM_CONNECT=0x9, CONFIG_FPGA_GEN=0x3,
               CONFIG_PACKET_DATA=0xB, READ_FPGA_VERSION=0xE)


def dca_command(code, data=b""):
    """One DCA1000 command: u16 header 0xA55A, u16 code, u16 data length, data, u16 footer 0xEEAA (little-endian).
    Layout as in CPSL_TI_Radar_cpp/src/DCA1000/DCA1000Commands.cpp and the TI DCA1000EVM CLI developer guide
    (DCA_Programming/Docs/TI_DCA1000EVM_CLI_Software_DeveloperGuide.pdf)."""
    return struct.pack("<HHH", DCA_HEADER, code, len(data)) + data + struct.pack("<H", DCA_FOOTER)


def dca_config_fpga_gen(lanes=2, timer_s=30):
    """CONFIG_FPGA_GEN payload: raw logging (1), LVDS lanes (1 = 4-lane, 2 = 2-lane), LVDS capture (1), ethernet
    stream (2), 16-bit format (3), timer. Same bytes as UdpPacketSource::send_configFPGAGen."""
    return bytes([0x01, 0x01 if lanes == 4 else 0x02, 0x01, 0x02, 0x03, timer_s & 0xFF])


def dca_config_packet_data(packet_bytes=1472, delay_us=100):
    return struct.pack("<HHH", packet_bytes, delay_us, 0)


class Dca1000:
    """DCA1000 over UDP. `send` raises RuntimeError on a missing or non-zero response. Opens the cmd socket when
    constructed; the data socket is opened by the caller (dca_capture.py)."""

    def __init__(self, fpga_ip, host_ip, cmd_port):
        self.addr = (fpga_ip, cmd_port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.sock.bind((host_ip, cmd_port))

    def send(self, name, data=b""):
        self.sock.sendto(dca_command(DCA_CMD[name], data), self.addr)
        try:
            resp, _ = self.sock.recvfrom(64)
        except socket.timeout:
            raise RuntimeError("DCA1000 did not answer %s" % name)
        status = resp[4] | (resp[5] << 8) if len(resp) >= 6 else -1
        if name != "READ_FPGA_VERSION" and status != 0:
            raise RuntimeError("DCA1000 rejected %s (status %d)" % (name, status))
        return status

    def close(self):
        self.sock.close()
