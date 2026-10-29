#!/usr/bin/env python3
"""Prueba del plataformero ampliado: doble salto, dash, condicion "Salto, doble salto, dash o aterrizaje", tiles
peligrosos, mapas de tiles en capas no solidas, caja de colision fija (no cambia con la escala) y particulas.
El jugador se maneja con simNext desde eventos (como tools/solve_platform.py). Imprime "S2DTEST {...}".
Uso: make_platformer_project.py <salida.json>"""
import json, sys

T = 32
COLS, ROWS = 40, 12

def C(t, inv=False, **p):
    return dict(type=t, enabled=True, invert=inv, params=p)

def ev(conds, acts):
    return dict(id='e%d' % len(events), enabled=True, level=0, conditions=conds, actions=acts)

solid_px = ['#556677'] * (T * T)
spike_px = ['#ffffff'] * (T * T)
ts = dict(id='ts', name='t', tileSize=T, columns=2, rows=1, tiles=[dict(pixels=solid_px, solid=True), dict(pixels=spike_px, solid=False, danger=True)])
ground = [-1] * (COLS * ROWS)
for c in range(COLS):
    ground[(ROWS - 1) * COLS + c] = 0
ground[(ROWS - 2) * COLS + 30] = 1
deco = [-1] * (COLS * ROWS)
for r in range(ROWS - 4, ROWS - 1):
    deco[r * COLS + 12] = 0
px = ['#ffffff'] * (32 * 32)
spr = dict(id='s_p', name='p', width=32, height=32, frames=[dict(pixels=px)])
player = dict(id='o_p', name='Jugador', type='sprite', spriteId='s_p', hitbox=dict(w=18, h=26), vars=[], animations=[],
              behaviors=dict(gravity=True, solid=False, oneway=False, collide=True, movement='platform', speed=250, jump=620, friction=0.85,
                             airFriction=0.99, bounce=0, mass=1, spin=0),
              comportamientos=dict(plataformero=dict(airJumps=1, airJump=560, dash=True, dashSpeed=720, dashTime=0.16, dashCooldown=0.3,
                                                     jumpCut=True, coyote=0.08, jumpBuffer=0.1, maxFall=820)))
GY = (ROWS - 1) * T - 13
script = r'''var f = $.state.frame, p = null; $.state.instances.forEach(function (i) { if (i.objId === 'o_p') p = i; });
var g = $.state.globals, s = {};
if (f === 10) s = { jump: true };                                   // salto simple (mantenido)
if (f > 10 && f < 40) s = { jumpHeld: true };
if (f === 60) s = { jump: true };                                   // salto + doble salto en el aire
if (f > 60 && f < 110) s = { jumpHeld: true };
if (f === 78) s = { jump: true, jumpHeld: true };
if (f === 140) s = { dash: true, right: true };                     // dash desde el suelo
if (f > 140 && f < 150) s = { right: true };
if (f >= 170 && f < 420) s = { right: true };                       // atraviesa el muro decorativo y llega al pincho
if (f === 165) { p.scaleY = 0.5; p.scaleX = 2; }                    // la caja de colision no cambia con la escala
p.simNext = s;
if (f >= 10 && f <= 40) g.minA = Math.min(g.minA == null ? p.y : g.minA, p.y);
if (f >= 60 && f <= 120) g.minB = Math.min(g.minB == null ? p.y : g.minB, p.y);
g.h1 = g.y0 - g.minA; g.h2 = g.y0 - g.minB;
if (f === 139) g.dx0 = p.x;
if (f === 150) { g.dash = p.x - g.dx0; g.dashY = p.y; }
'''
report = r'''var g = $.state.globals;
var got = { simple: g.h1 > 85 && g.h1 < 110, doble: g.h2 > 150 && g.h2 < 190, dash: g.dash > 100 && g.dash < 160, dashSuelo: Math.abs(g.dashY - %d) < 1,
            saltos: g.nj, aire: g.na, dashes: g.nd, aterrizajes: g.nl >= 3, muro: g.pasoMuro, pincho: g.pincho, caja: g.caja };
var want = { simple: true, doble: true, dash: true, dashSuelo: true, saltos: 2, aire: 1, dashes: 1, aterrizajes: true, muro: true, pincho: true, caja: true };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got, h1: g.h1, h2: g.h2, dx: g.dash }));''' % GY
events = []
events.append(ev([C('atStart')], [C('setVar', variable='nj', op='set', value='0'), C('setVar', variable='na', op='set', value='0'),
                                  C('setVar', variable='nd', op='set', value='0'), C('setVar', variable='nl', op='set', value='0'),
                                  C('setVar', variable='y0', op='set', value=str(GY)), C('setVar', variable='pincho', op='set', value='false')]))
events.append(ev([], [C('jsCode', code=script)]))
events.append(ev([C('platEvent', object='o_p', what='jump')], [C('setVar', variable='nj', op='add', value='1'),
                 C('particles', object='o_p', x=0, y=13, count=8, color='#ffffff,#aaaaaa', speed=100, angle=-90, spread=160, life=0.3, size=3, gravity=200, glow='no')]))
events.append(ev([C('platEvent', object='o_p', what='air')], [C('setVar', variable='na', op='add', value='1')]))
events.append(ev([C('platEvent', object='o_p', what='dash')], [C('setVar', variable='nd', op='add', value='1')]))
events.append(ev([C('platEvent', object='o_p', what='land')], [C('setVar', variable='nl', op='add', value='1')]))
events.append(ev([C('objPos', object='o_p', axis='x', op='gt', value=str(13 * T + 20))], [C('setVar', variable='pasoMuro', op='set', value='true')]))
events.append(ev([C('touchesTile', object='o_p', flag='danger')], [C('setBool', variable='pincho', value='true')]))
events.append(ev([C('jsCode', code='return $.state.frame === 168;')],
                 [C('jsCode', code="var p=null;$.state.instances.forEach(function(i){if(i.objId==='o_p')p=i;});$.state.globals.caja = Math.abs(p.y - %d) < 1;" % GY)]))
events.append(ev([C('jsCode', code='return $.state.frame >= 430;')], [C('jsCode', code=report), C('quitGame')]))
doc = dict(id='p', name='Plataformero', version=3, settings=dict(width=960, height=400), globals=[], sprites=[spr], tilesets=[ts], objects=[player],
           layers=[dict(id='base', name='Principal', z=0, parallax=1, solid=True), dict(id='deco', name='Deco', z=1, parallax=1, solid=False)],
           scenes=[dict(id='sc', name='s', width=COLS * T, height=ROWS * T, background='#000000',
                        instances=[dict(id='i', objectId='o_p', x=3 * T, y=GY, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)],
                        tilemaps=[dict(id='tm', name='suelo', tilesetId='ts', layer='base', x=0, y=0, cols=COLS, rows=ROWS, data=ground),
                                  dict(id='tm2', name='muro', tilesetId='ts', layer='deco', x=0, y=0, cols=COLS, rows=ROWS, data=deco)],
                        events=events)],
           events=[])
json.dump(doc, open(sys.argv[1], 'w'))
