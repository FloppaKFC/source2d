#!/usr/bin/env python3
"""Genera examples_rymik.source2d.json: "Project Ry-mik", juego de ritmo de 4 carriles recreado (y ampliado) desde el
proyecto de GDevelop de la carpeta "Proyect Ry-mik". Todo es con eventos.

- Inicio: se enciende como una tele vieja (shader CRT siempre activo) y muestra 5 botones: Iniciar, Ajustes, Perfil,
  Editor y Salir (que apaga la tele).
- Niveles: casetes de musica con el logo de cada cancion, vista previa, record, rango, precision y partidas.
- Ajustes: controles (cada carril y la pausa), volumenes, idioma, importar un nivel (.json) y quitar los importados.
- Perfil: precision media, puntuacion total, partidas, rangos y la tabla de cada nivel.
- Archivos: explorador de carpetas dibujado en el juego para elegir .json, canciones e imagenes.
- Editor: nombre, cancion, logo, color de fondo, velocidad y retraso; grabar notas tocando las teclas mientras suena
  (largas, naranjas con Shift, grises con Ctrl) y efectos; revisar y retocar la linea de tiempo; probar y exportar a JSON.
- Juego: la jugabilidad del original (notas largas, naranjas, grises, juicios, combo, efectos de camara...). El CRT
  se desvanece al empezar; al reiniciar desde la pausa vuelve, aparece "REBOBINANDO", el nivel retrocede y con un
  destello empieza otra vez.

Los niveles no van dentro de los eventos: cada cancion es un .json (formato "rymik-nivel" con la cancion, el logo, el
color y las notas) en los Archivos iniciales del proyecto; el juego los lee al arrancar, igual que los que importes.
Las 7 canciones del original se convierten en esos .json (con --niveles <carpeta> tambien se escriben sueltos).

Uso: make_rymik.py <carpeta del proyecto de GDevelop> <salida.json> [--niveles <carpeta>]"""
import base64, hashlib, io, json, math, os, re, shutil, subprocess, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rymik_art as art

SRC, OUT = sys.argv[1], sys.argv[2]
NIVELES_DIR = sys.argv[sys.argv.index('--niveles') + 1] if '--niveles' in sys.argv else None
GD = json.load(open(os.path.join(SRC, 'Project Ry-mik.json')))
VW, VH = 640, 360
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'gen', 'rymik')


def strip_comments(doc):
    """los juegos de prueba van sin comentarios en la hoja de eventos"""
    for lst in [doc.get('events', [])] + [sc.get('events', []) for sc in doc.get('scenes', [])]:
        lst[:] = [e for e in lst if e.get('kind') != 'comment']


# ---------------- imagenes ----------------
def pixels(im):
    raw = im.convert('RGBA').tobytes()
    return [None if raw[i + 3] < 128 else '#%02x%02x%02x' % (raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 4)]


def img(path):
    return Image.open(os.path.join(SRC, path)).convert('RGBA')


def sprite(sid, name, ims):
    return dict(id=sid, name=name, width=ims[0].width, height=ims[0].height, frames=[dict(pixels=pixels(i)) for i in ims])


def nine_slice(im, w, h, l, t, r, b):
    out = Image.new('RGBA', (w, h))
    W, H = im.size
    for sx0, sx1, dx0, dx1 in [(0, l, 0, l), (l, W - r, l, w - r), (W - r, W, w - r, w)]:
        for sy0, sy1, dy0, dy1 in [(0, t, 0, t), (t, H - b, t, h - b), (H - b, H, h - b, h)]:
            if sx1 > sx0 and sy1 > sy0 and dx1 > dx0 and dy1 > dy0:
                out.alpha_composite(im.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.NEAREST), (dx0, dy0))
    return out


NOTA = img('assets/default.png')                       # 96x32: la nota y el notador (blancos, se tinen)
barra = Image.new('RGBA', (120, 14))                    # barra de volumen (borde y relleno del deslizador original)
barra.alpha_composite(nine_slice(img('assets/Square Blue Slider Border.png'), 120, 14, 4, 4, 4, 4), (0, 0))
barra.alpha_composite(img('assets/Square Blue Slider Fill Bar.png').resize((116, 10), Image.NEAREST), (2, 2))
sprites = [
    sprite('s_nota', 'Nota', [NOTA]),
    sprite('s_blanco', 'Blanco', [Image.new('RGBA', (4, 4), (255, 255, 255, 255))]),
    sprite('s_casete', 'Casete', [art.casete()]),
    sprite('s_bobina', 'Bobina', [art.bobina()]),
    sprite('s_logo', 'Logo vacío', [art.logo_vacio()]),
    sprite('s_boton', 'Botón', [art.boton()]),
    sprite('s_circulo', 'Círculo', [art.circulo()]),
    sprite('s_rebobinar', 'Rebobinar', [art.rebobinar()]),
    sprite('s_fila', 'Fila elegida', [art.fila()]),
    sprite('s_barra', 'Barra de volumen', [barra]),
    sprite('s_flecha_i', 'Flecha izquierda', [art.flecha(izquierda=True)]),
    sprite('s_flecha_d', 'Flecha derecha', [art.flecha(izquierda=False)]),
]

# ---------------- fuente bitmap (BMFont) -> fuente de pixeles ----------------
fnt = open(os.path.join(SRC, 'assets/NotJamOldStyle11.fnt')).read()
atlas = img('assets/NotJamOldStyle11.png')
chars = [dict((k, int(v)) for k, v in re.findall(r'(\w+)=(-?\d+)', l)) for l in fnt.splitlines() if l.startswith('char ')]
CELL_H = 11
CELL_W = max(c['xoffset'] + c['width'] - 1 for c in chars)
glyphs, cs = [], ''
for c in chars:
    px = [None] * (CELL_W * CELL_H)
    for iy in range(c['height']):
        for ix in range(c['width']):
            x, y = ix + c['xoffset'], iy + c['yoffset'] - 1      # sin el relleno de 1 px del atlas
            if atlas.getpixel((c['x'] + ix, c['y'] + iy))[3] >= 128 and 0 <= x < CELL_W and 0 <= y < CELL_H:
                px[y * CELL_W + x] = '#ffffff'
    cs += chr(c['id'])
    glyphs.append(dict(pixels=px, adv=c['xadvance']))
FONT = dict(id='f_notjam', name='NotJam Old Style 11', kind='pixel', cellW=CELL_W, cellH=CELL_H, chars=cs, glyphs=glyphs, spacing=0, tint=True)


# ---------------- canciones ----------------
def encode(name):
    src = os.path.join(SRC, name)
    data = open(src, 'rb').read()
    if not shutil.which('ffmpeg'):
        return data
    os.makedirs(CACHE, exist_ok=True)
    dst = os.path.join(CACHE, hashlib.sha1(data).hexdigest()[:16] + '.mp3')
    if not os.path.exists(dst):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-map_metadata', '-1', '-vn', '-codec:a', 'libmp3lame', '-b:a', '128k', dst], check=True)
    small = open(dst, 'rb').read()
    return small if len(small) < len(data) else data


# ---------------- niveles del original -> .json "rymik-nivel" ----------------
def parse_level(text):
    """como el JS original (CARGAR NIVEL + refreshBeatmaps): metadatos, notas (con las largas validadas) y efectos"""
    meta = {}
    for k in ('V', 'F', 'M', 'R'):
        m = re.search(r'\(%s=([^)]*)\)' % k, text)
        if m:
            meta[k] = m.group(1).strip()
    notes, effects = [], []
    for m in re.finditer(r'([\d.]+)s\(([^)]+)\)', text):
        t = float(m.group(1))
        # las comas dentro de corchetes son parametros de la nota (D[naranja,2,6]); el original las partia y perdia esas notas
        for item in re.split(r',(?![^\[]*\])', m.group(2).upper()):
            parts = item.strip().split(':')
            key = parts[0].strip()
            if not key:
                continue
            props = dict(re.findall(r'\{(\w+):([^}]+)\}', key))
            key = re.sub(r'\{\w+:[^}]+\}', '', key)
            if key.startswith('EF_'):
                effects.append(dict(t=t, c=key[3:], dur=float(props.get('DUR', 0) or 0), fc=props.get('FC', '')))
                continue
            esp, g, h, w = 0, 2, 6, 5
            bm = re.match(r'^([DFJK])\[([^\]]+)\]', key)
            if bm:
                key = bm.group(1)
                ps = bm.group(2).split(',')
                kind = ps[0].lower()
                if kind == 'naranja':
                    esp = 1
                    g = int(ps[1]) if len(ps) > 1 and ps[1].strip().isdigit() else 2
                    h = float(ps[2]) if len(ps) > 2 else 6
                elif kind == 'gris':
                    esp = 2
                    w = float(ps[1]) if len(ps) > 1 else 5
            if key not in 'DFJK' or len(key) != 1:
                continue
            notes.append(dict(t=t, end=float(parts[1]) if len(parts) > 1 else t, c='DFJK'.index(key), esp=esp, g=g, h=h, w=w))
    notes.sort(key=lambda n: n['t'])
    for n in notes:                     # nota larga valida: dura 0.2 s o mas y nadie empieza en su carril mientras dura
        ok = n['end'] >= n['t'] + 0.2 and not any(o is not n and o['c'] == n['c'] and n['t'] <= o['t'] <= n['end'] for o in notes)
        n['fin'] = n['end'] if ok else -1
    effects.sort(key=lambda e: e['t'])
    return meta, notes, effects


def png_b64(im):
    b = io.BytesIO()
    im.save(b, 'PNG', optimize=True)
    return base64.b64encode(b.getvalue()).decode()


# nombre y portada de cada nivel (tarjetas del menu original: variable Marco = nivel, animacion de la Foto)
gd_menu = [l for l in GD['layouts'] if l['name'] == 'Niveles'][0]
FOTOS = ['0x1900-000000-80-0-0(1).jpg', 'artworks-yMUDp2qyz4OBFsvX-jEJPcQ-t500x500(1).jpg', 'ab67616d00001e0244f4d8e99ee8335a3a0873b0(1).jpg',
         'images(1).png', 'images(1).jpg', 'ab67616d0000b273057d046c6dfb348e0215567c(1).jpg']
card = {}
marcos = [x for x in gd_menu['instances'] if x['name'] == 'Marco']
for x in gd_menu['instances']:
    if x['name'] not in ('Nombre', 'Foto') or x['y'] > 300:
        continue
    near = min(marcos, key=lambda m: math.hypot(m['x'] - x['x'], m['y'] - x['y']))
    k = {v['name']: v.get('value') for v in near.get('initialVariables', [])}.get('Marco', 0)
    c = card.setdefault(k, {})
    if x['name'] == 'Nombre':
        c['nombre'] = {v['name']: v.get('value') for v in x.get('initialVariables', [])}.get('Texto', '')
    else:
        c['foto'] = int(x['numberProperties'][0]['value']) if x.get('numberProperties') else 0

level_files = []
for v in GD['variables'][0]['children']:
    k = int(re.sub(r'\D', '', v['name']))
    meta, notes, effects = parse_level(str(v.get('value') or ''))
    if 'M' not in meta:
        continue                                           # nivel vacio (sin cancion)
    nombre = card.get(k, {}).get('nombre') or os.path.splitext(meta['M'])[0]
    foto = card.get(k, {}).get('foto')
    for n in notes:
        n['esp'] = 0                                       # por ahora sin notas naranjas ni grises
    lv = dict(formato='rymik-nivel', version=1, nombre=nombre, fondo=(meta.get('F') or '#808080').split(',')[0].strip().lower(),
              velocidad=float(meta.get('V', 400)), retraso=float(meta.get('R', 0) or 0),
              musica=dict(tipo='audio/mpeg', datos=base64.b64encode(encode(meta['M'])).decode()),
              logo=dict(tipo='image/png', datos=png_b64(img(FOTOS[foto]).resize((64, 64), Image.LANCZOS)) if foto is not None else ''),
              notas=dict(t=[round(n['t'], 4) for n in notes], fin=[round(n['fin'], 4) for n in notes], c=[n['c'] for n in notes],
                         esp=[n['esp'] for n in notes], g=[n['g'] for n in notes], h=[n['h'] for n in notes], w=[n['w'] for n in notes]),
              efectos=dict(t=[round(e['t'], 4) for e in effects], c=[e['c'] for e in effects], dur=[e['dur'] for e in effects],
                           color=[e['fc'] for e in effects]))
    level_files.append(('%02d %s.json' % (k, nombre), lv))
files = [dict(id='fi_%d' % i, name=n, text=json.dumps(lv, ensure_ascii=False, separators=(',', ':'))) for i, (n, lv) in enumerate(level_files)]
if NIVELES_DIR:
    os.makedirs(NIVELES_DIR, exist_ok=True)
    for f in files:
        open(os.path.join(NIVELES_DIR, f['name']), 'w').write(f['text'])

# ---------------- eventos: ayudas ----------------
_n = [0]


def C(t, inv=False, **p):
    return dict(type=t, enabled=True, invert=inv, params=p)


def X(expr, inv=False):
    """condicion con una expresion (verdadera si no es 0)"""
    return C('exprTrue', inv, value=expr)


def SET(var, val, op='set'):
    return C('setVar', variable=var, op=op, value=str(val))


def ev(conds, acts, level=0, **extra):
    _n[0] += 1
    e = dict(id='ev%d' % _n[0], enabled=True, level=level, conditions=list(conds), actions=list(acts))
    e.update(extra)
    return e


def note(text, level=0):
    _n[0] += 1
    return dict(id='nt%d' % _n[0], kind='comment', text=text, level=level)


def folder(name):
    _n[0] += 1
    return dict(id='fd%d' % _n[0], kind='folder', name=name, collapsed=False)


def in_folder(name, evs):
    fd = folder(name)
    for e in evs:
        if e.get('kind') != 'folder':
            e['folder'] = fd['id']
    return [fd] + evs


def fn(name, params, body):
    """funcion de eventos (nivel 0) con su cuerpo (nivel 1 y mas)"""
    out = [ev([], [], 0, type='function', name=name, params=params)]
    for e in body:
        e['level'] = e.get('level', 0) + 1
        out.append(e)
    return out


def sub(evs, d):
    """los mismos eventos d niveles mas adentro"""
    for e in evs:
        e['level'] = e.get('level', 0) + d
    return evs


def tr(es, en):
    """texto en el idioma elegido (variable global Idioma); la fuente NotJam no tiene tildes ni enes"""
    return '($Idioma == "en" ? %s : %s)' % (json.dumps(en, ensure_ascii=False), json.dumps(es, ensure_ascii=False))


SFX_VOL = '$Volumen_Global / 100 * $Volumen_SFX / 100'


def sfx(wave, f0, f1, dur, vol):
    return C('sfx', wave=wave, freqFrom=f0, freqTo=f1, duration=dur, volume='%g * %s' % (vol * 100, SFX_VOL))


S_HOVER = lambda: sfx('sine', 440, 880, 0.05, 0.15)
S_CLICK = lambda: sfx('triangle', 523.25, 659.25, 0.18, 0.15)
S_BACK = lambda: sfx('triangle', 659.25, 392, 0.15, 0.15)
OUTLINE = dict(width=2, color='#000000')                    # el efecto Outline de los textos del original


def obj(oid, name, otype='sprite', scene=None, **k):
    o = dict(id=oid, name=name, type=otype, spriteId=None, hud=False, shader='', capa='base', vars=[], animations=[],
             behaviors=dict(gravity=False, solid=False, oneway=False, collide=False, movement='none', speed=0, jump=0,
                            friction=0.85, airFriction=0.99, bounce=0, mass=1, spin=0),
             hooks=dict(onCreate='', onUpdate=''), flipOnMove=False, comportamientos={})
    if scene:
        o['scene'] = scene
    o.update(k)
    return o


def spr(oid, name, scene, sid, vars=(), hud=False, outline=None):
    o = obj(oid, name, scene=scene, spriteId=sid, hud=hud, vars=[dict(name=n, value=v) for n, v in vars])
    if outline:
        o['outline'] = outline
    return o


def txt(oid, name, scene, align='center', content='', hud=False, vars=(), outline=True, color='#ffffff'):
    o = obj(oid, name, 'text', scene, hud=hud, vars=[dict(name=n, value=v) for n, v in vars],
            text=dict(content=content, size=CELL_H, color=color, align=align, bold=False, font=FONT['id']))
    if outline:
        o['outline'] = dict(OUTLINE)
    return o


_ii = [0]


