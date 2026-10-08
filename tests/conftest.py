"""Fixtures: a throw-away firmware_dev tree (real fw + fwcli code), fake compose, fake fuser, pty helpers."""
import json
import os
import pty
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pytest

REAL = Path(__file__).resolve().parents[1]
BIN = REAL / "tests" / "bin"

DESCRIPTOR = {"id": "dummy", "identify": {"IWR9999": {"level": "bench", "timeout_ms": 1500, "once_safe": True,
              "probes": [{"cmd": "version", "require": ["Platform\\s*:\\s*xWR99"], "reject": ["BAD"],
                          "show": {"platform": "Platform\\s*:\\s*([^\\s]+)"}}]}}}


def toml_dump(d: dict) -> str:
    def val(v):
        if isinstance(v, bool):
            return "true" if v else "false"
        if isinstance(v, (int, float)):
            return str(v)
        if isinstance(v, list):
            return "[" + ", ".join(val(x) for x in v) + "]"
        return json.dumps(v)
    out = []
    for k, v in d.items():
        if isinstance(v, dict):
            out.append(f"[{k}]")
            out += [f"{a} = {val(b)}" for a, b in v.items()]
        elif isinstance(v, list) and v and isinstance(v[0], dict):
            for item in v:
                out.append(f"[[{k}]]")
                out += [f"{a} = {val(b)}" for a, b in item.items()]
        out.append("")
    return "\n".join(out)


def base_manifest(name="proj", **over):
    m = {
        "project": {"name": name, "summary": "test fw", "status": "built"},
        "deps": {"sdk": "s", "sdk_version": "1", "toolchain": "tc 1", "download_items": []},
        "source": {"baseline": "b", "baseline_commit": "abc"},
        "build": {"script": "build.sh", "variants": ["a", "b"], "est_minutes": 1},
        "artifact": [{"file": "img.bin", "board": "IWR9999", "descriptor": "dummy", "flashable": True},
                     {"file": "img.elf", "board": "IWR9999", "descriptor": "", "flashable": False}],
        "flash": {"method": "dslite", "gate": "sop", "port_glob": "*-if00",
                  "mode_steps": ["Set SOP", "Power-cycle"], "after_steps": ["Restore run mode"],
                  "success_marker": "SUCCESS!!"},
        "verify": {"cli_port_glob": "*-if00", "baud": 115200, "descriptor": "dummy"},
        "test": {"commands": [], "cfgs": ["configs/*.cfg"], "bench_doc": "docs/bench_check.md"},
    }
    for k, v in over.items():
        if v is None:
            m.pop(k, None)
        else:
            m[k] = v
    return m


BUILD_SH = """#!/bin/bash
set -e
cd "$(dirname "$0")"; mkdir -p build
echo "[ 50%] building ${FW_VARIANT:-none}"
printf 'image-%s' "${FAKE_IMG_TAG:-1}" > build/img.bin
printf 'elf' > build/img.elf
exit "${FAKE_BUILD_RC:-0}"
"""
FLASH_SH = """#!/bin/bash
DRY=0; POS=()
for a in "$@"; do case "$a" in --dry-run) DRY=1;; *) POS+=("$a");; esac; done
echo "image : ${POS[1]}"
echo "sha256: $(sha256sum "${POS[1]}" | cut -d' ' -f1)"
echo "command: dslite flash -f ${POS[1]},1"
if [[ $DRY -eq 1 ]]; then echo "(dry run: nothing flashed, no port touched)"; exit 0; fi
echo "$@" >> "$FAKE_FLASH_LOG"
echo "SUCCESS!! File type META_IMAGE1"
exit "${FAKE_FLASH_RC:-0}"
"""


