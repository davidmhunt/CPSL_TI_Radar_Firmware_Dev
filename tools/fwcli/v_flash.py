"""flash and verify verbs (R12-R16)."""
from __future__ import annotations

import json
import os
import re
import select
import sys
import termios
import time
import tty
from pathlib import Path

from . import gates
from . import manifest as mf
from . import procs
from .core import (DESCRIPTOR_DIR, FAILED, NOCONFIRM, REFUSED, ROOT, UNSUPPORTED, USAGE, FwExit, Out, git_head,
                   rel, sha256_file)
from .state import lock, lock_held, make_token, take_token


def is_tty() -> bool:
    return sys.stdin.isatty() and sys.stdout.isatty()


def _container_path(img: Path) -> str:
    return "/build_context/" + str(img.relative_to(ROOT))


def _flash_run(m: mf.Manifest, port: str, image_arg: str, dry: bool) -> tuple[int, str]:
    env = {"FW_PROJECT": m.name, "FW_PORT": port, "FW_IMAGE": image_arg, "FW_DRY_RUN": "1" if dry else "0"}
    try:
        commit = git_head()
    except FwExit:
        commit = None
    args = (["--dry-run"] if dry else []) + [port, image_arg]
    argv = procs.compose_args("firmware-env" if dry else "flash", f"/build_context/projects/{m.name}/flash.sh",
                              args, env, commit)
    Out.cancel_note = m.flash.get("after_steps", [])
    return procs.run_streaming(argv, "flash")


def _script_lines(out: str) -> list[str]:
    """flash.sh's manual-step output as a checklist (blank lines dropped)."""
    return [ln.strip() for ln in out.splitlines() if ln.strip()]


