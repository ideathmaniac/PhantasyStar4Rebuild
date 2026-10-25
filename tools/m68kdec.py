"""68000 instruction decoder: length, validity and control flow.

Written from the M68000 Programmer's Reference Manual opcode map. Only what a
tracer needs: how long each instruction is, whether the opcode is legal on a
68000, and where control can go next.
"""
import struct

def w(rom, a):  return struct.unpack_from(">H", rom, a)[0]
def sw(rom, a): return struct.unpack_from(">h", rom, a)[0]
def l(rom, a):  return struct.unpack_from(">I", rom, a)[0]

CC = ["t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"]


class Bad(Exception):
    pass


def ea_ext(mode, reg, size, allow="all"):
    """Extension words (bytes) for an effective address. size: 1, 2, 4."""
    if mode in (0, 1):
        if allow in ("data_alt_nodn", "mem", "control") or (mode == 1 and allow in ("data", "data_alt")):
            raise Bad
        return 0
    if mode in (2, 3, 4):
        if allow == "control" and mode != 2:
            raise Bad
        return 0
    if mode in (5, 6):
        return 2
    # mode 7
    if reg == 0: return 2
    if reg == 1: return 4
    if reg in (2, 3):
        if allow in ("alt", "data_alt", "data_alt_nodn"):
            raise Bad
        return 2
    if reg == 4:
        if allow not in ("all", "data"):
            raise Bad
        return 4 if size == 4 else 2
    raise Bad


class Insn:
    __slots__ = ("addr", "size", "op", "kind", "target", "ea", "name")

    def __str__(self):
        t = f" -> {self.target:06X}" if self.target is not None else ""
        return f"{self.addr:06X} {self.op:04X} {self.name}{t}"


