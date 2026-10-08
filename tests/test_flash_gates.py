"""R13/R14: flash gates G1-G7 (A6), tokens (A4), the TTY path (pty) and the non-TTY path."""
import json
import os
import re
import subprocess

import pytest

PHRASE = "FLASH MODE CONFIRMED"


def calls(t, service="flash"):
    return [c for c in t.compose_calls() if c["service"] == service]


def plan(t, port, *extra):
    return t.run("flash", "proj", port, "--plan", "--json", *extra)


# ---- G1 -----------------------------------------------------------------------------------------
def test_gate_G1_image_outside_firmware_dev(built):
    port, _ = built.port()
    out = built.top / "outside.bin"
    out.write_bytes(b"x")
    r = built.run("flash", "proj", port, str(out), "--plan", "--json")
    assert built.result(r)["code"] == 4 and "inside firmware_dev" in built.result(r)["reason"]
    assert not built.compose_calls()


# ---- G2 -----------------------------------------------------------------------------------------
def test_gate_G2_port_must_exist_plan(built):
    r = plan(built, str(built.serial / "usb-nothing-if00"))
    assert built.result(r)["code"] == 4 and "not found" in built.result(r)["reason"]


def test_gate_G2_port_must_exist_tty(built):
    rc, out, err = built.run_tty(["flash", "proj", str(built.serial / "usb-nothing-if00")], [])
    assert rc == 4 and "not found" in err and not built.compose_calls()


# ---- G3 -----------------------------------------------------------------------------------------
def test_gate_G3_port_not_held_plan(built):
    port, _ = built.port()
    built.hold(port)
    r = plan(built, port)
    assert built.result(r)["code"] == 4 and "held" in built.result(r)["reason"]


def test_gate_G3_port_not_held_tty(built):
    port, _ = built.port()
    built.hold(port)
    rc, out, err = built.run_tty(["flash", "proj", port], [PHRASE])
    assert rc == 4 and "held" in err and not built.compose_calls()


# ---- G4 -----------------------------------------------------------------------------------------
def test_gate_G4_non_byid_refused_under_json_plan(built):
    p = built.top / "ttyX"
    p.write_text("")
    r = plan(built, str(p))
    assert built.result(r)["code"] == 4 and "by-id" in built.result(r)["reason"]


def test_gate_G4_non_byid_tty_needs_ack(built):
    p = built.top / "ttyACM9"
    p.write_text("")
    rc, out, err = built.run_tty(["flash", "proj", str(p)], ["no", PHRASE])
    assert rc == 5 and not calls(built)
    rc, out, err = built.run_tty(["flash", "proj", str(p)], ["USE THIS PORT", PHRASE])
    assert rc == 0 and len(calls(built)) == 1


def test_gate_G4_byid_needs_no_ack(built):
    port, _ = built.port()
    rc, out, err = built.run_tty(["flash", "proj", port], [PHRASE])
    assert rc == 0 and "USE THIS PORT" not in out


# ---- G5 -----------------------------------------------------------------------------------------
def test_gate_G5_checklist_from_manifest(built):
    port, _ = built.port()
    res = built.result(plan(built, port))
    assert res["data"]["checklist"] == ["Set SOP", "Power-cycle"]
    rc, out, err = built.run_tty(["flash", "proj", port], [PHRASE])
    assert "1. Set SOP" in out and "2. Power-cycle" in out and "Type exactly" in err


# ---- G6 -----------------------------------------------------------------------------------------
def test_gate_G6_wrong_phrase_exits_5(built):
    port, _ = built.port()
    rc, out, err = built.run_tty(["flash", "proj", port], ["flash mode confirmed"])
    assert rc == 5 and "nothing flashed" in err and not calls(built)


def test_gate_G6_right_phrase_flashes(built):
    port, _ = built.port()
    rc, out, err = built.run_tty(["flash", "proj", port], [PHRASE])
    assert rc == 0, out + err
    c = calls(built)
    assert len(c) == 1 and c[0]["args"][0] == port and c[0]["args"][1].endswith("projects/proj/build/img.bin")
    assert "Restore run mode" in out
    assert "image : projects/proj/build/img.bin" in out and re.search(r"sha256: [0-9a-f]{64}", out)


