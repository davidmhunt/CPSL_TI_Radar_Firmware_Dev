"""deps, build, publish verbs and the build-state logic (R17-R19, A5, A7)."""
from __future__ import annotations

import json
import os
import shutil
import time
from pathlib import Path

from . import manifest as mf
from . import procs
from .core import (FAILED, REFUSED, ROOT, SHIPPED_DIR, USAGE, FwExit, Out, git, git_head, is_dirty, changed_since, rel,
                   sha256_file)
from .state import lock

# download.sh item number -> glob in downloads/ proving the installer was fetched.
DOWNLOAD_ITEMS = {
    1: "mmwave_mcuplus_sdk_*", 2: "mmwave_sdk_*", 3: "sysconfig-*", 4: "ti_cgt_tms470_*",
    5: "ti_cgt_armllvm_*", 6: "ti_cgt_c6000_*", 7: "radar_toolbox_*", 8: "CCS*", 9: "uniflash_*",
}


def deps_status(m: mf.Manifest) -> list[dict]:
    items = []
    for n in m.deps.get("download_items", []):
        g = DOWNLOAD_ITEMS.get(n)
        present = bool(g and list((ROOT / "downloads").glob(g)))
        items.append({"item": n, "glob": g, "present": present})
    return items


def deps_available(m: mf.Manifest) -> bool:
    return all(i["present"] for i in deps_status(m))


def cmd_deps(args) -> None:
    names = [args.project] if args.project else mf.project_names()
    out = {}
    missing = []
    for n in names:
        m = mf.load(n)
        st = deps_status(m)
        out[n] = st
        for i in st:
            if not i["present"]:
                missing.append(f"{n}: download item {i['item']} ({i['glob']}) not in downloads/")
    for line in missing:
        Out.say(line)
    if missing:
        Out.say("run ./downloads/download.sh")
        raise FwExit(REFUSED, "deps missing: " + "; ".join(missing), {"deps": out})
    Out.say("deps: all download items present")
    raise FwExit(0, "", {"deps": out})


# ---- build state ----------------------------------------------------------------------------

def read_record(m: mf.Manifest) -> dict | None:
    p = m.dir / "build" / "build_info.json"
    try:
        return json.loads(p.read_text())
    except (OSError, ValueError):
        return None


def build_state(m: mf.Manifest) -> tuple[str, dict | None, str]:
    """(none|clean|dirty|stale, record, reason) per A7."""
    rec = read_record(m)
    bdir = m.dir / "build"
    if rec is None:
        if (bdir / "build_info.txt").exists():
            return "stale", None, "only legacy build_info.txt exists (rebuild to write build_info.json)"
        return "none", None, ""
    try:
        for f, sha in rec.get("artifacts", {}).items():
            p = bdir / f
            if not p.is_file() or sha256_file(p) != sha:
                return "stale", rec, f"artifact {f} differs from the build record"
        commit = rec.get("firmware_dev_commit", "")
        if changed_since(m.name, commit):
            return "stale", rec, f"projects/{m.name}, fw or tools changed since build commit {commit}"
        if rec.get("dirty") or is_dirty(m.name):
            return "dirty", rec, "built from, or sitting on, uncommitted changes"
    except FwExit as e:
        return "stale", rec, e.reason
    return "clean", rec, ""


def effective_status(m: mf.Manifest, state: str, rec: dict | None) -> tuple[str, str]:
    """A5: downgrade the declared status when the evidence is missing."""
    st, reason = m.status, ""
    if st in ("built", "bench") and rec is None:
        return "source", "no build record (run fw build)"
    if st == "bench":
        arts = m.artifacts
        cur = ""
        if arts:
            p = m.dir / "build" / arts[0]["file"]
            cur = sha256_file(p) if p.is_file() else ""
        if not any(b["result"] == "pass" and b["sha256"] == cur for b in m.benches):
            return "built", "no [[bench]] pass record matches the current default artifact sha256"
    return st, reason


# ---- build ----------------------------------------------------------------------------------

