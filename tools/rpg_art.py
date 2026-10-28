#!/usr/bin/env python3
"""Arte de "El Faro de Ámbar" (RPG de ejemplo que exportado ligero ocupa menos de 1 MB), todo dibujado por codigo en pixel art de 16x16.
Imagenes como listas de colores '#rrggbb' o None (transparente), igual que los sprites del editor.
Paleta calida de pueblo costero: hierba, arena, mar, tejados rojos; cueva violeta y faro de piedra.
Con "python3 tools/rpg_art.py salida.png" guarda una hoja con todo para revisarlo."""
import math, random, zlib

T = 16


class Img:
    def __init__(self, w, h, fill=None):
        self.w, self.h = w, h
        self.p = [fill] * (w * h)

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

    def ellipse(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                if ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1:
                    self.put(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(1, n)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), c)

    def outline(self, c):
        """contorno de 1 px alrededor de lo dibujado"""
        src = list(self.p)
        for y in range(self.h):
            for x in range(self.w):
                if src[y * self.w + x] is not None:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < self.w and 0 <= yy < self.h and src[yy * self.w + xx] is not None and src[yy * self.w + xx] != c:
                        self.p[y * self.w + x] = c
                        break
        return self

    def flipx(self):
        o = Img(self.w, self.h)
        for y in range(self.h):
            for x in range(self.w):
                o.p[y * self.w + x] = self.p[y * self.w + self.w - 1 - x]
        return o

    def paste(self, other, ox, oy):
        for y in range(other.h):
            for x in range(other.w):
                c = other.p[y * other.w + x]
                if c is not None:
                    self.put(ox + x, oy + y, c)
        return self

    def copy(self):
        o = Img(self.w, self.h)
        o.p = list(self.p)
        return o


def rng(*seed):
    """generador con semilla estable entre ejecuciones (hash() de Python cambia en cada proceso)"""
    return random.Random(zlib.crc32(repr(seed).encode()))


# ---------------- paleta ----------------
OUT = '#231a26'
GRASS = ['#3f7d3a', '#4b8f43', '#5aa24d', '#336a32', '#77bd5f']
DGRASS = ['#2d5a35', '#356a3c', '#27502f', '#447a45']
PATH = ['#b88e5a', '#a77f4d', '#c9a26c', '#94703f']
SAND = ['#e3cf95', '#d6be80', '#ecdcab']
WATER = ['#2d6cb0', '#3a82c6', '#5a9fdb', '#8cc4ee']
TRUNK = ['#6b4a2b', '#523720']
LEAF = ['#2a6a35', '#37843f', '#4f9f4a', '#1e4e29', '#6fbd5a']
PINE = ['#1f5a3c', '#2a7049', '#163f2c', '#3d8a5a']
ROCK = ['#8a8e99', '#6d717c', '#b0b4bd', '#53565f']
WALL = ['#e0cba1', '#c9ae80', '#efdcb4']
ROOF = ['#b04a3f', '#c95c4c', '#8a3530', '#de7a62']
WOOD = ['#8a5e35', '#6e4826', '#a8774a']
CAVE = ['#4a4058', '#41384f', '#55496a']
CWALL = ['#2a2233', '#352c42', '#1d1726', '#45395a']
CRYS = ['#7fe0ff', '#c3f6ff', '#4aa9d6']
STONE = ['#9d9589', '#8a8277', '#b3ab9e', '#756e64']
LWALL = ['#e9e3d6', '#cfc7b7', '#c64a3a', '#9e372b']


def noise_fill(img, cols, seed, weights=None):
    r = rng(seed)
    for i in range(len(img.p)):
        img.p[i] = r.choices(cols, weights=weights)[0]
    return img


# ---------------- tiles del suelo (opacos) ----------------
def grass(seed=0, flowers=False, pal=GRASS):
    im = noise_fill(Img(T, T), pal[:2], ('g', seed), [3, 2])
    r = rng('gt', seed)
    for _ in range(5):
        x, y = r.randrange(T), r.randrange(2, T)
        im.put(x, y, pal[2]); im.put(x, y - 1, pal[2])
        if r.random() < 0.5:
            im.put(x + 1, y, pal[3])
    if flowers:
        for _ in range(3):
            x, y = r.randrange(1, T - 1), r.randrange(1, T - 1)
            c = r.choice(['#f3e36b', '#ffffff', '#e87fa6', '#9fc8ff'])
            im.put(x, y, c); im.put(x - 1, y, c); im.put(x + 1, y, c); im.put(x, y - 1, c); im.put(x, y + 1, c)
            im.put(x, y, '#f7b733' if c != '#f3e36b' else '#d9822b')
    return im


