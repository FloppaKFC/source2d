#!/usr/bin/env python3
"""Genera src/runtime_js.h con el runtime JS del juego.
Uso: gen_runtime.py <runtime.js | editor.html> <salida.h>
La fuente es src/runtime.js, compactada con tools/jsmin.py (el bloque runtime-code del editor HTML antiguo tambien se acepta).
Lo usa el jugador (source2d-player) para abrir proyectos .source2d.json."""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jsmin
html, out = sys.argv[1:3]
src = open(html, encoding='utf-8').read()
if html.endswith('.js'):
    runtime = jsmin.thin(src).encode('utf-8')   # sin comentarios ni sangria; mismas lineas
else:
    m = re.search(r'<script id="runtime-code" type="text/plain">(.*?)</script>', src, re.S)
    if not m:
        sys.exit('no encuentro el bloque runtime-code en ' + html)
    runtime = m.group(1).replace('window.PixelForge', 'window.Source2D').encode('utf-8')
# QuickJS necesita el texto terminado en cero (JS_Eval lee hasta el): se anade un 0 que no cuenta en runtime_js_len
data = runtime + b'\0'
lines = []
for i in range(0, len(data), 20):
    lines.append(','.join(str(b) for b in data[i:i + 20]))
with open(out, 'w') as f:
    f.write('/* generado por tools/gen_runtime.py - no editar */\n')
    f.write('static const unsigned char runtime_js[] = {\n' + ',\n'.join(lines) + '\n};\n')
    f.write('static const unsigned long runtime_js_len = %d;\n' % len(runtime))
print('runtime_js.h', len(runtime), 'bytes')
