#!/usr/bin/env python3
"""Genera el juego de ejemplo "Ajedrez" (examples_ajedrez.source2d.json): menu, partida jugador contra jugador
y contra el bot (tres niveles). Todo el juego son eventos normales del editor: el generador de jugadas,
las reglas (enroque, al paso, coronacion, jaque mate, tablas), la busqueda del bot (negamax con poda
alfa-beta y quiescencia) y la interfaz. Las tablas de valores por casilla van como datos (arrays globales).
Uso: python3 tools/make_chess.py [salida.json]"""
import json, os, sys
def strip_comments(doc):
    """los juegos de prueba van sin comentarios en la hoja de eventos"""
    for lst in [doc.get('events', [])] + [sc.get('events', []) for sc in doc.get('scenes', [])]:
        lst[:] = [e for e in lst if e.get('kind') != 'comment']

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chess_art as art
import chess_font

FONT = chess_font.font()   # fuente pixel art de 16x16 para todos los textos (tamanos multiplos de 8 para que se vea nitida)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'examples_ajedrez.source2d.json')

# ---------------------------------------------------------------- datos del motor
# 0x88: casilla = fila * 16 + columna, fila 0 = octava (arriba). Pieza = tipo (1 P, 2 C, 3 A, 4 T, 5 D, 6 R) + 8 si es negra.
MAT = [0, 100, 320, 330, 500, 900, 0]
PST = {
    1: [0, 0, 0, 0, 0, 0, 0, 0, 50, 50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30, 20, 10, 10, 5, 5, 10, 25, 25, 10, 5, 5,
        0, 0, 0, 20, 20, 0, 0, 0, 5, -5, -10, 0, 0, -10, -5, 5, 5, 10, 10, -20, -20, 10, 10, 5, 0, 0, 0, 0, 0, 0, 0, 0],
    2: [-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0, 0, 0, 0, -20, -40, -30, 0, 10, 15, 15, 10, 0, -30, -30, 5, 15, 20, 20, 15, 5, -30,
        -30, 0, 15, 20, 20, 15, 0, -30, -30, 5, 10, 15, 15, 10, 5, -30, -40, -20, 0, 5, 5, 0, -20, -40, -50, -40, -30, -30, -30, -30, -40, -50],
    3: [-20, -10, -10, -10, -10, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 10, 10, 5, 0, -10, -10, 5, 5, 10, 10, 5, 5, -10,
        -10, 0, 10, 10, 10, 10, 0, -10, -10, 10, 10, 10, 10, 10, 10, -10, -10, 5, 0, 0, 0, 0, 5, -10, -20, -10, -10, -10, -10, -10, -10, -20],
    4: [0, 0, 0, 0, 0, 0, 0, 0, 5, 10, 10, 10, 10, 10, 10, 5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
        -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5, 0, 0, 0, 5, 5, 0, 0, 0],
    5: [-20, -10, -10, -5, -5, -10, -10, -20, -10, 0, 0, 0, 0, 0, 0, -10, -10, 0, 5, 5, 5, 5, 0, -10, -5, 0, 5, 5, 5, 5, 0, -5,
        0, 0, 5, 5, 5, 5, 0, -5, -10, 5, 5, 5, 5, 5, 0, -10, -10, 0, 5, 0, 0, 0, 0, -10, -20, -10, -10, -5, -5, -10, -10, -20],
    6: [-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30,
        -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20, -20, -20, -20, -10, 20, 20, 0, 0, 0, 0, 20, 20, 20, 30, 10, 0, 0, 10, 30, 20],
}


def piece_values():
    """PV[pieza*128 + casilla]: valor de la pieza en esa casilla visto por las blancas (material + tabla)"""
    pv = [0] * (15 * 128)
    for t in range(1, 7):
        for r in range(8):
            for f in range(8):
                sq = r * 16 + f
                pv[t * 128 + sq] = MAT[t] + PST[t][r * 8 + f]
                pv[(t + 8) * 128 + sq] = -(MAT[t] + PST[t][(7 - r) * 8 + f])
    return pv


def castle_masks():
    m = [15] * 128
    m[0], m[4], m[7] = 15 & ~8, 15 & ~12, 15 & ~4         # a8, e8, h8
    m[112], m[116], m[119] = 15 & ~2, 15 & ~3, 15 & ~1    # a1, e1, h1
    return m


def start_board():
    b = [0] * 128
    back = [4, 2, 3, 5, 6, 3, 2, 4]
    for f in range(8):
        b[f] = back[f] + 8
        b[16 + f] = 9
        b[96 + f] = 1
        b[112 + f] = back[f]
    return b


# ---------------------------------------------------------------- utilidades para escribir eventos
def cond(t, inv=False, **p):
    return {'type': t, 'invert': inv, 'enabled': True, 'params': p}


def act(t, **p):
    return {'type': t, 'invert': False, 'enabled': True, 'params': p}


def X(e):            # condicion: la expresion es verdadera
    return cond('exprTrue', value=e)


def setv(v, e, op='set'):
    return act('setVar', variable=v, op=op, value=str(e))


def loc(v, e):
    return act('localVar', variable=v, value=str(e))


def push(arr, e):
    return act('arrayPushNum', variable=arr, value=str(e))


def call(name, args='', result=''):
    return act('callFunction', name=name, args=args, result=result)


def ret(e=0):
    return act('returnValue', value=str(e))


BRK = act('breakLoop')


class Events:
    def __init__(self):
        self.list, self.level, self.folder, self.n = [], 0, None, 0

    def _id(self, p):
        self.n += 1
        return '%s%d' % (p, self.n)

    def ev(self, conds=(), acts=(), **extra):
        e = {'id': self._id('ev'), 'enabled': True, 'level': self.level, 'conditions': list(conds), 'actions': list(acts)}
        e.update(extra)
        if self.folder:
            e['folder'] = self.folder
        self.list.append(e)
        return _Nest(self)

    def func(self, name, params='', doc=''):
        if doc:
            self.comment(doc)
        return self.ev(type='function', name=name, params=params)

    def rep(self, count, var='', conds=(), acts=()):
        return self.ev(conds, acts, type='repeat', count=str(count), **({'var': var} if var else {}))

    def whl(self, wconds, acts=()):
        return self.ev((), acts, type='while', whileConds=list(wconds))

    def comment(self, text):
        c = {'id': self._id('nt'), 'kind': 'comment', 'text': text}
        if self.folder:
            c['folder'] = self.folder
        self.list.append(c)

    def group(self, name):
        fid = self._id('fd')
        self.list.append({'id': fid, 'kind': 'folder', 'name': name, 'collapsed': True})
        self.folder = fid


class _Nest:
    def __init__(self, b):
        self.b = b

    def __enter__(self):
        self.b.level += 1

    def __exit__(self, *a):
        self.b.level -= 1


# ---------------------------------------------------------------- objetos
SC_MENU, SC_GAME = 'sc_menu', 'sc_partida'
SPR = {}
sprites = []


def sprite(name, frames, w=16, h=16):
    sid = 's_' + name.lower()
    SPR[name] = sid
    sprites.append({'id': sid, 'name': name, 'width': w, 'height': h, 'frames': frames})


sprite('Piezas', art.piece_frames())
sprite('Marcas', art.mark_frames())
sprite('PromoFondo', art.promo_tile())
for nm, col in (('Negro', '#000000'), ('Panel', '#1f232b'), ('PanelBorde', '#343a47'), ('Linea', '#2f3540'),
                ('MaderaOscura', '#3a271a'), ('MaderaClara', '#7a5638'), ('Cartel', '#232833'), ('CartelBorde', '#c9a66b')):
    sprite(nm, art.solid(col), 2, 2)

BEH = {'gravity': False, 'solid': False, 'oneway': False, 'collide': False, 'movement': 'none', 'speed': 150, 'jump': 620,
       'friction': 0.85, 'airFriction': 0.99, 'bounce': 0, 'mass': 1, 'spin': 0}
O = {}
objects = []


