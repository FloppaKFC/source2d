#!/usr/bin/env python3
"""Colisiones con forma (pestana Animaciones > Colision): una caja que no esta centrada (agachado: la mitad de abajo)
se apoya bien en el suelo, dos circulos que no se tocan aunque sus cajas si, y un personaje que sube andando una rampa
de tiles con forma de puntos. Imprime "S2DTEST {...}".
Uso: make_collision_project.py <salida.json>"""
import json, sys

T = 16
full = ['#808080'] * (T * T)
ts = dict(id='ts', name='t', tileSize=T, columns=2, rows=1,
          tiles=[dict(pixels=full, solid=True),
                 dict(pixels=full, solid=True, collision=dict(shape='poly', points=[[0, 16], [16, 0], [16, 16]]))])   # rampa /


def spr(sid, w, h):
    return dict(id=sid, name=sid, width=w, height=h, frames=[dict(pixels=['#ffffff'] * (w * h))])


def obj(oid, sid, coll=None, bh=None, anims=None):
    o = dict(id=oid, name=oid, type='sprite', spriteId=sid, behaviors=bh or {}, vars=[], animations=anims or [])
    if coll: o['collision'] = coll
    return o


def inst(iid, oid, x, y):
    return dict(id=iid, objectId=oid, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)


phys = dict(gravity=True, collide=True, friction=1, airFriction=1)
COLS, ROWS = 40, 12
data = [-1] * (COLS * ROWS)
for c in range(COLS): data[(ROWS - 1) * COLS + c] = 0            # suelo en la fila de abajo (y 176..192)
for c in range(20, 24): data[(ROWS - 2) * COLS + c] = 0          # a partir de la rampa, un escalon de un tile
data[(ROWS - 2) * COLS + 19] = 1                                  # la rampa sube hasta el
agachado = dict(id='a1', name='Agachado', spriteId='s_hero', fps=8, loop=True, collision=dict(shape='rect', x=0, y=16, w=16, h=16))
objects = [obj('Heroe', 's_hero', bh=phys, anims=[agachado]),
           obj('Bola', 's_ball', coll=dict(shape='circle', x=0, y=0, w=32, h=32)),
           obj('Andador', 's_hero', bh=phys)]
report = r'''var h = $.listOf('Heroe')[0], w = $.listOf('Andador')[0], g = $.state.globals;
var got = { heroeY: Math.round(h.y), circulosLejos: g.lejos, circulosCerca: g.cerca, andadorMinY: Math.round(g.minY), andadorX: Math.round(w.x) };
var fails = [];
if (got.heroeY !== 160) fails.push('heroeY');           /* suelo en 176: el dibujo (32 de alto) va de 144 a 176, centro 160 */
if (got.circulosLejos !== 0) fails.push('lejos');
if (got.circulosCerca !== 1) fails.push('cerca');
if (!(got.andadorMinY <= 145 && got.andadorX > 384)) fails.push('rampa');   /* subio la rampa hasta el escalon (centro a 144) y siguio */
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));'''
def C(t, inv=False, **p): return dict(type=t, enabled=True, invert=inv, params=p)
events = [
    dict(id='e0', conditions=[C('atStart')], actions=[C('setVar', variable='lejos', op='set', value='0'), C('setVar', variable='cerca', op='set', value='0')]),
    dict(id='e1', conditions=[C('jsCode', code='var L=$.listOf("Bola"),a=L[0],b=L[1],c=L[2];'
                                            '$.state.globals.lejos=$.collide(a,b)?1:0;$.state.globals.cerca=$.collide(a,c)?1:0;return false;')], actions=[]),
    dict(id='e2', conditions=[], actions=[C('jsCode', code='var a=$.listOf("Andador")[0];a.vx=70;var g=$.state.globals;if($.state.frame>30)g.minY=Math.min(g.minY==null?1e9:g.minY,a.y);')]),
    dict(id='e3', conditions=[C('jsCode', code='return $.state.frame >= 300;')], actions=[C('jsCode', code=report), C('quitGame')])]
doc = dict(id='p', name='Colisiones', version=3, settings=dict(width=640, height=192), globals=[], sprites=[spr('s_hero', 16, 32), spr('s_ball', 32, 32)],
           tilesets=[ts], objects=objects,
           scenes=[dict(id='sc', name='A', width=COLS * T, height=ROWS * T, background='#000000',
                        instances=[inst('i_heroe', 'Heroe', 40, 40), inst('i_and', 'Andador', 250, 150),
                                   inst('i_b1', 'Bola', 500, 40), inst('i_b2', 'Bola', 528, 68), inst('i_b3', 'Bola', 520, 40)],
                        tilemaps=[dict(id='tm', name='suelo', tilesetId='ts', layer='base', x=0, y=0, cols=COLS, rows=ROWS, data=data)], events=events)],
           events=[])
json.dump(doc, open(sys.argv[1], 'w'))
