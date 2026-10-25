#!/usr/bin/env python3
"""Recursive-descent tracer for Phantasy Star IV.

Usage: trace.py ROM [hints.json] [outdir]
Writes codemap.json (functions, labels, code ranges, jump tables, unresolved
indirect jumps) and listing.txt.
"""
import json, os, sys
from collections import deque, Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kdec import decode, w, sw, l

rom = open(sys.argv[1], "rb").read()
HINTS = json.load(open(sys.argv[2])) if len(sys.argv) > 2 and os.path.exists(sys.argv[2]) else {}
OUT = sys.argv[3] if len(sys.argv) > 3 else "out"
END = len(rom)

insns, owner = {}, {}
functions, labels = set(), set()
unresolved, tables, conflicts, invalid = {}, {}, [], {}
work = deque()


def add(addr, func=False, src=None):
    if not (0x200 <= addr < END) or addr & 1:
        if src is not None and addr >= END:
            unresolved.setdefault(src, f"target {addr:06X} outside ROM")
        return
    if func:
        functions.add(addr)
    labels.add(addr)
    work.append((addr, src))


def prev_insns(addr, n=4):
    out, a = [], addr
    for _ in range(n):
        f = None
        for s in (2, 4, 6, 8, 10):
            p = insns.get(a - s)
            if p and p.addr + p.size == a:
                f = p; break
        if not f: break
        out.append(f); a = f.addr
    return out


def d8_base(a):  # base of a d8(pc,Xn) extension word at a
    return (a + ((w(rom, a) & 0xFF) ^ 0x80) - 0x80) & 0xFFFFFF


def valid_target(t):
    return not (t & 1) and 0x200 <= t < END and decode(rom, t) is not None


def offset_table(tbl, rel, kind, site, limit=256):
    targets, end, a = [], None, tbl
    while len(targets) < limit and (end is None or a < end):
        t = (rel + sw(rom, a)) & 0xFFFFFF
        if not valid_target(t): break
        targets.append(t)
        if t > tbl: end = t if end is None else min(end, t)
        a += 2
    if targets:
        tables[tbl] = {"kind": kind, "site": site, "rel": rel, "targets": targets}
        for t in targets: add(t, src=site)
    return bool(targets)


def bra_table(tbl, site, limit=256):
    targets, end, a = [], None, tbl
    while len(targets) < limit and (end is None or a < end):
        i = decode(rom, a)
        if not i or i.kind != "branch": break
        targets.append(i.target)
        insns[a] = i
        for b in range(a, a + i.size): owner[b] = a
        if i.target > tbl: end = i.target if end is None else min(end, i.target)
        a += i.size
    if targets:
        tables[tbl] = {"kind": "bra_table", "site": site, "targets": targets}
        for t in targets: add(t, src=site)
    return bool(targets)


def ptr_table(tbl, site, limit=256):
    targets, a = [], tbl
    while len(targets) < limit and a + 4 <= END:
        t = l(rom, a)
        if not valid_target(t): break
        targets.append(t); a += 4
    if targets:
        tables[tbl] = {"kind": "ptr_table", "site": site, "targets": targets}
        for t in targets: add(t, func=True, src=site)
    return bool(targets)


def lea_target(q):
    op = w(rom, q.addr); mode, reg = (op >> 3) & 7, op & 7
    if mode == 7 and reg == 1: return l(rom, q.addr + 2) & 0xFFFFFF
    if mode == 7 and reg == 0: return sw(rom, q.addr + 2) & 0xFFFFFF
    if mode == 7 and reg == 2: return (q.addr + 2 + sw(rom, q.addr + 2)) & 0xFFFFFF
    return None


def resolve_pc_index(ins):
    base = d8_base(ins.addr + 2)
    for p in prev_insns(ins.addr, 3):
        op = w(rom, p.addr)
        # move.w T(pc,Xn),Dn  (src mode 7/3) feeding jmp T(pc,Dn)
        if p.name == "move" and (op >> 12) == 3 and (op & 0x3F) == 0x3B and d8_base(p.addr + 2) == base:
            return offset_table(base, base, "pc_offset_table", ins.addr)
    return bra_table(max(base, ins.addr + ins.size), ins.addr)


def resolve_areg(ins):
    reg = ins.ea[1]
    prev = prev_insns(ins.addr, 5)
    for idx, p in enumerate(prev):
        op = w(rom, p.addr)
        # movea.l <ea>,An  (0x2040 | An<<9)
        if (op & 0xF1C0) == (0x2040 | reg << 9):
            smode, sreg = (op >> 3) & 7, op & 7
            if smode == 7 and sreg == 3:
                return ptr_table(d8_base(p.addr + 2), ins.addr)
            if smode == 6:  # d8(Ab,Xn): look for lea T,Ab earlier
                for q in prev[idx + 1:]:
                    qo = w(rom, q.addr)
                    if q.name == "lea" and ((qo >> 9) & 7) == sreg:
                        t = lea_target(q)
                        if t is not None:
                            return ptr_table(t + ((w(rom, p.addr + 2) & 0xFF) ^ 0x80) - 0x80, ins.addr)
            return False
        # lea T,An directly before jmp (An)
        if p.name == "lea" and ((op >> 9) & 7) == reg:
            t = lea_target(p)
            if t is not None:
                add(t, func=ins.kind == "call_ind", src=ins.addr)
                tables[ins.addr] = {"kind": "lea_direct", "site": ins.addr, "targets": [t]}
                return True
            return False
        # anything else writing An stops the search
        if ((op >> 9) & 7) == reg and (op >> 12) in (2, 3) and ((op >> 6) & 7) == 1:
            return False
    return False