def path(seed=0):
    im = noise_fill(Img(T, T), PATH[:2], ('p', seed), [3, 2])
    r = rng('pp', seed)
    for _ in range(4):
        x, y = r.randrange(T), r.randrange(T)
        im.put(x, y, PATH[2]); im.put(x + 1, y, PATH[3])
    return im


def sand(seed=0):
    im = noise_fill(Img(T, T), SAND[:2], ('s', seed), [4, 1])
    r = rng('ss', seed)
    for _ in range(3):
        im.put(r.randrange(T), r.randrange(T), SAND[2])
    return im


def water(seed=0):
    im = Img(T, T, WATER[0])
    r = rng('w', seed)
    for y in range(T):
        for x in range(T):
            if math.sin(x * 0.7 + y * 1.3 + seed) > 0.75:
                im.put(x, y, WATER[1])
    for _ in range(2):
        x, y = r.randrange(2, T - 4), r.randrange(2, T - 2)
        im.line(x, y, x + 3, y, WATER[3]); im.put(x + 1, y + 1, WATER[2])
    return im


def shore(seed=0):
    """arena arriba y agua abajo"""
    im = water(seed)
    s = sand(seed)
    for y in range(T):
        for x in range(T):
            edge = 6 + int(1.5 * math.sin(x * 0.8 + seed))
            if y < edge:
                im.put(x, y, s.get(x, y))
            elif y == edge:
                im.put(x, y, '#f4f1e2')
    return im


