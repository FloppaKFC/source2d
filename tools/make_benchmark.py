#!/usr/bin/env python3
"""Genera "Prueba de rendimiento" (examples_benchmark.source2d.json): un juego que mide el motor solo, en tres pruebas
de 8 segundos que se encadenan, y una pantalla de resultados:
  1. CPU: 1500 bloques que se mueven y rebotan por eventos (condiciones de posicion, acciones de mover y de
     variables), un Repetir de 3000 vueltas con cuentas y una funcion recursiva en cada fotograma.
  2. GPU: 3000 sprites que giran, cambian de tamano y de opacidad, particulas, filtros de escena (desenfoque, vineta,
     aberracion) y una capa con shader GLSL.
  3. Fisica: 400 cajas con gravedad que caen y chocan contra el suelo y entre si, y colisiones contra el jugador.
Cada prueba muestra los fps y al final se ven todos los resultados (y se imprimen como "S2DBENCH {...}").
Para medir sin el limite de 60 fps: abrirlo con el jugador y --uncapped (o exportarlo y ./juego --uncapped).
Con la variable global "salir" a 1 (o el guion de pruebas) se cierra solo al terminar.
Uso: make_benchmark.py <salida.json>"""
import copy
import json
import os
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
base = json.load(open(os.path.join(RAIZ, 'examples_plataformas.source2d.json')))
OB = {o['name']: o for o in base['objects']}
SEG = 8          # segundos por prueba


def C(t, **p):
    return dict(type=t, enabled=True, invert=False, params=p)


def obj(oid, name, sprite, **beh):
    b = dict(OB['Moneda']['behaviors'])
    b.update(beh)
    return dict(id=oid, name=name, type='sprite', spriteId=sprite, hud=False, shader='', vars=[], behaviors=b,
                animations=[], flipOnMove=False, comportamientos={})


def text(oid, name, content, size=20, color='#ffffff'):
    t = copy.deepcopy(OB['Puntuacion'])
    t.update(id=oid, name=name)
    t['text'] = dict(content=content, size=size, color=color, align='left')
    return t


objects = [
    obj('o_bloque', 'Bloque', 's_3vbyfc'),
    dict(obj('o_estrella', 'Estrella', 's_ylw6zo'), capa='fx'),
    obj('o_caja', 'Caja', 's_5qjo2h', gravity=True, collide=True, solid=True, bounce=0.3),
    copy.deepcopy(OB['Jugador']),
    text('o_titulo', 'Titulo', '', 26),
    text('o_info', 'Info', '', 18, '#8fb8ff'),
    text('o_res', 'Resultados', '', 22),
]
for o in objects:
    if o['type'] == 'text':
        o['hud'] = True

# medicion: cada fotograma suma; al final de la prueba guarda fps medios (fotogramas / segundos de verdad)
MEDIR = '''var g = $.vars, t = (typeof performance !== 'undefined' ? performance.now() : Date.now());
if (!g._t0) { g._t0 = t; g._n = 0; }
g._n++;
var s = (t - g._t0) / 1000;
g.fps_ahora = s > 0 ? Math.round(g._n / s) : 0;
g.objetos = $.state.instances.length;
g.t = Math.round(s * 100) / 100; g.resta = Math.max(0, %d - Math.floor(s));''' % SEG
FIN = '''var g = $.vars, s = ((typeof performance !== 'undefined' ? performance.now() : Date.now()) - g._t0) / 1000;
g['%s'] = Math.round(g._n / s * 10) / 10; g._t0 = 0;'''


def hud(sc, titulo, detalle):
    sc['instances'] += [
        dict(id='i_tit', objectId='o_titulo', x=16, y=24, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0, z=100),
        dict(id='i_inf', objectId='o_info', x=16, y=56, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0, z=100),
    ]
    sc['events'] += [
        dict(id='m', enabled=True, level=0, conditions=[], actions=[C('jsCode', code=MEDIR)]),
        dict(id='h', enabled=True, level=0, conditions=[],
             actions=[C('setText', object='o_titulo', text=titulo + '  -  {g:fps_ahora} fps'),
                      C('setText', object='o_info', text=detalle + '  -  {g:objetos} objetos  -  quedan {g:resta} s')]),
    ]


