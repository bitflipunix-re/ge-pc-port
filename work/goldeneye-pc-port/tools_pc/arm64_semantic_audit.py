#!/usr/bin/env python3
"""ARM64/LP64 semantic regression gate.

Fail only on port patterns that have already caused real bugs in this codebase.
Broader 32-bit carrier patterns are reported for review but do not fail CI.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_ROOTS = [ROOT / "src", ROOT / "port", ROOT / "include"]
EXTS = {".c", ".h", ".cpp", ".hpp"}

FORBIDDEN = [
    (
        re.compile(
            r"\(u8\s*\*\)\s*g_CurrentPlayer\s*\+\s*handoffset\s*\+\s*"
            r"0x(?:AD8|AD0|B08|B00|B48|B40)",
            re.I,
        ),
        "raw N64 struct-player/hand offset used on host",
    ),
    (
        re.compile(
            r"ALEventQueue\s*\*\s*evtq\s*=\s*&\s*g_sndPlayerPtr\s*->\s*evtq"
        ),
        "audio queue pointer formed before NULL guard",
    ),
]

SUSPICIOUS = [
    re.compile(r"\(\s*u32\s*\)\s*[A-Za-z_][A-Za-z0-9_]*(?:->|\[|\.)"),
    re.compile(r"\(\s*s32\s*\)\s*[A-Za-z_][A-Za-z0-9_]*(?:->|\[|\.)"),
    re.compile(r"\(\s*u32\s*\)\s*\([^\n;]*\*[^\n;]*\)"),
]

failures: list[str] = []
suspects: list[str] = []
files = 0

for root in SCAN_ROOTS:
    if not root.exists():
        continue
    for path in root.rglob("*"):
        if path.suffix not in EXTS or not path.is_file():
            continue
        files += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT)

        for rx, why in FORBIDDEN:
            for m in rx.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                failures.append(f"{rel}:{line}: {why}: {m.group(0)!r}")

        for rx in SUSPICIOUS:
            for m in rx.finditer(text):
                line = text.count("\n", 0, m.start()) + 1
                suspects.append(f"{rel}:{line}: {m.group(0)[:120]}")

print(f"ARM64 semantic audit: scanned {files} source files")
print(
    f"ARM64 semantic audit: {len(suspects)} suspicious 32-bit carrier candidates "
    "(review list)"
)
for item in suspects[:80]:
    print("  REVIEW", item)
if len(suspects) > 80:
    print(f"  ... {len(suspects) - 80} more candidates omitted")

if failures:
    print(
        f"ARM64 semantic audit: FAIL ({len(failures)} known-danger patterns)",
        file=sys.stderr,
    )
    for item in failures:
        print("  ERROR", item, file=sys.stderr)
    sys.exit(1)

print("ARM64 semantic audit: PASS (no known-danger patterns)")
