#!/usr/bin/env python3
"""Genera examples_rpg.source2d.json: "El Faro de Ámbar", un RPG de accion completo hecho con eventos para el
exportacion ligera (el juego exportado, motor incluido, ocupa menos de 1 MB).

Historia: la Sombra robo las tres brasas del Faro de Ámbar y los barcos de Puerto Musgo ya no encuentran el camino.
La Abuela Rosa te da la vieja espada del abuelo; hay que recuperar una brasa en el Bosque Susurro (y de paso la
linterna), otra en la Gruta Salada (y la llave del faro) y la ultima de la propia Sombra, en lo alto del faro.

- Seis escenas: titulo, pueblo, bosque, gruta, faro (jefe) y final. Paso de una a otra por los bordes del mapa.
- Dialogos con maquina de escribir, nombre de quien habla, paginas, elecciones (Z: si / X: no) y tienda.
- Combate con espada en 4 direcciones, slimes, murcielagos y la Sombra (dos fases, dispara orbes).
- Vida con corazones, monedas, pociones, objetos (espada, linterna, llave), mision secundaria (el gato de Lia),
  guardado automatico al entrar al pueblo y "Continuar" en el titulo.
- Fuente de pixeles de 8x10 (las letras de tools/chess_font.py sin ampliar) y arte de tools/rpg_art.py.

Uso: make_rpg.py <salida.json>"""
import json, math, random, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import rpg_art as A
import chess_font

T = 16
VW, VH = 384, 216
SPEED = 85
rnd = random.Random(11)

# ---------------- fuente de pixeles (8x10) ----------------
G = dict(chess_font.G)
G['>'] = ['#....', '.#...', '..#..', '...#.', '..#..', '.#...', '#....']
G['<'] = ['....#', '...#.', '..#..', '.#...', '..#..', '...#.', '....#']
G['['] = ['###', '#..', '#..', '#..', '#..', '#..', '###']
G[']'] = ['###', '..#', '..#', '..#', '..#', '..#', '###']
G['*'] = ['.....', '#.#.#', '.###.', '#####', '.###.', '#.#.#']
# mayusculas con tilde mas claras que las del ajedrez (la tilde separada de la letra)
G['Á'] = ['...#.', '..#..', '.###.', '#...#', '#####', '#...#', '#...#']
G['É'] = ['...#.', '..#..', '#####', '#....', '####.', '#....', '#####']
G['Ó'] = ['...#.', '..#..', '.###.', '#...#', '#...#', '#...#', '.###.']
G['Ú'] = ['...#.', '..#..', '#...#', '#...#', '#...#', '#...#', '.###.']
FCHARS = ''.join(G.keys())
FW, FH = 8, 10


def glyph_px(c):
    rows = G[c]
    return [['#' if y < len(rows) and x < len(rows[y]) and rows[y][x] == '#' else '.' for x in range(FW)] for y in range(FH)]


FONT = dict(id='f_rpg', name='Pixel 8', kind='pixel', cellW=FW, cellH=FH, chars=FCHARS, spacing=1, mono=False, tint=True,
            glyphs=[dict(pixels=['#ffffff' if g[y][x] == '#' else '' for y in range(FH) for x in range(FW)]) for g in map(glyph_px, FCHARS)])


def adv(c):
    if c not in G:
        return FW // 2
    g = glyph_px(c)
    right = max([x for y in range(FH) for x in range(FW) if g[y][x] == '#'] or [-1])
    return right + 1 if right >= 0 else FW // 2


def text_w(s):
    return sum(adv(c) for c in s) + max(0, len(s) - 1)


def wrap(text, width=350):
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


def each(obj, children, level=0):
    """"Para cada objeto" con sus subeventos"""
    out = [ev([], [], level=level, type='forEach', object=obj)]
    for c in children:
        c = dict(c)
        c['level'] = c.get('level', 0) + level + 1
        out.append(c)
    return out


def var(name, op, value):
    return C('varCmp', variable=name, op=op, value=str(value))


def gv(name, value, op='eq'):
    """variable global de texto"""
    return C('globalVar', variable=name, op=op, value=value)


def setv(name, value, op='set'):
    return C('setVar', variable=name, op=op, value=str(value))


def sets(name, value):
    return C('setGlobal', variable=name, value=value)


def timer(name):
    return [C('timerCtl', name=name, action='reset'), C('timerCtl', name=name, action='start')]


def sfx(wave, f0, f1, dur, vol=35):
    return C('sfx', wave=wave, freqFrom=f0, freqTo=f1, duration=dur, volume=vol)


def parts(obj, count, color, speed=80, life=0.5, size=2, gravity=0, x=0, y=0, spread=360, angle=-90, glow='yes'):
    return C('particles', object=obj, x=x, y=y, count=count, color=color, speed=speed, angle=angle, spread=spread, life=life,
             size=size, gravity=gravity, glow=glow)


ACT = OR(C('keyPressed', key='z'), C('keyPressed', key=' '), C('keyPressed', key='enter'))
J = 'o_jugador'
DLG_ON = var('dlg', 'eq', 1)
DLG_OFF = var('dlg', 'eq', 0)
FREE = [DLG_OFF, var('muerto', 'eq', 0), C('timerCompare', name='dlgfin', op='ge', value=0.25)]

# ---------------- sprites ----------------
sprites = []


def sprite(sid, name, frames):
    sprites.append(dict(id=sid, name=name, width=frames[0].w, height=frames[0].h, frames=[dict(pixels=f.p) for f in frames]))
    return sid


for d in ('abajo', 'arriba', 'lado'):
    sprite('s_heroe_' + d, 'Héroe ' + d, [A.hero(d, 0), A.hero(d, 1)])
    sprite('s_heroe_q' + d, 'Héroe quieto ' + d, [A.hero(d, 0)])
for k in A.NPCS:
    sprite('s_' + k, k.capitalize(), [A.npc(k, 0), A.npc(k, 1)])
sprite('s_gato', 'Gato Pimienta', [A.cat(0), A.cat(1)])
sprite('s_cartel', 'Cartel', [A.sign()])
sprite('s_slime', 'Slime', [A.slime(0), A.slime(1)])
sprite('s_slime_morado', 'Slime de la gruta', [A.slime(0, ('#a46ad8', '#7f48b5', '#e2c2ff', '#4a2a70')), A.slime(1, ('#a46ad8', '#7f48b5', '#e2c2ff', '#4a2a70'))])
sprite('s_murcielago', 'Murciélago', [A.bat(0), A.bat(1)])
sprite('s_sombra', 'La Sombra', [A.shadow_boss(0), A.shadow_boss(1)])
sprite('s_orbe', 'Orbe', [A.orb(0), A.orb(1)])
sprite('s_tajo', 'Tajo', [A.slash()])
sprite('s_moneda', 'Moneda', [A.coin(0), A.coin(1)])
sprite('s_corazon', 'Corazón', [A.heart(True), A.heart(False)])
sprite('s_pocion', 'Poción', [A.potion()])
sprite('s_brasa', 'Brasa', [A.ember(0), A.ember(1)])
sprite('s_cofre', 'Cofre', [A.chest(False), A.chest(True)])
sprite('s_linterna', 'Linterna', [A.lantern()])
sprite('s_llave', 'Llave', [A.key_item()])
sprite('s_espada', 'Espada', [A.sword_icon()])
sprite('s_reja', 'Reja', [A.Img(32, 16).paste(A.gate(False), 0, 0).paste(A.gate(False), 16, 0)])
sprite('s_lampara', 'Lámpara del faro', [A.lamp(False), A.lamp(True)])
sprite('s_aviso', 'Aviso', [A.bubble()])
sprite('s_caja', 'Caja de diálogo', [A.dialog_box(376, 58)])
sprite('s_etiqueta', 'Etiqueta del nombre', [A.name_tag(84, 14)])
sprite('s_panel', 'Panel del HUD', [A.hud_panel(VW, 14)])
sprite('s_faro_grande', 'Faro (portada)', [A.lighthouse_big(False), A.lighthouse_big(True)])
sprite('s_luna', 'Luna', [A.moon()])
sky_im = A.sky(1, 54)
sprite('s_cielo', 'Cielo', [sky_im])


def beam():
    im = A.Img(320, 24)
    for x in range(160, 320):
        h = 2 + (x - 160) * 0.07
        for y in range(int(12 - h), int(12 + h) + 1):
            im.put(x, y, '#fff2b0' if abs(y - 12) < h * 0.5 else '#ffd75e')
    return im


sprite('s_haz', 'Haz de luz', [beam()])

# ---------------- tileset ----------------
TILES = [
    ('hierba', A.grass(0), False), ('hierba2', A.grass(1), False), ('flores', A.grass(2, True), False), ('camino', A.path(0), False),
    ('arena', A.sand(0), False), ('agua', A.water(0), True), ('orilla', A.shore(0), True), ('tablas', A.planks(0), False),
    ('arbol', A.tree(0), True), ('pino', A.pine(0), True), ('arbusto', A.bush(0), True), ('bayas', A.bush(1, True), True),
    ('roca', A.rock(0), True), ('muro', A.wall(0), True), ('ventana', A.wall(window=True), True), ('puerta', A.wall(door=True), True),
    ('tejado', A.roof(0), True), ('alero', A.roof(edge=True), True), ('cumbrera', A.roof(top=True), True), ('valla', A.fence(), True),
    ('pozo', A.well(), True), ('tocon', A.stump(), True), ('hierba_osc', A.grass(3, pal=A.DGRASS), False),
    ('flores_osc', A.grass(4, True, A.DGRASS), False), ('suelo_cueva', A.cave_floor(0), False), ('suelo_cueva2', A.cave_floor(1), False),
    ('charco', A.puddle(0), False), ('pared_cueva', A.cave_wall(0), True), ('cristales', A.cave_wall(1, True), True),
    ('estalagmita', A.stalagmite(), True), ('baldosa', A.stone_floor(0), False), ('alfombra', A.carpet(0), False),
    ('pared_faro', A.lwall(0), True), ('pebetero', A.brazier(True), True), ('roca_cueva', A.rock(1, A.CWALL[1:] + ['#2a2233']), True),
    ('camino2', A.path(1), False), ('arena2', A.sand(1), False), ('agua2', A.water(3), True), ('hierba_osc2', A.grass(5, pal=A.DGRASS), False),
]
TI = {name: i for i, (name, _, _) in enumerate(TILES)}
tileset = dict(id='ts_mundo', name='Mundo', tileSize=T, columns=8, rows=(len(TILES) + 7) // 8,
               tiles=[dict(pixels=im.p, solid=solid, name=name) for name, im, solid in TILES])