def inst(oid, x, y, **k):
    _ii[0] += 1
    d = dict(id='i%d' % _ii[0], objectId=oid, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
    if 's' in k:
        d['scaleX'] = d['scaleY'] = k.pop('s')
    d.update(k)
    return d


def ir(dest):
    """cambiar de escena con el ruido de cambio de canal (ver eventos comunes)"""
    return [SET('IrA', '"%s"' % dest), sfx('sawtooth', 1800, 200, 0.25, 0.12)]


SAVE_VARS = 'Volumen_Global, Volumen_Musica, Volumen_SFX, CrtNivel, Teclas, Idioma, RecNombre, RecPuntos, RecPrec, RecJugadas'
GUARDAR = lambda: C('saveVars', slot='rymik2', vars=SAVE_VARS)
SC = dict(inicio='sc_inicio', niveles='sc_niveles', ajustes='sc_ajustes', perfil='sc_perfil', archivos='sc_archivos',
          editor='sc_editor', juego='sc_juego')
LIBRE = '$IrA == ""'
DT = 'TiempoDelta()'


def burbujas(n, seed):
    """circulos de colores que flotan por el fondo de los menus"""
    import random
    r = random.Random(seed)
    cols = ['255;80;140', '80;200;255', '255;210;60', '150;90;255', '60;255;170', '255;120;60']
    return [inst('o_burbuja', r.uniform(0, VW), r.uniform(0, VH), s=r.uniform(0.6, 2.2), opacity=r.uniform(10, 22), z=-50,
                 vars=dict(vx=r.uniform(-14, 14), vy=r.uniform(-22, -6), col=cols[i % len(cols)])) for i in range(n)]


def lista_nav(n, y0, dy, x0, x1, extra_guard='1'):
    """navegacion de una lista de n filas: flechas, raton (al moverlo) y Enter/clic. Deja Act=1 al activar,
    Izq/Der = 1 con las flechas laterales y Sel = fila elegida"""
    g = '%s && %s' % (LIBRE, extra_guard)
    return [
        ev([], [SET('Act', 0), SET('Izq', 0), SET('Der', 0), SET('Movido', 'cursor.SX() != $mx0 || cursor.SY() != $my0 ? 1 : 0'),
                SET('mx0', 'cursor.SX()'), SET('my0', 'cursor.SY()'),
                SET('FilaRaton', 'cursor.X() >= %d && cursor.X() <= %d ? floor((cursor.Y() - %g) / %g) : -1' % (x0, x1, y0 - dy / 2, dy))]),
        ev([X(g)], []),
        ev([C('keyPressed', key='arrowdown')], [SET('Sel', '($Sel + 1) %% %d' % n), S_HOVER()], level=1),
        ev([C('keyPressed', key='arrowup')], [SET('Sel', '($Sel + %d) %% %d' % (n - 1, n)), S_HOVER()], level=1),
        ev([X('$Movido == 1 && $FilaRaton >= 0 && $FilaRaton < %d && $FilaRaton != $Sel' % n)], [SET('Sel', '$FilaRaton'), S_HOVER()], level=1),
        ev([C('keyPressed', key='enter')], [SET('Act', 1)], level=1),
        ev([C('mousePressed', button='left'), X('$FilaRaton == $Sel')], [SET('Act', 1)], level=1),
        ev([C('keyPressed', key='arrowleft')], [SET('Izq', 1)], level=1),
        ev([C('keyPressed', key='arrowright')], [SET('Der', 1)], level=1),
    ]


# ======================================================================================================
#  OBJETOS GLOBALES Y EVENTOS COMUNES
# ======================================================================================================
objects = [
    obj('o_fondo', 'Fondo', spriteId='s_blanco', capa='fondo'),
    spr('o_burbuja', 'Burbuja', None, 's_circulo', vars=(('vx', 0), ('vy', 0), ('col', '255;255;255'))),
]
PALETA = ['#272757', '#000000', '#1e320d', '#ffc0cb', '#ffffff', '#010024', '#ff0000', '#3b1f5c', '#0f4c5c', '#5c2a0f',
          '#2a2a2a', '#e0475a', '#2f9e8f', '#f2c14e', '#1b3a6b', '#6b1b4f']
globals_ = [
    dict(name='Idioma', type='text', value='es'),
    dict(name='Volumen_Global', value=50), dict(name='Volumen_Musica', value=50), dict(name='Volumen_SFX', value=50),
    dict(name='Teclas', type='array', value=['d', 'f', 'j', 'k', 'p']),
    dict(name='RecNombre', type='array', value=[]), dict(name='RecPuntos', type='array', value=[]),
    dict(name='RecPrec', type='array', value=[]), dict(name='RecJugadas', type='array', value=[]),
    dict(name='Paleta', type='array', value=PALETA),
    dict(name='Seleccion', value=0), dict(name='SelNiveles', value=0), dict(name='VolverA', type='text', value='niveles'),
    dict(name='IrA', type='text', value=''), dict(name='Cargado', value=0), dict(name='BibCargada', value=0), dict(name='BibVer', value=0),
    dict(name='FlashInicio', value=0), dict(name='CrtNivel', value=50), dict(name='RuidoA', value=0),
    dict(name='FondoRGB', type='text', value='110;50;200'), dict(name='EdR', value=39), dict(name='EdG', value=39), dict(name='EdB', value=87),
    dict(name='EdCancK', value=0),
    dict(name='ArchivoModo', type='text', value=''), dict(name='ArchivoElegido', type='text', value=''),
    dict(name='ArchivoVolver', type='text', value='inicio'), dict(name='DirArchivos', type='text', value=''),
    dict(name='EdNombre', type='text', value='Mi nivel'), dict(name='EdFondo', type='text', value='#272757'), dict(name='EdColI', value=0),
    dict(name='EdVel', value=700), dict(name='EdRet', value=0), dict(name='EdMusOk', value=0), dict(name='EdCancion', type='text', value=''),
    dict(name='EdMusica', type='text', value=''), dict(name='EdMusTipo', type='text', value='audio/mpeg'),
    dict(name='EdLogo', type='text', value=''), dict(name='EdLogoTipo', type='text', value='image/png'), dict(name='EdLogoVer', value=0),
    dict(name='EdAbrir', value=0),
] + [dict(name=n, type='array', value=[]) for n in ('LvNombre', 'LvArchivo', 'LvIni', 'LvR', 'LvG', 'LvB', 'LvLogo', 'LvVel',
                                                    'EdT', 'EdFin', 'EdC', 'EdEsp', 'EdG', 'EdH', 'EdW', 'EdET', 'EdEC', 'EdED', 'EdEF')]

com = []
com += fn('rango', 'pts', [
    ev([X('$pts >= 1000000')], [C('returnValue', value='"SS"')]),
    ev([X('$pts >= 900000')], [C('returnValue', value='"S"')]),
    ev([X('$pts >= 800000')], [C('returnValue', value='"A"')]),
    ev([X('$pts >= 700000')], [C('returnValue', value='"B"')]),
    ev([X('$pts >= 600000')], [C('returnValue', value='"C"')]),
    ev([X('$pts > 0')], [C('returnValue', value='"F"')]),
    ev([], [C('returnValue', value='"-"')]),
])
com += fn('recIdx', 'nombre', [
    ev([], [], type='repeat', count='len($RecNombre)', var='ri'),
    ev([X('"" + $RecNombre[$ri] == "" + $nombre')], [C('returnValue', value='$ri')], level=1),
    ev([], [C('returnValue', value='-1')]),
])
com += fn('nombreTecla', 'k', [
    ev([X('$k == " "')], [C('returnValue', value=tr('ESPACIO', 'SPACE'))]),
    ev([X('$k == "arrowleft"')], [C('returnValue', value=tr('IZQUIERDA', 'LEFT'))]),
    ev([X('$k == "arrowright"')], [C('returnValue', value=tr('DERECHA', 'RIGHT'))]),
    ev([X('$k == "arrowup"')], [C('returnValue', value=tr('ARRIBA', 'UP'))]),
    ev([X('$k == "arrowdown"')], [C('returnValue', value=tr('ABAJO', 'DOWN'))]),
    ev([], [C('returnValue', value='upper($k)')]),
])
com += fn('limpio', 'nombre', [
    note('Nombre de archivo sin caracteres raros.'),
    ev([], [C('returnValue', value='replace(replace(replace(replace(replace(trim($nombre), "/", "-"), "\\\\", "-"), ":", "-"), "*", "-"), "?", "")')]),
])
com += fn('cargarMeta', 'archivo, ini', [
    note('Lee el nivel que hay en txtBib (formato rymik-nivel) y lo anade a la biblioteca: nombre, archivo, color, logo y su cancion.'),
    ev([], [C('jsonToVars', source='txtBib', prefix='L')]),
    ev([C('cmpStr', a='{= $L.formato}', op='eq', b='rymik-nivel')], [
        C('arrayPush', variable='LvNombre', value='{= $L.nombre}'), C('arrayPush', variable='LvArchivo', value='{= $archivo}'),
        C('arrayPushNum', variable='LvIni', value='$ini'), C('arrayPushNum', variable='LvVel', value='$L.velocidad'),
        C('arrayPushNum', variable='LvR', value='hex($L.fondo, 0)'), C('arrayPushNum', variable='LvG', value='hex($L.fondo, 1)'),
        C('arrayPushNum', variable='LvB', value='hex($L.fondo, 2)'), C('arrayPush', variable='LvLogo', value='{= $L.logo.datos}'),
        C('musicFromData', id='{= "lv" + (len($LvNombre) - 1)}', source='L.musica.datos', mime='{= $L.musica.tipo}')]),
    ev([], [C('varsClear', prefix='L')]),
])
com += in_folder('Arranque y biblioteca de niveles', [
    note('Una vez por partida: carga los ajustes y records guardados.'),
    ev([X('$Cargado == 0')], [SET('Cargado', 1), C('loadGame', slot='rymik2')]),
    note('La biblioteca: los .json de los Archivos iniciales (en su orden) y luego los importados (carpeta de datos/ry-mik/niveles).'),
    ev([X('$BibCargada == 0')], [SET('BibCargada', 1), SET('BibVer', 1, 'add')] + [C('arrayClear', variable=v) for v in ('LvNombre', 'LvArchivo', 'LvIni', 'LvR', 'LvG', 'LvB', 'LvLogo', 'LvVel')] +
       [C('initialFiles', variable='ArchIni'), C('dirList', path='{= DirDatos()}/ry-mik/niveles', variable='ArchUsr')]),
    ev([], [], level=1, type='repeat', count='len($ArchIni)', var='bi'),
    ev([X('endsWith(lower($ArchIni[$bi]), ".json")')], [C('initialFileRead', name='{= $ArchIni[$bi]}', variable='txtBib'),
                                                        C('callFunction', name='cargarMeta', args='$ArchIni[$bi], 1', result='')], level=2),
    ev([], [], level=1, type='repeat', count='len($ArchUsr)', var='bi'),
    ev([X('endsWith(lower($ArchUsr[$bi]), ".json")')], [C('fileRead', path='{= DirDatos()}/ry-mik/niveles/{= $ArchUsr[$bi]}', variable='txtBib'),
                                                        C('callFunction', name='cargarMeta', args='DirDatos() + "/ry-mik/niveles/" + $ArchUsr[$bi], 0', result='')], level=2),
    ev([], [SET('txtBib', '""')], level=1),
])
com += in_folder('Pantalla de tele', [
    note('En todas las escenas: brillo (glow) como el original, el ruido de cambio de canal y el CRT (intensidad de Ajustes; el Juego lo desvanece).'),
    ev([C('atStart')], [C('screenShader', shader='sh_glow', state='on'), C('screenShader', shader='sh_ruido', state='on'), C('screenShader', shader='sh_crt', state='on'),
                        C('shaderAmount', shader='sh_crt', value='$CrtNivel / 100')]),
    ev([C('sceneIs', True, scene=SC['juego'])], [C('shaderAmount', shader='sh_crt', value='$CrtNivel / 100')]),
    note('Fondo animado de los menus (shader de la capa Fondo); su color base es FondoRGB.'),
    ev([C('atStart'), C('sceneIs', True, scene=SC['juego'])], [C('create', object='o_fondo', x=VW / 2, y=VH / 2), C('setScale', object='o_fondo', x=VW / 4 + 1, y=VH / 4 + 1),
                                                              C('setZ', object='o_fondo', value=-1000)]),
    ev([], [C('setColor', object='o_fondo', color='{= $FondoRGB}')]),
    note('Cambio de escena (IrA = destino): el ruido sube poco a poco, se cambia de escena y en la nueva baja poco a poco. El resto de shaders sigue igual.'),
    ev([X('$IrA != ""')], [SET('RuidoA', 'min(1, $RuidoA + %s / 0.35)' % DT)]),
    ev([X('$IrA == "" && $RuidoA > 0')], [SET('RuidoA', 'max(0, $RuidoA - %s / 0.5)' % DT)]),
    ev([], [C('shaderAmount', shader='sh_ruido', value='$RuidoA * $RuidoA * (3 - 2 * $RuidoA)')]),
] + [ev([X('$RuidoA >= 1 && $IrA == "%s"' % k)], [SET('IrA', '""'), C('stopMusic'), C('changeScene', scene=v, transition='none', duration=0)])
     for k, v in SC.items()] + [
    note('Burbujas de colores del fondo de los menus.'),
    ev([C('atStart')], []),
    ev([], [], level=1, type='forEach', object='o_burbuja'),
    ev([], [C('setColor', object='o_burbuja', color='{= Burbuja.col}')], level=2),
    ev([], [], type='forEach', object='o_burbuja'),
    ev([], [C('setPos', object='o_burbuja', x='Burbuja.X + Burbuja.vx * %s' % DT, y='Burbuja.Y + Burbuja.vy * %s' % DT)], level=1),
    ev([X('Burbuja.Y < -60')], [C('setY', object='o_burbuja', value=VH + 60)], level=1),
    ev([X('Burbuja.X < -60')], [C('setX', object='o_burbuja', value=VW + 60)], level=1),
    ev([X('Burbuja.X > %d' % (VW + 60))], [C('setX', object='o_burbuja', value=-60)], level=1),
])

# ======================================================================================================
#  INICIO: tele que se enciende y menu de 5 botones
# ======================================================================================================
S = SC['inicio']
BOTONES = [('Iniciar', 'Start'), ('Ajustes', 'Settings'), ('Perfil', 'Profile'), ('Editor', 'Editor'), ('Salir', 'Quit')]
objects += [
    spr('o_negro', 'Negro', S, 's_blanco', hud=True), spr('o_raya', 'Raya', S, 's_blanco', hud=True),
    txt('o_titulo', 'Titulo', S, content='RY-MIK'),
    spr('o_boton', 'Boton', S, 's_boton', vars=(('i', 0), ('es', 1))), txt('o_botontxt', 'BotonTxt', S, vars=(('i', 0),)),
    txt('o_version', 'Version', S, align='left', content='V. alfa 0.1.0'),
]
ini_insts = burbujas(10, 1) + [
    inst('o_negro', VW / 2, VH / 2, scaleX=VW / 4 + 1, scaleY=VH / 4 + 1, z=900),
    inst('o_raya', VW / 2, VH / 2, scaleX=0, scaleY=0, z=901),
    inst('o_titulo', VW / 2, 70, s=5, opacity=0, z=10),
    inst('o_version', 560, VH - 10, z=10)]
for i in range(5):
    ini_insts += [inst('o_boton', -200, 160 + i * 34, z=20, opacity=0, vars=dict(i=i, es=1)),
                  inst('o_botontxt', -200, 160 + i * 34, s=1.5, z=21, opacity=0, vars=dict(i=i))]
RAYA = lambda sx, sy: C('setScale', object='o_raya', x=sx, y=sy)
ini = in_folder('Tele: encender', [
    note('La tele se enciende: un punto, una raya, la imagen se abre y el blanco se desvanece (tambien al volver al menu).'),
    ev([C('atStart')], [SET('TT', 0), SET('Sel', 0), SET('Apagar', 0), SET('AT', 0), C('setColor', object='o_negro', color='0;0;0'),
                        SET('Act', 0), C('stopMusic'), SET('FondoRGB', '"110;50;200"'),
                        sfx('sine', 60, 2400, 0.7, 0.3), sfx('square', 15600, 15600, 0.9, 0.04)]),
    ev([X('$Apagar == 0')], [SET('TT', DT, 'add')]),
    ev([X('$TT < 0.15')], [RAYA('0.3 + $TT / 0.15 * 1.2', '0.3 + $TT / 0.15 * 1.2')], level=1),
    ev([X('$TT >= 0.15 && $TT < 0.4')], [RAYA('1.5 + ($TT - 0.15) / 0.25 * %g' % (VW / 4 + 1 - 1.5), 0.5)], level=1),
    ev([X('$TT >= 0.4 && $TT < 0.65')], [RAYA(VW / 4 + 1, '0.5 + ($TT - 0.4) / 0.25 * %g' % (VH / 4 + 1)),
                                        C('setOpacity', object='o_negro', value='100 * (1 - ($TT - 0.4) / 0.25)')], level=1),
    ev([X('$TT >= 0.65')], [RAYA(VW / 4 + 1, VH / 4 + 1), C('setOpacity', object='o_negro', value=0),
                           C('setOpacity', object='o_raya', value='max(0, 100 * (1 - ($TT - 0.65) / 0.45))')], level=1),
    ev([X('$TT >= 0.7'), C('triggerOnce')], [C('setScale', object='o_titulo', x=2, y=2), C('tweenScale', object='o_titulo', x=5, y=5, duration=0.6, easing='backOut'),
                                           C('tweenOpacity', object='o_titulo', value=100, duration=0.3, easing='easeOut')]),
] + [ev([X('$TT >= %g' % (0.8 + i * 0.08)), C('triggerOnce'), C('objVar', object='o_boton', variable='i', op='eq', value=str(i))],
        [C('setOpacity', object='o_boton', value=100), C('tweenPos', object='o_boton', x=VW / 2, y=160 + i * 34, duration=0.45, easing='backOut')])
     for i in range(5)])
ini += in_folder('Menu principal', [
    note('Titulo con colores que cambian, fondo que respira y botones: el elegido crece y se pone amarillo.'),
    ev([], [SET('tt', DT, 'add'),
            C('setTextColor', object='o_titulo', color='rgb({= floor(200 + 55 * sin($tt * 2))},{= floor(130 + 110 * sin($tt * 2 + 2))},{= floor(200 + 55 * sin($tt * 2 + 4))})'),
            C('setY', object='o_titulo', value='70 + sin($tt * 2) * 3')]),
    ev([], [], type='forEach', object='o_boton'),
    ev([C('objVar', object='o_botontxt', variable='i', op='eq', value='{= Boton.i}')], [
        C('setText', object='o_botontxt', text='{= ["%s", "%s", "%s", "%s", "%s"][Boton.i]}' % tuple(b[0] for b in BOTONES)),
        C('setPos', object='o_botontxt', x='Boton.X', y='Boton.Y'), C('setOpacity', object='o_botontxt', value='Boton.Opacidad')], level=1),
    ev([X('$Idioma == "en"'), C('objVar', object='o_botontxt', variable='i', op='eq', value='{= Boton.i}')], [
        C('setText', object='o_botontxt', text='{= ["%s", "%s", "%s", "%s", "%s"][Boton.i]}' % tuple(b[1] for b in BOTONES))], level=1),
    ev([X('Boton.i == $Sel')], [C('setObjVar', object='o_boton', variable='es', value='{= Boton.es + (1.12 - Boton.es) * 14 * TiempoDelta()}'),
                                C('setColor', object='o_boton', color='255;214;64')], level=1),
    ev([X('Boton.i != $Sel')], [C('setObjVar', object='o_boton', variable='es', value='{= Boton.es + (1 - Boton.es) * 14 * TiempoDelta()}'),
                                C('setColor', object='o_boton', color='150;140;255')], level=1),
    ev([], [C('setScale', object='o_boton', x='Boton.es', y='Boton.es')], level=1),
    ev([C('objVar', object='o_botontxt', variable='i', op='eq', value='{= Boton.i}')], [C('setScale', object='o_botontxt', x='1.5 * Boton.es', y='1.5 * Boton.es')], level=1),
    note('Elegir: flechas, raton (al moverlo) y Enter o clic.'),
    ev([], [SET('Act', 0), SET('Movido', 'cursor.SX() != $mx0 || cursor.SY() != $my0 ? 1 : 0'), SET('mx0', 'cursor.SX()'), SET('my0', 'cursor.SY()')]),
    ev([X('$TT >= 1.1 && $Apagar == 0 && %s' % LIBRE)], []),
    ev([C('keyPressed', key='arrowdown')], [SET('Sel', '($Sel + 1) % 5'), S_HOVER()], level=1),
    ev([C('keyPressed', key='arrowup')], [SET('Sel', '($Sel + 4) % 5'), S_HOVER()], level=1),
    ev([X('$Movido == 1'), C('mouseOn', object='o_boton'), X('Boton.i != $Sel')], [SET('Sel', 'Boton.i'), S_HOVER()], level=1),
    ev([C('keyPressed', key='enter')], [SET('Act', 1)], level=1),
    ev([C('mousePressed', button='left'), C('mouseOn', object='o_boton'), X('Boton.i == $Sel')], [SET('Act', 1)], level=1),
    ev([C('keyPressed', key='escape')], [SET('Sel', 4), SET('Act', 1)], level=1),
    ev([X('$Act == 1')], [S_CLICK()], level=1),
    ev([X('$Sel == 0')], ir('niveles'), level=2),
    ev([X('$Sel == 1')], ir('ajustes'), level=2),
    ev([X('$Sel == 2')], ir('perfil'), level=2),
    ev([X('$Sel == 3')], ir('editor'), level=2),
    ev([X('$Sel == 4')], [SET('Apagar', 1), SET('AT', 0), sfx('sine', 1800, 40, 0.6, 0.3)], level=2),
])
ini += in_folder('Tele: apagar (Salir)', [
    note('Salir apaga la tele: la imagen se encoge a una raya, luego a un punto, y se cierra el juego.'),
    ev([X('$Apagar == 1')], [SET('AT', DT, 'add'), C('setOpacity', object='o_raya', value=100)]),
    ev([X('$AT < 0.12')], [RAYA(VW / 4 + 1, VH / 4 + 1), C('setOpacity', object='o_raya', value='$AT / 0.12 * 100')], level=1),
    ev([X('$AT >= 0.12 && $AT < 0.3')], [C('setOpacity', object='o_negro', value=100), RAYA(VW / 4 + 1, 'max(0.5, %g * (1 - ($AT - 0.12) / 0.18))' % (VH / 4 + 1))], level=1),
    ev([X('$AT >= 0.3 && $AT < 0.5')], [RAYA('max(1.5, %g * (1 - ($AT - 0.3) / 0.2))' % (VW / 4 + 1), 0.5)], level=1),
    ev([X('$AT >= 0.5 && $AT < 0.75')], [RAYA('1.5 * (1 - ($AT - 0.5) / 0.25)', '1.5 * (1 - ($AT - 0.5) / 0.25)')], level=1),
    ev([X('$AT >= 0.85')], [C('quitGame')], level=1),
])

# ======================================================================================================
#  NIVELES: casetes
# ======================================================================================================
S = SC['niveles']
objects += [
    spr('o_casete', 'Casete', S, 's_casete', vars=(('k', 0), ('sc', 1))),
    spr('o_bobina', 'Bobina', S, 's_bobina', vars=(('k', 0), ('lado', -1))),
    spr('o_logo', 'Logo', S, 's_logo', vars=(('k', 0),)),
    txt('o_cnombre', 'CNombre', S, vars=(('k', 0),), outline=False, color='#20202a'),
    txt('o_ntitulo', 'NTitulo', S), txt('o_nrecord', 'NRecord', S), txt('o_nayuda', 'NAyuda', S),
    txt('o_vacio', 'Vacio', S), spr('o_flechai', 'FlechaI', S, 's_flecha_i'), spr('o_flechad', 'FlechaD', S, 's_flecha_d'),
    txt('o_ncab', 'NCabecera', S),
]
niv_insts = burbujas(8, 2) + [
    inst('o_ncab', VW / 2, 22, s=2, z=30), inst('o_ntitulo', VW / 2, 236, s=2, z=30), inst('o_nrecord', VW / 2, 266, s=1.2, z=30),
    inst('o_nayuda', VW / 2, 336, z=30), inst('o_vacio', VW / 2, VH / 2, s=1.2, z=30, opacity=0),
    inst('o_flechai', 30, 132, s=1.5, z=30), inst('o_flechad', VW - 30, 132, s=1.5, z=30)]
CAS = lambda d: 'Casete.k - $SS'
niv = in_folder('Casetes', [
    note('Un casete por nivel de la biblioteca, con su logo en la etiqueta. El elegido va en el centro y sus bobinas giran.'),
    ev([C('atStart')], [SET('LvSel', 'clamp($SelNiveles, 0, max(0, len($LvNombre) - 1))'), SET('SS', '$LvSel'), SET('Prev', -1), SET('PrevT', 0),
                        SET('Sonando', 0), SET('vol', 0), SET('bgR', 30), SET('bgG', 30), SET('bgB', 30),
                        C('setText', object='o_ncab', text='{= %s}' % tr('ELIGE UNA CINTA', 'PICK A TAPE')),
                        C('setText', object='o_nayuda', text='{= %s}' % tr('Izquierda/Derecha: elegir    Enter: jugar    Esc: volver',
                                                                          'Left/Right: choose    Enter: play    Esc: back'))]),
    ev([], [], level=1, type='repeat', count='len($LvNombre)', var='k'),
    ev([], [C('create', object='o_casete', x='%d + $k * 190' % (VW / 2), y=132), C('setObjVar', object='o_casete', variable='k', value='{= $k}'),
            C('create', object='o_bobina', x=0, y=0), C('setObjVar', object='o_bobina', variable='k', value='{= $k}'), C('setObjVar', object='o_bobina', variable='lado', value='-1'),
            C('create', object='o_cnombre', x=0, y=0), C('setObjVar', object='o_cnombre', variable='k', value='{= $k}'),
            C('setText', object='o_cnombre', text='{= len("" + $LvNombre[$k]) > 13 ? substr($LvNombre[$k], 0, 12) + "." : $LvNombre[$k]}'),
            C('create', object='o_logo', x=0, y=0), C('setObjVar', object='o_logo', variable='k', value='{= $k}')], level=2),
    ev([X('"" + $LvLogo[$k] != ""')], [C('imageFromData', object='o_logo', key='{= "logo" + $BibVer + "_" + $k}', source='LvLogo[$k]')], level=3),
    ev([], [C('create', object='o_bobina', x=0, y=0), C('setObjVar', object='o_bobina', variable='k', value='{= $k}'),
            C('setObjVar', object='o_bobina', variable='lado', value='1')], level=2),
    ev([X('len($LvNombre) == 0')], [C('setOpacity', object='o_vacio', value=100),
                                    C('setText', object='o_vacio', text='{= %s}' % tr('No hay niveles.\nAnade .json en Archivos iniciales (editor)\no importa uno en Ajustes.',
                                                                                     'No levels yet.\nAdd .json files to Initial files (editor)\nor import one in Settings.'))], level=1),
    note('Elegir con las flechas, la rueda o un clic; Enter o clic sobre el del centro para jugar.'),
    ev([X('%s && len($LvNombre) > 0' % LIBRE)], []),
    ev([C('keyPressed', key='arrowleft')], [SET('LvSel', 'max(0, $LvSel - 1)'), S_HOVER()], level=1),
    ev([C('keyPressed', key='arrowright')], [SET('LvSel', 'min(len($LvNombre) - 1, $LvSel + 1)'), S_HOVER()], level=1),
    ev([C('wheelUp')], [SET('LvSel', 'max(0, $LvSel - 1)'), S_HOVER()], level=1),
    ev([C('wheelDown')], [SET('LvSel', 'min(len($LvNombre) - 1, $LvSel + 1)'), S_HOVER()], level=1),
    ev([C('mousePressed', button='left'), X('abs(cursor.Y() - 132) < 60')], [SET('clicK', 'clamp(round($SS + (cursor.X() - %d) / 190), 0, len($LvNombre) - 1)' % (VW / 2))], level=1),
    ev([X('$clicK != $LvSel')], [SET('LvSel', '$clicK'), S_HOVER()], level=2),
    ev([X('$clicK == $LvSel')], [SET('jugar', 1)], level=2),
    ev([C('keyPressed', key='enter')], [SET('jugar', 1)], level=1),
    ev([X('$jugar == 1')], [SET('jugar', 0), SET('Seleccion', '$LvSel'), SET('SelNiveles', '$LvSel'), SET('VolverA', '"niveles"'), S_CLICK()] + ir('juego'), level=1),
    ev([C('keyPressed', key='escape'), X(LIBRE)], [S_BACK()] + ir('inicio')),
    ev([C('keyPressed', key='backspace'), X(LIBRE)], [S_BACK()] + ir('inicio')),
    note('Colocacion: posicion, tamano y transparencia segun la distancia al elegido.'),
    ev([], [SET('SS', '($LvSel - $SS) * 10 * %s' % DT, 'add')]),
    ev([], [], type='forEach', object='o_casete'),
    ev([], [SET('dd', CAS(0)), C('setObjVar', object='o_casete', variable='sc', value='{= 1 - min(abs($dd), 2) * 0.2}'),
            C('setPos', object='o_casete', x='%d + $dd * 190' % (VW / 2), y='132 + abs($dd) * 10'),
            C('setScale', object='o_casete', x='Casete.sc', y='Casete.sc'), C('setOpacity', object='o_casete', value='100 - min(abs($dd), 3) * 28'),
            C('setZ', object='o_casete', value='100 - abs($dd) * 10')], level=1),
    ev([C('objVar', object='o_logo', variable='k', op='eq', value='{= Casete.k}')], [
        C('setPos', object='o_logo', x='Casete.X - 50 * Casete.sc', y='Casete.Y - 16 * Casete.sc'), C('setScale', object='o_logo', x='Casete.sc', y='Casete.sc'),
        C('setOpacity', object='o_logo', value='Casete.Opacidad'), C('setZ', object='o_logo', value='101 - abs($dd) * 10')], level=1),
    ev([C('objVar', object='o_cnombre', variable='k', op='eq', value='{= Casete.k}')], [
        C('setPos', object='o_cnombre', x='Casete.X + 22 * Casete.sc', y='Casete.Y - 16 * Casete.sc'), C('setScale', object='o_cnombre', x='Casete.sc', y='Casete.sc'),
        C('setOpacity', object='o_cnombre', value='Casete.Opacidad'), C('setZ', object='o_cnombre', value='101 - abs($dd) * 10')], level=1),
    ev([C('objVar', object='o_bobina', variable='k', op='eq', value='{= Casete.k}')], [], level=1),
    ev([], [], level=2, type='forEach', object='o_bobina'),
    ev([], [C('setPos', object='o_bobina', x='Casete.X + Bobina.lado * 30 * Casete.sc', y='Casete.Y + 26 * Casete.sc'),
            C('setScale', object='o_bobina', x='Casete.sc', y='Casete.sc'), C('setOpacity', object='o_bobina', value='Casete.Opacidad'),
            C('setZ', object='o_bobina', value='101 - abs($dd) * 10')], level=3),
    ev([X('Casete.k == $LvSel')], [C('rotateBy', object='o_bobina', value='-220 * %s' % DT)], level=3),
    ev([], [C('setOpacity', object='o_flechai', value='$LvSel > 0 ? 100 : 0'),
            C('setOpacity', object='o_flechad', value='$LvSel < len($LvNombre) - 1 ? 100 : 0')]),
])
niv += in_folder('Informacion y fondo', [
    note('Nombre, record, rango, precision y partidas del nivel elegido; el fondo toma su color.'),
    ev([X('len($LvNombre) > 0')], [SET('ri', 'Fn.recIdx($LvNombre[$LvSel])'),
                                   C('setText', object='o_ntitulo', text='{= $LvNombre[$LvSel]}'),
                                   SET('tR', '$LvR[$LvSel]'), SET('tG', '$LvG[$LvSel]'), SET('tB', '$LvB[$LvSel]')]),
    ev([X('$ri >= 0')], [C('setText', object='o_nrecord', text='{= %s}: {= padStart(floor($RecPuntos[$ri]), 7, "0")} [{= Fn.rango($RecPuntos[$ri])}]    '
                                                             '{= %s}: {= fixed($RecPrec[$ri], 1)} %%    {= %s}: {= $RecJugadas[$ri]}' % (
                                                                 tr('Record', 'Best'), tr('Precision', 'Accuracy'), tr('Partidas', 'Plays')))], level=1),
    ev([X('$ri < 0')], [C('setText', object='o_nrecord', text='{= %s}' % tr('Sin jugar todavia', 'Not played yet'))], level=1),
    ev([X('len($LvNombre) == 0')], [SET('tR', 30), SET('tG', 30), SET('tB', 30), C('setText', object='o_ntitulo', text=''), C('setText', object='o_nrecord', text='')]),
    ev([], [SET('bgR', '($tR * 0.6 - $bgR) * 5 * %s' % DT, 'add'), SET('bgG', '($tG * 0.6 - $bgG) * 5 * %s' % DT, 'add'),
            SET('bgB', '($tB * 0.6 - $bgB) * 5 * %s' % DT, 'add'), SET('FondoRGB', 'floor($bgR) + ";" + floor($bgG) + ";" + floor($bgB)')]),
])
niv += in_folder('Vista previa de la cancion', [
    note('Al quedarse en un casete suena su cancion: entra con fundido, dura 15 s y vuelve a empezar.'),
    ev([X('%s && len($LvNombre) > 0' % LIBRE)], []),
    ev([X('$LvSel != $Prev')], [SET('vol', '400 * %s' % DT, 'sub')], level=1),
    ev([X('$vol <= 0')], [SET('vol', 0), C('stopMusic'), SET('Prev', '$LvSel'), SET('PrevT', 0), SET('Sonando', 0)], level=2),
    ev([X('$LvSel == $Prev')], [SET('PrevT', DT, 'add'),
                                SET('tv', 'clamp($PrevT < 1.3 ? ($PrevT - 0.3) * 100 : ($PrevT > 14.3 ? (15.3 - $PrevT) * 100 : 100), 0, 100)'),
                                SET('vol', '($tv - $vol) * 10 * %s' % DT, 'add')], level=1),
    ev([X('$PrevT >= 0.3 && $Sonando == 0')], [SET('Sonando', 1), C('playMusic', track='{= "lv" + $LvSel}', loop='no')], level=2),
    ev([X('$PrevT >= 15.3')], [SET('PrevT', 0.3), SET('Sonando', 0), C('stopMusic')], level=2),
    ev([], [C('musicVolume', value='max(0, $vol) * $Volumen_Global / 100 * $Volumen_Musica / 100')]),
])

# ======================================================================================================
#  AJUSTES
# ======================================================================================================
S = SC['ajustes']
A_ROWS = [('Carril 1', 'Lane 1'), ('Carril 2', 'Lane 2'), ('Carril 3', 'Lane 3'), ('Carril 4', 'Lane 4'), ('Pausa', 'Pause'),
          ('Volumen general', 'Master volume'), ('Volumen de la musica', 'Music volume'), ('Volumen de los efectos', 'Effects volume'),
          ('Pantalla CRT', 'CRT screen'), ('Idioma', 'Language'), ('Importar nivel...', 'Import level...'), ('Quitar nivel importado', 'Remove imported level'), ('Volver', 'Back')]
AY0, ADY, AN = 62, 19, len(A_ROWS)
objects += [txt('o_afila', 'AFila', S, align='left', vars=(('r', 0),)), txt('o_avalor', 'AValor', S, align='left', vars=(('r', 0),)),
            spr('o_asel', 'ASel', S, 's_fila'), txt('o_atitulo', 'ATitulo', S), txt('o_amsg', 'AMensaje', S),
            spr('o_avbarra', 'AVBarra', S, 's_barra', vars=(('r', 0), ('bx', 0)))]
aj_insts = burbujas(6, 3) + [inst('o_atitulo', VW / 2, 26, s=2.2, z=10), inst('o_amsg', VW / 2, 330, s=1.1, z=10),
                             inst('o_asel', VW / 2, AY0, scaleX=1.55, scaleY=1.15, opacity=18, z=1)]
for r in range(AN):
    aj_insts += [inst('o_afila', 70, AY0 + r * ADY, s=1.2, z=10, vars=dict(r=r)), inst('o_avalor', 380, AY0 + r * ADY, s=1.2, z=10, vars=dict(r=r))]
for r, var in ((5, 'Volumen_Global'), (6, 'Volumen_Musica'), (7, 'Volumen_SFX'), (8, 'CrtNivel')):
    aj_insts.append(inst('o_avbarra', 380 + 60, AY0 + r * ADY, z=9, vars=dict(r=r, bx=380)))
VOLV = {5: 'Volumen_Global', 6: 'Volumen_Musica', 7: 'Volumen_SFX', 8: 'CrtNivel'}
aj = []
aj += fn('sigPersonal', 'desde, paso', [
    note('Siguiente nivel importado (LvIni = 0) desde "desde" en la direccion "paso", o -1.'),
    ev([], [C('localVar', variable='j', value='$desde')]),
    ev([], [], type='repeat', count='len($LvNombre) + 1'),
    ev([], [C('localVar', variable='j', value='$j + $paso')], level=1),
    ev([X('$j < 0 || $j >= len($LvNombre)')], [C('returnValue', value='-1')], level=1),
    ev([X('$LvIni[$j] == 0')], [C('returnValue', value='$j')], level=1),
    ev([], [C('returnValue', value='-1')]),
])
aj += in_folder('Ajustes: lista', [
    ev([C('atStart')], [SET('Sel', 0), SET('Esperando', -1), SET('EspT', 0), SET('Confirmar', 0), SET('MsgT', 0),
                        SET('Quitar', 'Fn.sigPersonal(-1, 1)'), SET('FondoRGB', '"70;50;150"'),
                        C('setText', object='o_atitulo', text='{= %s}' % tr('AJUSTES', 'SETTINGS'))]),
    note('Al volver del explorador con un .json: si es un nivel de Ry-mik se copia a la carpeta de niveles importados.'),
    ev([C('atStart'), X('$ArchivoModo == "nivel" && $ArchivoElegido != ""')], [
        C('fileRead', path='{= $ArchivoElegido}', variable='txtImp'), C('jsonToVars', source='txtImp', prefix='I'), SET('Sel', 10), SET('MsgT', 4)]),
    ev([C('cmpStr', a='{= $I.formato}', op='eq', b='rymik-nivel')], [
        C('fileWrite', path='{= DirDatos()}/ry-mik/niveles/{= substr($ArchivoElegido, lastIndexOf($ArchivoElegido, "/") + 1)}', source='txtImp'),
        SET('BibCargada', 0), SET('Msg', '%s + $I.nombre' % tr('Nivel importado: ', 'Level imported: '))], level=1),
    ev([C('cmpStr', True, a='{= $I.formato}', op='eq', b='rymik-nivel')], [SET('Msg', tr('Ese archivo no es un nivel de Ry-mik', 'That file is not a Ry-mik level'))], level=1),
    ev([], [SET('ArchivoElegido', '""'), SET('ArchivoModo', '""'), SET('txtImp', '""'), C('varsClear', prefix='I')], level=1),
] + lista_nav(AN, AY0, ADY, 40, 600, '$Esperando < 0') + [
    ev([], [C('setY', object='o_asel', value='%d + $Sel * %d' % (AY0, ADY)), C('setOpacity', object='o_asel', value='16 + 6 * sin($tt * 6)'), SET('tt', DT, 'add')]),
    ev([], [], type='forEach', object='o_afila'),
    ev([], [C('setText', object='o_afila', text='{= $Idioma == "en" ? %s[AFila.r] : %s[AFila.r]}' % (json.dumps([r[1] for r in A_ROWS]), json.dumps([r[0] for r in A_ROWS]))),
            C('setTextColor', object='o_afila', color='{= AFila.r == $Sel ? "#ffd640" : "#ffffff"}')], level=1),
    ev([], [], type='forEach', object='o_avalor'),
    ev([X('AValor.r < 5')], [C('setText', object='o_avalor', text='{= $Esperando == AValor.r ? %s : Fn.nombreTecla($Teclas[AValor.r])}' % tr('Pulsa una tecla...', 'Press a key...'))], level=1),
    ev([X('AValor.r >= 5 && AValor.r <= 8')], [C('setText', object='o_avalor', text='')], level=1),
    ev([X('AValor.r == 9')], [C('setText', object='o_avalor', text='{= $Idioma == "en" ? "English" : "Espanol"}')], level=1),
    ev([X('AValor.r == 10 || AValor.r == 12')], [C('setText', object='o_avalor', text='')], level=1),
    ev([X('AValor.r == 11')], [C('setText', object='o_avalor', text='{= $Quitar < 0 ? %s : ($Confirmar == 1 ? %s : "< " + $LvNombre[$Quitar] + " >")}' % (
        tr('(no hay)', '(none)'), tr('Enter otra vez para borrar', 'Enter again to delete')))], level=1),
    ev([], [C('setTextColor', object='o_avalor', color='{= AValor.r == $Sel ? "#ffd640" : "#c8c0ff"}')], level=1),
    ev([], [], type='forEach', object='o_avbarra'),
] + [ev([X('AVBarra.r == %d' % r)], [C('setScale', object='o_avbarra', x='max(0.02, $%s / 100)' % v, y=1),
                                     C('setX', object='o_avbarra', value='AVBarra.bx + 60 * max(0.02, $%s / 100)' % v)], level=1) for r, v in VOLV.items()] + [
])
aj += in_folder('Ajustes: acciones', [
    ev([X('$Esperando < 0 && %s' % LIBRE), C('keyPressed', key='escape')], [S_BACK()] + ir('inicio')),
    note('Controles: Enter y luego la tecla nueva (Esc cancela).'),
    ev([X('$Act == 1 && $Sel < 5')], [SET('Esperando', '$Sel'), SET('EspT', 0), S_CLICK()]),
    ev([X('$Esperando >= 0')], [SET('EspT', DT, 'add')]),
    ev([X('$EspT > 0.15 && UltimaTecla() != ""')], [], level=1),
    ev([X('UltimaTecla() == "escape"')], [SET('Esperando', -1), S_BACK()], level=2),
    ev([X('UltimaTecla() != "escape" && $Esperando >= 0')], [SET('Teclas[$Esperando]', 'UltimaTecla()'), SET('Esperando', -1), S_CLICK(), GUARDAR()], level=2),
    note('Volumenes: izquierda/derecha de 5 en 5, o arrastrando la barra.'),
] + [e for r, v in VOLV.items() for e in [
    ev([X('$Sel == %d && $Izq == 1' % r)], [SET(v, 'max(0, $%s - 5)' % v), S_HOVER(), GUARDAR()]),
    ev([X('$Sel == %d && $Der == 1' % r)], [SET(v, 'min(100, $%s + 5)' % v), S_HOVER(), GUARDAR()]),
    ev([C('mouseDown', button='left'), X('$FilaRaton == %d && cursor.X() >= 370 && cursor.X() <= 510' % r)], [SET(v, 'clamp((cursor.X() - 380) / 120, 0, 1) * 100')]),
]] + [
    ev([C('mouseReleased', button='left'), X('$FilaRaton >= 5 && $FilaRaton <= 8')], [GUARDAR()]),
    ev([X('$Sel == 9 && ($Act == 1 || $Izq == 1 || $Der == 1)')], [SET('Idioma', '$Idioma == "en" ? "es" : "en"'), S_CLICK(), GUARDAR()]),
    ev([X('$Sel == 10 && $Act == 1')], [SET('ArchivoModo', '"nivel"'), SET('ArchivoVolver', '"ajustes"'), S_CLICK()] + ir('archivos')),
    ev([X('$Sel == 11 && $Izq == 1 && $Quitar >= 0')], [SET('q2', 'Fn.sigPersonal($Quitar, -1)'), SET('Confirmar', 0)]),
    ev([X('$q2 >= 0')], [SET('Quitar', '$q2'), S_HOVER()], level=1),
    ev([X('$Sel == 11 && $Der == 1 && $Quitar >= 0')], [SET('q2', 'Fn.sigPersonal($Quitar, 1)'), SET('Confirmar', 0)]),
    ev([X('$q2 >= 0')], [SET('Quitar', '$q2'), S_HOVER()], level=1),
    ev([X('$Sel != 11')], [SET('Confirmar', 0)]),
    ev([X('$Sel == 11 && $Act == 1 && $Quitar >= 0 && $Confirmar == 1')], [
        C('fileDelete', path='{= $LvArchivo[$Quitar]}'), SET('Msg', '%s + $LvNombre[$Quitar]' % tr('Nivel quitado: ', 'Level removed: ')), SET('MsgT', 4),
        SET('BibCargada', 0), SET('Confirmar', 2), S_BACK()]),
    ev([X('$Sel == 11 && $Act == 1 && $Quitar >= 0 && $Confirmar == 0')], [SET('Confirmar', 1), S_CLICK()]),
    ev([X('$Confirmar == 2 && $BibCargada == 1')], [SET('Confirmar', 0)]),
    ev([X('$Quitar < 0 || $Quitar >= len($LvNombre) || $LvIni[$Quitar] != 0')], [SET('Quitar', 'Fn.sigPersonal(-1, 1)')]),
    ev([X('$Sel == 12 && $Act == 1')], [S_BACK()] + ir('inicio')),
    ev([X('$MsgT > 0')], [SET('MsgT', DT, 'sub'), C('setText', object='o_amsg', text='{= $Msg}'), C('setOpacity', object='o_amsg', value='min(100, $MsgT * 100)')]),
    ev([X('$MsgT <= 0')], [C('setOpacity', object='o_amsg', value=0)]),
])

# ======================================================================================================
#  PERFIL
# ======================================================================================================
S = SC['perfil']
P_COLS = [(50, 'left'), (300, 'center'), (385, 'center'), (470, 'center'), (565, 'center')]
PN = 8
objects += [txt('o_ptitulo', 'PTitulo', S), txt('o_pdato', 'PDato', S, align='left', vars=(('r', 0),)),
            txt('o_pcab', 'PCabecera', S, vars=(('c', 0),)), txt('o_pcelda', 'PCelda', S, vars=(('r', 0), ('c', 0))),
            txt('o_pcelda_i', 'PCeldaNombre', S, align='left', vars=(('r', 0), ('c', 0))), txt('o_payuda', 'PAyuda', S)]
pf_insts = burbujas(6, 4) + [inst('o_ptitulo', VW / 2, 24, s=2.2, z=10), inst('o_payuda', VW / 2, 340, z=10)]
for r, (x, y) in enumerate(((50, 56), (340, 56), (50, 78), (340, 78))):
    pf_insts.append(inst('o_pdato', x, y, s=1.2, z=10, vars=dict(r=r)))
for c, (x, al) in enumerate(P_COLS):
    pf_insts.append(inst('o_pcab', x if al == 'center' else x + 60, 118, s=1.1, z=10, vars=dict(c=c)))
    for r in range(PN):
        pf_insts.append(inst('o_pcelda_i' if c == 0 else 'o_pcelda', x, 140 + r * 22, s=1.1, z=10, vars=dict(r=r, c=c)))
CELDA = ('{= $j < len($LvNombre) ? (PCelda.c == 1 ? ($ri < 0 ? "-" : padStart(floor($RecPuntos[$ri]), 7, "0")) : '
         '(PCelda.c == 2 ? ($ri < 0 ? "-" : Fn.rango($RecPuntos[$ri])) : (PCelda.c == 3 ? ($ri < 0 ? "-" : fixed($RecPrec[$ri], 1) + " %") : '
         '($ri < 0 ? "0" : $RecJugadas[$ri])))) : ""}')
pf = in_folder('Perfil', [
    note('Totales: precision media de los niveles jugados, puntuacion total (suma de records), partidas y rangos.'),
    ev([C('atStart')], [SET('Desp', 0), SET('pTot', 0), SET('pPrec', 0), SET('pPart', 0), SET('pSS', 0), SET('pS', 0), SET('pA', 0),
                        SET('FondoRGB', '"50;70;150"'), C('setText', object='o_ptitulo', text='{= %s}' % tr('PERFIL', 'PROFILE')),
                        C('setText', object='o_payuda', text='{= %s}' % tr('Arriba/Abajo: mover la tabla    Esc: volver', 'Up/Down: scroll    Esc: back'))]),
    ev([], [], level=1, type='repeat', count='len($RecNombre)', var='i'),
    ev([], [SET('pTot', '$RecPuntos[$i]', 'add'), SET('pPrec', '$RecPrec[$i]', 'add'), SET('pPart', '$RecJugadas[$i]', 'add'),
            SET('pSS', 'Fn.rango($RecPuntos[$i]) == "SS" ? 1 : 0', 'add'), SET('pS', 'Fn.rango($RecPuntos[$i]) == "S" ? 1 : 0', 'add'),
            SET('pA', 'Fn.rango($RecPuntos[$i]) == "A" ? 1 : 0', 'add')], level=2),
    ev([], [SET('pMedia', 'len($RecNombre) > 0 ? $pPrec / len($RecNombre) : 0')], level=1),
    ev([], [], type='forEach', object='o_pdato'),
    ev([], [C('setText', object='o_pdato', text='{= [%s + fixed($pMedia, 1) + " %%", %s + $pTot, %s + $pPart, %s + len($RecNombre) + "     SS: " + $pSS + "   S: " + $pS + "   A: " + $pA][PDato.r]}' % (
        tr('Precision media: ', 'Average accuracy: '), tr('Puntuacion total: ', 'Total score: '), tr('Partidas: ', 'Plays: '), tr('Niveles completados: ', 'Levels cleared: ')))], level=1),
    ev([], [], type='forEach', object='o_pcab'),
    ev([], [C('setText', object='o_pcab', text='{= ($Idioma == "en" ? ["Level", "Best", "Rank", "Accuracy", "Plays"] : ["Nivel", "Record", "Rango", "Precision", "Partidas"])[PCabecera.c]}'),
            C('setTextColor', object='o_pcab', color='#ffd640')], level=1),
    ev([C('keyPressed', key='arrowdown')], [SET('Desp', 'min(max(0, len($LvNombre) - %d), $Desp + 1)' % PN)]),
    ev([C('keyPressed', key='arrowup')], [SET('Desp', 'max(0, $Desp - 1)')]),
    ev([C('wheelDown')], [SET('Desp', 'min(max(0, len($LvNombre) - %d), $Desp + 1)' % PN)]),
    ev([C('wheelUp')], [SET('Desp', 'max(0, $Desp - 1)')]),
    ev([], [], type='forEach', object='o_pcelda_i'),
    ev([], [SET('j', '$Desp + PCeldaNombre.r'), C('setText', object='o_pcelda_i', text='{= $j < len($LvNombre) ? $LvNombre[$j] : ""}')], level=1),
    ev([], [], type='forEach', object='o_pcelda'),
    ev([], [SET('j', '$Desp + PCelda.r'), SET('ri', '$j < len($LvNombre) ? Fn.recIdx($LvNombre[$j]) : -1'), C('setText', object='o_pcelda', text=CELDA)], level=1),
    ev([X(LIBRE), C('keyPressed', key='escape')], [S_BACK()] + ir('inicio')),
    ev([X(LIBRE), C('keyPressed', key='enter')], [S_BACK()] + ir('inicio')),
    ev([X(LIBRE), C('mousePressed', button='right')], [S_BACK()] + ir('inicio')),
])

# ======================================================================================================
#  ARCHIVOS: explorador
# ======================================================================================================
S = SC['archivos']
FN_ROWS, FY0, FDY = 13, 62, 19
objects += [txt('o_ftitulo', 'FTitulo', S), txt('o_fruta', 'FRuta', S, align='left'), txt('o_ffila', 'FFila', S, align='left', vars=(('r', 0),)),
            spr('o_fsel', 'FSel', S, 's_fila'), txt('o_fatajos', 'FAtajos', S)]
fa_insts = [inst('o_ftitulo', VW / 2, 16, s=1.6, z=10), inst('o_fruta', 20, 40, z=10), inst('o_fatajos', VW / 2, 330, z=10),
            inst('o_fsel', VW / 2, FY0, scaleX=1.65, scaleY=1.1, opacity=20, z=1)]
for r in range(FN_ROWS):
    fa_insts.append(inst('o_ffila', 34, FY0 + r * FDY, s=1.2, z=10, vars=dict(r=r)))
EXT = ('($ArchivoModo == "nivel" ? endsWith(lower($nm), ".json") : ($ArchivoModo == "musica" ? endsWith(lower($nm), ".mp3") || endsWith(lower($nm), ".ogg") || '
       'endsWith(lower($nm), ".wav") : endsWith(lower($nm), ".png") || endsWith(lower($nm), ".jpg") || endsWith(lower($nm), ".jpeg") || endsWith(lower($nm), ".bmp")))')
fa = in_folder('Explorador de archivos', [
    note('Lista la carpeta (".." sube): carpetas y los archivos del tipo que se busca. Enter o clic abre; Esc cancela.'),
    ev([C('atStart')], [SET('Recargar', 1), SET('Sel', 0), SET('Desp', 0), SET('FondoRGB', '"40;50;90"'),
                        C('setText', object='o_ftitulo', text='{= $ArchivoModo == "nivel" ? %s : ($ArchivoModo == "musica" ? %s : %s)}' % (
                            tr('Elige un nivel (.json)', 'Pick a level (.json)'), tr('Elige una cancion (.mp3, .ogg, .wav)', 'Pick a song (.mp3, .ogg, .wav)'),
                            tr('Elige una imagen (.png, .jpg)', 'Pick an image (.png, .jpg)'))),
                        C('setText', object='o_fatajos', text='{= %s}' % tr('1 Personal   2 Documentos   3 Descargas   4 Escritorio   Esc cancelar',
                                                                            '1 Home   2 Documents   3 Downloads   4 Desktop   Esc cancel'))]),
    ev([X('$DirArchivos == ""')], [SET('DirArchivos', 'DirDocumentos()')], level=1),
    ev([X(LIBRE)], []),
    ev([C('keyPressed', key='1')], [SET('DirArchivos', 'DirUsuario()'), SET('Recargar', 1)], level=1),
    ev([C('keyPressed', key='2')], [SET('DirArchivos', 'DirDocumentos()'), SET('Recargar', 1)], level=1),
    ev([C('keyPressed', key='3')], [SET('DirArchivos', 'DirUsuario() + "/Descargas"'), SET('Recargar', 1)], level=1),
    ev([C('keyPressed', key='4')], [SET('DirArchivos', 'DirUsuario() + "/Escritorio"'), SET('Recargar', 1)], level=1),
    ev([X('$Recargar == 1')], [SET('Recargar', 0), SET('Sel', 0), SET('Desp', 0), C('dirList', path='{= $DirArchivos}', variable='ArchLista'),
                              C('arrayClear', variable='Ents'), C('arrayPush', variable='Ents', value='..')]),
    ev([], [], level=1, type='repeat', count='len($ArchLista)', var='ai'),
    ev([], [SET('nm', '"" + $ArchLista[$ai]')], level=2),
    ev([X('endsWith($nm, "/") && substr($nm, 0, 1) != "."')], [C('arrayPush', variable='Ents', value='{= $nm}')], level=2),
    ev([X('!endsWith($nm, "/") && substr($nm, 0, 1) != "." && %s' % EXT)], [C('arrayPush', variable='Ents', value='{= $nm}')], level=2),
    ev([], [SET('Act', 0), SET('Movido', 'cursor.SX() != $mx0 || cursor.SY() != $my0 ? 1 : 0'), SET('mx0', 'cursor.SX()'), SET('my0', 'cursor.SY()'),
            SET('FilaRaton', 'cursor.Y() >= %g && cursor.Y() < %g ? floor((cursor.Y() - %g) / %d) : -1' % (FY0 - FDY / 2, FY0 + FN_ROWS * FDY - FDY / 2, FY0 - FDY / 2, FDY))]),
    ev([X(LIBRE)], []),
    ev([C('keyPressed', key='arrowdown')], [SET('Sel', 'min(len($Ents) - 1, $Sel + 1)')], level=1),
    ev([C('keyPressed', key='arrowup')], [SET('Sel', 'max(0, $Sel - 1)')], level=1),
    ev([C('wheelDown')], [SET('Desp', 'min(max(0, len($Ents) - %d), $Desp + 3)' % FN_ROWS)], level=1),
    ev([C('wheelUp')], [SET('Desp', 'max(0, $Desp - 3)')], level=1),
    ev([X('$Movido == 1 && $FilaRaton >= 0 && $Desp + $FilaRaton < len($Ents)')], [SET('Sel', '$Desp + $FilaRaton')], level=1),
    ev([C('keyPressed', key='enter')], [SET('Act', 1)], level=1),
    ev([C('mousePressed', button='left'), X('$FilaRaton >= 0 && $Desp + $FilaRaton == $Sel')], [SET('Act', 1)], level=1),
    ev([C('keyPressed', key='backspace')], [SET('Sel', 0), SET('Act', 1)], level=1),
    ev([C('keyPressed', key='escape')], [SET('ArchivoElegido', '""'), S_BACK(), SET('IrA', '$ArchivoVolver')], level=1),
    ev([X('$Sel < $Desp')], [SET('Desp', '$Sel')]),
    ev([X('$Sel >= $Desp + %d' % FN_ROWS)], [SET('Desp', '$Sel - %d' % (FN_ROWS - 1))]),
    ev([X('$Act == 1 && len($Ents) > 0')], [SET('nm', '"" + $Ents[$Sel]'), S_CLICK()]),
    ev([X('$nm == ".."')], [SET('pos', 'lastIndexOf($DirArchivos, "/")'), SET('DirArchivos', '$pos > 0 ? substr($DirArchivos, 0, $pos) : "/"'), SET('Recargar', 1)], level=1),
    ev([X('$nm != ".." && endsWith($nm, "/")')], [SET('DirArchivos', '($DirArchivos == "/" ? "" : $DirArchivos) + "/" + substr($nm, 0, len($nm) - 1)'), SET('Recargar', 1)], level=1),
    ev([X('$nm != ".." && !endsWith($nm, "/")')], [SET('ArchivoElegido', '($DirArchivos == "/" ? "" : $DirArchivos) + "/" + $nm'), SET('IrA', '$ArchivoVolver')], level=1),
    ev([], [C('setText', object='o_fruta', text='{= len($DirArchivos) > 70 ? "..." + substr($DirArchivos, len($DirArchivos) - 67, 67) : $DirArchivos}'),
            C('setY', object='o_fsel', value='%d + ($Sel - $Desp) * %d' % (FY0, FDY))]),
    ev([], [], type='forEach', object='o_ffila'),
    ev([], [SET('j', '$Desp + FFila.r'), C('setText', object='o_ffila', text='{= $j < len($Ents) ? $Ents[$j] : ""}'),
            C('setTextColor', object='o_ffila', color='{= $j == $Sel ? "#ffd640" : (endsWith("" + $Ents[$j], "/") || $Ents[$j] == ".." ? "#8fd8ff" : "#ffffff")}')], level=1),
])

# ======================================================================================================
#  EDITOR DE NIVELES
# ======================================================================================================
S = SC['editor']
E_ROWS = [('Nombre', 'Name'), ('Cancion', 'Song'), ('Logo', 'Logo'), ('Color (paleta)', 'Color (palette)'), ('Rojo', 'Red'), ('Verde', 'Green'),
          ('Azul', 'Blue'), ('Velocidad', 'Speed'), ('Retraso', 'Delay'), ('Abrir nivel', 'Open level'), ('Abrir archivo .json...', 'Open .json file...'),
          ('Grabar notas', 'Record notes'), ('Revisar notas', 'Review notes'), ('Probar nivel', 'Test level'), ('Exportar a JSON', 'Export to JSON'),
          ('Borrar notas', 'Clear notes'), ('Volver', 'Back')]
EY0, EDY, EN = 46, 16, len(E_ROWS)
LANE_X = [80, 240, 400, 560]
EF_TECLAS = [('q', 'S', 'ESTELA'), ('w', 'W', 'OLEAJE'), ('t', 'Y', 'FLASH'), ('y', 'T', 'TEMBLOR'), ('z', 'ZB', 'ZOOM'), ('x', 'R', 'GIRO'),
             ('a', 'A', 'ENFOQUE'), ('arrowleft', 'CL', 'CAMARA IZQ'), ('arrowright', 'CR', 'CAMARA DER'), ('0', 'Z0', 'SIN RESPIRAR')]
objects += [
    txt('o_efila', 'EFila', S, align='left', vars=(('r', 0),)), txt('o_evalor', 'EValor', S, align='left', vars=(('r', 0),)),
    spr('o_esel', 'ESel', S, 's_fila'), txt('o_etitulo', 'ETitulo', S), txt('o_emsg', 'EMensaje', S), txt('o_einfo', 'EInfo', S),
    spr('o_ecasete', 'ECasete', S, 's_casete'), spr('o_elogo', 'ELogo', S, 's_logo'), txt('o_enombre', 'ENombre', S, outline=False, color='#20202a'),
    spr('o_enotador', 'ENotador', S, 's_nota', vars=(('c', 0), ('tb', 255))), txt('o_etecla', 'ETecla', S, vars=(('c', 0),)),
    spr('o_emarca', 'EMarca', S, 's_nota', vars=(('t', 0), ('idx', -1), ('sc', 1))), spr('o_ecola', 'ECola', S, 's_blanco'),
    txt('o_eef', 'EEfecto', S, align='left', vars=(('idx', -1),)), txt('o_ecuenta', 'ECuenta', S), txt('o_eayuda', 'EAyuda', S, align='left'),
    txt('o_erec', 'ERec', S), txt('o_eflot', 'EFlot', S), spr('o_elinea', 'ELinea', S, 's_blanco'),
]
ed_insts = [inst('o_etitulo', 190, 18, s=1.6, z=10), inst('o_emsg', VW / 2, 336, s=1.1, z=60), inst('o_einfo', 500, 210, z=10),
            inst('o_esel', 190, EY0, scaleX=0.85, scaleY=0.95, opacity=18, z=1), inst('o_ecasete', 500, 120, z=5), inst('o_elogo', 450, 104, z=6),
            inst('o_enombre', 522, 104, z=6), inst('o_ecuenta', VW / 2, 150, s=4, z=50, opacity=0), inst('o_eayuda', 8, 46, z=50, opacity=0),
            inst('o_erec', 600, 16, s=1.5, z=50, opacity=0), inst('o_eflot', VW / 2, 60, s=1.6, z=50, opacity=0),
            inst('o_elinea', VW / 2, 304, scaleX=VW / 4, scaleY=0.5, opacity=0, z=2)]
for r in range(EN):
    ed_insts += [inst('o_efila', 24, EY0 + r * EDY, s=1.0, z=10, vars=dict(r=r)), inst('o_evalor', 180, EY0 + r * EDY, s=1.0, z=10, vars=dict(r=r))]
for c in range(4):
    ed_insts += [inst('o_enotador', LANE_X[c], 304, z=20, opacity=0, vars=dict(c=c, tb=255)), inst('o_etecla', LANE_X[c], 304, s=1.5, z=21, opacity=0, vars=dict(c=c))]
PANEL = ['o_efila', 'o_evalor', 'o_esel', 'o_etitulo', 'o_einfo', 'o_ecasete', 'o_elogo', 'o_enombre']
GRAB = ['o_enotador', 'o_etecla', 'o_elinea']
ED_ARRS = [('EdT', 't'), ('EdFin', 'fin'), ('EdC', 'c'), ('EdEsp', 'esp'), ('EdG', 'g'), ('EdH', 'h'), ('EdW', 'w')]
ED_EFS = [('EdET', 't'), ('EdEC', 'c'), ('EdED', 'dur'), ('EdEF', 'color')]
ed = []
ed += fn('insertarNota', 'tt, fin, c, esp', [
    note('Inserta una nota manteniendo el orden por tiempo (los arrays EdT, EdFin, EdC, EdEsp, EdG, EdH, EdW).'),
    ev([], [C('localVar', variable='p', value='len($EdT)')]),
    ev([], [], type='while', whileConds=[X('$p > 0 && $EdT[$p - 1] > $tt')]),
    ev([], [C('localVar', variable='p', value='$p - 1')], level=1),
    ev([], [C('arrayInsertAt', variable='EdT', index='$p', value='round($tt * 1000) / 1000'), C('arrayInsertAt', variable='EdFin', index='$p', value='$fin < 0 ? -1 : round($fin * 1000) / 1000'),
            C('arrayInsertAt', variable='EdC', index='$p', value='$c'), C('arrayInsertAt', variable='EdEsp', index='$p', value='$esp'),
            C('arrayInsertAt', variable='EdG', index='$p', value='2'), C('arrayInsertAt', variable='EdH', index='$p', value='6'),
            C('arrayInsertAt', variable='EdW', index='$p', value='5')]),
])
ed += fn('quitarNota', 'i', [ev([], [C('arrayRemoveAt', variable=a, index='$i') for a, _ in ED_ARRS])])
ed += fn('insertarEfecto', 'tt, codigo', [
    ev([], [C('localVar', variable='p', value='len($EdET)')]),
    ev([], [], type='while', whileConds=[X('$p > 0 && $EdET[$p - 1] > $tt')]),
    ev([], [C('localVar', variable='p', value='$p - 1')], level=1),
    ev([], [C('arrayInsertAt', variable='EdET', index='$p', value='round($tt * 1000) / 1000'), C('arrayInsertAt', variable='EdEC', index='$p', value='$codigo'),
            C('arrayInsertAt', variable='EdED', index='$p', value='0'), C('arrayInsertAt', variable='EdEF', index='$p', value='""')]),
])
COLOR_RGB = [SET('EdR', 'hex($EdFondo, 0)'), SET('EdG', 'hex($EdFondo, 1)'), SET('EdB', 'hex($EdFondo, 2)')]
ed += fn('cargarEnEditor', '', [
    note('Pasa al editor el nivel (JSON) que hay en txtEd: nombre, color, velocidad, cancion, logo, notas y efectos.'),
    ev([], [C('jsonToVars', source='txtEd', prefix='E'), SET('txtEd', '""')]),
    ev([C('cmpStr', True, a='{= $E.formato}', op='eq', b='rymik-nivel')], [SET('Msg', tr('Ese archivo no es un nivel de Ry-mik', 'That file is not a Ry-mik level')), SET('MsgT', 3),
                                                                         C('varsClear', prefix='E')]),
    ev([C('cmpStr', a='{= $E.formato}', op='eq', b='rymik-nivel')], [
        SET('EdNombre', '"" + $E.nombre'), SET('EdFondo', '"" + $E.fondo'), SET('EdVel', '$E.velocidad'), SET('EdRet', '$E.retraso'),
        SET('EdMusica', '$E.musica.datos'), SET('EdMusTipo', '$E.musica.tipo'), SET('EdLogo', '$E.logo.datos'), SET('EdLogoTipo', '$E.logo.tipo'),
        SET('EdCancion', '"" + $E.nombre'), SET('EdLogoVer', 1, 'add')] + COLOR_RGB +
       [C('arrayCopy', **{'from': 'E.notas.' + k, 'to': a}) for a, k in ED_ARRS] + [C('arrayCopy', **{'from': 'E.efectos.' + k, 'to': a}) for a, k in ED_EFS] +
       [C('arrayFill', variable='EdEsp', size='len($EdT)', value='0'),
        C('musicFromData', id='editor', source='EdMusica', mime='{= $EdMusTipo}'), SET('EdMusOk', '$EdMusica != "" ? 1 : 0'), C('varsClear', prefix='E'),
        SET('Msg', '%s + $EdNombre' % tr('Abierto: ', 'Opened: ')), SET('MsgT', 3)]),
    ev([X('$EdLogo != ""')], [C('imageFromData', object='o_elogo', key='{= "edlogo" + $EdLogoVer}', source='EdLogo')], level=1),
    ev([X('$EdLogo == ""')], [C('imageFromData', object='o_elogo', key='', source='EdLogo')], level=1),
])
ed += fn('leerNivelK', 'k', [
    note('Lee en txtEd el .json del nivel k de la biblioteca (archivo inicial o importado).'),
    ev([X('$LvIni[$k] == 1')], [C('initialFileRead', name='{= $LvArchivo[$k]}', variable='txtEd')]),
    ev([X('$LvIni[$k] == 0')], [C('fileRead', path='{= $LvArchivo[$k]}', variable='txtEd')]),
])
ed += in_folder('Editor: entrar y volver del explorador', [
    note('EdModo: 0 panel, 1 escribiendo el nombre, 2 grabando, 3 revisando la linea de tiempo.'),
    ev([C('atStart')], [SET('EdModo', 0), SET('ModoAnt', 0), SET('Sel', 0), SET('MsgT', 0), SET('BorrarOk', 0), SET('Tc', 0), SET('Tocando', 0),
                        C('setText', object='o_etitulo', text='{= %s}' % tr('EDITOR DE NIVELES', 'LEVEL EDITOR')), C('stopMusic')]),
    ev([X('$EdLogo != ""')], [C('imageFromData', object='o_elogo', key='{= "edlogo" + $EdLogoVer}', source='EdLogo')], level=1),
    ev([C('atStart'), X('$ArchivoModo == "musica" && $ArchivoElegido != ""')], [
        C('fileReadB64', path='{= $ArchivoElegido}', variable='EdMusica'), SET('Sel', 1),
        SET('EdMusTipo', 'endsWith(lower($ArchivoElegido), ".ogg") ? "audio/ogg" : (endsWith(lower($ArchivoElegido), ".wav") ? "audio/wav" : "audio/mpeg")'),
        SET('EdCancion', 'substr($ArchivoElegido, lastIndexOf($ArchivoElegido, "/") + 1)'),
        C('musicFromData', id='editor', source='EdMusica', mime='{= $EdMusTipo}'), SET('EdMusOk', '$EdMusica != "" ? 1 : 0'),
        SET('Msg', '%s + $EdCancion' % tr('Cancion: ', 'Song: ')), SET('MsgT', 3)]),
    ev([C('atStart'), X('$ArchivoModo == "imagen" && $ArchivoElegido != ""')], [
        C('fileReadB64', path='{= $ArchivoElegido}', variable='EdLogo'), SET('EdLogoVer', 1, 'add'), SET('Sel', 2),
        SET('EdLogoTipo', 'endsWith(lower($ArchivoElegido), ".png") ? "image/png" : "image/jpeg"'),
        C('imageFromData', object='o_elogo', key='{= "edlogo" + $EdLogoVer}', source='EdLogo')]),
    ev([C('atStart'), X('$ArchivoModo == "nivel" && $ArchivoElegido != ""')], [
        C('fileRead', path='{= $ArchivoElegido}', variable='txtEd'), SET('Sel', 10), C('callFunction', name='cargarEnEditor', args='', result='')]),
    ev([C('atStart')], [SET('ArchivoModo', '""'), SET('ArchivoElegido', '""')]),
])
ed += in_folder('Editor: panel', [
    note('La tecla que cambia de modo no cuenta en el modo nuevo: cada modo solo escucha si ya estaba activo al empezar el fotograma.'),
    ev([X('$EdModo != $ModoAnt')], [SET('ModoAnt', '$EdModo')]),
    ev([], [SET('ShiftOn', 0)]),
    ev([C('keyDown', key='shift')], [SET('ShiftOn', 1)]),
    ev([], [SET('EdFondo', '"#" + toHex($EdR, 2) + toHex($EdG, 2) + toHex($EdB, 2)'), SET('FondoRGB', 'floor($EdR * 0.6) + ";" + floor($EdG * 0.6) + ";" + floor($EdB * 0.6)')]),
] + sub(lista_nav(EN, EY0, EDY, 10, 330, '$EdModo == 0'), 0) + [
    ev([], [C('setY', object='o_esel', value='%d + $Sel * %d' % (EY0, EDY)), SET('tt', DT, 'add')]),
    ev([], [], type='forEach', object='o_efila'),
    ev([], [C('setText', object='o_efila', text='{= $Idioma == "en" ? %s[EFila.r] : %s[EFila.r]}' % (json.dumps([r[1] for r in E_ROWS]), json.dumps([r[0] for r in E_ROWS]))),
            C('setTextColor', object='o_efila', color='{= EFila.r == $Sel ? "#ffd640" : "#ffffff"}')], level=1),
    ev([], [], type='forEach', object='o_evalor'),
    ev([], [C('setText', object='o_evalor', text='{= [$EdModo == 1 ? $EdNombre + (floor($tt * 3) %% 2 ? "_" : " ") : $EdNombre, '
                                                 '"< " + ($EdMusOk ? $EdCancion : %s) + " >", $EdLogo != "" ? %s : %s, "< " + $EdFondo + " >", '
                                                 '"< " + $EdR + " >", "< " + $EdG + " >", "< " + $EdB + " >", "< " + $EdVel + " >", '
                                                 '"< " + fixed($EdRet, 2) + " s >", len($LvNombre) > 0 ? "< " + $LvNombre[clamp($EdAbrir, 0, len($LvNombre) - 1)] + " >" : %s, '
                                                 '"", "", "", "", "", $BorrarOk == 1 ? %s : "", ""][EValor.r]}' % (
        tr('Enter: archivo   flechas: de un nivel', 'Enter: file   arrows: from a level'), tr('(imagen elegida)', '(image chosen)'), tr('(elige una imagen)', '(pick an image)'),
        tr('(no hay)', '(none)'), tr('Enter otra vez', 'Enter again'))),
            C('setTextColor', object='o_evalor', color='{= EValor.r == $Sel ? "#ffd640" : "#c8c0ff"}')], level=1),
    ev([], [C('setText', object='o_enombre', text='{= len($EdNombre) > 13 ? substr($EdNombre, 0, 12) + "." : $EdNombre}'),
            C('setText', object='o_einfo', text='{= %s + len($EdT) + "    " + %s + len($EdET)}' % (tr('Notas: ', 'Notes: '), tr('Efectos: ', 'Effects: ')))]),
    note('Cada fila: Enter (o clic) y las flechas laterales.'),
    ev([X('$EdModo == 0 && $ModoAnt == $EdModo && %s' % LIBRE)], []),
    ev([X('$Sel == 0 && $Act == 1')], [SET('EdModo', 1), S_CLICK()], level=1),
    ev([X('$Sel == 1 && $Act == 1')], [SET('ArchivoModo', '"musica"'), SET('ArchivoVolver', '"editor"'), S_CLICK()] + ir('archivos'), level=1),
    ev([X('$Sel == 1 && ($Izq == 1 || $Der == 1) && len($LvNombre) > 0')], [
        SET('EdCancK', '($EdCancK + ($Izq == 1 ? len($LvNombre) - 1 : 1)) % len($LvNombre)'), C('callFunction', name='leerNivelK', args='$EdCancK', result=''),
        C('jsonToVars', source='txtEd', prefix='E'), SET('txtEd', '""'), SET('EdMusica', '$E.musica.datos'), SET('EdMusTipo', '$E.musica.tipo'),
        SET('EdCancion', '$LvNombre[$EdCancK]'), C('varsClear', prefix='E'),
        C('musicFromData', id='editor', source='EdMusica', mime='{= $EdMusTipo}'), SET('EdMusOk', '$EdMusica != "" ? 1 : 0'), S_HOVER()], level=1),
    ev([X('$Sel == 2 && $Act == 1')], [SET('ArchivoModo', '"imagen"'), SET('ArchivoVolver', '"editor"'), S_CLICK()] + ir('archivos'), level=1),
    ev([X('$Sel == 3 && ($Izq == 1 || $Der == 1 || $Act == 1)')], [SET('EdColI', '($EdColI + ($Izq == 1 ? len($Paleta) - 1 : 1)) % len($Paleta)'),
                                                                  SET('EdFondo', '$Paleta[$EdColI]')] + COLOR_RGB + [S_HOVER()], level=1),
] + [e for r, v in ((4, 'EdR'), (5, 'EdG'), (6, 'EdB')) for e in [
    ev([X('$Sel == %d && $Izq == 1' % r)], [SET(v, 'max(0, $%s - ($ShiftOn ? 1 : 5))' % v), S_HOVER()], level=1),
    ev([X('$Sel == %d && $Der == 1' % r)], [SET(v, 'min(255, $%s + ($ShiftOn ? 1 : 5))' % v), S_HOVER()], level=1)]] + [
    ev([X('$Sel == 7 && $Izq == 1')], [SET('EdVel', 'max(200, $EdVel - 20)'), S_HOVER()], level=1),
    ev([X('$Sel == 7 && $Der == 1')], [SET('EdVel', 'min(1400, $EdVel + 20)'), S_HOVER()], level=1),
    ev([X('$Sel == 8 && $Izq == 1')], [SET('EdRet', 'max(0, round(($EdRet - 0.05) * 100) / 100)'), S_HOVER()], level=1),
    ev([X('$Sel == 8 && $Der == 1')], [SET('EdRet', 'min(5, round(($EdRet + 0.05) * 100) / 100)'), S_HOVER()], level=1),
    ev([X('$Sel == 9 && $Izq == 1')], [SET('EdAbrir', 'max(0, $EdAbrir - 1)'), S_HOVER()], level=1),
    ev([X('$Sel == 9 && $Der == 1')], [SET('EdAbrir', 'min(max(0, len($LvNombre) - 1), $EdAbrir + 1)'), S_HOVER()], level=1),
    ev([X('$Sel == 9 && $Act == 1 && len($LvNombre) > 0')], [S_CLICK(), C('callFunction', name='leerNivelK', args='clamp($EdAbrir, 0, len($LvNombre) - 1)', result=''),
                                                            C('callFunction', name='cargarEnEditor', args='', result='')], level=1),
    ev([X('$Sel == 10 && $Act == 1')], [SET('ArchivoModo', '"nivel"'), SET('ArchivoVolver', '"editor"'), S_CLICK()] + ir('archivos'), level=1),
    ev([X('($Sel == 11 || $Sel == 12 || $Sel == 13) && $Act == 1 && $EdMusOk == 0')], [SET('Msg', tr('Primero elige una cancion', 'Pick a song first')), SET('MsgT', 3), S_BACK()], level=1),
    ev([X('$Sel == 11 && $Act == 1 && $EdMusOk == 1')], [SET('EdModo', 2), SET('GT', -3), SET('Tocando', 0), S_CLICK()] +
       [C('arrayFill', variable=v, size='4', value='0') for v in ('RecIni', 'RecOn')], level=1),
    ev([X('$Sel == 12 && $Act == 1 && $EdMusOk == 1')], [SET('EdModo', 3), SET('Tocando', 0), S_CLICK()], level=1),
    ev([X('$Sel == 13 && $Act == 1 && $EdMusOk == 1 && len($EdT) > 0')], [SET('Seleccion', -1), SET('VolverA', '"editor"'), S_CLICK()] + ir('juego'), level=1),
    ev([X('$Sel == 13 && $Act == 1 && $EdMusOk == 1 && len($EdT) == 0')], [SET('Msg', tr('Graba alguna nota primero', 'Record some notes first')), SET('MsgT', 3)], level=1),
    ev([X('$Sel == 14 && $Act == 1')], [S_CLICK()], level=1),
    ev([X('trim($EdNombre) == "" || $EdMusOk == 0')], [SET('Msg', tr('Pon un nombre y una cancion', 'Set a name and a song')), SET('MsgT', 3)], level=2),
    ev([X('trim($EdNombre) != "" && $EdMusOk == 1')], [
        C('varsClear', prefix='X'), SET('X.formato', '"rymik-nivel"'), SET('X.version', 1), SET('X.nombre', 'trim($EdNombre)'), SET('X.fondo', '$EdFondo'),
        SET('X.velocidad', '$EdVel'), SET('X.retraso', '$EdRet'), SET('X.musica.tipo', '$EdMusTipo'), SET('X.musica.datos', '$EdMusica'),
        SET('X.logo.tipo', '$EdLogoTipo'), SET('X.logo.datos', '$EdLogo')] +
       [C('arrayCopy', **{'from': a, 'to': 'X.notas.' + k}) for a, k in ED_ARRS] + [C('arrayCopy', **{'from': a, 'to': 'X.efectos.' + k}) for a, k in ED_EFS] + [
        C('varsToJson', prefix='X', variable='txtExp'), C('varsClear', prefix='X'), SET('nf', 'Fn.limpio($EdNombre) + ".json"'),
        C('fileWrite', path='{= DirDocumentos()}/Ry-mik/{= $nf}', source='txtExp'),
        C('fileWrite', path='{= DirDatos()}/ry-mik/niveles/{= $nf}', source='txtExp'), SET('txtExp', '""'), SET('BibCargada', 0),
        SET('Msg', '%s + $nf' % tr('Guardado en Documentos/Ry-mik/', 'Saved to Documents/Ry-mik/')), SET('MsgT', 5)], level=2),
    ev([X('$Sel != 15')], [SET('BorrarOk', 0)], level=1),
    ev([X('$Sel == 15 && $Act == 1 && $BorrarOk == 1')], [SET('BorrarOk', 0), S_BACK(), SET('Msg', tr('Notas borradas', 'Notes cleared')), SET('MsgT', 3)] +
       [C('arrayClear', variable=a) for a, _ in ED_ARRS + ED_EFS], level=1),
    ev([X('$Sel == 15 && $Act == 1 && $BorrarOk == 0 && len($EdT) + len($EdET) > 0')], [SET('BorrarOk', 1)], level=1),
    ev([X('$Sel == 16 && $Act == 1')], [S_BACK()] + ir('inicio'), level=1),
    ev([C('keyPressed', key='escape')], [S_BACK()] + ir('inicio'), level=1),
])
ed += in_folder('Editor: escribir el nombre', [
    ev([X('$EdModo == 1 && $ModoAnt == $EdModo')], []),
    ev([X('Tecleado() != "" && len($EdNombre) < 28')], [SET('EdNombre', '$EdNombre + Tecleado()')], level=1),
    ev([C('keyPressed', key='backspace')], [SET('EdNombre', 'substr($EdNombre, 0, max(0, len($EdNombre) - 1))')], level=1),
    ev([C('keyPressed', key='enter')], [SET('EdModo', 0), S_CLICK()], level=1),
    ev([C('keyPressed', key='escape')], [SET('EdModo', 0), S_BACK()], level=1),
])
VIS = lambda objs, cond: [C('setOpacity', object=o, value='%s ? 100 : 0' % cond) for o in objs]
ed += in_folder('Editor: que se ve en cada modo', [
    ev([], VIS(PANEL, '$EdModo <= 1') + VIS(GRAB, '$EdModo >= 2') +
       [C('setOpacity', object='o_esel', value='$EdModo <= 1 ? 18 : 0'), C('setOpacity', object='o_eayuda', value='$EdModo >= 2 ? 100 : 0'),
        C('setOpacity', object='o_erec', value='$EdModo == 2 && $GT >= 0 && floor($tt * 2) % 2 == 0 ? 100 : 0')]),
    ev([X('$EdModo == 2')], [C('setText', object='o_eayuda', text='{= %s}' % tr(
        'Teclas de carril: notas (mantener = larga)   Q estela  W oleaje  T flash  Y temblor\n'
        'Z zoom  X giro  A enfoque  flechas: camara  E+1/2/3 alejar  R+1/2/3 respirar  0 parar   Enter o Esc: terminar',
        'Lane keys: notes (hold = long)   Q trail  W waves  T flash  Y shake\n'
        'Z zoom  X spin  A focus  arrows: camera  E+1/2/3 zoom out  R+1/2/3 breathe  0 stop   Enter or Esc: finish')),
        C('setText', object='o_erec', text='REC'), C('setTextColor', object='o_erec', color='#ff4040')]),
    ev([X('$EdModo == 3')], [C('setText', object='o_eayuda', text='{= %s}' % tr(
        'Rueda/arriba/abajo: moverse (Shift: rapido)   Espacio: escuchar   Clic en un carril: nota nueva\n'
        'Clic derecho: borrar nota o efecto   Enter o Esc: volver',
        'Wheel/up/down: move (Shift: fast)   Space: listen   Click a lane: new note\n'
        'Right click: delete note or effect   Enter or Esc: back'))]),
    ev([], [], type='forEach', object='o_etecla'),
    ev([], [C('setText', object='o_etecla', text='{= Fn.nombreTecla($Teclas[ETecla.c])}')], level=1),
])
LANEX = '($c == 0 ? 80 : ($c == 1 ? 240 : ($c == 2 ? 400 : 560)))'
ed += in_folder('Editor: grabar', [
    note('Cuenta atras de 3 s y suena la cancion: cada tecla de carril graba una nota (mantenida 0.5 s o mas, larga).'),
    ev([X('$EdModo == 2')], [SET('GT', DT, 'add'), C('setOpacity', object='o_ecuenta', value='$GT < 0 ? 100 : 0'),
                             C('setText', object='o_ecuenta', text='{= ceil(-$GT)}')]),
    ev([X('$GT >= 0 && $Tocando == 0')], [SET('Tocando', 1), C('playMusic', track='editor', loop='no'),
                                         C('musicVolume', value='$Volumen_Global * $Volumen_Musica / 100')], level=1),
    ev([X('$Tocando == 1'), C('musicPlaying', True)], [SET('fin', 1)], level=1),
    ev([C('keyPressed', key='enter'), X('$ModoAnt == $EdModo')], [SET('fin', 1)], level=1),
    ev([C('keyPressed', key='escape'), X('$ModoAnt == $EdModo')], [SET('fin', 1)], level=1),
    ev([], [], level=1, type='repeat', count='4', var='c'),
    ev([C('keyPressed', key='{= $Teclas[$c]}')], [SET('RecIni[$c]', 'max(0, $GT)'), SET('RecOn[$c]', 1),
                                                 C('create', object='o_emarca', x=LANEX, y=304), C('setObjVar', object='o_emarca', variable='t', value='{= max(0, $GT)}'),
                                                 C('setColor', object='o_emarca', color='255;255;0')], level=2),
    ev([C('objVar', object='o_enotador', variable='c', op='eq', value='{= $c}'), C('keyPressed', key='{= $Teclas[$c]}')], [
        C('setObjVar', object='o_enotador', variable='tb', value='0'), C('setScale', object='o_enotador', x=0.85, y=0.85)], level=2),
    ev([C('keyReleased', key='{= $Teclas[$c]}'), X('$RecOn[$c] == 1')], [
        SET('RecOn[$c]', 0), SET('du', 'max(0, $GT) - $RecIni[$c]'),
        C('callFunction', name='insertarNota', args='$RecIni[$c], $du >= 0.5 ? $RecIni[$c] + $du : -1, $c, 0', result='')], level=2),
    ev([], [SET('ShiftOn', 0), SET('CtrlOn', 0)], level=1),
    ev([C('keyDown', key='shift')], [SET('ShiftOn', 1)], level=1),
    ev([C('keyDown', key='control')], [SET('CtrlOn', 1)], level=1),
] + [ev([C('keyPressed', key=k), C('keyDown', True, key='e'), C('keyDown', True, key='r')],
        [C('callFunction', name='insertarEfecto', args='max(0, $GT), "%s"' % code, result=''), C('setText', object='o_eflot', text=nm), SET('flotT', 1)], level=1)
     for k, code, nm in EF_TECLAS] + [
    ev([C('keyDown', key=g), C('keyPressed', key=str(d))], [C('callFunction', name='insertarEfecto', args='max(0, $GT), "%s%d"' % (code, d), result=''),
                                                          C('setText', object='o_eflot', text='%s %d' % (nm, d)), SET('flotT', 1)], level=1)
    for g, code, nm in (('e', 'O', 'ALEJAR'), ('r', 'Z', 'RESPIRAR')) for d in (1, 2, 3)] + [
    ev([X('$fin == 1')], [SET('fin', 0), SET('EdModo', 0), C('stopMusic'), C('removeObj', object='o_emarca'),
                         SET('Msg', '%s + len($EdT) + " / " + len($EdET)' % tr('Notas / efectos: ', 'Notes / effects: ')), SET('MsgT', 3)], level=1),
    note('Las notas grabadas suben desde los notadores; los notadores se iluminan al tocar.'),
    ev([X('$EdModo == 2')], [], type='forEach', object='o_emarca'),
    ev([], [C('setY', object='o_emarca', value='304 - (max(0, $GT) - EMarca.t) * $EdVel')], level=1),
    ev([X('EMarca.Y < -40')], [C('removeObj', object='o_emarca')], level=1),
    ev([X('$EdModo >= 2')], [], type='forEach', object='o_enotador'),
    ev([X('ENotador.tb < 255')], [C('setObjVar', object='o_enotador', variable='tb', value='{= min(255, ENotador.tb + 600 * TiempoDelta())}'),
                                  C('setColor', object='o_enotador', color='255;255;{= floor(ENotador.tb / 16) * 16 + (ENotador.tb >= 255 ? 15 : 0)}'),
                                  C('setScale', object='o_enotador', x='1 - 0.15 * (1 - ENotador.tb / 255)', y='1 - 0.15 * (1 - ENotador.tb / 255)')], level=1),
    ev([X('$flotT > 0')], [SET('flotT', DT, 'sub'), C('setOpacity', object='o_eflot', value='$flotT * 100')]),
    ev([X('$flotT <= 0')], [C('setOpacity', object='o_eflot', value=0)]),
])
TYE = '288 - ($EdT[$i] - $Tc) * $EdVel'
ed += in_folder('Editor: revisar', [
    note('Linea de tiempo como en el juego, alrededor del instante Tc. Las notas se dibujan de nuevo cada fotograma.'),
    ev([X('$EdModo == 3')], [C('removeObj', object='o_emarca'), C('removeObj', object='o_ecola'), C('removeObj', object='o_eef'),
                             SET('pasoT', '$ShiftOn ? 1 : 0.1'), SET('ShiftOn', 0)]),
    ev([C('keyDown', key='shift')], [SET('ShiftOn', 1), SET('pasoT', 1)], level=1),
    ev([C('wheelUp')], [SET('Tc', 'max(0, $Tc - $pasoT)')], level=1),
    ev([C('wheelDown')], [SET('Tc', '$Tc + $pasoT')], level=1),
    ev([C('keyDown', key='arrowup')], [SET('Tc', '$Tc + 3 * $pasoT * %s * 10' % DT)], level=1),
    ev([C('keyDown', key='arrowdown')], [SET('Tc', 'max(0, $Tc - 3 * $pasoT * %s * 10)' % DT)], level=1),
    ev([C('keyPressed', key=' ')], [SET('Tocando', '1 - $Tocando')], level=1),
    ev([X('$Tocando == 1'), C('musicPlaying', True)], [C('playMusic', track='editor', loop='no'), C('musicSeek', seconds='$Tc'),
                                                      C('musicVolume', value='$Volumen_Global * $Volumen_Musica / 100')], level=2),
    ev([X('$Tocando == 1')], [SET('Tc', DT, 'add')], level=1),
    ev([X('$Tocando == 0'), C('musicPlaying')], [C('stopMusic')], level=1),
    ev([], [], level=1, type='repeat', count='len($EdT)', var='i'),
    ev([X('$EdT[$i] <= $Tc + 700 / $EdVel && max($EdT[$i], $EdFin[$i]) >= $Tc - 0.5')], [SET('c', '$EdC[$i]')], level=2),
    ev([X('$EdFin[$i] >= 0')], [C('create', object='o_ecola', x=LANEX, y='(%s + 288 - ($EdFin[$i] - $Tc) * $EdVel) / 2 + 16' % TYE),
                                C('setScale', object='o_ecola', x=1.5, y='($EdFin[$i] - $EdT[$i]) * $EdVel / 4'), C('setZ', object='o_ecola', value=18),
                                C('setColor', object='o_ecola', color='255;180;0')], level=3),
    ev([], [C('create', object='o_emarca', x=LANEX, y='%s + 16' % TYE), C('setObjVar', object='o_emarca', variable='idx', value='{= $i}'), C('setZ', object='o_emarca', value=20),
            C('setColor', object='o_emarca', color='255;255;0')], level=3),
    ev([], [], level=1, type='repeat', count='len($EdET)', var='i'),
    ev([X('$EdET[$i] <= $Tc + 700 / $EdVel && $EdET[$i] >= $Tc - 0.5')], [
        C('create', object='o_eef', x=6, y='288 - ($EdET[$i] - $Tc) * $EdVel + 16'), C('setText', object='o_eef', text='{= $EdEC[$i]}'),
        C('setObjVar', object='o_eef', variable='idx', value='{= $i}'), C('setTextColor', object='o_eef', color='#ff88ff'), C('setZ', object='o_eef', value=30)], level=2),
    ev([C('mousePressed', button='right'), C('mouseOn', object='o_emarca')], [C('callFunction', name='quitarNota', args='EMarca.idx', result=''), sfx('sawtooth', 300, 80, 0.1, 0.3)], level=1),
    ev([C('mousePressed', button='right'), C('mouseOn', object='o_eef')], [C('arrayRemoveAt', variable=a, index='EEfecto.idx') for a, _ in ED_EFS] + [sfx('sawtooth', 300, 80, 0.1, 0.3)], level=1),
    ev([], [SET('sobre', 0)], level=1),
    ev([C('mouseOn', object='o_emarca')], [SET('sobre', 1)], level=1),
    ev([C('mousePressed', button='left'), X('$sobre == 0 && cursor.Y() > 30 && cursor.Y() < 330')], [
        SET('cc', 'round((cursor.X() - 80) / 160)')], level=1),
    ev([X('$cc >= 0 && $cc <= 3 && abs(cursor.X() - (80 + $cc * 160)) <= 48')], [
        C('callFunction', name='insertarNota', args='max(0, $Tc + (288 - (cursor.Y() - 16)) / $EdVel), -1, $cc, 0', result=''), sfx('triangle', 1200, 300, 0.06, 0.4)], level=2),
] + [
    ev([C('keyPressed', key='enter'), X('$ModoAnt == $EdModo')], [SET('EdModo', 0), SET('Tocando', 0), C('stopMusic')], level=1),
    ev([C('keyPressed', key='escape'), X('$ModoAnt == $EdModo')], [SET('EdModo', 0), SET('Tocando', 0), C('stopMusic')], level=1),
    ev([X('$EdModo != 3')], [C('removeObj', object='o_ecola'), C('removeObj', object='o_eef')]),
    ev([X('$EdModo == 0')], [C('removeObj', object='o_emarca')]),
    ev([X('$EdModo == 3')], [C('setOpacity', object='o_ecuenta', value=100), C('setText', object='o_ecuenta', text='{= fixed($Tc, 2)} s'),
                             C('setScale', object='o_ecuenta', x=1.5, y=1.5), C('setY', object='o_ecuenta', value=20)]),
    ev([X('$EdModo == 2')], [C('setScale', object='o_ecuenta', x=4, y=4), C('setY', object='o_ecuenta', value=150)]),
    ev([X('$MsgT > 0')], [SET('MsgT', DT, 'sub'), C('setText', object='o_emsg', text='{= $Msg}'), C('setOpacity', object='o_emsg', value='min(100, $MsgT * 100)')]),
    ev([X('$MsgT <= 0')], [C('setOpacity', object='o_emsg', value=0)]),
])

# ======================================================================================================
#  JUEGO
# ======================================================================================================
S = SC['juego']
YN, YS, LIM = 288, -400, 338
KEYS = ['{= $Teclas[%d]}' % c for c in range(4)]
objects += [
    spr('o_notador', 'Notador', S, 's_nota', vars=tuple((n, v) for n, v in (
        ('carril', 0), ('bx', 0), ('by', 0), ('yo', 300), ('ta', 0), ('ret', 0), ('tb', 255), ('gris', 0), ('gt', 0),
        ('sh', 0), ('salto', 0), ('tc', 0), ('sc', 1.3), ('op', 0)))),
    spr('o_nota', 'Nota', S, 's_nota', vars=tuple((n, 0) for n in ('t', 'tipo', 'esp', 'gA', 'gR', 'alt', 'w', 'carril', 'uid', 'sc', 'ty', 'fall'))),
    txt('o_tecla', 'Tecla', S, vars=(('carril', 0),)), txt('o_puntuacion', 'Puntuacion', S), txt('o_combo', 'Combo', S),
    txt('o_notificacion', 'Notificacion', S, align='left', hud=True), txt('o_flotante', 'Flotante', S, vars=(('vida', 255),)),
    txt('o_nnivel', 'NombreNivel', S, align='right', hud=True),
    spr('o_estela', 'Estela', S, 's_nota', vars=tuple((n, 0) for n in ('vx', 'vy', 'vr', 'op'))),
    spr('o_fundido', 'Fundido', S, 's_blanco', hud=True),
    spr('o_rebicono', 'RebobinarIcono', S, 's_rebobinar', hud=True), txt('o_rebtxt', 'RebobinarTexto', S, hud=True),
]
PUNT_Y, COMBO_Y = 128, 160


def tcy(y, s):
    return y + CELL_H * s / 2


game_insts = []
for c in range(4):
    game_insts.append(inst('o_notador', LANE_X[c], YN + 16, z=10, opacity=0, s=1.3, angle=-360, vars=dict(carril=c, ret=c * 0.18)))
game_insts += [inst('o_puntuacion', VW / 2, tcy(PUNT_Y, 1.2), z=100, s=1.2), inst('o_combo', VW / 2, tcy(COMBO_Y, 1.3), z=100, s=1.3, opacity=0),
               inst('o_notificacion', 0, tcy(0, 1.4), z=200, s=1.4, opacity=0), inst('o_nnivel', VW - 6, tcy(0, 1.2), z=200, s=1.2),
               inst('o_fundido', VW / 2, VH / 2, z=1000, scaleX=VW / 4 + 1, scaleY=VH / 4 + 1, opacity=0),
               inst('o_rebicono', VW / 2 - 150, VH / 2, s=1.5, z=900, opacity=0), inst('o_rebtxt', VW / 2 + 28, VH / 2, s=2.4, z=900, opacity=0)]

CAIDA = '(%d - (%d)) / $V' % (YN, YS)
NOT_PAUSA = '$Pausa == 0'
TY = '%d - (Nota.t - $TM) * $V' % YN
DIST = 'abs((Nota.t - $TM) * $V)'
SC_LANE = lambda c: C('objVar', object='o_notador', variable='carril', op='eq', value=str(c))
g = []
g += fn('eioCubic', 't', [ev([], [C('returnValue', value='$t < 0.5 ? 4 * $t * $t * $t : 1 - pow(-2 * $t + 2, 3) / 2')])])
g += fn('eioSine', 't', [ev([], [C('returnValue', value='-(cos(PI * $t) - 1) / 2')])])
g += fn('backOut', 't', [ev([], [C('returnValue', value='1 + 2.70158 * pow($t - 1, 3) + 1.70158 * pow($t - 1, 2)')])])
g += fn('elasticOut', 't', [
    ev([X('$t <= 0')], [C('returnValue', value='0')]),
    ev([X('$t >= 1')], [C('returnValue', value='1')]),
    ev([], [C('returnValue', value='pow(2, -10 * $t) * sin(($t - 0.35 / 4) * (2 * PI) / 0.35) + 1')])])
g += fn('grado', 'pct, perdida', [
    ev([X('$pct >= 1 && $perdida == 0')], [C('returnValue', value='"SS"')]),
    ev([X('$pct >= 0.95')], [C('returnValue', value='"S"')]),
    ev([X('$pct >= 0.9')], [C('returnValue', value='"A"')]),
    ev([X('$pct >= 0.8')], [C('returnValue', value='"B"')]),
    ev([X('$pct >= 0.7')], [C('returnValue', value='"C"')]),
    ev([X('$pct >= 0.6')], [C('returnValue', value='"D"')]),
    ev([], [C('returnValue', value='"F"')])])
g += fn('flotante', 'x, y, texto', [
    ev([], [C('create', object='o_flotante', x='$x', y='$y - 30 + %g' % (CELL_H * 1.5 / 2)),
            C('setText', object='o_flotante', text='{= $texto}'), C('setScale', object='o_flotante', x=1.5, y=1.5),
            C('setZ', object='o_flotante', value=150), C('setObjVar', object='o_flotante', variable='vida', value='255'),
            C('setX', object='o_flotante', value='$x + Flotante.Ancho / 2')])])
g += fn('explotar', 'x, y', [
    ev([X('$ModoEstela == 1')], [], type='repeat', count='8'),
    ev([], [C('localVar', variable='es', value='0.3 + random() * 0.4'), C('create', object='o_estela', x='$x', y='$y'), C('setZ', object='o_estela', value=110),
            C('setColor', object='o_estela', color='255;255;0'), C('setScale', object='o_estela', x='$es', y='$es'),
            C('setObjVar', object='o_estela', variable='vx', value='{= (random() - 0.5) * 800}'),
            C('setObjVar', object='o_estela', variable='vy', value='{= -(random() * 600) - 200}'),
            C('setObjVar', object='o_estela', variable='vr', value='{= (random() - 0.5) * 1200}'),
            C('setObjVar', object='o_estela', variable='op', value='255')], level=1),
])
g += fn('aparecer', 'tt, carril, tipo, esp, gr, alt, w', [
    ev([], [C('create', object='o_nota', x='$carril == 0 ? 80 : ($carril == 1 ? 240 : ($carril == 2 ? 400 : 560))', y=-1000),
            C('setObjVar', object='o_nota', variable='t', value='{= $tt}'), C('setObjVar', object='o_nota', variable='tipo', value='{= $tipo}'),
            C('setObjVar', object='o_nota', variable='esp', value='{= $esp}'), C('setObjVar', object='o_nota', variable='carril', value='{= $carril}'),
            C('setObjVar', object='o_nota', variable='gR', value='{= $gr}'), C('setObjVar', object='o_nota', variable='alt', value='{= $alt}'),
            C('setObjVar', object='o_nota', variable='w', value='{= $w}'), C('setObjVar', object='o_nota', variable='gA', value='0'),
            C('setObjVar', object='o_nota', variable='uid', value='{= $uidSig}'), SET('uidSig', 1, 'add'),
            C('setObjVar', object='o_nota', variable='sc', value='{= $tipo == 0 ? 1 : ($tipo == 1 ? 0.55 : 0.8)}'),
            C('setScale', object='o_nota', x='Nota.sc', y='Nota.sc'),
            C('setZ', object='o_nota', value='$tipo == 2 ? 21 : ($tipo == 1 ? 19 : 20)'),
            C('setY', object='o_nota', value='%s + 16 * Nota.sc' % TY)]),
    ev([X('$tipo == 1')], [C('setOpacity', object='o_nota', value='200 / 2.55')]),
    ev([X('$esp == 0 && $tipo != 2')], [C('setColor', object='o_nota', color='255;255;0')]),
    ev([X('$tipo == 2 && $esp != 2')], [C('setColor', object='o_nota', color='255;180;0')]),
    ev([X('$esp == 1 && $tipo == 0')], [C('setColor', object='o_nota', color='255;140;0')]),
    ev([X('$esp == 1 && $tipo == 1')], [C('setColor', object='o_nota', color='180;80;0')]),
    ev([X('$esp == 2')], [C('setColor', object='o_nota', color='100;100;100')]),
])
g += fn('golpeNotador', 'carril, yo, escala, color', [
    ev([C('objVar', object='o_notador', variable='carril', op='eq', value='{= $carril}')], [
        C('setObjVar', object='o_notador', variable='yo', value='{= $yo}'), C('setObjVar', object='o_notador', variable='tb', value='0'),
        C('setColor', object='o_notador', color='{= $color}')]),
    ev([X('$escala > 0')], [C('setScale', object='o_notador', x='$escala', y='$escala')], level=1),
])
ARRS = [('T', 't'), ('FIN', 'fin'), ('C', 'c'), ('ESP', 'esp'), ('G', 'g'), ('H', 'h'), ('W', 'w')]
EFS = [('ET', 't'), ('EC', 'c'), ('ED', 'dur'), ('EF', 'color')]
g += in_folder('Inicio y carga del nivel', [
    note('El nivel sale de su .json (archivo inicial o importado) o, al probarlo desde el editor, de los datos del editor.'),
    ev([C('atStart')], [
        SET('TM', -3), SET('TEspera', 3), SET('Pausa', 0), SET('EnDespausa', 0), SET('TDespausa', 0), SET('Saliendo', 0), SET('SalT', 0),
        SET('IN', 0), SET('IE', 0), SET('Combo', 0), SET('Pts', 0), SET('Puntuacion', 0), SET('SSPerdida', 0), SET('TotalNotas', 0),
        SET('Terminado', 0), SET('FaseFinal', 0), SET('TPost', 0), SET('TSalN', 0), SET('MusicaIniciada', 0),
        SET('ShakeT', 0), SET('FlashA', 0), SET('TargetRot', 0), SET('Resp', 0), SET('ModoEstela', 0), SET('ModoOleaje', 0), SET('ModoEnfoque', 0),
        SET('ZoomOut', 0), SET('BumpX', 0), SET('GrisPausa', 0), SET('TG', 0), SET('ComboScale', 1), SET('NotifOp', 0), SET('ultimoNum', -1),
        SET('tCambio', 0), SET('CamZoom', 1), SET('CamRot', 0), SET('EX', 0), SET('EZ', 0), SET('ER', 0), SET('uidSig', 1),
        SET('FadeA', 0), SET('FadeTo', 0), SET('FadeSpeed', 1), SET('Retraso', 0), SET('V', 400), SET('R0', 128), SET('G0', 128), SET('B0', 128),
        SET('Rebob', 0), SET('CrtT', 0), SET('CrtAmt', '$CrtNivel / 100'), SET('CrtExtra', 0), C('stopMusic'), C('cameraLimits', mode='none', x=0, y=0, w=VW, h=VH)] +
       [C('arrayFill', variable=v, size='4', value='0') for v in ('KD', 'HF', 'RH', 'TeclaT', 'LA', 'LR', 'LF', 'LE', 'Abs')] + [
        C('setText', object='o_notificacion', text='{= Fn.nombreTecla($Teclas[4])}{= %s}' % tr(' para pausar', ' to pause')),
        C('setText', object='o_puntuacion', text=''), C('setText', object='o_combo', text='')]),
    ev([X('$Seleccion >= 0 && $Seleccion < len($LvNombre)')], [SET('k', '$Seleccion'), SET('Pista', '"lv" + $k')], level=1),
    ev([X('$LvIni[$k] == 1')], [C('initialFileRead', name='{= $LvArchivo[$k]}', variable='txtJ')], level=2),
    ev([X('$LvIni[$k] == 0')], [C('fileRead', path='{= $LvArchivo[$k]}', variable='txtJ')], level=2),
    ev([], [C('jsonToVars', source='txtJ', prefix='Nv'), SET('txtJ', '""'), SET('V', '$Nv.velocidad > 0 ? $Nv.velocidad : 400'), SET('Retraso', '$Nv.retraso'),
            SET('R0', 'hex($Nv.fondo, 0)'), SET('G0', 'hex($Nv.fondo, 1)'), SET('B0', 'hex($Nv.fondo, 2)'), SET('NombreNivel', '"" + $Nv.nombre')] +
       [C('arrayCopy', **{'from': 'Nv.notas.' + k, 'to': a}) for a, k in ARRS] + [C('arrayCopy', **{'from': 'Nv.efectos.' + k, 'to': a}) for a, k in EFS] +
       [C('varsClear', prefix='Nv')], level=2),
    ev([X('$Seleccion < 0')], [SET('Pista', '"editor"'), SET('V', '$EdVel'), SET('Retraso', '$EdRet'), SET('NombreNivel', '$EdNombre'),
                               SET('R0', 'hex($EdFondo, 0)'), SET('G0', 'hex($EdFondo, 1)'), SET('B0', 'hex($EdFondo, 2)')] +
       [C('arrayCopy', **{'from': 'Ed' + {'T': 'T', 'FIN': 'Fin', 'C': 'C', 'ESP': 'Esp', 'G': 'G', 'H': 'H', 'W': 'W'}[a], 'to': a}) for a, _ in ARRS] +
       [C('arrayCopy', **{'from': {'ET': 'EdET', 'EC': 'EdEC', 'ED': 'EdED', 'EF': 'EdEF'}[a], 'to': a}) for a, _ in EFS], level=1),
    ev([], [C('setText', object='o_nnivel', text='{= $NombreNivel}'), C('arrayFill', variable='ESP', size='len($T)', value='0')], level=1),
    ev([], [], level=1, type='repeat', count='len($T)', var='i'),
    ev([], [SET('TotalNotas', '1 + ($FIN[$i] >= 0 ? 1 : 0)', 'add')], level=2),
    ev([], [], level=1, type='forEach', object='o_notador'),
    ev([], [C('setObjVar', object='o_notador', variable='bx', value='{= Notador.X}'), C('setObjVar', object='o_notador', variable='by', value='{= Notador.Y}'),
            C('create', object='o_tecla', x='Notador.X', y='Notador.Y'), C('setText', object='o_tecla', text='{= Fn.nombreTecla($Teclas[Notador.carril])}'),
            C('setObjVar', object='o_tecla', variable='carril', value='{= Notador.carril}'), C('setZ', object='o_tecla', value=11),
            C('setOpacity', object='o_tecla', value=0), C('setAngle', object='o_tecla', value=-360), C('setScale', object='o_tecla', x=1.3 * 1.5, y=1.3 * 1.5)], level=2),
    note('Tras rebobinar, el nivel empieza con un destello blanco.'),
    ev([C('atStart'), X('$FlashInicio == 1')], [SET('FlashInicio', 0), SET('FadeA', 1), SET('FadeTo', 0), SET('FadeSpeed', 1.6),
                                               C('setColor', object='o_fundido', color='255;255;255'), sfx('sine', 2400, 600, 0.4, 0.3)]),
])
JUEGO = '$Pausa == 0 && $Saliendo == 0'
g += in_folder('Teclado, pausa, salida y rebobinado', [
    note('KD[c] = la tecla del carril c esta pulsada (teclas de Ajustes).'),
] + [ev([C('keyDown', key=KEYS[c])], [SET('KD[%d]' % c, 1)]) for c in range(4)] + [ev([C('keyDown', True, key=KEYS[c])], [SET('KD[%d]' % c, 0)]) for c in range(4)] + [
    note('Pausa (tecla de Ajustes); en pausa vuelve a pulsarla para seguir tras 3 s. R rebobina y reinicia; Enter sale.'),
    ev([C('keyPressed', key='{= $Teclas[4]}'), X('$Rebob == 0 && $Terminado == 0')], []),
    ev([X('$Pausa == 1 && $EnDespausa == 0')], [SET('EnDespausa', 1), SET('TDespausa', 3)], level=1),
    ev([X('$Pausa == 0')], [SET('Pausa', 1)], level=1),
    ev([X('$EnDespausa == 1')], [SET('TDespausa', DT, 'sub')]),
    ev([X('$TDespausa <= 0')], [SET('EnDespausa', 0), SET('Pausa', 0)], level=1),
    ev([X('$Pausa == 1 && $EnDespausa == 0 && $Saliendo == 0 && $Rebob == 0')], []),
    ev([C('keyPressed', key='r')], [SET('Rebob', 1), SET('RbT', 0), C('stopMusic'), SET('MusicaIniciada', 0), C('removeObj', object='o_flotante'),
                                   C('setOpacity', object='o_combo', value=0), C('setOpacity', object='o_puntuacion', value=0)], level=1),
    ev([C('keyPressed', key='enter')], [SET('Saliendo', 1), SET('SalT', 0), C('stopMusic')], level=1),
    ev([X('$Saliendo == 1')], [SET('SalT', DT, 'add'),
                              C('setOpacity', object='o_notificacion', value='max(0, Notificacion.Opacidad - 1000 / 2.55 * TiempoDelta())'),
                              C('setOpacity', object='o_combo', value='max(0, Combo.Opacidad - 1000 / 2.55 * TiempoDelta())'),
                              C('setOpacity', object='o_puntuacion', value='max(0, Puntuacion.Opacidad - 1000 / 2.55 * TiempoDelta())')]),
    ev([], [C('setOpacity', object='o_nota', value='max(0, Nota.Opacidad - 1000 / 2.55 * TiempoDelta())')], level=1, type='forEach', object='o_nota'),
    ev([X('$SalT >= 2.5 && %s' % LIBRE)], [SET('IrA', '$VolverA')], level=1),
    note('Rebobinado: vuelve el CRT, aparece REBOBINANDO y el tiempo corre hacia atras (las notas suben); al llegar al principio, destello y otra vez.'),
    ev([X('$Rebob == 1')], [SET('RbT', DT, 'add'), SET('TM', 'max(-3, $TM - max(6, ($TM + 3) / 1.4) * %s)' % DT), SET('caida', CAIDA),
                           SET('CrtAmt', 'min(max(0.35, $CrtNivel / 100), $CrtAmt + 2.5 * %s)' % DT), C('removeObj', object='o_nota'),
                           C('setOpacity', object='o_rebtxt', value='floor($RbT * 3) % 2 == 0 ? 100 : 25'), C('setOpacity', object='o_rebicono', value=100),
                           C('setText', object='o_rebtxt', text='{= %s}' % tr('REBOBINANDO', 'REWINDING'))]),
    ev([C('every', seconds=0.09)], [sfx('square', '1800 + random() * 900', '2600 + random() * 900', 0.07, 0.08)], level=1),
    ev([], [], level=1, type='repeat', count='len($T)', var='i'),
    ev([X('$T[$i] >= $TM - 0.15 && $T[$i] <= $TM + $caida')], [C('callFunction', name='aparecer', args='$T[$i], $C[$i], 0, $ESP[$i], $G[$i], $H[$i], $W[$i]', result='')], level=2),
    ev([X('$TM <= -3')], [SET('FlashInicio', 1), C('restartScene')], level=1),
    ev([], [SET('dt', '$Pausa == 1 ? 0 : TiempoDelta()'), SET('enCascada', '$FaseFinal == 1 || $Saliendo == 1 ? 1 : 0')]),
    note('El CRT de la tele (intensidad de Ajustes) se desvanece poco a poco al empezar (curva suave de 2.5 s).'),
    ev([X('$Rebob == 0')], [SET('CrtT', DT, 'add'), SET('CrtAmt', '$CrtNivel / 100 * (1 - Fn.eioSine(min(1, $CrtT / 2.5)))')]),
    ev([], [C('shaderAmount', shader='sh_crt', value='max($CrtAmt, $CrtExtra)')]),
])
g += in_folder('Fundido de pantalla y aviso', [
    ev([X('abs($FadeTo - $FadeA) < 0.005')], [SET('FadeA', '$FadeTo')]),
    ev([X('abs($FadeTo - $FadeA) >= 0.005')], [SET('FadeA', 'clamp($FadeA + sign($FadeTo - $FadeA) * $FadeSpeed * TiempoDelta(), 0, 1)')]),
    ev([], [C('setOpacity', object='o_fundido', value='$FadeA * 100')]),
    ev([X('$Saliendo == 0')], []),
    ev([X('$FaseFinal == 0 && $TM >= -1.5')], [SET('NotifOp', 'min(255, $NotifOp + 100 * TiempoDelta())')], level=1),
    ev([X('$FaseFinal == 1')], [SET('NotifOp', 'max(0, $NotifOp - 200 * TiempoDelta())')], level=1),
    ev([X('$FaseFinal == 0 && $TM < -1.5')], [SET('NotifOp', 0)], level=1),
    ev([], [C('setOpacity', object='o_notificacion', value='$NotifOp / 2.55'), C('setOpacity', object='o_nnivel', value='$NotifOp / 2.55')], level=1),
])
g += in_folder('Musica', [
    ev([X('$Pausa == 1 && $MusicaIniciada == 1')], [C('stopMusic'), SET('MusicaIniciada', 0)]),
    ev([X('$Pausa == 0 && $MusicaIniciada == 0 && $TM >= $Retraso')], [SET('MusicaIniciada', 1), C('playMusic', track='{= $Pista}', loop='no')]),
    ev([X('$TM - $Retraso > 0')], [C('musicSeek', seconds='$TM - $Retraso')], level=1),
    ev([], [C('musicVolume', value='$Volumen_Global * $Volumen_Musica / 100')], level=1),
])
LBL = lambda acts, lvl: ev([C('objVar', object='o_tecla', variable='carril', op='eq', value='{= Notador.carril}')], acts, level=lvl)
g += in_folder('Notadores', [
    ev([], [], type='forEach', object='o_notador'),
    ev([], [C('addObjVar', object='o_notador', variable='ta', value='TiempoDelta()')], level=1),
    ev([X('Notador.gris == 1')], [C('addObjVar', object='o_notador', variable='gt', value='TiempoDelta()')], level=1),
    ev([X('Notador.gt < 0.3')], [
        C('addObjVar', object='o_notador', variable='yo', value='(500 + Notador.gt * 1000) * TiempoDelta()'),
        C('rotateBy', object='o_notador', value='(300 + Notador.gt * 600) * TiempoDelta()'),
        C('setObjVar', object='o_notador', variable='sh', value='{= (random() - 0.5) * max(0, 20 * (1 - Notador.gt / 0.3))}')], level=2),
    ev([X('Notador.gt >= 0.3')], [C('setObjVar', object='o_notador', variable='op', value='0')], level=2),
    ev([X('Notador.gris == 0 && (Notador.ta - Notador.ret >= 0.75 || Notador.ta > 2) && Notador.salto > 0')], [
        C('addObjVar', object='o_notador', variable='yo', value='-Notador.salto * TiempoDelta() * 200'),
        C('setObjVar', object='o_notador', variable='salto', value='{= max(0, Notador.salto - TiempoDelta() * 3)}'),
        C('rotateBy', object='o_notador', value='-720 * TiempoDelta()')], level=1),
    LBL([C('setAngle', object='o_tecla', value='Notador.Angulo * 0.65')], 2),
    ev([X('$enCascada == 0 && Notador.gris == 0 && Notador.ta >= Notador.ret')], [SET('prog', 'min(1, (Notador.ta - Notador.ret) / 0.75)')], level=1),
    ev([X('$prog < 1')], [
        SET('rp', '$prog < 0.82 ? Fn.eioCubic($prog / 0.82) : 1'), SET('over', '$prog >= 0.8 ? sin((($prog - 0.8) / 0.2) * PI) * 8 : 0'),
        C('setAngle', object='o_notador', value='-360 * (1 - $rp) + $over'),
        C('setObjVar', object='o_notador', variable='yo', value='{= 300 * (1 - Fn.backOut(min(1, $prog / 0.85)))}'),
        SET('sb', '$prog > 0.75 && $prog < 0.92 ? 1 - sin((($prog - 0.75) / 0.17) * PI) * 0.1 : 1.3 - 0.3 * Fn.eioSine($prog)'),
        C('setObjVar', object='o_notador', variable='sc', value='{= max(0.01, $sb)}'),
        C('setObjVar', object='o_notador', variable='op', value='{= floor(min(1, $prog / 0.3) * 255)}')], level=2),
    LBL([C('setAngle', object='o_tecla', value='Notador.Angulo * 0.65')], 3),
    ev([X('$prog >= 1 && Notador.salto <= 0')], [C('setObjVar', object='o_notador', variable='sc', value='1'), C('setObjVar', object='o_notador', variable='op', value='255')], level=2),
    ev([X('abs(Notador.Angulo) > 1')], [C('setAngle', object='o_notador', value='Notador.Angulo - Notador.Angulo * 10 * TiempoDelta()')], level=3),
    ev([X('abs(Notador.Angulo) <= 1')], [C('setAngle', object='o_notador', value=0)], level=3),
    LBL([C('setAngle', object='o_tecla', value='Notador.Angulo * 0.65')], 3),
    ev([X('Notador.yo != 0')], [C('addObjVar', object='o_notador', variable='yo', value='-Notador.yo * 8 * TiempoDelta()')], level=3),
    ev([X('abs(Notador.yo) < 0.5')], [C('setObjVar', object='o_notador', variable='yo', value='0')], level=4),
    ev([X('Notador.tb < 255')], [C('setObjVar', object='o_notador', variable='tb', value='{= min(255, Notador.tb + 300 * TiempoDelta())}'),
                                 C('setColor', object='o_notador', color='255;255;{= floor(Notador.tb / 16) * 16 + (Notador.tb >= 255 ? 15 : 0)}')], level=2),
    ev([X('$enCascada == 1 && Notador.gris == 0')], [C('addObjVar', object='o_notador', variable='tc', value='TiempoDelta()')], level=1),
    ev([X('Notador.tc > Notador.ret')], [SET('at', 'Notador.tc - Notador.ret')], level=2),
    ev([X('$at < 0.18')], [SET('jt', 'Fn.eioSine($at / 0.18)'), C('setObjVar', object='o_notador', variable='yo', value='{= -28 * sin($jt * PI)}'),
                           C('setAngle', object='o_notador', value='$jt * 30')], level=3),
    LBL([C('setAngle', object='o_tecla', value='$jt * 20')], 4),
    ev([X('$at >= 0.18')], [SET('ft', '$at - 0.18'), C('addObjVar', object='o_notador', variable='yo', value='$ft * $ft * 2200 * TiempoDelta()'),
                            C('setObjVar', object='o_notador', variable='op', value='{= max(0, Notador.op - 420 * TiempoDelta())}'),
                            C('rotateBy', object='o_notador', value='(200 + $ft * 800) * TiempoDelta()')], level=3),
    ev([], [C('setPos', object='o_notador', x='Notador.bx + Notador.sh', y='Notador.by + Notador.yo'),
            C('setScale', object='o_notador', x='Notador.sc', y='Notador.sc'), C('setOpacity', object='o_notador', value='Notador.op / 2.55'),
            C('setObjVar', object='o_notador', variable='sh', value='{= Notador.sh * 0.7}')], level=1),
    LBL([C('setPos', object='o_tecla', x='Notador.bx + Notador.sh', y='Notador.by + Notador.yo'),
         C('setScale', object='o_tecla', x='Notador.sc * 1.5', y='Notador.sc * 1.5'), C('setOpacity', object='o_tecla', value='Notador.op / 2.55')], 2),
])
g += in_folder('Fondo y camara', [
    ev([X('$Pausa == 0 && $Saliendo == 0 && $FlashA > 0')], [SET('FlashA', 'max(0, $FlashA - 800 * $dt)')]),
    ev([], [SET('fa', '$Pausa == 1 || $Saliendo == 1 ? 0 : $FlashA / 255'),
            C('setBgColor', color='{= floor($R0 + (255 - $R0) * $fa)};{= floor($G0 + (255 - $G0) * $fa)};{= floor($B0 + (255 - $B0) * $fa)}')]),
    ev([], [SET('cxx', VW / 2), SET('cyy', VH / 2), SET('zo', '$ZoomOut == 1 ? 0.85 : ($ZoomOut == 2 ? 0.7 : ($ZoomOut == 3 ? 0.625 : 1))'),
            SET('trot', '$TargetRot'), SET('tEX', 0), SET('tEZ', 0), SET('tER', 0)]),
    ev([X(NOT_PAUSA)], []),
    ev([X('$Resp > 0')], [SET('zo', 0.85, 'mul'), SET('cxx', 'sin($TM * 1.5) * ($Resp * 8)', 'add'), SET('cyy', 'cos($TM * 2) * ($Resp * 10)', 'add')], level=1),
    ev([X('$ShakeT > 0')], [SET('ShakeT', '$dt', 'sub'), SET('cxx', '(random() - 0.5) * 35', 'add'), SET('cyy', '(random() - 0.5) * 35', 'add')], level=1),
    ev([X('abs($BumpX) > 0.5')], [SET('cxx', '$BumpX', 'add'), SET('BumpX', '-$BumpX * 12 * $dt', 'add')], level=1),
    ev([X('abs($BumpX) <= 0.5')], [SET('BumpX', 0)], level=1),
    ev([X('$ModoOleaje == 1')], [SET('cxx', 'cos($TM * 2.5) * 10', 'add'), SET('cyy', 'sin($TM * 3) * 5', 'add'), SET('trot', 'sin($TM * 1.5) * 3', 'add')], level=1),
    ev([], [], level=1, type='repeat', count='4', var='c'),
    ev([X('$RH[$c] > 0')], [SET('RH[$c]', '$dt', 'sub')], level=2),
    ev([X('$ModoEnfoque == 1')], [SET('activos', '($RH[0] > 0 ? 1 : 0) + ($RH[1] > 0 ? 1 : 0) + ($RH[2] > 0 ? 1 : 0) + ($RH[3] > 0 ? 1 : 0)'), SET('mejorC', -1)], level=1),
    ev([X('$activos == 4')], [SET('tEZ', -0.25), SET('tER', '-$trot')], level=2),
    ev([X('$activos > 0 && $activos < 4')], [], level=2, type='repeat', count='4', var='c'),
    ev([X('$RH[$c] > 0 && ($mejorC < 0 || $TeclaT[$c] > $TeclaT[$mejorC])')], [SET('mejorC', '$c')], level=3),
    ev([X('$mejorC >= 0')], [SET('dX', '($mejorC == 0 ? 80 : ($mejorC == 1 ? 240 : ($mejorC == 2 ? 400 : 560))) - %d' % (VW / 2)),
                             SET('tEX', '$dX * 0.15'), SET('tEZ', 0.15), SET('tER', '$dX * 0.005')], level=2),
    ev([X('$Pausa == 1')], [SET('zo', 1), SET('trot', 0)]),
    ev([], [SET('EX', '($tEX - $EX) * 8 * TiempoDelta()', 'add'), SET('EZ', '($tEZ - $EZ) * 8 * TiempoDelta()', 'add'),
            SET('ER', '($tER - $ER) * 8 * TiempoDelta()', 'add'), SET('cxx', '$EX', 'add'), SET('zo', '$EZ', 'add'), SET('trot', '$ER', 'add'),
            SET('CamZoom', '($zo - $CamZoom) * ($Pausa == 1 ? 10 : 4) * TiempoDelta()', 'add'),
            SET('CamRot', '($trot - $CamRot) * ($Pausa == 1 ? 15 : 12) * TiempoDelta()', 'add'),
            C('cameraZoom', value='$CamZoom'), C('cameraSetPos', x='$cxx - %d / $CamZoom' % (VW / 2), y='$cyy - %d / $CamZoom' % (VH / 2)),
            C('cameraRotate', value='-$CamRot')]),
])
EFFECTS = [
    ('T', [SET('ShakeT', 0.3)]), ('Y', [SET('FlashA', 255)]), ('R', [SET('TargetRot', '$TargetRot == 0 ? 180 : 0')]),
    ('Z0', [SET('Resp', 0)]), ('Z1', [SET('Resp', '$Resp == 1 ? 0 : 1')]), ('Z2', [SET('Resp', '$Resp == 2 ? 0 : 2')]), ('Z3', [SET('Resp', '$Resp == 3 ? 0 : 3')]),
    ('ZB', [SET('CamZoom', 1.15)]), ('CR', [SET('BumpX', 20)]), ('CL', [SET('BumpX', -20)]),
    ('S', [SET('ModoEstela', '1 - $ModoEstela')]), ('W', [SET('ModoOleaje', '1 - $ModoOleaje')]), ('A', [SET('ModoEnfoque', '1 - $ModoEnfoque')]),
    ('O1', [SET('ZoomOut', '$ZoomOut == 1 ? 0 : 1')]), ('O2', [SET('ZoomOut', '$ZoomOut == 2 ? 0 : 2')]), ('O3', [SET('ZoomOut', '$ZoomOut == 3 ? 0 : 3')]),
    ('FI', [SET('FadeA', 1), SET('FadeTo', 0), SET('FadeSpeed', '$ED[$IE] > 0 ? 1 / $ED[$IE] : 1'), C('setColor', object='o_fundido', color='{= $EF[$IE] != "" ? $EF[$IE] : "#000000"}')]),
    ('FW', [SET('FadeA', 1), SET('FadeTo', 0), SET('FadeSpeed', '$ED[$IE] > 0 ? 1 / $ED[$IE] : 1'), C('setColor', object='o_fundido', color='{= $EF[$IE] != "" ? $EF[$IE] : "#ffffff"}')]),
    ('FO', [SET('FadeA', 0), SET('FadeTo', 1), SET('FadeSpeed', '$ED[$IE] > 0 ? 1 / $ED[$IE] : 1'), C('setColor', object='o_fundido', color='{= $EF[$IE] != "" ? $EF[$IE] : "#000000"}')]),
    ('SH_CRT', [SET('CrtExtra', '1 - $CrtExtra')]), ('SH_VIG', [C('screenShader', shader='sh_vineta', state='toggle')]),
    ('SH_RN', [C('screenShader', shader='sh_lluvia', state='toggle')]),
]
g += in_folder('Tiempo, notas y efectos del nivel', [
    ev([X(NOT_PAUSA)], []),
    ev([X('$TEspera > 0')], [SET('TEspera', DT, 'sub')], level=1),
    ev([], [SET('TM', '$dt', 'add'), SET('caida', CAIDA)], level=1),
    ev([X('$GrisPausa == 0')], [], level=1),
    ev([], [], level=2, type='while', whileConds=[X('$IN < len($T) && $TM >= $T[$IN] - $caida')]),
    ev([], [SET('c', '$C[$IN]'), C('callFunction', name='aparecer', args='$T[$IN], $c, 0, $ESP[$IN], $G[$IN], $H[$IN], $W[$IN]', result='')], level=3),
    ev([X('$FIN[$IN] >= 0')], [SET('LA[$c]', 1), SET('LR[$c]', 'ceil(($T[$IN] + 0.001) * 10) / 10'), SET('LF[$c]', '$FIN[$IN]'), SET('LE[$c]', '$ESP[$IN]')], level=3),
    ev([], [SET('IN', 1, 'add')], level=3),
    ev([], [], level=2, type='repeat', count='4', var='c'),
    ev([], [], level=3, type='while', whileConds=[X('$LA[$c] == 1 && $TM >= ($LR[$c] < $LF[$c] ? $LR[$c] : $LF[$c]) - $caida')]),
    ev([X('$LR[$c] < $LF[$c]')], [C('callFunction', name='aparecer', args='$LR[$c], $c, 1, $LE[$c], 2, 6, 5', result=''), SET('LR[$c]', 0.1, 'add')], level=4),
    ev([X('$LR[$c] >= $LF[$c]')], [C('callFunction', name='aparecer', args='$LF[$c], $c, 2, $LE[$c], 2, 6, 5', result=''), SET('LA[$c]', 0)], level=4),
    ev([], [], level=1, type='while', whileConds=[X('$IE < len($ET) && $TM >= $ET[$IE]')]),
] + [ev([C('cmpStr', a='{= $EC[$IE]}', op='eq', b=code)], acts, level=2) for code, acts in EFFECTS] + [
    ev([], [SET('IE', 1, 'add')], level=2),
    ev([X('$GrisPausa == 1')], [SET('TG', '$dt', 'sub')], level=1),
    ev([X('$TG <= 0')], [SET('GrisPausa', 0), SET('TG', 0)], level=2),
    ev([C('objVar', object='o_notador', variable='gris', op='eq', value='1')], [
        C('setObjVar', object='o_notador', variable='gris', value='0'), C('setObjVar', object='o_notador', variable='gt', value='0'),
        C('setObjVar', object='o_notador', variable='yo', value='300'), C('setObjVar', object='o_notador', variable='ta', value='{= Notador.ret}'),
        C('setObjVar', object='o_notador', variable='op', value='0'), C('setAngle', object='o_notador', value=0)], level=3),
])
g += in_folder('Particulas y textos flotantes', [
    ev([X('$Pausa == 0 && $Saliendo == 0')], [], type='forEach', object='o_estela'),
    ev([], [C('addObjVar', object='o_estela', variable='vy', value='1500 * $dt'),
            C('setPos', object='o_estela', x='Estela.X + Estela.vx * $dt', y='Estela.Y + Estela.vy * $dt'),
            C('rotateBy', object='o_estela', value='Estela.vr * $dt'), C('addObjVar', object='o_estela', variable='op', value='-600 * $dt'),
            C('setOpacity', object='o_estela', value='Estela.op / 2.55')], level=1),
    ev([X('Estela.op <= 0')], [C('removeObj', object='o_estela')], level=1),
    ev([], [], type='forEach', object='o_flotante'),
    ev([], [C('addObjVar', object='o_flotante', variable='vida', value='-400 * TiempoDelta()'),
            C('setY', object='o_flotante', value='Flotante.Y - 40 * TiempoDelta()'), C('setOpacity', object='o_flotante', value='Flotante.vida / 2.55')], level=1),
    ev([X('Flotante.vida <= 0')], [C('removeObj', object='o_flotante')], level=1),
])
g += in_folder('Notas en pantalla', [
    ev([X('$Rebob == 0')], [], type='forEach', object='o_nota'),
    ev([X(JUEGO)], [C('setObjVar', object='o_nota', variable='ty', value='{= %s}' % TY), C('setY', object='o_nota', value='Nota.ty + 16 * Nota.sc')], level=1),
    ev([X('Nota.tipo == 0 && Nota.ty < %d' % (YS + 50))], [SET('HF[Nota.carril]', 0)], level=2),
    ev([X('Nota.tipo != 0 && Nota.ty >= %d && Nota.ty <= %d && $KD[Nota.carril] == 0 && Nota.esp == 0' % (YN - 15, YN + 15))], [SET('HF[Nota.carril]', 1)], level=2),
    ev([X('Nota.tipo != 0 && $HF[Nota.carril] == 1')], [C('setColor', object='o_nota', color='0;0;0'), C('setOpacity', object='o_nota', value='100 / 2.55'),
                                                       C('setObjVar', object='o_nota', variable='fall', value='1')], level=2),
    ev([X('Nota.ty > %d' % LIM)], [], level=1),
    ev([X('%s && Nota.tipo != 1 && Nota.esp == 2' % JUEGO)], [
        SET('GrisPausa', 1), SET('TG', 'Nota.w'), SET('c', 'Nota.carril'), sfx('sawtooth', 80, 30, 0.35, 0.7),
        C('callFunction', name='flotante', args='Nota.X - 48, %d, "PESADA!"' % (YN + 40), result='')], level=2),
    ev([C('objVar', object='o_notador', variable='carril', op='eq', value='{= $c}')], [
        C('setObjVar', object='o_notador', variable='gris', value='1'), C('setObjVar', object='o_notador', variable='gt', value='0'),
        C('setObjVar', object='o_notador', variable='yo', value='0')], level=3),
    ev([X('%s && Nota.tipo != 1 && Nota.esp != 2' % JUEGO)], [
        SET('ComboAntes', '$Combo'), SET('Combo', 0), SET('SSPerdida', 1),
        C('callFunction', name='flotante', args='Nota.X - 48, %d, %s' % (YN + 40, tr('Fallo', 'Miss')), result='')], level=2),
    ev([X('$ComboAntes > 5')], [sfx('sawtooth', 250, 30, 0.25, 0.5)], level=3),
    ev([], [C('removeObj', object='o_nota')], level=2),
])


def lane_block(c):
    k = KEYS[c]
    lx = LANE_X[c]
    notador = lambda yo, esc, col: C('callFunction', name='golpeNotador', args='%d, %s, %s, "%s"' % (c, yo, esc, col), result='')
    return [
        ev([C('keyDown', key=k), X('%s && $GrisPausa == 0' % JUEGO)], [SET('Abs[%d]' % c, 0)]),
        ev([C('objVar', object='o_nota', variable='carril', op='eq', value=str(c))], [], level=1, type='forEach', object='o_nota'),
        ev([X('$Abs[%d] == 0 && Nota.fall != 1 && Nota.tipo == 1 && %s <= 50' % (c, DIST))], [
            SET('Abs[%d]' % c, 1), SET('RH[%d]' % c, 0.15), C('callFunction', name='explotar', args='Nota.X, Nota.Y', result='')], level=2),
        ev([X('Nota.esp == 1')], [sfx('square', 600, 900, 0.08, 0.5), notador(15, 0.85, '255;140;0')], level=3),
        ev([X('Nota.esp != 1')], [sfx('triangle', 1200, 300, 0.06, 0.4), notador(15, 0.85, '255;255;0')], level=3),
        ev([], [C('removeObj', object='o_nota')], level=3),
        ev([X('$Abs[%d] == 0 && Nota.fall != 1 && Nota.tipo == 2 && %s <= 50 && (Nota.esp != 1 || Nota.gA >= Nota.gR)' % (c, DIST))], [
            SET('Abs[%d]' % c, 1), SET('HF[%d]' % c, 0), SET('RH[%d]' % c, 0.15), SET('Combo', 1, 'add'), SET('Pts', 1, 'add'), SET('ComboScale', 1.2),
            C('callFunction', name='explotar', args='Nota.X, Nota.Y', result='')], level=2),
        ev([X('Nota.esp == 1')], [sfx('square', 600, 900, 0.08, 0.5), notador(25, 0.85, '255;140;0'),
                                  C('callFunction', name='flotante', args='%d, %d, "PARRY!"' % (lx, YN - 40), result='')], level=3),
        ev([X('Nota.esp != 1')], [sfx('triangle', 1200, 300, 0.06, 0.4), notador(25, 0.85, '255;255;0')], level=3),
        ev([], [C('removeObj', object='o_nota')], level=3),
        ev([C('keyPressed', key=k), X('$Rebob == 0')], [SET('TeclaT[%d]' % c, '$TM')]),
        ev([X(JUEGO), SC_LANE(c)], [C('setScale', object='o_notador', x=0.8, y=0.8)], level=1),
        ev([X('%s && $GrisPausa == 0' % JUEGO)], [SET('mejor', 9999), SET('mejorU', -1), SET('tocado', 0), SET('rango', '60 * $V / 400')], level=1),
        ev([C('objVar', object='o_nota', variable='carril', op='eq', value=str(c))], [], level=2, type='forEach', object='o_nota'),
        ev([X('Nota.tipo != 2 && Nota.esp == 1 && %s <= $rango' % DIST)], [
            C('addObjVar', object='o_nota', variable='gA', value=1), SET('tocado', 1), sfx('square', 600, 900, 0.08, 0.5),
            C('callFunction', name='explotar', args='Nota.X, Nota.Y', result=''),
            C('callFunction', name='flotante', args='%d, %d, "HIT! (" + Nota.gA + "/" + Nota.gR + ")"' % (lx, YN - 40), result='')], level=3),
        ev([SC_LANE(c)], [C('addObjVar', object='o_notador', variable='salto', value='Nota.alt'), C('setObjVar', object='o_notador', variable='tb', value='0'),
                          C('setColor', object='o_notador', color='255;140;0')], level=4),
        ev([X('Nota.gA >= Nota.gR')], [C('setObjVar', object='o_nota', variable='tipo', value='2')], level=4),
        ev([X('Nota.tipo == 0 && Nota.esp == 2 && %s <= $rango' % DIST)], [
            SET('tocado', 1), sfx('sawtooth', 80, 30, 0.35, 0.7), C('callFunction', name='explotar', args='Nota.X, Nota.Y', result=''),
            notador(50, 0.7, '180;180;180'), SET('Combo', 1, 'add'), SET('Pts', 1, 'add'), SET('ComboScale', 1.2),
            C('callFunction', name='flotante', args='%d, %d, "PESADA"' % (lx, YN - 40), result=''), C('removeObj', object='o_nota')], level=3),
        ev([SC_LANE(c)], [C('setAngle', object='o_notador', value=12)], level=4),
        ev([X('Nota.tipo == 0 && Nota.esp == 0 && %s < $rango && %s < $mejor' % (DIST, DIST))], [SET('mejor', DIST), SET('mejorU', 'Nota.uid')], level=3),
        ev([X('$mejorU >= 0')], [SET('f', '$V / 400')], level=2),
        ev([X('$mejor <= 20 * $f')], [SET('Combo', 1, 'add'), SET('Pts', 1, 'add'), SET('ComboScale', 1.2), sfx('triangle', 1200, 300, 0.06, 0.4),
                                      notador(25, 0, '255;255;0'), SET('juicio', tr('Increible', 'Awesome'))], level=3),
        ev([X('$mejor > 20 * $f && $mejor <= 40 * $f')], [SET('Combo', 1, 'add'), SET('Pts', 0.6, 'add'), SET('ComboScale', 1.2), SET('SSPerdida', 1),
                                                          sfx('triangle', 1200, 300, 0.06, 0.4), SET('juicio', tr('Bueno', 'Good'))], level=3),
        ev([SC_LANE(c)], [C('setObjVar', object='o_notador', variable='yo', value='15')], level=4),
        ev([X('$mejor > 40 * $f')], [SET('Pts', 0.2, 'add'), SET('SSPerdida', 1), SET('juicio', tr('Malo', 'Bad'))], level=3),
        ev([X('$Combo > 5')], [sfx('sawtooth', 250, 30, 0.25, 0.5)], level=4),
        ev([X('$Combo <= 5')], [sfx('triangle', 1200, 300, 0.06, 0.4)], level=4),
        ev([], [SET('Combo', 0)], level=4),
        ev([C('objVar', object='o_nota', variable='uid', op='eq', value='{= $mejorU}')], [
            C('callFunction', name='explotar', args='Nota.X, Nota.Y', result=''), C('removeObj', object='o_nota'),
            C('callFunction', name='flotante', args='%d, %d, $juicio' % (lx, YN - 40), result='')], level=3),
        ev([X('$mejorU < 0 && $tocado == 0')], [SET('SSPerdida', 1), SET('ComboAntes', '$Combo'), SET('Combo', 0)], level=2),
        ev([X('$ComboAntes > 5')], [sfx('sawtooth', 250, 30, 0.25, 0.5)], level=3),
        ev([SC_LANE(c)], [C('setObjVar', object='o_notador', variable='yo', value='10')], level=3),
    ]


lanes = []
for c in range(4):
    lanes += lane_block(c)
g += in_folder('Golpes', lanes)
g += in_folder('Puntuacion, cuenta atras y final', [
    ev([], [SET('Mult', '$Combo >= 30 ? 4 : ($Combo >= 20 ? 3 : ($Combo >= 10 ? 2 : 1))'), SET('Puntuacion', 'floor($Pts / max(1, $TotalNotas) * 1000000)')]),
    ev([X('$ComboScale > 1')], [SET('ComboScale', 'max(1, $ComboScale - 1.5 * TiempoDelta())')]),
    ev([X('$Rebob == 1')], [C('setOpacity', object='o_combo', value=0), C('setOpacity', object='o_puntuacion', value=0)]),
    ev([X('$Saliendo == 0 && $Rebob == 0')], []),
    ev([X('$Pausa == 1 && $EnDespausa == 0')], [
        C('setText', object='o_combo', text='{= %s}' % tr('Enter: Salir | P: Reanudar | R: Rebobinar', 'Enter: Exit | P: Resume | R: Rewind')),
        C('setScale', object='o_combo', x=1.3, y=1.3), C('setOpacity', object='o_combo', value=100),
        C('setText', object='o_puntuacion', text='{= %s}' % tr('PAUSA', 'PAUSE')), C('setScale', object='o_puntuacion', x=1.2, y=1.2),
        C('setY', object='o_puntuacion', value=tcy(PUNT_Y, 1.2)), C('setOpacity', object='o_puntuacion', value=100), SET('ultimoNum', -1)], level=1),
    ev([X('!($Pausa == 1 && $EnDespausa == 0) && ($TEspera > 0 || $EnDespausa == 1)')], [
        SET('trest', '$EnDespausa == 1 ? $TDespausa : $TEspera'), SET('num', '$trest > 0 ? ceil($trest) : 0')], level=1),
    ev([X('$num != $ultimoNum')], [SET('ultimoNum', '$num'), SET('tCambio', 0)], level=2),
    ev([], [SET('tCambio', 'TiempoDelta()', 'add'), SET('pE', 'min(1, $tCambio / 0.28)'),
            SET('pS', 'max(0, ((1 - ($trest - floor($trest))) - (1 - 0.22)) / 0.22)'),
            SET('eE', 'Fn.elasticOut($pE)'), SET('eS', '$pS * $pS'),
            SET('scN', 'min(1.2 * (1.8 - $eE * 0.8), 1.2 * (1 - $eS * 0.5))'),
            SET('opN', 'min(min(1, $pE / 0.3), 1 - $eS)'), SET('yoN', '(1 - $eE) * -60 + $eS * -40'),
            C('setOpacity', object='o_combo', value=0)], level=2),
    ev([X('$num > 0')], [C('setText', object='o_puntuacion', text='{= $num}'), C('setOpacity', object='o_puntuacion', value='round($opN * 255) / 2.55'),
                         C('setScale', object='o_puntuacion', x='max(0.01, $scN)', y='max(0.01, $scN)'),
                         C('setY', object='o_puntuacion', value='%d + %g * $scN + $yoN' % (PUNT_Y, CELL_H / 2))], level=2),
    ev([X('$num <= 0')], [C('setText', object='o_puntuacion', text=''), C('setOpacity', object='o_puntuacion', value=0)], level=2),
    ev([X('!($Pausa == 1 && $EnDespausa == 0) && !($TEspera > 0 || $EnDespausa == 1)')], [
        SET('ultimoNum', -1), C('setText', object='o_combo', text='{= $Combo} Combo!'),
        C('setScale', object='o_combo', x='1.3 * $ComboScale', y='1.3 * $ComboScale'), C('setOpacity', object='o_combo', value='$Combo >= 5 ? 100 : 0'),
        C('setScale', object='o_puntuacion', x=1.2, y=1.2), C('setY', object='o_puntuacion', value=tcy(PUNT_Y, 1.2)), C('setOpacity', object='o_puntuacion', value=100),
        C('setText', object='o_puntuacion', text='{= padStart($Puntuacion, 7, "0")} [{= Fn.grado($Pts / max(1, $TotalNotas), $SSPerdida)}]')], level=1),
    note('Fin: se guarda el record (puntuacion y precision mejores) y una partida mas; los notadores caen y se vuelve al menu.'),
    ev([X('$Pausa == 0 && $Saliendo == 0 && $Rebob == 0 && len($T) > 0 && $IN >= len($T) && $LA[0] + $LA[1] + $LA[2] + $LA[3] == 0'),
        C('objCount', object='o_nota', op='eq', value='0')], [SET('TPost', 'TiempoDelta()', 'add')]),
    ev([X('$TPost >= 1 && $Terminado == 0')], [SET('Terminado', 1), SET('FaseFinal', 1), SET('Prec', '$Pts / max(1, $TotalNotas) * 100')], level=1),
    ev([X('$Seleccion >= 0')], [SET('ri', 'Fn.recIdx($NombreNivel)')], level=2),
    ev([X('$ri < 0')], [C('arrayPush', variable='RecNombre', value='{= $NombreNivel}'), C('arrayPushNum', variable='RecPuntos', value='0'),
                        C('arrayPushNum', variable='RecPrec', value='0'), C('arrayPushNum', variable='RecJugadas', value='0'), SET('ri', 'len($RecNombre) - 1')], level=3),
    ev([], [SET('RecPuntos[$ri]', 'max($RecPuntos[$ri], $Puntuacion)'), SET('RecPrec[$ri]', 'max($RecPrec[$ri], round($Prec * 10) / 10)'),
            SET('RecJugadas[$ri]', 1, 'add'), GUARDAR()], level=3),
    ev([X('$Pausa == 0 && $Saliendo == 0 && $Terminado == 1')], [
        SET('TSalN', 'TiempoDelta()', 'add'),
        C('setOpacity', object='o_notificacion', value='max(0, Notificacion.Opacidad - 800 / 2.55 * TiempoDelta())'),
        C('setOpacity', object='o_combo', value='max(0, Combo.Opacidad - 800 / 2.55 * TiempoDelta())'),
        C('setOpacity', object='o_puntuacion', value='max(0, Puntuacion.Opacidad - 800 / 2.55 * TiempoDelta())')]),
    ev([X('$TSalN >= 2.5'), C('triggerOnce')], [C('stopMusic'), SET('FadeTo', 1), SET('FadeSpeed', 0.5), C('setColor', object='o_fundido', color='0;0;0')], level=1),
    ev([X('$TSalN >= 3 && %s' % LIBRE)], [SET('IrA', '$VolverA')], level=1),
])

# ---------------- shaders de pantalla ----------------
GLOW = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_res;
uniform float u_amount;
// Brillo (glow) del original: la imagen desenfocada, mas saturada y brillante, se suma en modo pantalla.
void main() {
  vec4 base = texture2D(u_tex, v_uv);
  vec2 px = 1.0 / u_res;
  vec3 a = vec3(0.0), b = vec3(0.0);
  for (int i = 0; i < 12; i++) {
    float an = float(i) * 0.5235988;
    vec2 d = vec2(cos(an), sin(an));
    a += texture2D(u_tex, v_uv + d * px * 4.0).rgb;
    b += texture2D(u_tex, v_uv + d * px * 10.0).rgb;
  }
  a /= 12.0; b /= 12.0;
  float la = dot(a, vec3(0.299, 0.587, 0.114)), lb = dot(b, vec3(0.299, 0.587, 0.114));
  vec3 g1 = clamp(mix(vec3(la), a, 3.0) * 1.5, 0.0, 1.0) * 0.55;
  vec3 g2 = clamp(mix(vec3(lb), b, 2.0) * 1.2, 0.0, 1.0) * 0.25;
  vec3 g = (g1 + g2) * u_amount * 0.6;
  gl_FragColor = vec4(1.0 - (1.0 - base.rgb) * (1.0 - g), base.a);
}
"""
CRT = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_res;
uniform float u_amount;
// Tele vieja: curvatura, lineas de barrido, separacion de color y bordes oscuros. u_amount = intensidad (0 = nada).
void main() {
  vec2 cc = v_uv - 0.5;
  cc *= 1.0 + dot(cc, cc) * 0.3 * u_amount;
  cc += 0.5;
  if (cc.x < 0.0 || cc.x > 1.0 || cc.y < 0.0 || cc.y > 1.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
  vec4 col = texture2D(u_tex, cc);
  col.r = texture2D(u_tex, cc + vec2(0.0015 * u_amount, 0.0)).r;
  col.b = texture2D(u_tex, cc - vec2(0.0015 * u_amount, 0.0)).b;
  float line = sin(cc.y * u_res.y * 3.14159) * 0.5 + 0.5;
  col.rgb *= mix(1.0, mix(0.65, 1.0, line), u_amount);
  if (mod(gl_FragCoord.y, 3.0) < 1.0) col.rgb *= 1.0 - 0.18 * u_amount;
  vec2 v = cc - 0.5;
  col.rgb *= mix(1.0, clamp(1.0 - dot(v, v) * 1.6, 0.0, 1.0), u_amount);
  col.rgb *= 1.0 + 0.06 * u_amount;
  gl_FragColor = col;
}
"""
VIG = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
// Vineta: oscurece los bordes.
void main() {
  vec4 col = texture2D(u_tex, v_uv);
  vec2 uv = v_uv - 0.5;
  col.rgb *= clamp(1.0 - dot(uv, uv) * 3.5, 0.0, 1.0);
  gl_FragColor = col;
}
"""
RUIDO = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_res;
uniform float u_amount;
// Ruido de tele (cambio de canal), de un pixel: grano que cambia 30 veces por segundo, bandas que bajan y lineas que tiemblan.
float h(vec2 p) { p = fract(p * vec2(443.897, 441.423)); p += dot(p, p.yx + 19.19); return fract((p.x + p.y) * p.x); }
void main() {
  if (u_amount <= 0.001) { gl_FragColor = texture2D(u_tex, v_uv); return; }
  float t = floor(u_time * 30.0);
  vec2 px = floor(v_uv * u_res);
  float j = (h(vec2(px.y, t)) - 0.5) * 0.03 * u_amount;
  vec4 c = texture2D(u_tex, v_uv + vec2(j, 0.0));
  float n = h(px + vec2(t * 13.0, t * 7.0));
  float banda = 1.0 - 0.35 * smoothstep(0.8, 1.0, sin(v_uv.y * 4.0 - u_time * 5.0) * 0.5 + 0.5);
  float raya = step(0.985, h(vec2(floor(v_uv.y * u_res.y / 2.0), t))) * 0.4;
  vec3 ruido = vec3(n * banda + raya);
  gl_FragColor = vec4(mix(c.rgb, ruido, u_amount), 1.0);
}
"""
FONDO = """precision mediump float;
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;
uniform vec2 u_res;
// Fondo de los menus: el color de la capa (FondoRGB) se mueve como plasma, con bandas de luz que cruzan,
// una rejilla en perspectiva que avanza por abajo, el resplandor del horizonte y estrellas que parpadean.
float h(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main() {
  vec4 t = texture2D(u_tex, v_uv);
  if (t.a < 0.01) { gl_FragColor = t; return; }
  vec3 base = t.rgb;
  vec2 uv = v_uv;
  float tm = u_time;
  float p = sin(uv.x * 6.0 + tm * 0.7) + sin(uv.y * 5.0 - tm * 0.5) + sin((uv.x + uv.y) * 4.0 + tm * 0.9) + sin(length(uv - 0.5) * 10.0 - tm * 1.3);
  vec3 col = base * (0.55 + 0.12 * p) * (1.15 - 0.5 * uv.y);
  float banda = smoothstep(0.93, 1.0, sin((uv.x * 1.6 + uv.y) * 10.0 - tm * 1.4) * 0.5 + 0.5);
  col += (base + 0.15) * banda * 0.22;
  float hz = 0.66;
  col += (base * 0.6 + vec3(0.35, 0.1, 0.3)) * exp(-abs(uv.y - hz) * 30.0) * 0.55;
  if (uv.y > hz) {
    float d = (uv.y - hz) / (1.0 - hz);
    float z = 1.0 / max(d, 0.03);
    float fx = fract((uv.x - 0.5) * z * 1.8);
    float fy = fract(z * 0.5 - tm * 0.7);
    float lx = 1.0 - smoothstep(0.0, 0.05 * z * 0.25 + 0.02, min(fx, 1.0 - fx));
    float ly = 1.0 - smoothstep(0.0, 0.08, min(fy, 1.0 - fy));
    col += mix(base, vec3(1.0, 0.45, 0.95), 0.55) * max(lx, ly) * d * 0.8;
  } else {
    vec2 c = floor(uv * u_res / 3.0);
    float e = h(c);
    if (e > 0.992) col += vec3(0.7) * (0.5 + 0.5 * sin(tm * 3.0 + e * 60.0)) * (1.0 - uv.y / hz);
  }
  gl_FragColor = vec4(col, 1.0);
}
"""
RAIN = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'editor', 'presets', 'lluvia.glsl')).read()
shaders = [dict(id='sh_glow', name='Brillo', code=GLOW), dict(id='sh_crt', name='CRT', code=CRT), dict(id='sh_ruido', name='Ruido de tele', code=RUIDO),
           dict(id='sh_fondo', name='Fondo animado', code=FONDO),
           dict(id='sh_vineta', name='Viñeta', code=VIG), dict(id='sh_lluvia', name='Lluvia', code=RAIN)]