def obj(name, kind='sprite', hud=False, **extra):
    oid = 'o_' + name.lower()
    O[name] = oid
    o = {'id': oid, 'name': name, 'type': kind, 'hud': hud, 'shader': '', 'hooks': {'onCreate': '', 'onUpdate': ''},
         'behaviors': dict(BEH), 'vars': [], 'animations': [], 'flipOnMove': False, 'comportamientos': {}}
    o.update(extra)
    objects.append(o)
    return o


def text_obj(name, content, size, color, align='left'):
    return obj(name, 'text', True, text={'content': content, 'size': size, 'color': color, 'align': align, 'font': FONT['id']})


def button(name, label, w=120, h=38, bg='#3a4150', fg='#ffffff', fs=16):
    return obj(name, 'button', True, button={'text': label, 'fontSize': fs, 'bg': bg, 'fg': fg, 'width': w, 'height': h, 'radius': 8, 'font': FONT['id']})


obj('Pieza', spriteId=SPR['Piezas'], vars=[{'name': 'sq', 'value': -1}])
obj('Marca', spriteId=SPR['Marcas'], vars=[{'name': 'k', 'value': 0}])
obj('Cursor', spriteId=SPR['Marcas'])
obj('PromoOpcion', spriteId=SPR['Piezas'], vars=[{'name': 'pr', 'value': 5}])
obj('PromoFondo', spriteId=SPR['PromoFondo'])
obj('VeloTablero', spriteId=SPR['Negro'])
obj('MarcoExt', spriteId=SPR['MaderaOscura'])
obj('MarcoInt', spriteId=SPR['MaderaClara'])
obj('Deco', spriteId=SPR['Piezas'], behaviorList=[{'type': 'sine', 'axis': 'y', 'amplitude': 3, 'period': 3.2, 'phase': 0}])
obj('Capturada', spriteId=SPR['Piezas'], hud=True)
obj('IconoTurno', spriteId=SPR['Piezas'], hud=True)
obj('PanelBorde', spriteId=SPR['PanelBorde'], hud=True)
obj('Panel', spriteId=SPR['Panel'], hud=True)
obj('Separador', spriteId=SPR['Linea'], hud=True)
obj('Velo', spriteId=SPR['Negro'], hud=True)
obj('CartelBorde', spriteId=SPR['CartelBorde'], hud=True)
obj('Cartel', spriteId=SPR['Cartel'], hud=True)
text_obj('Titulo', 'AJEDREZ', 32, '#f0e6d2')
text_obj('ModoTxt', '', 16, '#9aa3b5')
text_obj('Estado', '', 16, '#f0e6d2')
text_obj('Aviso', '', 16, '#ff6b5e')
text_obj('Etiqueta', '', 16, '#7d8596')
text_obj('Ventaja', '', 16, '#c9a66b', 'right')
text_obj('Jugada', '', 16, '#e6e9ef')
text_obj('Coord', '', 8, '#d8c09a', 'center')
text_obj('FinTitulo', '', 32, '#f0e6d2', 'center')
text_obj('FinSub', '', 16, '#c9cfdb', 'center')
text_obj('MenuTitulo', 'AJEDREZ', 64, '#f0e6d2', 'center')
text_obj('MenuSub', 'Un juego hecho solo con eventos en Source 2D', 16, '#9aa3b5', 'center')
text_obj('MenuPie', 'Haz clic en una pieza para ver sus jugadas · F11 pantalla completa', 16, '#6d7484', 'center')
button('BtnPvP', 'Jugador contra jugador', 300, 48, '#3d63c9')
button('BtnBot', 'Jugar contra el bot', 300, 48, '#3a4150')
button('BtnFacil', 'Fácil', 300, 44, '#2f7d4f')
button('BtnNormal', 'Normal', 300, 44, '#b07a2a')
button('BtnDificil', 'Difícil', 300, 44, '#a8383b')
button('BtnVolver', 'Volver', 300, 40, '#2c313c', '#c9cfdb')
button('BtnDeshacer', 'Deshacer', 112, 38)
button('BtnRendirse', 'Rendirse', 112, 38, '#5a2f33')
button('BtnMenu', 'Menú', 112, 38)
button('BtnRevancha', 'Revancha', 130, 40, '#3d63c9')
button('BtnSalirFin', 'Menú', 130, 40)


def inst(name, x, y, sx=1, sy=1, frame=0, opacity=100, vars=None, z=0):
    d = {'id': 'i_%d' % (len(_insts) + 1), 'objectId': O[name], 'x': x, 'y': y, 'angle': 0, 'scaleX': sx, 'scaleY': sy,
         'opacity': opacity, 'frame': frame, 'z': z}
    if vars:
        d['vars'] = vars
    _insts.append(d)
    return d


# ---------------------------------------------------------------- escenas
tilesets = [{'id': 'ts_tablero', 'name': 'Tablero', 'tileSize': 16, 'columns': 6, 'rows': 1, 'tiles': art.board_tiles()}]

_insts = []
board = [(r * 8 + f) for r in range(8) for f in range(8)]
board_data = [(0 if (r + f) % 2 == 0 else 2) + ((r * 3 + f * 5) % 2) for r in range(8) for f in range(8)]
# marco del tablero: tiras alrededor (los tilemaps se dibujan debajo de las instancias)
for x, y, sx, sy in ((68, 2.5, 67, 1.5), (68, 133.5, 67, 1.5), (2.5, 68, 1.5, 67), (133.5, 68, 1.5, 67)):
    inst('MarcoExt', x, y, sx, sy)
for x, y, sx, sy in ((68, 3.5, 65, 0.5), (68, 132.5, 65, 0.5), (3.5, 68, 0.5, 65), (132.5, 68, 0.5, 65)):
    inst('MarcoInt', x, y, sx, sy)
inst('Cursor', -40, -40, frame=5, opacity=55, z=3)
inst('PanelBorde', 750, 270, 196, 256)
inst('Panel', 750, 270, 194, 254)
inst('Titulo', 582, 52)
inst('ModoTxt', 584, 84)
inst('Separador', 750, 108, 172, 0.5)
inst('IconoTurno', 600, 138, 2, 2, frame=5)
inst('Estado', 626, 138)
inst('Aviso', 626, 168)
inst('Etiqueta', 584, 204, vars={'t': 'Capturas'})
inst('Ventaja', 918, 204)
inst('Separador', 750, 300, 172, 0.5)
inst('Etiqueta', 584, 320, vars={'t': 'Jugadas'})
for i in range(8):
    for c, x in enumerate((584, 630, 740)):
        inst('Jugada', x, 346 + i * 17, vars={'n': i, 'col': c})
inst('BtnDeshacer', 632, 494)
inst('BtnRendirse', 750, 494)
inst('BtnMenu', 868, 494)
game_insts = _insts

_insts = []
inst('Deco', 34, 72, 2, 2, frame=5)
inst('Deco', 206, 72, 2, 2, frame=10)
inst('MenuTitulo', 480, 118)
inst('MenuSub', 480, 174)
inst('BtnPvP', 480, 262)
inst('BtnBot', 480, 324)
inst('BtnFacil', 480, 246, opacity=0)      # ocultos al empezar: aparecen al pulsar "Jugar contra el bot"
inst('BtnNormal', 480, 300, opacity=0)
inst('BtnDificil', 480, 354, opacity=0)
inst('BtnVolver', 480, 414, opacity=0)
inst('MenuPie', 480, 512)
menu_insts = _insts

MENU_SHADER = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
void main() {
  vec4 c = texture2D(u_tex, v_uv);
  vec2 d = v_uv - 0.5;
  float vig = smoothstep(0.9, 0.18, length(d * vec2(1.3, 1.0)));
  float band = fract(v_uv.x * 0.55 - v_uv.y * 0.35 - u_time * 0.06);
  float sweep = 0.07 * smoothstep(0.18, 0.0, abs(band - 0.5));
  gl_FragColor = vec4(c.rgb * (0.45 + 0.55 * vig) + sweep * vig, 1.0);
}
"""
GAME_SHADER = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
void main() {
  vec4 c = texture2D(u_tex, v_uv);
  vec2 d = v_uv - 0.5;
  float vig = smoothstep(1.0, 0.3, length(d * vec2(1.2, 1.0)));
  gl_FragColor = vec4(c.rgb * (0.82 + 0.18 * vig), 1.0);
}
"""
shaders = [{'id': 'sh_menu', 'name': 'Menú: viñeta y brillo', 'code': MENU_SHADER},
           {'id': 'sh_partida', 'name': 'Partida: viñeta suave', 'code': GAME_SHADER}]

