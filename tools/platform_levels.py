#!/usr/bin/env python3
"""Niveles de Platform Test (el Limbo): tres senderos, cada uno mas largo y dificil que el anterior.

  1. "La orilla"        solo doble salto. Corto y para aprender.
  2. "El puente roto"   el doble de dificil; a mitad, el santuario del dash (desde ahi hay fosos que lo piden).
  3. "La torre"         mucho mas dificil y largo; al principio, el santuario de la pared: pozos que se suben
                        saltando de pared en pared, con dash y doble salto encadenados.

Rejilla de tiles de 32 px: '#' roca, '^' 'v' '<' '>' pinchos, '.' vacio. Ademas: inicio, puntos de control (faroles),
sierras, santuarios (habilidad que dan y donde estan), recuerdos (dialogos) y meta.
tools/solve_platform.py demuestra que cada nivel se completa con la fisica de tools/platform_sim.py (la del motor)."""

T = 32
H = 40
PLAYER = dict(speed=250.0, jump=620.0, airJumps=1, airJump=560.0, dash=True, dashSpeed=720.0, dashTime=0.16,
              dashCooldown=0.3, jumpCut=True, coyote=0.08, jumpBuffer=0.1, maxFall=820.0, w=18.0, h=26.0,
              wall=True, wallJump=True)
SAW_SIZE, SAW_HIT = 48, 34


class Level:
    def __init__(self, num, name, w):
        self.num, self.name, self.W = num, name, w
        self.g = [['.'] * w for _ in range(H)]
        self.saws, self.checkpoints, self.shrines, self.memories = [], [], [], []
        self.start = self.goal = None
        # techo, suelo y abismo de pinchos en todo el mapa
        self.rect(0, 0, w - 1, 1)
        self.rect(0, H - 2, w - 1, H - 1)
        self.row(0, w - 1, H - 3, '^')

    def rect(self, x0, y0, x1, y1, c='#'):
        for y in range(max(0, y0), min(H, y1 + 1)):
            for x in range(max(0, x0), min(self.W, x1 + 1)):
                self.g[y][x] = c

    def col(self, x, y0, y1, c='#'):
        self.rect(x, y0, x, y1, c)

    def row(self, x0, x1, y, c='#'):
        self.rect(x0, y, x1, y, c)

    def ground(self, x0, x1, top):
        """columna de roca desde 'top' hasta el abismo"""
        self.rect(x0, top, x1, H - 3)

    def saw(self, tx, ty, axis='y', amp=0.0, period=2.0, phase=0.0):
        self.saws.append(dict(x=(tx + 0.5) * T, y=(ty + 0.5) * T, axis=axis, amplitude=amp * T, period=period, phase=phase))

    def cp(self, tx, ty):
        """farol de pie sobre el suelo del tile (tx, ty + 1)"""
        self.checkpoints.append(((tx + 0.5) * T, (ty + 1) * T - 24))

    def shrine(self, tx, ty, ability):
        """santuario que da una habilidad ('dash' o 'wall'); tambien es punto de control"""
        self.cp(tx, ty)
        self.shrines.append(dict(x=(tx + 0.5) * T, y=(ty + 1) * T - 32, ability=ability, cp=len(self.checkpoints) - 1))

    def memory(self, tx, ty, key):
        """recuerdo flotante: al tocarlo, un dialogo (key)"""
        self.memories.append(dict(x=(tx + 0.5) * T, y=(ty + 0.5) * T, key=key))

    def danger_count(self):
        return sum(1 for r in self.g for c in r if c in '^v<>') - self.W