scenes = [
    dict(id=SC['inicio'], name='Inicio', width=VW, height=VH, background='#22103a', instances=ini_insts, tilemaps=[], effects=[], events=ini),
    dict(id=SC['niveles'], name='Niveles', width=VW, height=VH, background='#1e1e1e', instances=niv_insts, tilemaps=[], effects=[], events=niv),
    dict(id=SC['ajustes'], name='Ajustes', width=VW, height=VH, background='#181428', instances=aj_insts, tilemaps=[], effects=[], events=aj),
    dict(id=SC['perfil'], name='Perfil', width=VW, height=VH, background='#141828', instances=pf_insts, tilemaps=[], effects=[], events=pf),
    dict(id=SC['archivos'], name='Archivos', width=VW, height=VH, background='#10121c', instances=fa_insts, tilemaps=[], effects=[], events=fa),
    dict(id=SC['editor'], name='Editor', width=VW, height=VH, background='#141428', instances=ed_insts, tilemaps=[], effects=[], events=ed),
    dict(id=SC['juego'], name='Juego', width=VW, height=VH, background='#808080', instances=game_insts, tilemaps=[], effects=[], events=g),
]
doc = dict(id='p_rymik', name='Project Ry-mik', version=3,
           settings=dict(width=VW, height=VH, fullscreen=False, autofullscreen=False, pixelArt=True),
           globals=globals_, sprites=sprites, tilesets=[], fonts=[FONT], music=[], files=files,
           objects=objects, scenes=scenes, events=com,
           layers=[dict(id='fondo', name='Fondo', z=-10, parallax=0, solid=False, shaderId='sh_fondo'),
                   dict(id='base', name='Principal', z=0, parallax=1, solid=False)], shaders=shaders)
strip_comments(doc)
json.dump(doc, open(OUT, 'w'), ensure_ascii=False, separators=(',', ':'))
print(OUT, '%d niveles en archivos iniciales (%.1f MB), %d escenas, %d eventos' % (
    len(files), sum(len(f['text']) for f in files) / 1e6, len(scenes), len(com) + sum(len(s['events']) for s in scenes)))
