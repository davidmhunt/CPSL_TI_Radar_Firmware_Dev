"""R1-R6, R22, A5: manifest validation, the project.env fallback, the template."""
import json
import re

from conftest import base_manifest, toml_dump


def test_valid_manifest_lists(tree):
    tree.add_project()
    r = tree.run("list", "--json")
    d = tree.result(r)["data"]["projects"][0]
    assert d["name"] == "proj" and d["status"] == "source" and d["build"] == "none"
    assert d["artifacts"][0] == {"file": "img.bin", "board": "IWR9999", "descriptor": "dummy", "flashable": True}


def bad(tree, mutate, expect):
    m = base_manifest()
    mutate(m)
    tree.add_project(manifest=m)
    r = tree.run("build", "proj", "--dry-run", "--json")
    res = tree.result(r)
    assert res["code"] == 1 and expect in res["reason"], res["reason"]


def test_missing_key_names_path(tree):
    bad(tree, lambda m: m["deps"].pop("sdk"), "deps.sdk: missing")


def test_unknown_key(tree):
    bad(tree, lambda m: m["verify"].update(probes=["x"]), "verify.probes: unknown key")


def test_wrong_type(tree):
    bad(tree, lambda m: m["build"].update(est_minutes="ten"), "build.est_minutes: must be int")


def test_bad_enum(tree):
    bad(tree, lambda m: m["project"].update(status="done"), "project.status")
    bad(tree, lambda m: m["flash"].update(method="usb"), "flash.method")
    bad(tree, lambda m: m["flash"].update(gate="x"), "flash.gate")


def test_name_must_match_folder(tree):
    bad(tree, lambda m: m["project"].update(name="other"), "does not match the folder")


def test_tables_required_by_status(tree):
    bad(tree, lambda m: m.pop("deps"), "deps: missing table")
    bad(tree, lambda m: m.pop("flash"), "flash: missing table")
    bad(tree, lambda m: m.pop("verify"), "verify: missing table")


def test_stub_needs_only_project_and_test(tree):
    m = {"project": {"name": "proj", "summary": "s", "status": "stub"},
         "test": {"commands": [], "cfgs": [], "bench_doc": "docs/bench_check.md"}}
    tree.add_project(manifest=m)
    assert tree.run("list", "--json").returncode == 0
    assert tree.result(tree.run("list", "--json"))["data"]["projects"][0]["status"] == "stub"


def test_bench_status_needs_matching_pass_record(built):
    p = built.fw / "projects" / "proj" / "project.toml"
    m = base_manifest()
    m["project"]["status"] = "bench"
    sha = __import__("hashlib").sha256(b"image-1").hexdigest()
    m["bench"] = [{"board": "IWR9999", "date": "2026-10-08", "sha256": "0" * 64, "result": "pass",
                   "doc": "docs/bench_check.md"}]
    p.write_text(toml_dump(m))
    d = built.result(built.run("list", "--json"))["data"]["projects"][0]
    assert d["status"] == "built" and "bench" in d["reason"]
    m["bench"][0]["sha256"] = sha
    p.write_text(toml_dump(m))
    assert built.result(built.run("list", "--json"))["data"]["projects"][0]["status"] == "bench"


def test_bench_record_validation(tree):
    m = base_manifest()
    m["bench"] = [{"board": "b", "date": "yesterday", "sha256": "zz", "result": "great", "doc": "d"}]
    tree.add_project(manifest=m)
    res = tree.result(tree.run("build", "proj", "--dry-run", "--json"))
    assert res["code"] == 1 and "bench[0].result" in res["reason"] and "bench[0].date" in res["reason"]


def test_built_without_record_reports_source(tree):
    tree.add_project()
    d = tree.result(tree.run("list", "--json"))["data"]["projects"][0]
    assert d["status"] == "source" and "no build record" in d["reason"]