def level1():
    L = Level(1, 'La orilla', 150)
    g = 30
    L.ground(0, 14, g)
    L.rect(0, 2, 0, g)
    L.start = ((3 + 0.5) * T, g * T - 13)
    L.memory(10, g - 3, 'despertar')
    L.ground(19, 26, g)                       # foso de 4: salto
    L.ground(27, 29, g - 2)                   # escalones
    L.ground(30, 33, g - 4)
    L.ground(40, 47, g - 4)                   # foso de 6: doble salto
    L.cp(44, g - 5)
    L.ground(48, 52, g - 1)
    L.rect(53, g - 5, 55, H - 3)              # muro de 5: doble salto para subirlo
    L.ground(56, 62, g - 5)
    L.ground(68, 70, g - 4)                   # islas con pinchos abajo
    L.ground(75, 76, g - 6)
    L.ground(81, 88, g - 5)
    L.cp(85, g - 6)
    L.memory(86, g - 9, 'nombre')
    L.ground(94, 100, g - 5)                  # sierra lenta sobre un foso
    L.saw(97, g - 9, 'y', 1.5, 2.6, 0)
    L.ground(106, 112, g - 3)
    L.cp(110, g - 4)
    L.rect(106, 2, 125, g - 9)                # techo bajo: saltos cortos
    L.row(113, 125, g - 8, 'v')
    L.ground(117, 119, g - 3)
    L.ground(124, 149, g - 2)
    L.rect(149, 2, 149, g - 2)
    L.goal = ((143 + 0.5) * T, (g - 2) * T - 32)
    return L


def level2():
    L = Level(2, 'El puente roto', 240)
    g = 30
    L.ground(0, 10, g)
    L.rect(0, 2, 0, g)
    L.start = ((3 + 0.5) * T, g * T - 13)
    L.memory(8, g - 3, 'puente')
    # ---- primera mitad: doble salto con mas peligro ----
    L.ground(17, 18, g - 1)                   # islas estrechas
    L.ground(24, 24, g - 3)
    L.ground(30, 31, g - 2)
    L.ground(37, 44, g - 3)
    L.cp(41, g - 4)
    L.rect(45, 2, 70, g - 10)                 # techo de pinchos bajo: saltos medidos
    L.row(45, 70, g - 9, 'v')
    L.ground(49, 50, g - 3)
    L.saw(54, g - 6, 'y', 2.0, 2.0, 0)
    L.ground(57, 58, g - 3)
    L.saw(61, g - 6, 'y', 2.0, 2.0, 180)
    L.ground(64, 74, g - 3)
    L.cp(70, g - 4)
    L.rect(75, g - 8, 76, H - 3)              # muro de 5 con pinchos encima de la entrada
    L.ground(77, 81, g - 8)
    L.ground(87, 87, g - 6)
    L.ground(92, 92, g - 8)
    L.saw(95, g - 12, 'x', 1.5, 1.8, 90)
    L.ground(99, 112, g - 8)
    # ---- santuario del dash ----
    L.shrine(106, g - 9, 'dash')
    L.memory(103, g - 12, 'dash')
    # ---- segunda mitad: fosos que piden dash ----
    L.ground(124, 128, g - 8)                 # foso de 11: doble salto + dash
    L.rect(129, 2, 160, g - 14)               # tunel bajo con techo de pinchos: fosos que se cruzan con salto corto + dash
    L.row(129, 160, g - 13, 'v')
    for x0, x1 in ((129, 131), (139, 141), (149, 151)):
        L.ground(x0, x1, g - 8)
    L.ground(158, 166, g - 8)
    L.cp(140, g - 9)
    L.cp(164, g - 9)
    L.ground(177, 178, g - 7)                 # fosos largos con sierras
    L.saw(172, g - 10, 'y', 2.0, 1.6, 0)
    L.ground(190, 191, g - 9)
    L.saw(184, g - 13, 'x', 2.0, 1.7, 0)
    L.ground(201, 206, g - 8)
    L.cp(204, g - 9)
    L.rect(207, 2, 239, g - 15)
    L.row(207, 232, g - 14, 'v')
    L.ground(216, 216, g - 9)
    L.saw(221, g - 11, 'y', 1.5, 1.2, 0)
    L.ground(227, 239, g - 8)
    L.rect(239, 2, 239, g - 8)
    L.goal = ((234 + 0.5) * T, (g - 8) * T - 32)
    return L