def _length(rom, a, op):
    """Returns (length, name) or raises Bad."""
    hi = op >> 12
    mode, reg = (op >> 3) & 7, op & 7
    sz2 = (op >> 6) & 3
    SZ = {0: 1, 1: 2, 2: 4}
    if hi == 0:
        if op in (0x003C, 0x007C, 0x023C, 0x027C, 0x0A3C, 0x0A7C):
            return 4, "imm_ccr_sr"
        if (op & 0x0138) == 0x0108:
            return 4, "movep"
        if op & 0x0100:
            return 2 + ea_ext(mode, reg, 1, "data" if sz2 == 0 else "data_alt"), "bitop_dyn"
        sub = (op >> 9) & 7
        if sub == 4:  # static bit
            if mode == 1: raise Bad
            return 4 + ea_ext(mode, reg, 1, "data" if sz2 == 0 else "data_alt"), "bitop_imm"
        if sub == 7 or sz2 == 3: raise Bad
        imm = 4 if sz2 == 2 else 2
        allow = "data" if sub == 6 else "data_alt"  # cmpi on 68000: data alterable, but allow pc modes harmlessly
        if sub == 6 and mode == 7 and reg in (2, 3): raise Bad
        return 2 + imm + ea_ext(mode, reg, SZ[sz2], "data_alt" if sub != 6 else "data"), \
            ["ori", "andi", "subi", "addi", "", "eori", "cmpi"][sub]
    if hi in (1, 2, 3):
        size = {1: 1, 3: 2, 2: 4}[hi]
        dmode, dreg = (op >> 6) & 7, (op >> 9) & 7
        if size == 1 and mode == 1: raise Bad
        n = 2 + ea_ext(mode, reg, size)
        if dmode == 1:
            if size == 1: raise Bad
            return n, "movea"
        return n + ea_ext(dmode, dreg, size, "data_alt"), "move"
    if hi == 4:
        if op == 0x4E72: return 4, "stop"
        if op in (0x4E70, 0x4E71, 0x4E73, 0x4E75, 0x4E76, 0x4E77):
            return 2, {0x4E70: "reset", 0x4E71: "nop", 0x4E73: "rte", 0x4E75: "rts", 0x4E76: "trapv", 0x4E77: "rtr"}[op]
        if op == 0x4AFC: return 2, "illegal"
        if (op & 0xFFF0) == 0x4E40: return 2, "trap"
        if (op & 0xFFF8) == 0x4E50: return 4, "link"
        if (op & 0xFFF8) == 0x4E58: return 2, "unlk"
        if (op & 0xFFF0) == 0x4E60: return 2, "move_usp"
        if (op & 0xFFC0) == 0x4E80: return 2 + ea_ext(mode, reg, 4, "control"), "jsr"
        if (op & 0xFFC0) == 0x4EC0: return 2 + ea_ext(mode, reg, 4, "control"), "jmp"
        if (op & 0xF1C0) == 0x41C0: return 2 + ea_ext(mode, reg, 4, "control"), "lea"
        if (op & 0xF1C0) == 0x4180:
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 2, "data"), "chk"
        if (op & 0xFFB8) == 0x4880: return 2, "ext"
        if (op & 0xFB80) == 0x4880:  # movem
            to_mem = not (op & 0x0400)
            if to_mem:
                if mode in (0, 1, 3) or (mode == 7 and reg > 1): raise Bad
            else:
                if mode in (0, 1, 4) or (mode == 7 and reg > 3): raise Bad
            return 4 + ea_ext(mode, reg, 4), "movem"
        if (op & 0xFFF8) == 0x4840: return 2, "swap"
        if (op & 0xFFC0) == 0x4840: return 2 + ea_ext(mode, reg, 4, "control"), "pea"
        if (op & 0xFFC0) == 0x4800:
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 1, "data_alt"), "nbcd"
        if (op & 0xFFC0) == 0x40C0:
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 2, "data_alt"), "move_from_sr"
        if (op & 0xFFC0) in (0x44C0, 0x46C0):
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 2, "data"), "move_to_ccr" if (op & 0xFFC0) == 0x44C0 else "move_to_sr"
        if (op & 0xFFC0) == 0x4AC0:
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 1, "data_alt"), "tas"
        grp = op & 0xFF00
        if grp in (0x4000, 0x4200, 0x4400, 0x4600, 0x4A00) and sz2 != 3:
            if mode == 1: raise Bad
            if grp == 0x4A00:
                if mode == 7 and reg > 1: raise Bad
                return 2 + ea_ext(mode, reg, SZ[sz2]), "tst"
            return 2 + ea_ext(mode, reg, SZ[sz2], "data_alt"), {0x4000: "negx", 0x4200: "clr", 0x4400: "neg", 0x4600: "not"}[grp]
        raise Bad
    if hi == 5:
        if sz2 == 3:
            if mode == 1: return 4, "dbcc"
            return 2 + ea_ext(mode, reg, 1, "data_alt"), "scc"
        if mode == 1 and sz2 == 0: raise Bad
        return 2 + ea_ext(mode, reg, SZ[sz2], "alt"), "addq" if not (op & 0x100) else "subq"
    if hi == 6:
        d8 = op & 0xFF
        if d8 == 0xFF: raise Bad
        return (4 if d8 == 0 else 2), "bcc"
    if hi == 7:
        if op & 0x100: raise Bad
        return 2, "moveq"
    if hi in (8, 0xC):
        if (op & 0xF0C0) == (hi << 12 | 0x00C0):
            if mode == 1: raise Bad
            return 2 + ea_ext(mode, reg, 2, "data"), ("divu" if hi == 8 else "mulu") if not (op & 0x100) else ("divs" if hi == 8 else "muls")
        if (op & 0xF1F0) == (hi << 12 | 0x0100):
            return 2, "sbcd" if hi == 8 else "abcd"
        if hi == 0xC and (op & 0xF1F8) in (0xC140, 0xC148, 0xC188):
            return 2, "exg"
        if mode == 1: raise Bad
        if op & 0x100:
            return 2 + ea_ext(mode, reg, SZ[sz2], "mem"), "or" if hi == 8 else "and"
        return 2 + ea_ext(mode, reg, SZ[sz2], "data"), "or" if hi == 8 else "and"
    if hi in (9, 0xD):
        nm = "sub" if hi == 9 else "add"
        if sz2 == 3:
            return 2 + ea_ext(mode, reg, 4 if op & 0x100 else 2), nm + "a"
        if (op & 0x0130) == 0x0100:
            return 2, nm + "x"
        if op & 0x100:
            return 2 + ea_ext(mode, reg, SZ[sz2], "mem"), nm
        if mode == 1 and sz2 == 0: raise Bad
        return 2 + ea_ext(mode, reg, SZ[sz2]), nm
    if hi == 0xB:
        if sz2 == 3:
            return 2 + ea_ext(mode, reg, 4 if op & 0x100 else 2), "cmpa"
        if op & 0x100:
            if mode == 1: return 2, "cmpm"
            return 2 + ea_ext(mode, reg, SZ[sz2], "data_alt"), "eor"
        if mode == 1 and sz2 == 0: raise Bad
        return 2 + ea_ext(mode, reg, SZ[sz2]), "cmp"
    if hi == 0xE:
        if sz2 == 3:
            if op & 0x0800: raise Bad
            return 2 + ea_ext(mode, reg, 2, "mem"), "shift_mem"
        return 2, "shift"
    raise Bad  # line A / line F