CAM = {'zoom': 4, 'smooth': 0, 'limits': 'scene', 'lx': 0, 'ly': 0, 'lw': 240, 'lh': 135}
scenes = [
    {'id': SC_MENU, 'name': 'Menú', 'width': 240, 'height': 135, 'background': '#15171d', 'instances': menu_insts,
     'tilemaps': [{'id': 'tm_menu', 'name': 'Fondo', 'tilesetId': 'ts_tablero', 'x': 0, 'y': -4, 'cols': 15, 'rows': 9, 'shader': '',
                   'data': [4 + (r + c) % 2 for r in range(9) for c in range(15)]}],
     'effects': [], 'shaderCode': '', 'shaderId': 'sh_menu', 'camera': dict(CAM)},
    {'id': SC_GAME, 'name': 'Partida', 'width': 240, 'height': 135, 'background': '#17191f', 'instances': game_insts,
     'tilemaps': [{'id': 'tm_tablero', 'name': 'Tablero', 'tilesetId': 'ts_tablero', 'x': 4, 'y': 4, 'cols': 8, 'rows': 8, 'shader': '',
                   'data': board_data}],
     'effects': [], 'shaderCode': '', 'shaderId': 'sh_partida', 'camera': dict(CAM)},
]


def arr(name, value):
    return {'name': name, 'type': 'array', 'value': value}


globals_ = [
    arr('B', start_board()), arr('B0', start_board()), arr('PV', piece_values()), arr('CRM', castle_masks()),
    arr('DIRS', [-17, -15, 15, 17, -16, -1, 1, 16]), arr('KN', [-33, -31, -18, -14, 14, 18, 31, 33]),
    arr('KS', [116, 4]), arr('MS', []), arr('US', []), arr('LM', []), arr('HM', []), arr('HIS', []), arr('CNT', []),
    {'name': 'side', 'value': 0}, {'name': 'cr', 'value': 15}, {'name': 'ep', 'value': -1}, {'name': 'hm', 'value': 0},
    {'name': 'ev', 'value': 0}, {'name': 'modo', 'value': 0}, {'name': 'nivel', 'value': 2}, {'name': 'fase', 'value': 0},
    {'name': 'sel', 'value': -1}, {'name': 'bm', 'value': 0}, {'name': 'nodes', 'value': 0}, {'name': 'qmax', 'value': 3},
    {'name': 'fin', 'value': 0}, {'name': 'jaque', 'value': 0}, {'name': 'pensar', 'value': 0},
    {'name': 't1', 'type': 'text', 'value': ''}, {'name': 't2', 'type': 'text', 'value': ''},
]

# ---------------------------------------------------------------- eventos
E = Events()
SQX = '12 + (%s & 7) * 16'           # centro de una casilla en el mundo
SQY = '12 + (%s >> 4) * 16'


def sqx(v):
    return SQX % v


def sqy(v):
    return SQY % v


# ================= motor: generador de jugadas =================
E.group('Motor: reglas del ajedrez')
E.comment('Tablero 0x88: casilla = fila * 16 + columna (fila 0 = octava). Una casilla está fuera del tablero si (casilla & 136) != 0.\n'
          'Pieza = tipo (1 peón, 2 caballo, 3 alfil, 4 torre, 5 dama, 6 rey) + 8 si es negra. Jugada = origen + destino * 128 + coronación * 16384 + orden * 131072.')
with E.func('Gen', 's, q', 'Gen(lado, soloCapturas): añade a MS las jugadas del lado (sin comprobar si dejan al rey en jaque).\n'
                            'Las capturas van primero, ordenadas por pieza capturada (la más valiosa antes): así la poda alfa-beta corta más.'):
    E.ev((), [loc('K', '[]'), loc('Q', '[]')])
    with E.rep(64, 'i', (), [loc('sq', '$i + ($i & 56)'), loc('p', '$B[$sq]')]):
        with E.ev([X('$p != 0 && ($p >> 3) == $s')], [loc('t', '$p & 7')]):
            # peones
            with E.ev([X('$t == 1')], [loc('fw', '$s * 32 - 16'), loc('to', '$sq + $fw'), loc('lr', '$s == 0 ? 0 : 7')]):
                with E.ev([X('($to & 136) == 0 && $B[$to] == 0')]):
                    E.ev([X('($to >> 4) == $lr')], [push('K', '$sq + ($to << 7) + (5 << 14) + (47 << 17)'), push('K', '$sq + ($to << 7) + (4 << 14) + (39 << 17)'),
                                                    push('K', '$sq + ($to << 7) + (3 << 14) + (31 << 17)'), push('K', '$sq + ($to << 7) + (2 << 14) + (23 << 17)')])
                    with E.ev([X('($to >> 4) != $lr && $q == 0')], [push('Q', '$sq + ($to << 7)')]):
                        E.ev([X('($sq >> 4) == ($s == 0 ? 6 : 1) && $B[$to + $fw] == 0')], [push('Q', '$sq + (($to + $fw) << 7)')])
                with E.rep(2, 'c', (), [loc('x', '$to - 1 + 2 * $c')]):
                    with E.ev([X('($x & 136) == 0 && (($B[$x] != 0 && ($B[$x] >> 3) != $s) || $x == $ep)')], [loc('v', '$B[$x] == 0 ? 1 : ($B[$x] & 7)')]):
                        E.ev([X('($x >> 4) == $lr')], [push('K', '$sq + ($x << 7) + (5 << 14) + ((47 + $v) << 17)'), push('K', '$sq + ($x << 7) + (4 << 14) + ((39 + $v) << 17)'),
                                                       push('K', '$sq + ($x << 7) + (3 << 14) + ((31 + $v) << 17)'), push('K', '$sq + ($x << 7) + (2 << 14) + ((23 + $v) << 17)')])
                        E.ev([X('($x >> 4) != $lr')], [push('K', '$sq + ($x << 7) + (($v * 8 + 6) << 17)')])
            # caballos
            with E.ev([X('$t == 2')]):
                with E.rep(8, 'd', (), [loc('to', '$sq + $KN[$d]')]):
                    with E.ev([X('($to & 136) == 0')], [loc('v', '$B[$to]')]):
                        E.ev([X('$v == 0 && $q == 0')], [push('Q', '$sq + ($to << 7)')])
                        E.ev([X('$v != 0 && ($v >> 3) != $s')], [push('K', '$sq + ($to << 7) + ((($v & 7) * 8 + 5) << 17)')])
            # alfiles, torres y damas: recorren cada direccion hasta chocar
            with E.ev([X('$t >= 3 && $t <= 5')], [loc('d0', '$t == 4 ? 4 : 0'), loc('nd', '$t == 5 ? 8 : 4'), loc('at', '7 - $t')]):
                with E.rep('$nd', 'j', (), [loc('dir', '$DIRS[$d0 + $j]'), loc('to', '$sq + $dir')]):
                    with E.whl([X('($to & 136) == 0')]):
                        with E.ev([X('$B[$to] != 0')]):
                            E.ev([X('($B[$to] >> 3) != $s')], [push('K', '$sq + ($to << 7) + ((($B[$to] & 7) * 8 + $at) << 17)')])
                            E.ev((), [BRK])
                        E.ev([X('$q == 0')], [push('Q', '$sq + ($to << 7)')])
                        E.ev((), [setv('to', '$dir', 'add')])
            # rey y enroques
            with E.ev([X('$t == 6')]):
                with E.rep(8, 'd', (), [loc('to', '$sq + $DIRS[$d]')]):
                    with E.ev([X('($to & 136) == 0')], [loc('v', '$B[$to]')]):
                        E.ev([X('$v == 0 && $q == 0')], [push('Q', '$sq + ($to << 7)')])
                        E.ev([X('$v != 0 && ($v >> 3) != $s')], [push('K', '$sq + ($to << 7) + ((($v & 7) * 8 + 1) << 17)')])
                with E.ev([X('$q == 0 && $sq == ($s == 0 ? 116 : 4)')], [loc('o', '1 - $s')]):
                    E.ev([X('($cr & ($s == 0 ? 1 : 4)) != 0 && $B[$sq + 1] == 0 && $B[$sq + 2] == 0 && $B[$sq + 3] == 4 + 8 * $s && '
                            'Fn.Att($sq, $o) == 0 && Fn.Att($sq + 1, $o) == 0 && Fn.Att($sq + 2, $o) == 0')], [push('Q', '$sq + (($sq + 2) << 7)')])
                    E.ev([X('($cr & ($s == 0 ? 2 : 8)) != 0 && $B[$sq - 1] == 0 && $B[$sq - 2] == 0 && $B[$sq - 3] == 0 && $B[$sq - 4] == 4 + 8 * $s && '
                            'Fn.Att($sq, $o) == 0 && Fn.Att($sq - 1, $o) == 0 && Fn.Att($sq - 2, $o) == 0')], [push('Q', '$sq + (($sq - 2) << 7)')])
    E.ev((), [act('arraySort', variable='K', order='desc'), act('arrayAppend', to='MS', **{'from': 'K'}), act('arrayAppend', to='MS', **{'from': 'Q'})])

