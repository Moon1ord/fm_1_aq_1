#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Make AQ-1's site: fm1-base's tools/make_site.py, then the manual and AQ-1's own colours.

  index.html, emu/, install/, words/, firmware/              make_site.py's, from app/ and a release package
  manual/index.html, img/                                    docs/MANUAL.md

  tools/make_pages.py app/build/aq1-X.Y.fwsc X.Y OUT_DIR

Before it: ./build.sh --release X.Y and ./build.sh --web --release X.Y."""
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "base" / "tools"))
import make_site  # noqa: E402

COLOURS = ("--paper: #0d1720; --sheet: #17242f; --rule: #2d3f4e; --ink: #dbe9f3; --muted: #93a9b9;\n"    # the screen's ice blue
           "  --hot: #7cc4e8; --hot-t: #1d3442; --cool: #7cc4e8; --cool-t: #142b38;\n"
           "  --hot-ink: #93d2f0;\n"
           "  color-scheme: dark;")
# the same ice blue for fm1-base's own two pages, over both of their themes
OTHERS = {"install": "--bg: #0d1720; --fg: #dbe9f3; --dim: #93a9b9; --accent: #7cc4e8; --on-accent: #0d1720; --bad: #ff8a73; --focus: #dbe9f3;",
          "emu": "--ground: #0d1720; --ink: #dbe9f3; --muted: #93a9b9; --rule: #2d3f4e; --accent: #7cc4e8; --teal: #7cc4e8; --card: #17242f;"
                 " --bad: #ff8a73; --focus: #dbe9f3;"}


def restyle(path):
    """AQ-1's colours over the manual tool's (a firmware may restyle its site), which every page carries whole"""
    text = path.read_text(encoding="utf-8")
    text, n = re.subn(r"(:root \{\s*)--paper:.*?color-scheme: light;", lambda m: m.group(1) + COLOURS, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{path}: the manual tool's colours have moved")
    path.write_text(text, encoding="utf-8")


def main(pkg, version, out):
    out = Path(out)
    app = SRC / "app"
    read_conf = make_site.read_conf
    make_site.read_conf = lambda p: dict(read_conf(p), name="AQ-1")   # "AQ1" names the build's files; the site's name has its hyphen
    make_site.main(app, pkg, version, out)
    conf = make_site.read_conf(app / "fm1.toml")
    (out / "manual").mkdir()                      # the manual is at the repository's root, not in app/docs, where make_site looks
    make_site.page(SRC / "docs" / "MANUAL.md", out / "manual" / "index.html", conf, False)
    shutil.copytree(SRC / "docs" / "img", out / "manual" / "img")
    restyle(out / "index.html")
    restyle(out / "manual" / "index.html")
    emu = out / "emu" / "index.html"              # the emulator's page takes the build's name, and has no way home
    emu.write_text(re.sub(r"\bAQ1\b", "AQ-1", emu.read_text(encoding="utf-8"))
                   .replace("<h1>AQ-1</h1>", '<h1><a href="../" style="color:inherit;text-decoration:none">AQ-1</a></h1>'), encoding="utf-8")
    for name, colours in OTHERS.items():
        page = out / name / "index.html"
        text = page.read_text(encoding="utf-8")
        if text.count("</head>") != 1:
            raise SystemExit(f"{page}: no one </head>")
        page.write_text(text.replace("</head>", f"<style>html:root:not(#x) {{ {colours} color-scheme: dark; }}</style>\n</head>"), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