def tilemap(tid, name, layer, cols, rows, data):
    return dict(id=tid, name=name, tilesetId='ts_mundo', layer=layer, x=0, y=0, cols=cols, rows=rows, shader='', data=data)


class Map:
    """dos capas: suelo (sin colision) y obstaculos (capa solida)"""

    def __init__(self, cols, rows, ground='hierba'):
        self.cols, self.rows = cols, rows
        self.g = [[ground] * cols for _ in range(rows)]
        self.o = [[None] * cols for _ in range(rows)]

    def gset(self, c, r, t):
        if 0 <= c < self.cols and 0 <= r < self.rows:
            self.g[r][c] = t

    def oset(self, c, r, t):
        if 0 <= c < self.cols and 0 <= r < self.rows:
            self.o[r][c] = t

    def grect(self, c0, r0, c1, r1, t):
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                self.gset(c, r, t)

    def orect(self, c0, r0, c1, r1, t):
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                self.oset(c, r, t)

    def free(self, c, r):
        return 0 <= c < self.cols and 0 <= r < self.rows and self.o[r][c] is None

    def near_free(self, c, r):
        """la celda libre mas cercana (para que nadie empiece dentro de un arbol o una pared)"""
        for rad in range(0, 8):
            for dr in range(-rad, rad + 1):
                for dc in range(-rad, rad + 1):
                    if max(abs(dr), abs(dc)) == rad and self.free(c + dc, r + dr):
                        return c + dc, r + dr
        raise ValueError('sin sitio libre cerca de %d,%d' % (c, r))

    def maps(self, prefix):
        g = [TI[self.g[r][c]] for r in range(self.rows) for c in range(self.cols)]
        o = [TI[self.o[r][c]] if self.o[r][c] else -1 for r in range(self.rows) for c in range(self.cols)]
        return [tilemap('tm_%s_suelo' % prefix, 'Suelo', 'suelo', self.cols, self.rows, g),
                tilemap('tm_%s_obst' % prefix, 'Obstáculos', 'base', self.cols, self.rows, o)]


def px(c, r):
    """centro de la celda en pixeles"""
    return c * T + T / 2, r * T + T / 2


# ---------------- objetos ----------------
objects = []


def obj(oid, name, otype='sprite', scene=None, **k):
    o = dict(id=oid, name=name, type=otype, spriteId=None, hud=False, shader='', capa='base', vars=[], animations=[],
             behaviors=dict(gravity=False, solid=False, oneway=False, collide=False, movement='none', speed=0, jump=0,
                            friction=0.85, airFriction=0.85, bounce=0, mass=1, spin=0),
             hooks=dict(onCreate='', onUpdate=''), flipOnMove=False, comportamientos={})
    if scene:
        o['scene'] = scene
    bh = k.pop('bh', None)
    if bh:
        o['behaviors'].update(bh)
    o.update(k)
    objects.append(o)
    return oid


def anim(aid, name, sid, fps=4, loop=True):
    return dict(id=aid, name=name, spriteId=sid, fps=fps, loop=loop)


def txt(oid, name, content, size=10, color='#f4ecd8', align='left', hud=True, scene=None):
    return obj(oid, name, 'text', scene=scene, hud=hud, text=dict(content=content, size=size, color=color, align=align, bold=False, font='f_rpg'))


SC = dict(titulo='sc_titulo', pueblo='sc_pueblo', bosque='sc_bosque', gruta='sc_gruta', faro='sc_faro', final='sc_final')

hero_anims = [anim('a_' + d, d, 's_heroe_' + d, 6) for d in ('abajo', 'arriba', 'lado')] + \
             [anim('a_q' + d, 'q_' + d, 's_heroe_q' + d, 1) for d in ('abajo', 'arriba', 'lado')]
obj(J, 'Jugador', spriteId='s_heroe_qabajo', animations=hero_anims, flipOnMove=True, hitbox=dict(w=10, h=10),
    bh=dict(collide=True, movement='topdown', speed=SPEED), behaviorList=[dict(type='flash')])
npc_anims = [anim('a_' + k, k, 's_' + k, 2) for k in A.NPCS] + [anim('a_gato', 'gato', 's_gato', 3), anim('a_cartel', 'cartel', 's_cartel', 1)]
obj('o_aldeano', 'Aldeano', spriteId='s_rosa', animations=npc_anims, bh=dict(solid=True), hitbox=dict(w=14, h=14),
    vars=[dict(name='quien', value='rosa'), dict(name='anim', value='rosa')])
obj('o_slime', 'Slime', spriteId='s_slime', animations=[anim('a_slime', 'salta', 's_slime', 3), anim('a_slime_m', 'morado', 's_slime_morado', 3)],
    hitbox=dict(w=12, h=10), bh=dict(collide=True, friction=0.82, airFriction=0.82),
    behaviorList=[dict(type='health', max=3, start=''), dict(type='flash')], vars=[dict(name='aturdido', value=0)])
obj('o_murcielago', 'Murcielago', spriteId='s_murcielago', animations=[anim('a_bat', 'vuela', 's_murcielago', 8)],
    hitbox=dict(w=12, h=10), bh=dict(collide=True, friction=0.82, airFriction=0.82),
    behaviorList=[dict(type='health', max=2, start=''), dict(type='flash')], vars=[dict(name='aturdido', value=0)])
obj('o_sombra', 'Sombra', scene=SC['faro'], spriteId='s_sombra', animations=[anim('a_sombra', 'flota', 's_sombra', 3)],
    hitbox=dict(w=22, h=26), bh=dict(collide=True, friction=0.85, airFriction=0.85),
    behaviorList=[dict(type='health', max=16, start=''), dict(type='flash')], vars=[dict(name='aturdido', value=0)])
obj('o_orbe', 'Orbe', scene=SC['faro'], spriteId='s_orbe', animations=[anim('a_orbe', 'brilla', 's_orbe', 8)], hitbox=dict(w=6, h=6),
    vars=[dict(name='t', value=0)])
obj('o_tajo', 'Tajo', spriteId='s_tajo', vars=[dict(name='t', value=0)], hitbox=dict(w=16, h=16))
obj('o_moneda', 'Moneda', spriteId='s_moneda', animations=[anim('a_moneda', 'gira', 's_moneda', 5)], hitbox=dict(w=8, h=8))
obj('o_vidita', 'Vidita', spriteId='s_corazon', hitbox=dict(w=8, h=8))
obj('o_brasa', 'Brasa', spriteId='s_brasa', animations=[anim('a_brasa', 'arde', 's_brasa', 5)], hitbox=dict(w=12, h=12),
    vars=[dict(name='n', value=1)])
obj('o_cofre', 'Cofre', spriteId='s_cofre', bh=dict(solid=True), vars=[dict(name='que', value=''), dict(name='abierto', value=0)])
obj('o_reja', 'Reja', scene=SC['pueblo'], spriteId='s_reja', bh=dict(solid=True))
obj('o_lampara', 'Lampara', scene=SC['faro'], spriteId='s_lampara', bh=dict(solid=True))
obj('o_aviso', 'Aviso', spriteId='s_aviso', behaviorList=[dict(type='sine', axis='y', amplitude=1.5, period=0.8, phase=0)])
# HUD y dialogos (en todas las escenas de juego)
obj('o_panel', 'Panel', spriteId='s_panel', hud=True)
obj('o_corazon', 'Corazon', spriteId='s_corazon', hud=True, vars=[dict(name='idx', value=1)])
obj('o_ico_moneda', 'IcoMoneda', spriteId='s_moneda', hud=True)
obj('o_ico_brasa', 'IcoBrasa', spriteId='s_brasa', hud=True)
obj('o_ico_pocion', 'IcoPocion', spriteId='s_pocion', hud=True)
obj('o_ico_espada', 'IcoEspada', spriteId='s_espada', hud=True)
obj('o_ico_linterna', 'IcoLinterna', spriteId='s_linterna', hud=True)
obj('o_ico_llave', 'IcoLlave', spriteId='s_llave', hud=True)
txt('o_txt_monedas', 'TxtMonedas', '{g:monedas}')
txt('o_txt_brasas', 'TxtBrasas', '{g:brasas}/3')
txt('o_txt_pociones', 'TxtPociones', '{g:pociones}')
txt('o_txt_zona', 'TxtZona', '', color='#c9bfa3', align='right')
obj('o_caja', 'Caja', spriteId='s_caja', hud=True)
obj('o_etiqueta', 'Etiqueta', spriteId='s_etiqueta', hud=True)
txt('o_txt_dlg', 'TxtDialogo', '', color='#f4ecd8')
txt('o_txt_nombre', 'TxtNombre', '', color='#ffd75e', align='center')
txt('o_txt_mas', 'TxtSigue', '>', color='#ffd75e')
# portada y final
obj('o_cielo', 'Cielo', spriteId='s_cielo', capa='fondo')
obj('o_faro_grande', 'FaroGrande', spriteId='s_faro_grande')
obj('o_haz', 'Haz', spriteId='s_haz', bh=dict(spin=38), opacity=40)
obj('o_luna', 'Luna', spriteId='s_luna')
txt('o_titulo', 'Titulo', 'El Faro de Ámbar', size=20, color='#ffd75e', align='center', hud=False)
txt('o_subtitulo', 'Subtitulo', 'Una aventura de 1 MB', size=10, color='#c9bfa3', align='center', hud=False)
txt('o_txt_nueva', 'TxtNueva', 'Z  Nueva partida', size=10, color='#f4ecd8', align='left', hud=False)
txt('o_txt_continuar', 'TxtContinuar', 'C  Continuar', size=10, color='#f4ecd8', align='left', hud=False)
txt('o_txt_controles', 'TxtControles', 'Flechas o WASD: mover    Z o Espacio: hablar / atacar    X: poción', size=10, color='#e8e2f4',
    align='center', hud=False)