with E.func('Att', 'sq, by', 'Att(casilla, lado): 1 si alguna pieza del lado ataca la casilla.'):
    E.ev([X('$by == 0 ? (($B[$sq + 15] == 1 && (($sq + 15) & 136) == 0) || ($B[$sq + 17] == 1 && (($sq + 17) & 136) == 0)) : '
            '(($B[$sq - 15] == 9 && (($sq - 15) & 136) == 0) || ($B[$sq - 17] == 9 && (($sq - 17) & 136) == 0))')], [ret(1)])
    E.ev((), [loc('kn', '2 + 8 * $by'), loc('kg', '6 + 8 * $by'), loc('qn', '5 + 8 * $by')])
    E.rep(8, 'd', [X('$B[$sq + $KN[$d]] == $kn && (($sq + $KN[$d]) & 136) == 0')], [ret(1)])
    E.rep(8, 'd', [X('$B[$sq + $DIRS[$d]] == $kg && (($sq + $DIRS[$d]) & 136) == 0')], [ret(1)])
    with E.rep(8, 'd', (), [loc('dir', '$DIRS[$d]'), loc('to', '$sq + $dir')]):
        E.whl([X('($to & 136) == 0 && $B[$to] == 0')], [setv('to', '$dir', 'add')])
        E.ev([X('($to & 136) == 0 && ($B[$to] == $qn || $B[$to] == ($d < 4 ? 3 : 4) + 8 * $by)')], [ret(1)])
    E.ev((), [ret(0)])

with E.func('Make', 'm', 'Make(jugada): la hace en el tablero. Guarda en US lo necesario para deshacerla y actualiza la evaluación (ev) sin recorrer el tablero.'):
    E.ev((), [loc('f', '$m & 127'), loc('t', '($m >> 7) & 127'), loc('pr', '($m >> 14) & 7'), loc('p', '$B[$f]'), loc('c', '$B[$t]'), loc('s', '$p >> 3'),
              push('US', '$c'), push('US', '$cr'), push('US', '$ep'), push('US', '$hm'), push('US', '$ev'),
              setv('ev', '$PV[$p * 128 + $t] - $PV[$p * 128 + $f] - $PV[$c * 128 + $t]', 'add'),
              setv('B[$t]', '$p'), setv('B[$f]', 0), setv('hm', '($p & 7) == 1 || $c != 0 ? 0 : $hm + 1')])
    E.ev([X('($p & 7) == 1 && $t == $ep && $c == 0')], [loc('x', '$t + ($s == 0 ? 16 : -16)'), setv('ev', '$PV[$B[$x] * 128 + $x]', 'sub'), setv('B[$x]', 0)])
    E.ev([X('$pr != 0')], [setv('B[$t]', '$pr + 8 * $s'), setv('ev', '$PV[($pr + 8 * $s) * 128 + $t] - $PV[$p * 128 + $t]', 'add')])
    with E.ev([X('($p & 7) == 6')], [setv('KS[$s]', '$t')]):
        E.ev([X('abs($t - $f) == 2')], [loc('rf', '$t > $f ? $f + 3 : $f - 4'), loc('rt', '($f + $t) >> 1'), loc('r', '$B[$rf]'),
                                        setv('B[$rt]', '$r'), setv('B[$rf]', 0), setv('ev', '$PV[$r * 128 + $rt] - $PV[$r * 128 + $rf]', 'add')])
    E.ev((), [setv('ep', '($p & 7) == 1 && abs($t - $f) == 32 ? ($f + $t) >> 1 : -1'), setv('cr', '$cr & $CRM[$f] & $CRM[$t]'), setv('side', '1 - $side')])

with E.func('Unmake', 'm', 'Unmake(jugada): deshace la última jugada hecha con Make.'):
    E.ev((), [setv('side', '1 - $side'), loc('f', '$m & 127'), loc('t', '($m >> 7) & 127'), loc('pr', '($m >> 14) & 7'), loc('s', '$side'), loc('p', '$B[$t]'),
              loc('u', 'len($US) - 5'), setv('ev', '$US[$u + 4]'), setv('hm', '$US[$u + 3]'), setv('ep', '$US[$u + 2]'), setv('cr', '$US[$u + 1]'),
              loc('c', '$US[$u]'), act('arrayResize', variable='US', size='$u')])
    E.ev([X('$pr != 0')], [setv('p', '1 + 8 * $s')])
    E.ev((), [setv('B[$f]', '$p'), setv('B[$t]', '$c')])
    with E.ev([X('($p & 7) == 6')], [setv('KS[$s]', '$f')]):
        E.ev([X('abs($t - $f) == 2')], [loc('rf', '$t > $f ? $f + 3 : $f - 4'), loc('rt', '($f + $t) >> 1'), setv('B[$rf]', '$B[$rt]'), setv('B[$rt]', 0)])
    E.ev([X('($p & 7) == 1 && $t == $ep && $c == 0')], [setv('B[$t + ($s == 0 ? 16 : -16)]', '9 - 8 * $s')])

with E.func('Legal', '', 'Legal(): deja en LM las jugadas legales del lado al que le toca.'):
    E.ev((), [setv('LM', '[]'), loc('st', 'len($MS)'), call('Gen', '$side, 0'), loc('en', 'len($MS)')])
    with E.rep('$en - $st', 'i', (), [loc('m', '$MS[$st + $i]'), call('Make', '$m')]):
        E.ev([X('Fn.Att($KS[1 - $side], $side) == 0')], [push('LM', '$m')])
        E.ev((), [call('Unmake', '$m')])
    E.ev((), [act('arrayResize', variable='MS', size='$st')])

# ================= bot =================
E.group('Bot: búsqueda')
with E.func('Search', 'd, a, b', 'Search(profundidad, alfa, beta): negamax con poda alfa-beta. Devuelve la puntuación para el lado al que le toca.\n'
                                 'Sin jugadas legales: jaque mate (muy negativo, peor cuanto antes) o rey ahogado (0).'):
    E.ev((), [setv('nodes', 1, 'add')])
    E.ev([X('$d <= 0')], [ret('Fn.Quies($a, $b, 0)')])
    E.ev((), [loc('st', 'len($MS)'), call('Gen', '$side, 0'), loc('en', 'len($MS)'), loc('best', -99999), loc('legal', 0)])
    with E.rep('$en - $st', 'i', (), [loc('m', '$MS[$st + $i]'), call('Make', '$m')]):
        with E.ev([X('Fn.Att($KS[1 - $side], $side) == 0')], [setv('legal', 1, 'add'), loc('sc', '-Fn.Search($d - 1, -$b, -$a)')]):
            E.ev([X('$sc > $best')], [setv('best', '$sc')])
            E.ev([X('$sc > $a')], [setv('a', '$sc')])
        E.ev((), [call('Unmake', '$m')])
        E.ev([X('$a >= $b')], [BRK])
    E.ev((), [act('arrayResize', variable='MS', size='$st')])
    E.ev([X('$legal == 0')], [ret('Fn.Att($KS[$side], 1 - $side) ? -40000 - $d : 0')])
    E.ev((), [ret('$best')])

