"""Hardware-free host tests for awr2243_cascade_ddm. `./fw test awr2243_cascade_ddm` runs them (stdlib unittest).

Needs no board and no network. The flush-patch anchor test reads one file out of the firmware-env image
and is skipped (with a reason) when Docker or the image is absent, or when FW_SKIP_DOCKER_TESTS=1.
"""
import json
import os
import re
import shutil
import subprocess
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
FW_ROOT = HERE.parents[1]
DESCRIPTOR = FW_ROOT.parent / "CPSL_TI_Radar_cpp" / "config" / "firmware" / "cascade_ddm.json"
BOARD = "AWR2243_CASCADE"
IMAGE = "cpsl-ti-radar-firmware-dev:latest"
UNIFLASH_IN_IMAGE = "/opt/ti/mcu_plus_sdk_am273x_08_05_00_24/tools/boot/uart_uniflash.py"


def manifest():
    return tomllib.loads((HERE / "project.toml").read_text())


def descriptor():
    return json.loads(DESCRIPTOR.read_text())


def cfg_files():
    out = []
    for g in manifest()["test"]["cfgs"]:
        out += sorted(p for p in HERE.glob(g) if p.is_file() and "build" not in p.relative_to(HERE).parts)
    return out


def cfg_commands(path):
    """Yield (command, [args]) for every non-comment, non-blank line of a cfg."""
    for ln in path.read_text().splitlines():
        ln = ln.strip()
        if ln and not ln.startswith("%"):
            parts = ln.split()
            yield parts[0], parts[1:]


class ManifestTest(unittest.TestCase):
    def test_manifest_names_the_folder_and_scripts_exist(self):
        m = manifest()
        self.assertEqual(m["project"]["name"], HERE.name)
        for s in ("build.sh", "flash.sh"):
            self.assertTrue((HERE / s).is_file(), s)
        self.assertFalse((HERE / "project.env").exists(), "project.env must be gone after migration")

    def test_artifacts(self):
        arts = {a["file"]: a for a in manifest()["artifact"]}
        self.assertEqual(list(arts)[0], "am273x_cascade.appimage", "first artifact is the default image")
        self.assertEqual(set(arts), {"am273x_cascade.appimage", "am273x_cascade.elf", "am273x_cascade_dss.xe66"})
        self.assertTrue(arts["am273x_cascade.appimage"]["flashable"])
        self.assertEqual(arts["am273x_cascade.appimage"]["descriptor"], "cascade_ddm")
        for f in ("am273x_cascade.elf", "am273x_cascade_dss.xe66"):
            self.assertFalse(arts[f]["flashable"], f)
            self.assertEqual(arts[f]["descriptor"], "", f)
        self.assertTrue(all(a["board"] == BOARD for a in arts.values()))

    def test_flash_table_is_the_uart_uniflash_j6_flow(self):
        fl = manifest()["flash"]
        self.assertEqual((fl["method"], fl["gate"]), ("uart_uniflash", "j6"))
        self.assertEqual(fl["success_marker"], "All commands from config file are executed")
        self.assertIn("J6", fl["mode_steps"][0])
        self.assertIn("J6", fl["after_steps"][0])
        # flash.sh must test for the same marker the manifest names
        self.assertIn(fl["success_marker"], (HERE / "flash.sh").read_text())

    def test_prebuilt_alternative_image_is_kept(self):
        sh = (HERE / "flash.sh").read_text()
        self.assertIn('"prebuilt"', sh)
        self.assertIn("am273x_mmw_cascade_demo_DDM.appimage", sh)
        for f in ("sbl_uart_uniflash.release.tiimage", "sbl_qspi.release.tiimage"):
            self.assertTrue((HERE / "prebuilt_binaries" / f).is_file(), f)

    def test_flash_sh_has_dry_run_and_fw_env(self):
        sh = (HERE / "flash.sh").read_text()
        for needle in ("--dry-run", "FW_DRY_RUN", "FW_PORT", "FW_IMAGE"):
            self.assertIn(needle, sh)

    def test_bring_up_tool_moved_here(self):
        self.assertTrue((HERE / "tools" / "cascade_serial_check.py").is_file())
        self.assertFalse((FW_ROOT / "tools" / "cascade_serial_check.py").exists())


@unittest.skipUnless(DESCRIPTOR.is_file(), "parent descriptor config/firmware/cascade_ddm.json not present")
class DescriptorTest(unittest.TestCase):
    def test_identify_entry_points_back_at_this_project(self):
        ent = descriptor()["identify"][BOARD]
        self.assertEqual(ent["source"], {"fw_project": HERE.name, "artifact": "am273x_cascade.appimage"})
        self.assertIs(ent["once_safe"], False, "cascade takes one cfg per power-up: verify must stay listen-only")
        v = manifest()["verify"]
        self.assertEqual(v["descriptor"], "cascade_ddm")

    def test_flash_hint_names_this_project(self):
        self.assertIn(HERE.name, descriptor()["identify"][BOARD]["flash_hint"])