objects[-1]['outline'] = dict(width=1, color='#1b1830')
txt('o_txt_final', 'TxtFinal', '', size=10, color='#f4ecd8', align='center', hud=False)

# ---------------- dialogos ----------------
DIALOGS = {}
NAMES = dict(rosa='Abuela Rosa', bruno='Bruno', lia='Lía', tomas='Tomás', mateo='Mateo', gato='Pimienta', sombra='La Sombra', cartel='Cartel', yo='Tú')


def dlg(key, pages, choice=False, then=()):
    """pages: lista de (quien, texto); then: subeventos (condiciones, acciones) que se ejecutan al cerrarlo"""
    DIALOGS[key] = dict(pages=[(NAMES.get(q, q), wrap(t)) for q, t in pages], choice=choice, then=list(then))


def open_dlg(key):
    return [sets('dk', key), setv('pg', 0), setv('vis', -1), setv('dlg', 1), setv('resp', 1),
            C('show', object='o_caja'), C('show', object='o_txt_dlg'), C('setText', object='o_txt_dlg', text=''),
            C('setMoveSpeed', object=J, value=0), C('stopMove', object=J)]


def give_msg(key, text, then=()):
    dlg(key, [('', text)], then=then)


dlg('intro', [
    ('rosa', '¡Por fin despiertas! Anoche la Sombra subió al Faro de Ámbar y se llevó sus tres brasas. Sin su luz, los barcos no encuentran el puerto.'),
    ('rosa', 'Una brasa cayó en el Bosque Susurro, al este. Otra rodó hasta la Gruta Salada, al oeste. La tercera la guarda la Sombra en lo alto del faro.'),
    ('rosa', 'Toma la vieja espada de tu abuelo. Con Z o Espacio hablas con la gente y atacas. Con X bebes una poción si tienes.'),
    ('rosa', 'Bruno vende pociones en la tienda. Y si ves a Lía, pregúntale por su gato... lleva dos días sin aparecer.'),
], then=[([], [setv('rosa', 1), setv('espada', 1), sfx('square', 520, 1040, 0.25)]),
         ([], open_dlg('espada'))])
give_msg('espada', '¡Conseguiste la Espada del Abuelo! Z o Espacio para atacar.')
dlg('rosa_bosque', [('rosa', 'El Bosque Susurro está al este, siguiendo el camino. Cuidado con los slimes: saltan pero no muerden fuerte.')])
dlg('rosa_gruta', [('rosa', 'Con la linterna ya puedes entrar a la Gruta Salada. Mateo vigila la entrada, al oeste del pueblo.')])
dlg('rosa_faro', [('rosa', 'La reja del muelle lleva al faro. Si tienes la llave, ábrela. La Sombra te espera arriba... ten valor.')])
dlg('rosa_falta', [('rosa', 'Ya tienes {g:brasas} de 3 brasas. ¡Sigue así!')])
dlg('rosa_fin', [('rosa', '¡Venciste a la Sombra! Lleva las tres brasas a la lámpara del faro y enciéndela.')])
dlg('rosa_gracias', [('rosa', 'Mira el mar: los barcos ya vuelven a casa. Gracias por devolvernos la luz.')])
dlg('revivir', [('rosa', 'Te encontré tirado en el camino y te traje a casa. Descansa... perdiste la mitad de tus monedas, pero no el ánimo.')])
dlg('tienda', [('bruno', '¡Bienvenido a la tienda de Bruno! Mis pociones curan tres corazones.'),
               ('bruno', 'Tienes {g:monedas} monedas. ¿Compras una poción por 10?   Z: sí   X: no')], choice=True,
    then=[([var('resp', 'eq', 1), var('monedas', 'ge', 10)], [setv('monedas', -10, 'add'), setv('pociones', 1, 'add'), sfx('square', 660, 990, 0.18)] + open_dlg('compra_ok')),
          ([var('resp', 'eq', 1), var('monedas', 'lt', 10)], open_dlg('compra_no')),
          ([var('resp', 'eq', 0)], open_dlg('compra_adios'))])
dlg('compra_ok', [('bruno', '¡Gracias! Ahora tienes {g:pociones} pociones. Bébelas con X cuando te falte vida.')])
dlg('compra_no', [('bruno', 'Te faltan monedas, amigo. Los slimes y los murciélagos siempre llevan algunas encima.')])
dlg('compra_adios', [('bruno', 'Cuando quieras. Las pociones no se van a ningún lado.')])
dlg('lia_pide', [('lia', 'Mi gata Pimienta se fue al Bosque Susurro y no vuelve... Es naranja y muy curiosa.'),
                 ('lia', 'Si la encuentras, ¿me la traes? Te daré algo bonito.')])
dlg('lia_gracias', [('lia', '¡Pimienta! ¡Volviste! Gracias, gracias, gracias.'),
                    ('lia', 'Toma, este amuleto con forma de corazón era de mi mamá. Dice que da fuerzas.')],
    then=[([], [setv('gato', 2), setv('vidaMax', 1, 'add'), setv('vida', 'vidaMax'), setv('monedas', 15, 'add'),
                C('create', object='o_aldeano', x=px(34, 10)[0], y=px(34, 10)[1]), C('setObjVar', object='o_aldeano', variable='quien', value='gato'),
                C('setAnim', object='o_aldeano', name='gato'), sfx('triangle', 600, 1200, 0.3)] + open_dlg('amuleto'))])
give_msg('amuleto', '¡Tu vida máxima sube a {g:vidaMax} corazones! Y Lía te dio 15 monedas.')
dlg('lia_feliz', [('lia', 'Pimienta no se separa de mí. ¡Suerte con la Sombra!')])
dlg('gato', [('gato', '¡Miau! (Pimienta ronronea y se frota contra tus botas.)')])
dlg('gato_encontrado', [('gato', '¡Miau!'), ('yo', 'Así que aquí estabas, Pimienta. Vamos, Lía te está buscando.')],
    then=[([C('objVar', object='o_aldeano', variable='quien', op='eq', value='gato')],
           [setv('gato', 1), parts('o_aldeano', 14, '#ffd75e,#ffffff', 60, 0.5), C('removeObj', object='o_aldeano'), sfx('triangle', 800, 1300, 0.2)])])
dlg('tomas1', [('tomas', 'Llevo cuarenta años pescando y nunca vi el faro apagado. Sin luz, ni las gaviotas saben volver.'),
               ('tomas', 'Toma unas monedas para el viaje. Y recuerda: en la gruta, los murciélagos no ven bien de frente.')],
    then=[([], [setv('tomas', 1), setv('monedas', 5, 'add'), sfx('square', 880, 1320, 0.12)])])
dlg('tomas2', [('tomas', 'Dicen que la Sombra se vuelve loca cuando le quedan pocas fuerzas. Muévete sin parar y no la dejes apuntarte.')])
dlg('mateo_no', [('mateo', 'Alto ahí. La Gruta Salada es oscura como la boca de un lobo. Sin linterna no te dejo pasar.')])
dlg('mateo_si', [('mateo', '¿Esa es la linterna del bosque? Bueno, con eso sí. Pasa, pero vuelve entero.')],
    then=[([C('objVar', object='o_aldeano', variable='quien', op='eq', value='mateo')],
           [setv('mateo', 1), C('tweenPos', object='o_aldeano', x=px(1, 11)[0], y=px(1, 11)[1], duration=0.6, easing='easeOut')])])
dlg('mateo_suerte', [('mateo', 'Si oyes aleteos, es que vienen los murciélagos. Golpéalos y no te detengas.')])
dlg('cartel_bosque', [('cartel', 'Este: Bosque Susurro.   Oeste: Gruta Salada.   Sur: muelle y Faro de Ámbar.')])
dlg('cartel_casa', [('cartel', 'Casa de la Abuela Rosa. Se aceptan visitas y se regalan galletas.')])
dlg('reja_cerrada', [('', 'La reja del muelle está cerrada con un candado oxidado. Hace falta la llave del faro.')])
dlg('reja_abre', [('', 'Usas la llave del faro. El candado cae al agua con un plop. ¡La reja está abierta!')],
    then=[([], [setv('reja', 1), C('removeObj', object='o_reja'), sfx('square', 300, 900, 0.3), C('shakeCamera', intensity=2, duration=0.2)])])
give_msg('linterna', '¡Conseguiste la Linterna! Ahora Mateo te dejará entrar a la Gruta Salada.')
give_msg('llave', '¡Conseguiste la Llave del Faro! Abre la reja del muelle, al sur del pueblo.')
give_msg('tesoro', '¡El cofre tenía 25 monedas!')
give_msg('brasa1', '¡Recuperaste la primera brasa del faro! Brasas: {g:brasas} de 3.')
give_msg('brasa2', '¡Recuperaste la segunda brasa! Brasas: {g:brasas} de 3.')
give_msg('brasa3', '¡La última brasa! Llévalas a la lámpara, arriba del todo.')
dlg('sombra_intro', [('sombra', '¿Un niño con una espada vieja? Las brasas del faro ahora son mías. La noche es mía.'),
                     ('sombra', 'Sin luz, nadie vuelve a casa. ¡Y tú tampoco!')])
