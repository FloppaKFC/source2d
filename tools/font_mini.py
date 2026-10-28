#!/usr/bin/env python3
"""Genera src/font_data_mini.h: la fuente por defecto del jugador ligero. Es la misma DejaVu Sans de
src/font_data.h pero solo con ASCII, Latin-1 (todo el espanol: tildes, ñ, ¿ ¡) y €, sin tablas que stb_truetype no usa.
Necesita fontTools (solo para regenerarla; el .h ya generado va en src/).
Uso: font_mini.py src/font_data.h src/font_data_mini.h"""
import io, re, sys
from fontTools import subset
from fontTools.ttLib import TTFont

src, out = sys.argv[1:3]
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', open(src).read()))
font = TTFont(io.BytesIO(data))
opt = subset.Options()
opt.layout_features = ['kern']
opt.hinting = False
opt.name_IDs = [0, 1, 2]
opt.drop_tables += ['MATH', 'GSUB', 'gasp', 'GDEF']
opt.notdef_outline = True
sub = subset.Subsetter(opt)
sub.populate(unicodes=list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [0x20AC])
sub.subset(font)
buf = io.BytesIO()
font.save(buf)
b = buf.getvalue()
with open(out, 'w') as f:
    f.write('/* generado por tools/font_mini.py: DejaVu Sans (licencia libre) solo con ASCII, Latin-1 y €, para el jugador ligero. */\n')
    f.write('#ifndef S2D_FONT_DATA_H\n#define S2D_FONT_DATA_H\n#include <stddef.h>\nstatic const unsigned char font_data[] = {\n')
    for i in range(0, len(b), 24):
        f.write('  ' + ','.join('0x%02x' % x for x in b[i:i + 24]) + ',\n')
    f.write('};\nstatic const size_t font_data_len = %d;\n#endif\n' % len(b))
print(out, len(data), '->', len(b))
