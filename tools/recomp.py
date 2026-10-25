#!/usr/bin/env python3
"""68000 -> C static recompiler for Phantasy Star IV.

Usage: recomp.py ROM codemap.json outdir [extra_entries.txt]

Every traced function becomes `void ps4_f_XXXXXX(uint32_t entry)`; its body
is the code reachable from the entry without following calls, stopping at
other functions' entries. Each instruction is a `case` of the entry switch,
so execution can resume anywhere (after a frame yield, an interrupt, or a
return into the middle of a function).

Semantics mirror src/m68k.c exactly (same flag helpers from cpu.h, same
order of operand evaluation), and the per-instruction protocol is the one
in src/rt.h, so recompiled runs match interpreted runs bit for bit.
Rare instructions are delegated to the interpreter (cpu_interp_exec).
"""
import json, os, sys
from collections import deque
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kdec import decode, w, sw, l

rom = open(sys.argv[1], "rb").read()
cmap = json.load(open(sys.argv[2]))
OUT = sys.argv[3]
EXTRA = []
if len(sys.argv) > 4 and os.path.exists(sys.argv[4]):
    EXTRA = [int(x, 16) for x in open(sys.argv[4]).read().split() if x.strip()]

P = "ps4_f_"
HINTS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hints.json")
PATCHES = {int(k, 16): v for k, v in json.load(open(HINTS_PATH)).get("patches", {}).items()} if os.path.exists(HINTS_PATH) else {}
SZ = {0: 1, 1: 2, 2: 4}
MASK = {1: "0xFFu", 2: "0xFFFFu", 4: "0xFFFFFFFFu"}
BITS = {1: "8", 2: "16", 4: "32"}


def h(v):
    return f"0x{v & 0xFFFFFFFF:X}u"


class Unsupported(Exception):
    pass


class Cx:
    """Decoding cursor + temp allocator for one instruction."""

    def __init__(self, addr):
        self.addr = addr
        self.p = addr + 2
        self.n = 0
        self.lines = []

    def ext16(self):
        v = w(rom, self.p); self.p += 2; return v

    def ext32(self):
        v = l(rom, self.p); self.p += 4; return v

    def tmp(self):
        self.n += 1
        return f"t{self.n}"

    def emit(self, s):
        self.lines.append(s)


def sx8(v): return v - 256 if v & 0x80 else v
def sx16(v): return v - 65536 if v & 0x8000 else v


def index_expr(cx, base_expr):
    ext = cx.ext16()
    r = (ext >> 12) & 7
    xn = f"R.a[{r}]" if ext & 0x8000 else f"R.d[{r}]"
    if not (ext & 0x0800):
        xn = f"SX16({xn})"
    return f"({base_expr} + {h(sx8(ext & 0xFF))} + {xn})"