def decode(rom, addr):
    if addr & 1 or addr < 0 or addr + 2 > len(rom):
        return None
    op = w(rom, addr)
    try:
        n, name = _length(rom, addr, op)
    except Bad:
        return None
    if addr + n > len(rom):
        return None
    ins = Insn()
    ins.addr, ins.size, ins.op, ins.name = addr, n, op, name
    ins.kind, ins.target, ins.ea = "normal", None, None
    hi = op >> 12
    if hi == 6:
        cond = (op >> 8) & 0xF
        d8 = op & 0xFF
        disp = sw(rom, addr + 2) if d8 == 0 else (d8 - 256 if d8 & 0x80 else d8)
        ins.target = (addr + 2 + disp) & 0xFFFFFF
        ins.kind = {0: "branch", 1: "call"}.get(cond, "cbranch")
        ins.name = {0: "bra", 1: "bsr"}.get(cond, "b" + CC[cond])
    elif name == "dbcc":
        ins.target = (addr + 2 + sw(rom, addr + 2)) & 0xFFFFFF
        ins.kind = "cbranch"
    elif name in ("rts", "rte", "rtr"):
        ins.kind = "ret"
    elif name in ("stop", "illegal"):
        ins.kind = "stop"
    elif name in ("jmp", "jsr"):
        mode, reg = (op >> 3) & 7, op & 7
        tgt = None
        if mode == 7 and reg == 0: tgt = sw(rom, addr + 2) & 0xFFFFFF
        elif mode == 7 and reg == 1: tgt = l(rom, addr + 2) & 0xFFFFFF
        elif mode == 7 and reg == 2: tgt = (addr + 2 + sw(rom, addr + 2)) & 0xFFFFFF
        call = name == "jsr"
        if tgt is not None:
            ins.target, ins.kind = tgt, ("call" if call else "branch")
        else:
            ins.kind, ins.ea = ("call_ind" if call else "jump_ind"), (mode, reg)
    elif name == "trap":
        ins.kind = "trap"
    return ins


# ---------------------------------------------------------------------------
# Text disassembly (for reverse engineering listings)
# ---------------------------------------------------------------------------
def _ea_text(rom, p, mode, reg, size, pc_base=None):
    """returns (text, new_p)"""
    if mode == 0: return f"d{reg}", p
    if mode == 1: return f"a{reg}", p
    if mode == 2: return f"(a{reg})", p
    if mode == 3: return f"(a{reg})+", p
    if mode == 4: return f"-(a{reg})", p
    if mode == 5: return f"{sw(rom, p)}(a{reg})", p + 2
    if mode == 6:
        e = w(rom, p); xr = ("a" if e & 0x8000 else "d") + str((e >> 12) & 7) + (".l" if e & 0x800 else ".w")
        d = (e & 0xFF) - 256 if e & 0x80 else e & 0xFF
        return f"{d}(a{reg},{xr})", p + 2
    if reg == 0: return f"(${sw(rom, p) & 0xFFFFFF:06X}).w", p + 2
    if reg == 1: return f"(${l(rom, p):06X}).l", p + 4
    if reg == 2: return f"${(p + sw(rom, p)) & 0xFFFFFF:06X}(pc)", p + 2
    if reg == 3:
        e = w(rom, p); xr = ("a" if e & 0x8000 else "d") + str((e >> 12) & 7) + (".l" if e & 0x800 else ".w")
        d = (e & 0xFF) - 256 if e & 0x80 else e & 0xFF
        return f"${(p + d) & 0xFFFFFF:06X}(pc,{xr})", p + 2
    if reg == 4:
        if size == 4: return f"#${l(rom, p):X}", p + 4
        v = w(rom, p)
        return (f"#${v & 0xFF:X}" if size == 1 else f"#${v:X}"), p + 2
    return "?", p


