#!/usr/bin/env python3
"""Genera src/gfx_js.h desde src/gfx.js (la capa nativa del runtime: GPU, imagenes, entrada y fotogramas).
QuickJS necesita el texto terminado en cero: se anade un 0 que no cuenta en gfx_js_len.
Uso: gen_gfx.py src/gfx.js src/gfx_js.h [nombre]   (nombre del arreglo: gfx_js por defecto; gfx_web_js para la web)"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jsmin
src, out = sys.argv[1:3]
name = sys.argv[3] if len(sys.argv) > 3 else 'gfx_js'
data = jsmin.thin(open(src, encoding='utf-8').read()).encode('utf-8') + b'\0'   # compactado (tools/jsmin.py)
with open(out, 'w') as f:
    f.write('/* generado: no editar */\n#pragma once\n#include <stddef.h>\nstatic const unsigned char %s[] = {\n' % name)
    for i in range(0, len(data), 24):
        f.write('  ' + ','.join('0x%02x' % b for b in data[i:i + 24]) + ',\n')
    f.write('};\nstatic const size_t %s_len = %d;\n' % (name, len(data) - 1))
