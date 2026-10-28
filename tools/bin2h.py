#!/usr/bin/env python3
"""Convierte un archivo binario en una cabecera C con un arreglo de bytes.
Uso: bin2h.py <entrada> <nombre> <salida.h> [--opcional]
     bin2h.py <entrada> <nombre> <salida.c> --c [--opcional] [--deflate]
Con --c genera un .c aparte con el dato como literal de cadena (para binarios grandes: compilarlo
como arreglo de numeros en C++ gasta varios GB de memoria); las declaraciones extern van aparte."""
import sys
src, name, out = sys.argv[1:4]
optional = '--opcional' in sys.argv[4:]          # si el archivo no existe se genera un arreglo vacio (tamano 0)
as_c = '--c' in sys.argv[4:]
deflate = '--deflate' in sys.argv[4:]           # comprimido con deflate; ademas de <nombre>_len va <nombre>_raw (tamano original)
import os, zlib
data = open(src, 'rb').read() if not (optional and not os.path.exists(src)) else b''
raw_len = len(data)
if deflate and data:
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    data = c.compress(data) + c.flush()

def c_literal(chunk):
    s = []
    for b in chunk:
        c = chr(b)
        s.append(c if 32 <= b < 127 and c not in '\\"?' else '\\%03o' % b)
    return '"' + ''.join(s) + '"'

with open(out, 'w') as f:
    f.write('/* generado por tools/bin2h.py - no editar */\n')
    if as_c:
        f.write('const unsigned char %s[%d] =\n' % (name, max(len(data), 1)))
        for i in range(0, len(data), 64):
            f.write(c_literal(data[i:i + 64]) + '\n')
        if not data:
            f.write('""\n')
        f.write(';\nconst unsigned long %s_len = %d;\n' % (name, len(data)))
        if deflate:
            f.write('const unsigned long %s_raw = %d;\n' % (name, raw_len))
    else:
        f.write('#pragma once\n')
        f.write('static const unsigned char %s[] = {\n' % name)
        for i in range(0, len(data), 24):
            f.write(','.join(str(b) for b in data[i:i + 24]) + ',\n')
        if not data:
            f.write('0\n')
        f.write('};\nstatic const unsigned long %s_len = %d;\n' % (name, len(data)))
print(out, len(data), 'bytes')