class Rooms:
    """constructor por salas: un cursor (x) y la altura del suelo (fila de su superficie). Cada sala empieza en el
    cursor sobre el suelo actual y deja el cursor y el suelo donde sigue el camino"""

    def __init__(self, L, x, floor):
        self.L, self.x, self.f = L, x, floor

    def flat(self, n, cp=False, **extra):
        self.L.ground(self.x, self.x + n - 1, self.f)
        if cp:
            self.L.cp(self.x + n // 2, self.f - 1)
        self.x += n

    def gap(self, n, dy=0, land=3, saw=None, roof=0):
        """hueco de n tiles; el otro lado dy filas mas alto (negativo = mas bajo); saw = (amp, periodo, fase, eje);
        roof: techo de pinchos a esas filas por encima del suelo mas alto (limita la altura del salto)"""
        if roof:
            self.ceiling(self.x - 1, self.x + n + land, min(self.f, self.f - dy) - roof)
        if saw:
            amp, per, ph, ax = saw
            self.L.saw(self.x + n / 2 - 0.5, self.f - 3 + (0 if ax == 'y' else -1), ax, amp, per, ph)
        self.x += n
        self.f -= dy
        self.flat(land)

    def ceiling(self, x0, x1, row, spikes=True):
        """techo de roca desde arriba hasta 'row' (con pinchos debajo)"""
        self.L.rect(x0, 2, x1, row - (1 if spikes else 0))
        if spikes:
            self.L.row(x0, x1, row, 'v')

    def shaft(self, h, w=3, spikes=(), saws=(), exit_len=6, cp=True):
        """pozo de h filas entre dos paredes (se entra por abajo a la izquierda y se sale por arriba a la derecha).
        spikes: (lado 'L'|'R', fila desde el suelo, largo); saws: (fila desde el suelo, amplitud, periodo, fase)"""
        L, x, f = self.L, self.x, self.f
        top = f - h
        L.rect(x, top, x + 1, f - 4)                       # pared izquierda: abajo queda la entrada (3 filas)
        L.ground(x, x + 1 + w, f)                          # suelo del pozo
        L.rect(x + 2 + w, top + 3, x + 3 + w, H - 3)       # pared derecha, mas baja: por encima se sale
        for side, r, n in spikes:
            c, ch = (x + 2, '>') if side == 'L' else (x + 1 + w, '<')
            L.col(c, f - r - n + 1, f - r, ch)
        for r, amp, per, ph in saws:
            L.saw(x + 2 + (w - 1) / 2, f - r, 'x', amp, per, ph)
        self.ceiling(x, x + 3 + w + exit_len, top - 2, spikes=False)
        self.x = x + 2 + w
        self.f = top + 3
        self.flat(exit_len + 2, cp=cp)

    def wall(self, h, top_len=6, spikes_right=0, cp=True):
        """pared sola de h filas (sin otra enfrente): hay que trepar saltando contra ella"""
        L, x, f = self.L, self.x, self.f
        self.flat(2)
        top = f - h
        L.ground(self.x, self.x + top_len - 1, top)
        self.ceiling(self.x - 3, self.x + top_len, top - 5, spikes=False)
        if spikes_right:
            L.col(self.x - 3 + 0, top - 4, top - 4 + 0, '.')
        self.x += top_len
        self.f = top
        if cp:
            L.cp(self.x - top_len // 2, top - 1)

    def tunnel(self, n, gap_below=True):
        """tunel de pinchos arriba y abajo: solo se cruza con dash (y un salto bajo)"""
        L, x, f = self.L, self.x, self.f
        self.ceiling(x, x + n - 1, f - 3)
        L.ground(x, x + n - 1, f + 1)
        L.row(x, x + n - 1, f, '^')
        self.x += n
        self.flat(3)

    def drop(self, dy, n=4):
        """bajada: un hueco hacia un suelo dy filas mas abajo"""
        self.x += n
        self.f += dy
        self.flat(3)


def level3():
    L = Level(3, 'La torre', 400)
    g = 33
    L.rect(0, 2, 0, g)
    L.start = ((3 + 0.5) * T, g * T - 13)
    R = Rooms(L, 0, g)
    R.flat(12)
    L.memory(8, g - 3, 'torre')
    R.gap(5, 0, 2, saw=(1.5, 1.1, 0, 'y'), roof=6)
    R.gap(5, 1, 8, saw=(1.5, 1.1, 180, 'y'), roof=6)
    L.shrine(R.x - 4, R.f - 1, 'wall')                       # santuario de la pared
    L.memory(R.x - 7, R.f - 4, 'promesa')
    R.shaft(10, exit_len=4)                                  # pozo para aprender
    R.wall(9, top_len=4)                                     # pared sola
    R.gap(9, -2, 2, saw=(2.0, 1.0, 0, 'y'), roof=5)          # dash + doble salto bajo techo de pinchos
    R.gap(6, 0, 3, saw=(1.5, 0.9, 90, 'x'), roof=5)
    R.flat(1, cp=True)
    R.drop(8)
    R.shaft(13, spikes=(('L', 3, 3), ('R', 6, 3), ('L', 9, 3), ('R', 11, 2)), saws=((5, 0.6, 1.0, 0), (9, 0.6, 1.0, 180)))
    R.tunnel(9)
    R.gap(6, 1, 1, saw=(1.5, 0.9, 90, 'x'), roof=5)
    R.gap(5, 0, 3, roof=5)
    R.flat(1, cp=True)
    R.drop(9)
    R.gap(5, 0, 1, saw=(2.0, 0.8, 0, 'y'), roof=6)
    R.gap(5, 1, 1, saw=(2.0, 0.8, 180, 'y'), roof=6)
    R.gap(5, 0, 1, saw=(2.0, 0.8, 90, 'y'), roof=6)
    R.gap(5, -1, 3, roof=6)
    R.flat(1, cp=True)
    R.shaft(9, w=3, spikes=(('L', 2, 2), ('R', 5, 2)), exit_len=2, cp=False)
    R.shaft(9, w=3, spikes=(('R', 2, 2), ('L', 5, 2)), exit_len=2)
    R.shaft(8, w=3, spikes=(('L', 2, 2), ('R', 4, 3)), saws=((6, 0.7, 0.9, 90),))
    R.drop(11)
    R.gap(4, 0, 1, saw=(1.5, 0.7, 0, 'y'), roof=5)
    R.gap(4, 1, 1, saw=(1.2, 0.7, 90, 'x'), roof=5)
    R.gap(4, -1, 1, saw=(1.5, 0.7, 180, 'y'), roof=5)
    R.gap(5, 0, 3, roof=5)
    R.flat(1, cp=True)
    R.tunnel(8)
    R.wall(8, top_len=2, cp=False)
    R.gap(8, -1, 1, saw=(1.5, 0.8, 0, 'y'), roof=5)
    R.gap(7, 0, 3, roof=5)
    R.flat(1, cp=True)
    R.drop(6)
    R.shaft(12, w=3, spikes=(('L', 2, 3), ('R', 5, 3), ('L', 8, 2)), saws=((10, 0.5, 0.8, 0),))
    R.gap(10, 0, 1, saw=(2.0, 0.7, 0, 'y'), roof=5)
    R.gap(5, -1, 1, roof=5)
    R.gap(9, 1, 3, saw=(1.5, 0.7, 90, 'x'), roof=5)
    R.flat(12)
    L.rect(R.x - 1, 2, R.x - 1, R.f)
    L.goal = ((R.x - 6 + 0.5) * T, R.f * T - 32)
    assert R.x <= L.W, R.x
    L.W = R.x                                                # el mapa acaba donde acaba el camino
    L.g = [row[:R.x] for row in L.g]
    return L


def build_all():
    return [level1(), level2(), level3()]


if __name__ == '__main__':
    import sys
    for L in build_all():
        if len(sys.argv) > 1 and str(L.num) not in sys.argv[1:]:
            continue
        print('== nivel', L.num, L.name, L.W, 'tiles')
        for r in L.g:
            print(''.join(r))