with E.func('Quies', 'a, b, k', 'Quies(alfa, beta, nivel): al final de la búsqueda sigue mirando solo capturas, para no cortar a mitad de un cambio de piezas.'):
    E.ev((), [setv('nodes', 1, 'add'), loc('sp', '$side == 0 ? $ev : -$ev')])
    E.ev([X('$sp >= $b')], [ret('$sp')])
    E.ev([X('$sp > $a')], [setv('a', '$sp')])
    E.ev([X('$k >= $qmax')], [ret('$a')])
    E.ev((), [loc('st', 'len($MS)'), call('Gen', '$side, 1'), loc('en', 'len($MS)')])
    with E.rep('$en - $st', 'i', (), [loc('m', '$MS[$st + $i]')]):
        E.ev([X('($B[($m >> 7) & 127] & 7) == 6')], [act('arrayResize', variable='MS', size='$st'), ret(50000)])
        E.ev((), [call('Make', '$m'), loc('sc', '-Fn.Quies(-$b, -$a, $k + 1)'), call('Unmake', '$m')])
        E.ev([X('$sc >= $b')], [act('arrayResize', variable='MS', size='$st'), ret('$sc')])
        E.ev([X('$sc > $a')], [setv('a', '$sc')])
    E.ev((), [act('arrayResize', variable='MS', size='$st'), ret('$a')])

with E.func('Think', '', 'Think(): elige la jugada del bot en bm. Fácil: mira 1 jugada con algo de azar; Normal: 3 jugadas; Difícil: 4.'):
    E.ev((), [setv('nodes', 0), loc('prof', '$nivel == 1 ? 1 : $nivel + 1'), setv('qmax', '$nivel == 1 ? 1 : $nivel + 2'), call('Legal'), loc('best', -999999), setv('bm', '$LM[0]')])
    with E.rep('len($LM)', 'i', (), [loc('m', '$LM[$i]'), call('Make', '$m'), loc('sc', '-Fn.Search($prof - 1, -999999, -$best)'), call('Unmake', '$m')]):
        E.ev([X('$nivel == 1')], [setv('sc', 'randInt(-70, 70)', 'add')])
        E.ev([X('$sc > $best')], [setv('best', '$sc'), setv('bm', '$m')])

# ================= partida =================
E.group('Partida: jugadas en pantalla')
with E.func('Spawn', '', 'Spawn(): crea una Pieza por cada pieza del tablero.'):
    with E.rep(64, 'i', (), [loc('sq', '$i + ($i & 56)'), loc('p', '$B[$sq]')]):
        E.ev([X('$p != 0')], [act('create', object=O['Pieza'], x=sqx('$sq'), y=sqy('$sq')), act('setObjVar', object=O['Pieza'], variable='sq', value='{g:sq}'),
                              act('setFrame', object=O['Pieza'], value='($p >> 3) * 6 + ($p & 7) - 1'), act('setZ', object=O['Pieza'], value=5)])

with E.func('FindMove', 'f, t', 'FindMove(origen, destino): la jugada legal entre esas casillas (con coronación a dama), o -1.'):
    E.rep('len($LM)', 'i', [X('($LM[$i] & 127) == $f && (($LM[$i] >> 7) & 127) == $t')], [ret('$LM[$i]')])
    E.ev((), [ret(-1)])

with E.func('FindPromo', 'f, t, pr', 'FindPromo(origen, destino, pieza): la coronación elegida.'):
    E.rep('len($LM)', 'i', [X('($LM[$i] & 127) == $f && (($LM[$i] >> 7) & 127) == $t && (($LM[$i] >> 14) & 7) == $pr')], [ret('$LM[$i]')])
    E.ev((), [ret(-1)])

with E.func('San', 'm', 'San(jugada): la jugada en notación algebraica (C caballo, A alfil, T torre, D dama, R rey). Se llama antes de hacerla.'):
    E.ev((), [loc('f', '$m & 127'), loc('t', '($m >> 7) & 127'), loc('pr', '($m >> 14) & 7'), loc('ty', '$B[$f] & 7')])
    E.ev([X('$ty == 6 && abs($t - $f) == 2')], [ret('$t > $f ? "O-O" : "O-O-O"')])
    E.ev((), [loc('cap', '$B[$t] != 0 || ($ty == 1 && ($t & 7) != ($f & 7))'), loc('amb', '""')])
    E.rep('len($LM)', 'j', [X('$ty != 1 && (($LM[$j] >> 7) & 127) == $t && ($LM[$j] & 127) != $f && ($B[$LM[$j] & 127] & 7) == $ty')],
          [setv('amb', '(($LM[$j] & 127) & 7) != ($f & 7) ? substr("abcdefgh", $f & 7, 1) : str(8 - ($f >> 4))')])
    E.ev((), [ret('($ty == 1 ? ($cap ? substr("abcdefgh", $f & 7, 1) : "") : substr("PCATDR", $ty - 1, 1) + $amb) + ($cap ? "x" : "") + '
                  'substr("abcdefgh", $t & 7, 1) + (8 - ($t >> 4)) + ($pr ? "=" + substr("PCATDR", $pr - 1, 1) : "")')])

with E.func('ClearHints', '', 'Quita los puntos de jugada y la casilla seleccionada.'):
    E.ev([cond('objVar', object=O['Marca'], variable='k', op='eq', value='0')], [act('removeObj', object=O['Marca'])])

with E.func('Select', 'c', 'Select(casilla): selecciona la pieza y marca sus jugadas legales.'):
    E.ev((), [setv('sel', '$c'), call('ClearHints')])
    E.ev((), [act('create', object=O['Marca'], x=sqx('$c'), y=sqy('$c')), act('setObjVar', object=O['Marca'], variable='k', value='0'),
              act('setFrame', object=O['Marca'], value=2), act('setOpacity', object=O['Marca'], value=55), act('setZ', object=O['Marca'], value=1)])
    E.ev([cond('objVar', object=O['Pieza'], variable='sq', op='eq', value='{g:c}')],
         [act('setScale', object=O['Pieza'], x=1.25, y=1.25), act('tweenScale', object=O['Pieza'], x=1, y=1, duration=0.22, easing='backOut')])
    with E.rep('len($LM)', 'i', (), [loc('m', '$LM[$i]')]):
        E.ev([X('($m & 127) == $c && ((($m >> 14) & 7) == 0 || (($m >> 14) & 7) == 5)')],
             [loc('t', '($m >> 7) & 127'), act('create', object=O['Marca'], x=sqx('$t'), y=sqy('$t')), act('setObjVar', object=O['Marca'], variable='k', value='0'),
              act('setFrame', object=O['Marca'], value='$B[$t] != 0 || ($B[$c] & 7) == 1 && ($t & 7) != ($c & 7) ? 1 : 0'),
              act('setOpacity', object=O['Marca'], value=38), act('setZ', object=O['Marca'], value=7),
              act('setScale', object=O['Marca'], x=0.3, y=0.3), act('tweenScale', object=O['Marca'], x=1, y=1, duration=0.16, easing='easeOut')])

with E.func('Trays', '', 'Trays(): cuenta las piezas (CNT) y dibuja las capturadas de cada jugador en el panel.'):
    E.ev((), [act('removeObj', object=O['Capturada']), act('arrayFill', variable='CNT', size=15, value=0), loc('tw', 0), loc('tb', 0)])
    with E.rep(64, 'i', (), [loc('sq', '$i + ($i & 56)')]):
        E.ev([X('$B[$sq] != 0')], [setv('CNT[$B[$sq]]', 1, 'add')])
    with E.rep(5, 'j', (), [loc('ty', '5 - $j'), loc('ini', '$ty == 1 ? 8 : ($ty == 5 ? 1 : 2)')]):
        E.rep('max(0, $ini - $CNT[8 + $ty])', 'k', (), [act('create', object=O['Capturada'], x='592 + $tw * 17', y=232), act('setFrame', object=O['Capturada'], value='5 + $ty'),
                                                        act('setScale', object=O['Capturada'], x=1.5, y=1.5), setv('tw', 1, 'add')])
        E.rep('max(0, $ini - $CNT[$ty])', 'k', (), [act('create', object=O['Capturada'], x='592 + $tb * 17', y=266), act('setFrame', object=O['Capturada'], value='$ty - 1'),
                                                    act('setScale', object=O['Capturada'], x=1.5, y=1.5), setv('tb', 1, 'add')])
    E.ev((), [loc('dif', '($CNT[1] + 3 * $CNT[2] + 3 * $CNT[3] + 5 * $CNT[4] + 9 * $CNT[5]) - ($CNT[9] + 3 * $CNT[10] + 3 * $CNT[11] + 5 * $CNT[12] + 9 * $CNT[13])'),
              act('setText', object=O['Ventaja'], text='{= $dif > 0 ? "Blancas +" + $dif : ($dif < 0 ? "Negras +" + (-$dif) : "Material igualado")}')])