def cmd_flash(args) -> None:
    m = mf.load(args.project)
    if args.plan and args.confirm:
        raise FwExit(USAGE, "--plan and --confirm are mutually exclusive")
    if args.dry_run and (args.plan or args.confirm):
        raise FwExit(USAGE, "--dry-run cannot be combined with --plan/--confirm")
    fl = m.flash
    flashable = [a for a in m.artifacts if a.get("flashable")]
    if not flashable or not fl:
        raise FwExit(UNSUPPORTED, f"{m.name} has no flashable artifact")
    steps = fl.get("mode_steps", [])
    port = args.port
    # ---- image
    keyword = None
    rec = None
    if args.confirm:
        rec = take_token(args.confirm)  # single use: deleted now, also if a later check fails
        if rec.get("project") != m.name or rec.get("port") != port or \
                (args.image and str(Path(args.image).resolve()) != rec.get("image")):
            raise FwExit(NOCONFIRM, "token does not belong to this project/port/image")
        img = Path(rec["image"])
        if not img.is_file():
            raise FwExit(REFUSED, f"image missing: {img}")
    elif args.image == "prebuilt" and fl.get("method") == "uart_uniflash" and not args.plan:
        keyword = "prebuilt"
        img = None
    elif args.image:
        p = Path(args.image)
        if not p.is_file():
            raise FwExit(REFUSED, f"image {args.image} not found")
        img = p.resolve()
    else:
        first = m.artifacts[0]
        img = m.dir / "build" / first["file"]
        if not first.get("flashable"):
            raise FwExit(UNSUPPORTED, f"default artifact {first['file']} is not flashable")
        if not img.is_file():
            raise FwExit(REFUSED, f"default image {img} not found (run ./fw build {m.name} first)")
        img = img.resolve()
    if img is not None:
        gates.g1_inside(img)                                              # G1
    sha = sha256_file(img) if img is not None else "prebuilt (in container)"
    byid = gates.is_byid(port)
    shown = rel(img) if img is not None else "prebuilt"
    cimg = _container_path(img) if img is not None else keyword

    if fl["method"] == "manual":
        for s in steps + fl.get("after_steps", []):
            Out.say("  - " + s)
        raise FwExit(UNSUPPORTED, "manual flash method: follow the steps above", {"checklist": steps})

    # ---- per-image manual flash ([flash].manual_images): flash.sh prints its own steps and exits 3, so --plan and
    # --dry-run show those steps (image-specific), never the manifest-level mode_steps, and issue no token.
    if img is not None and img.name in fl.get("manual_images", []) and (args.plan or args.dry_run):
        rc, out = _flash_run(m, port, cimg, True)
        if rc != 3:
            raise FwExit(FAILED, f"{img.name} is listed in [flash].manual_images but flash.sh --dry-run exited {rc}, not 3")
        manual = _script_lines(out)
        for ln in manual:
            Out.say("  - " + ln)
        raise FwExit(UNSUPPORTED, f"manual flash image {img.name}: follow the steps above",
                     {"checklist": manual, "image": shown, "manual": True})

    # ---- G7: --dry-run flashes nothing
    if args.dry_run:
        if not byid:
            Out.warn(f"{port} is not a {gates.SERIAL_DIR}/...-if00 path; a real flash will demand an extra "
                     "acknowledgement")
        Out.say("== DRY RUN: flash-mode checklist a real flash will require you to confirm ==")
        for ln in gates.checklist_text(steps):
            Out.say(ln)
        if fl["method"] == "dslite":
            rc, dry_out = _flash_run(m, port, cimg, True)
            if rc != 0:
                raise FwExit(UNSUPPORTED if rc == 3 else FAILED, f"flash.sh --dry-run failed (exit {rc})",
                             {"checklist": _script_lines(dry_out)} if rc == 3 else None)
        else:
            Out.say(f"image : {shown}")
            Out.say(f"sha256: {sha}")
            Out.say(f"command: projects/{m.name}/flash.sh {port} {cimg}  (method {fl['method']})")
            Out.say("(dry run: nothing flashed, no port touched)")
        raise FwExit(0, "", {"checklist": steps, "sha256": sha, "port": port, "image": shown, "dry_run": True,
                             "after_steps": fl.get("after_steps", [])})

    # ---- token paths (R14)
    if args.plan:
        real = gates.g2_exists(port)                                      # G2
        gates.g3_free(real)                                               # G3
        if lock_held(f"port-{real}"):                                     # R8: a live fw lock counts as held
            raise FwExit(REFUSED, f"lock held: {port} is in use by another fw process")
        if not byid:                                                      # G4 (no acknowledgement possible)
            raise FwExit(REFUSED, f"{port} is not a {gates.SERIAL_DIR}/...-if00 path; --plan accepts by-id ports only")
        tok, exp = make_token({"project": m.name, "port": port, "real_port": real, "image": str(img),
                               "sha256": sha})
        Out.say(f"token {tok} valid until {time.strftime('%H:%M:%S', time.localtime(exp))}")
        for ln in gates.checklist_text(steps):
            Out.say(ln)
        raise FwExit(0, "", {"checklist": steps, "sha256": sha, "port": port, "token": tok,
                             "expires_at": exp, "after_steps": fl.get("after_steps", []), "image": shown})

    if rec is not None:                                                   # --confirm: re-run G2-G4 + sha
        if not os.path.exists(port):
            raise FwExit(REFUSED, f"token's port {port} is gone")
        real = gates.g2_exists(port)
        gates.g3_free(real)
        if not byid:
            raise FwExit(REFUSED, f"{port} is not a by-id -if00 path")
        if sha != rec["sha256"]:
            raise FwExit(REFUSED, "image sha256 changed since --plan; plan again")
    else:                                                                 # TTY path
        if not is_tty():                                                  # no TTY and no token
            raise FwExit(NOCONFIRM, "refusing to flash: an interactive terminal (TTY) or a --plan/--confirm "
                                    "token is required to confirm flash mode")
        real = gates.g2_exists(port)                                      # G2
        gates.g3_free(real)                                               # G3

    with lock(f"port-{real}", "flash"):
        if rec is None:
            Out.say(f"image : {shown}")
            Out.say(f"sha256: {sha}")
            Out.say(f"port  : {port} -> {real}")
            if not byid:                                                  # G4
                if Out.json:
                    raise FwExit(REFUSED, f"{port} is not a by-id -if00 path (refused under --json)")
                Out.say(f"WARNING: {port} is not a {gates.SERIAL_DIR}/...-if00 path; you must be sure it is "
                        "the board's CLI/UART port.")
                try:
                    ack = input(f"Type exactly '{gates.ACK_PHRASE}' to accept this port: ")
                except EOFError:
                    ack = ""
                if ack != gates.ACK_PHRASE:
                    raise FwExit(NOCONFIRM, "port not acknowledged; nothing flashed")
            for ln in gates.checklist_text(steps):                        # G5
                Out.say(ln)
            try:                                                          # G6
                reply = input(f"Type exactly '{gates.PHRASE}' to flash (anything else aborts): ")
            except EOFError:
                reply = ""
            if reply != gates.PHRASE:
                raise FwExit(NOCONFIRM, "acknowledgement not given; nothing flashed")
        rc, out = _flash_run(m, port, cimg, False)
    marker = fl.get("success_marker", "")
    if rc == 0 and marker and marker not in out:
        raise FwExit(FAILED, f"flash.sh exited 0 but the success marker '{marker}' was not seen")
    if rc != 0:
        raise FwExit(UNSUPPORTED if rc == 3 else USAGE if rc == 2 else FAILED, f"flash failed (flash.sh exit {rc})",
                     {"checklist": _script_lines(out)} if rc == 3 else None)
    for s in fl.get("after_steps", []):
        Out.say("  - " + s)
    raise FwExit(0, "", {"port": port, "sha256": sha, "after_steps": fl.get("after_steps", [])})