@unittest.skipUnless(DESCRIPTOR.is_file(), "parent descriptor config/firmware/cascade_ddm.json not present")
class CfgLimitsTest(unittest.TestCase):
    """configs/cascade_*.cfg parse and respect the error-level limits of the descriptor."""

    def setUp(self):
        self.lim = {k: v["value"] for k, v in descriptor()["limits"][BOARD].items() if v["level"] == "error"}
        self.files = cfg_files()

    def test_cfgs_found(self):
        self.assertGreaterEqual(len(self.files), 4)

    def test_every_cfg(self):
        for f in self.files:
            with self.subTest(cfg=f.name):
                cmds = {}
                for name, args in cfg_commands(f):
                    cmds.setdefault(name, []).append(args)
                for needed in ("sensorStop", "channelCfg", "profileCfg", "chirpCfg", "frameCfg", "sensorStart"):
                    self.assertIn(needed, cmds)
                self.assertEqual(list(cfg_commands(f))[-1][0], "sensorStart", "sensorStart must be last")
                # channelCfg rx tx cascading rx2 tx2
                ch = cmds["channelCfg"][0]
                self.assertEqual(len(ch), self.lim["channel_cfg_args"])
                rx = bin(int(ch[0])).count("1") + bin(int(ch[3])).count("1")
                tx = bin(int(ch[1])).count("1") + bin(int(ch[4])).count("1")
                self.assertEqual(rx, self.lim["n_rx"])
                self.assertIn(tx, self.lim["valid_tx_counts"])
                self.assertLessEqual(tx, self.lim["n_tx"])
                # profileCfg id start idle adcStart rampEnd pwr phase slope txStart numAdc rate hpf1 hpf2 gain
                pr = [float(x) for x in cmds["profileCfg"][0]]
                start, slope, ramp_end, n_adc, rate = pr[1], pr[7], pr[4], pr[9], pr[10]
                lo, hi = self.lim["band_ghz"]
                self.assertGreaterEqual(start, lo)
                self.assertLessEqual(start + slope * ramp_end / 1000.0, hi)
                self.assertLessEqual(slope, self.lim["max_slope_mhz_us"])
                self.assertGreaterEqual(rate, self.lim["min_sample_rate_ksps"])
                self.assertLessEqual(rate, self.lim["max_sample_rate_ksps"])
                self.assertLessEqual(n_adc, self.lim["max_samples_silicon"])
                # frameCfg chirpStart chirpEnd loops frames samples period trigger delay ?
                fr = cmds["frameCfg"][0]
                self.assertEqual(len(fr), self.lim["frame_cfg_args"])
                n_chirps_loop = int(fr[1]) - int(fr[0]) + 1
                loops = int(fr[2])
                self.assertLessEqual(loops, self.lim["max_loops"])
                self.assertEqual(n_chirps_loop % 8, 0, "DDMA: chirps per loop must be a multiple of 8")
                self.assertLessEqual(n_chirps_loop * loops, self.lim["max_chirps"])
                self.assertEqual(int(fr[4]), int(n_adc), "frameCfg samples must equal profileCfg numAdcSamples")


class FlushPatchAnchorTest(unittest.TestCase):
    """flash.sh patches the SDK's uart_uniflash.py after one anchor line; check the anchor still exists once."""

    def test_anchor_matches_in_image_script(self):
        if os.environ.get("FW_SKIP_DOCKER_TESTS") == "1":
            self.skipTest("FW_SKIP_DOCKER_TESTS=1")
        docker = shutil.which("docker")
        if not docker:
            self.skipTest("docker not installed: in-image uart_uniflash.py anchor not checked")
        try:
            have = subprocess.run([docker, "image", "inspect", IMAGE], capture_output=True, timeout=30).returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            have = False
        if not have:
            self.skipTest(f"image {IMAGE} not present: in-image uart_uniflash.py anchor not checked")
        r = subprocess.run([docker, "run", "--rm", "--network", "none", "--entrypoint", "cat", IMAGE,
                            UNIFLASH_IN_IMAGE], capture_output=True, text=True, timeout=120)
        if r.returncode != 0:
            self.skipTest(f"could not read {UNIFLASH_IN_IMAGE} from the image: {r.stderr.strip()[:120]}")
        sh = (HERE / "flash.sh").read_text()
        anchor = re.search(r'anchor = "(.*)"\n', sh).group(1).encode().decode("unicode_escape")
        self.assertEqual(r.stdout.count(anchor), 1, "uart_uniflash.py changed; update the flush patch in flash.sh")


if __name__ == "__main__":
    unittest.main()
