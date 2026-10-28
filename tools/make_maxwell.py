#!/usr/bin/env python3
"""Genera examples_maxwell.source2d.json: "Maxwell el gato", un gato que gira sin parar, todo en 2D.

El giro es una animacion de 36 imagenes (una cada 10 grados) en tools/maxwell_giro.png; los eventos eligen la imagen
segun el angulo (que avanza con la velocidad) y la escala hace de zoom. Modelo y textura de Podepi
("maxwell the cat spins horizontally"). Flechas izquierda/derecha: velocidad; arriba/abajo: zoom; espacio: reiniciar.

Uso: make_maxwell.py <salida.json>"""
import json, os, sys
from PIL import Image

OUT = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
VW, VH, N = 240, 136, 36
tira = Image.open(os.path.join(HERE, 'maxwell_giro.png')).convert('RGBA')
FW, FH = tira.width // N, tira.height


def pixels(im):
    raw = im.tobytes()
    return [None if raw[i + 3] < 128 else '#%02x%02x%02x' % (raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 4)]


frames = [dict(pixels=pixels(tira.crop((k * FW, 0, (k + 1) * FW, FH)))) for k in range(N)]
sprites = [dict(id='s_maxwell', name='Maxwell girando', width=FW, height=FH, frames=frames)]


def obj(oid, name, otype='sprite', **k):
    o = dict(id=oid, name=name, type=otype, spriteId=None, hud=False, shader='', capa='base', vars=[], animations=[],
             behaviors=dict(gravity=False, solid=False, oneway=False, collide=False, movement='none', speed=0, jump=0,
                            friction=0.85, airFriction=0.99, bounce=0, mass=1, spin=0),
             hooks=dict(onCreate='', onUpdate=''), flipOnMove=False, comportamientos={})
    o.update(k)
    return o


def C(t, **p):
    return dict(type=t, enabled=True, invert=False, params=p)


def ev(eid, conds, acts):
    return dict(id=eid, enabled=True, level=0, conditions=conds, actions=acts)


objects = [obj('o_maxwell', 'Maxwell', spriteId='s_maxwell'),
           obj('o_ayuda', 'Ayuda', 'text', hud=True, text=dict(content='Flechas: velocidad y zoom   Espacio: reiniciar', size=8,
                                                               color='#f4f4f4', align='center', bold=False))]
CX, CY = VW / 2, 63.5
INICIO = [C('setVar', variable='velocidad', op='set', value='68.75'), C('setVar', variable='angulo', op='set', value='0'),
          C('setVar', variable='distancia', op='set', value='6')]
events = [
    ev('ev_inicio', [C('atStart')], INICIO),
    ev('ev_giro', [], [C('setVar', variable='angulo', op='add', value='velocidad * TiempoDelta()'),
                       C('setFrame', object='o_maxwell', value='floor(((angulo % 360) + 360) % 360 / 10) % 36'),
                       C('setScale', object='o_maxwell', x='6 / distancia', y='6 / distancia')]),
    ev('ev_mas', [C('keyDown', key='arrowright')], [C('setVar', variable='velocidad', op='add', value='120 * TiempoDelta()')]),
    ev('ev_menos', [C('keyDown', key='arrowleft')], [C('setVar', variable='velocidad', op='sub', value='120 * TiempoDelta()')]),
    ev('ev_cerca', [C('keyDown', key='arrowup')], [C('setVar', variable='distancia', op='set', value='max(2.5, distancia - 3 * TiempoDelta())')]),
    ev('ev_lejos', [C('keyDown', key='arrowdown')], [C('setVar', variable='distancia', op='set', value='min(14, distancia + 3 * TiempoDelta())')]),
    ev('ev_reset', [C('keyPressed', key=' ')], INICIO),
    ev('ev_ayuda', [C('timeFromStart', op='ge', value='4'), C('triggerOnce')], [C('tweenOpacity', object='o_ayuda', value=0, duration=1.5, easing='easeInOut')]),
]
doc = dict(id='p_maxwell', name='Maxwell el gato', version=3,
           settings=dict(width=VW, height=VH, fullscreen=True, autofullscreen=False),
           globals=[], sprites=sprites, tilesets=[], objects=objects,
           scenes=[dict(id='sc_maxwell', name='Maxwell', width=VW, height=VH, background='#29366f',
                        instances=[dict(id='i_maxwell', objectId='o_maxwell', x=CX, y=CY, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0),
                                   dict(id='i_ayuda', objectId='o_ayuda', x=VW / 2, y=128, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)],
                        tilemaps=[], effects=[], events=events)],
           events=[], layers=[dict(id='base', name='Principal', z=0, parallax=1, solid=False)])
json.dump(doc, open(OUT, 'w'), ensure_ascii=False, separators=(',', ':'))
print(OUT, '%d imagenes de %dx%d' % (N, FW, FH))
