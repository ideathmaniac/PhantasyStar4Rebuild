#!/usr/bin/env python3
"""dis.py ROM START END : text listing with labels"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m68kdec import disasm
rom = open(sys.argv[1], 'rb').read()
a, e = int(sys.argv[2], 16), int(sys.argv[3], 16)
cm = json.load(open(os.path.join(os.path.dirname(__file__), '..', 'out', 'codemap.json')))
funcs = set(cm['functions'])
while a < e:
    t, n = disasm(rom, a)
    print(("\nsub_%06X:\n" % a if a in funcs else "") + f"  {a:06X}  {t}")
    a += n
