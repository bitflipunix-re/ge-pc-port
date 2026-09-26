#!/usr/bin/env python3
"""ARM64/LP64 semantic regression gate.

This is intentionally conservative: it fails only on port patterns that have
already caused real bugs in this codebase, while reporting broader suspicious
32-bit address carriers for follow-up review.
"""
from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCAN_ROOTS = [ROOT / "src", ROOT / "port", ROOT / "include"]
EXTS = {".c", ".h", ".cpp", ".hpp"}

FORBIDDEN = [
    (re.compile(r"\(\s*u32\s*\)\s*local_stage"), "stage base truncated to u32"),
    (re.compile(r"\(\s*s32\s*\)\s*local_stage"), "stage base truncated to s32"),
    (re.compile(r"\(u8\s*\*\)\s*g_CurrentPlayer\s*\+\s*handoffset\s*\+\s*0x(?:AD8|AD0|B08|B00|B48|B40)", re.I),
     "raw N64 struct-player/hand offset used on host"),
    (re.compile(r"&\s*g_sndPlayerPtr\s*->\s*evtq"), "audio member address formed through player pointer"),
]

# The snd rule is permitted only after an explicit NULL guard in the same
# function. Handle it specially rather than blanket-failing all occurrences.
def snd_member_is_guarded(text: str, pos: int) -> bool:
    window = text[max(0, pos - 900):pos]
    return bool(re.search(r"if\s*\(\s*g_sndPlayerPtr\s*==\s*NULL\s*\)\s*(?:\{|)\s*return", window))

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
print(f"ARM64 semantic audit: {len(suspects)} suspicious 32-bit carrier candidates (review list)")
for item in suspects[:80]:
    print("  REVIEW", item)
if len(suspects) > 80:
    print(f"  ... {len(suspects)-80} more candidates omitted")

if failures:
    print(f"ARM64 semantic audit: FAIL ({len(failures)} known-danger patterns)", file=sys.stderr)
    for item in failures:
        print("  ERROR", item, file=sys.stderr)
    sys.exit(1)

print("ARM64 semantic audit: PASS (no known-danger patterns)")
