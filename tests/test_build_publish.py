"""R17-R19, A7: build records, dirty/stale states, deps, publish."""
import hashlib
import json
import shutil
import subprocess

from conftest import base_manifest, toml_dump


def state(t):
    return t.result(t.run("list", "--json"))["data"]["projects"][0]["build"]


def test_build_writes_record_and_streams_progress(tree):
    tree.add_project()
    tree.commit()
    r = tree.run("build", "proj", "--variant", "a", "--json")
    ev = tree.events(r)
    assert ev[0]["event"] == "progress" and any(e.get("percent") == 50 for e in ev)
    res = ev[-1]
    assert res["event"] == "result" and res["code"] == 0 and res["verb"] == "build"
    rec = res["data"]["build_info"]
    head = tree.git("rev-parse", "--short", "HEAD")
    assert rec["firmware_dev_commit"] == head and rec["dirty"] is False and rec["variant"] == "a"
    assert rec["artifacts"]["img.bin"] == hashlib.sha256(b"image-1").hexdigest()
    assert set(rec["artifacts"]) == {"img.bin", "img.elf"}
    on_disk = json.loads((tree.fw / "projects" / "proj" / "build" / "build_info.json").read_text())
    assert on_disk == rec
    c = tree.compose_calls()[0]
    assert c["env"]["FW_VARIANT"] == "a" and c["env"]["FW_COMMIT"] == head and c["args"] == ["a"]
    assert state(tree) == "clean"


def test_build_dirty_flag_and_state(tree):
    tree.add_project()
    tree.commit()
    (tree.fw / "projects" / "proj" / "src" / "new.c").write_text("x")
    res = tree.result(tree.run("build", "proj", "--json"))
    assert res["data"]["build_info"]["dirty"] is True
    assert tree.compose_calls()[0]["env"]["FW_COMMIT"].endswith("-dirty")
    assert state(tree) == "dirty"


def test_A7_states(built):
    assert state(built) == "clean"
    p = built.fw / "projects" / "proj"
    (p / "src" / "x.c").write_text("edit")
    assert state(built) == "dirty"               # uncommitted change in the project
    built.commit("edit")
    assert state(built) == "stale"               # HEAD moved over the project's files
    built.run("build", "proj")
    assert state(built) == "clean"
    (p / "build" / "img.bin").write_bytes(b"other")
    assert state(built) == "stale"               # artifact differs from the record
    built.run("build", "proj")
    (p / "build" / "build_info.json").unlink()
    (p / "build" / "build_info.txt").write_text("legacy")
    assert state(built) == "stale"               # only legacy build_info.txt
    shutil.rmtree(p / "build")
    assert state(built) == "none"
    # fw / tools changes also make builds stale
    built.run("build", "proj")
    (built.fw / "tools" / "fwcli" / "__init__.py").write_text("# changed\n")
    built.commit("fwcli")
    assert state(built) == "stale"


def test_unrelated_project_change_does_not_stale(built):
    built.add_project("other")
    built.commit("other")
    assert [p["build"] for p in built.result(built.run("list", "--json"))["data"]["projects"]
            if p["name"] == "proj"] == ["clean"]


def test_build_git_unavailable_exits_4(tree):
    tree.add_project()
    shutil.rmtree(tree.fw / ".git")
    res = tree.result(tree.run("build", "proj", "--json", env={"GIT_CEILING_DIRECTORIES": str(tree.top)}))
    assert res["code"] == 4 and "git" in res["reason"] and not tree.compose_calls()


def test_build_deps_missing_exits_4_and_deps_verb(tree):
    m = base_manifest()
    m["deps"]["download_items"] = [2]
    tree.add_project(manifest=m)
    tree.commit()
    assert tree.result(tree.run("build", "proj", "--json"))["code"] == 4
    assert tree.result(tree.run("deps", "proj", "--json"))["code"] == 4
    assert not tree.compose_calls()
    assert tree.result(tree.run("list", "--json"))["data"]["projects"][0]["deps_available"] is False
    (tree.fw / "downloads" / "mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin").write_text("x")
    assert tree.result(tree.run("deps", "proj", "--json"))["code"] == 0
    assert tree.result(tree.run("build", "proj", "--json"))["code"] == 0


def test_build_tool_failure_exits_1_no_record(tree):
    tree.add_project()
    tree.commit()
    res = tree.result(tree.run("build", "proj", "--json", env={"FAKE_BUILD_RC": "3"}))
    assert res["code"] == 1 and not (tree.fw / "projects" / "proj" / "build" / "build_info.json").exists()


def test_build_unknown_variant_exits_2(tree):
    tree.add_project()
    assert tree.result(tree.run("build", "proj", "--variant", "zzz", "--json"))["code"] == 2


def test_build_dry_run_starts_nothing(tree):
    tree.add_project()
    tree.commit()
    res = tree.result(tree.run("build", "proj", "--dry-run", "--json"))
    assert res["code"] == 0 and res["data"]["command"][:2] == [
        tree.env["FW_COMPOSE"].split()[0], tree.env["FW_COMPOSE"].split()[1]] and not tree.compose_calls()


