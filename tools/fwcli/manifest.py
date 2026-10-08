"""project.toml loader + validator (R1-R6, R22, A5), with the project.env fallback."""
from __future__ import annotations

import fnmatch
import re
import tomllib
from pathlib import Path

from .core import (DESCRIPTOR_DIR, PROJECTS, SERIAL_DIR, TEMPLATE, FwExit, Out, FAILED, USAGE)

STATUSES = ("stub", "source", "built", "bench")
METHODS = ("dslite", "uart_uniflash", "manual")
GATES = ("sop", "j6")
RESULTS = ("pass", "partial", "fail")
BENCH_HEADINGS = ["Preconditions", "Board state", "Steps", "Expected output", "Pass/fail", "Results log"]
README_HEADINGS = ["Purpose", "Status", "Build", "Test", "Flash", "Verify", "Bench check", "Layout",
                   "Changes vs TI", "Known limits"]

STR, INT, BOOL, STRS, INTS = "str", "int", "bool", "list[str]", "list[int]"
SPEC = {
    "project": {"name": STR, "summary": STR, "status": STR},
    "deps": {"sdk": STR, "sdk_version": STR, "toolchain": STR, "download_items": INTS},
    "source": {"baseline": STR, "baseline_commit": STR},
    "build": {"script": STR, "variants": STRS, "est_minutes": INT},
    "artifact": {"file": STR, "board": STR, "descriptor": STR, "flashable": BOOL},
    "flash": {"method": STR, "gate": STR, "port_glob": STR, "mode_steps": STRS, "after_steps": STRS,
              "success_marker": STR, "manual_images": STRS},
    "verify": {"cli_port_glob": STR, "baud": INT, "descriptor": STR},
    "test": {"commands": STRS, "cfgs": STRS, "bench_doc": STR},
    "bench": {"board": STR, "date": STR, "sha256": STR, "result": STR, "doc": STR},
}
REQUIRED = {  # required keys per table (optional ones are omitted here)
    "project": ["name", "summary", "status"],
    "deps": ["sdk", "sdk_version", "toolchain", "download_items"],
    "source": ["baseline", "baseline_commit"],
    "build": ["script", "est_minutes"],
    "artifact": ["file", "board", "descriptor", "flashable"],
    "flash": ["method", "gate", "port_glob", "mode_steps", "after_steps", "success_marker"],
    "verify": ["cli_port_glob", "baud", "descriptor"],
    "test": ["commands", "cfgs"],
    "bench": ["board", "date", "sha256", "result", "doc"],
}
NEEDS = {  # A5
    "stub": ["project", "test"],
    "source": ["project", "test", "deps", "source", "build", "artifact"],
}
NEEDS["built"] = NEEDS["bench"] = NEEDS["source"]


def _type_ok(v, t) -> bool:
    if t == STR:
        return isinstance(v, str)
    if t == INT:
        return isinstance(v, int) and not isinstance(v, bool)
    if t == BOOL:
        return isinstance(v, bool)
    if t == STRS:
        return isinstance(v, list) and all(isinstance(x, str) for x in v)
    if t == INTS:
        return isinstance(v, list) and all(isinstance(x, int) and not isinstance(x, bool) for x in v)
    return False


def _check_table(path: str, d, table: str, errs: list[str]) -> None:
    if not isinstance(d, dict):
        errs.append(f"{path}: must be a table")
        return
    spec = SPEC[table]
    for k, v in d.items():
        if k not in spec:
            errs.append(f"{path}.{k}: unknown key")
        elif not _type_ok(v, spec[k]):
            errs.append(f"{path}.{k}: must be {spec[k]}")
    for k in REQUIRED[table]:
        if k not in d:
            errs.append(f"{path}.{k}: missing")


