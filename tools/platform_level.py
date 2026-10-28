#!/usr/bin/env python3
"""Nivel de Platform Test: un sendero de pinchos y sierras al estilo del Sendero del Dolor de Hollow Knight.

El mapa se construye por tramos (cada uno un reto) sobre una rejilla de tiles de 32 px:
  '#' roca solida   '^' 'v' '<' '>' pinchos (hacia arriba, abajo, izquierda, derecha)   '.' vacio
Ademas: puntos de control, sierras (con su movimiento sinusoidal), salida y meta.
El fisico del jugador es el de tools/platform_sim.py (mismos valores que el objeto Jugador del juego)."""

T = 32
W, H = 300, 40

PLAYER = dict(speed=250.0, jump=620.0, airJumps=1, airJump=560.0, dash=True, dashSpeed=720.0, dashTime=0.16,
              dashCooldown=0.3, jumpCut=True, coyote=0.08, jumpBuffer=0.1, maxFall=820.0, w=18.0, h=26.0)
SAW_SIZE, SAW_HIT = 48, 34          # sprite de la sierra y su caja de colision (mas pequena: perdona las esquinas)


class Level:
    def __init__(self):
        self.g = [['.'] * W for _ in range(H)]
        self.saws, self.checkpoints = [], []
        self.start = self.goal = None

    def rect(self, x0, y0, x1, y1, c='#'):
        for y in range(max(0, y0), min(H, y1 + 1)):
            for x in range(max(0, x0), min(W, x1 + 1)):
                self.g[y][x] = c

    def col(self, x, y0, y1, c='#'):
        self.rect(x, y0, x, y1, c)

    def row(self, x0, x1, y, c='#'):
        self.rect(x0, y, x1, y, c)

    def saw(self, tx, ty, axis='y', amp=0.0, period=2.0, phase=0.0):
        """sierra centrada en el tile (tx, ty); amp en tiles"""
        self.saws.append(dict(x=(tx + 0.5) * T, y=(ty + 0.5) * T, axis=axis, amplitude=amp * T, period=period, phase=phase))

    def cp(self, tx, ty):
        """punto de control de pie sobre el suelo del tile (tx, ty+1)"""
        self.checkpoints.append(((tx + 0.5) * T, (ty + 1) * T - 24))

    # --- en pixeles ---
    def solids(self):
        out = []
        for r in range(H):
            start = -1
            for c in range(W + 1):
                s = c < W and self.g[r][c] == '#'
                if s and start < 0:
                    start = c
                elif not s and start >= 0:
                    n = c - start
                    out.append(((start + n / 2) * T, (r + 0.5) * T, n * T, T))
                    start = -1
        return out

    def danger(self):
        return [((c + 0.5) * T, (r + 0.5) * T, T * 0.7, T * 0.7) for r in range(H) for c in range(W) if self.g[r][c] in '^v<>']