dlg('sombra_fin', [('sombra', 'No... la luz... otra vez la luz...'), ('', 'La Sombra se deshace en humo. Algo brilla donde estaba.')])
dlg('lampara_no', [('', 'La gran lámpara del faro está fría. Le faltan brasas: tienes {g:brasas} de 3.')])
dlg('lampara_si', [('', 'Colocas las tres brasas en la lámpara...')],
    then=[([], [setv('final', 1), C('setFrame', object='o_lampara', value=1), C('shakeCamera', intensity=4, duration=1.2),
                parts('o_lampara', 80, '#ffd75e,#fff2b0,#ffb43a,#ffffff', 220, 1.4, 3), sfx('triangle', 220, 880, 1.2, 45)] + timer('fin'))])

# ---------------- eventos comunes: dialogos y HUD ----------------
common = []
# avanzar pagina (o mostrarla entera si aun se esta escribiendo)
common.append(ev([DLG_ON, ACT], []))
common.append(ev([C('timerCompare', name='tw', op='ge', value='tt')], [setv('pg', 1, 'add'), setv('resp', 1), sfx('square', 330, 330, 0.04, 20)], level=1))
common.append(ev([C('timerCompare', name='tw', op='lt', value='tt')], [C('setText', object='o_txt_dlg', text='{g:full}'), setv('tt', 0)], level=1))
# elegir "no" en las preguntas
common.append(ev([DLG_ON, C('keyPressed', key='x'), var('elige', 'eq', 1), C('timerCompare', name='tw', op='ge', value='tt')],
                 [setv('resp', 0), setv('pg', 1, 'add'), sfx('square', 220, 160, 0.06, 20)]))
for key, d in DIALOGS.items():
    pages = d['pages']
    for i, (who, text) in enumerate(pages):
        last_choice = d['choice'] and i == len(pages) - 1
        acts = [setv('vis', i), sets('full', text), C('typewriter', object='o_txt_dlg', text=text, speed=45),
                setv('tt', round(len(text) / 45 + 0.05, 3)), setv('elige', 1 if last_choice else 0)] + timer('tw')
        if who:
            acts += [C('setText', object='o_txt_nombre', text=who), C('show', object='o_etiqueta'), C('show', object='o_txt_nombre')]
        else:
            acts += [C('hide', object='o_etiqueta'), C('hide', object='o_txt_nombre')]
        common.append(ev([DLG_ON, gv('dk', key), var('pg', 'eq', i), var('vis', 'ne', i)], acts))
    close = [setv('dlg', 0), C('hide', object='o_caja'), C('hide', object='o_txt_dlg'), C('hide', object='o_etiqueta'),
             C('hide', object='o_txt_nombre'), C('hide', object='o_txt_mas'), C('setMoveSpeed', object=J, value=SPEED)] + timer('dlgfin')
    common.append(ev([DLG_ON, gv('dk', key), var('pg', 'ge', len(pages))], close))
    for conds, acts in d['then']:
        common.append(ev(conds, acts, level=1))
# flechita de "sigue" cuando la pagina ya se escribio entera
common.append(ev([DLG_ON, C('timerCompare', name='tw', op='ge', value='tt')], [C('show', object='o_txt_mas')]))
common.append(ev([DLG_ON, C('timerCompare', name='tw', op='lt', value='tt')], [C('hide', object='o_txt_mas')]))
# HUD
common += each('o_corazon', [
    ev([C('cmpNum', a='Corazon.idx', op='le', b='vida')], [C('setFrame', object='o_corazon', value=0)]),
    ev([C('cmpNum', a='Corazon.idx', op='gt', b='vida')], [C('setFrame', object='o_corazon', value=1)]),
    ev([C('cmpNum', a='Corazon.idx', op='gt', b='vidaMax')], [C('hide', object='o_corazon')]),
    ev([C('cmpNum', a='Corazon.idx', op='le', b='vidaMax')], [C('show', object='o_corazon')]),
])
for flag, o in (('espada', 'o_ico_espada'), ('linterna', 'o_ico_linterna'), ('llave', 'o_ico_llave')):
    common.append(ev([var(flag, 'ge', 1)], [C('show', object=o)]))
    common.append(ev([var(flag, 'lt', 1)], [C('hide', object=o)]))


# ---------------- piezas de las escenas de juego ----------------
def hud_insts(zone):
    out = [inst('i_panel', 'o_panel', VW / 2, 7)]
    for i in range(6):
        out.append(inst('i_cor%d' % i, 'o_corazon', 10 + i * 10, 7, vars=dict(idx=i + 1)))
    out += [inst('i_imon', 'o_ico_moneda', 82, 7), inst('i_tmon', 'o_txt_monedas', 89, 7),
            inst('i_ibra', 'o_ico_brasa', 122, 7, scaleX=0.75, scaleY=0.75), inst('i_tbra', 'o_txt_brasas', 130, 7),
            inst('i_ipoc', 'o_ico_pocion', 166, 7), inst('i_tpoc', 'o_txt_pociones', 173, 7),
            inst('i_iesp', 'o_ico_espada', 200, 7), inst('i_ilin', 'o_ico_linterna', 212, 7), inst('i_ill', 'o_ico_llave', 224, 7),
            inst('i_zona', 'o_txt_zona', VW - 6, 7),
            inst('i_caja', 'o_caja', VW / 2, VH - 34), inst('i_etiq', 'o_etiqueta', 52, VH - 63),
            inst('i_tdlg', 'o_txt_dlg', 16, VH - 33), inst('i_tnom', 'o_txt_nombre', 52, VH - 62),
            inst('i_tmas', 'o_txt_mas', VW - 18, VH - 14), inst('i_aviso', 'o_aviso', -50, -50)]
    return out