def validate(data: dict, dirname: str) -> list[str]:
    """Return a list of 'key.path: problem' strings (empty = valid)."""
    errs: list[str] = []
    for k in data:
        if k not in SPEC:
            errs.append(f"{k}: unknown table")
    p = data.get("project")
    status = p.get("status") if isinstance(p, dict) else None
    if status not in STATUSES:
        errs.append(f"project.status: must be one of {'|'.join(STATUSES)}")
    for t in NEEDS.get(status, NEEDS["stub"]):
        if t not in data:
            errs.append(f"{t}: missing table (required for status '{status}')")
    for t in ("project", "deps", "source", "build", "flash", "verify", "test"):
        if t in data:
            _check_table(t, data[t], t, errs)
    arts = data.get("artifact", [])
    benches = data.get("bench", [])
    if "artifact" in data and not (isinstance(arts, list) and arts and all(isinstance(a, dict) for a in arts)):
        errs.append("artifact: must be a non-empty array of tables ([[artifact]])")
        arts = []
    for i, a in enumerate(arts):
        _check_table(f"artifact[{i}]", a, "artifact", errs)
    if "bench" in data and not (isinstance(benches, list) and all(isinstance(b, dict) for b in benches)):
        errs.append("bench: must be an array of tables ([[bench]])")
        benches = []
    for i, b in enumerate(benches):
        _check_table(f"bench[{i}]", b, "bench", errs)
        if b.get("result") not in RESULTS:
            errs.append(f"bench[{i}].result: must be one of {'|'.join(RESULTS)}")
        if isinstance(b.get("sha256"), str) and not re.fullmatch(r"[0-9a-f]{64}", b["sha256"]):
            errs.append(f"bench[{i}].sha256: must be 64 lowercase hex digits")
        if isinstance(b.get("date"), str) and not re.fullmatch(r"\d{4}-\d{2}-\d{2}", b["date"]):
            errs.append(f"bench[{i}].date: must be YYYY-MM-DD")
    if status == "bench" and not benches:
        errs.append("bench: status 'bench' needs at least one [[bench]] record")
    flashable = any(isinstance(a, dict) and a.get("flashable") is True for a in arts)
    if flashable and "flash" not in data:
        errs.append("flash: missing table (a flashable [[artifact]] exists)")
    if any(isinstance(a, dict) and a.get("flashable") is True and a.get("descriptor") for a in arts) \
            and "verify" not in data:
        errs.append("verify: missing table (a flashable artifact has a descriptor)")
    fl = data.get("flash")
    if isinstance(fl, dict):
        if fl.get("method") not in METHODS:
            errs.append(f"flash.method: must be one of {'|'.join(METHODS)}")
        if fl.get("gate") not in GATES:
            errs.append(f"flash.gate: must be one of {'|'.join(GATES)}")
    if isinstance(p, dict) and isinstance(p.get("name"), str) and dirname != TEMPLATE and p["name"] != dirname:
        errs.append(f"project.name: '{p['name']}' does not match the folder name '{dirname}'")
    return errs


class Manifest:
    def __init__(self, name: str, data: dict, legacy: bool):
        self.name, self.data, self.legacy = name, data, legacy
        self.dir = PROJECTS / name

    # convenience accessors
    @property
    def status(self) -> str:
        return self.data["project"]["status"]

    @property
    def artifacts(self) -> list[dict]:
        return self.data.get("artifact", [])

    @property
    def flash(self) -> dict:
        return self.data.get("flash", {})

    @property
    def verify(self) -> dict:
        return self.data.get("verify", {})

    @property
    def test(self) -> dict:
        return self.data.get("test", {})

    @property
    def build(self) -> dict:
        return self.data.get("build", {})

    @property
    def deps(self) -> dict:
        return self.data.get("deps", {})

    @property
    def benches(self) -> list[dict]:
        return self.data.get("bench", [])

    def warnings(self) -> list[str]:
        """Template placeholders are warnings, not errors."""
        out: list[str] = []

        def walk(path, v):
            if isinstance(v, str) and "TODO" in v:
                out.append(f"{path}: TODO placeholder")
            elif isinstance(v, dict):
                for k, x in v.items():
                    walk(f"{path}.{k}" if path else k, x)
            elif isinstance(v, list):
                for i, x in enumerate(v):
                    walk(f"{path}[{i}]", x)
        walk("", self.data)
        return out


def project_names() -> list[str]:
    if not PROJECTS.is_dir():
        return []
    return sorted(d.name for d in PROJECTS.iterdir()
                  if d.is_dir() and d.name != TEMPLATE
                  and ((d / "project.toml").exists() or (d / "project.env").exists()))


def check_name(name: str, allow_template: bool = False) -> Path:
    if not name:
        raise FwExit(USAGE, "missing <project> (run ./fw list)")
    if name == TEMPLATE and not allow_template:
        raise FwExit(USAGE, f"'{TEMPLATE}' is the template, not a project; use ./fw new <project>")
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", name) or not (PROJECTS / name).is_dir():
        raise FwExit(USAGE, f"no project '{name}' in projects/ (run ./fw list)")
    return PROJECTS / name