class Op:
    """An operand: prep() emits address computation (with side effects), then get()/put()."""

    def __init__(self, cx, mode, reg, size):
        self.cx, self.mode, self.reg, self.size = cx, mode, reg, size
        self.var = None
        self.imm = None

    def prep(self):
        cx, m, r, s = self.cx, self.mode, self.reg, self.size
        if m in (0, 1):
            return self
        if m == 7 and r == 4:
            self.imm = cx.ext32() if s == 4 else cx.ext16() if s == 2 else (cx.ext16() & 0xFF)
            return self
        t = cx.tmp()
        inc = 2 if (s == 1 and r == 7) else s
        if m == 2: cx.emit(f"uint32_t {t} = R.a[{r}];")
        elif m == 3: cx.emit(f"uint32_t {t} = R.a[{r}]; R.a[{r}] += {inc};")
        elif m == 4: cx.emit(f"R.a[{r}] -= {inc}; uint32_t {t} = R.a[{r}];")
        elif m == 5: cx.emit(f"uint32_t {t} = R.a[{r}] + {h(sx16(cx.ext16()))};")
        elif m == 6: cx.emit(f"uint32_t {t} = {index_expr(cx, f'R.a[{r}]')};")
        elif m == 7:
            if r == 0: cx.emit(f"uint32_t {t} = {h(sx16(cx.ext16()))};")
            elif r == 1: cx.emit(f"uint32_t {t} = {h(cx.ext32())};")
            elif r == 2:
                base = cx.p
                cx.emit(f"uint32_t {t} = {h(base + sx16(cx.ext16()))};")
            elif r == 3:
                base = cx.p
                cx.emit(f"uint32_t {t} = {index_expr(cx, h(base))};")
            else:
                raise Unsupported(f"ea 7/{r}")
        self.var = t
        return self

    def get(self):
        m, r, s = self.mode, self.reg, self.size
        if m == 0: return f"(R.d[{r}] & {MASK[s]})" if s != 4 else f"R.d[{r}]"
        if m == 1: return f"(R.a[{r}] & {MASK[s]})" if s != 4 else f"R.a[{r}]"
        if self.imm is not None: return h(self.imm)
        return f"R{BITS[s]}({self.var})"

    def put(self, val):
        m, r, s = self.mode, self.reg, self.size
        if m == 0:
            if s == 4: return f"R.d[{r}] = (uint32_t)({val});"
            return f"R.d[{r}] = (R.d[{r}] & ~{MASK[s]}) | (({val}) & {MASK[s]});"
        if m == 1: return f"R.a[{r}] = (uint32_t)({val});"
        if self.imm is not None: raise Unsupported("write to immediate")
        return f"W{BITS[s]}({self.var}, ({val}) & {MASK[s]});"

    def addr(self):
        """address operand for lea/pea/jmp/jsr (control modes, no side effects)"""
        self.prep()
        return self.var


def op_name(fn, s):
    return f"{fn}{BITS[s]}"


