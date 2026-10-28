#!/usr/bin/env python3
"""Compacta el JS del runtime y de gfx.js que va dentro de los jugadores: quita comentarios, sangria y espacios que
sobran, pero deja todos los saltos de linea (los errores siguen apuntando a la misma linea de src/*.js y no cambia
la insercion automatica de punto y coma). No toca el contenido de cadenas, plantillas ni expresiones regulares.
Uso como modulo: jsmin.thin(texto) -> texto"""
import sys

ID = set('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$\\')
# tras estas palabras una / empieza una expresion regular
RE_KW = {'return', 'typeof', 'case', 'in', 'of', 'new', 'delete', 'void', 'throw', 'else', 'do', 'instanceof', 'yield', 'await'}


def is_id(c):
    return c in ID or ord(c) > 127


def thin(src):
    out = []          # trozos de salida
    i, n = 0, len(src)
    pend_space = False
    last = ''         # ultimo caracter significativo emitido (para decidir espacios y si / es regex)
    last_word = ''
    braces = []       # pila: 't' = llave que abrio ${ de una plantilla, 'c' = llave de codigo

    def emit(s):
        nonlocal pend_space, last, last_word
        if pend_space and last and last != '\n':
            a, b = last, s[0]
            if (is_id(a) and is_id(b)) or (a in '+-' and b in '+-') or a == '/' or b == '/' or (a.isdigit() and b == '.'):
                out.append(' ')
        pend_space = False
        out.append(s)
        last = s[-1]

    def template(i):
        """copia una plantilla desde la ` de apertura; devuelve el indice tras la ` de cierre o tras ${"""
        j = i + 1
        while j < n:
            c = src[j]
            if c == '\\':
                j += 2
                continue
            if c == '`':
                return j + 1, False
            if c == '$' and j + 1 < n and src[j + 1] == '{':
                return j + 2, True
            j += 1
        raise ValueError('plantilla sin cerrar')

    while i < n:
        c = src[i]
        if c == '\n':
            out.append('\n')
            last = '\n'
            pend_space = False
            i += 1
            continue
        if c in ' \t\r':
            pend_space = True
            i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.index('*/', i + 2)
            nl = src.count('\n', i, j)
            if nl:
                out.append('\n' * nl)
                last = '\n'
                pend_space = False
            else:
                pend_space = True
            i = j + 2
            continue
        if c in '"\'':
            j = i + 1
            while src[j] != c:
                j += 2 if src[j] == '\\' else 1
            emit(src[i:j + 1])
            last_word = ''
            i = j + 1
            continue
        if c == '`':
            j, opened = template(i)
            emit(src[i:j])
            if opened:
                braces.append('t')
            last_word = ''
            i = j
            continue
        if c == '}' and braces and braces[-1] == 't':
            braces.pop()
            j, opened = template(i)      # sigue la plantilla: desde la } (el codigo de template() salta el primer caracter)
            emit(src[i:j])
            if opened:
                braces.append('t')
            i = j
            continue
        if c == '{':
            braces.append('c')
        elif c == '}' and braces:
            braces.pop()
        if c == '/':
            prev = last if last != '\n' else ''
            k = len(out) - 1
            while not prev and k >= 0:      # el ultimo caracter significativo antes del salto de linea
                s = out[k].rstrip('\n ')
                if s:
                    prev = s[-1]
                k -= 1
            regex = not prev or (not is_id(prev) and prev not in ')]}') or last_word in RE_KW
            if regex:
                j, cls = i + 1, False
                while True:
                    d = src[j]
                    if d == '\\':
                        j += 2
                        continue
                    if d == '[':
                        cls = True
                    elif d == ']':
                        cls = False
                    elif d == '/' and not cls:
                        break
                    elif d == '\n':
                        raise ValueError('regex sin cerrar en %d' % i)
                    j += 1
                j += 1
                while j < n and is_id(src[j]):
                    j += 1
                emit(src[i:j])
                last_word = ''
                i = j
                continue
        if is_id(c):
            j = i
            while j < n and is_id(src[j]):
                j += 1
            emit(src[i:j])
            last_word = src[i:j]
            i = j
            continue
        emit(c)
        last_word = ''
        i += 1
    return ''.join(out)


if __name__ == '__main__':
    s = open(sys.argv[1], encoding='utf-8').read()
    t = thin(s)
    assert t.count('\n') == s.count('\n')
    sys.stdout.write(t)
