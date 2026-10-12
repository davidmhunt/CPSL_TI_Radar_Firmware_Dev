"""Docs hygiene (hardware-free): links and anchors resolve, `./fw <verb>` is a real verb, no harness IDs."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from fwcli.cli import build_parsers  # noqa: E402

VERBS = set(build_parsers())
LINK = re.compile(r"(?<!\!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FW_CALL = re.compile(r"(?<![\w/.-])\./fw\s+([A-Za-z][\w-]*)")
DIRECTIVE_ID = re.compile(r"\b(firmware|fwstd|fwdocs|gui|core|rel)-[0-9]{2}\b")
SKIP_PARTS = {".venv", "downloads"}

# Findings in other projects' docs, owned by fwdocs-02 / fwdocs-03: xfail until those land.
# Remove an entry when its directive fixes the file (strict: an unexpected pass fails).
KNOWN = {
    ("test_no_directive_ids", "projects/ti_stock_demos/docs/bench_check.md"): "fwdocs-02",
    ("test_no_directive_ids", "projects/iwr1843_sar_lvds/README.md"): "fwdocs-03",
}


def tracked_md() -> list[str]:
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    files = []
    for rel in out.split():
        parts = Path(rel).parts
        if SKIP_PARTS & set(parts) or "src" in parts[:-1] or not (ROOT / rel).is_file():
            continue
        files.append(rel)
    return sorted(files)


def is_archive(rel: str) -> bool:
    return "archive" in Path(rel).parts[:-1]


def params(test):
    return list(_params(test))


def _params(test):
    for rel in tracked_md():
        marks = []
        if (test, rel) in KNOWN:
            marks.append(pytest.mark.xfail(strict=True, reason=f"{KNOWN[(test, rel)]} owns this file"))
        yield pytest.param(rel, marks=marks, id=rel)


def anchors(path: Path) -> set[str]:
    """GitHub-style heading slugs of a Markdown file."""
    found, fenced = set(), False
    for line in path.read_text().splitlines():
        if line.lstrip().startswith("```"):
            fenced = not fenced
        m = None if fenced else re.match(r"#{1,6}\s+(.*?)\s*#*$", line)
        if m:
            text = re.sub(r"[`*]", "", m.group(1)).lower()
            found.add(re.sub(r" ", "-", re.sub(r"[^\w\s-]", "", text)))
    return found


def prose_lines(text: str):
    """Yield (lineno, line, in_fence)."""
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        yield n, line, fenced


@pytest.mark.parametrize("rel", params("test_relative_links_resolve"))
def test_relative_links_resolve(rel):
    bad = []
    for n, line, fenced in prose_lines((ROOT / rel).read_text()):
        if fenced:
            continue
        for target in LINK.findall(line):
            if re.match(r"^[a-z][a-z0-9+.-]*:", target):
                continue
            path, _, frag = target.partition("#")
            dest = (ROOT / rel).parent / path if path else ROOT / rel
            if not dest.exists():
                bad.append(f"{rel}:{n}: {target}")
            elif frag and dest.suffix == ".md" and frag not in anchors(dest):
                bad.append(f"{rel}:{n}: {target} (no such heading)")
    assert not bad, "broken links:\n" + "\n".join(bad)


@pytest.mark.parametrize("rel", params("test_fw_verbs_are_real"))
def test_fw_verbs_are_real(rel):
    bad = []
    for n, line, _ in prose_lines((ROOT / rel).read_text()):
        for verb in FW_CALL.findall(line):
            if verb not in VERBS:
                bad.append(f"{rel}:{n}: ./fw {verb}")
    assert not bad, f"unknown verbs (real: {sorted(VERBS)}):\n" + "\n".join(bad)


@pytest.mark.parametrize("rel", [p for p in params("test_no_directive_ids") if not is_archive(p.values[0])])
def test_no_directive_ids(rel):
    bad = [f"{rel}:{n}: {m.group(0)}" for n, line, _ in prose_lines((ROOT / rel).read_text())
           for m in DIRECTIVE_ID.finditer(line)]
    assert not bad, "harness directive IDs in non-archive docs:\n" + "\n".join(bad)