def test_build_lock_held_exits_4(tree):
    tree.add_project()
    tree.commit()
    sleeper = subprocess.Popen(["sleep", "30"])
    try:
        (tree.fw / ".fw" / "locks").mkdir(parents=True)
        (tree.fw / ".fw" / "locks" / "project-proj").write_text(json.dumps({"pid": sleeper.pid}))
        assert tree.result(tree.run("build", "proj", "--json"))["code"] == 4
    finally:
        sleeper.kill()
        sleeper.wait()


# ---- publish -------------------------------------------------------------------------------------
def test_publish_copies_flashable_with_provenance(built, tmp_path):
    dest = tmp_path / "ship"
    res = built.result(built.run("publish", "proj", "--dest", str(dest), "--json"))
    assert res["code"] == 0
    img = dest / "IWR9999" / "dummy" / "img.bin"
    assert img.read_bytes() == b"image-1" and res["data"]["paths"] == [str(img)]
    prov = json.loads((dest / "IWR9999" / "dummy" / "img.bin.provenance.json").read_text())
    assert prov["sha256"] == hashlib.sha256(b"image-1").hexdigest() and prov["project"] == "proj"
    assert prov["firmware_dev_commit"] == built.git("rev-parse", "--short", "HEAD")
    assert prov["toolchain"] == "tc 1" and prov["chirp_cfg"][0]["path"] == "configs/a.cfg"
    assert res["data"]["skipped"] == ["img.elf"]               # not flashable / no descriptor
    assert not (dest / "IWR9999" / "dummy" / "img.elf").exists()


def test_publish_refuses_dirty_stale_edited_none(built, tmp_path):
    dest = tmp_path / "ship"
    p = built.fw / "projects" / "proj"
    (p / "src" / "x.c").write_text("e")                          # dirty
    assert built.result(built.run("publish", "proj", "--dest", str(dest), "--json"))["code"] == 4
    built.commit("e")                                            # stale
    assert built.result(built.run("publish", "proj", "--dest", str(dest), "--json"))["code"] == 4
    built.run("build", "proj")
    (p / "build" / "img.bin").write_bytes(b"edited")             # edited image
    assert built.result(built.run("publish", "proj", "--dest", str(dest), "--json"))["code"] == 4
    shutil.rmtree(p / "build")                                   # never built
    assert built.result(built.run("publish", "proj", "--dest", str(dest), "--json"))["code"] == 4
    assert not dest.exists()                                     # nothing written


def test_publish_default_dest_is_shipped_firmware(built):
    assert built.result(built.run("publish", "proj", "--dry-run", "--json"))["data"]["paths"][0].startswith(
        str(built.top / "shipped_firmware"))
    assert not (built.top / "shipped_firmware").exists()


def test_sigterm_during_build_kills_child_releases_project_lock(tree):
    import os
    import signal
    import time
    tree.add_project()
    tree.commit()
    pidfile = tree.top / "child.pid"
    env = {**tree.env, "FAKE_COMPOSE_MODE": "hang", "FAKE_COMPOSE_PIDFILE": str(pidfile)}
    p = subprocess.Popen([str(tree.fw / "fw"), "build", "proj", "--json"], env=env, stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for _ in range(100):
        if pidfile.exists() and pidfile.read_text():
            break
        time.sleep(0.1)
    child = int(pidfile.read_text())
    assert (tree.fw / ".fw" / "locks" / "project-proj").exists()
    p.send_signal(signal.SIGINT)
    out, _ = p.communicate(timeout=20)
    res = json.loads(out.splitlines()[-1])
    assert res["code"] == 1 and res["reason"] == "cancelled"
    time.sleep(0.2)
    try:
        os.kill(child, 0)
        alive = True
    except ProcessLookupError:
        alive = False
    assert not alive and not (tree.fw / ".fw" / "locks" / "project-proj").exists()


# ---- A7 exemption: [[bench]] / [project].status edits do not dirty or stale a build (fwstd-07) ------
def _toml(t):
    return t.fw / "projects" / "proj" / "project.toml"


def _bench_edit(t):
    s = _toml(t).read_text().replace('status = "built"', 'status = "bench"')
    _toml(t).write_text(s + '\n[[bench]]\nboard = "IWR9999"\ndate = "2026-10-08"\nsha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"\nresult = "pass"\ndoc = "d.md"\n')


def _other_edit(t):
    _toml(t).write_text(_toml(t).read_text().replace('summary = "test fw"', 'summary = "changed"'))


def test_A7_bench_only_commit_stays_clean(built):
    _bench_edit(built)
    built.commit("bench record")
    assert state(built) == "clean"


def test_A7_bench_plus_other_key_commit_is_stale(built):
    _bench_edit(built)
    _other_edit(built)
    built.commit("bench + summary")
    assert state(built) == "stale"


def test_A7_bench_only_uncommitted_stays_clean(built):
    _bench_edit(built)
    assert state(built) == "clean"


def test_A7_bench_plus_other_key_uncommitted_is_dirty(built):
    _bench_edit(built)
    _other_edit(built)
    assert state(built) == "dirty"


def test_A7_bench_commit_plus_other_file_is_stale(built):
    _bench_edit(built)
    (built.fw / "projects" / "proj" / "src").mkdir(exist_ok=True)
    (built.fw / "projects" / "proj" / "src" / "y.c").write_text("y")
    built.commit("bench + src")
    assert state(built) == "stale"