def test_project_env_fallback_has_deprecation_line(tree):
    p = tree.fw / "projects" / "old"
    p.mkdir(parents=True)
    (p / "project.env").write_text('BOARD="IWR1843BOOST"\nSDK="mmwave_sdk"\nSDK_VERSION="3"\nTOOLCHAIN="t"\n'
                                  'BASELINE="b"\nBASELINE_COMMIT="c"\nARTIFACTS="old.bin old.elf"\n')
    (p / "build.sh").write_text("#!/bin/bash\n")
    r = tree.run("list", "--json")
    assert "project.env is deprecated" in r.stderr
    d = tree.result(r)["data"]["projects"][0]
    assert d["name"] == "old" and d["artifacts"][0]["file"] == "old.bin"


def test_cfgs_ignore_build_dir(tree):
    tree.add_project(files={"build/x.cfg": "stray"})
    m = base_manifest()
    m["test"]["cfgs"] = ["**/*.cfg"]
    (tree.fw / "projects" / "proj" / "project.toml").write_text(toml_dump(m))
    (tree.fw / "projects" / "proj" / "configs" / "a.cfg").unlink()
    r = tree.run("test", "proj", "--json")
    assert tree.result(r)["code"] == 1 and "matches no tracked file" in r.stdout  # stray build/x.cfg ignored


def test_test_verb_passes_clean_project_and_checks_headings(tree):
    tree.add_project()
    assert tree.run("test", "proj", "--json").returncode == 0
    (tree.fw / "projects" / "proj" / "docs" / "bench_check.md").write_text("## Steps\n")
    r = tree.run("test", "proj", "--json")
    assert r.returncode == 1 and "heading 'Preconditions' missing" in r.stdout
    (tree.fw / "projects" / "proj" / "docs" / "bench_check.md").write_text(
        "\n".join(f"## {h}" for h in ["Preconditions", "Board state", "Steps", "Expected output", "Pass/fail",
                                       "Results log"]))
    (tree.fw / "projects" / "proj" / "README.md").write_text("## Purpose\nmeasured 2026-10-08\n")
    r = tree.run("test", "proj", "--json")
    assert r.returncode == 1 and "contains a date" in r.stdout


def test_descriptor_consistency(tree):
    m = base_manifest()
    m["artifact"][0]["descriptor"] = "missing_desc"
    tree.add_project(manifest=m)
    r = tree.run("test", "proj", "--json")
    assert r.returncode == 1 and "missing_desc" in r.stdout


def test_descriptor_backlink_mismatch(tree):
    d = {"identify": {"IWR9999": {"probes": [], "source": {"fw_project": "someone_else", "artifact": "img.bin"}}}}
    tree.descriptor("dummy", d)
    tree.add_project()
    r = tree.run("test", "proj", "--json")
    assert r.returncode == 1 and "source.fw_project" in r.stdout
    d["identify"]["IWR9999"]["source"] = {"fw_project": "proj", "artifact": "nope.bin"}
    tree.descriptor("dummy", d)
    assert "source.artifact" in tree.run("test", "proj", "--json").stdout
    d["identify"]["IWR9999"]["source"] = {"fw_project": "proj", "artifact": "img.bin"}
    tree.descriptor("dummy", d)
    assert tree.run("test", "proj", "--json").returncode == 0


def test_template_new_then_test_passes_with_todo_warnings(tree):
    r = tree.run("new", "my_demo", "--json")
    assert r.returncode == 0, r.stdout + r.stderr
    t = tree.run("test", "my_demo", "--json")
    res = tree.result(t)
    assert res["code"] == 0, t.stdout + t.stderr
    assert res["data"]["projects"]["my_demo"]["warnings"]  # TODOs are warnings
    assert "TODO" in t.stderr
    assert not (tree.fw / "projects" / "my_demo" / "project.env").exists()
    assert "PROJECT_NAME" not in (tree.fw / "projects" / "my_demo" / "project.toml").read_text()
    rl = tree.result(tree.run("list", "--json"))["data"]["projects"]
    assert any(p["name"] == "my_demo" and p["status"] == "stub" for p in rl)


def test_new_refuses_bad_names_and_overwrite(tree):
    assert tree.run("new", "Bad-Name").returncode == 2
    tree.run("new", "ok_one")
    assert tree.run("new", "ok_one").returncode == 2
