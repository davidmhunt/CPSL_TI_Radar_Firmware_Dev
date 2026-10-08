"""list, ports, new, test, help verbs."""
from __future__ import annotations

import json
import re
import shutil

from . import gates
from . import manifest as mf
from . import procs
from .core import (DESCRIPTOR_DIR, FAILED, PROJECTS, ROOT, TEMPLATE, USAGE, FwExit, Out, rel)
from .v_build import build_state, cmd_build, deps_available, effective_status

USAGE_TEXT = """Usage: ./fw <command> [args] [--json]     (run from firmware_dev/; guide: projects/README.md)

  list                            Projects: status, board, SDK, baseline, build state
  ports [<project>]               Candidate serial ports (by-id) and whether a process holds them
  new <project>                   Create projects/<project>/ from projects/_template/
  deps [<project>]                Check the TI installers the project needs are in downloads/
  build <project> [--variant V] [--dry-run] [args...]
                                  Check deps, run build.sh in the container, write build/build_info.json
  test [<project>]                Hardware-free checks: manifest, descriptors, cfgs, [test] commands
  flash <project> <port> [image] [--dry-run | --plan | --confirm TOKEN]
                                  Gated flash (G1-G7). A human at a terminal types the phrase;
                                  a script runs --plan (checklist + token), then --confirm TOKEN
  verify <project> [--port P]     Send the descriptor's identify probes (never a chirp cfg)
  publish <project> [--dest D] [--dry-run]
                                  Copy flashable images + provenance to shipped_firmware/<BOARD>/<descriptor>/
  help                            This summary

Every command accepts --help and --json (JSON Lines on stdout: progress events, one final result).
Exit codes: 0 ok, 1 failed, 2 usage, 3 unsupported/manual, 4 precondition refused, 5 confirmation missing/invalid.
Prerequisites (once): ./downloads/download.sh && docker compose build
"""


def cmd_help(args) -> None:
    print(USAGE_TEXT, end="", file=__import__("sys").stderr if Out.json else __import__("sys").stdout)
    raise FwExit(0, "", {"usage": USAGE_TEXT})


def cmd_list(args) -> None:
    rows = []
    for name in mf.project_names():
        try:
            m = mf.load(name)
        except FwExit as e:
            rows.append({"name": name, "status": "invalid", "reason": e.reason, "artifacts": [], "bench": [],
                         "deps_available": False, "build": "none", "_board": "", "_sdk": "", "_base": ""})
            continue
        state, rec, why = build_state(m)
        st, reason = effective_status(m, state, rec)
        row = {"name": name, "status": st,
               "artifacts": [{k: a[k] for k in ("file", "board", "descriptor", "flashable")} for a in m.artifacts],
               "bench": m.benches, "deps_available": deps_available(m), "build": state}
        if reason:
            row["reason"] = reason
        elif m.legacy:
            row["reason"] = "project.env (deprecated)"
        row["_board"] = ",".join(sorted({a["board"] for a in m.artifacts}))
        row["_sdk"] = f"{m.deps.get('sdk', '')} {m.deps.get('sdk_version', '')}"
        row["_base"] = f"{m.data['source']['baseline']} @ {m.data['source']['baseline_commit']}" \
            if "source" in m.data else ""
        rows.append(row)
    if not Out.json:
        print(f"{'PROJECT':<24} {'STATUS':<8} {'BUILD':<6} {'BOARD':<22} {'SDK':<34} BASELINE")
        for r in rows:
            print(f"{r['name']:<24} {r['status']:<8} {r['build']:<6} {r['_board'][:22]:<22} {r['_sdk']:<34} {r['_base']}")
            if r.get("reason"):
                print(f"    note: {r['reason']}")
        if not rows:
            print("(no projects yet: create one with ./fw new <project>)")
    data = [{k: v for k, v in r.items() if not k.startswith("_")} for r in rows]
    raise FwExit(0, "", {"projects": data})


def cmd_ports(args) -> None:
    glob = None
    if args.project:
        m = mf.load(args.project)
        glob = m.flash.get("port_glob") or m.verify.get("cli_port_glob")
    ports = gates.list_ports(glob)
    for p in ports:
        Out.say(f"{p['path']}{'  (held)' if p['held'] else ''}")
    if not ports:
        Out.say(f"(no ports under {gates.SERIAL_DIR})")
    raise FwExit(0, "", {"ports": ports})