def scene(sid, name, bg='#1b1f2a'):
    return dict(id=sid, name=name, width=960, height=540, background=bg, instances=[], tilemaps=[], effects=[],
                shaderCode='', camera=dict(zoom=1, smooth=0, limits='none'), events=[])


def next_after(sc, key, nxt):
    sc['events'].append(dict(id='fin', enabled=True, level=0, conditions=[C('cmpNum', a='$t', op='ge', b=str(SEG)), C('triggerOnce')],
                             actions=[C('jsCode', code=FIN % key), C('changeScene', scene=nxt)]))


# 1. CPU: eventos, variables, bucles y funciones
cpu = scene('sc_cpu', 'CPU: eventos')
cpu['events'] += [
    dict(id='f', enabled=True, level=0, type='function', name='fib', params='n', conditions=[], actions=[]),
    dict(id='f1', enabled=True, level=1, conditions=[C('cmpNum', a='$n', op='lt', b='2')], actions=[C('returnValue', value='$n')]),
    dict(id='f2', enabled=True, level=1, conditions=[], actions=[C('returnValue', value='Fn.fib($n - 1) + Fn.fib($n - 2)')]),
    dict(id='c0', enabled=True, level=0, type='repeat', count='1500', var='k', conditions=[C('atStart')],
         actions=[C('create', object='o_bloque', x='randInt(10, 950)', y='randInt(10, 530)')]),
    # cada bloque avanza y rebota en los bordes (condiciones y acciones por instancia)
    dict(id='c1', enabled=True, level=0, conditions=[], actions=[C('addToX', object='o_bloque', value='3'), C('addToY', object='o_bloque', value='1')]),
    dict(id='c2', enabled=True, level=0, conditions=[C('objPos', object='o_bloque', axis='x', op='gt', value='950')], actions=[C('setX', object='o_bloque', value='10')]),
    dict(id='c3', enabled=True, level=0, conditions=[C('objPos', object='o_bloque', axis='y', op='gt', value='530')], actions=[C('setY', object='o_bloque', value='10')]),
    # cuentas: un Repetir de 3000 vueltas y fib(14) recursiva en cada fotograma
    dict(id='c4', enabled=True, level=0, conditions=[], actions=[C('setGlobal', variable='acum', value='0')]),
    dict(id='c5', enabled=True, level=0, type='repeat', count='3000', var='i', conditions=[],
         actions=[C('addGlobal', variable='acum', value='($i * 7) % 13')]),
    dict(id='c6', enabled=True, level=0, conditions=[], actions=[C('setGlobal', variable='fib', value='Fn.fib(14)')]),
]
hud(cpu, 'Prueba 1/3: CPU', '1500 bloques por eventos, Repetir x3000 y fib(14) por fotograma')
next_after(cpu, 'fps_cpu', 'sc_gpu')

# 2. GPU: muchos sprites con giro, escala y opacidad, particulas, filtros y una capa con shader
gpu = scene('sc_gpu', 'GPU: dibujo', '#0e1220')
gpu['effects'] = [dict(type='filter', amount=50, code='saturate(1.3) contrast(1.1)'), dict(type='vignette', amount=60),
                  dict(type='chroma', amount=30)]
gpu['events'] += [
    dict(id='g0', enabled=True, level=0, type='repeat', count='3000', var='k', conditions=[C('atStart')],
         actions=[C('create', object='o_estrella', x='randInt(0, 960)', y='randInt(0, 540)')]),
    dict(id='g1', enabled=True, level=0, conditions=[], actions=[
        C('rotateBy', object='o_estrella', value='4'),
        C('setScale', object='o_estrella', x='1 + 0.5 * sin($t * 3)', y='1 + 0.5 * sin($t * 3)'),
        C('setOpacity', object='o_estrella', value='60 + 40 * sin($t * 2)')]),
    dict(id='g2', enabled=True, level=0, conditions=[C('every', seconds='0.1')], actions=[
        C('particles', object='', x='randInt(0, 960)', y='randInt(0, 540)', count='60', color='#ffcc44,#ff6688,#66ccff', size='5', speed='240',
          life='1.2', angle='-90', spread='360', gravity='300', glow=True)]),
]
hud(gpu, 'Prueba 2/3: GPU', '3000 sprites que giran y cambian, particulas, 3 filtros de escena y una capa con shader')
next_after(gpu, 'fps_gpu', 'sc_fis')

