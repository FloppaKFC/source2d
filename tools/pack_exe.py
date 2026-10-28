#!/usr/bin/env python3
"""Comprime un ejecutable (el editor de la Release) con el lanzador de la exportacion ligera (src/stub.c):
  [lanzador][programa en LZMA + filtro x86][proyecto vacio][cola de 56 bytes, "S2DMINI3"]
Al abrirse, el lanzador lo descomprime en memoria (Linux) o en un archivo temporal que reutiliza (Windows), deja su
carpeta real en S2D_EXE_DIR y lo ejecuta con los mismos argumentos. Comprueba que se descomprime igual.
Uso: pack_exe.py <lanzador> <programa> <salida>"""
import os
import struct
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lzpack import pack

stub_p, prog_p, out_p = sys.argv[1:4]
stub, prog = open(stub_p, 'rb').read(), open(prog_p, 'rb').read()
z, props = pack(prog)
# cola: motor comprimido | motor | proyecto comprimido | proyecto entero | props | datos tal cual | marca
tail = struct.pack('<6Q', len(z), len(prog), 0, 0, props, 0) + b'S2DMINI3'
with open(out_p, 'wb') as f:
    f.write(stub + z + tail)
os.chmod(out_p, 0o755)
print('%s: %.1f MB -> %.1f MB' % (out_p, len(prog) / 1048576, (len(stub) + len(z) + len(tail)) / 1048576))