def inst(iid, oid, x, y, **k):
    d = dict(id=iid, objectId=oid, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
    d.update(k)
    return d


def scene_start(spawns, map_w, map_h, zone):
    """al empezar: camara, HUD oculto lo que toca, posicion segun por donde se entra y animaciones de los aldeanos"""
    out = [ev([C('atStart')], [C('cameraFollow', object=J, smoothing=0.15), C('cameraLimits', mode='scene', x=0, y=0, w=map_w, h=map_h),
                               C('hide', object='o_caja'), C('hide', object='o_txt_dlg'), C('hide', object='o_etiqueta'),
                               C('hide', object='o_txt_nombre'), C('hide', object='o_txt_mas'), C('hide', object='o_aviso'),
                               C('setText', object='o_txt_zona', text=zone), setv('dlg', 0), setv('muerto', 0), setv('saliendo', 0),
                               C('setMoveSpeed', object=J, value=SPEED)] + timer('dlgfin') + timer('atk') + timer('inv'))]
    for name, (x, y, d) in spawns.items():
        out.append(ev([C('atStart'), gv('entrada', name)], [C('setPos', object=J, x=x, y=y), sets('dir', d), C('setAnim', object=J, name='q_' + d)]))
    out += each('o_aldeano', [ev([C('atStart')], [C('setAnim', object='o_aldeano', name='{= Aldeano.anim}')])])
    return out


def player_events():
    out = []
    keys = (('lado', -1, ('arrowleft', 'a')), ('lado', 1, ('arrowright', 'd')), ('arriba', 0, ('arrowup', 'w')), ('abajo', 0, ('arrowdown', 's')))
    for d, fx, ks in keys:
        acts = [sets('dir', d)] + ([setv('fx', fx)] if fx else [])
        out.append(ev([DLG_OFF, OR(C('keyDown', key=ks[0]), C('keyDown', key=ks[1]))], acts))
    out.append(ev([C('objMoving', object=J)], [C('setAnim', object=J, name='{g:dir}')]))
    out.append(ev([C('objMoving', inv=True, object=J)], [C('setAnim', object=J, name='q_{g:dir}')]))
    # aviso "!" sobre quien se puede hablar o abrir
    out.append(ev([], [C('hide', object='o_aviso')]))
    out.append(ev([DLG_OFF, C('distance', object=J, **{'with': 'o_aldeano'}, op='lt', value=24)],
                  [C('setPos', object='o_aviso', x='Aldeano.X', y='Aldeano.Y - 15'), C('show', object='o_aviso')]))
    out.append(ev([DLG_OFF, C('distance', object=J, **{'with': 'o_cofre'}, op='lt', value=22), C('objVar', object='o_cofre', variable='abierto', op='eq', value='0')],
                  [C('setPos', object='o_aviso', x='Cofre.X', y='Cofre.Y - 14'), C('show', object='o_aviso')]))
    # pociones
    out.append(ev(FREE + [C('keyPressed', key='x'), var('pociones', 'gt', 0), C('cmpNum', a='vida', op='lt', b='vidaMax')],
                  [setv('pociones', -1, 'add'), setv('vida', 'min(vidaMax, vida + 3)'), parts(J, 24, '#ff7a96,#ffffff,#e04a6a', 70, 0.6, 2, -60),
                   sfx('triangle', 400, 900, 0.3)]))
    # recoger monedas y corazones
    out.append(ev([C('collision', object=J, **{'with': 'o_moneda'})],
                  [setv('monedas', 1, 'add'), parts('o_moneda', 6, '#ffe27a,#ffffff', 50, 0.3), C('removeObj', object='o_moneda'), sfx('square', 990, 1480, 0.08, 25)]))
    out.append(ev([C('collision', object=J, **{'with': 'o_vidita'})],
                  [setv('vida', 'min(vidaMax, vida + 1)'), parts('o_vidita', 8, '#ff7a96,#ffffff', 50, 0.4), C('removeObj', object='o_vidita'), sfx('triangle', 700, 1100, 0.15)]))
    out.append(ev([C('every', seconds=0.2)], [C('setFrame', object='o_vidita', value=0)]))
    return out


def hurt_events(enemy, dmg=1, remove=False):
    acts = [setv('vida', -dmg, 'add'), C('flash', object=J, duration=1.0), C('shakeCamera', intensity=3, duration=0.2),
            parts(J, 12, '#e5384f,#ffb0bc', 80, 0.4), sfx('sawtooth', 300, 90, 0.25, 40)] + timer('inv')
    if remove:
        acts.append(C('removeObj', object=enemy))
    return [ev([DLG_OFF, var('muerto', 'eq', 0), C('collision', object=J, **{'with': enemy}), C('timerCompare', name='inv', op='ge', value=1.0)], acts)]


def enemy_events(enemy, name, chase=110, speed=38, drops=True, coin=55, heart=18):
    """IA de persecucion, golpes de la espada (con retroceso) y muerte con botin"""
    hit = [C('damage', object=enemy, value=1), C('flash', object=enemy, duration=0.35), C('stopMove', object=enemy),
           C('setVX', object=enemy, value='cos(atan2(%s.Y - Jugador.Y, %s.X - Jugador.X)) * 240' % (name, name)),
           C('setVY', object=enemy, value='sin(atan2(%s.Y - Jugador.Y, %s.X - Jugador.X)) * 240' % (name, name)),
           C('setObjVar', object=enemy, variable='aturdido', value='0.3'), parts(enemy, 8, '#ffffff,#d8f4ff', 90, 0.25), sfx('square', 180, 90, 0.08, 35)]
    death = [ev([C('isDead', object=enemy)], [parts(enemy, 22, '#ffffff,#c3f6ff,#a6f0a0', 110, 0.5, 2), sfx('sawtooth', 400, 100, 0.25, 35),
                                               setv('sx', '%s.X' % name), setv('sy', '%s.Y' % name)])]
    if drops:
        death.append(ev([C('random', percent=coin)], [C('create', object='o_moneda', x='sx', y='sy')], level=1))
        death.append(ev([C('random', percent=heart), C('cmpNum', a='vida', op='lt', b='vidaMax')], [C('create', object='o_vidita', x='sx + 6', y='sy')], level=1))
    death.append(ev([], [C('removeObj', object=enemy)], level=1))
    body = [
        ev([C('objVar', object=enemy, variable='aturdido', op='gt', value='0')], [C('addObjVar', object=enemy, variable='aturdido', value='-TiempoDelta()')]),
        ev([C('objVar', object=enemy, variable='aturdido', op='le', value='0'), DLG_OFF, var('muerto', 'eq', 0),
            C('distance', object=enemy, **{'with': J}, op='lt', value=chase)], [C('moveToObj', object=enemy, target=J, speed=speed)]),
        ev([C('objVar', object=enemy, variable='aturdido', op='le', value='0'), C('distance', object=enemy, **{'with': J}, op='ge', value=chase)],
           [C('stopMove', object=enemy)]),
        ev([DLG_ON], [C('stopMove', object=enemy)]),
        ev([C('collision', object='o_tajo', **{'with': enemy}), C('isFlashing', inv=True, object=enemy)], hit),
    ]
    return each(enemy, body) + death + hurt_events(enemy)


def attack_events():
    """al final de cada escena: asi la tecla que abre un dialogo no lanza tambien un golpe"""
    out = [ev(FREE + [var('espada', 'eq', 1), ACT, C('timerCompare', name='atk', op='ge', value=0.3)],
              timer('atk') + [sfx('sawtooth', 900, 300, 0.09, 22)])]
    for cond, dx, dy, ang in ((gv('dir', 'lado'), 13, 0, 0), (gv('dir', 'arriba'), 0, -13, 270), (gv('dir', 'abajo'), 0, 13, 90)):
        if dx:
            out.append(ev([cond, var('fx', 'eq', 1)], [C('create', object='o_tajo', x='Jugador.X + 13', y='Jugador.Y'), C('setAngle', object='o_tajo', value=0)], level=1))
            out.append(ev([cond, var('fx', 'eq', -1)], [C('create', object='o_tajo', x='Jugador.X - 13', y='Jugador.Y'), C('setAngle', object='o_tajo', value=180)], level=1))
        else:
            out.append(ev([cond], [C('create', object='o_tajo', x='Jugador.X + %d' % dx, y='Jugador.Y + %d' % dy), C('setAngle', object='o_tajo', value=ang)], level=1))
    out.append(ev([], [C('addObjVar', object='o_tajo', variable='t', value='TiempoDelta()')]))
    out.append(ev([C('objVar', object='o_tajo', variable='t', op='gt', value='0.14')], [C('removeObj', object='o_tajo')]))
    return out


def death_events():
    return [
        ev([var('vida', 'le', 0), var('muerto', 'eq', 0)],
           [setv('muerto', 1), C('hide', object=J), C('setMoveSpeed', object=J, value=0), parts(J, 40, '#ffffff,#e5384f,#3f9e5a', 140, 0.8, 2),
            C('shakeCamera', intensity=5, duration=0.4), sfx('sawtooth', 400, 60, 0.8, 45)] + timer('muerte')),
        ev([var('muerto', 'eq', 1), var('saliendo', 'eq', 0), C('timerCompare', name='muerte', op='ge', value=1.6)],
           [setv('saliendo', 1), setv('vida', 'vidaMax'), setv('monedas', 'floor(monedas / 2)'), setv('revivio', 1), sets('entrada', 'casa'),
            C('changeScene', scene=SC['pueblo'], transition='fade', duration=0.6)]),
    ]


def talk(quien, key, extra=()):
    return ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_aldeano'}, op='lt', value=24),
                      C('objVar', object='o_aldeano', variable='quien', op='eq', value=quien)] + list(extra), open_dlg(key))


def chest_events(que, flag, key, give):
    return [
        ev([C('atStart'), var(flag, 'eq', 1), C('objVar', object='o_cofre', variable='que', op='eq', value=que)],
           [C('setFrame', object='o_cofre', value=1), C('setObjVar', object='o_cofre', variable='abierto', value='1')]),
        ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_cofre'}, op='lt', value=22),
                   C('objVar', object='o_cofre', variable='que', op='eq', value=que), C('objVar', object='o_cofre', variable='abierto', op='eq', value='0')],
           [C('setFrame', object='o_cofre', value=1), C('setObjVar', object='o_cofre', variable='abierto', value='1'), setv(flag, 1),
            parts('o_cofre', 26, '#ffd75e,#ffffff,#fff2b0', 90, 0.6, 2, -40), sfx('triangle', 520, 1560, 0.4)] + give + open_dlg(key)),
    ]


def ember_events(n):
    flag = 'b%d' % n
    return [
        ev([C('atStart'), var(flag, 'eq', 1)], [C('removeObj', object='o_brasa')]),
        ev([C('every', seconds=0.12)], [parts('o_brasa', 1, '#ffb43a,#ffd75e,#f08a24', 26, 0.8, 2, -40)]),
        ev([C('collision', object=J, **{'with': 'o_brasa'})],
           [setv(flag, 1), setv('brasas', 1, 'add'), parts('o_brasa', 50, '#ffd75e,#ffb43a,#ffffff', 160, 0.9, 3), C('removeObj', object='o_brasa'),
            C('shakeCamera', intensity=3, duration=0.3), sfx('triangle', 330, 1320, 0.6, 45)] + open_dlg('brasa%d' % n)),
    ]


def exit_event(cond, entrada, scene):
    """salida por un borde: una sola vez (si no, el cambio de escena volveria a empezar en cada fotograma)"""
    return ev([cond, var('muerto', 'eq', 0), var('saliendo', 'eq', 0)],
              [setv('saliendo', 1), C('setMoveSpeed', object=J, value=0), sets('entrada', entrada),
               C('changeScene', scene=scene, transition='fade', duration=0.4)])


# ================= PUEBLO =================
PW, PH = 40, 28
m = Map(PW, PH)
r = random.Random(3)
for rr in range(PH):
    for c in range(PW):
        k = r.random()
        m.g[rr][c] = 'flores' if k < 0.06 else 'hierba2' if k < 0.35 else 'hierba'
# caminos
m.grect(0, 13, PW - 1, 14, 'camino')
for c in (7, 18, 30):
    m.grect(c, 8, c, 12, 'camino')
m.grect(27, 15, 28, 20, 'camino')
# playa y mar
m.grect(0, 20, PW - 1, 21, 'arena')
for c in range(PW):
    m.gset(c, 22, 'arena2' if c % 3 else 'arena')
m.orect(0, 23, PW - 1, PH - 1, 'agua')
for c in range(PW):
    m.oset(c, 22, 'orilla')
# muelle
m.grect(27, 21, 28, PH - 1, 'tablas')
m.orect(27, 22, 28, PH - 1, None)
for c in range(PW):
    if c in (27, 28):
        continue
# borde de arboles (salvo las salidas)
for c in range(PW):
    for rr in (0, 1):
        m.oset(c, rr, 'arbol' if (c + rr) % 2 == 0 else 'pino')
for rr in range(2, 20):
    for c in (0, PW - 1):
        if rr in (13, 14):
            continue
        m.oset(c, rr, 'arbol')
m.oset(0, 14, 'arbol')        # salida oeste de una sola fila (la tapa Mateo)


def house(c0, r0, door_c):
    m.orect(c0, r0, c0 + 5, r0, 'cumbrera')
    m.orect(c0, r0 + 1, c0 + 5, r0 + 1, 'tejado')
    m.orect(c0, r0 + 2, c0 + 5, r0 + 2, 'alero')
    m.orect(c0, r0 + 3, c0 + 5, r0 + 4, 'muro')
    m.oset(c0 + 1, r0 + 3, 'ventana'); m.oset(c0 + 4, r0 + 3, 'ventana')
    m.oset(door_c, r0 + 4, 'puerta')