def load(name: str, allow_template: bool = False) -> Manifest:
    d = check_name(name, allow_template)
    toml = d / "project.toml"
    if toml.exists():
        try:
            data = tomllib.loads(toml.read_text())
        except tomllib.TOMLDecodeError as e:
            raise FwExit(FAILED, f"projects/{name}/project.toml: invalid TOML: {e}")
        errs = validate(data, name)
        if errs:
            raise FwExit(FAILED, f"projects/{name}/project.toml invalid: " + "; ".join(errs), {"errors": errs})
        return Manifest(name, data, legacy=False)
    env = d / "project.env"
    if env.exists():
        Out.warn(f"projects/{name}/project.env is deprecated; it is read until the project migrates to project.toml")
        return Manifest(name, _from_env(name, d, env), legacy=True)
    raise FwExit(FAILED, f"projects/{name}: neither project.toml nor project.env found")


# ---- project.env fallback (fwstd-03 migration aid; remove at fwstd-08) -------------------------

LEGACY_DESCRIPTOR = {"ti_stock_demos": "demo", "awr2243_cascade_ddm": "cascade_ddm",
                     "iwr1843_sar_lvds": "iwr1843_sar_lvds"}
LEGACY_BOARD = (("iwr1843", "IWR1843"), ("iwr6843", "IWR6843"), ("am273x", "AWR2243_CASCADE"))
SOP_STEPS = [
    "The board is in FLASH mode: SOP0 + SOP2 closed (IWR1843BOOST), set with power off.",
    "The board was power-cycled after setting the jumpers.",
    "Nothing else is using the serial port (monitor, driver, ModemManager).",
    "The port is the board's CLI/UART port (XDS110 ...-if00).",
]
J6_STEPS = [
    "The board is in UART flash mode: J6 jumper on the bottom two pins, set with power off.",
    "The board was power-cycled (12 V) after setting the jumper.",
    "Nothing else is using the serial port (monitor, driver, ModemManager).",
    "The port is the board's CLI/UART port (XDS110 ...-if00).",
]


def _from_env(name: str, d: Path, env: Path) -> dict:
    kv = dict(re.findall(r'^([A-Z_]+)="(.*)"\s*$', env.read_text(), re.M))
    arts_f = kv.get("ARTIFACTS", "").split()
    board = kv.get("BOARD", "")
    ccxml = bool(list((d / "configs").glob("*_uniflash.ccxml")))
    cascade = kv.get("SDK") == "mmwave_mcuplus_sdk"
    desc_id = LEGACY_DESCRIPTOR.get(name, "")
    if desc_id and not (DESCRIPTOR_DIR / f"{desc_id}.json").exists():
        desc_id = ""
    artifacts = []
    for f in arts_f:
        b = next((v for p, v in LEGACY_BOARD if f.startswith(p)), board)
        flashable = f.endswith((".bin", ".appimage")) and not f.startswith("iwr6843") and (ccxml or cascade)
        artifacts.append({"file": f, "board": b, "descriptor": desc_id if flashable else "",
                          "flashable": flashable})
    items = [1, 3, 5, 6, 7, 8] if cascade else [2]
    if ccxml:
        items.append(9)
    data = {
        "project": {"name": name, "summary": kv.get("BASELINE", ""), "status": "source"},
        "deps": {"sdk": kv.get("SDK", ""), "sdk_version": kv.get("SDK_VERSION", ""),
                 "toolchain": kv.get("TOOLCHAIN", ""), "download_items": items},
        "source": {"baseline": kv.get("BASELINE", ""), "baseline_commit": kv.get("BASELINE_COMMIT", "")},
        "build": {"script": "build.sh", "variants": ["18xx", "68xx"] if name == "ti_stock_demos" else [],
                  "est_minutes": 10},
        "artifact": artifacts,
        "test": {"commands": [], "cfgs": [], "bench_doc": "docs/bench_check.md"},
    }
    if any(a["flashable"] for a in artifacts):
        if cascade:
            data["flash"] = {"method": "uart_uniflash", "gate": "j6", "port_glob": f"{SERIAL_DIR}/*-if00",
                             "mode_steps": J6_STEPS,
                             "after_steps": ["Move J6 to the top two pins (QSPI boot) and power-cycle (12 V) to run the demo."],
                             "success_marker": "All commands from config file are executed"}
        else:
            data["flash"] = {"method": "dslite", "gate": "sop", "port_glob": f"{SERIAL_DIR}/*-if00",
                             "mode_steps": SOP_STEPS,
                             "after_steps": ["Power off, set functional mode (SOP0 only), power on."],
                             "success_marker": "SUCCESS!! File type META_IMAGE1"}
        if desc_id:
            data["verify"] = {"cli_port_glob": "*-if00", "baud": 115200, "descriptor": desc_id}
    return data


def matches_glob(path: str, pattern: str) -> bool:
    return fnmatch.fnmatch(path, pattern)