def cmd_build(args) -> None:
    m = mf.load(args.project)
    script = m.dir / m.build.get("script", "build.sh")
    if not script.is_file():
        raise FwExit(FAILED, f"{rel(script)} not found")
    extras = list(args.rest)
    variants = m.build.get("variants", [])
    variant = args.variant
    if not variant and variants and extras and extras[0] in variants:
        variant = extras.pop(0)
    if variant and variant not in variants:
        raise FwExit(USAGE, f"unknown variant '{variant}' (variants: {', '.join(variants) or 'none'})")
    items = deps_status(m)
    missing = [i for i in items if not i["present"]]
    if missing:
        msg = "deps missing: download item(s) " + ", ".join(str(i["item"]) for i in missing) + \
              " (run ./downloads/download.sh)"
        if not args.dry_run:
            raise FwExit(REFUSED, msg)
        Out.warn(msg)
    try:
        commit = git_head()
        dirty = is_dirty(m.name)
    except FwExit as e:
        if not args.dry_run:
            raise
        Out.warn(f"{e.reason} (dry-run continues)")
        commit, dirty = "unknown", False
    fw_commit = commit + ("-dirty" if dirty else "")
    env = {"FW_PROJECT": m.name}
    cargs = list(extras)
    if variant:
        env["FW_VARIANT"] = variant
        cargs.insert(0, variant)
    argv = procs.compose_args("firmware-env", f"/build_context/projects/{m.name}/{script.name}", cargs, env,
                              fw_commit)
    if args.dry_run:
        Out.say("DRY RUN: would run: " + " ".join(argv))
        raise FwExit(0, "", {"command": argv, "commit": commit, "dirty": dirty, "variant": variant or ""})
    with lock(f"project-{m.name}", "build"):
        Out.progress("build", f"building {m.name} at {fw_commit}")
        rc, _ = procs.run_streaming(argv, "build")
        if rc != 0:
            raise FwExit(FAILED, f"build failed (container exit {rc})")
        bdir = m.dir / "build"
        arts = {}
        for a in m.artifacts:
            p = bdir / a["file"]
            if p.is_file():
                arts[a["file"]] = sha256_file(p)
        if not arts:
            raise FwExit(FAILED, "build produced none of the declared artifacts")
        rec = {"project": m.name, "firmware_dev_commit": commit, "dirty": dirty, "variant": variant or "",
               "toolchain": m.deps.get("toolchain", ""),
               "built_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "artifacts": arts}
        (bdir / "build_info.json").write_text(json.dumps(rec, indent=2) + "\n")
        Out.say(f"Wrote {rel(bdir / 'build_info.json')}")
    raise FwExit(0, "", {"build_info": rec})


# ---- publish --------------------------------------------------------------------------------

def cmd_publish(args) -> None:
    m = mf.load(args.project)
    dest = Path(args.dest).resolve() if args.dest else SHIPPED_DIR
    todo, skipped = [], []
    with lock(f"project-{m.name}", "publish"):
        state, rec, reason = build_state(m)
        if state in ("none", "dirty", "stale"):
            raise FwExit(REFUSED, f"refusing to publish: build is {state}" + (f" ({reason})" if reason else ""))
        for a in m.artifacts:
            if not a.get("flashable") or not a.get("descriptor"):
                skipped.append(a["file"])
                continue
            src = m.dir / "build" / a["file"]
            if not src.is_file() or rec["artifacts"].get(a["file"]) != sha256_file(src):
                raise FwExit(REFUSED, f"refusing to publish: {a['file']} missing or differs from the build record")
            todo.append((a, src))
        cfgs = []
        for g in m.test.get("cfgs", []):
            for p in sorted(m.dir.glob(g)):
                if p.is_file() and "build" not in p.relative_to(m.dir).parts:
                    cfgs.append({"path": str(p.relative_to(m.dir)), "sha256": sha256_file(p)})
        paths = []
        plan = []
        for a, src in todo:
            d = dest / a["board"] / a["descriptor"]
            prov = {"project": m.name, "firmware_dev_commit": rec["firmware_dev_commit"],
                    "variant": rec.get("variant", ""), "toolchain": rec.get("toolchain", ""),
                    "built_utc": rec.get("built_utc", ""), "artifact": a["file"], "board": a["board"],
                    "descriptor": a["descriptor"], "sha256": rec["artifacts"][a["file"]],
                    "chirp_cfg": cfgs,
                    "published_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
            plan.append((src, d / a["file"], prov))
            paths.append(str(d / a["file"]))
        if args.dry_run:
            Out.say("DRY RUN: would publish " + (", ".join(paths) or "nothing"))
            raise FwExit(0, "", {"paths": paths, "skipped": skipped})
        for src, dst, prov in plan:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            Path(str(dst) + ".provenance.json").write_text(json.dumps(prov, indent=2) + "\n")
            Out.progress("publish", f"published {dst}")
    raise FwExit(0, "", {"paths": paths, "skipped": skipped})