def planks(seed=0, vertical=True):
    im = Img(T, T, WOOD[0])
    for i in range(T):
        for j in range(T):
            a, b = (i, j) if vertical else (j, i)
            if a % 4 == 3:
                im.put(i, j, WOOD[1])
            elif (b + (a // 4) * 5) % 9 == 0:
                im.put(i, j, WOOD[2])
    return im


def cave_floor(seed=0):
    im = noise_fill(Img(T, T), CAVE[:2], ('c', seed), [3, 2])
    r = rng('cf', seed)
    for _ in range(3):
        x, y = r.randrange(T), r.randrange(T)
        im.put(x, y, CAVE[2])
    return im


def puddle(seed=0):
    im = cave_floor(seed)
    im.ellipse(8, 9, 6, 3.5, '#2f5f86')
    im.ellipse(7, 8, 3, 1.5, '#4f86b0')
    return im


def stone_floor(seed=0):
    im = Img(T, T, STONE[0])
    r = rng('sf', seed)
    for y in range(T):
        for x in range(T):
            if y % 8 == 7 or (x + (8 if (y // 8) % 2 else 0)) % 16 == 15:
                im.put(x, y, STONE[3])
            elif r.random() < 0.12:
                im.put(x, y, STONE[1 if r.random() < 0.6 else 2])
    return im


def carpet(seed=0):
    im = stone_floor(seed)
    im.rect(2, 0, 13, 15, '#7a2f3a')
    im.rect(3, 0, 12, 15, '#963c48')
    for y in range(0, T, 4):
        im.put(7, y, '#d9a441'); im.put(8, y, '#d9a441')
    return im


# ---------------- obstaculos (con transparencia, van encima del suelo) ----------------
def tree(seed=0):
    im = Img(T, T)
    im.rect(6, 11, 9, 15, TRUNK[0]); im.rect(6, 11, 6, 15, TRUNK[1])
    im.ellipse(8, 7, 7.5, 6.5, LEAF[0])
    im.ellipse(7.5, 6, 6, 5, LEAF[1])
    im.ellipse(6, 4.5, 3.2, 2.5, LEAF[2])
    r = rng('tr', seed)
    for _ in range(6):
        x, y = r.randrange(3, 13), r.randrange(2, 12)
        if im.get(x, y) == LEAF[1]:
            im.put(x, y, LEAF[2])
    im.outline(LEAF[3])
    return im


def pine(seed=0):
    im = Img(T, T)
    im.rect(7, 12, 8, 15, TRUNK[1])
    for k, (y0, w) in enumerate(((1, 2), (4, 4), (7, 6), (10, 7))):
        for y in range(y0, y0 + 4):
            ww = w * (y - y0 + 1) / 4 + 0.5
            for x in range(int(8 - ww), int(8 + ww)):
                im.put(x, y, PINE[1] if x < 8 else PINE[0])
    im.put(7, 2, PINE[3]); im.put(6, 6, PINE[3]); im.put(5, 10, PINE[3])
    im.outline(PINE[2])
    return im


def bush(seed=0, berries=False):
    im = Img(T, T)
    im.ellipse(8, 10, 7, 5.5, LEAF[1])
    im.ellipse(6, 8.5, 3.5, 2.5, LEAF[2])
    im.ellipse(11, 9.5, 2.5, 2, LEAF[2])
    if berries:
        for x, y in ((5, 11), (9, 8), (12, 12), (8, 13)):
            im.put(x, y, '#d8434c')
    im.outline(LEAF[3])
    return im


def rock(seed=0, pal=ROCK):
    im = Img(T, T)
    im.ellipse(8, 10, 7, 5, pal[1])
    im.ellipse(7, 9, 5.5, 3.8, pal[0])
    im.ellipse(5.5, 7.5, 2.5, 1.5, pal[2])
    im.line(9, 9, 12, 12, pal[3])
    im.outline(OUT)
    return im


def wall(seed=0, window=False, door=False):
    im = Img(T, T, WALL[0])
    for y in range(T):
        for x in range(T):
            if y % 5 == 4:
                im.put(x, y, WALL[1])
            elif (x + (y // 5) * 7) % 13 == 0:
                im.put(x, y, WALL[1])
    if window:
        im.rect(4, 3, 11, 11, '#5a3a22'); im.rect(5, 4, 10, 10, '#8fc6e8'); im.rect(5, 4, 6, 5, '#d8f0ff')
        im.line(5, 7, 10, 7, '#5a3a22'); im.line(7, 4, 7, 10, '#5a3a22'); im.line(8, 4, 8, 10, '#5a3a22')
    if door:
        im.rect(3, 2, 12, 15, '#4a2e18'); im.rect(4, 3, 11, 15, '#7a4c28')
        for x in (6, 9):
            im.line(x, 3, x, 15, '#6a4020')
        im.put(10, 9, '#e3c25b')
    return im


def roof(seed=0, edge=False, top=False):
    im = Img(T, T, ROOF[0])
    for y in range(T):
        for x in range(T):
            if y % 4 == 3:
                im.put(x, y, ROOF[2])
            elif (x + (y // 4) * 3) % 6 == 0:
                im.put(x, y, ROOF[1])
    if top:
        im.rect(0, 0, 15, 2, ROOF[2]); im.rect(0, 0, 15, 0, ROOF[3])
    if edge:
        im.rect(0, 13, 15, 15, ROOF[2]); im.rect(0, 15, 15, 15, '#5e2420')
    return im


def fence(seed=0):
    im = Img(T, T)
    for x in (2, 13):
        im.rect(x, 4, x + 1, 14, WOOD[0]); im.put(x, 4, WOOD[2])
    im.rect(0, 6, 15, 7, WOOD[2]); im.rect(0, 11, 15, 12, WOOD[0])
    im.outline(OUT)
    return im


def well():
    im = Img(T, T)
    im.ellipse(8, 10, 7, 5, ROCK[1]); im.ellipse(8, 9.5, 5, 3, '#1b2a44'); im.ellipse(8, 9, 3, 1.5, WATER[1])
    im.rect(2, 2, 3, 9, WOOD[1]); im.rect(12, 2, 13, 9, WOOD[1]); im.rect(1, 1, 14, 2, ROOF[0])
    im.outline(OUT)
    return im


def sign():
    im = Img(T, T)
    im.rect(7, 9, 8, 15, WOOD[1])
    im.rect(2, 3, 13, 9, WOOD[0]); im.rect(3, 4, 12, 8, WOOD[2])
    im.line(4, 5, 11, 5, WOOD[1]); im.line(4, 7, 9, 7, WOOD[1])
    im.outline(OUT)
    return im


def stump():
    im = Img(T, T)
    im.ellipse(8, 11, 6, 4, TRUNK[0]); im.ellipse(8, 9, 6, 3, '#c8a06a'); im.ellipse(8, 9, 3, 1.5, '#a87f4c')
    im.outline(OUT)
    return im


def cave_wall(seed=0, crystal=False):
    im = noise_fill(Img(T, T), CWALL[:2], ('cw', seed), [2, 3])
    r = rng('cwr', seed)
    for _ in range(4):
        x, y = r.randrange(T), r.randrange(T)
        im.line(x, y, x + r.randint(1, 3), y + 1, CWALL[2])
    for x in range(T):
        im.put(x, 0, CWALL[3])
    if crystal:
        for cx, h in ((5, 9), (9, 12), (12, 7)):
            im.line(cx, 15, cx, 15 - h, CRYS[0]); im.line(cx + 1, 15, cx + 1, 16 - h, CRYS[2])
            im.put(cx, 15 - h, CRYS[1])
    return im


def stalagmite():
    im = Img(T, T)
    for y in range(3, 16):
        w = (y - 2) * 0.45
        for x in range(int(8 - w), int(8 + w) + 1):
            im.put(x, y, CWALL[1] if x < 8 else CWALL[0])
    im.outline(OUT)
    return im


def lwall(seed=0):
    """pared del faro: piedra blanca con franja roja"""
    im = Img(T, T, LWALL[0])
    for y in range(T):
        for x in range(T):
            if 6 <= y <= 9:
                im.put(x, y, LWALL[2] if y > 6 else LWALL[3])
            elif y % 5 == 4 or (x + (y // 5) * 6) % 11 == 0:
                im.put(x, y, LWALL[1])
    return im


def brazier(lit=True):
    im = Img(T, T)
    im.rect(6, 9, 9, 15, ROCK[1]); im.rect(4, 7, 11, 9, ROCK[0]); im.rect(4, 7, 11, 7, ROCK[2])
    if lit:
        im.ellipse(8, 4.5, 3, 3.5, '#f08a24'); im.ellipse(8, 5, 1.6, 2, '#ffd75e')
    im.outline(OUT)
    return im


# ---------------- personajes ----------------
SKIN = ('#f2c8a0', '#d9a47c')


def person(d, f, pal, kind='normal'):
    """chibi de 16x16: d = 'abajo' | 'arriba' | 'lado' (mirando a la derecha); f = 0/1 (paso)"""
    hair, hair2, top, top2, legs, shoes = pal['hair'], pal['hair2'], pal['top'], pal['top2'], pal.get('legs', '#4a3a2a'), pal.get('shoes', '#2e2218')
    skin, skin2 = pal.get('skin', SKIN[0]), pal.get('skin2', SKIN[1])
    im = Img(T, T)
    bob = 1 if f == 1 else 0
    y0 = 1 + bob
    # piernas y pies
    if kind == 'dress':
        im.rect(4, 10 + bob, 11, 14, top2); im.rect(5, 10 + bob, 10, 13, top)
        im.rect(5, 15, 6, 15, shoes); im.rect(9, 15, 10, 15, shoes)
    elif d == 'lado':
        if f == 1:
            im.rect(6, 13, 9, 14, legs); im.rect(6, 15, 9, 15, shoes)
        else:
            im.rect(5, 13, 6, 14, legs); im.rect(9, 13, 10, 14, legs); im.put(4, 15, shoes); im.put(5, 15, shoes); im.put(10, 15, shoes); im.put(11, 15, shoes)
    else:
        l, r = (0, 1) if f == 1 else (0, 0)
        im.rect(5, 13, 6, 14 - l, legs); im.rect(9, 13, 10, 14 - r, legs)
        im.rect(5, 15 - l, 6, 15 - l, shoes); im.rect(9, 15, 10, 15, shoes)
    # cuerpo
    if kind != 'dress':
        im.rect(4, 9 + bob, 11, 12 + bob, top); im.rect(4, 12 + bob, 11, 12 + bob, top2)
        if pal.get('belt'):
            im.rect(4, 11 + bob, 11, 11 + bob, pal['belt'])
    else:
        im.rect(4, 9 + bob, 11, 10 + bob, top)
    if pal.get('apron') and d != 'arriba':
        im.rect(6, 10 + bob, 9, 13, pal['apron'])
    # brazos
    if d == 'lado':
        im.rect(7, 10 + bob, 8, 12 + bob, top2); im.put(8, 13, skin)
    else:
        im.rect(3, 9 + bob, 3, 11 + bob, top2); im.rect(12, 9 + bob, 12, 11 + bob, top2)
        im.put(3, 12 + bob, skin); im.put(12, 12 + bob, skin)
    # cabeza
    im.ellipse(8, y0 + 4.5, 4.6, 4.3, skin)
    if d == 'abajo':
        im.rect(4, y0, 11, y0 + 2, hair); im.rect(4, y0 + 3, 4, y0 + 5, hair); im.rect(11, y0 + 3, 11, y0 + 5, hair)
        im.put(5, y0 + 3, hair); im.put(10, y0 + 3, hair); im.put(7, y0 + 3, hair2)
        im.put(6, y0 + 5, OUT); im.put(9, y0 + 5, OUT); im.put(6, y0 + 4, '#ffffff' if False else skin)
        im.put(5, y0 + 6, '#e89a8a'); im.put(10, y0 + 6, '#e89a8a')
        im.rect(5, y0 + 7, 10, y0 + 7, skin2)
    elif d == 'arriba':
        im.ellipse(8, y0 + 4, 4.6, 4.3, hair); im.rect(4, y0 + 6, 11, y0 + 7, hair2)
    else:
        im.rect(4, y0, 11, y0 + 2, hair); im.rect(4, y0 + 3, 7, y0 + 6, hair); im.put(8, y0 + 3, hair)
        im.put(10, y0 + 5, OUT); im.put(11, y0 + 6, '#e89a8a'); im.put(12, y0 + 5, skin)
        im.put(6, y0 + 4, hair2)
    if pal.get('bun') and d != 'abajo':
        im.ellipse(8 if d == 'arriba' else 5, y0 + 1, 2, 1.5, hair2)
    if pal.get('bun') and d == 'abajo':
        im.ellipse(8, y0 - 0.3, 2.2, 1.3, hair2)
    if pal.get('helmet'):
        im.rect(4, y0, 11, y0 + 2, pal['helmet']); im.rect(4, y0, 11, y0, '#d7dde6')
        if d == 'abajo':
            im.put(8, y0 + 3, pal['helmet'])
    if pal.get('hat'):
        im.rect(3, y0 + 1, 12, y0 + 2, pal['hat']); im.rect(5, y0 - 1, 10, y0 + 1, pal['hat'])
    if pal.get('beard') and d != 'arriba':
        xs = range(5, 11) if d == 'abajo' else range(8, 12)
        for x in xs:
            im.put(x, y0 + 7, pal['beard']); im.put(x, y0 + 8, pal['beard'])
        im.put(xs[0] + 1, y0 + 9, pal['beard']); im.put(xs[-1] - 1, y0 + 9, pal['beard'])
    im.outline(OUT)
    return im


HERO = dict(hair='#7a4524', hair2='#5e331a', top='#3f9e5a', top2='#2d7442', legs='#5a4632', belt='#8a5a2e')
NPCS = {
    'rosa': (dict(hair='#d8d4df', hair2='#b9b3c4', top='#8a5aa8', top2='#6d4489', bun=True, shoes='#4b3348'), 'dress'),
    'bruno': (dict(hair='#3b2a1e', hair2='#2a1d14', top='#c96f3b', top2='#a5552a', apron='#efe6d0', beard='#3b2a1e'), 'normal'),
    'lia': (dict(hair='#e0913a', hair2='#c4752a', top='#e8668a', top2='#c64c6f', shoes='#5a3a5a'), 'dress'),
    'tomas': (dict(hair='#4a4a52', hair2='#35353c', top='#3e6fa8', top2='#2e5585', hat='#e3c46b', beard='#bfbfc6', legs='#3c3c48'), 'normal'),
    'mateo': (dict(hair='#2a2a2a', hair2='#1a1a1a', top='#7d8796', top2='#5e6776', helmet='#9aa5b5', legs='#4a4f5c', belt='#5a3a22'), 'normal'),
}


def hero(d, f):
    return person(d, f, HERO)


def npc(who, f):
    pal, kind = NPCS[who]
    return person('abajo', f, pal, kind)


def cat(f=0):
    im = Img(T, T)
    c, c2 = '#e08a3a', '#b8652a'
    im.ellipse(8, 11, 5, 3, c)
    im.ellipse(4.5, 7.5, 3, 2.8, c)
    im.put(2, 4, c); im.put(3, 5, c); im.put(6, 4, c); im.put(6, 5, c)
    im.put(3, 7, OUT); im.put(5, 7, OUT); im.put(4, 9, '#f3a0a0')
    for x in (7, 10):
        im.rect(x, 13, x, 14, c2)
    tail = [(13, 10), (14, 9), (14, 8), (13, 7)] if f == 0 else [(13, 10), (14, 10), (15, 9), (15, 8)]
    for x, y in tail:
        im.put(x, y, c2)
    for x in (8, 10, 12):
        im.put(x, 10, c2)
    im.outline(OUT)
    return im


def slime(f=0, pal=('#5bc46a', '#3e9a50', '#a6f0a0', '#24693a')):
    im = Img(T, T)
    rx, ry, cy = (6.5, 5, 10.5) if f == 0 else (7.5, 4, 11.5)
    im.ellipse(8, cy, rx, ry, pal[0])
    im.ellipse(8, cy + 1.5, rx - 1, ry - 2, pal[1])
    im.ellipse(5.5, cy - 2.5, 1.8, 1.2, pal[2])
    im.put(6, int(cy) - 1, OUT); im.put(10, int(cy) - 1, OUT); im.put(6, int(cy) - 2, '#ffffff'); im.put(10, int(cy) - 2, '#ffffff')
    im.outline(pal[3])
    return im


def bat(f=0):
    im = Img(T, T)
    body, wing, wing2 = '#4a3a6a', '#6b55a0', '#38295a'
    im.ellipse(8, 8, 3, 3.5, body)
    im.put(6, 4, body); im.put(10, 4, body)
    if f == 0:
        for i in range(6):
            im.line(5 - i, 7 - i // 2, 5 - i, 9 - i // 3, wing); im.line(10 + i, 7 - i // 2, 10 + i, 9 - i // 3, wing)
    else:
        for i in range(6):
            im.line(5 - i, 8 + i // 2, 5 - i, 10 + i // 3, wing); im.line(10 + i, 8 + i // 2, 10 + i, 10 + i // 3, wing)
    im.put(7, 7, '#ffd75e'); im.put(9, 7, '#ffd75e'); im.put(7, 10, '#ffffff'); im.put(9, 10, '#ffffff')
    im.outline(wing2)
    return im


def shadow_boss(f=0):
    S = 32
    im = Img(S, S)
    c0, c1, c2 = '#2a1838', '#3f2552', '#5a3874'
    for y in range(6, S):
        w = 5 + (y - 6) * 0.45
        wave = math.sin((y * 0.6) + f * 2.2) * (1.5 if y > 22 else 0.5)
        for x in range(int(16 - w + wave), int(16 + w + wave) + 1):
            im.put(x, y, c1 if abs(x - 16) < w * 0.6 else c0)
    im.ellipse(16, 9, 7, 7, c0)
    im.ellipse(16, 10, 5, 5, '#120a1a')
    for x in range(6, 26, 4):
        if f == 0:
            im.put(x, S - 1, None); im.put(x + 1, S - 2, None)
        else:
            im.put(x + 2, S - 1, None); im.put(x + 3, S - 2, None)
    for ex in (13, 18):
        im.rect(ex, 10, ex + 1, 11, '#ffb43a'); im.put(ex, 10, '#fff2b0')
    im.line(9, 18, 5, 24, c2); im.line(23, 18, 27, 24, c2)
    im.outline('#0c0612')
    return im


def orb(f=0):
    im = Img(8, 8)
    im.ellipse(4, 4, 3.6, 3.6, '#7a3fb0' if f == 0 else '#9450cc')
    im.ellipse(4, 4, 2.2, 2.2, '#c48cff')
    im.put(3, 3, '#ffffff')
    return im


def slash():
    """tajo de espada hacia la derecha"""
    im = Img(T, T)
    for a in range(-70, 71, 4):
        r = math.radians(a)
        for rr, c in ((7, '#ffffff'), (6, '#d8f4ff'), (5, '#9fd8ff')):
            im.put(4 + math.cos(r) * rr, 8 + math.sin(r) * rr, c)
    return im


def coin(f=0):
    im = Img(8, 8)
    if f == 0:
        im.ellipse(4, 4, 3.5, 3.5, '#f2c12e'); im.ellipse(4, 4, 2, 2.5, '#ffe27a'); im.rect(4, 2, 4, 5, '#d39a1e')
    else:
        im.ellipse(4, 4, 1.6, 3.5, '#f2c12e'); im.rect(4, 1, 4, 6, '#ffe27a')
    im.outline('#7a5410')
    return im


def heart(full=True, w=8):
    im = Img(w, w)
    pts = ['.##.##..', '#######.', '#######.', '.#####..', '..###...', '...#....']
    for y, row in enumerate(pts):
        for x, ch in enumerate(row):
            if ch == '#':
                im.put(x, y + 1, '#e5384f' if full else '#4a3040')
    if full:
        im.put(1, 2, '#ffb0bc'); im.put(2, 2, '#ff7a8c')
    im.outline(OUT)
    return im


def potion():
    im = Img(8, 8)
    im.rect(3, 0, 4, 1, '#b08a5a')
    im.ellipse(4, 5, 3.4, 2.8, '#e04a6a'); im.ellipse(4, 5.5, 2.5, 1.8, '#ff7a96'); im.put(2, 4, '#ffffff')
    im.rect(3, 2, 4, 2, '#cfe6f2')
    im.outline(OUT)
    return im


def ember(f=0, size=12):
    im = Img(size, size)
    c = size / 2
    im.ellipse(c, c + 1, c - 1.5, c - 1, '#c2410c')
    im.ellipse(c, c + 1.5, c - 3, c - 2.8, '#f08a24')
    im.ellipse(c - 0.5, c + 1.5 - f, c - 4.5, c - 4.2, '#ffd75e')
    im.put(c, 1 + f, '#ffb43a'); im.put(c - 1, 2 + f, '#f08a24')
    im.outline('#5a1d08')
    return im


def chest(opened=False):
    im = Img(T, T)
    body, dark, gold = '#a8672e', '#7a461c', '#e3b23c'
    im.rect(2, 7, 13, 14, body); im.rect(2, 11, 13, 11, dark)
    if opened:
        im.rect(2, 3, 13, 6, dark); im.rect(3, 4, 12, 6, '#3a2310'); im.rect(4, 5, 11, 6, '#ffd75e')
    else:
        im.rect(2, 4, 13, 7, body); im.rect(2, 4, 13, 4, '#c88545'); im.rect(7, 6, 8, 9, gold)
    im.rect(2, 7, 13, 7, dark)
    for x in (4, 11):
        im.rect(x, 4 if not opened else 7, x, 14, gold)
    im.outline(OUT)
    return im


def lantern():
    im = Img(8, 8)
    im.rect(3, 0, 4, 0, '#5a5a66'); im.rect(1, 1, 6, 1, '#5a5a66'); im.rect(1, 6, 6, 6, '#5a5a66')
    im.rect(2, 2, 5, 5, '#ffd75e'); im.rect(3, 3, 4, 4, '#fff2b0')
    im.outline(OUT)
    return im


def key_item():
    im = Img(8, 8)
    im.ellipse(2.5, 2.5, 2, 2, '#e3b23c'); im.put(2, 2, None)
    im.line(3, 3, 7, 7, '#e3b23c'); im.put(6, 5, '#e3b23c'); im.put(5, 6, '#e3b23c')
    im.outline(OUT)
    return im


def sword_icon():
    im = Img(8, 8)
    im.line(1, 6, 6, 1, '#d8e4f0'); im.line(2, 6, 6, 2, '#9fb0c4')
    im.line(1, 4, 3, 6, '#8a5e35'); im.put(0, 7, '#8a5e35')
    im.outline(OUT)
    return im


def gate(opened=False):
    im = Img(T, T)
    if not opened:
        for x in range(2, 15, 3):
            im.rect(x, 1, x, 15, '#5a5f6b'); im.put(x, 1, '#9aa1ad')
        im.rect(1, 4, 14, 4, '#3e424c'); im.rect(1, 11, 14, 11, '#3e424c')
        im.rect(7, 6, 9, 9, '#e3b23c'); im.put(8, 8, OUT)
    else:
        for x in (1, 14):
            im.rect(x, 1, x, 15, '#5a5f6b')
    return im


def lamp(lit=False):
    im = Img(T, T)
    im.rect(3, 11, 12, 15, '#5a5f6b'); im.rect(3, 11, 12, 11, '#9aa1ad')
    im.rect(4, 3, 11, 10, '#cfe6f2' if not lit else '#ffd75e')
    im.rect(4, 3, 4, 10, '#5a5f6b'); im.rect(11, 3, 11, 10, '#5a5f6b'); im.rect(3, 1, 12, 2, '#3e424c')
    if lit:
        im.rect(6, 5, 9, 9, '#fff2b0'); im.rect(7, 6, 8, 8, '#ffffff')
    else:
        im.rect(6, 6, 9, 9, '#4a4f5c')
    im.outline(OUT)
    return im


def bubble():
    """aviso "!" sobre quien tiene algo que decir"""
    im = Img(8, 10)
    im.rect(1, 0, 6, 7, '#ffffff'); im.put(1, 0, None); im.put(6, 0, None); im.put(1, 7, None); im.put(6, 7, None)
    im.rect(3, 8, 4, 8, '#ffffff'); im.put(3, 9, '#ffffff')
    im.rect(3, 1, 4, 4, '#d9822b'); im.rect(3, 6, 4, 6, '#d9822b')
    im.outline(OUT)
    return im


def dialog_box(w, h):
    im = Img(w, h, '#1b1830')
    for x in range(w):
        for y in (0, 1, h - 2, h - 1):
            im.put(x, y, '#e8d9b0')
    for y in range(h):
        for x in (0, 1, w - 2, w - 1):
            im.put(x, y, '#e8d9b0')
    for x in range(3, w - 3):
        im.put(x, 3, '#3a3256'); im.put(x, h - 4, '#3a3256')
    for y in range(3, h - 3):
        im.put(3, y, '#3a3256'); im.put(w - 4, y, '#3a3256')
    for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        im.put(x, y, None)
    return im


def name_tag(w, h):
    im = Img(w, h, '#e8d9b0')
    im.rect(1, 1, w - 2, h - 2, '#3a3256')
    for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
        im.put(x, y, None)
    return im


def hud_panel(w, h):
    im = Img(w, h, '#1b1830')
    im.rect(0, h - 1, w - 1, h - 1, '#3a3256')
    return im


def lighthouse_big(lit=False):
    """faro grande para la portada (48x112)"""
    W, H = 48, 112
    im = Img(W, H)
    for y in range(30, H):
        w = 9 + (y - 30) * 0.12
        for x in range(int(24 - w), int(24 + w) + 1):
            band = ((y - 30) // 14) % 2 == 0
            c = '#e9e3d6' if band else '#c64a3a'
            if x > 24 + w * 0.35:
                c = '#cfc7b7' if band else '#9e372b'
            im.put(x, y, c)
    im.rect(12, 26, 36, 30, '#3e424c'); im.rect(10, 25, 38, 26, '#5a5f6b')
    im.rect(15, 12, 33, 25, '#ffd75e' if lit else '#4a4f5c')
    for x in (15, 21, 27, 33):
        im.rect(x, 12, x, 25, '#3e424c')
    if lit:
        im.rect(20, 15, 28, 22, '#fff2b0')
    im.ellipse(24, 9, 10, 5, '#3e424c'); im.rect(23, 0, 25, 5, '#3e424c')
    im.rect(20, 92, 27, 111, '#4a2e18'); im.rect(21, 93, 26, 111, '#7a4c28')
    im.outline(OUT)
    return im


def moon():
    im = Img(16, 16)
    im.ellipse(8, 8, 6.5, 6.5, '#f4ecc8')
    im.ellipse(10.5, 6.5, 5, 5, None)
    return im


def sky(w, h, top='#0d0b24', bottom='#3a2a5a'):
    im = Img(w, h)
    for y in range(h):
        t = y / max(1, h - 1)
        a = [int(top[i:i + 2], 16) for i in (1, 3, 5)]
        b = [int(bottom[i:i + 2], 16) for i in (1, 3, 5)]
        k = round(t * 6) / 6
        c = '#%02x%02x%02x' % tuple(int(a[j] + (b[j] - a[j]) * k) for j in range(3))
        for x in range(w):
            im.put(x, y, c)
    return im


if __name__ == '__main__':
    import sys
    from PIL import Image
    items = [grass(0), grass(1, True), path(), sand(), water(), shore(), planks(), cave_floor(), puddle(), stone_floor(), carpet(),
             tree(), pine(), bush(), bush(1, True), rock(), wall(), wall(window=True), wall(door=True), roof(), roof(edge=True), roof(top=True),
             fence(), well(), sign(), stump(), cave_wall(), cave_wall(1, True), stalagmite(), lwall(), brazier(),
             hero('abajo', 0), hero('abajo', 1), hero('arriba', 0), hero('lado', 0), hero('lado', 1)] + \
            [npc(k, 0) for k in NPCS] + [cat(), slime(), slime(1), bat(), bat(1), slash(), chest(), chest(True), gate(), lamp(), lamp(True)]
    S = 4
    cols = 12
    sheet = Image.new("RGBA", (300 * S, ((len(items) + cols - 1) // cols) * 18 * S + 120 * S), (40, 40, 48, 255))
    def blit(im, ox, oy):
        for y in range(im.h):
            for x in range(im.w):
                c = im.p[y * im.w + x]
                if c:
                    for dy in range(S):
                        for dx in range(S):
                            sheet.putpixel((ox + x * S + dx, oy + y * S + dy), tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) + (255,))
    for i, im in enumerate(items):
        blit(im, (i % cols) * 18 * S, (i // cols) * 18 * S)
    base = ((len(items) + cols - 1) // cols) * 18 * S
    for i, im in enumerate([shadow_boss(0), shadow_boss(1), lighthouse_big(True), ember(), coin(), heart(), heart(False), potion(), lantern(), key_item(), bubble(), orb()]):
        blit(im, i * 40 * S if i < 3 else (120 + i * 12) * S, base)
    sheet.save(sys.argv[1])