with E.func('AfterMove', '', 'AfterMove(): jugadas legales, jaque, fin de la partida y textos del panel después de cada jugada.'):
    E.ev((), [call('Legal'), call('Trays'), setv('jaque', 'Fn.Att($KS[$side], 1 - $side)'), setv('fin', 0)])
    E.ev([cond('objVar', object=O['Marca'], variable='k', op='eq', value='2')], [act('removeObj', object=O['Marca'])])
    E.ev([X('$jaque')], [act('create', object=O['Marca'], x=sqx('$KS[$side]'), y=sqy('$KS[$side]')), act('setObjVar', object=O['Marca'], variable='k', value='2'),
                         act('setFrame', object=O['Marca'], value=4), act('setOpacity', object=O['Marca'], value=80), act('setZ', object=O['Marca'], value=2),
                         act('setScale', object=O['Marca'], x=0.4, y=0.4), act('tweenScale', object=O['Marca'], x=1, y=1, duration=0.3, easing='backOut'),
                         act('shakeCamera', duration=0.15, intensity=2)])
    E.ev((), [act('setText', object=O['Estado'], text='{= $side == 0 ? "Juegan las blancas" : "Juegan las negras"}'),
              act('setFrame', object=O['IconoTurno'], value='$side == 0 ? 5 : 11'),
              act('setText', object=O['Aviso'], text='{= $jaque ? "¡Jaque!" : ""}'), act('setTextColor', object=O['Aviso'], color='#ff6b5e'),
              loc('heavy', '$CNT[1] + $CNT[4] + $CNT[5] + $CNT[9] + $CNT[12] + $CNT[13]'), loc('minor', '$CNT[2] + $CNT[3] + $CNT[10] + $CNT[11]')])
    E.ev([X('len($LM) == 0 && $jaque')], [setv('fin', 1), act('setVarText', variable='t1', text='¡Jaque mate!'),
                                          act('setVarText', variable='t2', text='{= $modo == 1 ? ($side == 1 ? "¡Has ganado!" : "Gana el bot") : ($side == 1 ? "Ganan las blancas" : "Ganan las negras")}')])
    E.ev([X('len($LM) == 0 && !$jaque')], [setv('fin', 1), act('setVarText', variable='t1', text='Tablas'), act('setVarText', variable='t2', text='Rey ahogado')])
    E.ev([X('$fin == 0 && $hm >= 100')], [setv('fin', 1), act('setVarText', variable='t1', text='Tablas'), act('setVarText', variable='t2', text='Regla de los 50 movimientos')])
    E.ev([X('$fin == 0 && $heavy == 0 && $minor <= 1')], [setv('fin', 1), act('setVarText', variable='t1', text='Tablas'), act('setVarText', variable='t2', text='Material insuficiente')])
    E.ev([X('$fin == 1')], [act('setText', object=O['Aviso'], text='{g:t1}'), act('setTextColor', object=O['Aviso'], color='#c9a66b')])

with E.func('HistLine', 'n, col', 'HistLine(fila, columna): texto de la lista de jugadas (las 8 últimas jugadas completas).'):
    E.ev((), [loc('total', 'floor((len($HIS) + 1) / 2)'), loc('k', 'max(0, $total - 8) + $n')])
    E.ev([X('$k >= $total')], [ret('""')])
    E.ev([X('$col == 0')], [ret('($k + 1) + "."')])
    E.ev([X('$col == 1')], [ret('$HIS[2 * $k]')])
    E.ev((), [ret('2 * $k + 1 < len($HIS) ? $HIS[2 * $k + 1] : ""')])

with E.func('UpdateHist', '', 'Escribe la lista de jugadas del panel.'):
    E.ev((), [act('setText', object=O['Jugada'], text='{= Fn.HistLine(Jugada.n, Jugada.col)}')], type='forEach', object=O['Jugada'])

with E.func('LastMoveMarks', '', 'Marca en amarillo el origen y el destino de la última jugada.'):
    E.ev([cond('objVar', object=O['Marca'], variable='k', op='eq', value='1')], [act('removeObj', object=O['Marca'])])
    with E.ev([X('len($HM) > 0')], [loc('m', '$HM[len($HM) - 1]')]):
        E.rep(2, 'i', (), [loc('q', '$i == 0 ? $m & 127 : ($m >> 7) & 127'), act('create', object=O['Marca'], x=sqx('$q'), y=sqy('$q')),
                           act('setObjVar', object=O['Marca'], variable='k', value='1'), act('setFrame', object=O['Marca'], value=3),
                           act('setOpacity', object=O['Marca'], value=42), act('setZ', object=O['Marca'], value=1)])

with E.func('PlayMove', 'm', 'PlayMove(jugada): mueve las piezas en pantalla con animaciones y hace la jugada (la usan el jugador y el bot).'):
    E.ev((), [loc('f', '$m & 127'), loc('t', '($m >> 7) & 127'), loc('pr', '($m >> 14) & 7'), loc('p', '$B[$f]'), loc('s', '$p >> 3'),
              loc('x', '($p & 7) == 1 && $t == $ep && $B[$t] == 0 ? $t + ($s == 0 ? 16 : -16) : $t'), loc('cp', '$B[$x]'), loc('san', 'Fn.San($m)')])
    E.ev([X('$cp != 0'), cond('objVar', object=O['Pieza'], variable='sq', op='eq', value='{g:x}')],
         [act('setObjVar', object=O['Pieza'], variable='sq', value='-1'), act('setZ', object=O['Pieza'], value=4),
          act('tweenOpacity', object=O['Pieza'], value=0, duration=0.25, easing='easeIn'), act('tweenScale', object=O['Pieza'], x=1.6, y=1.6, duration=0.25, easing='easeOut'),
          act('objTimerCtl', object=O['Pieza'], name='muere', action='reset')])
    E.ev([cond('objVar', object=O['Pieza'], variable='sq', op='eq', value='{g:f}')],
         [act('setObjVar', object=O['Pieza'], variable='sq', value='{g:t}'), act('setZ', object=O['Pieza'], value=9),
          act('tweenPos', object=O['Pieza'], x=sqx('$t'), y=sqy('$t'), duration=0.2, easing='easeOut')])
    E.ev([X('$pr != 0'), cond('objVar', object=O['Pieza'], variable='sq', op='eq', value='{g:t}')], [act('setFrame', object=O['Pieza'], value='$s * 6 + $pr - 1')])
    with E.ev([X('($p & 7) == 6 && abs($t - $f) == 2')], [loc('rf', '$t > $f ? $f + 3 : $f - 4'), loc('rt', '($f + $t) >> 1')]):
        E.ev([cond('objVar', object=O['Pieza'], variable='sq', op='eq', value='{g:rf}')],
             [act('setObjVar', object=O['Pieza'], variable='sq', value='{g:rt}'), act('tweenPos', object=O['Pieza'], x=sqx('$rt'), y=sqy('$rt'), duration=0.26, easing='easeInOut')])
    E.ev((), [call('Make', '$m'), push('HM', '$m'), setv('sel', -1), call('ClearHints'), call('LastMoveMarks'), call('AfterMove'),
              act('arrayPush', variable='HIS', value='{g:san}{= $fin == 1 && $jaque ? "#" : ($jaque ? "+" : "")}'), call('UpdateHist')])
    E.ev([X('$cp != 0')], [act('beep', freq=260, duration=0.07)])
    E.ev([X('$cp == 0')], [act('beep', freq=520, duration=0.04)])
    E.ev([X('$jaque')], [act('beep', freq=880, duration=0.12)])
    E.ev((), [setv('fase', 1), act('timerCtl', name='anim', action='reset')])

