"""Guard: tracked non-Markdown files must not cite project-tracking task IDs (a lab-area name, a hyphen, two digits)."""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATTERN = re.compile(r"\b(radar|core|gui|sensors|release|firmware|docs|rover|nav|create3|ugv)-[0-9]{2}\b")
# path -> reason the ID is kept (none today)
ALLOWLIST: dict[str, str] = {}


def test_no_task_ids_in_tracked_non_markdown_files():
    files = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout.split(b"\0")
    hits = []
    for rel in filter(None, (f.decode() for f in files)):
        if rel.endswith(".md") or rel.startswith(".friday/") or rel in ALLOWLIST:
            continue
        p = ROOT / rel
        if not p.is_file():
            continue
        data = p.read_bytes()
        if b"\0" in data:
            continue
        for n, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            if PATTERN.search(line):
                hits.append(f"{rel}:{n}: {line.strip()[:100]}")
    assert not hits, "task IDs found:\n" + "\n".join(hits)