house(4, 3, 7)
house(15, 3, 18)
house(27, 3, 30)
# vallas, pozo, arbustos y rocas
for c in range(2, 6):
    m.oset(c, 10, 'valla')
for c in range(10, 13):
    m.oset(c, 10, 'valla')
m.oset(23, 10, 'pozo')
for c, rr in ((13, 5), (24, 6), (34, 6), (36, 9), (3, 17), (11, 17), (16, 18), (34, 17), (22, 17), (37, 16)):
    m.oset(c, rr, r.choice(['arbusto', 'bayas']))
for c, rr in ((25, 16), (8, 18), (36, 3), (12, 2)):
    m.oset(c, rr, 'roca')
pueblo_maps = m.maps('pueblo')
P_SPAWN = {'inicio': (*px(9, 10), 'abajo'), 'casa': (*px(7, 9), 'abajo'), 'de_bosque': (*px(36, 13), 'lado'),
           'de_gruta': (*px(1, 13), 'lado'), 'de_faro': (*px(27, 23), 'arriba')}
P_SPAWN['de_gruta'] = (*px(3, 13), 'lado')
pueblo_insts = [inst('i_j', J, *px(9, 10)),
                inst('i_rosa', 'o_aldeano', *px(10, 9), vars=dict(quien='rosa', anim='rosa')),
                inst('i_bruno', 'o_aldeano', *px(19, 9), vars=dict(quien='bruno', anim='bruno')),
                inst('i_lia', 'o_aldeano', *px(32, 10), vars=dict(quien='lia', anim='lia')),
                inst('i_tomas', 'o_aldeano', *px(25, 21), vars=dict(quien='tomas', anim='tomas')),
                inst('i_mateo', 'o_aldeano', *px(0, 13), vars=dict(quien='mateo', anim='mateo')),
                inst('i_cartel1', 'o_aldeano', *px(35, 12), vars=dict(quien='cartel_este', anim='cartel')),
                inst('i_cartel2', 'o_aldeano', *px(5, 9), vars=dict(quien='cartel_casa', anim='cartel')),
                inst('i_reja', 'o_reja', 28 * T, 25 * T + T / 2)] + hud_insts('Puerto Musgo')
pueblo_ev = scene_start(P_SPAWN, PW * T, PH * T, 'Puerto Musgo') + [
    ev([C('atStart')], [C('saveGame', slot='faro')]),
    ev([C('atStart'), var('mateo', 'eq', 1), C('objVar', object='o_aldeano', variable='quien', op='eq', value='mateo')],
       [C('setPos', object='o_aldeano', x=px(1, 11)[0], y=px(1, 11)[1])]),
    ev([C('atStart'), var('reja', 'eq', 1)], [C('removeObj', object='o_reja')]),
    ev([C('atStart'), var('gato', 'eq', 2)], [C('create', object='o_aldeano', x=px(34, 10)[0], y=px(34, 10)[1]),
                                              C('setObjVar', object='o_aldeano', variable='quien', value='gato'), C('setAnim', object='o_aldeano', name='gato')]),
    ev([C('atStart'), var('rosa', 'eq', 0)], open_dlg('intro')),
    ev([C('atStart'), var('revivio', 'eq', 1)], [setv('revivio', 0)] + open_dlg('revivir')),
] + player_events() + [
    talk('rosa', 'intro', [var('rosa', 'eq', 0)]),
    talk('rosa', 'rosa_bosque', [var('rosa', 'eq', 1), var('linterna', 'eq', 0)]),
    talk('rosa', 'rosa_gruta', [var('linterna', 'eq', 1), var('llave', 'eq', 0)]),
    talk('rosa', 'rosa_faro', [var('llave', 'eq', 1), var('jefe', 'eq', 0)]),
    talk('rosa', 'rosa_fin', [var('jefe', 'eq', 1), var('final', 'eq', 0)]),
    talk('rosa', 'rosa_gracias', [var('final', 'eq', 1)]),
    talk('bruno', 'tienda'),
    talk('lia', 'lia_pide', [var('gato', 'eq', 0)]),
    talk('lia', 'lia_gracias', [var('gato', 'eq', 1)]),
    talk('lia', 'lia_feliz', [var('gato', 'eq', 2)]),
    talk('gato', 'gato'),
    talk('tomas', 'tomas1', [var('tomas', 'eq', 0)]),
    talk('tomas', 'tomas2', [var('tomas', 'eq', 1)]),
    talk('mateo', 'mateo_no', [var('linterna', 'eq', 0)]),
    talk('mateo', 'mateo_si', [var('linterna', 'eq', 1), var('mateo', 'eq', 0)]),
    talk('mateo', 'mateo_suerte', [var('mateo', 'eq', 1)]),
    talk('cartel_este', 'cartel_bosque'),
    talk('cartel_casa', 'cartel_casa'),
    ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_reja'}, op='lt', value=30), var('llave', 'eq', 0)], open_dlg('reja_cerrada')),
    ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_reja'}, op='lt', value=30), var('llave', 'eq', 1)], open_dlg('reja_abre')),
    exit_event(C('objPos', object=J, axis='x', op='gt', value=str(PW * T - 5)), 'de_pueblo', SC['bosque']),
    exit_event(C('objPos', object=J, axis='x', op='lt', value='5'), 'de_pueblo', SC['gruta']),
    exit_event(C('objPos', object=J, axis='y', op='gt', value=str(PH * T - 5)), 'de_pueblo', SC['faro']),
] + death_events() + attack_events()

# ================= BOSQUE =================
BW, BH = 44, 32
m = Map(BW, BH, 'hierba_osc')
r = random.Random(5)
for rr in range(BH):
    for c in range(BW):
        k = r.random()
        m.g[rr][c] = 'flores_osc' if k < 0.05 else 'hierba_osc2' if k < 0.4 else 'hierba_osc'


def noise(c, rr, s):
    return (math.sin(c * 0.41 + s) + math.sin(rr * 0.53 + s * 1.7) + math.sin((c + rr) * 0.23 + s * 0.3)) / 3


# camino: de la entrada (oeste) al claro del cofre (norte) y al altar de la brasa (este)
trail = []
for c in range(0, 22):
    trail.append((c, 14 + int(2 * math.sin(c * 0.3))))
for rr in range(5, 15):
    trail.append((21, rr))
for c in range(21, 41):
    trail.append((c, 14 - int((c - 21) * 0.32)))
for rr in range(16, 27):
    trail.append((10, rr))
for c in range(4, 11):
    trail.append((c, 26))
open_cells = set()
for c, rr in trail:
    for dc in (-1, 0, 1):
        for dr in (-1, 0, 1):
            open_cells.add((c + dc, rr + dr))
    m.gset(c, rr, 'camino'); m.gset(c, rr + 1, 'camino2')
# claros
for cx, cy, rad in ((21, 5, 4), (40, 8, 3), (6, 26, 3), (30, 22, 4), (15, 9, 2)):
    for rr in range(BH):
        for c in range(BW):
            if (c - cx) ** 2 + (rr - cy) ** 2 <= rad * rad:
                open_cells.add((c, rr))
# estanque
for rr in range(19, 25):
    for c in range(30, 36):
        if (c - 32.5) ** 2 / 9 + (rr - 21.5) ** 2 / 6 <= 1:
            m.oset(c, rr, 'agua')
for rr in range(BH):
    for c in range(BW):
        if (c, rr) in open_cells or m.o[rr][c]:
            continue
        border = c < 1 or rr < 1 or c >= BW - 1 or rr >= BH - 1
        if border or noise(c, rr, 1.3) > 0.12:
            m.oset(c, rr, 'pino' if noise(c, rr, 7) > 0 else 'arbol')
        elif r.random() < 0.08:
            m.oset(c, rr, r.choice(['arbusto', 'bayas', 'tocon', 'roca']))
for rr in (13, 14, 15):
    m.oset(0, rr, None)
# el gato, escondido detras de unos arbustos
for c, rr in ((8, 24), (8, 25), (8, 27), (9, 28)):
    m.oset(c, rr, 'bayas')
bosque_maps = m.maps('bosque')
B_SPAWN = {'de_pueblo': (*px(3, 14), 'lado')}
slimes_b = [(14, 13), (21, 9), (24, 4), (18, 4), (28, 12), (34, 10), (37, 7), (30, 17), (10, 20)]
bosque_insts = [inst('i_j', J, *px(1, 14)),
                inst('i_cofre', 'o_cofre', *px(21, 3), vars=dict(que='linterna', abierto=0)),
                inst('i_brasa', 'o_brasa', *px(41, 8), vars=dict(n=1)),
                inst('i_gato', 'o_aldeano', *px(5, 26), vars=dict(quien='gato', anim='gato'))] + \
               [inst('i_sl%d' % i, 'o_slime', *px(*m.near_free(c, rr))) for i, (c, rr) in enumerate(slimes_b)] + hud_insts('Bosque Susurro')
bosque_ev = scene_start(B_SPAWN, BW * T, BH * T, 'Bosque Susurro') + [
    ev([C('atStart'), var('gato', 'ge', 1), C('objVar', object='o_aldeano', variable='quien', op='eq', value='gato')], [C('removeObj', object='o_aldeano')]),
    ev([C('every', seconds=0.25)], [parts(J, 1, '#d8ff7a,#fff2b0', 10, 2.5, 2, -6, x='randInt(-190, 190)', y='randInt(-100, 100)')]),
] + player_events() + [talk('gato', 'gato_encontrado')] + chest_events('linterna', 'c_linterna', 'linterna', [setv('linterna', 1)]) + \
    ember_events(1) + enemy_events('o_slime', 'Slime') + [
    exit_event(C('objPos', object=J, axis='x', op='lt', value='5'), 'de_bosque', SC['pueblo']),
] + death_events() + attack_events()