with E.func('UndoPly', '', 'Deshace la última jugada de la lista.'):
    E.ev([X('len($HM) > 0')], [loc('m', '$HM[len($HM) - 1]'), act('arrayPop', variable='HM'), act('arrayPop', variable='HIS'), call('Unmake', '$m')])

with E.func('Resync', '', 'Vuelve a crear las piezas desde el tablero (después de deshacer).'):
    E.ev((), [act('removeObj', object=O['Pieza']), act('removeObj', object=O['Marca']), call('Spawn'), setv('sel', -1), call('LastMoveMarks'),
              call('AfterMove'), call('UpdateHist'), setv('fase', 0)])

with E.func('ShowPromo', 'f, t', 'ShowPromo(origen, destino): muestra las cuatro piezas para coronar en la columna de destino.'):
    E.ev((), [setv('fase', 3), setv('pf', '$f'), setv('pt', '$t'), call('ClearHints'), loc('dir', '$t < 16 ? 16 : -16'),
              act('setText', object=O['Aviso'], text='Elige la coronación'), act('setTextColor', object=O['Aviso'], color='#c9a66b'),
              act('create', object=O['VeloTablero'], x=68, y=68), act('setScale', object=O['VeloTablero'], x=64, y=64), act('setZ', object=O['VeloTablero'], value=20),
              act('setOpacity', object=O['VeloTablero'], value=0), act('tweenOpacity', object=O['VeloTablero'], value=55, duration=0.15, easing='easeOut')])
    E.rep(4, 'i', (), [loc('q', '$t + $i * $dir'), act('create', object=O['PromoFondo'], x=sqx('$q'), y=sqy('$q')), act('setZ', object=O['PromoFondo'], value=20.5),
                       act('setScale', object=O['PromoFondo'], x=0.2, y=0.2), act('tweenScale', object=O['PromoFondo'], x=1, y=1, duration=0.22, easing='backOut'),
                       act('create', object=O['PromoOpcion'], x=sqx('$q'), y=sqy('$q')),
                       act('setObjVar', object=O['PromoOpcion'], variable='pr', value='{= 5 - $i}'), act('setFrame', object=O['PromoOpcion'], value='$side * 6 + 4 - $i'),
                       act('setZ', object=O['PromoOpcion'], value=21), act('setScale', object=O['PromoOpcion'], x=0.2, y=0.2),
                       act('tweenScale', object=O['PromoOpcion'], x=1, y=1, duration=0.22, easing='backOut')])

with E.func('ClosePromo', '', 'Cierra el selector de coronación.'):
    E.ev((), [act('removeObj', object=O['PromoOpcion']), act('removeObj', object=O['PromoFondo']), act('removeObj', object=O['VeloTablero'])])

with E.func('ShowEnd', '', 'ShowEnd(): cartel de fin de partida con los textos t1 y t2.'):
    E.ev((), [setv('fase', 4), act('setText', object=O['Aviso'], text='{g:t1}'), act('setTextColor', object=O['Aviso'], color='#c9a66b'),
              act('create', object=O['Velo'], x=480, y=270), act('setScale', object=O['Velo'], x=480, y=270), act('setOpacity', object=O['Velo'], value=0),
              act('tweenOpacity', object=O['Velo'], value=62, duration=0.35, easing='easeOut')])
    for nm, sx, sy in (('CartelBorde', 172, 92), ('Cartel', 170, 90)):
        E.ev((), [act('create', object=O[nm], x=272, y=262), act('setScale', object=O[nm], x=sx * 0.1, y=sy * 0.1),
                  act('tweenScale', object=O[nm], x=sx, y=sy, duration=0.35, easing='backOut')])
    E.ev((), [act('create', object=O['FinTitulo'], x=272, y=212), act('setText', object=O['FinTitulo'], text='{g:t1}'),
              act('setScale', object=O['FinTitulo'], x=0.2, y=0.2), act('tweenScale', object=O['FinTitulo'], x=1, y=1, duration=0.4, easing='backOut'),
              act('create', object=O['FinSub'], x=272, y=252), act('setText', object=O['FinSub'], text='{g:t2}'),
              act('setOpacity', object=O['FinSub'], value=0), act('tweenOpacity', object=O['FinSub'], value=100, duration=0.5, easing='easeIn'),
              act('create', object=O['BtnRevancha'], x=197, y=308), act('create', object=O['BtnSalirFin'], x=347, y=308),
              act('beep', freq=440, duration=0.35)])

with E.func('CloseEnd', '', ''):
    E.ev((), [act('removeObj', object=O[n]) for n in ('Velo', 'CartelBorde', 'Cartel', 'FinTitulo', 'FinSub', 'BtnRevancha', 'BtnSalirFin')])

with E.func('NewGame', '', 'NewGame(): posición inicial y piezas nuevas.'):
    E.ev((), [act('arrayCopy', to='B', **{'from': 'B0'}), setv('side', 0), setv('cr', 15), setv('ep', -1), setv('hm', 0), setv('ev', 0), setv('KS', '[116, 4]'),
              setv('MS', '[]'), setv('US', '[]'), setv('HM', '[]'), setv('HIS', '[]'), setv('sel', -1), setv('pensar', 0),
              act('removeObj', object=O['Pieza']), act('removeObj', object=O['Marca']), call('ClosePromo'), call('CloseEnd'),
              call('Spawn'), call('LastMoveMarks'), call('AfterMove'), call('UpdateHist'), setv('fase', 0),
              act('setText', object=O['ModoTxt'], text='{= $modo == 0 ? "Jugador contra jugador" : "Contra el bot · " + ["Fácil", "Normal", "Difícil"][$nivel - 1]}')])

E.group('Partida: controles')
E.comment('Clic en el tablero: si hay una pieza seleccionada y la casilla es una de sus jugadas, se juega; si no, se selecciona la pieza propia.\n'
          'Coronación: el jugador elige la pieza (un clic fuera la cancela); esos eventos van antes del clic en el tablero para que el clic que abre\n'
          'el selector no elija también la primera pieza. Al terminar la animación toca al bot o al jugador;\n'
          'el turno del bot muestra "Pensando…" un fotograma y en el siguiente busca la jugada.')
