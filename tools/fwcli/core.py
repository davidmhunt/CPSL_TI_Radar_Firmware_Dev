"""Shared plumbing: exit codes, output (text or JSON Lines), paths, git, hashing."""
from __future__ import annotations

import hashlib
import json
import os
import re
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


_HEADER = re.compile(r"^\s*\[\[?\s*([^\]\s]+(?:\s*\.\s*[^\]\s]+)*)\s*\]\]?\s*(#.*)?$")
_STATUS = re.compile(r"^status\s*=")


def _strip_record_keys(text: str) -> list[str]:
    """project.toml as text lines minus the A7 record areas: everything inside `[[bench]]` tables and the
    `status = ...` line of `[project]`. Every other byte (comments, quote style, ordering) is kept, because
    build.sh reads `key = "value"` lines with sed. Blank lines before a `[[bench]]` header are dropped so
    that adding a bench block is not a diff; blank lines after it are kept."""
    kept: list[str] = []
    section, in_bench = "", False
    for ln in text.splitlines():
        m = _HEADER.match(ln)
        if m:
            in_bench = ln.strip().startswith("[[") and m.group(1) == "bench"
            section = m.group(1)
            if in_bench:
                while kept and not kept[-1].strip():
                    kept.pop()
                continue
        elif in_bench:
            if not ln.strip():
                kept.append(ln)  # trailing blanks of a bench block stay
            continue
        elif section == "project" and _STATUS.match(ln):
            continue
        kept.append(ln)
    while kept and not kept[-1].strip():
        kept.pop()
    return kept


def toml_same_ignoring_records(project: str, old_rev: str, new_rev: str | None) -> bool:
    """A7 exemption: projects/<p>/project.toml differs between old_rev and new_rev (None = working tree)
    only in `[[bench]]` entries and/or `[project].status`."""
    path = f"projects/{project}/project.toml"
    a = git("show", f"{old_rev}:{path}")
    if a is None or a.returncode != 0:
        return False
    if new_rev is None:
        try:
            new_text = (ROOT / path).read_text()
        except OSError:
            return False
    else:
        b = git("show", f"{new_rev}:{path}")
        if b is None or b.returncode != 0:
            return False
        new_text = b.stdout
    x, y = _strip_record_keys(a.stdout), _strip_record_keys(new_text)
    return x == y


def changed_since(project: str, commit: str) -> bool:
    """A7 stale test: projects/<p>, fw or tools changed between `commit` and HEAD, except a project.toml
    change confined to `[[bench]]` / `[project].status`."""
    r = git("diff", "--name-only", commit, "HEAD", "--", f"projects/{project}", "fw", "tools")
    if r is None or r.returncode != 0:
        return True
    files = [ln for ln in r.stdout.splitlines() if ln.strip()]
    if not files:
        return False
    if files == [f"projects/{project}/project.toml"]:
        return not toml_same_ignoring_records(project, commit, "HEAD")
    return True


def is_dirty(project: str) -> bool:
    """A7: `git status --porcelain -- projects/<p> fw tools` is non-empty, except uncommitted edits confined
    to `[[bench]]` / `[project].status` in project.toml."""
    r = git("status", "--porcelain", "--", f"projects/{project}", "fw", "tools")
    if r is None or r.returncode != 0:
        raise FwExit(REFUSED, "git unavailable: cannot determine whether the tree is dirty")
    lines = [ln for ln in r.stdout.splitlines() if ln.strip()]
    if not lines:
        return False
    path = f"projects/{project}/project.toml"
    if len(lines) == 1 and lines[0][3:] == path and lines[0][:2].strip() == "M":
        return not toml_same_ignoring_records(project, "HEAD", None)
    return True


def rel(p: Path) -> str:
    try:
        return str(p.resolve().relative_to(ROOT))
    except ValueError:
        return str(p)