# ---- verify ---------------------------------------------------------------------------------

CFG_CMD = re.compile(r"^(sensorStart|sensorStop|flushCfg|\w+Cfg)\b", re.I)


def _probe(fd: int, cmd: str, timeout_ms: int) -> str:
    termios.tcflush(fd, termios.TCIFLUSH)
    os.write(fd, (cmd + "\n").encode())
    buf = b""
    end = time.time() + timeout_ms / 1000
    last = None
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                break
            if d:
                buf += d
                last = time.time()
                continue
        if last is not None and time.time() - last > 0.4 and re.search(rb":/>\s*$", buf):
            break
    return buf.decode(errors="replace")


def cmd_verify(args) -> None:
    m = mf.load(args.project)
    desc_id = m.verify.get("descriptor") or (m.artifacts[0].get("descriptor") if m.artifacts else "")
    if not desc_id:
        raise FwExit(UNSUPPORTED, "verify: no descriptor for this project")
    dpath = DESCRIPTOR_DIR / f"{desc_id}.json"
    if not dpath.is_file():
        raise FwExit(UNSUPPORTED, f"verify: descriptor {desc_id} not found in config/firmware/")
    board = next((a["board"] for a in m.artifacts if a.get("descriptor") == desc_id), m.artifacts[0]["board"])
    try:
        ident = json.loads(dpath.read_text()).get("identify", {})
    except ValueError:
        raise FwExit(UNSUPPORTED, f"verify: descriptor {desc_id} is not valid JSON")
    ent = ident.get(board) if isinstance(ident, dict) else None
    if not isinstance(ent, dict):
        raise FwExit(UNSUPPORTED, f"verify: descriptor {desc_id} has no identify entry for {board}")
    probes = ent.get("probes", [])
    for p in probes:
        if CFG_CMD.match(p.get("cmd", "")):
            raise FwExit(REFUSED, f"probe '{p['cmd']}' looks like a sensor/chirp cfg; verify never sends one")
    if ent.get("once_safe", True) is False:
        Out.say("verify skipped: this firmware accepts a cfg once per power-up and identify is not once-safe")
        raise FwExit(0, "", {"outcome": "skipped", "probes": [], "once_safe": False})
    if args.dry_run:
        for p in probes:
            Out.say(f"would send: {p['cmd']}")
        raise FwExit(0, "", {"outcome": "skipped", "probes": [{"cmd": p["cmd"]} for p in probes], "dry_run": True})
    glob = m.verify.get("cli_port_glob", "*-if00")
    port = args.port
    if not port:
        cands = [p["path"] for p in gates.list_ports(glob)]
        if len(cands) != 1:
            raise FwExit(REFUSED, f"verify: {len(cands)} ports match {glob}; pass --port")
        port = cands[0]
    if not gates.is_byid(port) or not mf.matches_glob(port, glob if "/" in glob else f"*{glob}"):
        raise FwExit(REFUSED, f"{port} is not a by-id port matching {glob}")
    real = gates.g2_exists(port)
    gates.g3_free(real)
    timeout_ms = int(ent.get("timeout_ms", 3000))
    results, ok_all = [], True
    with lock(f"port-{real}", "verify"):
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            tty.setraw(fd)
            try:
                a = termios.tcgetattr(fd)
                sp = getattr(termios, f"B{m.verify.get('baud', 115200)}", None)
                if sp:
                    a[4] = a[5] = sp
                    termios.tcsetattr(fd, termios.TCSANOW, a)
            except termios.error:
                pass
            for p in probes:
                Out.progress("verify", f"probe {p['cmd']}")
                reply = _probe(fd, p["cmd"], timeout_ms)
                req = p.get("require", [])
                rej = p.get("reject", [])
                matched = [r for r in req if re.search(r, reply)]
                bad = [r for r in rej if re.search(r, reply)]
                ok = bool(reply.strip()) and len(matched) == len(req) and not bad
                show = {}
                for k, rx in (p.get("show") or {}).items():
                    mm = re.search(rx, reply)
                    if mm:
                        show[k] = mm.group(1) if mm.groups() else mm.group(0)
                results.append({"cmd": p["cmd"], "ok": ok, "matched": matched, "show": show})
                ok_all &= ok
        finally:
            os.close(fd)
    outcome = "pass" if ok_all else "fail"
    Out.say(f"verify {outcome}: " + ", ".join(f"{r['cmd']}={'ok' if r['ok'] else 'FAIL'}" for r in results))
    raise FwExit(0 if ok_all else FAILED, "" if ok_all else "probe failed or no response",
                 {"outcome": outcome, "probes": results, "port": port})