def cmd_new(args) -> None:
    p = args.project
    if not re.fullmatch(r"[a-z0-9][a-z0-9_]*", p or ""):
        raise FwExit(USAGE, "project names use lowercase letters, digits and '_' (e.g. iwr1843_my_demo)")
    dst = PROJECTS / p
    if dst.exists():
        raise FwExit(USAGE, f"projects/{p} already exists; not overwriting it")
    shutil.copytree(PROJECTS / TEMPLATE, dst, ignore=shutil.ignore_patterns("build", "__pycache__"))
    for f in dst.rglob("*"):
        if f.is_file():
            try:
                t = f.read_text()
            except UnicodeDecodeError:
                continue
            if "PROJECT_NAME" in t:
                f.write_text(t.replace("PROJECT_NAME", p))
    Out.say(f"Created projects/{p}/. Next: fill in the TODOs in projects/{p}/project.toml, add the TI source "
            f"to src/, write build.sh, then: ./fw test {p} && ./fw build {p}   (walkthrough: projects/README.md)")
    raise FwExit(0, "", {"path": f"projects/{p}"})


def _headings(path, wanted: list[str]) -> list[str]:
    if not path.is_file():
        return [f"{rel(path)} missing"]
    have = {re.sub(r"^#+\s*", "", ln).strip().lower() for ln in path.read_text().splitlines() if ln.startswith("#")}
    return [f"{rel(path)}: heading '{h}' missing" for h in wanted if h.lower() not in have]


def _test_one(name: str, skip_commands: bool) -> dict:
    errs: list[str] = []
    warns: list[str] = []
    try:
        m = mf.load(name, allow_template=True)
    except FwExit as e:
        return {"ok": False, "errors": [e.reason], "warnings": []}
    warns += m.warnings()
    if m.legacy:
        warns.append("project.env fallback: manifest checks are limited to the synthesized manifest")
    have_desc = DESCRIPTOR_DIR.is_dir()
    seen = set()
    for a in m.artifacts:
        d = a.get("descriptor", "")
        if d and "TODO" not in d and have_desc and d not in seen:
            seen.add(d)
            f = DESCRIPTOR_DIR / f"{d}.json"
            if not f.is_file():
                errs.append(f"artifact descriptor '{d}' not found in config/firmware/")
                continue
            try:
                dj = json.loads(f.read_text())
            except ValueError:
                errs.append(f"descriptor {d}.json is not valid JSON")
                continue
            src = dj.get("source")
            if isinstance(src, dict) and src.get("fw_project") not in (None, name):
                errs.append(f"descriptor {d}: source.fw_project is '{src.get('fw_project')}', not '{name}'")
            if a.get("flashable") and m.verify and not m.legacy and d == m.verify.get("descriptor") \
                    and a["board"] not in (dj.get("identify") or {}):
                errs.append(f"descriptor {d} has no identify entry for {a['board']}")
    for g in m.test.get("cfgs", []):
        if "TODO" in g:
            continue
        hits = [p for p in m.dir.glob(g) if p.is_file() and "build" not in p.relative_to(m.dir).parts]
        if not hits:
            errs.append(f"[test].cfgs '{g}' matches no tracked file")
    if not m.legacy:
        errs += _headings(m.dir / m.test.get("bench_doc", "docs/bench_check.md"), mf.BENCH_HEADINGS)
        errs += _headings(m.dir / "README.md", mf.README_HEADINGS)
        readme = m.dir / "README.md"
        if readme.is_file() and re.search(r"20[0-9]{2}-[0-9]{2}-[0-9]{2}", readme.read_text()):
            errs.append("README.md contains a date (bench dates live only in [[bench]])")
    if not skip_commands:
        for c in m.test.get("commands", []):
            if "TODO" in c:
                warns.append(f"[test].commands '{c}' skipped")
                continue
            Out.say(f"$ {c}")
            import shlex
            if procs.run_host(shlex.split(c), m.dir) != 0:
                errs.append(f"[test] command failed: {c}")
    if m.build and m.artifacts:
        class A:  # build --dry-run: no container, no deps failure
            project, variant, dry_run, rest = name, None, True, []
        try:
            cmd_build(A)
        except FwExit as e:
            if e.code != 0:
                errs.append(f"build --dry-run: {e.reason}")
    return {"ok": not errs, "errors": errs, "warnings": warns}


def cmd_test(args) -> None:
    names = [args.project] if args.project else mf.project_names()
    res = {}
    for n in names:
        r = _test_one(n, args.skip_commands)
        res[n] = r
        for w in r["warnings"]:
            Out.warn(f"{n}: {w}")
        for e in r["errors"]:
            Out.say(f"FAIL {n}: {e}")
        Out.say(f"{n}: {'ok' if r['ok'] else 'FAILED'}")
    bad = [n for n, r in res.items() if not r["ok"]]
    raise FwExit(FAILED if bad else 0, f"fw test failed for: {', '.join(bad)}" if bad else "", {"projects": res})