with E.ev([cond('sceneIs', scene=SC_GAME)]):
    with E.ev([cond('atStart')], [act('cameraZoom', value=4)]):
        E.ev((), [call('NewGame'), act('hide', object=O['Cursor'])])
        E.ev((), [act('setText', object=O['Etiqueta'], text='{= Etiqueta.t}')], type='forEach', object=O['Etiqueta'])
        E.ev([cond('objVar', object=O['Jugada'], variable='col', op='eq', value='0')], [act('setTextColor', object=O['Jugada'], color='#7d8596')])
        E.rep(8, 'i', (), [act('create', object=O['Coord'], x='48 + $i * 64', y=534), act('setText', object=O['Coord'], text='{= substr("abcdefgh", $i, 1)}'),
                           act('create', object=O['Coord'], x=8, y='48 + $i * 64'), act('setText', object=O['Coord'], text='{= 8 - $i}')])
    E.ev([X('$fase == 3'), cond('clicked', object=O['PromoOpcion'])],
         [setv('mv', 'Fn.FindPromo($pf, $pt, PromoOpcion.pr)'), call('ClosePromo'), call('PlayMove', '$mv')])
    E.ev([X('$fase == 3'), cond('mousePressed', button='left')], [call('ClosePromo'), setv('fase', 0), setv('sel', -1),
                                                                   act('setText', object=O['Aviso'], text='{= $jaque ? "¡Jaque!" : ""}'), act('setTextColor', object=O['Aviso'], color='#ff6b5e')])
    with E.ev([cond('mousePressed', button='left'), X('$fase == 0 && ($modo == 0 || $side == 0)'),
               X('cursor.X() >= 4 && cursor.X() < 132 && cursor.Y() >= 4 && cursor.Y() < 132')],
              [setv('c', 'floor((cursor.Y() - 4) / 16) * 16 + floor((cursor.X() - 4) / 16)'), setv('mv', -1)]):
        E.ev([X('$sel >= 0')], [setv('mv', 'Fn.FindMove($sel, $c)')])
        E.ev((), [setv('elegir', '$mv < 0 && $B[$c] != 0 && ($B[$c] >> 3) == $side && $c != $sel ? 1 : 0')])
        E.ev([X('$mv >= 0 && (($mv >> 14) & 7) != 0')], [call('ShowPromo', '$sel, $c')])
        E.ev([X('$mv >= 0 && (($mv >> 14) & 7) == 0')], [call('PlayMove', '$mv')])
        E.ev([X('$elegir == 1')], [call('Select', '$c'), act('beep', freq=700, duration=0.02)])
        E.ev([X('$mv < 0 && $elegir == 0')], [setv('sel', -1), call('ClearHints')])
    E.ev([X('$fase == 0 && ($modo == 0 || $side == 0) && cursor.X() >= 4 && cursor.X() < 132 && cursor.Y() >= 4 && cursor.Y() < 132')],
         [act('show', object=O['Cursor']), act('setPos', object=O['Cursor'], x='12 + floor((cursor.X() - 4) / 16) * 16', y='12 + floor((cursor.Y() - 4) / 16) * 16')])
    E.ev([cond('exprTrue', True, value='$fase == 0 && ($modo == 0 || $side == 0) && cursor.X() >= 4 && cursor.X() < 132 && cursor.Y() >= 4 && cursor.Y() < 132')],
         [act('hide', object=O['Cursor'])])
    with E.ev([X('$fase == 1'), cond('timerCompare', name='anim', op='ge', value='0.24')], [act('setZ', object=O['Pieza'], value=5)]):
        E.ev([X('$fin == 1')], [call('ShowEnd')])
        E.ev([X('$fin == 0 && $modo == 1 && $side == 1')], [setv('fase', 2), setv('pensar', 0)])
        E.ev([X('$fin == 0 && ($modo == 0 || $side == 0)')], [setv('fase', 0)])
    E.ev([cond('objTimer', object=O['Pieza'], name='muere', op='gt', value='0.26')], [act('removeObj', object=O['Pieza'])])
    with E.ev([X('$fase == 2')]):
        E.ev([X('$pensar >= 1'), cond('timerCompare', name='anim', op='ge', value='0.45')], [call('Think'), setv('pensar', 0), call('PlayMove', '$bm')])
        E.ev([X('$pensar == 0 && $fase == 2')], [setv('pensar', 1), act('setText', object=O['Aviso'], text='Pensando…'), act('setTextColor', object=O['Aviso'], color='#9aa3b5')])
    with E.ev([cond('btnClicked', object=O['BtnDeshacer']), X('$fase == 0 && len($HM) > 0')], [call('UndoPly')]):
        E.ev([X('$modo == 1 && $side == 1')], [call('UndoPly')])
        E.ev((), [call('Resync'), act('beep', freq=400, duration=0.04)])
    E.ev([cond('btnClicked', object=O['BtnRendirse']), X('$fase == 0')],
         [act('setVarText', variable='t1', text='Abandono'),
          act('setVarText', variable='t2', text='{= $modo == 1 ? "Gana el bot" : ($side == 0 ? "Ganan las negras" : "Ganan las blancas")}'), call('ShowEnd')])
    E.ev([cond('btnClicked', object=O['BtnMenu']), X('$fase != 2 && $fase != 4')], [act('changeScene', scene=SC_MENU, transition='fade', duration=0.35)])
    E.ev([cond('btnClicked', object=O['BtnRevancha'])], [call('NewGame')])
    E.ev([cond('btnClicked', object=O['BtnSalirFin'])], [act('changeScene', scene=SC_MENU, transition='fade', duration=0.35)])

E.group('Menú')
E.comment('El orden importa: los botones que aparecen al pulsar otro van antes, para que el mismo clic no los pulse también.')
with E.ev([cond('sceneIs', scene=SC_MENU)]):
    E.ev([cond('atStart')], [act('cameraZoom', value=4), setv('sub', 0),
                             act('hide', object=O['BtnFacil']), act('hide', object=O['BtnNormal']), act('hide', object=O['BtnDificil']), act('hide', object=O['BtnVolver']),
                             act('setScale', object=O['MenuTitulo'], x=0.6, y=0.6), act('tweenScale', object=O['MenuTitulo'], x=1, y=1, duration=0.7, easing='backOut'),
                             act('setOpacity', object=O['MenuSub'], value=0), act('tweenOpacity', object=O['MenuSub'], value=100, duration=1.2, easing='easeIn')])
    E.ev([cond('btnClicked', object=O['BtnPvP'])], [setv('modo', 0), act('changeScene', scene=SC_GAME, transition='fade', duration=0.35)])
    for nm, lv in (('BtnFacil', 1), ('BtnNormal', 2), ('BtnDificil', 3)):
        E.ev([cond('btnClicked', object=O[nm])], [setv('modo', 1), setv('nivel', lv), act('changeScene', scene=SC_GAME, transition='fade', duration=0.35)])
    show_levels = [act('hide', object=O['BtnPvP']), act('hide', object=O['BtnBot'])]
    for nm in ('BtnFacil', 'BtnNormal', 'BtnDificil', 'BtnVolver'):
        show_levels += [act('show', object=O[nm]), act('setScale', object=O[nm], x=0.85, y=0.85), act('tweenScale', object=O[nm], x=1, y=1, duration=0.25, easing='backOut'),
                        act('setOpacity', object=O[nm], value=0), act('tweenOpacity', object=O[nm], value=100, duration=0.2, easing='easeOut')]
    E.ev([cond('btnClicked', object=O['BtnBot'])], [setv('sub', 1)] + show_levels + [act('beep', freq=600, duration=0.03)])
    E.ev([cond('btnClicked', object=O['BtnVolver'])], [setv('sub', 0), act('show', object=O['BtnPvP']), act('show', object=O['BtnBot']),
                                                     act('hide', object=O['BtnFacil']), act('hide', object=O['BtnNormal']), act('hide', object=O['BtnDificil']), act('hide', object=O['BtnVolver'])])

# ---------------------------------------------------------------- objetos y eventos por escena
MENU_OBJS = {'Deco', 'MenuTitulo', 'MenuSub', 'MenuPie', 'BtnPvP', 'BtnBot', 'BtnFacil', 'BtnNormal', 'BtnDificil', 'BtnVolver'}
for o in objects:
    o['scene'] = SC_MENU if o['name'] in MENU_OBJS else SC_GAME


def by_scene(evs):
    """reparte los eventos por escena segun su grupo y quita los envoltorios "La escena actual es" (ya no hacen falta)"""
    folders = {e['id']: e['name'] for e in evs if e.get('kind') == 'folder'}
    out = {SC_MENU: [], SC_GAME: []}
    for e in evs:
        name = e['name'] if e.get('kind') == 'folder' else folders.get(e.get('folder'), '')
        out[SC_MENU if name == 'Menú' else SC_GAME].append(e)
    for sid, lst in out.items():
        res, drop = [], False
        for e in lst:
            lvl = e.get('level', 0)
            if 'kind' not in e and lvl == 0 and any(c['type'] == 'sceneIs' for c in e.get('conditions', [])) and not e.get('actions'):
                drop = True
                continue
            if drop and 'kind' not in e:
                if lvl == 0: drop = False
                else: e['level'] = lvl - 1
            res.append(e)
        out[sid] = res
    return out


per_scene = by_scene(E.list)
for sc in scenes:
    sc['events'] = per_scene[sc['id']]

project = {
    'id': 'p_ajedrez', 'name': 'Ajedrez', 'version': 3,
    'settings': {'width': 960, 'height': 540, 'fullscreen': False, 'autofullscreen': False},
    'globals': globals_, 'sprites': sprites, 'tilesets': tilesets, 'objects': objects, 'scenes': scenes,
    'shaders': shaders, 'events': [], 'fonts': [FONT],
}
strip_comments(project)
json.dump(project, open(OUT, 'w'), ensure_ascii=False, separators=(',', ':'))
print('%s: %d eventos, %d objetos' % (os.path.normpath(OUT), sum(1 for e in E.list if 'kind' not in e), len(objects)))
