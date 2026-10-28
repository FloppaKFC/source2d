#!/usr/bin/env python3
"""Prueba del juego de prueba Maxwell (giro 2D con eventos): tras 60 fotogramas comprueba que el angulo avanza a
68,75 grados/s con TiempoDelta(), que el frame de la animacion es el del angulo y que la flecha arriba acerca al gato
(escala mayor que 1). Imprime "S2DTEST {...}".
Uso: make_maxwell_test.py <examples_maxwell.source2d.json> <salida.json>"""
import json, sys

d = json.load(open(sys.argv[1]))
sc = d['scenes'][0]
report = r'''var m = null, g = $.state.globals; $.state.instances.forEach(function (i) { if (i.objId === 'o_maxwell') m = i; });
var got = { angulo: g.angulo > 55 && g.angulo < 80, frame: m.frame === Math.floor(g.angulo / 10) % 36, zoom: m.scaleX > 1.05 };
var fails = Object.keys(got).filter(function (k) { return !got[k]; });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, angulo: g.angulo, frame: m.frame, escala: m.scaleX }));'''
C = lambda t, **p: dict(type=t, enabled=True, invert=False, params=p)
sc['events'].insert(0, dict(id='t_up', enabled=True, level=0, conditions=[C('jsCode', code='return $.state.frame >= 20 && $.state.frame < 30;')],
                            actions=[C('jsCode', code="$.state.keys['arrowup'] = true;")]))
sc['events'].insert(1, dict(id='t_up2', enabled=True, level=0, conditions=[C('jsCode', code='return $.state.frame === 30;')],
                            actions=[C('jsCode', code="$.state.keys['arrowup'] = false;")]))
sc['events'].append(dict(id='t_fin', enabled=True, level=0, conditions=[C('jsCode', code='return $.state.frame >= 60;')],
                         actions=[C('jsCode', code=report), C('quitGame')]))
json.dump(d, open(sys.argv[2], 'w'))
