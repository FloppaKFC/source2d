"""Arte de Project Ry-mik dibujado por codigo (PIL): casete, bobina, botones, ruido de tele, iconos.
Lo usa tools/make_rymik.py."""
import math, random
from PIL import Image, ImageDraw

def rgba(h, a=255):
    h = h.lstrip('#')
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)

def casete(w=176, h=112):
    """casete de musica: cuerpo oscuro, etiqueta clara (ahi va el logo y el nombre), ventana con las bobinas"""
    im = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 8, fill=rgba('#1d1d26'), outline=rgba('#0a0a0f'), width=2)
    d.rounded_rectangle((3, 3, w - 4, h - 4), 6, outline=rgba('#3a3a4a'), width=1)
    for x, y in ((7, 7), (w - 8, 7), (7, h - 8), (w - 8, h - 8)):
        d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=rgba('#8a8a99'), outline=rgba('#0a0a0f'))
        d.line((x - 2, y, x + 2, y), fill=rgba('#3a3a4a'))
    d.rounded_rectangle((12, 8, w - 13, 64), 4, fill=rgba('#efe6cf'), outline=rgba('#0a0a0f'), width=1)
    d.rectangle((12, 8, w - 13, 16), fill=rgba('#e0475a'))
    d.line((12, 16, w - 13, 16), fill=rgba('#0a0a0f'))
    d.rounded_rectangle((44, 70, w - 45, 94), 10, fill=rgba('#0b0b10'), outline=rgba('#5a5a6a'), width=1)
    d.rectangle((70, 76, w - 71, 88), fill=rgba('#3b2a20'))
    d.polygon([(30, h - 1), (40, h - 13), (w - 41, h - 13), (w - 31, h - 1)], fill=rgba('#2b2b36'), outline=rgba('#0a0a0f'))
    for x in (54, w // 2 - 4, w // 2 + 4, w - 55):
        d.ellipse((x - 2, h - 9, x + 2, h - 5), fill=rgba('#0a0a0f'))
    return im

def bobina(s=22):
    """bobina de la cinta (gira cuando el casete esta elegido)"""
    im = Image.new('RGBA', (s, s))
    d = ImageDraw.Draw(im)
    c = s / 2 - 0.5
    d.ellipse((0, 0, s - 1, s - 1), fill=rgba('#f2f2f2'), outline=rgba('#0a0a0f'))
    d.ellipse((c - 4, c - 4, c + 4, c + 4), fill=rgba('#0a0a0f'))
    for k in range(6):
        a = k * math.pi / 3
        x, y = c + math.cos(a) * 6.5, c + math.sin(a) * 6.5
        d.rectangle((x - 1, y - 1, x + 1, y + 1), fill=rgba('#0a0a0f'))
    return im

def logo_vacio(s=44):
    """etiqueta sin logo: una nota musical sobre un degradado"""
    im = Image.new('RGBA', (s, s))
    d = ImageDraw.Draw(im)
    for y in range(s):
        t = y / (s - 1)
        d.line((0, y, s - 1, y), fill=(int(90 + 120 * t), int(60 + 30 * t), int(200 - 60 * t), 255))
    d.ellipse((12, 26, 22, 34), fill=rgba('#ffffff'))
    d.rectangle((20, 10, 22, 30), fill=rgba('#ffffff'))
    d.polygon([(22, 10), (32, 14), (32, 18), (22, 14)], fill=rgba('#ffffff'))
    d.rectangle((0, 0, s - 1, s - 1), outline=rgba('#0a0a0f'))
    return im

def boton(w=200, h=28):
    """boton del menu principal (blanco: se tine al elegirlo)"""
    im = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 7, fill=rgba('#0a0a0f'))
    d.rounded_rectangle((2, 2, w - 3, h - 3), 5, fill=rgba('#ffffff'))
    d.rounded_rectangle((2, h // 2, w - 3, h - 3), 5, fill=rgba('#d8d8e4'))
    d.line((6, 4, w - 7, 4), fill=rgba('#ffffff'))
    return im

def circulo(s=48):
    im = Image.new('RGBA', (s, s))
    ImageDraw.Draw(im).ellipse((0, 0, s - 1, s - 1), fill=rgba('#ffffff'))
    return im

def ruido(w=160, h=90, frames=4, seed=3):
    """ruido de tele (cambio de canal)"""
    rnd = random.Random(seed)
    out = []
    for _ in range(frames):
        im = Image.new('RGBA', (w, h))
        px = im.load()
        for y in range(h):
            band = rnd.random() < 0.08
            for x in range(w):
                v = rnd.randint(0, 255) if not band else rnd.randint(150, 255)
                px[x, y] = (v, v, v, 255)
        out.append(im)
    return out

def rebobinar(w=44, h=24):
    """icono de rebobinar (dos triangulos hacia la izquierda) con borde negro"""
    im = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(im)
    for ox in (2, 22):
        d.polygon([(ox, h // 2), (ox + 19, 1), (ox + 19, h - 2)], fill=rgba('#ffffff'), outline=rgba('#0a0a0f'))
    return im

def fila(w=360, h=16):
    """barra para resaltar la fila elegida de una lista"""
    im = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 0, w - 1, h - 1), fill=rgba('#ffffff'))
    return im

def flecha(s=16, izquierda=True):
    im = Image.new('RGBA', (s, s))
    d = ImageDraw.Draw(im)
    pts = [(s - 3, 1), (2, s // 2), (s - 3, s - 2)] if izquierda else [(2, 1), (s - 3, s // 2), (2, s - 2)]
    d.polygon(pts, fill=rgba('#ffffff'), outline=rgba('#0a0a0f'))
    return im
