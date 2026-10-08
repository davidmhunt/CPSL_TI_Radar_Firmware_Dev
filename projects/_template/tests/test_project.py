"""Hardware-free host tests for this project. `./fw test PROJECT_NAME` runs them (stdlib unittest)."""
import tomllib
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


class ManifestTest(unittest.TestCase):
    def test_manifest_parses_and_names_the_folder(self):
        data = tomllib.loads((HERE / "project.toml").read_text())
        self.assertEqual(data["project"]["name"], HERE.name)

    def test_scripts_exist(self):
        for s in ("build.sh", "flash.sh"):
            self.assertTrue((HERE / s).is_file(), s)


if __name__ == "__main__":
    unittest.main()
