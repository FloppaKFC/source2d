#!/usr/bin/env python3
"""Compila los shaders del renderizador Vulkan (tools/vk/*.vert|frag) a SPIR-V con glslc y los deja en
src/vk_shaders.h (se guarda en el codigo: para compilar el motor no hace falta glslc).
Uso: tools/gen_vk_shaders.py"""
import os, subprocess, sys, tempfile
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'vk')
out = ['/* generado por tools/gen_vk_shaders.py a partir de tools/vk: no editar a mano */']
for f in sorted(os.listdir(here)):
    if not f.endswith(('.vert', '.frag')):
        continue
    spv = tempfile.mktemp(suffix='.spv')
    subprocess.check_call(['glslc', '-O', os.path.join(here, f), '-o', spv])
    data = open(spv, 'rb').read()
    os.remove(spv)
    name = 'spv_' + f.replace('.', '_')
    words = [int.from_bytes(data[i:i + 4], 'little') for i in range(0, len(data), 4)]
    out.append('static const uint32_t %s[] = {' % name)
    for i in range(0, len(words), 8):
        out.append('    ' + ', '.join('0x%08x' % w for w in words[i:i + 8]) + ',')
    out.append('};')
open(os.path.join(here, '..', '..', 'src', 'vk_shaders.h'), 'w').write('\n'.join(out) + '\n')
print('src/vk_shaders.h listo')