# ---------------------------------------------------------------------------
# Instruction generators. Each returns (lines, flow) where flow is
#   "next": falls through; the driver appends ACC
#   "done": generator already emitted ACC and control transfer
# ---------------------------------------------------------------------------
class Gen:
    def __init__(self, fn):
        self.fn = fn          # Function being generated (for branch targets)

    def jump(self, t):
        """transfer to constant address t (ACC already emitted)"""
        if t in self.fn.body:
            return f"goto L_{t:06X};"
        return f"{{ R.pc = {h(t)}; return; }}"

    def gen(self, ins):
        cx = Cx(ins.addr)
        op = ins.op
        hi = op >> 12
        acc = f"ACC(0x{op:04X});"
        nxt = ins.addr + ins.size
        e = cx.emit
        mode, reg = (op >> 3) & 7, op & 7
        sz = (op >> 6) & 3

        def done_next():
            e(acc)
            return "next_noacc"

        # ---- group 0 ----
        if hi == 0:
            if op in (0x003C, 0x023C, 0x0A3C):
                v = cx.ext16()
                expr = {0x003C: f"cpu_get_sr() | {h(v & 0x1F)}", 0x023C: f"cpu_get_sr() & {h(v & 0x1F)}",
                        0x0A3C: f"(cpu_get_sr() ^ {h(v)}) & 0x1F"}[op]
                e(f"cpu_set_ccr({expr});")
                return cx, "next"
            if op in (0x007C, 0x027C, 0x0A7C) or (op & 0x0138) == 0x0108:
                return cx, "interp"
            if op & 0x0100:
                e(f"uint32_t bit = R.d[{(op >> 9) & 7}];")
                self.bitop(cx, op, "bit")
                return cx, "next"
            sub = (op >> 9) & 7
            if sub == 4:
                b = cx.ext16() & 0xFF
                self.bitop(cx, op, str(b))
                return cx, "next"
            s = SZ.get(sz)
            if not s or sub == 7:
                return cx, "interp"
            imm = cx.ext32() if s == 4 else cx.ext16() if s == 2 else (cx.ext16() & 0xFF)
            imms = h(imm)
            pt = PATCHES.get(ins.addr)
            if pt and "imm" in pt:
                imms = f"(uint32_t)({pt['imm']})"
            d = Op(cx, mode, reg, s).prep()
            if sub == 6:
                e(f"{op_name('CMP', s)}({imms}, {d.get()});")
                return cx, "next"
            e(f"uint32_t v = {d.get()};")
            expr = {0: f"{op_name('LOGIC', s)}(v | {imms})", 1: f"{op_name('LOGIC', s)}(v & {imms})",
                    2: f"{op_name('SUB', s)}({imms}, v)", 3: f"{op_name('ADD', s)}({imms}, v)",
                    5: f"{op_name('LOGIC', s)}(v ^ {imms})"}[sub]
            e(d.put(expr))
            return cx, "next"

        # ---- move ----
        if hi in (1, 2, 3):
            s = {1: 1, 3: 2, 2: 4}[hi]
            src = Op(cx, mode, reg, s).prep()
            e(f"uint32_t v = {src.get()};")
            dm, dr = (op >> 6) & 7, (op >> 9) & 7
            if dm == 1:
                e(f"R.a[{dr}] = {'SX16(v)' if s == 2 else 'v'};")
                return cx, "next"
            e(f"{op_name('LOGIC', s)}(v);")
            dst = Op(cx, dm, dr, s).prep()
            e(dst.put("v"))
            return cx, "next"

        # ---- group 4 ----
        if hi == 4:
            if op == 0x4E71: return cx, "next"
            if op == 0x4E75:
                e("R.pc = POP32() & 0xFFFFFF;"); e(acc); e("return;")
                return cx, "done"
            if op == 0x4E73:
                e("if (!(R.srh & 0x20)) { R.pc = %s; cpu_interp_exec(); return; }" % h(ins.addr))
                e("{ uint16_t sr_ = (uint16_t)POP16(); uint32_t pc_ = POP32(); cpu_set_sr(sr_); R.pc = pc_ & 0xFFFFFF; }")
                e(acc); e("return;")
                return cx, "done"
            if (op & 0xFFF0) == 0x4E40:
                e(f"uint32_t sp0_ = R.a[7]; m68k_exception({32 + (op & 15)}, {h(nxt)});")
                e(acc)
                e(f"if (cpu_run_until({h(nxt)}, sp0_)) return;")
                return cx, "after_call"
            if (op & 0xFFF8) == 0x4E50:
                d = sx16(cx.ext16())
                e(f"PUSH32({'R.a[7] - 4' if reg == 7 else f'R.a[{reg}]'}); R.a[{reg}] = R.a[7]; R.a[7] += {h(d)};")
                return cx, "next"
            if (op & 0xFFF8) == 0x4E58:
                e(f"R.a[7] = R.a[{reg}]; R.a[{reg}] = POP32();")
                return cx, "next"
            if (op & 0xFFC0) in (0x4E80, 0x4EC0):
                is_call = (op & 0xFFC0) == 0x4E80
                a = Op(cx, mode, reg, 4)
                # constant target?
                tgt = None
                if mode == 7 and reg in (0, 1, 2):
                    tgt = ins.target
                    a.cx.p += 2 if reg in (0, 2) else 4
                else:
                    a.prep()
                if is_call:
                    ret = cx.p
                    if tgt is not None:
                        e(f"PUSH32({h(ret)});"); e(acc)
                        if tgt in FUNCS and tgt < 0x400000:
                            e(f"CALL_KNOWN({P}{tgt:06X}, {h(tgt)}, {h(ret)});")
                        else:
                            e(f"CALL_ANY({h(tgt)}, {h(ret)});")
                    else:
                        e(f"uint32_t tg = {a.var} & 0xFFFFFF;")
                        e(f"PUSH32({h(ret)});"); e(acc)
                        e(f"CALL_ANY(tg, {h(ret)});")
                    return cx, "after_call"
                if tgt is not None:
                    e(acc); e(self.jump(tgt))
                else:
                    e(f"R.pc = {a.var} & 0xFFFFFF;"); e(acc)
                    tbl = self.fn.table_targets(ins.addr)
                    if tbl:
                        e("switch (R.pc) {")
                        for t in tbl:
                            e(f"case {h(t)}: goto L_{t:06X};")
                        e("default: return; }")
                    else:
                        e("return;")
                return cx, "done"
            if (op & 0xF1C0) == 0x41C0:
                a = Op(cx, mode, reg, 4)
                if mode == 7 and reg in (0, 1, 2):
                    v = a.cx.ext16() if reg != 1 else a.cx.ext32()
                    t = {0: sx16(v) & 0xFFFFFFFF, 1: v, 2: (ins.addr + 2 + sx16(v)) & 0xFFFFFFFF}[reg]
                    e(f"R.a[{(op >> 9) & 7}] = {h(t)};")
                else:
                    a.prep()
                    e(f"R.a[{(op >> 9) & 7}] = {a.var};")
                return cx, "next"
            if (op & 0xF1C0) == 0x4180: return cx, "interp"   # CHK
            if (op & 0xFFB8) == 0x4880:
                if op & 0x40: e(f"R.d[{reg}] = SX16(R.d[{reg}]); NZ32(R.d[{reg}]);")
                else: e(f"R.d[{reg}] = (R.d[{reg}] & 0xFFFF0000u) | (SX8(R.d[{reg}]) & 0xFFFF); NZ16(R.d[{reg}]);")
                e("R.v = R.c = 0;")
                return cx, "next"
            if (op & 0xFB80) == 0x4880:
                self.movem(cx, op)
                return cx, "next"
            if (op & 0xFFF8) == 0x4840:
                e(f"R.d[{reg}] = (R.d[{reg}] >> 16) | (R.d[{reg}] << 16); LOGIC32(R.d[{reg}]);")
                return cx, "next"
            if (op & 0xFFC0) == 0x4840:
                a = Op(cx, mode, reg, 4).prep()
                e(f"PUSH32({a.var});")
                return cx, "next"
            if (op & 0xFFC0) == 0x40C0:
                d = Op(cx, mode, reg, 2).prep()
                e(d.put("cpu_get_sr()"))
                return cx, "next"
            if (op & 0xFFC0) == 0x44C0:
                src = Op(cx, mode, reg, 2).prep()
                e(f"cpu_set_ccr({src.get()});")
                return cx, "next"
            if (op & 0xFFC0) in (0x46C0, 0x4800, 0x4AC0) or op in (0x4E70, 0x4E72, 0x4E76, 0x4E77, 0x4AFC) \
                    or (op & 0xFFF0) == 0x4E60:
                return cx, "interp"
            s = SZ.get(sz)
            if not s: return cx, "interp"
            g = op & 0xFF00
            d = Op(cx, mode, reg, s).prep()
            if g == 0x4A00:
                e(f"{op_name('LOGIC', s)}({d.get()});"); return cx, "next"
            if g == 0x4200:
                e(d.put("0")); e("R.n = 0; R.z = 1; R.v = 0; R.c = 0;"); return cx, "next"
            fnm = {0x4000: "NEGX", 0x4400: "NEG"}.get(g)
            if fnm:
                e(f"uint32_t v = {d.get()};"); e(d.put(f"{op_name(fnm, s)}(v)")); return cx, "next"
            if g == 0x4600:
                e(f"uint32_t v = {d.get()};"); e(d.put(f"{op_name('LOGIC', s)}(~v)")); return cx, "next"
            return cx, "interp"

        # ---- group 5 ----
        if hi == 5:
            cc = (op >> 8) & 15
            if sz == 3:
                if mode == 1:   # DBcc
                    t = ins.target
                    e(f"int tk = 0; if (!cpu_cond({cc})) {{ uint16_t c_ = (uint16_t)(R.d[{reg}] - 1); "
                      f"R.d[{reg}] = (R.d[{reg}] & 0xFFFF0000u) | c_; tk = c_ != 0xFFFF; }}")
                    cx.p += 2
                    e(acc)
                    e(f"if (tk) {self.jump(t)}")
                    return cx, "next_noacc"
                d = Op(cx, mode, reg, 1).prep()
                e(d.put(f"cpu_cond({cc}) ? 0xFF : 0"))
                return cx, "next"
            s = SZ[sz]
            q = (op >> 9) & 7 or 8
            if mode == 1:
                e(f"R.a[{reg}] {'-' if op & 0x100 else '+'}= {q};")
                return cx, "next"
            d = Op(cx, mode, reg, s).prep()
            e(f"uint32_t v = {d.get()};")
            e(d.put(f"{op_name('SUB' if op & 0x100 else 'ADD', s)}({q}, v)"))
            return cx, "next"

        # ---- group 6: Bcc / BRA / BSR ----
        if hi == 6:
            cc = (op >> 8) & 15
            if (op & 0xFF) == 0: cx.p += 2
            t = ins.target
            if cc == 1:
                ret = cx.p
                e(f"PUSH32({h(ret)});"); e(acc)
                if t in FUNCS: e(f"CALL_KNOWN({P}{t:06X}, {h(t)}, {h(ret)});")
                else: e(f"CALL_ANY({h(t)}, {h(ret)});")
                return cx, "after_call"
            if cc == 0:
                e(acc); e(self.jump(t))
                return cx, "done"
            e(f"int tk = cpu_cond({cc});"); e(acc)
            e(f"if (tk) {self.jump(t)}")
            return cx, "next_noacc"

        if hi == 7:
            if op & 0x100: return cx, "interp"
            v = sx8(op & 0xFF) & 0xFFFFFFFF
            e(f"R.d[{(op >> 9) & 7}] = {h(v)}; LOGIC32({h(v)});")
            return cx, "next"

        # ---- OR/DIV/SBCD, AND/MUL/ABCD/EXG ----
        if hi in (8, 0xC):
            isC = hi == 0xC
            dn = (op >> 9) & 7
            if (op & 0x00C0) == 0x00C0:
                src = Op(cx, mode, reg, 2).prep()
                e(f"uint32_t s_ = {src.get()};")
                if isC:
                    if op & 0x100: e(f"R.d[{dn}] = (uint32_t)((int32_t)(int16_t)R.d[{dn}] * (int32_t)(int16_t)s_);")
                    else: e(f"R.d[{dn}] = (R.d[{dn}] & 0xFFFF) * (s_ & 0xFFFF);")
                    e(f"LOGIC32(R.d[{dn}]);")
                    return cx, "next"
                fnm = "DIVS" if op & 0x100 else "DIVU"
                e(f"if (!{fnm}({dn}, s_)) {{ m68k_exception(5, {h(cx.p)}); {acc} return; }}")
                return cx, "next"
            if (op & 0x01F0) == 0x0100: return cx, "interp"
            if isC and (op & 0x01F8) in (0x0140, 0x0148, 0x0188):
                k = op & 0x01F8
                a, b = ({0x0140: (f"R.d[{dn}]", f"R.d[{reg}]"), 0x0148: (f"R.a[{dn}]", f"R.a[{reg}]"),
                         0x0188: (f"R.d[{dn}]", f"R.a[{reg}]")})[k]
                e(f"uint32_t t_ = {a}; {a} = {b}; {b} = t_;")
                return cx, "next"
            s = SZ[sz]
            o = "&" if isC else "|"
            d = Op(cx, mode, reg, s).prep()
            if op & 0x100:
                e(f"uint32_t v = {d.get()};")
                e(d.put(f"{op_name('LOGIC', s)}(v {o} R.d[{dn}])"))
            else:
                e(f"uint32_t v = {d.get()};")
                e(f"uint32_t r_ = {op_name('LOGIC', s)}(v {o} R.d[{dn}]);")
                e(Op(cx, 0, dn, s).put("r_"))
            return cx, "next"

        # ---- SUB / ADD ----
        if hi in (9, 0xD):
            add = hi == 0xD
            rn, om = (op >> 9) & 7, (op >> 6) & 7
            if om in (3, 7):
                src = Op(cx, mode, reg, 2 if om == 3 else 4).prep()
                e(f"uint32_t v = {src.get()};")
                if om == 3: e("v = SX16(v);")
                e(f"R.a[{rn}] {'+' if add else '-'}= v;")
                return cx, "next"
            s = SZ[om & 3]
            if om >= 4 and mode < 2:
                return cx, "interp"   # ADDX/SUBX
            fnm = "ADD" if add else "SUB"
            if om < 4:
                src = Op(cx, mode, reg, s).prep()
                e(f"uint32_t v = {src.get()};")
                e(f"uint32_t r_ = {op_name(fnm, s)}(v, R.d[{rn}]);")
                e(Op(cx, 0, rn, s).put("r_"))
            else:
                d = Op(cx, mode, reg, s).prep()
                e(f"uint32_t v = {d.get()};")
                e(d.put(f"{op_name(fnm, s)}(R.d[{rn}], v)"))
            return cx, "next"

        # ---- CMP / CMPA / EOR / CMPM ----
        if hi == 0xB:
            rn, om = (op >> 9) & 7, (op >> 6) & 7
            if om in (3, 7):
                src = Op(cx, mode, reg, 2 if om == 3 else 4).prep()
                e(f"uint32_t v = {src.get()};")
                if om == 3: e("v = SX16(v);")
                e(f"CMP32(v, R.a[{rn}]);")
                return cx, "next"
            s = SZ[om & 3]
            if om < 4:
                src = Op(cx, mode, reg, s).prep()
                e(f"{op_name('CMP', s)}({src.get()}, R.d[{rn}]);")
                return cx, "next"
            if mode == 1:
                return cx, "interp"   # CMPM
            d = Op(cx, mode, reg, s).prep()
            e(f"uint32_t v = {d.get()};")
            e(d.put(f"{op_name('LOGIC', s)}(v ^ R.d[{rn}])"))
            return cx, "next"

        # ---- shifts ----
        if hi == 0xE:
            left = (op >> 8) & 1
            names = [("ASR", "ASL"), ("LSR", "LSL"), ("ROXR", "ROXL"), ("ROR", "ROL")]
            if sz == 3:
                t = (op >> 9) & 3
                d = Op(cx, mode, reg, 2).prep()
                e(f"uint32_t v = {d.get()};")
                e(d.put(f"{names[t][left]}16(v, 1)"))
                return cx, "next"
            s = SZ[sz]
            t = (op >> 3) & 3
            if op & 0x20:
                e(f"int n_ = (int)(R.d[{(op >> 9) & 7}] & 63);")
            else:
                e(f"int n_ = {((op >> 9) & 7) or 8};")
            e(f"uint32_t r_ = {names[t][left]}{BITS[s]}(R.d[{reg}], n_);")
            if t == 2:
                e("if (n_ == 0) R.c = R.x;")
            e(Op(cx, 0, reg, s).put("r_"))
            return cx, "next"

        return cx, "interp"

    def bitop(self, cx, op, bit):
        mode, reg, typ = (op >> 3) & 7, op & 7, (op >> 6) & 3
        e = cx.emit
        if mode == 0:
            e(f"uint32_t m_ = 1u << (({bit}) & 31);")
            e(f"R.z = (R.d[{reg}] & m_) == 0;")
            if typ == 1: e(f"R.d[{reg}] ^= m_;")
            elif typ == 2: e(f"R.d[{reg}] &= ~m_;")
            elif typ == 3: e(f"R.d[{reg}] |= m_;")
            return
        d = Op(cx, mode, reg, 1).prep()
        e(f"uint32_t m_ = 1u << (({bit}) & 7);")
        e(f"uint32_t v = {d.get()};")
        e("R.z = (v & m_) == 0;")
        if typ:
            e(d.put({1: "v ^ m_", 2: "v & ~m_", 3: "v | m_"}[typ]))

    def movem(self, cx, op):
        e = cx.emit
        s = 4 if op & 0x40 else 2
        mode, reg = (op >> 3) & 7, op & 7
        mask = cx.ext16()
        W = BITS[s]
        if not (op & 0x0400):
            if mode == 4:
                e(f"uint32_t a_ = R.a[{reg}];")
                for i in range(16):
                    if mask & (1 << i):
                        v = f"R.a[{7 - i}]" if i < 8 else f"R.d[{15 - i}]"
                        e(f"a_ -= {s}; W{W}(a_, {v});")
                e(f"R.a[{reg}] = a_;")
            else:
                a = Op(cx, mode, reg, s).prep()
                e(f"uint32_t a_ = {a.var};")
                for i in range(16):
                    if mask & (1 << i):
                        v = f"R.d[{i}]" if i < 8 else f"R.a[{i - 8}]"
                        e(f"W{W}(a_, {v}); a_ += {s};")
        else:
            if mode == 3:
                e(f"uint32_t a_ = R.a[{reg}];")
            else:
                a = Op(cx, mode, reg, s).prep()
                e(f"uint32_t a_ = {a.var};")
            for i in range(16):
                if mask & (1 << i):
                    dst = f"R.d[{i}]" if i < 8 else f"R.a[{i - 8}]"
                    rdx = f"R{W}(a_)" if s == 4 else f"SX16(R16(a_))"
                    e(f"{dst} = {rdx}; a_ += {s};")
            if mode == 3:
                e(f"R.a[{reg}] = a_;")


