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

# Positive invariants for PORT-only fixes where the original N64 form remains
# in a #else branch and therefore cannot be rejected by a plain-text regex.
REQUIRED = {
    "src/game/bondview2.c": [
        ("f32 pointbuf[12];", "host intro swirl spline buffer must hold four coord3d points"),
    ],
    "src/game/bg.h": [
        ("(u8 *)(base) + (u32)((u32)(off) + 0xF1000000u)", "BG segment rebasing must retain the full host base pointer"),
    ],
    "src/game/bg.c": [
        ("obLoadBGFileBytesAtOffset(levelinfotable[levelentry_index].bg_seg_filename, (u8 *)header, 0, 0x40);", "BG header probe must use the native stack pointer directly"),
        ("bg_room_data *probe_rooms =", "BG header probe must remain local/full-width on PORT"),
    ],
    "src/game/model.c": [
        ("MODEL_U32_PTR(ModelAnimBitField, anim->bitDescriptors)", "animation bit-descriptor token must zero-extend at the pointer boundary"),
        ("MODEL_U32_PTR(u8, model->unk34)", "cached animation frame addresses must zero-extend at the pointer boundary"),
    ],
    "src/game/language.c": [
        ("void *g_LangBanks[45];", "PORT language banks must retain native runtime pointers"),
        ("((u8 *)textbank_ptr + textslot_offset)", "language text offsets must rebase onto a native pointer"),
    ],
    "src/game/title.c": [
        ("u8 *barrelDisplayListPtr;", "gunbarrel vertex buffer must retain its native host pointer"),
        ("u8 *dword_CODE_bss_80069588;", "title RLE source buffer must retain its native host pointer"),
        ("GE_ANIMDATA_PTR(bond_eye_walk)", "gunbarrel walk animation must use the host-width animation boundary"),
    ],
    "src/game/model.c": [
        ("subdraw(arg0, arg1);", "PORT render-data pointer must not truncate through s32 in sub_GAME_7F074790"),
        ("void sub_GAME_7F074514(ModelRenderData *param_1", "PORT model node stubs must accept native render-data pointers"),
    ],
    "src/game/bg.h": [
        ("Visibility traversal state, not a pointer.", "US visibility queue next field must remain an integer token on PORT"),
        ("u32 next;", "US s_bound_info must preserve its 32-bit next field on PORT"),
    ],
    "src/game/title.c": [
        ("(void *)(uintptr_t)(u32)virtualaddress", "stored title address token must zero-extend at the host pointer boundary"),
        ("(Vtx *)(uintptr_t)(OS_K0_TO_PHYSICAL(barrelDisplayListPtr) | 0x80000000u)", "gunbarrel KSEG0 reconstruction must be an explicit low-address pointer conversion"),
    ],
    "src/game/chraction.c": [
        ("GE_ANIMTABLE_ENTRY_PTR(animation_table_ptrs1, animID)", "dense animation table entries must zero-extend at the host pointer boundary"),
        ("GE_ANIMDATA_MATCH(objecthandlerGetModelAnim(self->model), fire_kneel_forward_one_handed_weapon_slow)", "animation comparisons must retain host pointer width"),
    ],
}

for root in SCAN_ROOTS:
    if not root.exists():
        continue
    for path in root.rglob("*"):
        if path.suffix not in EXTS or not path.is_file():
            continue
        files += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT)

        relstr = rel.as_posix()
        for required, why in REQUIRED.get(relstr, []):
            if required not in text:
                failures.append(f"{rel}: required invariant missing: {why}")

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
