#!/usr/bin/env python3
"""Arte nuevo de Platform Test (el Limbo), con la paleta de tools/platform_art.py:
pasto alto que se aparta al pasar, santuarios de las habilidades, recuerdos flotantes, Velo (el espiritu guia),
botones del menu dibujados (sin texto: un icono cada uno), lianas, hongos, cristales y lirios palidos."""
import math, random
from platform_art import Img, noise2, MOSS, ROCK

PALE = ['#cfe9ff', '#9fd8ff', '#ffffff', '#8ff0ff']
VINE = ['#1f5a4c', '#2d7a63', '#3f9a7a', '#173f37']


def grass_blade(seed):
    """mata de pasto de 32x64: solo la mitad de arriba tiene pasto, asi el centro del sprite es la base (al girar,
    se dobla desde el suelo)"""
    im = Img(32, 64)
    r = random.Random(seed)
    for _ in range(9 + seed % 4):
        x0 = r.uniform(5, 27)
        h = r.uniform(12, 30)
        lean = r.uniform(-0.45, 0.45)
        c = r.choice(MOSS[:4])
        for k in range(int(h)):
            t = k / h
            x = x0 + lean * k + math.sin(t * 2.4 + seed) * 1.2
            y = 31 - k
            im.put(x, y, c if t < 0.8 else MOSS[min(4, MOSS.index(c) + 1)])
            if t < 0.35:
                im.put(x + 1, y, c)
    if seed % 3 == 0:                          # algunas con una flor palida (lirio del limbo)
        x, y = r.randint(10, 22), r.randint(4, 10)
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, -1), (0, 1)):
            im.put(x + dx, y + dy, PALE[0])
        im.put(x, y, '#fff3c9')
    return im


