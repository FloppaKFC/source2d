#!/usr/bin/env python3
"""Proyecto de prueba de objetos y eventos por escena: dos escenas con un objeto Txt_Hola cada una (con distinta
variable v), eventos propios de cada escena y eventos comunes. Imprime "S2DTEST {...}".
Uso: make_scenes_project.py <salida.json>"""
import json, sys

def C(t, inv=False, **p):
    return dict(type=t, enabled=True, invert=inv, params=p)

def txt(oid, scene, v):
    return dict(id=oid, name='Txt_Hola', type='text', scene=scene, hud=True, text=dict(content='Hola', size=20, color='#ffffff', align='left'),
                behaviors={}, vars=[dict(name='v', value=v)], animations=[])

def inst(oid):
    return dict(id='i_' + oid, objectId=oid, x=10, y=10, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)

A, B = 'sc_a', 'sc_b'
report = r'''var g = $.state.globals, got = { a: g.a, b: g.b, onlyA: g.onlyA, onlyB: g.onlyB, comun: g.comun > 4, fnB: g.fnB };
var want = { a: 1, b: 2, onlyA: 3, onlyB: 3, comun: true, fnB: 7 };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));'''
p = dict(id='p', name='Escenas', version=3, settings=dict(width=320, height=180), globals=[], sprites=[], tilesets=[],
         objects=[txt('o_a', A, 1), txt('o_b', B, 2)],
         scenes=[dict(id=A, name='A', width=320, height=180, background='#000000', instances=[inst('o_a')], tilemaps=[],
                      events=[dict(id='a1', conditions=[C('atStart')], actions=[C('setVar', variable='a', op='set', value='Txt_Hola.v')]),
                              dict(id='a2', conditions=[], actions=[C('setVar', variable='onlyA', op='add', value='1')]),
                              dict(id='a3', conditions=[C('jsCode', code='return $.state.frame >= 3;')], actions=[C('changeScene', scene=B, transition='none')])]),
                 dict(id=B, name='B', width=320, height=180, background='#000000', instances=[inst('o_b')], tilemaps=[],
                      events=[dict(id='bf', type='function', name='Siete', params='', conditions=[], actions=[C('returnValue', value='7')]),
                              dict(id='b1', conditions=[C('atStart')], actions=[C('setVar', variable='b', op='set', value='Txt_Hola.v'), C('setVar', variable='fnB', op='set', value='Fn.Siete()')]),
                              dict(id='b2', conditions=[], actions=[C('setVar', variable='onlyB', op='add', value='1')]),
                              dict(id='b3', conditions=[C('jsCode', code='return $.state.frame >= 3;')], actions=[C('jsCode', code=report), C('quitGame')])])],
         events=[dict(id='c1', conditions=[], actions=[C('setVar', variable='comun', op='add', value='1')])])
json.dump(p, open(sys.argv[1], 'w'))
