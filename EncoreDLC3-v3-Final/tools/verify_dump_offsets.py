#!/usr/bin/env python3
"""Sanity-check dump-derived PlayerManager method offsets against jni/dump.cs."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
text = (root / "jni" / "dump.cs").read_text(errors="replace")
src = (root / "jni" / "src" / "Esp.cpp").read_text(errors="replace")
needle = "public class PlayerManager : NetworkBehaviour // TypeDefIndex: 9722"
start = text.find(needle)
if start < 0:
    sys.exit("FAIL: PlayerManager TypeDefIndex 9722 not found in dump.cs")
end = text.find("\npublic ", start + len(needle))
block = text[start:end if end >= 0 else None].splitlines()
expected = {
    ".cctor": 0x69A2114,
    "Update": 0x698EB00,
    "OnStopClient": 0x69AC9B0,
    "DeserializeSyncVars": 0x6985C10,
    "OnStartClient": 0x6998994,
    "OnStartLocalPlayer": 0x698667C,
}
for name, offset in expected.items():
    found = []
    for i, line in enumerate(block):
        if name == ".cctor":
            match = re.search(r"\bvoid \.cctor\s*\(", line)
        else:
            match = re.search(r"\b" + re.escape(name) + r"\s*\(", line)
        if not match:
            continue
        for j in range(i - 1, max(-1, i - 8), -1):
            m = re.search(r"Offset: 0x([0-9A-Fa-f]+)", block[j])
            if m:
                found.append(int(m.group(1), 16))
                break
    if not found:
        sys.exit(f"FAIL: couldn't locate method metadata for {name}")
    got = found[-1]
    if got != offset:
        sys.exit(f"FAIL: {name}: source table=0x{offset:X}, dump=0x{got:X}")
    if f"0x{offset:X}" not in src:
        sys.exit(f"FAIL: {name}: offset missing from Esp.cpp")
print("PASS: PlayerManager exact-resolver offsets match dump 1.14.11949")
