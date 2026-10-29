#!/usr/bin/env python3
"""Crea Idiomas/Español.txt: todos los textos que se ven en el editor (de src/editor/*.cpp y de las condiciones y
acciones de src/editor/instructions.json), una linea por texto: "texto original = traduccion". Para traducir el
editor se copia con otro nombre (Ingles.txt, Portugues.txt...) y se cambia lo que va despues de " = "; el editor lo
muestra en Opciones > Idioma. Al volver a generar se conservan las traducciones que ya hay en los demas archivos.
Uso: tools/gen_idioma.py"""
import json, os, re

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
SRC = os.path.join(RAIZ, 'src', 'editor')

def esc(s):
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t')

def visible(s):
    """lo que se ve de una etiqueta de ImGui (antes de ## o ###); descarta ids, rutas, claves y formatos"""
    s = s.split('###')[0].split('##')[0]
    if len(s) < 2 or not re.search(r'[A-Za-zÁÉÍÓÚáéíóúñÑ]', s):
        return None
    if re.fullmatch(r'[a-z0-9_.\-/:]+', s):
        return None
    if re.fullmatch(r'[a-z][A-Za-z0-9]*', s) or re.fullmatch(r'[a-z]+_[A-Za-z0-9_]+', s):
        return None
    if re.search(r'\.(json|png|txt|h|cpp|js|zip|exe|wav|ogg)\b', s) or s.startswith(('#', 'http', '/', 'S2D')):
        return None
    if '%' in s and re.search(r'%[-0-9.]*[sdfux]', s) and len(re.sub(r'%[-0-9.]*[sdfux]', '', s).strip()) < 2:
        return None
    return s

textos = []
vistos = set()

def add(s):
    v = visible(s)
    if v and v not in vistos:
        vistos.add(v)
        textos.append(v)

lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
for f in sorted(os.listdir(SRC)):
    if not f.endswith(('.cpp', '.hpp')):
        continue
    for line in open(os.path.join(SRC, f), encoding='utf-8'):
        st = line.strip()
        if st.startswith(('#include', '//', '/*', '*')) or 'fprintf' in st or 'printf(' in st or 'jexpect' in st:
            continue
        for m in lit.finditer(line):
            raw = m.group(1)
            try:
                s = bytes(raw, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')
            except Exception:
                s = raw
            add(s)

d = json.load(open(os.path.join(SRC, 'instructions.json'), encoding='utf-8'))
for kind in ('cond', 'act'):
    for k, df in d[kind].items():
        add(df.get('label', ''))
        add(df.get('cat', ''))
        for p in df.get('params', []):
            add(p.get('l', ''))
            for o in p.get('options', []) or []:
                if isinstance(o, list) and len(o) > 1:
                    add(o[1])
    for k, s in d['sentences'][kind].items():
        add(s)
for name, lst in d.get('lists', {}).items():
    for o in lst:
        if isinstance(o, list) and len(o) > 1:
            add(o[1])
for c in d.get('gdCats', []):
    add(c)

carpeta = os.path.join(RAIZ, 'Idiomas')
os.makedirs(carpeta, exist_ok=True)
cab = ['# Source 2D - idioma: Español (el original)',
       '# Cada linea: texto original = traduccion. Para traducir el editor, copia este archivo con el nombre del idioma',
       '# (por ejemplo Ingles.txt), cambia solo lo que va despues de " = " y ponlo en esta carpeta Idiomas. Aparece en',
       '# Opciones > Idioma. Lo que esta entre llaves ({object}, {value}...) se deja igual: ahi va el dato.',
       '']
open(os.path.join(carpeta, 'Español.txt'), 'w', encoding='utf-8').write('\n'.join(cab + ['%s = %s' % (esc(t), esc(t)) for t in textos]) + '\n')
for f in os.listdir(carpeta):
    if not f.endswith('.txt') or f.startswith('Espa'):
        continue
    path = os.path.join(carpeta, f)
    have = {}
    lines = open(path, encoding='utf-8').read().splitlines()
    for l in lines:
        if ' = ' in l and not l.startswith('#'):
            have[l.split(' = ', 1)[0]] = True
    nuevos = ['%s = %s' % (esc(t), esc(t)) for t in textos if esc(t) not in have]
    if nuevos:
        open(path, 'a', encoding='utf-8').write('\n# textos nuevos (sin traducir todavia)\n' + '\n'.join(nuevos) + '\n')
print('Idiomas/Español.txt: %d textos' % len(textos))