# ---------------------------------------------------------------------------
# Functions
# ---------------------------------------------------------------------------
INSNS = {}
for s, e_ in cmap["code"]:
    a = s
    while a < e_:
        i = decode(rom, a)
        if i is None:
            break
        INSNS[a] = i
        a += i.size

FUNCS = set(cmap["functions"]) | set(EXTRA)
FUNCS = {f for f in FUNCS if f < 0x400000}
TABLES = {}
for k, t in cmap["tables"].items():
    TABLES.setdefault(t["site"], []).extend(t["targets"])

# extra entries (found by the interpreter at run time) may not be traced yet
missing = [f for f in FUNCS if f not in INSNS]
if missing:
    work = deque(missing)
    while work:
        a = work.popleft()
        while a not in INSNS:
            i = decode(rom, a)
            if i is None: break
            INSNS[a] = i
            if i.kind in ("branch", "cbranch") and i.target < 0x400000: work.append(i.target)
            if i.kind == "call" and i.target < 0x400000: FUNCS.add(i.target); work.append(i.target)
            if i.kind in ("branch", "jump_ind", "ret", "stop"): break
            a += i.size
    FUNCS = {f for f in FUNCS if f in INSNS}


class Function:
    def __init__(self, entry):
        self.entry = entry
        body, work = set(), deque([entry])
        while work:
            a = work.popleft()
            while a in INSNS and a not in body:
                if a != entry and a in FUNCS:
                    break
                body.add(a)
                i = INSNS[a]
                if i.kind in ("branch", "cbranch") and i.target in INSNS and i.target not in FUNCS:
                    work.append(i.target)
                if i.kind in ("jump_ind",):
                    for t in TABLES.get(a, []):
                        if t in INSNS and t not in FUNCS: work.append(t)
                if i.kind in ("branch", "jump_ind", "ret", "stop"):
                    break
                a += i.size
        self.body = body

    def table_targets(self, site):
        return sorted({t for t in TABLES.get(site, []) if t in self.body})


