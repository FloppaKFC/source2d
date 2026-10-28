#!/usr/bin/env python3
"""Genera un proyecto de prueba del runtime de eventos (seleccion, subeventos, O, bucles, una vez).
Al fotograma 30 imprime un informe JSON con console.log y detiene el juego.
Uso: make_events_project.py <ejemplo.json> <salida.json>"""
import json, sys
src, out = sys.argv[1:3]
p = json.load(open(src))
O = [o for o in p['objects'] if o['name'] == 'Moneda'][0]['id']
sc = p['scenes'][0]
sc['instances'] = [dict(id='ti%d' % i, objectId=O, x=x, y=100, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
                   for i, x in enumerate([10, 20, 30])]
sc['tilemaps'] = []
def C(t, params=None, **extra):
    d = dict(type=t, enabled=True, invert=False, params=params or {}); d.update(extra); return d
pos = lambda op, v: C('objPos', dict(object=O, axis='x', op=op, value=v))
report = r'''var st = $.state;
function who(v) { return st.instances.filter(function (i) { return i.vars && String(i.vars[v]) === '1'; }).map(function (i) { return i.x; }).join(','); }
var got = { hit: who('hit'), mid: who('mid'), inv: who('inv'), or: who('or'), cnt: st.globals.cnt, rep: st.globals.rep, w: st.globals.w, once: st.globals.once, fe2: st.globals.fe2 };
var want = { hit: '20,30', mid: '20', inv: '10', or: '10,30', cnt: 3, rep: 5, w: 7, once: 1, fe2: 2 };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));'''
for _sc in p['scenes']:
    _sc['events'] = []          # solo los eventos de la prueba
p['events'] = [
    dict(id='e1', enabled=True, conditions=[pos('gt', '15')], actions=[]),
    dict(id='e1s', enabled=True, level=1, conditions=[], actions=[C('setObjVar', dict(object=O, variable='hit', value='1'))]),
    dict(id='e2', enabled=True, conditions=[pos('gt', '15'), pos('lt', '25')], actions=[C('setObjVar', dict(object=O, variable='mid', value='1'))]),
    dict(id='e3', enabled=True, conditions=[dict(pos('gt', '15'), invert=True)], actions=[C('setObjVar', dict(object=O, variable='inv', value='1'))]),
    dict(id='e4', enabled=True, conditions=[C('or', sub=[pos('lt', '15'), pos('gt', '25')])], actions=[C('setObjVar', dict(object=O, variable='or', value='1'))]),
    dict(id='e5', enabled=True, type='forEach', object=O, conditions=[C('atStart')], actions=[C('addGlobal', dict(variable='cnt', value=1))]),
    dict(id='e6', enabled=True, type='repeat', count='5', conditions=[C('atStart')], actions=[C('addGlobal', dict(variable='rep', value=1))]),
    dict(id='e7', enabled=True, type='while', whileConds=[C('globalVar', dict(variable='w', op='lt', value='7'))], conditions=[], actions=[C('addGlobal', dict(variable='w', value=1))]),
    dict(id='e8', enabled=True, conditions=[C('always'), C('triggerOnce')], actions=[C('addGlobal', dict(variable='once', value=1))]),
    dict(id='e9', enabled=True, type='forEach', object=O, conditions=[pos('gt', '15'), C('atStart')], actions=[C('addGlobal', dict(variable='fe2', value=1))]),
    dict(id='eR', enabled=True, conditions=[C('jsCode', dict(code='return $.state.frame >= 30;')), C('triggerOnce')],
         actions=[C('jsCode', dict(code=report)), C('quitGame')]),
]
p['globals'] = [dict(name='w', type='normal', value=0)]
json.dump(p, open(out, 'w'))
