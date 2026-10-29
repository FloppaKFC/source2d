#!/usr/bin/env python3
"""Proyecto de prueba de comportamientos e instrucciones de la fase 3.
Al fotograma 40 imprime "S2DTEST {...}" con console.log y detiene el juego.
Uso: make_behaviors_project.py <ejemplo.json> <salida.json>"""
import json, sys
src, out = sys.argv[1:3]
p = json.load(open(src))
objs = {o['name']: o for o in p['objects']}
M, E, J = objs['Moneda']['id'], objs['Enemigo']['id'], objs['Jugador']['id']
objs['Moneda']['behaviorList'] = [{'type': 'health', 'max': 50, 'start': ''}, {'type': 'flash'}, {'type': 'fireBullet', 'cooldown': 10},
                                  {'type': 'sine', 'axis': 'y', 'amplitude': 20, 'period': 1, 'phase': 0}]
objs['Enemigo']['behaviorList'] = [{'type': 'destroyOutside', 'margin': 0}]
for o in (objs['Enemigo'],):
    o['behaviors'] = dict(o.get('behaviors') or {}, gravity=False, collide=False, movement='none')
    o.pop('comportamientos', None)
sc = p['scenes'][0]
sc['tilemaps'] = []
def I(oid, x, y, iid): return dict(id=iid, objectId=oid, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
sc['instances'] = [I(M, 100, 100, 'm1'), I(J, 400, 100, 'j1'), I(J, 106, 100, 'j2')]
def C(t, params=None, **extra):
    d = dict(type=t, enabled=True, invert=False, params=params or {}); d.update(extra); return d
report = r'''var st = $.state, M = $.instOf('Moneda'), E = $.instOf('Enemigo'), J = $.instOf('Jugador');
var m = st.instances.filter(function (i) { return i.objId === M; })[0];
var js = st.instances.filter(function (i) { return i.objId === J; });
var got = { hp: m.hp, flashing: m.flashT > 0, timer: Math.round(m.timers.t1.t * 10) / 10 > 0, fired: st.globals.fired, enemies: st.instances.filter(function (i) { return i.objId === E; }).length,
  near: st.globals.near, sineMoved: Math.abs(m.y - 100) > 1 || Math.abs(m.sineOff || 0) > 1, angle: Math.round(js[0].angle), sep: Math.round(Math.abs(js[1].x - m.x)),
  hpExpr: st.globals.hpx };
function sprW(oid) { var o = $.project.objects.filter(function (x) { return x.id === oid; })[0]; var s = $.project.sprites.filter(function (x) { return x.id === o.spriteId; })[0]; return s ? s.width : 32; }
got.sep = Math.abs(js[1].x - m.x) >= (sprW(J) + sprW(M)) / 2 - 0.01;
var want = { hp: 15, flashing: true, timer: true, fired: 1, enemies: 0, near: 1, sineMoved: true, angle: 180, sep: true, hpExpr: 15 };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));'''
for _sc in p['scenes']:
    _sc['events'] = []
p['events'] = [
    dict(id='a', enabled=True, conditions=[C('atStart')], actions=[
        C('damage', dict(object=M, value=40)), C('heal', dict(object=M, value=5)), C('flash', dict(object=M, duration=5)),
        C('objTimerCtl', dict(object=M, name='t1', action='reset'))]),
    dict(id='b', enabled=True, conditions=[C('atStart')], actions=[C('fireAngle', dict(object=M, bullet=E, angle=0, speed=3000)), C('fireAngle', dict(object=M, bullet=E, angle=0, speed=3000))]),
    dict(id='b2', enabled=True, conditions=[C('objectExists', dict(object=E)), C('triggerOnce')], actions=[C('addGlobal', dict(variable='fired', value=1))]),
    dict(id='c', enabled=True, conditions=[C('distance', dict(object=J, **{'with': M}, op='lt', value='50')), C('triggerOnce')], actions=[C('addGlobal', dict(variable='near', value=1))]),
    dict(id='d', enabled=True, conditions=[C('objPos', dict(object=J, axis='x', op='gt', value='300'))], actions=[C('rotateToward', dict(object=J, x='0', y='100', speed='0'))]),
    dict(id='e', enabled=True, conditions=[C('objPos', dict(object=J, axis='x', op='lt', value='300'))], actions=[C('separate', dict(object=J, **{'with': M}))]),
    dict(id='f', enabled=True, conditions=[C('jsCode', dict(code='return $.state.frame >= 2;')), C('triggerOnce')], actions=[C('addGlobal', dict(variable='hpx', value='Moneda.Salud'))]),
    dict(id='z', enabled=True, conditions=[C('jsCode', dict(code='return $.state.frame >= 40;')), C('triggerOnce')], actions=[C('jsCode', dict(code=report)), C('quitGame')]),
]
p['globals'] = []
for o in p['objects']:
    if o['name'] == 'Jugador':
        o['behaviors'] = dict(o.get('behaviors') or {}, gravity=False, collide=False, movement='none'); o.pop('comportamientos', None)
    if o['name'] == 'Moneda':
        o['behaviors'] = dict(o.get('behaviors') or {}, gravity=False, collide=False, movement='none')
json.dump(p, open(out, 'w'))
