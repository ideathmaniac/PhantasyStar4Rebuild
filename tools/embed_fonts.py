#!/usr/bin/env python3
"""Writes the DejaVu fonts (third_party/fonts) as C arrays for port/sdl_win32.c."""
import os, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'third_party', 'fonts')
with open(sys.argv[1], 'w') as out:
    for name, f in (('font_regular', 'DejaVuSans.ttf'), ('font_bold', 'DejaVuSans-Bold.ttf')):
        d = open(os.path.join(root, f), 'rb').read()
        out.write('const unsigned char %s[] = {' % name)
        for i in range(0, len(d), 4096):
            out.write(','.join(str(b) for b in d[i:i + 4096]) + (',\n' if i + 4096 < len(d) else ''))
        out.write('};\nconst unsigned int %s_len = %d;\n' % (name, len(d)))
