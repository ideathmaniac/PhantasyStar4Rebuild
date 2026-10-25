#!/usr/bin/env python3
"""Convert SingleStepTests Z80 JSON files into one compact binary for z80_run.
Usage: z80_convert.py out.bin file.json...
Record: name[32], state initial, state final, u8 nports, nports*(u16 port, u8 val, u8 dir)
State: 26 u16 (pc sp a b c d e f h l i r ei wz ix iy af_ bc_ de_ hl_ im p q iff1 iff2 pad), u16 nram, nram*(u16 addr,u8 val,u8 pad)
"""
import json, struct, sys

K = ["pc", "sp", "a", "b", "c", "d", "e", "f", "h", "l", "i", "r", "ei", "wz", "ix", "iy",
     "af_", "bc_", "de_", "hl_", "im", "p", "q", "iff1", "iff2"]


def st(s):
    b = struct.pack("<26H", *([s.get(k, 0) & 0xFFFF for k in K] + [0]))
    b += struct.pack("<H", len(s["ram"]))
    for a, v in s["ram"]:
        b += struct.pack("<HBB", a, v, 0)
    return b


out = open(sys.argv[1], "wb")
n = 0
for fn in sys.argv[2:]:
    for t in json.load(open(fn)):
        out.write(t["name"].encode()[:31].ljust(32, b"\0"))
        out.write(st(t["initial"])); out.write(st(t["final"]))
        ports = t.get("ports", [])
        out.write(struct.pack("<B", len(ports)))
        for p, v, d in ports:
            out.write(struct.pack("<HBB", p, v, 1 if d == "r" else 0))
        n += 1
print(n, "tests")