# ================= GRUTA =================
GW, GH = 40, 30
m = Map(GW, GH, 'suelo_cueva')
r = random.Random(9)
for rr in range(GH):
    for c in range(GW):
        m.g[rr][c] = 'suelo_cueva2' if r.random() < 0.3 else 'suelo_cueva'
open_cells = set()
tunnel = []
for c in range(GW - 1, 24, -1):
    tunnel.append((c, 15))
for rr in range(15, 5, -1):
    tunnel.append((25, rr))
for c in range(25, 6, -1):
    tunnel.append((c, 6 + int(1.5 * math.sin(c * 0.4))))
for rr in range(6, 24):
    tunnel.append((7, rr))
for c in range(7, 20):
    tunnel.append((c, 23))
for rr in range(15, 26):
    tunnel.append((30, rr))
for c, rr in tunnel:
    for dc in (-1, 0, 1):
        for dr in (-1, 0, 1):
            open_cells.add((c + dc, rr + dr))
for cx, cy, rad in ((4, 4, 3), (20, 23, 4), (31, 25, 3), (14, 6, 3), (7, 15, 3)):
    for rr in range(GH):
        for c in range(GW):
            if (c - cx) ** 2 + (rr - cy) ** 2 <= rad * rad:
                open_cells.add((c, rr))
for rr in range(GH):
    for c in range(GW):
        if (c, rr) in open_cells and 0 < c < GW - 1 and 0 < rr < GH - 1:
            if r.random() < 0.04:
                m.gset(c, rr, 'charco')
            elif r.random() < 0.025:
                m.oset(c, rr, 'estalagmita')
            continue
        m.oset(c, rr, 'cristales' if noise(c, rr, 4) > 0.45 else 'pared_cueva')
for rr in (14, 15, 16):
    m.oset(GW - 1, rr, None)
for c, rr in tunnel + [(4, 4), (20, 23), (31, 25)]:
    if m.o[rr][c] == 'estalagmita':
        m.oset(c, rr, None)
gruta_maps = m.maps('gruta')
G_SPAWN = {'de_pueblo': (*px(GW - 4, 15), 'lado')}
bats = [(31, 15), (25, 10), (20, 6), (12, 7), (7, 12), (7, 19), (15, 23), (30, 21)]
slimes_g = [(28, 15), (25, 7), (9, 22)]
gruta_insts = [inst('i_j', J, *px(GW - 2, 15)),
               inst('i_cofre1', 'o_cofre', *px(4, 3), vars=dict(que='llave', abierto=0)),
               inst('i_cofre2', 'o_cofre', *px(31, 26), vars=dict(que='tesoro', abierto=0)),
               inst('i_brasa', 'o_brasa', *px(21, 24), vars=dict(n=2))] + \
              [inst('i_bat%d' % i, 'o_murcielago', *px(*m.near_free(c, rr))) for i, (c, rr) in enumerate(bats)] + \
              [inst('i_slg%d' % i, 'o_slime', *px(*m.near_free(c, rr))) for i, (c, rr) in enumerate(slimes_g)] + hud_insts('Gruta Salada')
gruta_ev = scene_start(G_SPAWN, GW * T, GH * T, 'Gruta Salada') + [
    ev([C('atStart')], [C('setAnim', object='o_slime', name='morado')]),
    ev([C('every', seconds=0.08)], [parts(J, 1, '#ffd75e,#fff2b0', 12, 0.6, 2, -10, x='randInt(-4, 4)', y='randInt(-4, 4)')]),
    ev([C('every', seconds=0.3)], [parts(J, 1, '#7fe0ff,#c3f6ff', 6, 3, 1, 4, x='randInt(-190, 190)', y='randInt(-100, 100)')]),
] + player_events() + chest_events('llave', 'c_llave', 'llave', [setv('llave', 1)]) + \
    chest_events('tesoro', 'c_tesoro', 'tesoro', [setv('monedas', 25, 'add')]) + ember_events(2) + \
    enemy_events('o_slime', 'Slime', coin=60) + enemy_events('o_murcielago', 'Murcielago', chase=115, speed=52, coin=50, heart=25) + [
    exit_event(C('objPos', object=J, axis='x', op='gt', value=str(GW * T - 5)), 'de_gruta', SC['pueblo']),
] + death_events() + attack_events()

# ================= FARO =================
FWm, FHm = 24, 26
m = Map(FWm, FHm, 'baldosa')
for rr in range(FHm):
    for c in range(FWm):
        if c <= 1 or c >= FWm - 2 or rr <= 1:
            m.oset(c, rr, 'pared_faro')
        elif rr >= FHm - 1 and c not in (11, 12):
            m.oset(c, rr, 'pared_faro')
m.grect(11, 2, 12, FHm - 1, 'alfombra')
for c, rr in ((4, 4), (19, 4), (4, 18), (19, 18)):
    m.oset(c, rr, 'pebetero')
for rr in range(2, FHm - 1):
    for c in (2, FWm - 3):
        if (rr + c) % 5 == 0:
            m.oset(c, rr, 'pebetero' if rr % 2 else None)
faro_maps = m.maps('faro')
F_SPAWN = {'de_pueblo': (*px(11.5, FHm - 4), 'arriba')}
faro_insts = [inst('i_j', J, *px(11.5, FHm - 2)), inst('i_lampara', 'o_lampara', 12 * T, 2 * T + T / 2),
              inst('i_sombra', 'o_sombra', 12 * T, 11 * T)] + hud_insts('Faro de Ámbar')
SOMBRA = 'Sombra'
faro_ev = scene_start(F_SPAWN, FWm * T, FHm * T, 'Faro de Ámbar') + [
    ev([C('atStart'), var('jefe', 'eq', 1)], [C('removeObj', object='o_sombra')]),
    ev([C('atStart'), var('jefe', 'eq', 1), var('b3', 'eq', 0)], [C('create', object='o_brasa', x=12 * T, y=11 * T)]),   # si salio sin recogerla
    ev([C('atStart'), var('final', 'eq', 1)], [C('setFrame', object='o_lampara', value=1)]),
    ev([C('atStart'), var('jefe', 'eq', 0)], [setv('fase', 0)] + timer('jm') + timer('ja')),
    ev([var('jefe', 'eq', 0), var('fase', 'eq', 0), C('distance', object=J, **{'with': 'o_sombra'}, op='lt', value=110), DLG_OFF],
       [setv('fase', 1), C('shakeCamera', intensity=2, duration=0.5), sfx('sawtooth', 120, 60, 0.8, 40)] + open_dlg('sombra_intro')),
    ev([C('every', seconds=0.15)], [parts('o_lampara', 1, '#ffd75e,#fff2b0', 20, 1, 2, -30)]),
] + player_events() + [
    # la Sombra: flota de un sitio a otro y dispara orbes (mas rapido y mas orbes con poca vida)
    ev([var('fase', 'ge', 1), var('jefe', 'eq', 0), DLG_OFF, var('muerto', 'eq', 0), C('timerCompare', name='jm', op='ge', value='2.2 - 0.9 * (Sombra.Salud < 8)')],
       timer('jm') + [C('moveTo', object='o_sombra', x='randInt(4 * 16, 20 * 16)', y='randInt(5 * 16, 16 * 16)', speed='60 + 35 * (Sombra.Salud < 8)')]),
    ev([var('fase', 'ge', 1), var('jefe', 'eq', 0), DLG_OFF, var('muerto', 'eq', 0), C('timerCompare', name='ja', op='ge', value='1.7 - 0.6 * (Sombra.Salud < 8)')],
       timer('ja') + [C('fireAt', object='o_sombra', bullet='o_orbe', x='Jugador.X', y='Jugador.Y', speed=95),
                      C('fireAt', object='o_sombra', bullet='o_orbe', x='Jugador.X + 40', y='Jugador.Y - 30', speed=95),
                      C('fireAt', object='o_sombra', bullet='o_orbe', x='Jugador.X - 40', y='Jugador.Y + 30', speed=95), sfx('sine', 500, 200, 0.2, 30)]),
    ev([C('health', object='o_sombra', op='lt', value=8)],
       [C('fireAt', object='o_sombra', bullet='o_orbe', x='Sombra.X', y='Sombra.Y + 100', speed=80),
        C('fireAt', object='o_sombra', bullet='o_orbe', x='Sombra.X', y='Sombra.Y - 100', speed=80),
        C('fireAt', object='o_sombra', bullet='o_orbe', x='Sombra.X + 100', y='Sombra.Y', speed=80),
        C('fireAt', object='o_sombra', bullet='o_orbe', x='Sombra.X - 100', y='Sombra.Y', speed=80)], level=1),
    ev([C('health', object='o_sombra', op='lt', value=8), var('fase', 'eq', 1)],
       [setv('fase', 2), C('setColor', object='o_sombra', color='#ff9a9a'), C('shakeCamera', intensity=4, duration=0.6), sfx('sawtooth', 90, 40, 0.9, 45)]),
    ev([DLG_ON], [C('stopMove', object='o_sombra')]),
    ev([C('every', seconds=0.1)], [parts('o_sombra', 1, '#2a1838,#5a3874', 20, 0.8, 4, -30, glow='no')]),
    ev([], [C('addObjVar', object='o_orbe', variable='t', value='TiempoDelta()')]),
    ev([OR(C('objVar', object='o_orbe', variable='t', op='gt', value='5'), C('touchesTile', object='o_orbe', flag='solid'))],
       [parts('o_orbe', 5, '#c48cff,#ffffff', 40, 0.3), C('removeObj', object='o_orbe')]),
    ev([C('collision', object='o_tajo', **{'with': 'o_orbe'})], [parts('o_orbe', 6, '#c48cff,#ffffff', 60, 0.3), C('removeObj', object='o_orbe')]),
    ev([C('collision', object='o_tajo', **{'with': 'o_sombra'}), C('isFlashing', inv=True, object='o_sombra')],
       [C('damage', object='o_sombra', value=1), C('flash', object='o_sombra', duration=0.45), parts('o_sombra', 12, '#ffffff,#c48cff', 100, 0.3),
        C('shakeCamera', intensity=2, duration=0.12), sfx('square', 160, 70, 0.12, 40)]),
    ev([C('isDead', object='o_sombra')],
       [setv('jefe', 1), setv('fase', 3), setv('sx', 'Sombra.X'), setv('sy', 'Sombra.Y'), parts('o_sombra', 90, '#2a1838,#5a3874,#c48cff,#ffffff', 200, 1.2, 3),
        C('shakeCamera', intensity=6, duration=0.8), sfx('sawtooth', 600, 50, 1.0, 50), C('removeObj', object='o_orbe'), C('removeObj', object='o_sombra'),
        C('create', object='o_brasa', x='sx', y='sy')] + open_dlg('sombra_fin')),
] + hurt_events('o_sombra', 1) + hurt_events('o_orbe', 1, remove=True) + ember_events(3) + [
    ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_lampara'}, op='lt', value=28), var('brasas', 'lt', 3)], open_dlg('lampara_no')),
    ev(FREE + [ACT, C('distance', object=J, **{'with': 'o_lampara'}, op='lt', value=28), var('brasas', 'ge', 3), var('final', 'eq', 0)], open_dlg('lampara_si')),
    ev([var('final', 'eq', 1), var('saliendo', 'eq', 0), C('timerCompare', name='fin', op='ge', value=2.5)],
       [setv('saliendo', 1), C('changeScene', scene=SC['final'], transition='fade', duration=1.2)]),
    exit_event(C('objPos', object=J, axis='y', op='gt', value=str(FHm * T - 5)), 'de_faro', SC['pueblo']),
] + death_events() + attack_events()