def gen_function(fn, gen):
    out = [f"void {P}{fn.entry:06X}(uint32_t entry)", "{", "    switch (entry) {"]
    for a in sorted(fn.body):
        out.append(f"    case {h(a)}: goto L_{a:06X};")
    out.append("    default: R.pc = entry; return;")
    out.append("    }")
    stats = {"insns": 0, "interp": 0}
    addrs = sorted(fn.body)
    for idx, a in enumerate(addrs):
        ins = INSNS[a]
        nxt = a + ins.size
        stats["insns"] += 1
        out.append(f"L_{a:06X}: I({h(a)}); /* {ins.name} */")
        try:
            cx, flow = gen.gen(ins)
            if flow != "interp" and flow not in ("done",) and cx.p != nxt:
                raise Unsupported(f"size mismatch {cx.p - a} vs {ins.size}")
        except Unsupported:
            cx, flow = Cx(a), "interp"
        if flow == "interp":
            stats["interp"] += 1
            out.append(f"    {{ R.pc = {h(a)}; cpu_interp_exec(); if (R.pc != {h(nxt)}) return; }}")
            flow = "next_noacc"
        else:
            pt = PATCHES.get(a)
            if pt and "post" in pt and flow == "next":
                cx.lines.append(pt["post"])
            out.append("    { " + " ".join(cx.lines) + (f" ACC(0x{ins.op:04X});" if flow == "next" else "") + " }")
        if flow in ("next", "next_noacc", "after_call"):
            if not (idx + 1 < len(addrs) and addrs[idx + 1] == nxt):
                out.append(f"    {gen.jump(nxt)}")
    out.append("}")
    return out, stats