def shrine(ability, awake):
    """santuario de 32x64: pedestal de piedra y una runa (dash: alas; pared: garra). Despierto, brilla"""
    im = Img(32, 64)
    im.rect(4, 54, 27, 63, ROCK[1]); im.rect(4, 54, 27, 54, ROCK[3])
    im.rect(8, 26, 23, 53, ROCK[2]); im.rect(8, 26, 8, 53, ROCK[4]); im.rect(23, 26, 23, 53, ROCK[0])
    im.rect(6, 22, 25, 26, ROCK[3]); im.rect(6, 22, 25, 22, ROCK[5])
    glow = '#8ff0ff' if ability == 'dash' else '#9be3bf'
    dim = '#2c374f'
    c = glow if awake else dim
    if ability == 'dash':                      # dos alas
        for i in range(6):
            im.line(15 - i, 34 + i, 11 - i, 38 + i // 2, c)
            im.line(16 + i, 34 + i, 20 + i, 38 + i // 2, c)
        im.rect(15, 32, 16, 46, c)
    else:                                      # garra de tres dedos
        for dx in (-4, 0, 4):
            im.line(15.5 + dx, 46, 15.5 + dx * 1.4, 33, c)
            im.put(15.5 + dx * 1.5, 32, c)
        im.rect(11, 46, 20, 48, c)
    # cristal flotante encima
    cc = glow if awake else '#3a4a68'
    for y in range(6, 20):
        w = 5 - abs(y - 13) * 0.7
        for x in range(int(16 - w), int(16 + w) + 1):
            im.put(x, y, cc if x < 16 else ('#ffffff' if awake else '#4a5a7a'))
    return im


def memory_orb(f):
    """recuerdo: luz palida que late (24x24)"""
    im = Img(24, 24)
    rad = 6 + (1 if f == 1 else 0) + (2 if f == 2 else 0)
    for y in range(24):
        for x in range(24):
            d = math.hypot(x + 0.5 - 12, y + 0.5 - 12)
            if d < rad * 0.45:
                im.put(x, y, '#ffffff')
            elif d < rad * 0.75:
                im.put(x, y, PALE[0])
            elif d < rad and noise2(x, y, f + 3) > 0.35:
                im.put(x, y, PALE[1])
    return im


def velo(f):
    """Velo, el espiritu guia: una llamita encapuchada que sostiene un farol (32x40)"""
    im = Img(32, 40)
    bob = 1 if f else 0
    # capucha / cuerpo de niebla
    for y in range(6, 34):
        w = 4 + (y - 6) * 0.38
        for x in range(int(14 - w), int(14 + w) + 1):
            wave = math.sin(y * 0.7 + f * 2) * 0.8
            if y > 28 and (x + y + f) % 3 == 0:
                continue
            im.put(x + wave, y + bob, '#cfe9ff' if x < 12 else '#9fb8d8')
    im.disc(14, 13 + bob, 5.5, '#1a2236')
    im.put(12, 13 + bob, '#8ff0ff'); im.put(16, 13 + bob, '#8ff0ff')
    # farol
    im.line(22, 18 + bob, 24, 22 + bob, '#5d6e8b')
    im.rect(22, 22 + bob, 27, 29 + bob, '#36435d')
    im.rect(23, 23 + bob, 26, 28 + bob, '#ffd27a' if f == 0 else '#fff3c9')
    return im


def button(icon, hover):
    """boton del menu (176x72): losa de piedra con una runa luminosa. Sin texto: play, continuar o salir"""
    W, H = 176, 72
    im = Img(W, H)
    base, edge, top = ('#24304a', '#4fd1e0', '#33415f') if hover else ('#171f33', '#2e3c5c', '#202a42')
    for y in range(H):
        for x in range(W):
            cut = min(x, y, W - 1 - x, H - 1 - y)
            if cut < 0 or (x + y < 8) or (W - 1 - x + y < 8) or (x + H - 1 - y < 8) or (W - 1 - x + H - 1 - y < 8):
                continue
            c = edge if cut < 2 else top if y < H * 0.45 else base
            if cut >= 2 and noise2(x, y, 9) > 0.93:
                c = '#2c374f'
            im.put(x, y, c)
    g = '#8ff0ff' if hover else '#5fa3c4'
    cx, cy = W // 2, H // 2
    if icon == 'play':
        for y in range(-14, 15):
            w = int((14 - abs(y)) * 1.3)
            im.line(cx - 8, cy + y, cx - 8 + w, cy + y, g)
    elif icon == 'continue':                  # dos triangulos
        for off in (-14, 8):
            for y in range(-11, 12):
                w = int((11 - abs(y)) * 1.1)
                im.line(cx + off - 4, cy + y, cx + off - 4 + w, cy + y, g)
    else:                                     # puerta con flecha hacia fuera
        im.rect(cx - 22, cy - 18, cx - 4, cy + 18, g); im.rect(cx - 19, cy - 15, cx - 7, cy + 18, base)
        im.rect(cx - 10, cy, cx - 9, cy + 1, g)
        im.line(cx + 2, cy, cx + 22, cy, g); im.line(cx + 2, cy + 1, cx + 22, cy + 1, g)
        for k in range(7):
            im.put(cx + 22 - k, cy - k, g); im.put(cx + 22 - k, cy + 1 + k, g)
    if hover:                                  # brillo arriba
        im.line(12, 3, W - 13, 3, '#bff8ff')
    return im


def arrow_down():
    """flechita de "sigue" del dialogo (16x12)"""
    im = Img(16, 12)
    for y in range(8):
        im.line(y, y + 2, 15 - y, y + 2, '#8ff0ff')
    return im


# ---------------- tiles de decoracion (32x32) ----------------
def vine(part, seed):
    """liana que cuelga del techo: 'top' pegada a la roca, 'mid' y 'end'"""
    im = Img(32, 32)
    r = random.Random(seed)
    for k in range(2 + seed % 2):
        x = r.randint(6, 25)
        for y in range(32 if part != 'end' else r.randint(12, 26)):
            xx = x + math.sin((y + seed * 7) * 0.25) * 2
            im.put(xx, y, VINE[k % 3]); im.put(xx + 1, y, VINE[3])
            if y % 7 == (seed + k) % 7:
                im.put(xx - 1, y, VINE[2]); im.put(xx - 2, y + 1, VINE[2]); im.put(xx + 2, y, VINE[1])
    if part == 'end':
        im.disc(r.randint(10, 22), 26, 2, '#8ff0ff')
    return im


def mushrooms(seed):
    im = Img(32, 32)
    r = random.Random(seed)
    for _ in range(2 + seed % 2):
        x, h = r.randint(5, 26), r.randint(4, 10)
        im.rect(x, 31 - h, x + 1, 31, '#cfe9ff')
        w = r.randint(3, 5)
        for dx in range(-w, w + 2):
            im.put(x + dx, 31 - h, '#4fd1e0'); im.put(x + dx, 30 - h, '#8ff0ff' if abs(dx) < w else '#4fd1e0')
        im.put(x, 29 - h, '#bff8ff'); im.put(x - 1, 29 - h, '#8ff0ff'); im.put(x + 1, 29 - h, '#8ff0ff')
    return im


def crystals(seed):
    im = Img(32, 32)
    r = random.Random(seed)
    for _ in range(3):
        x, h, lean = r.randint(6, 24), r.randint(8, 18), r.uniform(-0.3, 0.3)
        for k in range(h):
            w = max(1, int((h - k) / h * 3))
            for dx in range(-w, w + 1):
                im.put(x + dx + lean * k, 31 - k, '#9fd8ff' if dx < 0 else '#cfe9ff' if dx == 0 else '#5fa3c4')
        im.put(x + lean * h, 31 - h, '#ffffff')
    return im
