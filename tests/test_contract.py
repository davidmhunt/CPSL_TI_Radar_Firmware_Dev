"""R10-R12: JSON Lines shape, one A1 exit-code test per row, no hangs without a TTY, A3 shape."""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from conftest import REAL

PHRASE = "FLASH MODE CONFIRMED"


def check_lines(t, r):
    """Every stdout line is valid JSON with a known event; exactly one result, last."""
    ev = [json.loads(ln) for ln in r.stdout.splitlines()]
    assert ev and ev[-1]["event"] == "result"
    assert [e["event"] for e in ev].count("result") == 1
    for e in ev:
        assert e["event"] in ("progress", "result") and isinstance(e["verb"], str)
        if e["event"] == "progress":
            assert {"stage", "message"} <= set(e)
        else:
            assert {"ok", "code", "reason", "data"} <= set(e) and e["ok"] == (e["code"] == 0)
    return ev[-1]


VERBS = [("list",), ("ports",), ("deps", "proj"), ("build", "proj", "--dry-run"), ("test", "proj"),
         ("flash", "proj", "/nonexistent", "--dry-run"), ("verify", "proj", "--port", "/x"),
         ("publish", "proj", "--dry-run"), ("help",), ("new", "Bad Name"), ("nosuchverb",), ("flash",)]


@pytest.mark.parametrize("verb", VERBS, ids=lambda v: v[0] + (f"_{v[-1]}" if len(v) > 1 else ""))
def test_every_verb_json_is_valid_and_never_hangs(built, verb):
    r = built.run(*verb, "--json", timeout=10)            # stdin is /dev/null, 10 s cap
    res = check_lines(built, r)
    assert res["code"] in (0, 1, 2, 3, 4, 5)


@pytest.mark.parametrize("verb", ["list", "build", "flash", "verify", "publish", "deps", "ports", "test", "new"])
def test_every_verb_has_help(built, verb):
    r = built.run(verb, "--help")
    assert r.returncode == 0 and "--json" in r.stdout


def test_help_json_is_one_result(built):
    res = check_lines(built, built.run("verb-does-not-exist", "--json"))
    assert res["code"] == 2
    res = check_lines(built, built.run("flash", "--help", "--json"))
    assert res["code"] == 0


def test_text_goes_to_stderr_under_json(built):
    r = built.run("ports", "--json")
    check_lines(built, r)
    r = built.run("build", "proj", "--dry-run", "--json")
    assert "DRY RUN" in r.stderr and "DRY RUN" not in r.stdout


# ---- A1 rows (R12) ------------------------------------------------------------------------------
def code(t, *a, **kw):
    return check_lines(t, t.run(*a, "--json", **kw))["code"]


def test_A1_0_success(built):
    assert code(built, "list") == 0


def test_A1_1_tool_failed_bad_manifest(built):
    assert code(built, "build", "proj", env={"FAKE_BUILD_RC": "2"}) == 1
    (built.fw / "projects" / "proj" / "project.toml").write_text("[project]\nname = 'proj'\n")
    assert code(built, "build", "proj") == 1
    (built.fw / "projects" / "proj" / "project.toml").write_text("not = [toml")
    assert code(built, "build", "proj") == 1


def test_A1_2_usage(built):
    assert code(built, "build") == 2
    assert code(built, "build", "no_such_project") == 2
    assert code(built, "build", "_template") == 2
    assert code(built, "flash", "proj") == 2


def test_A1_3_manual_or_unsupported(built):
    from conftest import base_manifest, toml_dump
    m = base_manifest()
    m["flash"]["method"] = "manual"
    (built.fw / "projects" / "proj" / "project.toml").write_text(toml_dump(m))
    port, _ = built.port()
    assert code(built, "flash", "proj", port, "--plan") == 3


def test_A1_4_precondition_refused(built):
    port, _ = built.port()
    assert code(built, "flash", "proj", str(built.serial / "usb-none-if00"), "--plan") == 4   # port missing
    built.hold(port)
    assert code(built, "flash", "proj", port, "--plan") == 4                                 # held
    shutil.rmtree(built.fw / "projects" / "proj" / "build")
    assert code(built, "flash", "proj", port, "--plan") == 4                                 # image missing


def test_A1_5_confirmation(built):
    port, _ = built.port()
    assert code(built, "flash", "proj", port) == 5
    assert code(built, "flash", "proj", port, "--confirm", "f" * 32) == 5


def test_only_a1_codes_for_python_errors(built):
    (built.fw / "projects" / "proj" / "project.toml").write_text("[project]\nname = 5\n")
    assert code(built, "list") == 0


# ---- A3 --------------------------------------------------------------------------------------------
def test_A3_list_shape(built):
    d = check_lines(built, built.run("list", "--json"))["data"]["projects"][0]
    assert set(d) >= {"name", "status", "artifacts", "bench", "deps_available", "build"}
    assert d["build"] in ("none", "clean", "dirty", "stale") and isinstance(d["deps_available"], bool)
    assert all(set(a) == {"file", "board", "descriptor", "flashable"} for a in d["artifacts"])


def test_ports_lists_held_flag(built):
    p1, _ = built.port("usb-a-if00")
    p2, _ = built.port("usb-b-if00")
    built.hold(p2)
    ports = check_lines(built, built.run("ports", "proj", "--json"))["data"]["ports"]
    assert {(p["path"], p["held"]) for p in ports} == {(p1, False), (p2, True)}


# ---- the real tree (read only) ------------------------------------------------------------------
def real_fw(*a):
    return subprocess.run([str(REAL / "fw"), *a], capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=60)


def test_real_tree_lists_projects_with_deprecation_note():
    r = real_fw("list", "--json")
    res = check_lines(None, r)
    names = {p["name"] for p in res["data"]["projects"]}
    assert res["code"] == 0 and {"iwr1843_sar_lvds", "ti_stock_demos", "awr2243_cascade_ddm"} <= names
    assert "project.env is deprecated" in r.stderr or any(
        (REAL / "projects" / n / "project.toml").exists() for n in names)


def test_real_tree_no_tty_flash_exits_5_without_container():
    r = real_fw("flash", "iwr1843_sar_lvds", "/dev/null")
    assert r.returncode in (4, 5)           # 5 with a build present, 4 when the image is absent
    assert "container" not in r.stdout
