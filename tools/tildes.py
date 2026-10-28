#!/usr/bin/env python3
# Pasada de tildes sobre los textos visibles del editor nativo (heredados del HTML sin acentos).
# Solo toca literales de cadena de src/editor/*.cpp|hpp, los campos de texto de instructions.json
# y las frases esperadas (lineas "ev.sentence") de test/editor_*.txt. No toca codigo GLSL/JS,
# claves JSON ni valores que el runtime compara.
# Uso: tools/tildes.py [--dry]
import glob, json, re, sys

WORDS = '''aberracion aberración|accion acción|acciones|alineacion alineación|anade añade|anaden añaden|anadir añadir
analogico analógico|angulo ángulo|animacion animación|area área|automatica automática|automatico automático
boton botón|camara cámara|cesped césped|codigo código|combinacion combinación|condicion condición
cromatica cromática|cuadricula cuadrícula|curacion curación|dano daño|despues después|devolvio devolvió
dialogos diálogos|direccion dirección|duracion duración|eliminara eliminará|dejaran dejarán|exportacion exportación
expresion expresión|extension extensión|fisica física|funcion función|guardalo guárdalo|instruccion instrucción
lapiz lápiz|limites límites|linea línea|lineas líneas|maquina máquina|mas más|maxima máxima|mayus mayús
movil móvil|musica música|ningun ningún|numero número|numeros números|parametros parámetros|parentesis paréntesis
pequeno pequeño|pixel píxel|pixeles píxeles|posicion posición|precision precisión|proporcion proporción
puntuacion puntuación|quedo quedó|raton ratón|rectangulo rectángulo|resolucion resolución|rotacion rotación
seleccion selección|simetria simetría|solida sólida|solido sólido|solidos sólidos|subcondicion subcondición
tamano tamaño|tambien también|titulo título|todavia todavía|transicion transición|ultimo último|usalo úsalo
vacia vacía|vacio vacío|valido válido|vineta viñeta|version versión|opcion opción|informacion información'''
MAP = {}
for item in re.split(r'[|\n]', WORDS):
    p = item.split()
    if len(p) == 2: MAP[p[0]] = p[1]
# "esta" es verbo salvo delante de estas palabras
ESTA_DEMOSTRATIVO = ('escena', 'instancia', 'instruccion', 'instrucción', 'vez', 'capa', 'variable', 'ventana')
CODE = re.compile(r'uniform|void main|gl_|return \{|function|#include|mediump|varying')
# literales que son datos y no texto visible: claves JSON, nombres por defecto de objetos (se usan en
# expresiones como Boton1.X), textos que dibuja el juego y la salida de las pruebas
SKIP = {'<extension>', 'Pixel perfect', 'Boton '}
SKIP_RE = re.compile(r'^[a-z]+$|^S2DEDIT')
PHRASES = {'La exportacion fallo': 'La exportación falló'}

def fix_word(w):
    low = w.lower()
    if low not in MAP: return w
    r = MAP[low]
    if w.isupper() and len(w) > 1: return r.upper()
    if w[0].isupper(): return r[0].upper() + r[1:]
    return r

def fix_text(s):
    if CODE.search(s) or s in SKIP or SKIP_RE.search(s): return s
    if s in PHRASES: return PHRASES[s]
    s = re.sub(r'[A-Za-zÁÉÍÓÚáéíóúÑñ]+', lambda m: fix_word(m.group(0)), s)
    s = s.replace('Píxel perfect', 'Pixel perfect')
    def esta(m):
        if m.group(3).lower() in ESTA_DEMOSTRATIVO: return m.group(0)
        return ('Está' if m.group(1)[0] == 'E' else 'está') + m.group(2)
    return re.sub(r'\b([Ee]sta)(\s+([A-Za-záéíóúñ]+))', esta, s)

dry = '--dry' in sys.argv
changed = 0
warn = []

# literales C++
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
for f in sorted(glob.glob('src/editor/*.cpp') + glob.glob('src/editor/*.hpp')):
    src = open(f, encoding='utf-8').read()
    out, last = [], 0
    for m in LIT.finditer(src):
        new = fix_text(m.group(1))
        if re.search(r'"text",\s*$', src[max(0, m.start() - 12):m.start()]): new = m.group(1)
        if new != m.group(1):
            line = src.count('\n', 0, m.start()) + 1
            ctx = src[max(0, m.start() - 40):m.end() + 40]
            if re.search(r'[=!]=\s*"' + re.escape(m.group(1)) + '"|"' + re.escape(m.group(1)) + r'"\s*[=!]=|find\(\s*"' + re.escape(m.group(1)), ctx):
                warn.append(f'{f}:{line} comparacion: "{m.group(1)}"')
            if dry: print(f'{f}:{line}: "{m.group(1)}" -> "{new}"')
            changed += 1
        out.append(src[last:m.start()]); out.append('"' + new + '"'); last = m.end()
    out.append(src[last:])
    if not dry: open(f, 'w', encoding='utf-8').write(''.join(out))

# instructions.json: solo campos de texto
p = 'src/editor/instructions.json'
d = json.load(open(p, encoding='utf-8'))
def fx(s):
    global changed
    n = fix_text(s)
    if n != s:
        changed += 1
        if dry: print(f'{p}: "{s}" -> "{n}"')
    return n
for kind in ('cond', 'act'):
    for df in d[kind].values():
        for k in ('label', 'cat', 'gd'):
            if k in df: df[k] = fx(df[k])
        for prm in df.get('params', []):
            if 'l' in prm: prm['l'] = fx(prm['l'])
            for opt in prm.get('options', []):
                if isinstance(opt, list) and len(opt) > 1: opt[1] = fx(opt[1])
for name, lst in d['lists'].items():
    for opt in lst:
        if isinstance(opt, list) and len(opt) > 1: opt[1] = fx(opt[1])
for kind in d['sentences']:
    for k in d['sentences'][kind]: d['sentences'][kind][k] = fx(d['sentences'][kind][k])
for k in d['paramNames']: d['paramNames'][k] = fx(d['paramNames'][k])
d['gdCats'] = [fx(c) for c in d['gdCats']]
d['oldCatGd'] = {fx(k): fx(v) for k, v in d['oldCatGd'].items()}
if not dry:
    open(p, 'w', encoding='utf-8').write(json.dumps(d, ensure_ascii=False, indent=1) + '\n')

# frases esperadas en los guiones de prueba
for f in sorted(glob.glob('test/editor_*.txt')):
    lines = open(f, encoding='utf-8').read().split('\n')
    for i, l in enumerate(lines):
        if l.startswith('ev.sentence '):
            parts = l.split(' ', 3)
            if len(parts) == 4:
                n = ' '.join(parts[:3]) + ' ' + fix_text(parts[3])
                if n != l:
                    changed += 1
                    if dry: print(f'{f}:{i + 1}: {l} -> {n}')
                    lines[i] = n
    if not dry: open(f, 'w', encoding='utf-8').write('\n'.join(lines))

print(f'{changed} cambio(s)')
for w in warn: print('AVISO', w)