# ================= TITULO Y FINAL =================
sea_cols, sea_rows = VW // T + 1, 4
sea = [TI['agua' if (c + rr) % 3 else 'agua2'] for rr in range(sea_rows) for c in range(sea_cols)]
sea_tm = dict(id='tm_mar', name='Mar', tilesetId='ts_mundo', layer='base', x=0, y=VH - sea_rows * T, cols=sea_cols, rows=sea_rows, shader='', data=sea)
shore_tm = dict(id='tm_costa', name='Costa', tilesetId='ts_mundo', layer='base', x=220, y=VH - sea_rows * T - T, cols=10, rows=1, shader='',
                data=[TI['orilla']] * 10)


def backdrop(lit):
    return [inst('i_cielo', 'o_cielo', VW / 2, 108, scaleX=VW, scaleY=4.1, z=-10), inst('i_luna', 'o_luna', 60, 36),
            inst('i_faro', 'o_faro_grande', 300, VH - sea_rows * T - 54, frame=1 if lit else 0)] + \
           ([inst('i_haz', 'o_haz', 300, VH - sea_rows * T - 54 - 41)] if lit else [])


titulo_insts = backdrop(False) + [inst('i_titulo', 'o_titulo', 140, 58), inst('i_sub', 'o_subtitulo', 140, 80),
                                  inst('i_nueva', 'o_txt_nueva', 82, 116), inst('i_cont', 'o_txt_continuar', 82, 132),
                                  inst('i_ctrl', 'o_txt_controles', VW / 2, VH - 8)]
RESET = [setv('vida', 5), setv('vidaMax', 5), setv('monedas', 0), setv('pociones', 0), setv('brasas', 0)] + \
        [setv(k, 0) for k in ('espada', 'linterna', 'llave', 'gato', 'b1', 'b2', 'b3', 'c_linterna', 'c_llave', 'c_tesoro', 'jefe', 'rosa',
                              'tomas', 'mateo', 'reja', 'final', 'muerto', 'revivio', 'dlg', 'fase')] + [sets('entrada', 'inicio'), sets('dir', 'abajo'), setv('fx', 1)]
titulo_ev = [
    ev([C('atStart')], [C('setScale', object='o_titulo', x=0.6, y=0.6), C('tweenScale', object='o_titulo', x=1, y=1, duration=0.9, easing='backOut'),
                        C('hide', object='o_txt_continuar')]),
    ev([C('atStart'), C('saveExists', slot='faro')], [C('show', object='o_txt_continuar')]),
    ev([C('every', seconds=0.18)], [parts('o_luna', 1, '#ffffff,#fff2b0,#cfe6f2', 4, 3, 1, 0, x='randInt(-60, 330)', y='randInt(-30, 60)')]),
    ev([C('every', seconds=0.6)], [C('tweenOpacity', object='o_txt_nueva', value=55, duration=0.3, easing='easeInOut')]),
    ev([C('every', seconds=1.2)], [C('tweenOpacity', object='o_txt_nueva', value=100, duration=0.3, easing='easeInOut')]),
    ev([ACT], RESET + [sfx('triangle', 440, 880, 0.3), C('changeScene', scene=SC['pueblo'], transition='fade', duration=0.8)]),
    ev([C('keyPressed', key='c'), C('saveExists', slot='faro')], [sfx('triangle', 440, 880, 0.3), C('loadGame', slot='faro')]),
]
final_text = (wrap('La luz del Faro de Ámbar vuelve a cruzar el mar. Los barcos encuentran el camino a Puerto Musgo y Pimienta '
                   'duerme al sol en el muelle.', 230) + '\n\nMonedas: {g:monedas}   Pociones: {g:pociones}\nCorazones: {g:vidaMax}\n\n'
              '¡Gracias por jugar!\nZ: volver al título')
final_insts = backdrop(True) + [inst('i_tfin', 'o_txt_final', 136, 104), inst('i_titulo', 'o_titulo', 136, 22)]
final_ev = [
    ev([C('atStart')], [setv('saliendo', 0), C('typewriter', object='o_txt_final', text=final_text, speed=30), C('setText', object='o_titulo', text='Fin')] + timer('t')),
    ev([C('every', seconds=0.7)], [parts('o_faro_grande', 14, '#ffd75e,#ffb43a,#ffffff,#9fd8ff', 130, 1.1, 2, 60,
                                         x='randInt(-150, 40)', y='randInt(-90, -40)')]),
    ev([ACT, C('timerCompare', name='t', op='ge', value=3), var('saliendo', 'eq', 0)],
       [setv('saliendo', 1), C('changeScene', scene=SC['titulo'], transition='fade', duration=1)]),
]

layers = [dict(id='fondo', name='Fondo', z=-5, parallax=1, solid=False), dict(id='suelo', name='Suelo', z=-2, parallax=1, solid=False), dict(id='base', name='Base', z=0, parallax=1, solid=True)]


def scene(sid, name, w, h, bg, insts, tms, events, effects=()):
    return dict(id=sid, name=name, width=w, height=h, background=bg, instances=insts, tilemaps=tms, effects=list(effects), events=events)


scenes = [
    scene(SC['titulo'], 'Título', VW, VH, '#0d0b24', titulo_insts, [sea_tm, shore_tm], titulo_ev, [dict(type='vignette', amount=40)]),
    scene(SC['pueblo'], 'Puerto Musgo', PW * T, PH * T, '#2d6cb0', pueblo_insts, pueblo_maps, pueblo_ev, [dict(type='vignette', amount=25)]),
    scene(SC['bosque'], 'Bosque Susurro', BW * T, BH * T, '#1e4e29', bosque_insts, bosque_maps, bosque_ev, [dict(type='vignette', amount=45)]),
    scene(SC['gruta'], 'Gruta Salada', GW * T, GH * T, '#1d1726', gruta_insts, gruta_maps, gruta_ev, [dict(type='vignette', amount=65)]),
    scene(SC['faro'], 'Faro de Ámbar', FWm * T, FHm * T, '#231a26', faro_insts, faro_maps, faro_ev, [dict(type='vignette', amount=55)]),
    scene(SC['final'], 'Final', VW, VH, '#0d0b24', final_insts, [sea_tm, shore_tm], final_ev, [dict(type='vignette', amount=30)]),
]
GLOBALS = [dict(name=k, type='normal', value=v) for k, v in (
    ('vida', 5), ('vidaMax', 5), ('monedas', 0), ('pociones', 0), ('brasas', 0), ('espada', 0), ('linterna', 0), ('llave', 0), ('gato', 0),
    ('b1', 0), ('b2', 0), ('b3', 0), ('c_linterna', 0), ('c_llave', 0), ('c_tesoro', 0), ('jefe', 0), ('rosa', 0), ('tomas', 0), ('mateo', 0),
    ('reja', 0), ('final', 0), ('muerto', 0), ('revivio', 0), ('dlg', 0), ('pg', 0), ('vis', -1), ('tt', 0), ('resp', 1), ('elige', 0),
    ('fx', 1), ('fase', 0), ('sx', 0), ('sy', 0), ('saliendo', 0))] + \
    [dict(name=k, type='text', value=v) for k, v in (('entrada', 'inicio'), ('dir', 'abajo'), ('dk', ''), ('full', ''))]

doc = dict(id='p_faro_ambar', name='El Faro de Ámbar', version=3,
           settings=dict(width=VW, height=VH, fullscreen=True, autofullscreen=False),
           globals=GLOBALS, fonts=[FONT], sprites=sprites, tilesets=[tileset], objects=objects, scenes=scenes, events=common, layers=layers,
           shaders=[])
json.dump(doc, open(sys.argv[1], 'w'), ensure_ascii=False, separators=(',', ':'))
nev = len(common) + sum(len(s['events']) for s in scenes)
print(sys.argv[1], len(sprites), 'sprites,', len(TILES), 'tiles,', len(objects), 'objetos,', len(scenes), 'escenas,', nev, 'eventos,',
      len(DIALOGS), 'dialogos')
