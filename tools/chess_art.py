"""Arte del ajedrez de ejemplo (lo usa tools/make_chess.py): piezas de 16x16, casillas del tablero con
veta de madera, marcas (jugadas, seleccion, jaque) y colores lisos para paneles. Todo dibujado aqui
con texto, sin imagenes externas. Con "python3 tools/chess_art.py salida.png" guarda una vista previa."""
import math

# o = contorno, w = color base, s = sombra, l = brillo
PIECES = {
    'P': """
................
................
................
................
......oooo......
.....olwwso.....
.....owwwso.....
......owso......
.....olwwso.....
......owso......
......owso......
.....olwwso.....
....olwwwwso....
...oooooooooo...
...olwwwwwwso...
...oooooooooo...""",
    'N': """
................
.......o.o......
......olwlo.....
.....olwwwoo....
....olwwwwwso...
...olwowwwwso...
..olwwwwwwwwso..
..owwwwoowwwso..
...oooo.olwwso..
.......olwwwso..
......olwwwwso..
......owwwwwso..
.....olwwwwwwso.
....oooooooooooo
....olwwwwwwwwso
....oooooooooooo""",
    'B': """
.......oo.......
......olso......
.......oo.......
......olwo......
.....olwwso.....
....olwwowso....
....olwowwso....
....olwwwwso....
.....owwwso.....
......owso......
.....olwwso.....
.....olwwso.....
....olwwwwso....
...oooooooooo...
...olwwwwwwso...
...oooooooooo...""",
    'R': """
................
................
...ooo.oo.ooo...
...olo.oo.oso...
...olooooooso...
...olwwwwwwso...
....oooooooo....
....olwwwwso....
....olwwwwso....
....olwwwwso....
....olwwwwso....
....olwwwwso....
...oooooooooo...
...olwwwwwwso...
..oooooooooooo..
..olwwwwwwwwso..""",
    'Q': """
................
..o..o.oo.o..o..
.olo.olwwlo.olo.
..ow.owwwwo.wo..
..owowwwwwwowo..
..owwwwwwwwwso..
...olwwwwwwso...
....oooooooo....
.....olwwso.....
.....olwwso.....
.....olwwso.....
....olwwwwso....
...oooooooooo...
...olwwwwwwso...
..oooooooooooo..
..olwwwwwwwwso..""",
    'K': """
......oooo......
....ooolsooo....
....olwwwwso....
....ooolsooo....
..ooo.olso.ooo..
.olwwooloowwwso.
.olwwwwolwwwwso.
.olwwwwolwwwwso.
..olwwwolwwwso..
...olwwwwwwso...
....oooooooo....
....olwwwwso....
...olwwwwwwso...
...oooooooooo...
..olwwwwwwwwso..
..oooooooooooo..""",
}
ORDER = 'PNBRQK'   # tipo 1..6

PALETTE = {
    'w': {'o': '#1c1a20', 'w': '#f2e8d4', 's': '#c4b393', 'l': '#ffffff'},
    'b': {'o': '#0b0a0e', 'w': '#45414f', 's': '#2a2731', 'l': '#76707f'},
}


def piece_pixels(kind, side):
    rows = [r for r in PIECES[kind].strip('\n').split('\n')]
    assert len(rows) == 16 and all(len(r) == 16 for r in rows), kind
    pal = PALETTE[side]
    return [pal[ch] if ch in pal else '' for r in rows for ch in r]


def piece_frames():
    """12 fotogramas: blancas P N B R Q K y luego negras (fotograma = color*6 + tipo-1)."""
    return [{'pixels': piece_pixels(k, s)} for s in 'wb' for k in ORDER]


def _rnd(seed):
    x = seed & 0xffffffff
    while True:
        x = (x * 1103515245 + 12345) & 0x7fffffff
        yield x / 0x7fffffff


def _hex(rgb):
    return '#%02x%02x%02x' % tuple(max(0, min(255, int(round(c)))) for c in rgb)