class Tree:
    def __init__(self, root: Path):
        self.top = root
        self.fw = root / "firmware_dev"
        self.serial = root / "serial" / "by-id"
        self.env = dict(os.environ)
        for k in [k for k in self.env if k.startswith("FW_") or k.startswith("FAKE_")]:
            del self.env[k]
        self.env.update({
            "FW_COMPOSE": f"{sys.executable} {BIN / 'fake_compose.py'}", "FAKE_ROOT": str(self.fw),
            "FAKE_COMPOSE_LOG": str(root / "compose.log"), "FAKE_FLASH_LOG": str(root / "flash.log"),
            "FAKE_FUSER_HELD": str(root / "held.txt"), "FW_SERIAL_BYID_DIR": str(self.serial),
            "PATH": f"{BIN}:{os.environ['PATH']}",
        })

    # -- tree building
    def git(self, *a):
        return subprocess.run(["git", "-C", str(self.fw), "-c", "user.name=t", "-c", "user.email=t@t", *a],
                              capture_output=True, text=True, check=True).stdout.strip()

    def add_project(self, name="proj", manifest=None, readme=True, files=None):
        p = self.fw / "projects" / name
        for d in ("configs", "docs", "src"):
            (p / d).mkdir(parents=True, exist_ok=True)
        (p / "project.toml").write_text(toml_dump(manifest or base_manifest(name)))
        (p / "build.sh").write_text(BUILD_SH)
        (p / "flash.sh").write_text(FLASH_SH)
        for s in ("build.sh", "flash.sh"):
            (p / s).chmod(0o755)
        (p / "configs" / "a.cfg").write_text("cfg")
        heads = ["Preconditions", "Board state", "Steps", "Expected output", "Pass/fail", "Results log"]
        (p / "docs" / "bench_check.md").write_text("\n".join(f"## {h}\n\ntext\n" for h in heads))
        rh = ["Purpose", "Status", "Build", "Test", "Flash", "Verify", "Bench check", "Layout", "Changes vs TI",
              "Known limits"]
        if readme:
            (p / "README.md").write_text("# x\n\n" + "\n".join(f"## {h}\n\ntext\n" for h in rh))
        for rel, txt in (files or {}).items():
            (p / rel).parent.mkdir(parents=True, exist_ok=True)
            (p / rel).write_text(txt)
        return p

    def commit(self, msg="c"):
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", msg)

    def descriptor(self, name="dummy", data=None):
        d = self.top / "CPSL_TI_Radar_cpp" / "config" / "firmware"
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{name}.json").write_text(json.dumps(data or DESCRIPTOR))

    def port(self, name="usb-board-if00"):
        """A fake by-id port: a pty whose slave is symlinked into the fake by-id dir. Returns (path, master_fd)."""
        self.serial.mkdir(parents=True, exist_ok=True)
        m, s = pty.openpty()
        link = self.serial / name
        link.symlink_to(os.ttyname(s))
        self._keep = getattr(self, "_keep", []) + [s]
        return str(link), m

    def hold(self, path):
        with open(self.top / "held.txt", "a") as f:
            f.write(os.path.realpath(path) + "\n")

    # -- running fw
    def run(self, *args, stdin=None, env=None, timeout=30):
        e = {**self.env, **(env or {})}
        return subprocess.run([str(self.fw / "fw"), *args], capture_output=True, text=True, env=e,
                              stdin=subprocess.DEVNULL if stdin is None else None, input=stdin, timeout=timeout)

    def events(self, r):
        return [json.loads(ln) for ln in r.stdout.splitlines()]

    def result(self, r):
        ev = self.events(r)
        assert ev and ev[-1]["event"] == "result", r.stdout + r.stderr
        assert sum(e["event"] == "result" for e in ev) == 1
        return ev[-1]

    def run_tty(self, args, typed, env=None, timeout=20):
        """Run fw with stdin/stdout on a pty and type `typed` (list of lines) into it."""
        m, s = pty.openpty()
        e = {**self.env, **(env or {})}
        p = subprocess.Popen([str(self.fw / "fw"), *args], stdin=s, stdout=s, stderr=subprocess.PIPE, env=e,
                             text=True)
        os.close(s)
        out = b""
        for line in typed:
            time.sleep(0.4)
            os.write(m, (line + "\n").encode())
        end = time.time() + timeout
        import select
        while time.time() < end:
            r, _, _ = select.select([m], [], [], 0.2)
            if r:
                try:
                    d = os.read(m, 4096)
                except OSError:
                    break
                if not d:
                    break
                out += d
            elif p.poll() is not None:
                break
        rc = p.wait(timeout=timeout)
        err = p.stderr.read()
        os.close(m)
        return rc, out.decode(errors="replace"), err

    def compose_calls(self):
        f = self.top / "compose.log"
        return [json.loads(ln) for ln in f.read_text().splitlines()] if f.exists() else []


@pytest.fixture
def tree(tmp_path):
    t = Tree(tmp_path)
    t.fw.mkdir()
    shutil.copy2(REAL / "fw", t.fw / "fw")
    shutil.copytree(REAL / "tools" / "fwcli", t.fw / "tools" / "fwcli", ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(REAL / "projects" / "_template", t.fw / "projects" / "_template",
                    ignore=shutil.ignore_patterns("build", "__pycache__"))
    (t.fw / "downloads").mkdir()
    (t.fw / ".gitignore").write_text("/projects/*/build/\n/.fw/\n__pycache__/\n")
    subprocess.run(["git", "-C", str(t.fw), "init", "-q", "-b", "main"], check=True)
    t.descriptor()
    return t


@pytest.fixture
def built(tree):
    """A tree with one committed, built, clean project `proj`."""
    tree.add_project()
    tree.commit("add proj")
    r = tree.run("build", "proj", "--json")
    assert r.returncode == 0, r.stdout + r.stderr
    (tree.top / "compose.log").unlink()  # tests count container starts after the build
    return tree
