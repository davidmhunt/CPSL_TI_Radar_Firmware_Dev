"""Flash gates G1-G7 (A6) as small functions; flash.py sequences them.

Old `gated_flash()` line mapping is recorded in the fwstd-03 directive Log."""
from __future__ import annotations

import fnmatch
import os
import re
import shutil
import subprocess
from pathlib import Path

from .core import ROOT, SERIAL_DIR, REFUSED, FwExit

PHRASE = "FLASH MODE CONFIRMED"
ACK_PHRASE = "USE THIS PORT"


def g1_inside(img: Path) -> None:
    """G1: the image lives inside firmware_dev/."""
    if not str(img).startswith(str(ROOT) + os.sep):
        raise FwExit(REFUSED, f"image must be inside firmware_dev/ (got {img})")


def g2_exists(port: str) -> str:
    """G2: the port exists; returns the resolved device path."""
    if not os.path.exists(port):
        raise FwExit(REFUSED, f"serial port {port} not found (try: ls -l {SERIAL_DIR}/)")
    return os.path.realpath(port)


def holders(real: str) -> str:
    """PIDs holding `real` (fuser/lsof only scan /proc; they never open the port)."""
    if shutil.which("fuser"):
        r = subprocess.run(["fuser", real], capture_output=True, text=True)
        return r.stdout.strip()
    if shutil.which("lsof"):
        r = subprocess.run(["lsof", "-t", "--", real], capture_output=True, text=True)
        return r.stdout.strip()
    raise FwExit(REFUSED, "neither fuser nor lsof is installed, so I cannot check that the port is free; "
                          "install psmisc or lsof")


def g3_free(real: str) -> None:
    """G3: no process holds the port."""
    h = holders(real)
    if h.strip():
        raise FwExit(REFUSED, f"{real} is held by process(es): {h}; close the serial monitor / driver / "
                              f"ModemManager that has it open, then retry")


def is_byid(port: str) -> bool:
    """G4 predicate: <serial by-id dir>/...-if00."""
    return os.path.normpath(port) == port and re.fullmatch(re.escape(SERIAL_DIR) + r"/.+-if00", port) is not None


def checklist_text(steps: list[str]) -> list[str]:
    """G5: the manifest gate's checklist, numbered."""
    return ["Before flashing, confirm ALL of these:"] + [f"  {i}. {s}" for i, s in enumerate(steps, 1)]


def list_ports(glob: str | None = None) -> list[dict]:
    d = Path(SERIAL_DIR)
    out = []
    if d.is_dir():
        for p in sorted(d.iterdir()):
            s = str(p)
            if glob and not fnmatch.fnmatch(s, glob if "/" in glob else f"*{glob}"):
                continue
            real = os.path.realpath(s)
            try:
                held = bool(holders(real).strip())
            except FwExit:
                held = False
            out.append({"path": s, "held": held})
    return out
