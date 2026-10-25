#!/usr/bin/env python3
"""Convert SingleStepTests 68000 JSON (gz) to a compact binary for sst_run.

Per test: name(64 bytes), initial state, final state.
State: 19 u32 (d0-d7, a0-a6, usp, ssp, sr, pc), 2 u16 prefetch, u32 nram, nram * (u32 addr, u32 byte).
Tests whose bus transactions include an odd-address word/long access are
skipped: they raise address errors, which the Mega Drive game never relies on.
"""
import gzip, json, struct, sys

KEYS = ["d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "usp", "ssp", "sr", "pc"]


def state(s):
    b = struct.pack("<19I", *[s[k] & 0xFFFFFFFF for k in KEYS])
    b += struct.pack("<2H", *s["prefetch"])
    b += struct.pack("<I", len(s["ram"]))
    for a, v in s["ram"]:
        b += struct.pack("<II", a, v)
    return b


def addr_error(t):
    # address-error processing fetches vector 3 ($00000C)
    for tr in t.get("transactions", []):
        if tr[0] == "r" and len(tr) >= 4 and tr[3] in (12, 14):
            return True
    return False


src, dst = sys.argv[1], sys.argv[2]
tests = json.load(gzip.open(src))
n = skipped = 0
with open(dst, "wb") as f:
    for t in tests:
        if addr_error(t):
            skipped += 1
            continue
        f.write(t["name"].encode()[:63].ljust(64, b"\0"))
        f.write(state(t["initial"]))
        f.write(state(t["final"]))
        n += 1
print(f"{src}: {n} tests, {skipped} skipped (address errors)")
