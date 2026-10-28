#!/usr/bin/env python3
"""Proyecto de prueba del lenguaje de eventos: funciones con parametros y recursion, variables locales,
arrays con indice, Repetir con contador, salir del bucle, devolver valor, plantillas {= ...} y operadores
de bits. Al tercer fotograma imprime "S2DTEST {...}" (con el tiempo de fib(16) en ms) y detiene el juego.
Uso: make_lang_project.py <ejemplo.json> <salida.json>"""
import json, sys
src, out = sys.argv[1:3]
p = json.load(open(src))
sc = p['scenes'][0]
sc['instances'] = [i for i in sc['instances'] if i['objectId'] == p['objects'][0]['id']][:1]
sc['tilemaps'] = []
P0 = p['objects'][0]['id']


def C(t, **params):
    return dict(type=t, enabled=True, invert=False, params=params)


def E(conds=(), acts=(), level=0, **extra):
    d = dict(id='e%d' % len(evs), enabled=True, conditions=list(conds), actions=list(acts), level=level)
    d.update(extra)
    evs.append(d)
    return d


evs = []
# fib(n) recursiva
E(level=0, type='function', name='fib', params='n')
E([C('cmpNum', a='$n', op='lt', b='2')], [C('returnValue', value='$n')], level=1)
E([], [C('returnValue', value='Fn.fib($n - 1) + Fn.fib($n - 2)')], level=1)
# suma(a, b) con variable local y llamada por accion
E(level=0, type='function', name='suma', params='a, b')
E([], [C('localVar', variable='t', value='$a + $b'), C('returnValue', value='$t')], level=1)
# primer indice de v en el array A (o -1): Repetir con contador local y salir antes de terminar
E(level=0, type='function', name='buscar', params='v')
E([], [C('localVar', variable='r', value='-1')], level=1)
E([C('cmpNum', a='$A[$i]', op='eq', b='$v')], [C('setVar', variable='r', op='set', value='$i'), C('breakLoop')], level=1, type='repeat', count='len($A)', var='i')
E([], [C('returnValue', value='$r')], level=1)

E([C('atStart')], [
    C('arrayFill', variable='A', size='8', value='0'),
    C('setVar', variable='A[3]', op='set', value='7'),
    C('setVar', variable='A[5]', op='set', value='7'),
    C('setVar', variable='A[$A[3] - 6]', op='add', value='2'),       # A[1] += 2
    C('arrayPushNum', variable='A', value='0x88 & 0x0f | 3'),       # 11
    C('callFunction', name='suma', args='20, 22', result='s'),
    C('callFunction', name='buscar', args='7', result='k'),
    C('setVar', variable='cnt', op='set', value='0'),
    C('setVarText', variable='txt', text='a={g:s} b={= $A[1] * 10 + 1} c={= substr("abcdefgh", $A[3], 1) + (8 - 2)}'),
    C('jsCode', code='$.vars.__t0 = Date.now();'),
    C('callFunction', name='fib', args='16', result='f'),
    C('jsCode', code='$.vars.__ms = Date.now() - $.vars.__t0;'),
])
# un Repetir anidado: el contador global se cuenta 3*4 veces y "salir" corta solo el bucle interior
E([C('atStart')], [], type='repeat', count='3', var='x')
E([], [], level=1, type='repeat', count='10', var='y')
E([C('cmpNum', a='$y', op='ge', b='4')], [C('breakLoop')], level=2)
E([], [C('setVar', variable='cnt', op='add', value='1')], level=2)
report = r'''var g = $.vars;
var got = { s: g.s, k: g.k, a1: g.A[1], a8: g.A[8], len: g.A.length, cnt: g.cnt, txt: g.txt, f: g.f };
var want = { s: 42, k: 3, a1: 2, a8: 11, len: 9, cnt: 12, txt: 'a=42 b=21 c=h6', f: 987 };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got, fibMs: g.__ms }));'''
E([C('jsCode', code='return $.state.frame >= 3;'), C('triggerOnce')], [C('jsCode', code=report), C('quitGame')])
for _sc in p['scenes']:
    _sc['events'] = []          # solo los eventos de la prueba
p['events'] = evs
p['globals'] = []
json.dump(p, open(out, 'w'))
