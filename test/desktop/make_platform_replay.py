#!/usr/bin/env python3
"""Prueba de Platform Test: reproduce en el jugador real (con --fixed-step) la ruta de un nivel que encontro
tools/solve_platform.py y comprueba que el jugador llega a la meta sin morir (con prueba = 1, sin dialogos). Si la
fisica del motor y la del simulador no coinciden, la ruta falla. Imprime "S2DTEST {...}".
Uso: make_platform_replay.py <examples_platform_test.source2d.json> <rutas.json> <nivel> <salida.json>"""
import json, sys

d = json.load(open(sys.argv[1]))
routes = json.load(open(sys.argv[2]))
lv = sys.argv[3]
inputs = routes[lv]
sid = 'sc_nivel' + lv
d['scenes'] = [s for s in d['scenes'] if s['id'] == sid] + [s for s in d['scenes'] if s['id'] != sid]
for g in d['globals']:
    if g['name'] == 'prueba':
        g['value'] = 1
sc = d['scenes'][0]
C = lambda t, **p: dict(type=t, enabled=True, invert=False, params=p)
feed = '''var IN = %s;
var a = IN[$.state.frame], pl = null;
$.state.instances.forEach(function (i) { if (i.objId === 'o_jugador') pl = i; });
if (pl && a) pl.simNext = { left: !!a[0], right: !!a[1], jump: !!a[2], jumpHeld: !!a[3], dash: !!a[4] };''' % json.dumps(inputs, separators=(',', ':'))
report = '''var g = $.state.globals, pl = null;
$.state.instances.forEach(function (i) { if (i.objId === 'o_jugador') pl = i; });
var got = { nivel: %s, meta: g.ganado == 1, muertes: g.muertes, frame: $.state.frame };
var ok = got.meta && got.muertes == 0;
console.log('S2DTEST ' + JSON.stringify({ ok: ok, fails: ok ? [] : ['meta'], got: got, x: pl && Math.round(pl.x), y: pl && Math.round(pl.y) }));''' % lv
sc['events'].insert(0, dict(id='t_feed', enabled=True, level=0, conditions=[], actions=[C('jsCode', code=feed)]))
sc['events'].append(dict(id='t_fin', enabled=True, level=0,
                         conditions=[C('jsCode', code='return $.state.globals.ganado == 1 || $.state.globals.muertes > 0 || $.state.frame > %d;' % (len(inputs) + 120))],
                         actions=[C('jsCode', code=report), C('quitGame')]))
json.dump(d, open(sys.argv[4], 'w'))
