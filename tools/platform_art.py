#!/usr/bin/env python3
"""Arte de Platform Test dibujado por codigo (pixel art en listas de colores '#rrggbb' o None = transparente).
Ambiente oscuro y frio, como una cueva: roca azul pizarra con musgo, espinas negras con puntas palidas,
siluetas de fondo en azules cada vez mas claros con la distancia, y un viajero con capucha y bufanda cian."""
import math, random

# ---------------- paleta ----------------
SKY = ['#070b16', '#0a1020', '#0e152a', '#121c35', '#17233f', '#1c2a49', '#223253', '#283a5c']
FAR, FAR2 = '#1a2642', '#1f2d4d'
MID, MID2, MID3 = '#141d33', '#18233d', '#1f2c4a'
WALL, WALL2, WALL3 = '#101727', '#131b2e', '#172036'
ROCK = ['#1b2234', '#232c41', '#2c374f', '#36435d', '#46546f', '#5d6e8b']
MOSS = ['#1f5a4c', '#2d7a63', '#3f9a7a', '#62c29a', '#9be3bf']
THORN = ['#07050b', '#120d1a', '#22182f', '#3a2a4d']
TIP = ['#cfc6dd', '#f1ecf8']
FG = ['#04060b', '#070a12']


class Img:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.p = [None] * (w * h)

    def put(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y * self.w + x] = c

    def get(self, x, y):
        return self.p[y * self.w + x] if 0 <= x < self.w and 0 <= y < self.h else None

    def rect(self, x0, y0, x1, y1, c):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                self.put(x, y, c)

    def disc(self, cx, cy, r, c):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    self.put(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(1, n)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def flipx(self):
        o = Img(self.w, self.h)
        for y in range(self.h):
            for x in range(self.w):
                o.p[y * self.w + x] = self.p[y * self.w + self.w - 1 - x]
        return o

    def rot90(self, k=1):
        o = self
        for _ in range(k % 4):
            n = Img(o.h, o.w)
            for y in range(o.h):
                for x in range(o.w):
                    n.p[x * n.w + (o.h - 1 - y)] = o.p[y * o.w + x]
            o = n
        return o


def noise2(x, y, seed=0):
    h = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xffffffff
    h = (h ^ (h >> 13)) * 1274126177 & 0xffffffff
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0


def vnoise(x, seed=0):
    """ruido suave 1D en [0,1]"""
    i, f = int(math.floor(x)), x - math.floor(x)
    a, b = noise2(i, 0, seed), noise2(i + 1, 0, seed)
    f = f * f * (3 - 2 * f)
    return a + (b - a) * f


# ---------------- roca con autotile ----------------
# mascara: 1 = arriba libre, 2 = derecha libre, 4 = abajo libre, 8 = izquierda libre
def rock_tile(mask, variant=0, seed=1):
    T = 32
    im = Img(T, T)
    rnd = random.Random(seed * 97 + mask * 13 + variant * 7)
    for y in range(T):
        for x in range(T):
            n = noise2(x // 2, y // 2, seed + variant) * 0.6 + noise2(x // 6, y // 6, seed + 3) * 0.4
            im.put(x, y, ROCK[1 + int(n * 2.6)])
    # grietas y piedras sueltas
    for _ in range(2 + variant):
        x, y = rnd.randrange(4, 28), rnd.randrange(6, 28)
        for k in range(rnd.randrange(3, 8)):
            im.put(x, y, ROCK[0])
            x += rnd.choice((-1, 0, 1)); y += rnd.choice((0, 1))
    for _ in range(3):
        x, y = rnd.randrange(3, 29), rnd.randrange(5, 29)
        im.put(x, y, ROCK[3]); im.put(x + 1, y, ROCK[3]); im.put(x, y + 1, ROCK[2])
    top, right, bottom, left = mask & 1, mask & 2, mask & 4, mask & 8
    if left:
        for y in range(T): im.put(0, y, ROCK[3]); im.put(1, y, ROCK[2])
    if right:
        for y in range(T): im.put(T - 1, y, ROCK[0]); im.put(T - 2, y, ROCK[1])
    if bottom:
        for x in range(T):
            im.put(x, T - 1, ROCK[0]); im.put(x, T - 2, ROCK[0])
        for x in range(2, T - 2, 5):   # raices colgando
            if noise2(x, mask, seed) > 0.45:
                L = 2 + int(noise2(x, 3, seed) * 4)
                for k in range(L): im.put(x, T - 2 - k, ROCK[0])
    if top:
        # musgo con borde irregular y brillo
        for x in range(T):
            d = 3 + int(noise2(x // 2, 7, seed + variant) * 3)
            for y in range(d):
                im.put(x, y, MOSS[1] if y < d - 1 else MOSS[0])
            im.put(x, 0, MOSS[3] if noise2(x, 1, seed) > 0.35 else MOSS[2])
            if noise2(x, 9, seed) > 0.8: im.put(x, d, MOSS[0])
    # esquinas redondeadas
    def cut(xs, ys):
        for (x, y) in zip(xs, ys): im.put(x, y, None)
    if top and left: cut([0, 1, 0], [0, 0, 1])
    if top and right: cut([T - 1, T - 2, T - 1], [0, 0, 1])
    if bottom and left: cut([0, 1, 0], [T - 1, T - 1, T - 2])
    if bottom and right: cut([T - 1, T - 2, T - 1], [T - 1, T - 1, T - 2])
    return im


# ---------------- espinas (peligro) ----------------
def thorn_tile(seed=3):
    """espinas apuntando hacia arriba, sobre una rama negra en la base"""
    T = 32
    im = Img(T, T)
    rnd = random.Random(seed)
    # rama retorcida abajo
    for x in range(T):
        yb = 27 + int(2 * math.sin(x * 0.45 + seed))
        for y in range(yb, T): im.put(x, y, THORN[1] if y > yb else THORN[2])
    # espinas: triangulos negros con punta palida
    xs = [2, 7, 12, 17, 22, 27]
    for i, bx in enumerate(xs):
        hgt = 14 + rnd.randrange(0, 10)
        base = 28
        lean = rnd.choice((-2, -1, 0, 1, 2))
        for k in range(hgt):
            y = base - k
            half = max(0, int((hgt - k) * 3 / hgt + 0.3))
            cx = bx + 2 + lean * k / hgt
            for dx in range(-half, half + 1):
                im.put(cx + dx, y, THORN[0] if abs(dx) < half else THORN[2])
        tx, ty = bx + 2 + lean, base - hgt
        im.put(tx, ty, TIP[1]); im.put(tx, ty + 1, TIP[0]); im.put(tx, ty + 2, THORN[3])
    # espinas pequenas de lado
    for _ in range(5):
        x, y = rnd.randrange(2, 30), rnd.randrange(16, 26)
        im.put(x, y, THORN[2]); im.put(x + 1, y - 1, TIP[0])
    return im


# ---------------- vegetacion (capa con viento) ----------------
def grass_tile(kind, seed):
    T = 32
    im = Img(T, T)
    rnd = random.Random(seed)
    if kind == 'grass':
        for _ in range(9):
            x = rnd.randrange(2, 30); h = rnd.randrange(6, 15); lean = rnd.uniform(-0.25, 0.25)
            col = rnd.choice(MOSS[1:4])
            for k in range(h):
                im.put(x + lean * k, T - 1 - k, col if k < h - 2 else MOSS[3])
    elif kind == 'fern':
        cx = rnd.randrange(10, 22)
        for side in (-1, 1):
            for k in range(3):
                ang = -math.pi / 2 + side * (0.35 + k * 0.32)
                L = 15 - k * 3
                for i in range(L):
                    x = cx + math.cos(ang) * i; y = T - 2 + math.sin(ang) * i + i * i * 0.015
                    im.put(x, y, MOSS[2] if i < L - 3 else MOSS[3])
                    if i % 3 == 0: im.put(x + side, y + 1, MOSS[1])
    elif kind == 'flower':
        for _ in range(4):
            x = rnd.randrange(4, 28); h = rnd.randrange(8, 18)
            for k in range(h): im.put(x, T - 1 - k, MOSS[1])
            im.disc(x + 0.5, T - h - 0.5, 1.6, '#7fe7f2'); im.put(x, T - h - 1, '#e4fdff')
    elif kind == 'reeds':
        for _ in range(6):
            x = rnd.randrange(3, 29); h = rnd.randrange(14, 26)
            for k in range(h): im.put(x, T - 1 - k, MOSS[1] if k < h - 4 else MOSS[2])
            im.rect(x, T - h - 3, x, T - h, '#3b2c22')
    return im


# ---------------- fondos (siluetas) ----------------
def silhouette_tiles(base, light, seed):
    """0 = bloque lleno; 1..4 = cimas irregulares (la parte de arriba transparente)"""
    T = 32
    full = Img(T, T)
    for y in range(T):
        for x in range(T):
            full.put(x, y, light if noise2(x // 4, y // 4, seed) > 0.86 else base)
    tiles = [full]
    for v in range(4):
        im = Img(T, T)
        for x in range(T):
            h = int(6 + 22 * vnoise(x / 9.0 + v * 3.7, seed + v))
            for y in range(T - h, T):
                im.put(x, y, light if y == T - h else base)
        tiles.append(im)
    return tiles


def wall_tiles(seed=11):
    """pared del fondo de la cueva (detras del camino): bloque y bordes deshilachados"""
    T = 32
    tiles = []
    for v in range(3):
        im = Img(T, T)
        for y in range(T):
            for x in range(T):
                n = noise2(x // 3, y // 3, seed + v)
                im.put(x, y, WALL3 if n > 0.85 else WALL2 if n > 0.45 else WALL)
        tiles.append(im)
    return tiles


def foreground_tiles(seed=21):
    """primer plano casi negro: 0 lleno, 1 cima de hierba, 2 raices colgando"""
    T = 32
    full = Img(T, T); full.rect(0, 0, T - 1, T - 1, FG[0])
    top = Img(T, T)
    for x in range(T):
        h = int(10 + 12 * vnoise(x / 5.0, seed))
        for y in range(T - h, T): top.put(x, y, FG[0])
        for k in range(int(4 * noise2(x, 2, seed))): top.put(x, T - h - k, FG[1])
    roots = Img(T, T)
    for x in range(0, T, 3):
        L = int(6 + 20 * noise2(x, 5, seed))
        for y in range(L): roots.put(x + (y // 7) % 2, y, FG[0])
    return [full, top, roots]


# ---------------- personaje ----------------
PL = dict(cloak='#1c2233', cloak2='#2b3550', cloak3='#3e4a6a', face='#eef1f7', face2='#c5cbdb', eye='#0b0e16', scarf='#4fd1e0', scarf2='#2a9fb3', leg='#121621')


def player_frame(pose, f=0):
    """32x32; la caja de colision (18x26) va de y=3 a y=29: los pies terminan en la fila 28"""
    im = Img(32, 32)
    c = PL
    bob = 0
    legs = [(0, 0), (0, 0)]
    lean = 0
    scarf = 'down'
    if pose == 'idle':
        bob = f % 2
    elif pose == 'run':
        cyc = [(-3, 2), (-1, 0), (3, -2), (1, 0)][f % 4]
        legs = [(cyc[0], 0), (cyc[1], 0)]
        bob = (0, 1, 0, 1)[f % 4]; lean = 1; scarf = 'back'
    elif pose == 'jump':
        legs = [(-2, -2), (2, -1)]; scarf = 'down2'
    elif pose == 'fall':
        legs = [(-2, 0), (3, -1)]; scarf = 'up'
    elif pose == 'dash':
        legs = [(-4, -2), (-1, -3)]; lean = 3; scarf = 'far'
    by = 2 + bob
    # piernas
    for i, (dx, dy) in enumerate(legs):
        x = 13 + i * 5 + dx
        im.rect(x, 24 + dy, x + 1, 28 + dy, c['leg'])
    # capa (trapecio)
    for y in range(13 + by, 26 + by):
        k = (y - 13 - by) / 12.0
        half = 5 + int(k * 4)
        x0, x1 = 16 - half + lean - int(k * lean), 16 + half + lean - int(k * lean * 2)
        for x in range(x0, x1 + 1):
            im.put(x, y, c['cloak2'] if x == x0 + 1 or y == 13 + by else c['cloak'])
        im.put(x0, y, c['cloak'])
    # bajo de la capa irregular
    for x in range(10 + lean, 23 + lean):
        if (x + f) % 3 == 0: im.put(x, 26 + by, c['cloak'])
    # capucha y cara
    hx, hy = 16 + lean, 8 + by
    im.disc(hx, hy, 7.2, c['cloak'])
    im.disc(hx - 1, hy - 1, 6.0, c['cloak2'])
    im.disc(hx + 1.5, hy + 1, 4.2, c['face'])
    im.rect(hx - 2, hy + 3, hx + 4, hy + 4, c['face2'])
    im.rect(hx + 1, hy, hx + 1, hy + 1, c['eye']); im.rect(hx + 4, hy, hx + 4, hy + 1, c['eye'])
    # punta de la capucha
    im.put(hx - 6, hy - 4, c['cloak']); im.put(hx - 7, hy - 5, c['cloak']); im.put(hx - 5, hy - 6, c['cloak3'])
    # bufanda
    sy = hy + 6
    im.rect(hx - 4, sy, hx + 4, sy + 1, c['scarf'])
    im.rect(hx - 4, sy + 1, hx + 4, sy + 1, c['scarf2'])
    tails = {'down': [(-4, 2), (-5, 3), (-5, 4), (-6, 5)], 'down2': [(-4, 2), (-5, 3), (-6, 4)],
             'back': [(-5, 1), (-7, 1), (-9, 2), (-11, 2), (-12, 3)], 'up': [(-4, -1), (-6, -2), (-7, -3), (-8, -4)],
             'far': [(-5, 0), (-8, 0), (-11, 1), (-14, 1), (-16, 1), (-18, 2)]}[scarf]
    for (dx, dy) in tails:
        im.put(hx + dx, sy + dy, c['scarf']); im.put(hx + dx, sy + dy + 1, c['scarf2'])
    if pose == 'dash':   # estela
        for y in range(14, 24, 3):
            im.line(2, y + by, 8, y + by, '#4fd1e0' if y % 2 else '#2a9fb3')
    return im


def saw_sprite():
    im = Img(48, 48)
    cx = cy = 23.5
    for y in range(48):
        for x in range(48):
            dx, dy = x + 0.5 - 24, y + 0.5 - 24
            r = math.hypot(dx, dy)
            a = math.atan2(dy, dx)
            tooth = 21 + 2.5 * ((a * 12 / (2 * math.pi)) % 1.0)
            if r <= tooth:
                if r < 5: col = '#22262f'
                elif r < 7: col = '#6b7588'
                elif r < 16:
                    col = '#8d99ad' if (a * 3 / math.pi) % 1 > 0.5 else '#77839a'
                elif r < 19: col = '#b8c3d4'
                else: col = '#dfe6f1' if dx + dy < 0 else '#9aa7b8'
                im.put(x, y, col)
    for k in range(3):
        a = k * 2 * math.pi / 3
        im.disc(24 + math.cos(a) * 10, 24 + math.sin(a) * 10, 2.2, '#3b4252')
    return im


def checkpoint_sprite(lit):
    """farol de piedra 32x48 (apagado / encendido)"""
    im = Img(32, 48)
    im.rect(9, 40, 22, 47, '#2a3349'); im.rect(10, 40, 21, 40, '#3c4763')
    im.rect(13, 18, 18, 39, '#2e3850'); im.rect(13, 18, 13, 39, '#40506e')
    im.rect(8, 10, 23, 18, '#2e3850'); im.rect(8, 10, 23, 10, '#4a5a7a')
    im.rect(10, 12, 21, 16, '#0d1220' if not lit else '#bff8ff')
    if lit:
        im.rect(12, 13, 19, 15, '#ffffff')
        for (x, y) in ((15, 6), (16, 4), (14, 8), (17, 7)): im.put(x, y, '#8ff0ff')
    else:
        im.rect(12, 13, 19, 15, '#1c2438')
    im.rect(10, 6, 21, 9, '#36415c'); im.rect(13, 3, 18, 5, '#36415c')
    return im


def goal_sprite():
    """arco de piedra palida con luz, 64x64"""
    im = Img(64, 64)
    for y in range(64):
        for x in range(64):
            d = math.hypot(x + 0.5 - 32, y + 0.5 - 30)
            if d < 28 and noise2(x, y, 5) > 0.55 + d / 50:
                im.put(x, y, '#9ff3ff' if d < 14 else '#4fb7c9')
    im.rect(8, 10, 15, 63, '#c9d3e6'); im.rect(48, 10, 55, 63, '#c9d3e6')
    im.rect(8, 10, 9, 63, '#eef3fb'); im.rect(54, 10, 55, 63, '#9aa6be')
    for x in range(6, 58):
        y = 10 - int(8 * math.sin((x - 6) / 52 * math.pi))
        im.rect(x, y, x, y + 5, '#c9d3e6'); im.put(x, y, '#eef3fb')
    im.disc(32, 30, 6, '#ffffff'); im.disc(32, 30, 4, '#e8feff')
    im.rect(4, 58, 59, 63, '#8e9ab3'); im.rect(4, 58, 59, 58, '#d7e0ef')
    return im


def sky_sprite():
    """degradado vertical 4x68 (se escala a toda la pantalla, con bandas de pixel art)"""
    im = Img(4, 68)
    for y in range(68):
        k = y / 67.0
        i = min(len(SKY) - 1, int(k * len(SKY)))
        for x in range(4):
            # tramado entre bandas
            j = i + 1 if (k * len(SKY) - i > 0.75 and (x + y) % 2 == 0 and i + 1 < len(SKY)) else i
            im.put(x, y, SKY[j])
    return im


def mote_sprite():
    """lucecita de 3x3 para los botones/menu (las motas del juego son particulas)"""
    im = Img(3, 3)
    im.put(1, 0, '#8ff0ff'); im.put(0, 1, '#8ff0ff'); im.put(2, 1, '#8ff0ff'); im.put(1, 2, '#8ff0ff'); im.put(1, 1, '#ffffff')
    return im


# ---------------- siluetas grandes (sprites para las capas de fondo) ----------------
def mountain_sprite(w, h, seed, base, rim, fog):
    """monte con cresta suave, borde iluminado y niebla hacia abajo (se apoya en el borde inferior)"""
    im = Img(w, h)
    for x in range(w):
        k = x / (w - 1)
        env = math.sin(k * math.pi) ** 0.7                     # forma de monte
        ridge = 0.75 * vnoise(x / 23.0, seed) + 0.25 * vnoise(x / 7.0, seed + 1)
        top = int(h - (h * 0.25 + h * 0.75 * env * (0.55 + 0.45 * ridge)))
        for y in range(max(0, top), h):
            f = (y - top) / max(1, h - top)
            col = rim if y - top < 2 else (fog if f > 0.82 and (x + y) % 2 == 0 else base)
            im.put(x, y, col)
    return im


def spike_rock_sprite(w, h, seed, base, rim, hanging):
    """columna o estalactita: punta afilada y ancha en la base; hanging = cuelga del techo"""
    im = Img(w, h)
    for y in range(h):
        k = y / (h - 1)
        if hanging:
            k = 1 - k
        half = (w / 2 - 1) * (k ** 0.8) * (0.85 + 0.15 * vnoise(y / 9.0, seed))
        cx = w / 2 + 3 * (vnoise(y / 31.0, seed + 2) - 0.5)
        for x in range(int(cx - half), int(cx + half) + 1):
            edge = x <= cx - half + 1.5
            im.put(x, y, rim if edge else base)
    return im


def wall_autotile(mask, seed=11):
    """pared del fondo con bordes irregulares: mascara como rock_tile (1 arriba, 2 derecha, 4 abajo, 8 izquierda libres)"""
    T = 32
    im = Img(T, T)
    for y in range(T):
        for x in range(T):
            n = noise2(x // 3, y // 3, seed)
            im.put(x, y, WALL3 if n > 0.86 else WALL2 if n > 0.45 else WALL)
    for x in range(T):
        if mask & 1:
            d = int(3 + 6 * vnoise(x / 6.0, seed + 1))
            for y in range(d): im.put(x, y, None)
            im.put(x, d, WALL3)
        if mask & 4:
            d = int(3 + 6 * vnoise(x / 6.0, seed + 2))
            for y in range(d): im.put(x, T - 1 - y, None)
    for y in range(T):
        if mask & 8:
            d = int(3 + 6 * vnoise(y / 6.0, seed + 3))
            for x in range(d): im.put(x, y, None)
            if im.get(d, y): im.put(d, y, WALL3)
        if mask & 2:
            d = int(3 + 6 * vnoise(y / 6.0, seed + 4))
            for x in range(d): im.put(T - 1 - x, y, None)
    return im