def disasm(rom, a):
    """One instruction as text (best effort, for listings)."""
    i = decode(rom, a)
    if i is None:
        return f"dc.w ${w(rom, a):04X}", 2
    op, p = i.op, a + 2
    mode, reg, sz = (op >> 3) & 7, op & 7, (op >> 6) & 3
    S = {0: 1, 1: 2, 2: 4}.get(sz, 2)
    sfx = {1: ".b", 2: ".w", 4: ".l"}
    hi = op >> 12
    n = i.name
    try:
        if hi in (1, 2, 3):
            s = {1: 1, 3: 2, 2: 4}[hi]
            src, p = _ea_text(rom, p, mode, reg, s)
            dst, p = _ea_text(rom, p, (op >> 6) & 7, (op >> 9) & 7, s)
            return f"{'movea' if ((op >> 6) & 7) == 1 else 'move'}{sfx[s]} {src},{dst}", i.size
        if n in ("bra", "bsr") or n.startswith("b") and hi == 6:
            return f"{n} ${i.target:06X}", i.size
        if n == "dbcc":
            return f"db{CC[(op >> 8) & 15]} d{reg},${i.target:06X}", i.size
        if n in ("jmp", "jsr", "lea", "pea"):
            t, p = _ea_text(rom, p, mode, reg, 4)
            return f"{n} {t}" + (f",a{(op >> 9) & 7}" if n == "lea" else ""), i.size
        if n == "moveq": return f"moveq #{(op & 0xFF) - 256 if op & 0x80 else op & 0xFF},d{(op >> 9) & 7}", i.size
        if n in ("ori", "andi", "subi", "addi", "eori", "cmpi"):
            imm, p = _ea_text(rom, p, 7, 4, S)
            t, p = _ea_text(rom, p, mode, reg, S)
            return f"{n}{sfx[S]} {imm},{t}", i.size
        if n in ("bitop_imm", "bitop_dyn"):
            bn = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            if n == "bitop_imm": b = f"#{w(rom, p) & 0xFF}"; p += 2
            else: b = f"d{(op >> 9) & 7}"
            t, p = _ea_text(rom, p, mode, reg, 1)
            return f"{bn} {b},{t}", i.size
        if n in ("addq", "subq"):
            q = (op >> 9) & 7 or 8
            t, p = _ea_text(rom, p, mode, reg, S)
            return f"{n}{sfx[S]} #{q},{t}", i.size
        if n in ("clr", "tst", "neg", "negx", "not"):
            t, p = _ea_text(rom, p, mode, reg, S)
            return f"{n}{sfx[S]} {t}", i.size
        if n == "scc":
            t, p = _ea_text(rom, p, mode, reg, 1)
            return f"s{CC[(op >> 8) & 15]} {t}", i.size
        if n in ("add", "sub", "and", "or", "cmp", "eor"):
            t, p = _ea_text(rom, p, mode, reg, S)
            dn = f"d{(op >> 9) & 7}"
            if n in ("eor",) or (op & 0x100 and n != "cmp"):
                return f"{n}{sfx[S]} {dn},{t}", i.size
            return f"{n}{sfx[S]} {t},{dn}", i.size
        if n in ("adda", "suba", "cmpa"):
            s = 4 if op & 0x100 else 2
            t, p = _ea_text(rom, p, mode, reg, s)
            return f"{n}{sfx[s]} {t},a{(op >> 9) & 7}", i.size
        if n in ("mulu", "muls", "divu", "divs"):
            t, p = _ea_text(rom, p, mode, reg, 2)
            return f"{n} {t},d{(op >> 9) & 7}", i.size
        if n == "shift":
            kinds = ["as", "ls", "rox", "ro"]
            k = kinds[(op >> 3) & 3] + ("l" if op & 0x100 else "r")
            cnt = f"d{(op >> 9) & 7}" if op & 0x20 else f"#{(op >> 9) & 7 or 8}"
            return f"{k}{sfx[S]} {cnt},d{reg}", i.size
        if n == "movem":
            s = 4 if op & 0x40 else 2
            mask = w(rom, p); p += 2
            t, p = _ea_text(rom, p, mode, reg, s)
            regs = []
            for b in range(16):
                bit = (15 - b) if mode == 4 else b
                if mask & (1 << bit): regs.append(("d" if b < 8 else "a") + str(b & 7))
            r = "/".join(regs)
            return (f"movem{sfx[s]} {r},{t}" if not (op & 0x400) else f"movem{sfx[s]} {t},{r}"), i.size
        if n == "trap": return f"trap #{op & 15}", i.size
        if n == "ext": return f"ext{'.l' if op & 0x40 else '.w'} d{reg}", i.size
        if n == "swap": return f"swap d{reg}", i.size
    except Exception:
        pass
    return n, i.size