def resolve_hint(ins):
    h = HINTS.get("jump_tables", {}).get(f"{ins.addr:06X}")
    if not h: return False
    targets = [int(t, 16) for t in h.get("targets", [])]
    if "table" in h:
        tbl, n = int(h["table"], 16), h["count"]
        if h["kind"] == "ptr_table":
            targets += [l(rom, tbl + 4 * k) & 0xFFFFFF for k in range(n)]
        elif h["kind"] == "word_offset_table":
            rel = int(h.get("rel", h["table"]), 16)
            targets += [(rel + sw(rom, tbl + 2 * k)) & 0xFFFFFF for k in range(n)]
    tables[ins.addr] = {"kind": "hint", "site": ins.addr, "targets": targets}
    for t in targets: add(t, func=ins.kind == "call_ind", src=ins.addr)
    return True


def trace():
    while work:
        a, src = work.popleft()
        while True:
            if a in insns: break
            if a in owner:
                conflicts.append((a, src)); break
            ins = decode(rom, a)
            if ins is None:
                invalid[a] = src; break
            insns[a] = ins
            for b in range(a, a + ins.size): owner[b] = a
            k = ins.kind
            if k in ("branch", "cbranch", "call"):
                if ins.target >= 0xFF0000:
                    unresolved[a] = f"{ins.name} into RAM {ins.target:06X}"
                else:
                    add(ins.target, func=(k == "call"), src=a)
            elif k == "trap":
                v = l(rom, 0x80 + 4 * (ins.op & 0xF)) & 0xFFFFFF
                add(v, func=True, src=a)
            elif k in ("jump_ind", "call_ind"):
                mode, reg = ins.ea
                ok = resolve_hint(ins)
                if not ok and mode == 7 and reg == 3: ok = resolve_pc_index(ins)
                elif not ok and mode == 2: ok = resolve_areg(ins)
                if not ok:
                    unresolved[a] = f"{ins.name} mode{mode} reg{reg}"
            if k in ("branch", "jump_ind", "ret", "stop"): break
            a += ins.size


def main():
    vec = [l(rom, 4 * i) & 0xFFFFFF for i in range(64)]
    for i in range(1, 64):
        add(vec[i], func=True)
    for e in HINTS.get("extra_entries", []):
        add(int(e, 16), func=True)
    trace()
    ranges = []
    for a in sorted(insns):
        e = a + insns[a].size
        if ranges and ranges[-1][1] >= a: ranges[-1][1] = max(ranges[-1][1], e)
        else: ranges.append([a, e])
    code = sum(e - s for s, e in ranges)
    os.makedirs(OUT, exist_ok=True)
    json.dump({
        "vectors": vec,
        "functions": sorted(functions),
        "labels": sorted(labels),
        "code": ranges,
        "tables": {f"{k:06X}": v for k, v in sorted(tables.items())},
        "unresolved": {f"{k:06X}": v for k, v in sorted(unresolved.items())},
        "invalid": {f"{k:06X}": (f"{v:06X}" if v is not None else None) for k, v in sorted(invalid.items())},
        "conflicts": [[f"{a:06X}", f"{s:06X}" if s is not None else None] for a, s in sorted(set(conflicts))],
    }, open(os.path.join(OUT, "codemap.json"), "w"), indent=1)
    with open(os.path.join(OUT, "listing.txt"), "w") as f:
        for a in sorted(insns):
            if a in functions: f.write(f"\nsub_{a:06X}:\n")
            elif a in labels: f.write(f"loc_{a:06X}:\n")
            f.write(f"    {insns[a]}\n")
    kinds = Counter(i.name for i in insns.values())
    print(f"instructions {len(insns)}  functions {len(functions)}  code {code} bytes ({code*100/END:.1f}% of ROM)")
    print(f"tables {len(tables)}  unresolved {len(unresolved)}  invalid {len(invalid)}  conflicts {len(set(conflicts))}")
    print("unresolved kinds:", Counter(v.split(' mode')[0] if 'mode' in v else v.split(' ')[0] + ' ' + v.split(' ')[1] for v in unresolved.values()).most_common(8))
    print("traps used:", sorted({i.op & 0xF for i in insns.values() if i.kind == 'trap'}))


if __name__ == "__main__":
    main()
