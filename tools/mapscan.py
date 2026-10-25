import sys, glob, os
def check(fn):
    d=open(fn,'rb').read(); vram=d[:65536]; reg=d[65744:65776]; ram=d[65776:65776+65536]
    def w(a): a&=0xffff; return int.from_bytes(ram[a:a+2],'big')
    def b(a): return ram[a&0xffff]
    cx, cy = w(0xEF94), w(0xEF90)
    out=[]
    for name, mb, wr, hr, vb in (('A',0xA000,0xEC64,0xEC65,(reg[2]&0x38)<<10),('B',0xB000,0xEC66,0xEC67,(reg[4]&7)<<13)):
        mw=b(wr)+1; mh=b(hr)+1
        ok=n=0
        for ty in range(0,28,2):
            for tx in range(0,40,2):
                sx=cx+tx*8; sy=cy+ty*8
                bx=(sx>>5)%mw; by=(sy>>5)%mh
                blk=b(mb+by*mw+bx)
                exp=w(0x6000+blk*32+(((sy>>3)&3)*4+((sx>>3)&3))*2)&(0x1FFF if b(0xEC4F) else 0xBFFF)
                got=int.from_bytes(vram[vb+(((sy>>3)&31)*64+((sx>>3)&63))*2:][:2],'big')
                ok+=exp==got; n+=1
        out.append(ok*100//n)
    return cx,cy,out, b(0xEC64)+1, b(0xEC65)+1, b(0xEC66)+1, b(0xEC67)+1
for fn in sorted(glob.glob(sys.argv[1]+'/*.bin')):
    cx,cy,o,a,b2,c,dd=check(fn)
    print(os.path.basename(fn), f'cam {cx:5d},{cy:5d}  A {o[0]:3d}%  B {o[1]:3d}%  mapA {a}x{b2} mapB {c}x{dd}')
