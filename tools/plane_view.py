#!/usr/bin/env python3
"""Render both scroll planes of a VDP dump in full, with the 320x224 view marked."""
import sys, struct
from PIL import Image, ImageDraw
d = open(sys.argv[1], 'rb').read()
vram = d[:65536]
cram = [int.from_bytes(d[65536 + 2*i:65538 + 2*i], 'little') for i in range(64)]
vsram = [int.from_bytes(d[65664 + 2*i:65666 + 2*i], 'little') for i in range(40)]
reg = d[65744:65776]
lv = [0, 52, 87, 116, 144, 172, 206, 255]
def rgb(c): return (lv[(c >> 1) & 7], lv[(c >> 5) & 7], lv[(c >> 9) & 7])
sz = {0: 32, 1: 64, 3: 128}
pw, ph = sz.get(reg[16] & 3, 32), sz.get((reg[16] >> 4) & 3, 32)
hsb = (reg[13] & 0x3f) << 10
out = Image.new('RGB', (pw * 8, ph * 8 * 2 + 8), (60, 0, 60))
px = out.load()
for pi, base in enumerate(((reg[2] & 0x38) << 10, (reg[4] & 7) << 13)):
    for ty in range(ph):
        for tx in range(pw):
            e = int.from_bytes(vram[base + (ty * pw + tx) * 2: base + (ty * pw + tx) * 2 + 2], 'big')
            t = e & 0x7ff; pal = (e >> 13) & 3; hf = e & 0x800; vf = e & 0x1000
            for y in range(8):
                for x in range(8):
                    sx = 7 - x if hf else x; sy = 7 - y if vf else y
                    b = vram[(t * 32 + sy * 4 + sx // 2) & 0xffff]
                    c = (b & 15) if sx & 1 else (b >> 4)
                    if c: px[tx * 8 + x, pi * (ph * 8 + 8) + ty * 8 + y] = rgb(cram[pal * 16 + c])
    hs = int.from_bytes(vram[hsb + pi * 2: hsb + pi * 2 + 2], 'big') & 0x3ff
    vs = vsram[pi] & 0x3ff
    x0 = (-hs) % (pw * 8); y0 = vs % (ph * 8)
    dr = ImageDraw.Draw(out)
    for dx in (0, -pw * 8):
        for dy in (0, -ph * 8):
            dr.rectangle([x0 + dx, pi * (ph * 8 + 8) + y0 + dy, x0 + dx + 319, pi * (ph * 8 + 8) + y0 + dy + 223], outline=(255, 255, 0))
    print(f"plane {'AB'[pi]} hscroll {hs} vscroll {vs} -> view x {x0} y {y0}")
print('plane size', pw, ph, 'hmode', reg[11] & 3, 'vmode', (reg[11] >> 2) & 1)
out.crop((0, 0, pw * 8, ph * 16 + 8)).save(sys.argv[2])