def build():
    L = Level()
    # techo y suelo base en todo el mapa (el abismo de abajo es de pinchos)
    L.rect(0, 0, W - 1, 1)
    L.rect(0, H - 2, W - 1, H - 1)
    L.row(0, W - 1, H - 3, '^')

    # ---- 1. comienzo: saltos sencillos y el primer foso con doble salto ----
    g = 30
    L.rect(0, g, 14, H - 3)                  # suelo inicial
    L.rect(0, 2, 0, g)                       # pared izquierda
    L.start = ((3 + 0.5) * T, g * T - 13)
    L.rect(19, g, 24, H - 3)                 # foso de 4
    L.rect(32, g - 1, 37, H - 3)             # foso de 7: doble salto
    L.cp(35, g - 2)

    # ---- 2. foso largo con techo de pinchos: salto + doble salto + dash ----
    L.rect(32, 2, 60, 22)                    # techo bajo
    L.row(38, 52, 23, 'v')
    L.rect(49, g, 51, H - 3)                 # isla
    L.rect(60, g - 1, 66, H - 3)
    L.cp(63, g - 2)

    # ---- 3. sierras que suben y bajan sobre pinchos ----
    L.rect(66, 2, 100, 16)
    L.row(66, 100, 17, 'v')
    L.rect(72, g - 1, 73, H - 3)
    L.saw(76, g - 4, 'y', 3.0, 1.8, 0)
    L.rect(79, g - 1, 80, H - 3)
    L.saw(83, g - 4, 'y', 3.0, 1.8, 180)
    L.rect(86, g - 1, 87, H - 3)
    L.saw(90, g - 4, 'y', 3.0, 1.8, 90)
    L.rect(93, g - 1, 100, H - 3)
    L.cp(97, g - 2)

    # ---- 4. subida por un pozo con paredes de pinchos ----
    L.rect(101, 12, 101, 25)                 # pared izquierda del pozo (se entra por abajo)
    L.col(102, 12, 24, '>')
    L.rect(101, g - 1, 101, H - 3)
    L.rect(115, 10, 116, H - 3)              # pared derecha (por encima de ella se sale)
    L.col(114, 14, 26, '<')
    L.rect(101, 2, 140, 5)                   # techo
    L.rect(102, g - 1, 114, H - 3)           # suelo del pozo
    L.rect(106, 25, 108, 25)                 # repisas en zigzag (4 tiles de alto entre una y otra)
    L.rect(110, 21, 112, 21)
    L.rect(105, 17, 107, 17)
    L.rect(110, 13, 112, 13)
    L.rect(117, 10, 130, H - 3)              # salida a la derecha
    L.cp(124, 9)

    # ---- 5. plataformas pequenas en lo alto con sierras horizontales ----
    L.rect(131, 6, 160, 6)
    L.row(131, 160, 7, 'v')
    L.rect(135, 14, 136, 14)
    L.saw(140, 11, 'x', 2.0, 2.2, 0)
    L.rect(144, 13, 145, 13)
    L.saw(149, 11, 'x', 2.0, 2.2, 180)
    L.rect(153, 14, 154, 14)
    L.rect(159, 12, 170, H - 3)
    L.cp(165, 11)

    # ---- 6. bajada por un pozo con paredes de pinchos y una sierra; salida por un pasillo bajo ----
    L.rect(161, 2, 196, 8)                   # techo
    L.col(171, 16, 27, '>')                  # pinchos en la pared izquierda (el bloque del tramo 5)
    L.rect(179, 9, 196, 27)                  # bloque de la derecha
    L.col(178, 14, 27, '<')
    L.saw(175, 21, 'x', 1.8, 2.0, 0)
    L.rect(171, 31, 196, H - 3)              # suelo
    L.row(179, 189, 28, 'v')                 # pasillo bajo con techo de pinchos
    L.cp(194, 30)

    # ---- 7. tunel de pinchos alternos (dash) ----
    L.rect(197, 2, 226, 24)
    L.rect(197, 30, 226, H - 3)
    L.rect(197, 2, 226, 24)
    L.row(199, 202, 29, '^')
    L.row(206, 209, 25, 'v')
    L.row(212, 215, 29, '^')
    L.row(218, 221, 25, 'v')
    L.rect(227, 28, 236, H - 3)
    L.rect(227, 2, 236, 22)
    L.cp(233, 27)

    # ---- 8. tramo final: islas diminutas, sierras y la meta ----
    L.rect(237, 2, 299, 8)
    L.row(237, 290, 9, 'v')
    L.rect(242, 24, 242, 24)
    L.saw(246, 21, 'y', 2.5, 1.6, 0)
    L.rect(250, 22, 250, 22)
    L.saw(254, 18, 'x', 1.5, 1.4, 90)
    L.rect(258, 24, 259, 24)
    L.saw(262, 22, 'y', 3.0, 1.5, 270)
    L.rect(266, 21, 266, 21)
    L.rect(271, 24, 272, 24)
    L.rect(277, 26, 299, H - 3)
    L.rect(299, 2, 299, 26)
    L.goal = ((290 + 0.5) * T, 26 * T - 32)
    return L


if __name__ == '__main__':
    L = build()
    for r in L.g:
        print(''.join(r))