def test_no_tty_without_token_exits_5_and_starts_nothing(built):
    port, _ = built.port()
    r = built.run("flash", "proj", port)
    assert r.returncode == 5 and not built.compose_calls()
    r = built.run("flash", "proj", port, "--json")
    assert built.result(r)["code"] == 5


# ---- G7 -----------------------------------------------------------------------------------------
def test_gate_G7_dry_run_flashes_nothing(built):
    port, _ = built.port()
    r = built.run("flash", "proj", port, "--dry-run")
    assert r.returncode == 0, r.stderr
    assert "DRY RUN" in r.stdout and "1. Set SOP" in r.stdout and "command: dslite flash" in r.stdout
    assert re.search(r"sha256: [0-9a-f]{64}", r.stdout)
    c = built.compose_calls()
    assert len(c) == 1 and c[0]["service"] == "firmware-env" and "--dry-run" in c[0]["args"]
    assert not (built.top / "flash.log").exists()


def test_dry_run_other_methods_run_no_container(built):
    from conftest import base_manifest, toml_dump
    m = base_manifest()
    m["flash"]["method"] = "uart_uniflash"
    m["flash"]["gate"] = "j6"
    (built.fw / "projects" / "proj" / "project.toml").write_text(toml_dump(m))
    port, _ = built.port()
    r = built.run("flash", "proj", port, "--dry-run", "--json")
    res = built.result(r)
    assert res["code"] == 0 and res["data"]["dry_run"] and len(res["data"]["sha256"]) == 64
    assert not built.compose_calls()


def test_manual_method_exits_3(built):
    from conftest import base_manifest, toml_dump
    m = base_manifest()
    m["flash"]["method"] = "manual"
    (built.fw / "projects" / "proj" / "project.toml").write_text(toml_dump(m))
    port, _ = built.port()
    r = plan(built, port)
    assert built.result(r)["code"] == 3 and not built.compose_calls()


# ---- plan / confirm (A4) ------------------------------------------------------------------------
def test_plan_returns_checklist_and_token_with_private_record(built):
    port, _ = built.port()
    res = built.result(plan(built, port))
    d = res["data"]
    assert res["code"] == 0 and re.fullmatch(r"[0-9a-f]{32}", d["token"]) and d["after_steps"] == ["Restore run mode"]
    assert d["expires_at"] > 0 and len(d["sha256"]) == 64
    f = built.fw / ".fw" / "tokens" / f"{d['token']}.json"
    assert oct(f.stat().st_mode & 0o777) == "0o600"
    rec = json.loads(f.read_text())
    assert rec["project"] == "proj" and rec["sha256"] == d["sha256"] and rec["image"].endswith("build/img.bin")
    assert subprocess.run(["git", "-C", str(built.fw), "check-ignore", "-q", ".fw/tokens/x"]).returncode == 0
    assert not built.compose_calls()  # plan starts no container


