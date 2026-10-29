#!/usr/bin/env python3
"""Genera <salida>.c con los motores que el editor lleva dentro para exportar juegos (y su cabecera players_bin.h):
  stub_linux, stub_win          lanzadores de la exportacion ligera (src/stub.c), tal cual
  full_linux, full_win          jugador completo
  lite_linux, lite_win          jugador ligero (sin JPG/MP3/FLAC/QOA, Date/Proxy/BigInt ni letras fuera de Latin-1)
Los jugadores van comprimidos con LZMA tras el filtro x86 (el lanzador y el editor los descomprimen con src/lzdec.h);
<nombre>_raw es su tamano real y <nombre>_props = lc + lp*9 + pb*45, mas 256 por el filtro x86.
Uso: gen_players.py <salida.c> <stub_linux> <full_linux> <lite_linux> <stub_win.exe> <full_win.exe> <lite_win.exe>
Los archivos que no existan quedan vacios (tamano 0): el editor no ofrece esa exportacion."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzpack import pack

out = sys.argv[1]
names = ['stub_linux', 'full_linux', 'lite_linux', 'stub_win', 'full_win', 'lite_win']
paths = dict(zip(names, sys.argv[2:8]))

def read(p):
    return open(p, 'rb').read() if os.path.exists(p) else b''

def lit(chunk):
    return '"' + ''.join(chr(b) if 32 <= b < 127 and chr(b) not in '\\"?' else '\\%03o' % b for b in chunk) + '"'

with open(out, 'w') as f:
    f.write('/* generado por tools/gen_players.py - no editar */\n')
    for name in names:
        data, player = read(paths[name]), not name.startswith('stub')
        z, props = pack(data) if player and data else (data, 0)
        f.write('const unsigned char %s[%d] =\n' % (name, max(len(z), 1)))
        for i in range(0, len(z), 64):
            f.write(lit(z[i:i + 64]) + '\n')
        if not z:
            f.write('""\n')
        f.write(';\nconst unsigned long %s_len = %d, %s_raw = %d, %s_props = %d;\n' % (name, len(z), name, len(data), name, props))
        print('%-11s %8d -> %8d' % (name, len(data), len(z)))
with open(os.path.join(os.path.dirname(out), 'players_bin.h'), 'w') as f:
    f.write('#pragma once\n/* generado por tools/gen_players.py */\n#ifdef __cplusplus\nextern "C" {\n#endif\n')
    for name in names:
        f.write('extern const unsigned char %s[];\nextern const unsigned long %s_len, %s_raw, %s_props;\n' % (name, name, name, name))
    f.write('#ifdef __cplusplus\n}\n#endif\n')