def _parse(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def wood_tile(base, seed, grain=0.06, speck=0.035):
    """casilla de 16x16 con veta horizontal suave y motas; el borde inferior y el derecho, un poco mas oscuros"""
    r = _rnd(seed)
    b = _parse(base)
    phase = next(r) * 6.28
    px = []
    for y in range(16):
        for x in range(16):
            g = math.sin(y * 0.9 + math.sin(x * 0.35 + phase) * 1.4 + phase) * grain
            g += (next(r) - 0.5) * speck
            if y == 15 or x == 15:
                g -= 0.035
            if y == 0 or x == 0:
                g += 0.02
            px.append(_hex(tuple(c * (1 + g) for c in b)))
    return {'pixels': px, 'solid': False}


def board_tiles():
    """0-1 casillas claras, 2-3 oscuras (dos variantes para que no se repita la veta), 4-5 claras y oscuras del menu"""
    return [wood_tile('#ecd9b6', 11), wood_tile('#e9d5b0', 23), wood_tile('#b4825c', 37), wood_tile('#ae7c56', 41),
            wood_tile('#2b2f3b', 53, 0.05, 0.03), wood_tile('#22252f', 67, 0.05, 0.03)]


def _disc(cx, cy, r0, r1, color):
    px = []
    for y in range(16):
        for x in range(16):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            px.append(color if r0 <= d <= r1 else '')
    return px


def mark_frames():
    """0 punto de jugada, 1 anillo de captura, 2 seleccion, 3 ultima jugada, 4 jaque, 5 cursor"""
    dot = _disc(8, 8, 0, 3.1, '#101318')
    ring = _disc(8, 8, 6.2, 8.2, '#101318')
    sel = ['#f7ec5a'] * 256
    last = ['#d8c24a'] * 256
    check = []
    for y in range(16):
        for x in range(16):
            d = math.hypot(x + 0.5 - 8, y + 0.5 - 8) / 8
            check.append('' if d > 1.05 else _hex(tuple(a + (b - a) * min(1, d) for a, b in zip((255, 90, 70), (170, 20, 25)))))
    cur = ['#ffffff' if x in (0, 15) or y in (0, 15) else '' for y in range(16) for x in range(16)]
    return [{'pixels': f} for f in (dot, ring, sel, last, check, cur)]


def promo_tile():
    """ficha clara con esquinas redondeadas y borde, debajo de cada pieza del selector de coronacion"""
    px = []
    for y in range(16):
        for x in range(16):
            cx, cy = min(x, 15 - x), min(y, 15 - y)
            if cx + cy < 2:
                px.append('')                     # esquina recortada
            elif cx == 0 or cy == 0 or cx + cy == 2:
                px.append('#9c8a6a')              # borde
            else:
                px.append('#f4ecdc' if y < 9 else '#e6dcc6')
    return [{'pixels': px}]


def solid(color, size=2):
    return [{'pixels': [color] * (size * size)}]


def preview(path):
    from PIL import Image
    frames = piece_frames()
    tiles = board_tiles()
    S = 8
    img = Image.new('RGB', (16 * 6 * S, 16 * 4 * S), (40, 40, 48))
    for i in range(12):
        side = i // 6
        for k, t in enumerate((tiles[(i + side) % 2 * 2], tiles[((i + side + 1) % 2) * 2])):
            pass
        bg = tiles[0] if (i + side) % 2 == 0 else tiles[2]
        ox, oy = (i % 6) * 16 * S, side * 16 * S
        for y in range(16):
            for x in range(16):
                c = frames[i]['pixels'][y * 16 + x] or bg['pixels'][y * 16 + x]
                for dy in range(S):
                    for dx in range(S):
                        img.putpixel((ox + x * S + dx, oy + y * S + dy), _parse(c))
    marks = mark_frames()
    for i, m in enumerate(marks):
        ox, oy = i * 16 * S, 2 * 16 * S
        for y in range(16):
            for x in range(16):
                c = m['pixels'][y * 16 + x] or tiles[0]['pixels'][y * 16 + x]
                for dy in range(S):
                    for dx in range(S):
                        img.putpixel((ox + x * S + dx, oy + y * S + dy), _parse(c))
    for i, t in enumerate(tiles):
        ox, oy = i * 16 * S, 3 * 16 * S
        for y in range(16):
            for x in range(16):
                for dy in range(S):
                    for dx in range(S):
                        img.putpixel((ox + x * S + dx, oy + y * S + dy), _parse(t['pixels'][y * 16 + x]))
    img.save(path)


if __name__ == '__main__':
    import sys
    preview(sys.argv[1] if len(sys.argv) > 1 else 'ajedrez_arte.png')