def test_confirm_flashes_once_then_token_is_dead(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 0 and len(calls(built)) == 1
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 5 and len(calls(built)) == 1  # reused -> 5, no second flash


def test_confirm_unknown_token_exits_5(built):
    port, _ = built.port()
    r = built.run("flash", "proj", port, "--confirm", "0" * 32, "--json")
    assert built.result(r)["code"] == 5 and not built.compose_calls()
    r = built.run("flash", "proj", port, "--confirm", "../../etc/passwd", "--json")
    assert built.result(r)["code"] == 5


def test_confirm_expired_token_exits_5_and_is_deleted(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    f = built.fw / ".fw" / "tokens" / f"{tok}.json"
    rec = json.loads(f.read_text())
    rec["expires_at"] = 1
    f.write_text(json.dumps(rec))
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 5 and not f.exists() and not built.compose_calls()


def test_new_plan_for_same_port_deletes_older_token(built):
    port, _ = built.port()
    t1 = built.result(plan(built, port))["data"]["token"]
    t2 = built.result(plan(built, port))["data"]["token"]
    assert not (built.fw / ".fw" / "tokens" / f"{t1}.json").exists()
    assert (built.fw / ".fw" / "tokens" / f"{t2}.json").exists()


def test_confirm_with_image_changed_exits_4_and_deletes_record(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    (built.fw / "projects" / "proj" / "build" / "img.bin").write_bytes(b"tampered")
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 4 and "sha256 changed" in built.result(r)["reason"]
    assert not list((built.fw / ".fw" / "tokens").glob("*.json")) and not built.compose_calls()


def test_confirm_with_port_gone_exits_4(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    os.unlink(port)
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 4 and "gone" in built.result(r)["reason"]


def test_confirm_with_port_now_held_exits_4(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    built.hold(port)
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 4 and not built.compose_calls()


def test_confirm_for_other_port_or_project_exits_5(built):
    p1, _ = built.port("usb-a-if00")
    p2, _ = built.port("usb-b-if00")
    tok = built.result(plan(built, p1))["data"]["token"]
    r = built.run("flash", "proj", p2, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 5


def test_confirm_needs_no_typed_phrase_and_no_tty(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok)  # stdin is /dev/null
    assert r.returncode == 0 and len(calls(built)) == 1


def test_flash_tool_failure_exits_1_and_missing_marker_fails(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok, "--json", env={"FAKE_FLASH_RC": "1"})
    assert built.result(r)["code"] == 1
    from conftest import base_manifest, toml_dump
    m = base_manifest()
    m["flash"]["success_marker"] = "NEVER PRINTED"
    (built.fw / "projects" / "proj" / "project.toml").write_text(toml_dump(m))
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")
    assert built.result(r)["code"] == 1 and "marker" in built.result(r)["reason"]


def test_plan_and_confirm_and_dry_run_are_exclusive(built):
    port, _ = built.port()
    assert built.run("flash", "proj", port, "--plan", "--dry-run").returncode == 2
    assert built.run("flash", "proj", port, "--plan", "--confirm", "0" * 32).returncode == 2


# ---- locks (R8) ---------------------------------------------------------------------------------
def lockfile(t, port):
    real = os.path.realpath(port)
    return t.fw / ".fw" / "locks" / re.sub(r"[^A-Za-z0-9_.-]", "_", f"port-{real}").strip("_")


def test_concurrent_flash_second_gets_4_and_dead_lock_is_reclaimed(built):
    port, _ = built.port()
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        tok = built.result(plan(built, port))["data"]["token"]
        lf = lockfile(built, port)
        lf.parent.mkdir(parents=True, exist_ok=True)
        lf.write_text(json.dumps({"pid": sleeper.pid}))
        r = built.run("flash", "proj", port, "--confirm", tok, "--json")
        assert built.result(r)["code"] == 4 and "lock held" in built.result(r)["reason"]
    finally:
        sleeper.kill()
        sleeper.wait()
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok, "--json")  # the lock's PID is dead now
    assert built.result(r)["code"] == 0 and not lf.exists()


# ---- cancellation (R15) -------------------------------------------------------------------------
def test_sigterm_during_flash_kills_child_releases_lock(built):
    import signal
    import time
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    pidfile = built.top / "child.pid"
    env = {**built.env, "FAKE_COMPOSE_MODE": "hang", "FAKE_COMPOSE_PIDFILE": str(pidfile)}
    p = subprocess.Popen([str(built.fw / "fw"), "flash", "proj", port, "--confirm", tok, "--json"], env=env,
                         stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for _ in range(100):
        if pidfile.exists() and pidfile.read_text():
            break
        time.sleep(0.1)
    child = int(pidfile.read_text())
    assert lockfile(built, port).exists()
    p.send_signal(signal.SIGTERM)
    out, err = p.communicate(timeout=20)
    res = json.loads(out.splitlines()[-1])
    assert res["code"] == 1 and res["reason"] == "cancelled" and p.returncode == 1
    assert "Restore run mode" in err  # after_steps printed
    with pytest.raises(ProcessLookupError):
        for _ in range(30):
            os.kill(child, 0)
            time.sleep(0.1)
        os.kill(child, 0)
    assert not lockfile(built, port).exists()


# ---- review fixes D1, D2, S1 --------------------------------------------------------------------
def test_plan_refuses_when_fw_port_lock_is_live_and_issues_no_token(built):
    port, _ = built.port()
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        lf = lockfile(built, port)
        lf.parent.mkdir(parents=True, exist_ok=True)
        lf.write_text(json.dumps({"pid": sleeper.pid}))
        res = built.result(plan(built, port))
        assert res["code"] == 4 and "lock held" in res["reason"]
        assert not list((built.fw / ".fw" / "tokens").glob("*")) if (built.fw / ".fw" / "tokens").exists() else True
    finally:
        sleeper.kill()
        sleeper.wait()


def test_confirm_with_image_deleted_exits_4_image_missing(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    (built.fw / "projects" / "proj" / "build" / "img.bin").unlink()
    res = built.result(built.run("flash", "proj", port, "--confirm", tok, "--json"))
    assert res["code"] == 4 and "image missing" in res["reason"] and not built.compose_calls()


def test_token_claim_is_atomic_two_confirms_one_wins(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    ps = [subprocess.Popen([str(built.fw / "fw"), "flash", "proj", port, "--confirm", tok, "--json"],
                           env=built.env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True) for _ in range(2)]
    codes = sorted(json.loads(p.communicate(timeout=30)[0].splitlines()[-1])["code"] for p in ps)
    assert codes in ([0, 4], [0, 5]) and len(calls(built)) == 1  # 4 if it hit the port lock, else 5



# ---- per-image manual flash ([flash].manual_images, fwstd-07 amendment) --------------------------
MANUAL_FLASH_SH = """#!/bin/bash
DRY=0; POS=()
for a in "$@"; do case "$a" in --dry-run) DRY=1;; *) POS+=("$a");; esac; done
case "${POS[1]}" in
    *man.bin) echo "No headless flasher: flash man.bin by hand."; echo "  1. Set SOP-MANUAL"; echo "  2. UniFlash GUI"; exit 3 ;;
esac
echo "$@" >> "$FAKE_FLASH_LOG"
echo "SUCCESS!! File type META_IMAGE1"
"""


def _with_manual_image(t):
    from conftest import base_manifest, toml_dump
    m = base_manifest()
    m["artifact"].append({"file": "man.bin", "board": "IWR9999", "descriptor": "dummy", "flashable": True})
    m["flash"]["manual_images"] = ["man.bin"]
    d = t.fw / "projects" / "proj"
    (d / "project.toml").write_text(toml_dump(m))
    (d / "flash.sh").write_text(MANUAL_FLASH_SH)
    (d / "build" / "man.bin").write_bytes(b"manual")
    return d / "build" / "man.bin"


def test_plan_manual_image_uses_flash_sh_steps_not_manifest_sop(built):
    img = _with_manual_image(built)
    port, _ = built.port()
    res = built.result(plan(built, port, str(img)))
    assert res["code"] == 3 and res["data"]["checklist"] == [
        "No headless flasher: flash man.bin by hand.", "1. Set SOP-MANUAL", "2. UniFlash GUI"]
    assert "Set SOP" not in " ".join(res["data"]["checklist"]).replace("SOP-MANUAL", "")
    assert "token" not in res["data"]
    assert not list((built.fw / ".fw" / "tokens").glob("*.json")) if (built.fw / ".fw" / "tokens").exists() else True


def test_plan_dslite_image_keeps_manifest_checklist_when_another_image_is_manual(built):
    _with_manual_image(built)
    port, _ = built.port()
    res = built.result(plan(built, port))                     # default image img.bin is not manual
    assert res["code"] == 0 and res["data"]["checklist"] == ["Set SOP", "Power-cycle"]
    assert not built.compose_calls()


def test_confirm_exit3_carries_flash_sh_output_in_checklist(built):
    port, _ = built.port()
    tok = built.result(plan(built, port))["data"]["token"]
    r = built.run("flash", "proj", port, "--confirm", tok, "--json", env={"FAKE_FLASH_RC": "3"})
    res = built.result(r)
    assert res["code"] == 3 and res["data"]["checklist"][-1] == "SUCCESS!! File type META_IMAGE1"
