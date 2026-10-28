#!/usr/bin/env python3
"""Reune las licencias de las bibliotecas y fuentes de terceros.
   tools/gen_licencias.py texto  LICENCIAS-TERCEROS.txt      todas (editor), para la carpeta Release
   tools/gen_licencias.py editor build/gen/licencias_editor.h   todas, como cadena C (Ayuda > Licencias)
   tools/gen_licencias.py jugador src/licencias_jugador.h       solo lo que va dentro de los juegos exportados
                                                                (./juego --licencias las muestra)"""
import os
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')

# (nombre, uso, archivo de licencia o texto, va en los juegos exportados)
PARTES = [
    ('raylib', 'ventanas, graficos y audio (licencia zlib)', 'third_party/raylib/LICENSE', True),
    ('GLFW', 'ventanas y entrada, dentro de raylib (licencia zlib)', 'third_party/raylib/src/external/glfw/LICENSE.md', True),
    ('QuickJS', 'motor de JavaScript (licencia MIT)', 'third_party/quickjs/LICENSE', True),
    ('Vulkan Headers', 'definiciones de Vulkan para el dibujo experimental (licencia MIT)', 'third_party/vulkan/LICENSE.txt', True),
    ('DejaVu Sans', 'fuente del texto de los juegos (licencia de Bitstream Vera; los cambios de DejaVu son de dominio publico)',
     'third_party/dejavu/LICENSE.txt', True),
    ('stb_image, stb_truetype, stb_vorbis, miniaudio, dr_wav, dr_mp3, dr_flac, qoi',
     'imagenes, letras y audio, dentro de raylib', None, True),
    ('Dear ImGui', 'interfaz del editor (licencia MIT)', 'third_party/imgui/LICENSE.txt', False),
    ('rlImGui', 'union de Dear ImGui con raylib (licencia zlib)', 'third_party/rlImGui/LICENSE', False),
    ('JSON for Modern C++ (nlohmann/json 3.11.3)', 'proyectos del editor (licencia MIT)', None, False),
    ('Outfit', 'fuente de la interfaz del editor (SIL Open Font License 1.1)', 'third_party/outfit/OFL.txt', False),
]

TEXTO_LIBRES = """Cada una se puede usar, a eleccion, como dominio publico o con licencia MIT
(miniaudio: dominio publico o MIT-0). Las licencias completas estan al final de cada
archivo en third_party/raylib/src/external/."""

MIT_JSON = """MIT License

Copyright (c) 2013-2023 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE."""


def reunir(solo_jugador):
    out = ['Source 2D usa estas bibliotecas y fuentes de terceros. Gracias a sus autores.\n']
    for nombre, uso, archivo, en_jugador in PARTES:
        if solo_jugador and not en_jugador:
            continue
        out.append('=' * 78)
        out.append('%s - %s' % (nombre, uso))
        out.append('=' * 78)
        if archivo:
            with open(os.path.join(RAIZ, archivo), encoding='utf-8') as f:
                out.append(f.read().strip())
        elif nombre.startswith('JSON'):
            out.append(MIT_JSON)
        else:
            out.append(TEXTO_LIBRES)
        out.append('')
    return '\n'.join(out) + '\n'


def cadena_c(nombre, texto):
    lineas = ['/* generado por tools/gen_licencias.py: no editar */', '#pragma once',
              'static const char %s[] =' % nombre]
    for l in texto.split('\n'):
        l = l.replace('\\', '\\\\').replace('"', '\\"')
        lineas.append('  "%s\\n"' % l)
    lineas[-1] += ';'
    return '\n'.join(lineas) + '\n'


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('texto', 'editor', 'jugador'):
        sys.exit(__doc__)
    modo, salida = sys.argv[1], sys.argv[2]
    texto = reunir(modo == 'jugador')
    if modo == 'texto':
        res = texto
    else:
        res = cadena_c('licencias_' + modo, texto)
    with open(salida, 'w', encoding='utf-8', newline='\n') as f:
        f.write(res)


if __name__ == '__main__':
    main()