def main():
    os.makedirs(OUT, exist_ok=True)
    funcs = [Function(e) for e in sorted(FUNCS)]
    files, cur, n, total = [], [], 0, {"insns": 0, "interp": 0}
    owner = {}
    for fn in funcs:
        for a in fn.body:
            owner.setdefault(a, fn.entry)
    header = ['#include "rt.h"']
    protos = [f"void {P}{f.entry:06X}(uint32_t entry);" for f in funcs]
    with open(os.path.join(OUT, "ps4_funcs.h"), "w") as f:
        f.write("#include \"rt.h\"\n" + "\n".join(protos) + "\n")
    chunk = 0
    for fn in funcs:
        g = Gen(fn)
        lines, st = gen_function(fn, g)
        for k in total: total[k] += st[k]
        cur.extend(lines); n += len(fn.body)
        if n > 4000:
            files.append(cur); cur, n = [], 0
    if cur: files.append(cur)
    for i, body in enumerate(files):
        with open(os.path.join(OUT, f"ps4_recomp_{i:03d}.c"), "w") as f:
            f.write('#include "ps4_funcs.h"\n\n' + "\n".join(body) + "\n")
    # lookup table: instruction address -> function containing it
    pairs = sorted(owner.items())
    with open(os.path.join(OUT, "ps4_table.c"), "w") as f:
        f.write('#include "ps4_funcs.h"\n#include <stdlib.h>\n')
        f.write(f"static const uint32_t addr_[{len(pairs)}] = {{\n")
        for k in range(0, len(pairs), 12):
            f.write("  " + ", ".join(h(a) for a, _ in pairs[k:k + 12]) + ",\n")
        f.write("};\n")
        f.write(f"static const recomp_fn fn_[{len(pairs)}] = {{\n")
        for k in range(0, len(pairs), 6):
            f.write("  " + ", ".join(f"{P}{e:06X}" for _, e in pairs[k:k + 6]) + ",\n")
        f.write("};\n")
        f.write("""static recomp_fn *map_;
static recomp_fn ps4_lookup(uint32_t pc)
{
    if (pc >= 0x400000 || (pc & 1)) return 0;
    return map_[pc >> 1];
}
void ps4_recomp_install(void)
{
    if (!map_) {
        map_ = calloc(0x200000, sizeof *map_);
        for (size_t i = 0; i < sizeof addr_ / sizeof addr_[0]; i++) map_[addr_[i] >> 1] = fn_[i];
    }
    cpu_lookup = ps4_lookup;
}
""")
    print(f"functions {len(funcs)}  instructions {total['insns']}  via interpreter {total['interp']}  files {len(files)}")


if __name__ == "__main__":
    main()