# 3. Fisica: cajas con gravedad que chocan con el suelo y entre si
fis = scene('sc_fis', 'Fisica y colisiones', '#202733')
fis['tilemaps'] = [dict(id='tm_suelo', name='Suelo', tilesetId=base['tilesets'][0]['id'], x=0, y=0, cols=60, rows=34,
                        shader='', data=[(0 if r >= 31 or c in (0, 59) else -1) for r in range(34) for c in range(60)])]
fis['instances'].append(dict(id='i_jug', objectId=OB['Jugador']['id'], x=480, y=400, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0))
fis['events'] += [
    dict(id='p0', enabled=True, level=0, type='repeat', count='400', var='k', conditions=[C('atStart')],
         actions=[C('create', object='o_caja', x='40 + ($k % 40) * 22', y='-20 - floor($k / 40) * 30')]),
    dict(id='p1', enabled=True, level=0, conditions=[C('collision', object=OB['Jugador']['id'], **{'with': 'o_caja'})],
         actions=[C('addGlobal', variable='choques', value='1')]),
]
hud(fis, 'Prueba 3/3: Fisica', '400 cajas con gravedad que chocan con el suelo y entre si')
next_after(fis, 'fps_fis', 'sc_res')

# resultados
res = scene('sc_res', 'Resultados', '#151821')
res['instances'] = [dict(id='i_res', objectId='o_res', x=60, y=200, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)]
REPORT = '''var g = $.vars;
console.log('S2DBENCH ' + JSON.stringify({ cpu: g.fps_cpu, gpu: g.fps_gpu, fisica: g.fps_fis }));'''
res['events'] = [
    dict(id='r0', enabled=True, level=0, conditions=[C('atStart')], actions=[
        C('setText', object='o_res', text='Resultados (fotogramas por segundo, mas es mejor)\n\n'
                                          'CPU (eventos):   {g:fps_cpu}\nGPU (dibujo):     {g:fps_gpu}\nFisica:           {g:fps_fis}\n\n'
                                          'Con limite a la pantalla (60 Hz) lo normal es 60. Para la cifra real: --uncapped.\nR: repetir'),
        C('jsCode', code=REPORT)]),
    dict(id='r1', enabled=True, level=0, conditions=[C('globalVar', variable='salir', op='eq', value='1'), C('triggerOnce')],
         actions=[C('quitGame')]),
    dict(id='r2', enabled=True, level=0, conditions=[C('keyPressed', key='r')], actions=[C('changeScene', scene='sc_cpu')]),
]

project = dict(id='p_rendimiento', name='Prueba de rendimiento', version=3,
               settings=dict(width=960, height=540, fullscreen=True, autofullscreen=False),
               globals=[dict(name=n, type='normal', value=0) for n in ('fps_cpu', 'fps_gpu', 'fps_fis', 'fps_ahora', 'objetos', 'acum', 'fib', 'choques', 'salir', 't', 'resta')],
               sprites=base['sprites'], tilesets=base['tilesets'], objects=objects,
               layers=[dict(id='base', name='Principal', z=0, parallax=1, solid=True),
                       dict(id='fx', name='Con shader', z=1, parallax=1, solid=False, shaderId='sh_ondas')],
               shaders=[dict(id='sh_ondas', name='Ondas', code='precision mediump float;\nvarying vec2 v_uv;\nuniform sampler2D u_tex;\n'
                             'uniform float u_time;\nvoid main() {\n  vec2 uv = v_uv + vec2(sin(v_uv.y * 30.0 + u_time * 4.0), '
                             'cos(v_uv.x * 30.0 + u_time * 3.0)) * 0.004;\n  gl_FragColor = texture2D(u_tex, uv);\n}\n')],
               scenes=[cpu, gpu, fis, res], events=[], fonts=[], music=[], files=[])
json.dump(project, open(sys.argv[1], 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
print(sys.argv[1], 'listo')
