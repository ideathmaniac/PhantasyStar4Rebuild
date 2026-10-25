import sys
d=open(sys.argv[1],'rb').read(); vram=d[:65536]; reg=d[65744:65776]; ram=d[65776:65776+65536]
def w(a): a&=0xffff; return int.from_bytes(ram[a:a+2],'big')
def b(a): return ram[a&0xffff]
cx, cy = w(0xEF94), w(0xEF90)
mask = 0x1FFF if b(0xEC4F) else 0xBFFF
res = {}
for name, mapbase, wreg, vbase in (('A', 0xA000, 0xEC64, (reg[2]&0x38)<<10), ('B', 0xB000, 0xEC66, (reg[4]&7)<<13)):
    mw = b(wreg) + 1
    ok = bad = 0; ext_ok = ext_bad = 0
    for ty in range(28):
        for tx in range(-6, 46):
            wx = cx + tx*8; wy = cy + ty*8
            if wx < 0 or wy < 0: continue
            bx, by = wx >> 5, wy >> 5
            blk = b(mapbase + by*mw + bx)
            exp = w(0x6000 + blk*32 + (((wy>>3)&3)*4 + ((wx>>3)&3))*2) & mask
            pa = vbase + (((wy>>3)&31)*64 + ((wx>>3)&63))*2
            got = int.from_bytes(vram[pa:pa+2],'big')
            inside = 0 <= tx < 40
            if inside: ok += got == exp; bad += got != exp
            else: ext_ok += got == exp; ext_bad += got != exp
    res[name] = (ok, bad, ext_ok, ext_bad, mw)
print(sys.argv[1], 'cam', cx, cy, ' '.join(f"{k}: view {v[0]}/{v[0]+v[1]} sides {v[2]}/{v[2]+v[3]} (map w {v[4]})" for k, v in res.items()))
