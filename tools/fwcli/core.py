"""Shared plumbing: exit codes, output (text or JSON Lines), paths, git, hashing."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

# Appendix A1 exit codes.
OK, FAILED, USAGE, UNSUPPORTED, REFUSED, NOCONFIRM = 0, 1, 2, 3, 4, 5

ROOT = Path(os.environ.get("FW_ROOT") or Path(__file__).resolve().parents[2]).resolve()
PROJECTS = ROOT / "projects"
TEMPLATE = "_template"
FW_DIR = ROOT / ".fw"
SERIAL_DIR = os.environ.get("FW_SERIAL_BYID_DIR", "/dev/serial/by-id")  # test seam
DESCRIPTOR_DIR = ROOT.parent / "CPSL_TI_Radar_cpp" / "config" / "firmware"
SHIPPED_DIR = ROOT.parent / "shipped_firmware"


class FwExit(Exception):
    """Ends the verb with an A1 exit code; main() turns it into the single `result` event."""

    def __init__(self, code: int, reason: str = "", data: dict | None = None):
        super().__init__(reason)
        self.code, self.reason, self.data = code, reason, data or {}


class Cancelled(Exception):
    """SIGINT/SIGTERM received (R15)."""


class Out:
    json = False
    verb = ""
    _done = False
    cancel_note: list = []

    @classmethod
    def progress(cls, stage: str, message: str, percent: int | None = None) -> None:
        if cls.json:
            ev = {"event": "progress", "verb": cls.verb, "stage": stage, "message": message}
            if percent is not None:
                ev["percent"] = percent
            print(json.dumps(ev), flush=True)
        else:
            print(message, flush=True)

    @classmethod
    def say(cls, message: str = "") -> None:
        """Human text: stdout normally, stderr under --json."""
        print(message, file=sys.stderr if cls.json else sys.stdout, flush=True)

    @classmethod
    def warn(cls, message: str) -> None:
        print(f"fw: warning: {message}", file=sys.stderr, flush=True)

    @classmethod
    def result(cls, code: int, reason: str = "", data: dict | None = None) -> None:
        if cls._done:
            return
        cls._done = True
        if cls.json:
            print(json.dumps({"event": "result", "verb": cls.verb, "ok": code == 0, "code": code,
                              "reason": reason, "data": data or {}}), flush=True)
        elif code != 0 and reason:
            print(f"fw: error: {reason}", file=sys.stderr, flush=True)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args: str) -> subprocess.CompletedProcess | None:
    try:
        return subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
    except OSError:
        return None


def git_head() -> str:
    """Short HEAD of firmware_dev; exit 4 when git cannot say (R17: never `unknown`)."""
    r = git("rev-parse", "--short", "HEAD")
    if r is None or r.returncode != 0 or not r.stdout.strip():
        raise FwExit(REFUSED, "git unavailable: cannot determine the firmware_dev commit")
    return r.stdout.strip()


def is_dirty(project: str) -> bool:
    """A7: `git status --porcelain -- projects/<p> fw tools` is non-empty."""
    r = git("status", "--porcelain", "--", f"projects/{project}", "fw", "tools")
    if r is None or r.returncode != 0:
        raise FwExit(REFUSED, "git unavailable: cannot determine whether the tree is dirty")
    return bool(r.stdout.strip())


def rel(p: Path) -> str:
    try:
        return str(p.resolve().relative_to(ROOT))
    except ValueError:
        return str(p)
