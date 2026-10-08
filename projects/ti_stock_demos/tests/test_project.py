"""Hardware-free host tests for ti_stock_demos. `./fw test ti_stock_demos` runs them (stdlib unittest)."""
import json
import re
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
DESCRIPTORS = HERE.parents[2] / "CPSL_TI_Radar_cpp" / "config" / "firmware"   # parent repo; absent in a standalone checkout
MANIFEST = tomllib.loads((HERE / "project.toml").read_text())
ARTIFACTS = {a["file"]: a for a in MANIFEST["artifact"]}
CFG_BOARD = {"xwr18xx": "IWR1843", "xwr68xx": "IWR6843"}   # cfg folder -> board of its default image


def descriptor():
    f = DESCRIPTORS / "demo.json"
    if not f.is_file():
        raise unittest.SkipTest("descriptor demo.json not available (standalone firmware_dev checkout)")
    return json.loads(f.read_text())


def commands(cfg: Path) -> list[str]:
    """First word of every non-comment, non-blank line of a TI cfg."""
    out = []
    for ln in cfg.read_text().splitlines():
        ln = ln.strip()
        if ln and not ln.startswith("%"):
            out.append(ln.split()[0])
    return out


class ManifestTest(unittest.TestCase):
    def test_name_and_scripts(self):
        self.assertEqual(MANIFEST["project"]["name"], HERE.name)
        for s in ("build.sh", "flash.sh"):
            self.assertTrue((HERE / s).is_file(), s)
        self.assertFalse((HERE / "project.env").exists(), "project.env must be gone after migration")

    def test_four_artifacts_in_order(self):
        self.assertEqual(list(ARTIFACTS), ["iwr1843_demo.bin", "iwr1843_demo.elf", "iwr6843_demo.bin", "iwr6843_demo.elf"])
        self.assertEqual(MANIFEST["build"]["variants"], ["18xx", "68xx"])

    def test_bins_have_demo_descriptor_elfs_do_not(self):
        for f, a in ARTIFACTS.items():
            self.assertEqual(a["descriptor"], "demo" if f.endswith(".bin") else "", f)
            self.assertEqual(a["flashable"], f.endswith(".bin"), f)
        self.assertEqual(ARTIFACTS["iwr1843_demo.bin"]["board"], "IWR1843")
        self.assertEqual(ARTIFACTS["iwr6843_demo.bin"]["board"], "IWR6843")

    def test_bench_records_use_the_default_image_and_a_real_doc(self):
        for b in MANIFEST.get("bench", []):
            self.assertTrue((HERE / b["doc"]).is_file(), b)
            self.assertRegex(b["sha256"], r"^[0-9a-f]{64}$")


class DescriptorTest(unittest.TestCase):
    def test_identify_back_links(self):
        ident = descriptor()["identify"]
        for f, a in ARTIFACTS.items():
            if not a["descriptor"]:
                continue
            src = ident[a["board"]]["source"]
            self.assertEqual(src, {"fw_project": HERE.name, "artifact": f})

    def test_verify_table_matches(self):
        self.assertEqual(MANIFEST["verify"]["descriptor"], "demo")
        self.assertIn("version", [p["cmd"] for p in descriptor()["identify"]["IWR1843"]["probes"]])

    def test_verify_probes_are_not_cfg_commands(self):
        for board, ent in descriptor()["identify"].items():
            for p in ent["probes"]:
                self.assertNotRegex(p["cmd"], r"(?i)^(sensorStart|sensorStop|flushCfg|\w+Cfg)\b", (board, p["cmd"]))


class CfgTest(unittest.TestCase):
    def test_cfgs_listed_and_tracked(self):
        for g in MANIFEST["test"]["cfgs"]:
            self.assertTrue(list(HERE.glob(g)), g)

    def test_profiles_parse_and_meet_cfg_rules(self):
        rules = descriptor().get("cfg_rules", {})
        for fam, board in CFG_BOARD.items():
            cfgs = sorted((HERE / "configs" / fam).glob("*.cfg"))
            self.assertTrue(cfgs, fam)
            r = rules.get(board, {})
            for c in cfgs:
                cmds = commands(c)
                self.assertTrue(cmds, c.name)
                for w in cmds:
                    self.assertRegex(w, r"^[A-Za-z]\w*$", (c.name, w))
                for req in r.get("required_commands", []):
                    self.assertIn(req, cmds, f"{fam}/{c.name} lacks required '{req}' for {board}")
                for bad in r.get("forbidden_commands", []):
                    self.assertNotIn(bad, cmds, f"{fam}/{c.name} has forbidden '{bad}' for {board}")


class ScriptContractTest(unittest.TestCase):
    def test_scripts_use_standard_env_not_project_env(self):
        build = (HERE / "build.sh").read_text()
        flash = (HERE / "flash.sh").read_text()
        for t in (build, flash):
            self.assertNotIn("project.env", re.sub(r"#.*", "", t))   # comments may mention it
        self.assertIn("FW_VARIANT", build)
        self.assertIn("FW_COMMIT", build)
        for v in ("FW_PORT", "FW_IMAGE", "FW_DRY_RUN"):
            self.assertIn(v, flash)

    def test_flash_refuses_6843_as_manual(self):
        flash = (HERE / "flash.sh").read_text()
        self.assertRegex(flash, r"\*6843\*\|\*68xx\*\)[\s\S]*?exit 3")


if __name__ == "__main__":
    unittest.main()
