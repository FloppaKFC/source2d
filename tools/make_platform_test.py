#!/usr/bin/env python3
"""Genera examples_platform_test.source2d.json: "Platform Test", un plataformero dificil en el Limbo.

- Tres niveles (tools/platform_levels.py), cada uno mas largo y dificil: en el 1 solo hay doble salto; en el 2 se
  descubre el dash en un santuario; en el 3, trepar por las paredes (salto de pared). tools/solve_platform.py
  demuestra que los tres se pueden completar con la fisica del motor.
- Historia con dialogos: Velo, un espiritu farol, explica que el jugador esta en el Limbo; recuerdos flotantes
  cuentan quien era. Ningun otro texto en pantalla (los botones del menu son iconos).
- Pasto alto que se aparta cuando el jugador pasa y vuelve con rebote; viento con un shader en la capa de vegetacion;
  lianas, hongos y cristales que brillan; parallax con montes, columnas y pared de cueva; particulas.
- Menu con botones dibujados (raton o flechas + Enter) y "Continuar" desde el ultimo nivel alcanzado.

Uso: make_platform_test.py <salida.json>"""
import json, math, random, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import platform_art as art
import limbo_art as la
import platform_levels as pl
import chess_font
import rpg_art

T = pl.T
VW, VH = 960, 540
rnd = random.Random(7)


def strip_comments(doc):
    """los juegos de prueba van sin comentarios en la hoja de eventos"""
    for lst in [doc.get('events', [])] + [sc.get('events', []) for sc in doc.get('scenes', [])]:
        lst[:] = [e for e in lst if e.get('kind') != 'comment']


# ---------------- ayudas de eventos ----------------
def C(t, inv=False, **p):
    return dict(type=t, enabled=True, invert=inv, params=p)


def OR(*subs):
    return dict(type='or', enabled=True, invert=False, params={}, sub=list(subs))


_n = [0]


def ev(conds, acts, level=0, **extra):
    _n[0] += 1
    e = dict(id='ev%d' % _n[0], enabled=True, level=level, conditions=conds, actions=acts)
    e.update(extra)
    return e


def var(name, op, value):
    return C('varCmp', variable=name, op=op, value=str(value))


def setv(name, value, op='set'):
    return C('setVar', variable=name, op=op, value=str(value))


def sets(name, value):
    return C('setGlobal', variable=name, value=value)


def timer(name):
    return [C('timerCtl', name=name, action='reset'), C('timerCtl', name=name, action='start')]


def parts(obj, count, color, speed=80, life=0.5, size=3, gravity=0, x=0, y=0, spread=360, angle=-90, glow='yes'):
    return C('particles', object=obj, x=x, y=y, count=count, color=color, speed=speed, angle=angle, spread=spread, life=life,
             size=size, gravity=gravity, glow=glow)


def sfx(wave, f0, f1, dur, vol=30):
    return C('sfx', wave=wave, freqFrom=f0, freqTo=f1, duration=dur, volume=vol)


# ---------------- fuente de pixeles (16 px de letra en celdas de 16x20: lineas separadas) ----------------
FH = 20
_f16 = chess_font.font('f_limbo', 'Limbo 16')
FONT = dict(_f16, cellH=FH, glyphs=[dict(pixels=g['pixels'] + [''] * (16 * (FH - 16))) for g in _f16['glyphs']])


def _adv(c):
    if c not in chess_font.G:
        return 8
    g = chess_font.scale2x(chess_font.base(chess_font.G[c]))
    right = max([x for y in range(16) for x in range(16) if g[y][x]] or [-1])
    return right + 1 if right >= 0 else 8


_ADV = {c: _adv(c) for c in chess_font.CHARS}


def text_w(s):
    return sum(_ADV.get(c, 8) for c in s) + 2 * max(0, len(s) - 1)


def wrap(text, width=820):
    out = []
    for para in text.split('\n'):
        line = ''
        for w in para.split(' '):
            t = (line + ' ' + w) if line else w
            if text_w(t) <= width:
                line = t
            else:
                out.append(line)
                line = w
        out.append(line)
    return '\n'.join(out)


# ---------------- sprites ----------------
sprites = []


def sprite(sid, name, frames):
    sprites.append(dict(id=sid, name=name, width=frames[0].w, height=frames[0].h, frames=[dict(pixels=f.p) for f in frames]))


pf = [art.player_frame('idle', i) for i in range(2)]
sprite('s_quieto', 'Jugador quieto', pf)
sprite('s_correr', 'Jugador corre', [art.player_frame('run', i) for i in range(4)])
sprite('s_saltar', 'Jugador salta', [art.player_frame('jump')])
sprite('s_caer', 'Jugador cae', [art.player_frame('fall')])
sprite('s_dash', 'Jugador dash', [art.player_frame('dash')])
sprite('s_pared', 'Jugador en la pared', [art.player_frame('fall')])
sprite('s_sierra', 'Sierra', [art.saw_sprite()])
sprite('s_farol', 'Farol', [art.checkpoint_sprite(False), art.checkpoint_sprite(True)])
sprite('s_meta', 'Meta', [art.goal_sprite()])
sprite('s_cielo', 'Cielo', [art.sky_sprite()])
sprite('s_monte1', 'Monte grande', [art.mountain_sprite(256, 150, 3, art.FAR, '#33497a', '#22335a')])
sprite('s_monte2', 'Monte pequeño', [art.mountain_sprite(176, 110, 4, art.FAR, '#33497a', '#22335a')])
sprite('s_columna', 'Columna', [art.spike_rock_sprite(72, 260, 5, art.MID, '#26375c', False)])
sprite('s_estalactita', 'Estalactita', [art.spike_rock_sprite(64, 220, 6, art.MID, '#26375c', True)])
sprite('s_pasto', 'Pasto alto', [la.grass_blade(s) for s in (1, 2, 3, 4)])
sprite('s_santuario_dash', 'Santuario del dash', [la.shrine('dash', False), la.shrine('dash', True)])
sprite('s_santuario_pared', 'Santuario de la pared', [la.shrine('wall', False), la.shrine('wall', True)])
sprite('s_recuerdo', 'Recuerdo', [la.memory_orb(0), la.memory_orb(1), la.memory_orb(2), la.memory_orb(1)])
sprite('s_velo', 'Velo', [la.velo(0), la.velo(1)])
sprite('s_btn_jugar', 'Botón jugar', [la.button('play', False), la.button('play', True)])
sprite('s_btn_seguir', 'Botón continuar', [la.button('continue', False), la.button('continue', True)])
sprite('s_btn_salir', 'Botón salir', [la.button('exit', False), la.button('exit', True)])
sprite('s_caja', 'Caja de diálogo', [rpg_art.dialog_box(880, 120)])
sprite('s_etiqueta', 'Etiqueta del nombre', [rpg_art.name_tag(200, 30)])
sprite('s_sigue', 'Sigue', [la.arrow_down()])


