"""`.fw/` state: locks (A4, R8) and single-use flash tokens (A4, R14)."""
from __future__ import annotations

import contextlib
import json
import os
import re
import secrets
import time
from pathlib import Path

from .core import FW_DIR, NOCONFIRM, REFUSED, FwExit

TOKEN_TTL = 300


def _dir(sub: str) -> Path:
    d = FW_DIR / sub
    d.mkdir(parents=True, exist_ok=True)
    os.chmod(FW_DIR, 0o700)
    os.chmod(d, 0o700)
    return d


def pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    # a zombie is not alive for our purposes
    try:
        st = Path(f"/proc/{pid}/stat").read_text()
        return st.rsplit(")", 1)[1].split()[0] != "Z"
    except OSError:
        return True


@contextlib.contextmanager
def lock(name: str, what: str):
    """Exclusive lock `.fw/locks/<name>`; a lock whose PID is dead is reclaimed (R8)."""
    safe = re.sub(r"[^A-Za-z0-9_.-]", "_", name).strip("_") or "lock"
    path = _dir("locks") / safe
    for _ in range(2):
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            break
        except FileExistsError:
            try:
                pid = int(json.loads(path.read_text()).get("pid", 0))
            except (OSError, ValueError):
                pid = 0
            if pid_alive(pid):
                raise FwExit(REFUSED, f"lock held: {name} is in use by fw process {pid}")
            with contextlib.suppress(OSError):
                path.unlink()
    else:
        raise FwExit(REFUSED, f"lock held: {name}")
    with os.fdopen(fd, "w") as f:
        json.dump({"pid": os.getpid(), "what": what, "since": time.time()}, f)
    try:
        yield
    finally:
        with contextlib.suppress(OSError):
            path.unlink()


# ---- tokens ------------------------------------------------------------------------------------

def sweep_tokens() -> None:
    d = FW_DIR / "tokens"
    if not d.is_dir():
        return
    now = time.time()
    for f in d.glob("*.json"):
        try:
            if json.loads(f.read_text()).get("expires_at", 0) < now:
                f.unlink()
        except (OSError, ValueError):
            with contextlib.suppress(OSError):
                f.unlink()


def make_token(rec: dict) -> tuple[str, float]:
    d = _dir("tokens")
    for f in d.glob("*.json"):  # a new --plan for the same port deletes older tokens
        try:
            if json.loads(f.read_text()).get("real_port") == rec["real_port"]:
                f.unlink()
        except (OSError, ValueError):
            pass
    tok = secrets.token_hex(16)  # 128-bit
    expires = time.time() + TOKEN_TTL
    path = d / f"{tok}.json"
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump({**rec, "expires_at": expires}, f)
    return tok, expires


def take_token(tok: str) -> dict:
    """Read and delete the record (single use, deleted also on failure). 5 if unknown/expired."""
    if not re.fullmatch(r"[0-9a-f]{32}", tok or ""):
        raise FwExit(NOCONFIRM, "token invalid")
    path = FW_DIR / "tokens" / f"{tok}.json"
    try:
        rec = json.loads(path.read_text())
    except (OSError, ValueError):
        raise FwExit(NOCONFIRM, "token unknown, expired or already used")
    with contextlib.suppress(OSError):
        path.unlink()
    if rec.get("expires_at", 0) < time.time():
        raise FwExit(NOCONFIRM, "token unknown, expired or already used")
    return rec
