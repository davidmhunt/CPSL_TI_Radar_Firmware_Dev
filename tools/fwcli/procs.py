"""Child processes with cancellation (R15) and the docker compose seam."""
from __future__ import annotations

import contextlib
import os
import re
import shlex
import signal
import subprocess
from pathlib import Path

from .core import ROOT, Cancelled, FwExit, Out, FAILED

_children: list[subprocess.Popen] = []


def install_signal_handlers() -> None:
    def handler(signum, frame):
        raise Cancelled(signal.Signals(signum).name)
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)


def kill_children() -> None:
    for p in list(_children):
        if p.poll() is None:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                with contextlib.suppress(ProcessLookupError, PermissionError):
                    os.killpg(p.pid, sig)
                try:
                    p.wait(timeout=5)
                    break
                except subprocess.TimeoutExpired:
                    continue
        _children.remove(p)


def compose_cmd() -> list[str]:
    """`docker compose`, or FW_COMPOSE (test seam: a fake compose script)."""
    return shlex.split(os.environ.get("FW_COMPOSE", "")) or ["docker", "compose"]


def compose_args(service: str, script: str, args: list[str], extra_env: dict[str, str],
                 commit: str | None) -> list[str]:
    cmd = compose_cmd() + ["run", "--rm", "--user", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/tmp/fwhome", "-T"]
    env = dict(extra_env)
    if commit:
        env["FW_COMMIT"] = commit
    for k, v in os.environ.items():  # CCS_CONFIG and any FW_* variable pass through
        if (k == "CCS_CONFIG" or k.startswith("FW_")) and k not in ("FW_COMMIT", "FW_ROOT", "FW_COMPOSE"):
            env.setdefault(k, v)
    for k, v in env.items():
        cmd += ["-e", f"{k}={v}"]
    cmd += [service, "bash", "-c", 'mkdir -p "$HOME" && exec "$0" "$@"', script, *args]
    return cmd


def run_streaming(argv: list[str], stage: str, verb_lines=None) -> tuple[int, str]:
    """Run argv in its own session, stream output lines as progress; return (rc, output).

    On Cancelled the child's process group is killed before the exception propagates."""
    p = subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
    _children.append(p)
    lines: list[str] = []
    try:
        for line in p.stdout:
            line = line.rstrip("\n")
            lines.append(line)
            m = re.search(r"\[\s*(\d{1,3})%\]", line)
            Out.progress(stage, line, int(m.group(1)) if m else None)
        rc = p.wait()
    except BaseException:
        kill_children()
        raise
    finally:
        with contextlib.suppress(ValueError):
            _children.remove(p)
    return rc, "\n".join(lines)


def run_host(argv: list[str], cwd: Path) -> int:
    p = subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
    _children.append(p)
    try:
        for line in p.stdout:
            Out.say(line.rstrip("\n"))
        return p.wait()
    except BaseException:
        kill_children()
        raise
    finally:
        with contextlib.suppress(ValueError):
            _children.remove(p)
