"""Convert a Markdown file to a PDF of the same name in the same directory.

Usage:
    poetry run python scripts/md_to_pdf.py <path/to/file.md>
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

import markdown

CSS = """
    body {
        font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif;
        font-size: 11pt;
        line-height: 1.5;
        color: #1a1a1a;
        max-width: 900px;
        margin: 0 auto;
        padding: 20px 40px;
    }
    h1 { font-size: 20pt; border-bottom: 2px solid #333; padding-bottom: 6px; }
    h2 { font-size: 15pt; border-bottom: 1px solid #ccc; padding-bottom: 4px; margin-top: 28px; }
    h3 { font-size: 12pt; margin-top: 20px; }
    table {
        border-collapse: collapse;
        width: 100%;
        margin: 12px 0;
        font-size: 10pt;
    }
    th, td {
        border: 1px solid #aaa;
        padding: 6px 10px;
        text-align: left;
    }
    th { background-color: #f0f0f0; font-weight: 600; }
    tr:nth-child(even) { background-color: #f9f9f9; }
    code {
        background-color: #f4f4f4;
        padding: 1px 4px;
        border-radius: 3px;
        font-size: 9.5pt;
        font-family: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
    }
    pre {
        background-color: #f4f4f4;
        padding: 12px;
        border-radius: 4px;
        overflow-x: auto;
    }
    pre code { background: none; padding: 0; }
    blockquote {
        border-left: 4px solid #ccc;
        margin-left: 0;
        padding-left: 16px;
        color: #555;
    }
    a { color: #0366d6; }
    .mermaid {
        margin: 20px 0;
        display: flex;
        justify-content: center;
    }
    @media print {
        body { padding: 0; }
    }
"""


def convert(md_path: Path) -> Path:
    md_text = md_path.read_text(encoding="utf-8")
    body_html = markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "pymdownx.arithmatex"],
        extension_configs={
            "pymdownx.arithmatex": {
                "generic": True,
            }
        }
    )

    # Convert fenced code blocks for mermaid into div elements class="mermaid" and unescape HTML entities
    def unescape_mermaid(match):
        import html
        return f'<div class="mermaid">{html.unescape(match.group(1))}</div>'

    body_html = re.sub(
        r'<pre><code class="language-mermaid">(.*?)</code></pre>',
        unescape_mermaid,
        body_html,
        flags=re.DOTALL
    )

    template = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>__TITLE__</title>
<style>__CSS__</style>
<script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>
<script>
    document.addEventListener("DOMContentLoaded", function() {
        mermaid.initialize({ startOnLoad: true, theme: 'default' });
    });
</script>
<script>
window.MathJax = {
  tex: {
    inlineMath: [['\\\\(', '\\\\)']],
    displayMath: [['\\\\[', '\\\\]']],
    processEscapes: true
  },
  options: {
    ignoreHtmlClass: 'tex2jax_ignore',
    processHtmlClass: 'arithmatex'
  }
};
</script>
<script src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js" id="MathJax-script" async></script>
</head>
<body>
__BODY__
</body>
</html>"""

    html = template.replace("__TITLE__", md_path.stem).replace("__CSS__", CSS).replace("__BODY__", body_html)

    pdf_path = md_path.with_suffix(".pdf")

    with tempfile.NamedTemporaryFile(suffix=".html", delete=False, mode="w", encoding="utf-8") as tmp:
        tmp.write(html)
        tmp_path = Path(tmp.name)

    try:
        subprocess.run(
            [
                "google-chrome",
                "--headless",
                "--disable-gpu",
                "--no-sandbox",
                "--virtual-time-budget=10000",
                "--run-all-compositor-stages-before-draw",
                f"--print-to-pdf={pdf_path.resolve()}",
                "--print-to-pdf-no-header",
                tmp_path.resolve().as_uri(),
            ],
            check=True,
            capture_output=True,
        )
    finally:
        tmp_path.unlink(missing_ok=True)

    return pdf_path


def main():
    if len(sys.argv) != 2:
        print("Usage: poetry run python scripts/md_to_pdf.py <file.md>")
        sys.exit(1)

    md_path = Path(sys.argv[1])
    if not md_path.exists():
        print(f"Error: file not found: {md_path}")
        sys.exit(1)
    if md_path.suffix.lower() != ".md":
        print(f"Error: expected a .md file, got: {md_path}")
        sys.exit(1)

    pdf_path = convert(md_path)
    print(f"PDF written to {pdf_path}")


if __name__ == "__main__":
    main()
