#!/usr/bin/env python3
"""From interpreter-executed ROM addresses (headless -p), find code entry
points not yet recompiled and merge them into hints.json extra_entries.
Usage: new_entries.py ROM interp_pcs.txt hints.json"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kdec import decode
rom = open(sys.argv[1], "rb").read()
seen = {int(x, 16) for x in open(sys.argv[2]).read().split()}
seen = {a for a in seen if a < len(rom)}
covered = set()
for a in seen:
    i = decode(rom, a)
    if i and i.kind not in ("branch", "jump_ind", "ret", "stop"):
        covered.add(a + i.size)
entries = sorted(seen - covered)
hp = sys.argv[3]
hints = json.load(open(hp)) if os.path.exists(hp) else {"jump_tables": {}, "extra_entries": [], "patches": {}}
old = set(hints.get("extra_entries", []))
new = [f"{a:06X}" for a in entries if f"{a:06X}" not in old]
hints["extra_entries"] = sorted(old | set(new))
json.dump(hints, open(hp, "w"), indent=1)
print(f"interpreted ROM addresses {len(seen)}, entry points {len(entries)}, new {len(new)}")