def tileset(tid, name, imgs, flags=None):
    tiles = []
    for i, im in enumerate(imgs):
        t = dict(pixels=im.p, solid=False)
        if flags and i in flags:
            t.update(flags[i])
        tiles.append(t)
    return dict(id=tid, name=name, tileSize=T, columns=8, rows=(len(tiles) + 7) // 8, tiles=tiles)


# ---------------- tilesets ----------------
rock_imgs = [art.rock_tile(m, v) for v in range(2) for m in range(16)]
th = art.thorn_tile()
thorns = [th, th.rot90(2), th.rot90(1), th.rot90(3)]          # ^ v > <
th2 = art.thorn_tile(9)
thorns += [th2, th2.rot90(2), th2.rot90(1), th2.rot90(3)]
ROCK_N = len(rock_imgs)
flags = {i: dict(solid=True) for i in range(ROCK_N)}
for i in range(8):
    flags[ROCK_N + i] = dict(danger=True)
ts_rock = tileset('ts_roca', 'Roca y espinas', rock_imgs + thorns, flags)
THORN_IDX = {'^': 0, 'v': 1, '>': 2, '<': 3}
# vegetacion baja (el pasto alto son objetos), hongos y cristales; lianas aparte (cuelgan: sin viento)
veg_imgs = [art.grass_tile('grass', s) for s in (1, 2)] + [art.grass_tile('fern', 4), art.grass_tile('flower', 6),
                                                          la.mushrooms(1), la.mushrooms(2), la.crystals(3), la.crystals(5)]
VEG_GRASS, VEG_FERN, VEG_FLOWER, VEG_MUSH, VEG_CRYS = (0, 1), 2, 3, (4, 5), (6, 7)
ts_veg = tileset('ts_veg', 'Vegetación', veg_imgs)
ts_vine = tileset('ts_lianas', 'Lianas', [la.vine('top', 1), la.vine('mid', 2), la.vine('end', 3), la.vine('top', 4), la.vine('end', 5)])
ts_far = tileset('ts_lejos', 'Montes lejanos', art.silhouette_tiles(art.FAR, art.FAR2, 31))
ts_mid = tileset('ts_medio', 'Columnas', art.silhouette_tiles(art.MID, art.MID3, 41))
ts_wall = tileset('ts_pared', 'Pared de la cueva', [art.wall_autotile(m) for m in range(16)])
ts_fg = tileset('ts_frente', 'Primer plano', art.foreground_tiles())


def tilemap(tid, name, ts, layer, cols, rows, data, x=0, y=0):
    return dict(id=tid, name=name, tilesetId=ts, layer=layer, x=x, y=y, cols=cols, rows=rows, shader='', data=data)


# ---------------- capas y shader ----------------
WIND = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_res;
uniform vec3 u_cam;
// Viento para la capa de vegetacion: cada planta (un tile de 32 px) se mece anclada a su base.
// u_cam = camara de la capa (x, y del mundo) y zoom: asi el movimiento va pegado al mundo, no a la pantalla.
const float TILE = 32.0;
void main() {
  vec2 world = u_cam.xy + v_uv * u_res / u_cam.z;
  float ly = fract(world.y / TILE);
  float bend = pow(1.0 - ly, 1.7);
  float gust = 0.55 + 0.45 * sin(u_time * 0.6 + world.x * 0.003);
  float sway = sin(u_time * 2.2 + world.x * 0.045) * 3.4 * gust + sin(u_time * 5.3 + world.x * 0.17) * 0.7;
  float dx = sway * bend * u_cam.z / u_res.x;
  gl_FragColor = texture2D(u_tex, v_uv - vec2(dx, 0.0));
}
"""
layers = [
    dict(id='cielo', name='Cielo', z=-6, parallax=0, solid=False),
    dict(id='lejos', name='Montes lejanos', z=-5, parallax=0.2, solid=False),
    dict(id='medio', name='Columnas', z=-4, parallax=0.45, solid=False),
    dict(id='pared', name='Pared de la cueva', z=-3, parallax=0.85, solid=False),
    dict(id='lianas', name='Lianas', z=-2, parallax=1, solid=False),
    dict(id='vegetacion', name='Vegetación', z=-1, parallax=1, solid=False, shaderId='sh_viento', shaderContentOnly=True),
    dict(id='base', name='Camino', z=0, parallax=1, solid=True),
    dict(id='frente', name='Primer plano', z=2, parallax=1.25, solid=False),
    dict(id='dialogo', name='Diálogos', z=5, parallax=1, solid=False),
]
shaders = [dict(id='sh_viento', name='Viento en la vegetación', code=WIND)]

# ---------------- objetos ----------------
objects = []


def obj(oid, name, otype='sprite', scene=None, **k):
    o = dict(id=oid, name=name, type=otype, spriteId=None, hud=False, shader='', capa='base', vars=[], animations=[],
             behaviors=dict(gravity=False, solid=False, oneway=False, collide=False, movement='none', speed=150, jump=620,
                            friction=0.85, airFriction=0.99, bounce=0, mass=1, spin=0),
             hooks=dict(onCreate='', onUpdate=''), flipOnMove=True, comportamientos={})
    if scene:
        o['scene'] = scene
    bh = k.pop('bh', None)
    if bh:
        o['behaviors'].update(bh)
    o.update(k)
    objects.append(o)
    return oid


P = pl.PLAYER
J = 'o_jugador'
anims = [dict(id='a_quieto', name='Quieto', spriteId='s_quieto', fps=2, loop=True),
         dict(id='a_correr', name='Correr', spriteId='s_correr', fps=12, loop=True),
         dict(id='a_saltar', name='Saltar', spriteId='s_saltar', fps=8, loop=True),
         dict(id='a_caer', name='Caer', spriteId='s_caer', fps=8, loop=True),
         dict(id='a_dash', name='Dash', spriteId='s_dash', fps=8, loop=True),
         dict(id='a_pared', name='Pared', spriteId='s_pared', fps=8, loop=True)]
obj(J, 'Jugador', spriteId='s_quieto', animations=anims,
    animSlots=dict(quieto='a_quieto', caminar='a_correr', saltar='a_saltar', caer='a_caer', dash='a_dash', pared='a_pared'),
    hitbox=dict(w=P['w'], h=P['h']),
    behaviors=dict(gravity=True, solid=False, oneway=False, collide=True, movement='platform', speed=P['speed'], jump=P['jump'],
                   friction=0.85, airFriction=0.99, bounce=0, mass=1, spin=0),
    comportamientos=dict(plataformero=dict(keyLeft='', keyRight='', keyJump='', airJumps=P['airJumps'], airJump=P['airJump'],
                                           dash=True, keyDash='shift', dashSpeed=P['dashSpeed'], dashTime=P['dashTime'],
                                           dashCooldown=P['dashCooldown'], jumpCut=True, coyote=P['coyote'],
                                           jumpBuffer=P['jumpBuffer'], maxFall=P['maxFall'], wall=True, wallJump=True)))
obj('o_sierra', 'Sierra', spriteId='s_sierra', hitbox=dict(w=pl.SAW_HIT, h=pl.SAW_HIT),
    behaviors=dict(gravity=False, solid=False, oneway=False, collide=False, movement='none', speed=0, jump=0,
                   friction=0.85, airFriction=0.99, bounce=0, mass=1, spin=540),
    vars=[dict(name='bx', value=0), dict(name='by', value=0), dict(name='ax', value=0), dict(name='amp', value=0),
          dict(name='per', value=2), dict(name='fase', value=0)])
obj('o_farol', 'Farol', spriteId='s_farol', vars=[dict(name='encendido', value=0)])
obj('o_meta', 'Meta', spriteId='s_meta')
obj('o_cielo', 'Cielo', spriteId='s_cielo', capa='cielo')
obj('o_pasto', 'Pasto', spriteId='s_pasto', capa='vegetacion', flipOnMove=False, vars=[dict(name='toca', value=0)])
obj('o_santuario_dash', 'SantuarioDash', spriteId='s_santuario_dash', vars=[dict(name='activo', value=0)],
    behaviorList=[dict(type='sine', axis='y', amplitude=0, period=2, phase=0)])
obj('o_santuario_pared', 'SantuarioPared', spriteId='s_santuario_pared', vars=[dict(name='activo', value=0)])
obj('o_recuerdo', 'Recuerdo', spriteId='s_recuerdo', animations=[dict(id='a_rec', name='Late', spriteId='s_recuerdo', fps=5, loop=True)],
    animSlots=dict(quieto='a_rec'), vars=[dict(name='clave', value=''), dict(name='visto', value=0)], hitbox=dict(w=40, h=40),
    behaviorList=[dict(type='sine', axis='y', amplitude=6, period=2.4, phase=0)])
obj('o_velo', 'Velo', spriteId='s_velo', animations=[dict(id='a_velo', name='Flota', spriteId='s_velo', fps=3, loop=True)],
    animSlots=dict(quieto='a_velo'), behaviorList=[dict(type='sine', axis='y', amplitude=5, period=2.0, phase=0)])
for oid, name, ic in (('o_btn_jugar', 'BotonJugar', 's_btn_jugar'), ('o_btn_seguir', 'BotonSeguir', 's_btn_seguir'),
                      ('o_btn_salir', 'BotonSalir', 's_btn_salir')):
    obj(oid, name, spriteId=ic, hud=True, flipOnMove=False, vars=[dict(name='idx', value=0)])
obj('o_titulo', 'Título', 'text', hud=True, text=dict(content='Platform Test', size=72, color='#eef4ff', align='center', bold=True))
obj('o_caja', 'Caja', spriteId='s_caja', hud=True)
obj('o_etiqueta', 'Etiqueta', spriteId='s_etiqueta', hud=True)
obj('o_sigue', 'Sigue', spriteId='s_sigue', hud=True, behaviorList=[dict(type='sine', axis='y', amplitude=3, period=0.8, phase=0)])
obj('o_txt_dlg', 'TxtDialogo', 'text', hud=True, text=dict(content='', size=FH, color='#e8f6ff', align='left', bold=False, font='f_limbo'))
obj('o_txt_nombre', 'TxtNombre', 'text', hud=True, text=dict(content='', size=FH, color='#8ff0ff', align='center', bold=False, font='f_limbo'))
bg_objs = [obj('o_monte1', 'Monte grande', spriteId='s_monte1', capa='lejos'), obj('o_monte2', 'Monte pequeño', spriteId='s_monte2', capa='lejos'),
           obj('o_columna', 'Columna', spriteId='s_columna', capa='medio'), obj('o_estalactita', 'Estalactita', spriteId='s_estalactita', capa='medio')]


def inst(iid, oid, x, y, **k):
    d = dict(id=iid, objectId=oid, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
    d.update(k)
    return d


def dialog_insts():
    return [inst('i_caja', 'o_caja', VW / 2, VH - 76), inst('i_etiq', 'o_etiqueta', 150, VH - 136),
            inst('i_tdlg', 'o_txt_dlg', 60, VH - 74), inst('i_tnom', 'o_txt_nombre', 150, VH - 135),
            inst('i_sigue', 'o_sigue', VW - 64, VH - 34)]


# ---------------- dialogos ----------------
DIALOGS = {}
VELO, REC, ECO = 'Velo', 'Recuerdo', 'Eco'


def dlg(key, pages, then=()):
    DIALOGS[key] = dict(pages=[(q, wrap(t)) for q, t in pages], then=list(then))


dlg('inicio_1', [
    (VELO, 'Shh... no te asustes. Lo peor ya pasó.'),
    (VELO, 'Esto es el Limbo: la orilla entre lo que fuiste y lo que vas a ser. Aquí despiertan las almas que todavía no recuerdan su nombre.'),
    (VELO, 'Yo soy Velo. Guío a los que despiertan. Tu cuerpo aún recuerda saltar... y saltar otra vez en el aire. Usa eso.'),
    (VELO, 'Enciende los faroles del camino: si caes, tu luz volverá al último. Y si ves una luz que late, tócala: es un recuerdo tuyo.'),
])
dlg('eco_cocina', [(REC, 'Una cocina con olor a pan recién hecho. Alguien te llama desde la otra habitación... pero no oyes tu nombre.')])
dlg('eco_manos', [(REC, 'Unas manos pequeñas agarran las tuyas. "No te vayas", dice una voz. Tú prometiste que volverías.')])
dlg('fin_1', [(VELO, 'La orilla queda atrás. Más allá está el puente roto: casi nadie lo cruza entero.'),
              (VELO, 'Pero tú no eres cualquiera. Lo noto en tu luz: brilla más que cuando despertaste.')])
dlg('inicio_2', [(VELO, 'El puente roto. Las almas que dudan caen al vacío y vuelven a empezar.'),
                 (VELO, 'Dicen que a mitad de camino duerme un eco de tu fuerza. Si lo encuentras, recuérdalo.')])
dlg('eco_bici', [(REC, 'Una bicicleta roja y una cuesta larguísima. Ibas tan rápido que el mundo se volvía una sola línea.')])
dlg('eco_hospital', [(REC, 'Un pasillo blanco. Una lámpara que parpadea. Alguien te aprieta la mano y llora en silencio.')])
dlg('santuario_dash', [(ECO, '"Ibas tan rápido que el mundo se volvía una sola línea..."'),
                       (VELO, '¡Lo recordaste! Ahora puedes lanzarte como un rayo, también en el aire (Shift). Los fosos largos ya no te detendrán.')])
dlg('fin_2', [(VELO, 'Cruzaste el puente. Ya casi puedo leer tu nombre en tu luz.'),
              (VELO, 'Falta la torre. Arriba está la Luz: quien llega, sigue su camino. Quien no... se queda aquí, conmigo.')])
dlg('inicio_3', [(VELO, 'La torre es el último sendero. Sus paredes son lisas y crueles, y el aire corta.'),
                 (VELO, 'Pero las almas que no se rinden aprenden a subir por donde no hay camino.')])
dlg('eco_vela', [(REC, 'Tu cumpleaños. Una vela que no se apagaba por mucho que soplaras. Todos reían.')])
dlg('eco_promesa', [(REC, 'La voz otra vez: "Te espero". Ahora sabes de quién es. Siempre lo supiste.')])
dlg('santuario_pared', [(ECO, '"Nunca te rendiste, ni una sola vez..."'),
                        (VELO, 'Eso eras tú. Ahora puedes agarrarte a las paredes y saltar de una a otra para subir: salta contra la pared.')])
dlg('final', [(VELO, 'Llegaste. Mira: la Luz.'),
              (VELO, 'Ahora lo recuerdas todo, ¿verdad? La cocina, la bicicleta roja, la vela... las manos pequeñas que te esperan.'),
              (VELO, 'Ve. Alguien te espera al otro lado desde hace mucho. Yo me quedo en la orilla, guiando a los que despiertan.'),
              (VELO, 'Adiós, alma valiente. Que tu luz no se apague nunca.')],
    then=[([], [C('changeScene', scene='sc_menu', transition='fade', duration=2.0)])])

MEMORY_KEYS = {'despertar': 'eco_cocina', 'nombre': 'eco_manos', 'puente': 'eco_bici', 'dash': 'eco_hospital',
               'torre': 'eco_vela', 'promesa': 'eco_promesa'}

ADV = OR(C('keyPressed', key='z'), C('keyPressed', key=' '), C('keyPressed', key='enter'))
DLG_ON = var('dlg', 'eq', 1)


def open_dlg(key):
    return [sets('dk', key), setv('pg', 0), setv('vis', -1), setv('dlg', 1),
            C('show', object='o_caja'), C('show', object='o_txt_dlg'), C('setText', object='o_txt_dlg', text=''),
            C('platOff', object=J), C('setVX', object=J, value=0)]


def common_events():
    """dialogos (en todas las escenas): avanzar, cada pagina y al cerrar"""
    out = [ev([DLG_ON, ADV, C('timerCompare', name='dlgin', op='ge', value=0.2)], []),
           ev([C('timerCompare', name='tw', op='ge', value='tt')], [setv('pg', 1, 'add'), sfx('square', 330, 330, 0.04, 15)], level=1),
           ev([C('timerCompare', name='tw', op='lt', value='tt')], [C('setText', object='o_txt_dlg', text='{g:full}'), setv('tt', 0)], level=1)]
    for key, d in DIALOGS.items():
        for i, (who, text) in enumerate(d['pages']):
            out.append(ev([DLG_ON, C('globalVar', variable='dk', op='eq', value=key), var('pg', 'eq', i), var('vis', 'ne', i)],
                          [setv('vis', i), sets('full', text), C('typewriter', object='o_txt_dlg', text=text, speed=50),
                           setv('tt', round(len(text) / 50 + 0.05, 3)), C('setText', object='o_txt_nombre', text=who),
                           C('show', object='o_etiqueta'), C('show', object='o_txt_nombre')] + timer('tw') + timer('dlgin')))
        out.append(ev([DLG_ON, C('globalVar', variable='dk', op='eq', value=key), var('pg', 'ge', len(d['pages']))],
                      [setv('dlg', 0), C('hide', object='o_caja'), C('hide', object='o_txt_dlg'), C('hide', object='o_etiqueta'),
                       C('hide', object='o_txt_nombre'), C('hide', object='o_sigue')] + timer('dlgfin')))
        out.append(ev([var('muerto', 'eq', 0)], [C('platOn', object=J)], level=1))
        for conds, acts in d['then']:
            out.append(ev(conds, acts, level=1))
    out.append(ev([DLG_ON, C('timerCompare', name='tw', op='ge', value='tt')], [C('show', object='o_sigue')]))
    out.append(ev([DLG_ON, C('timerCompare', name='tw', op='lt', value='tt')], [C('hide', object='o_sigue')]))
    return out


def talk(key):
    """abre un dialogo salvo en la prueba automatica (prueba = 1)"""
    return [ev([var('prueba', 'eq', 0)], open_dlg(key), level=1)]


# ---------------- mapas de cada nivel ----------------
def layer_size(Lw, Lh, par):
    MW, MH = Lw * T, Lh * T
    return int(math.ceil(((MW - VW) * par + VW) / T)) + 2, int(math.ceil(((MH - VH) * par + VH) / T)) + 2


def far_data(cols, rows, n):
    data = [-1] * (cols * rows)
    for r in range(rows - n, rows):
        for c in range(cols):
            data[r * cols + c] = 0
    return data


def wall_data(cols, rows, seed):
    full = [[False] * cols for _ in range(rows)]
    for r in range(rows):
        for c in range(cols):
            nn = 0.6 * art.vnoise(c * 0.09 + r * 0.31, 51 + seed) + 0.4 * art.vnoise(r * 0.12 + c * 0.05, 52 + seed)
            full[r][c] = nn > 0.46
    f = lambda c, r: c < 0 or r < 0 or c >= cols or r >= rows or full[r][c]
    data = [-1] * (cols * rows)
    for r in range(rows):
        for c in range(cols):
            if full[r][c]:
                data[r * cols + c] = (0 if f(c, r - 1) else 1) | (0 if f(c + 1, r) else 2) | (0 if f(c, r + 1) else 4) | (0 if f(c - 1, r) else 8)
    return data


def fg_data(cols, rows, seed):
    data = [-1] * (cols * rows)
    for c in range(cols):
        if art.vnoise(c * 0.11, 61 + seed) > 0.55:
            h = 1 + int(art.vnoise(c * 0.3, 62 + seed) * 2)
            for k in range(h):
                data[(rows - 1 - k) * cols + c] = 0
            data[(rows - 1 - h) * cols + c] = 1
        if art.vnoise(c * 0.13, 63 + seed) > 0.62:
            data[0 * cols + c] = 0
            data[1 * cols + c] = 2
    return data


def backdrop(prefix, w_px, h_px, far_base_rows, seed):
    out, r = [], random.Random(len(prefix) * 31 + w_px + seed)
    x, k = -60, 0
    base_y = h_px - far_base_rows * T
    while x < w_px + 200:
        big = r.random() < 0.55
        sx = r.choice((1, -1)) * r.uniform(0.8, 1.25)
        out.append(inst('%s_m%d' % (prefix, k), 'o_monte1' if big else 'o_monte2', x, base_y - (75 if big else 55) * abs(sx) + 4, scaleX=sx, scaleY=abs(sx)))
        x += r.randint(110, 210); k += 1
    x, k = 30, 0
    while x < w_px + 100:
        if r.random() < 0.5:
            out.append(inst('%s_c%d' % (prefix, k), 'o_columna', x, h_px - 130 * 0.9 + r.randint(0, 60), scaleX=r.choice((1, -1)) * 0.9, scaleY=r.uniform(0.75, 1.0)))
        else:
            out.append(inst('%s_e%d' % (prefix, k), 'o_estalactita', x, 110 * 0.9 - r.randint(0, 50), scaleX=r.choice((1, -1)) * 0.9, scaleY=r.uniform(0.7, 1.0)))
        x += r.randint(140, 300); k += 1
    return out


def level_scene(L):
    G, W, Hh = L.g, L.W, pl.H
    sid = 'sc_nivel%d' % L.num
    MAP_W, MAP_H = W * T, Hh * T

    def solid(c, r):
        return c < 0 or r < 0 or c >= W or r >= Hh or G[r][c] == '#'

    def busy(c, r):
        """cerca de pinchos, sierras, faroles o santuarios: sin plantas"""
        for s in L.saws:
            if abs(s['x'] / T - c) < 2.5 and abs(s['y'] / T - r) < 4:
                return True
        for x, y in L.checkpoints:
            if abs(x / T - c) < 1.5 and abs(y / T - r) < 2:
                return True
        return any(0 <= cc < W and G[r][cc] in '^v<>' for cc in (c - 1, c, c + 1))

    rock = []
    for r in range(Hh):
        for c in range(W):
            ch = G[r][c]
            if ch == '#':
                m = (0 if solid(c, r - 1) else 1) | (0 if solid(c + 1, r) else 2) | (0 if solid(c, r + 1) else 4) | (0 if solid(c - 1, r) else 8)
                rock.append((1 if art.noise2(c, r, 5 + L.num) > 0.6 else 0) * 16 + m)
            elif ch in THORN_IDX:
                rock.append(ROCK_N + THORN_IDX[ch] + (4 if art.noise2(c, r, 8) > 0.5 else 0))
            else:
                rock.append(-1)
    veg, vines = [-1] * (W * Hh), [-1] * (W * Hh)
    grass = []
    gr = random.Random(L.num * 101)
    for r in range(1, Hh):
        for c in range(W):
            if G[r][c] != '#' or G[r - 1][c] != '.' or busy(c, r - 1):
                continue
            k = art.noise2(c, r, 77 + L.num)
            if k < 0.62:                          # pasto alto (objeto que se aparta)
                grass.append(((c + 0.5) * T + gr.uniform(-6, 6), r * T, gr.randrange(4)))
            if k > 0.45:                          # plantas bajas, hongos y cristales que brillan
                kk = art.noise2(c, r, 78 + L.num)
                t = VEG_GRASS[0] if kk < 0.3 else VEG_GRASS[1] if kk < 0.45 else VEG_FERN if kk < 0.58 else VEG_FLOWER if kk < 0.7 else \
                    VEG_MUSH[0] if kk < 0.8 else VEG_MUSH[1] if kk < 0.88 else VEG_CRYS[0] if kk < 0.95 else VEG_CRYS[1]
                veg[(r - 1) * W + c] = t
    for r in range(1, Hh - 3):                    # lianas bajo los techos
        for c in range(W):
            if G[r][c] != '.' or G[r - 1][c] != '#' or r < 3 or art.noise2(c, r, 91) < 0.55:
                continue
            n = 1 + int(art.noise2(c, r, 92) * 3)
            for k in range(n):
                if r + k >= Hh or G[r + k][c] != '.':
                    break
                vines[(r + k) * W + c] = (0 if (c % 2) else 3) if k == 0 else 2 if k == n - 1 else 1
    fc, fr = layer_size(W, Hh, 0.2)
    mc, mr = layer_size(W, Hh, 0.45)
    wc, wr = layer_size(W, Hh, 0.85)
    gc, grr = layer_size(W, Hh, 1.25)
    p = 'n%d' % L.num
    tms = [
        tilemap('tm_%s_lejos' % p, 'Base de los montes', 'ts_lejos', 'lejos', fc, fr, far_data(fc, fr, 4)),
        tilemap('tm_%s_pared' % p, 'Pared de la cueva', 'ts_pared', 'pared', wc, wr, wall_data(wc, wr, L.num * 7)),
        tilemap('tm_%s_lianas' % p, 'Lianas', 'ts_lianas', 'lianas', W, Hh, vines),
        tilemap('tm_%s_veg' % p, 'Vegetación', 'ts_veg', 'vegetacion', W, Hh, veg),
        tilemap('tm_%s_camino' % p, 'Camino', 'ts_roca', 'base', W, Hh, rock),
        tilemap('tm_%s_frente' % p, 'Primer plano', 'ts_frente', 'frente', gc, grr, fg_data(gc, grr, L.num * 13)),
    ]
    insts = [inst('i_cielo', 'o_cielo', VW / 2, VH / 2, scaleX=VW / 4, scaleY=VH / 68 + 0.1)]
    insts += [i for i in backdrop('i_fondo', mc * T, mr * T, 4, L.num) if i['objectId'] in ('o_columna', 'o_estalactita')]
    insts += [i for i in backdrop('i_lejos', fc * T, fr * T, 4, L.num) if i['objectId'] in ('o_monte1', 'o_monte2')]
    for k, (x, y, f) in enumerate(grass):
        insts.append(inst('i_pasto%d' % k, 'o_pasto', round(x, 1), y, frame=f, scaleX=gr.choice((1, -1)) * gr.uniform(0.85, 1.15)))
    insts.append(inst('i_jugador', J, L.start[0], L.start[1]))
    insts.append(inst('i_velo', 'o_velo', L.start[0] - 46, L.start[1] - 40))
    for i, s in enumerate(L.saws):
        insts.append(inst('i_sierra%d' % i, 'o_sierra', s['x'], s['y'],
                          vars=dict(bx=s['x'], by=s['y'], ax=1 if s['axis'] == 'x' else 0, amp=s['amplitude'], per=s['period'], fase=s['phase'])))
    shrine_cps = {s['cp'] for s in L.shrines}
    for i, (x, y) in enumerate(L.checkpoints):
        if i not in shrine_cps:
            insts.append(inst('i_farol%d' % i, 'o_farol', x, y))
    for i, s in enumerate(L.shrines):
        insts.append(inst('i_santuario%d' % i, 'o_santuario_dash' if s['ability'] == 'dash' else 'o_santuario_pared', s['x'], s['y']))
    for i, m in enumerate(L.memories):
        insts.append(inst('i_recuerdo%d' % i, 'o_recuerdo', m['x'], m['y'], vars=dict(clave=MEMORY_KEYS[m['key']], visto=0)))
    insts.append(inst('i_meta', 'o_meta', L.goal[0], L.goal[1]))
    insts += dialog_insts()

    S, F = 'o_sierra', 'o_farol'
    DUST = '#7d8aa6,#56627c,#a9b6cc'
    GLOW = '#8ff0ff,#d8fbff,#ffffff'
    has_dash = L.num >= 3
    evs = [
        ev([C('atStart')], [
            C('cameraFollow', object=J, smoothing=0.12), C('cameraLimits', mode='scene', x=0, y=0, w=MAP_W, h=MAP_H),
            setv('cpX', round(L.start[0], 2)), setv('cpY', round(L.start[1], 2)),
            setv('muerto', 0), setv('ganado', 0), setv('saliendo', 0), setv('reloj', 0), setv('dlg', 0),
            C('platAbility', object=J, ability='air', state='on'),
            C('platAbility', object=J, ability='dash', state='on' if has_dash else 'off'),
            C('platAbility', object=J, ability='wall', state='off'),
            C('hide', object='o_caja'), C('hide', object='o_txt_dlg'), C('hide', object='o_etiqueta'), C('hide', object='o_txt_nombre'),
            C('hide', object='o_sigue'), sets('nivel', str(L.num)), C('saveVars', vars='nivel', slot='limbo')] + timer('dlgfin')),
        ev([C('atStart'), var('prueba', 'eq', 0)], open_dlg('inicio_%d' % L.num)),   # Velo habla al empezar cada nivel
    ]
    evs += [
        ev([], [setv('reloj', 'TiempoDelta()', 'add')]),
        ev([], [], type='forEach', object=S),
        ev([], [C('setPos', object=S,
                  x='Sierra.bx + Sierra.ax * Sierra.amp * sin(2 * PI * (reloj / Sierra.per + Sierra.fase / 360))',
                  y='Sierra.by + (1 - Sierra.ax) * Sierra.amp * sin(2 * PI * (reloj / Sierra.per + Sierra.fase / 360))')], level=1),
        # muerte y faroles
        ev([var('muerto', 'eq', 0), var('ganado', 'eq', 0),
            OR(C('touchesTile', object=J, flag='danger'), C('collision', object=J, **{'with': S}), C('objPos', object=J, axis='y', op='gt', value=str(MAP_H)))], [
            parts(J, 40, '#ffffff,#cfe9ff,#4fd1e0,#1c2233', 320, 0.7, 5, 500, glow='no'),
            parts(J, 18, GLOW, 160, 0.5, 3, 0),
            C('shakeCamera', intensity=9, duration=0.35), C('hide', object=J), C('platOff', object=J),
            setv('muerto', 1), setv('muertes', 1, 'add')] + timer('revivir')),
        ev([var('muerto', 'eq', 1)], [C('setPos', object=J, x='cpX', y='cpY'), C('setVX', object=J, value=0), C('setVY', object=J, value=0)]),
        ev([var('muerto', 'eq', 1), C('timerCompare', name='revivir', op='ge', value=0.7)], [
            C('show', object=J), C('platOn', object=J), setv('muerto', 0),
            parts(J, 24, GLOW, 120, 0.6, 3, -60, y=8, spread=120),
            C('setScale', object=J, x=0.6, y=1.4), C('tweenScale', object=J, x=1, y=1, duration=0.25, easing='backOut')]),
        ev([C('collision', object=J, **{'with': F}), C('objVar', object=F, variable='encendido', op='eq', value='0')], [
            C('setObjVar', object=F, variable='encendido', value='1'), C('setFrame', object=F, value=1),
            setv('cpX', 'Farol.X'), setv('cpY', 'Farol.Y + 11'),
            parts(F, 36, GLOW, 200, 0.9, 3, -40, y=-14), C('shakeCamera', intensity=3, duration=0.15), sfx('triangle', 440, 880, 0.25)]),
        ev([C('objVar', object=F, variable='encendido', op='eq', value='1'), C('every', seconds=0.2)], [
            parts(F, 1, GLOW, 25, 1.2, 2, -20, y=-14, spread=50)]),
    ]
    # santuarios: dan la habilidad, son farol y cuentan su eco
    for so, ab, key in (('o_santuario_dash', 'dash', 'santuario_dash'), ('o_santuario_pared', 'wall', 'santuario_pared')):
        name = 'SantuarioDash' if ab == 'dash' else 'SantuarioPared'
        evs.append(ev([C('collision', object=J, **{'with': so}), C('objVar', object=so, variable='activo', op='eq', value='0')], [
            C('setObjVar', object=so, variable='activo', value='1'), C('setFrame', object=so, value=1),
            C('platAbility', object=J, ability=ab, state='on'), setv('cpX', '%s.X' % name), setv('cpY', '%s.Y + 19' % name),
            parts(so, 90, GLOW + ',#9be3bf', 280, 1.4, 4, 40, y=-40), C('shakeCamera', intensity=6, duration=0.5),
            sfx('triangle', 220, 1320, 0.9, 40)]))
        evs += talk(key)
        evs.append(ev([C('objVar', object=so, variable='activo', op='eq', value='1'), C('every', seconds=0.08)], [
            parts(so, 1, GLOW, 40, 1.4, 2, -30, y=-40, spread=90)]))
        evs.append(ev([C('objVar', object=so, variable='activo', op='eq', value='0'), C('every', seconds=0.5)], [
            parts(so, 1, '#46546f,#5d6e8b', 15, 1.6, 2, -10, y=-40, spread=60, glow='no')]))
    # recuerdos
    evs.append(ev([C('collision', object=J, **{'with': 'o_recuerdo'}), C('objVar', object='o_recuerdo', variable='visto', op='eq', value='0'),
                   var('muerto', 'eq', 0)], [
        C('setObjVar', object='o_recuerdo', variable='visto', value='1'), parts('o_recuerdo', 40, '#ffffff,#cfe9ff,#9fd8ff', 160, 1.0, 3, -20),
        C('tweenOpacity', object='o_recuerdo', value=0, duration=0.8, easing='easeOut'), C('tweenScale', object='o_recuerdo', x=2.5, y=2.5, duration=0.8, easing='easeOut'),
        sfx('sine', 660, 990, 0.6, 30)]))
    evs.append(ev([var('prueba', 'eq', 0)], open_dlg('{= Recuerdo.clave}'), level=1))
    evs.append(ev([C('every', seconds=0.15)], [parts('o_recuerdo', 1, '#ffffff,#cfe9ff', 20, 1.0, 2, -20)]))
    # Velo: flota junto al principio y se desvanece cuando el jugador se aleja
    evs.append(ev([C('distance', object=J, **{'with': 'o_velo'}, op='gt', value=500), C('objVisible', object='o_velo')],
                  [C('tweenOpacity', object='o_velo', value=0, duration=1.0, easing='easeOut')]))
    evs.append(ev([C('every', seconds=0.12)], [parts('o_velo', 1, '#ffd27a,#fff3c9', 12, 0.8, 2, -30, x=11, y=6)]))
    # pasto: se aparta del jugador mientras lo toca y vuelve con rebote al soltarlo
    evs.append(ev([C('collision', object=J, **{'with': 'o_pasto'})], []))
    evs.append(ev([], [], level=1, type='forEach', object='o_pasto'))
    evs.append(ev([], [C('tweenAngle', object='o_pasto', value='sign(Pasto.X - Jugador.X + 0.1) * (14 + min(26, abs(Jugador.VelX) / 12))',
                         duration=0.08, easing='easeOut'), C('setObjVar', object='o_pasto', variable='toca', value='1')], level=2))
    evs.append(ev([C('platEvent', object=J, what='dashing')], [parts('o_pasto', 2, '#3f9a7a,#62c29a,#9be3bf', 90, 0.6, 2, 200, y=-14)], level=2))
    evs.append(ev([C('collision', inv=True, object=J, **{'with': 'o_pasto'}), C('objVar', object='o_pasto', variable='toca', op='eq', value='1')],
                  [C('setObjVar', object='o_pasto', variable='toca', value='0'), C('tweenAngle', object='o_pasto', value=0, duration=0.9, easing='elastic')]))
    # jugo
    evs += [
        ev([C('platEvent', object=J, what='jump')], [
            parts(J, 10, DUST, 110, 0.35, 3, 240, y=13, spread=170, glow='no'),
            C('setScale', object=J, x=0.75, y=1.25), C('tweenScale', object=J, x=1, y=1, duration=0.2, easing='easeOut')]),
        ev([C('platEvent', object=J, what='air')], [
            parts(J, 16, GLOW, 150, 0.35, 3, 0, y=10, angle=90),
            C('setScale', object=J, x=0.8, y=1.2), C('tweenScale', object=J, x=1, y=1, duration=0.2, easing='easeOut')]),
        ev([C('platEvent', object=J, what='wall')], [
            parts(J, 14, '#9be3bf,#ffffff,' + DUST, 140, 0.4, 3, 200),
            C('setScale', object=J, x=0.75, y=1.25), C('tweenScale', object=J, x=1, y=1, duration=0.2, easing='easeOut'), sfx('square', 300, 520, 0.06, 15)]),
        ev([C('platEvent', object=J, what='dash')], [
            parts(J, 14, '#4fd1e0,#bff8ff', 180, 0.3, 3, 0), C('shakeCamera', intensity=3, duration=0.1)]),
        ev([C('platEvent', object=J, what='dashing')], [parts(J, 3, '#4fd1e0,#bff8ff,#2a9fb3', 30, 0.28, 4, 0)]),
        ev([C('platEvent', object=J, what='land')], [
            parts(J, 12, DUST, 130, 0.35, 3, 320, y=13, spread=150, glow='no'),
            C('setScale', object=J, x=1.25, y=0.8), C('tweenScale', object=J, x=1, y=1, duration=0.18, easing='easeOut')]),
        ev([C('objMoving', object=J), C('onGround', object=J), C('every', seconds=0.11)], [
            parts(J, 2, DUST, 50, 0.3, 2, 150, y=13, spread=120, glow='no')]),
        ev([C('every', seconds=0.12)], [parts(S, 1, '#ffd27a,#fff3c9,#ff9a5a', 170, 0.3, 2, 500)]),
        ev([C('every', seconds=0.07)], [
            parts(J, 1, '#8ff0ff,#cfe9ff,#9be3bf', 14, 3.2, 2, -6, x='randInt(-520, 520)', y='randInt(-320, 320)', spread=120)]),
        ev([C('every', seconds=0.18)], [      # ceniza palida que cae: el Limbo
            parts(J, 1, '#cfd8e6,#9aa7bd', 20, 6.0, 2, 4, x='randInt(-560, 560)', y='-330', angle=100, spread=30, glow='no')]),
    ]
    # meta
    nxt = 'sc_nivel%d' % (L.num + 1) if L.num < 3 else 'sc_final'
    evs.append(ev([C('collision', object=J, **{'with': 'o_meta'}), var('ganado', 'eq', 0)], [
        setv('ganado', 1), C('platOff', object=J), C('setVX', object=J, value=0),
        parts('o_meta', 90, GLOW, 320, 1.4, 4, 60, y=-4), C('shakeCamera', intensity=6, duration=0.4), sfx('triangle', 330, 1320, 1.0, 40)]
        + ([sets('nivel', str(L.num + 1)), C('saveVars', vars='nivel', slot='limbo')] if L.num < 3 else []) + timer('fin')))
    evs += talk('fin_%d' % L.num) if L.num < 3 else []
    evs.append(ev([var('ganado', 'eq', 1), C('every', seconds=0.1)], [parts('o_meta', 3, GLOW, 120, 1.0, 3, -30, y=-4)]))
    evs.append(ev([var('ganado', 'eq', 1), var('dlg', 'eq', 0), var('saliendo', 'eq', 0), C('timerCompare', name='fin', op='ge', value=1.2)],
                  [setv('saliendo', 1), C('changeScene', scene=nxt, transition='fade', duration=1.0)]))
    sc = dict(id=sid, name='Nivel %d: %s' % (L.num, L.name), width=MAP_W, height=MAP_H, background='#070b16', instances=insts, tilemaps=tms,
              effects=[dict(type='vignette', amount=45 + 5 * L.num)], events=evs)
    return sc, len(grass)


# ---------------- menu ----------------
SC_MENU = 'sc_menu'
mcols, mrows = VW // T + 1, VH // T + 1
menu_ground = [-1] * (mcols * mrows)
menu_veg = [-1] * (mcols * mrows)
heights = []
for c in range(mcols):
    hgt = 2 + int(art.vnoise(c * 0.25, 90) * 2.2)
    heights.append(hgt)
    for k in range(hgt):
        menu_ground[(mrows - 1 - k) * mcols + c] = 0
    if art.noise2(c, 1, 91) > 0.25:
        k = art.noise2(c, 2, 92)
        menu_veg[(mrows - 1 - hgt) * mcols + c] = 0 if k < 0.35 else 1 if k < 0.55 else 4 if k < 0.75 else 3 if k < 0.88 else 6
for c in range(mcols):
    for r in range(mrows):
        if menu_ground[r * mcols + c] < 0:
            continue
        s_ = lambda cc, rr: cc < 0 or cc >= mcols or rr >= mrows or (rr >= 0 and menu_ground[rr * mcols + cc] >= 0)
        menu_ground[r * mcols + c] = (0 if s_(c, r - 1) else 1) | (0 if s_(c + 1, r) else 2) | (0 if s_(c, r + 1) else 4) | (0 if s_(c - 1, r) else 8)
menu_insts = [inst('i_cielo_m', 'o_cielo', VW / 2, VH / 2, scaleX=VW / 4, scaleY=VH / 68 + 0.1)] + backdrop('i_mfondo', VW + 64, VH, 2, 0)
mg = random.Random(5)
for c in range(mcols):
    if mg.random() < 0.7:
        menu_insts.append(inst('i_mpasto%d' % c, 'o_pasto', c * T + T / 2 + mg.uniform(-5, 5), (mrows - heights[c]) * T, frame=mg.randrange(4)))
menu_insts += [inst('i_titulo', 'o_titulo', VW / 2, 150),
               inst('i_jugar', 'o_btn_jugar', VW / 2, 280, vars=dict(idx=0)),
               inst('i_seguir', 'o_btn_seguir', VW / 2, 362, vars=dict(idx=1)),
               inst('i_salir', 'o_btn_salir', VW / 2, 444, vars=dict(idx=2)),
               inst('i_velo_m', 'o_velo', 170, VH - 4 * T - 40)]
menu_tms = [
    tilemap('tm_m_lejos', 'Base de los montes', 'ts_lejos', 'lejos', mcols, mrows, far_data(mcols, mrows, 2)),
    tilemap('tm_m_veg', 'Vegetación', 'ts_veg', 'vegetacion', mcols, mrows, menu_veg),
    tilemap('tm_m_suelo', 'Suelo', 'ts_roca', 'base', mcols, mrows, menu_ground),
]
BTN = [('o_btn_jugar', 'BotonJugar'), ('o_btn_seguir', 'BotonSeguir'), ('o_btn_salir', 'BotonSalir')]
PLAY_NEW = [sets('nivel', '1'), C('changeScene', scene='sc_nivel1', transition='fade', duration=0.6)]
CONTINUE = [C('loadGame', slot='limbo'), C('changeScene', scene='sc_nivel{g:nivel}', transition='fade', duration=0.6)]
menu_ev = [
    ev([C('atStart')], [setv('sel', 0), setv('hayPartida', 0), C('hide', object='o_btn_seguir'), setv('saliendo', 0),
                        C('setScale', object='o_titulo', x=0.85, y=0.85), C('tweenScale', object='o_titulo', x=1, y=1, duration=0.8, easing='backOut')]),
    ev([C('atStart'), C('saveExists', slot='limbo')], [setv('hayPartida', 1), C('show', object='o_btn_seguir'), setv('sel', 1)]),
    # teclado: arriba y abajo (saltando "continuar" si no hay partida)
    ev([OR(C('keyPressed', key='arrowdown'), C('keyPressed', key='s'))], [setv('sel', 'min(2, sel + 1 + (sel == 0) * (1 - hayPartida))'), sfx('square', 520, 520, 0.04, 15)]),
    ev([OR(C('keyPressed', key='arrowup'), C('keyPressed', key='w'))], [setv('sel', 'max(0, sel - 1 - (sel == 2) * (1 - hayPartida))'), sfx('square', 520, 520, 0.04, 15)]),
]
for oid, name in BTN:
    menu_ev += [
        ev([C('mouseOn', object=oid), C('objVisible', object=oid), C('cmpNum', a='%s.idx' % name, op='ne', b='sel')],
           [setv('sel', '%s.idx' % name), sfx('square', 520, 520, 0.04, 15)]),
        ev([C('cmpNum', a='%s.idx' % name, op='eq', b='sel'), C('objVar', object=oid, variable='idx', op='ge', value='0')],
           [C('setFrame', object=oid, value=1), C('tweenScale', object=oid, x=1.08, y=1.08, duration=0.12, easing='easeOut')]),
        ev([C('cmpNum', a='%s.idx' % name, op='ne', b='sel')],
           [C('setFrame', object=oid, value=0), C('tweenScale', object=oid, x=1, y=1, duration=0.12, easing='easeOut')]),
    ]
ACTIVATE = OR(C('keyPressed', key='enter'), C('keyPressed', key=' '), C('keyPressed', key='z'))
for idx, acts in ((0, PLAY_NEW), (1, CONTINUE), (2, [C('quitGame')])):
    oid = BTN[idx][0]
    menu_ev.append(ev([var('saliendo', 'eq', 0), OR(dict(type='and', enabled=True, invert=False, params={}, sub=[ACTIVATE, var('sel', 'eq', idx)]),
                                                    dict(type='and', enabled=True, invert=False, params={}, sub=[C('clicked', object=oid), C('objVisible', object=oid)]))],
                      [setv('saliendo', 1), sfx('triangle', 440, 880, 0.3, 30)] + acts))
menu_ev += [
    ev([C('every', seconds=0.05)], [
        parts('o_titulo', 1, '#8ff0ff,#cfe9ff,#9be3bf', 16, 3.5, 2, -8, x='randInt(-480, 480)', y='randInt(-160, 380)', spread=120)]),
    ev([C('every', seconds=0.12)], [parts('o_velo', 1, '#ffd27a,#fff3c9', 12, 0.8, 2, -30, x=11, y=6)]),
]

# ---------------- final: la Luz ----------------
fin_insts = [inst('i_cielo_f', 'o_cielo', VW / 2, VH / 2, scaleX=VW / 4, scaleY=VH / 68 + 0.1, opacity=60)] + backdrop('i_ffondo', VW + 64, VH, 2, 3) + \
            [inst('i_meta_f', 'o_meta', VW / 2 + 160, VH - 6 * T - 70, scaleX=2.2, scaleY=2.2),
             inst('i_jugador_f', J, VW / 2 - 140, VH - 6 * T - 13), inst('i_velo_f', 'o_velo', VW / 2 - 200, VH - 6 * T - 70)] + dialog_insts()
fin_ground = [-1] * (mcols * mrows)
for c in range(mcols):
    for k in range(6):
        fin_ground[(mrows - 1 - k) * mcols + c] = 0 if k < 5 else 1
fin_tms = [tilemap('tm_f_suelo', 'Suelo', 'ts_roca', 'base', mcols, mrows, fin_ground)]
fin_ev = [
    ev([C('atStart')], [C('hide', object='o_caja'), C('hide', object='o_txt_dlg'), C('hide', object='o_etiqueta'), C('hide', object='o_txt_nombre'),
                        C('hide', object='o_sigue'), setv('dlg', 0), setv('muerto', 0), C('platOff', object=J), C('deleteSave', slot='limbo')] + timer('t')),
    ev([C('timerCompare', name='t', op='ge', value=1.0), var('dlg', 'eq', 0), var('visto', 'eq', 0)], [setv('visto', 1)] + open_dlg('final')),
    ev([C('every', seconds=0.04)], [parts('o_meta', 2, '#ffffff,#fff3c9,#8ff0ff', 260, 1.6, 4, -20)]),
    ev([C('every', seconds=0.1)], [parts('o_velo', 1, '#ffd27a,#fff3c9', 12, 0.8, 2, -30, x=11, y=6)]),
]

# ---------------- proyecto ----------------
levels = pl.build_all()
level_scenes = []
grass_total = 0
for L in levels:
    sc, ng = level_scene(L)
    level_scenes.append(sc)
    grass_total += ng
scenes = [dict(id=SC_MENU, name='Menú', width=VW, height=VH, background='#070b16', instances=menu_insts, tilemaps=menu_tms,
               effects=[dict(type='vignette', amount=55)], events=menu_ev)] + level_scenes + \
         [dict(id='sc_final', name='La Luz', width=VW, height=VH, background='#1b2a49', instances=fin_insts, tilemaps=fin_tms,
               effects=[dict(type='vignette', amount=30)], events=fin_ev)]
for o in bg_objs:
    pass
GLOBALS = [dict(name=k, type='normal', value=v) for k, v in (('prueba', 0), ('muertes', 0), ('dlg', 0), ('pg', 0), ('vis', -1), ('tt', 0),
                                                             ('sel', 0), ('hayPartida', 0), ('visto', 0))] + \
          [dict(name=k, type='text', value=v) for k, v in (('nivel', '1'), ('dk', ''), ('full', ''))]
doc = dict(id='p_platform_test', name='Platform Test', version=3,
           settings=dict(width=VW, height=VH, fullscreen=True, autofullscreen=False),
           globals=GLOBALS, fonts=[FONT], sprites=sprites, tilesets=[ts_rock, ts_veg, ts_vine, ts_far, ts_mid, ts_wall, ts_fg],
           objects=objects, scenes=scenes, events=common_events(), layers=layers, shaders=shaders)
strip_comments(doc)
json.dump(doc, open(sys.argv[1], 'w'), ensure_ascii=False, separators=(',', ':'))
print(sys.argv[1], ', '.join('nivel %d: %dx%d tiles' % (L.num, L.W, pl.H) for L in levels), '| pasto:', grass_total, 'matas |',
      len(DIALOGS), 'dialogos')
