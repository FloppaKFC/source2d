#!/usr/bin/env python3
"""Simulador del plataformero de Source 2D (copia en Python de applyMovement + physics de src/runtime.js para un
personaje con el comportamiento plataformero) y buscador de rutas.

Sirve para demostrar que un nivel se puede completar: busca entradas (izquierda/derecha, salto, dash) que llevan de
un punto de control al siguiente sin tocar pinchos ni sierras, y las encadena hasta la meta. Las entradas se pueden
reproducir en el jugador real con --fixed-step (cada fotograma dura lo mismo que aqui), asi la prueba es la del motor.

Si cambias la fisica del plataformero en runtime.js, cambia tambien step() aqui."""
import heapq, math

GRAVITY, MAX_FALL = 1900.0, 2400.0


def frame_dts(n):
    """dt de cada fotograma con --fixed-step: el primero 0.016 y luego (t - t_anterior) / 1000 con t = 1000/60*k"""
    out, last = [], 0.0
    for k in range(1, n + 1):
        t = 1000.0 / 60.0 * k
        out.append(0.016 if last == 0 else min(0.05, (t - last) / 1000.0))
        last = t
    return out


DTS = frame_dts(60 * 60 * 20)


def overlap(ax, ay, aw, ah, bx, by, bw, bh):
    return abs(ax - bx) * 2 < aw + bw and abs(ay - by) * 2 < ah + bh


class World:
    """grid: filas de texto ('#' solido, '^v<>' pinchos); saws: dicts con x, y (base), axis, amplitude, period, phase,
    w, h (caja de colision). Las colisiones van contra la rejilla: el resultado es el de las cajas por filas del
    motor (al chocar se queda en el borde del primer tile solido que toca)."""

    def __init__(self, grid, saws, T=32):
        self.g, self.saws, self.T = grid, saws, T
        self.H, self.W = len(grid), len(grid[0])

    def solid(self, c, r):
        return 0 <= r < self.H and 0 <= c < self.W and self.g[r][c] == '#'

    def cols_rows(self, x, y, w, h):
        T = self.T
        c0, c1 = int(math.floor((x - w / 2) / T)), int(math.floor((x + w / 2) / T))
        r0, r1 = int(math.floor((y - h / 2) / T)), int(math.floor((y + h / 2) / T))
        return c0, c1, r0, r1

    def hit_cols(self, x, y, w, h):
        """columnas de tiles solidos que se solapan (solape estricto, como boxesOverlap)"""
        T = self.T
        c0, c1, r0, r1 = self.cols_rows(x, y, w, h)
        out = []
        for c in range(c0, c1 + 1):
            if not (x - w / 2 < (c + 1) * T and x + w / 2 > c * T):
                continue
            for r in range(r0, r1 + 1):
                if y - h / 2 < (r + 1) * T and y + h / 2 > r * T and self.solid(c, r):
                    out.append(c)
                    break
        return out

    def hit_rows(self, x, y, w, h):
        T = self.T
        c0, c1, r0, r1 = self.cols_rows(x, y, w, h)
        out = []
        for r in range(r0, r1 + 1):
            if not (y - h / 2 < (r + 1) * T and y + h / 2 > r * T):
                continue
            for c in range(c0, c1 + 1):
                if x - w / 2 < (c + 1) * T and x + w / 2 > c * T and self.solid(c, r):
                    out.append(r)
                    break
        return out

    def saw_pos(self, s, t):
        off = s['amplitude'] * math.sin(2 * math.pi * (t / max(0.05, s['period']) + s['phase'] / 360.0))
        return (s['x'] + off, s['y']) if s['axis'] == 'x' else (s['x'], s['y'] + off)

    def deadly(self, x, y, w, h, t):
        T = self.T
        c0, c1, r0, r1 = self.cols_rows(x, y, w + T, h + T)
        for r in range(max(0, r0), min(self.H, r1 + 1)):
            row = self.g[r]
            for c in range(max(0, c0), min(self.W, c1 + 1)):
                if row[c] in '^v<>' and overlap(x, y, w, h, (c + 0.5) * T, (r + 0.5) * T, T * 0.7, T * 0.7):
                    return True
        for s in self.saws:
            sx, sy = self.saw_pos(s, t)
            if abs(sx - x) < 80 and abs(sy - y) < 80 and overlap(x, y, w, h, sx, sy, s['w'], s['h']):
                return True
        return y > self.H * T


class P:
    """parametros del plataformero (como los compila compileObj)"""
    def __init__(self, **k):
        self.speed = k.get('speed', 250.0); self.jump = k.get('jump', 620.0)
        self.airJumps = k.get('airJumps', 1); self.airJump = k.get('airJump', 0.0)
        self.dash = k.get('dash', True); self.dashSpeed = k.get('dashSpeed', 720.0); self.dashTime = k.get('dashTime', 0.16)
        self.dashCooldown = k.get('dashCooldown', 0.35); self.jumpCut = k.get('jumpCut', True)
        self.coyote = k.get('coyote', 0.0); self.jumpBuffer = k.get('jumpBuffer', 0.0); self.maxFall = k.get('maxFall', 0.0)
        self.w = k.get('w', 18.0); self.h = k.get('h', 26.0)
        self.wall = k.get('wall', False); self.wallJump = k.get('wallJump', False)


# estado: tupla inmutable para poder guardarlo en la busqueda
# (frame, t, x, y, vx, vy, grounded, coyoteT, jumpBufT, airLeft, dashReady, dashCD, dashT, dashDir, faceX, held,
#  wallL, wallR, wallLockT)
def start_state(x, y):
    return (0, 0.0, x, y, 0.0, 0.0, False, 0.0, 0.0, None, True, 0.0, 0.0, 1, 1, False, False, False, 0.0)


def near_wall(world, x, y, p, d):
    """como nearWall de runtime.js: pared solida a 3 px por el lado d, a media altura"""
    return bool(world.hit_cols(x + d * 3, y, p.w, p.h * 0.6))


def step(st, inp, p, world):
    """un fotograma. inp = (izq, der, salto_pulsado, salto_mantenido, dash). Devuelve el estado nuevo o None si muere"""
    frame, t, x, y, vx, vy, grounded, coy, buf, air, ready, dcd, dt_, ddir, face, held, wl, wr, lock = st
    dt = DTS[frame]
    frame += 1; t += dt
    L, R, jp, jh, dp = inp
    jh = jh or jp
    # ---- applyMovement ----
    if grounded:
        coy = p.coyote; air = p.airJumps; ready = True
    else:
        coy = max(0.0, coy - dt)
    buf = p.jumpBuffer if jp else max(0.0, buf - dt)
    if dcd > 0:
        dcd -= dt
    dashing_now = False
    if p.dash and not (dt_ > 0) and not (dcd > 0) and ready is not False and dp:
        dt_ = p.dashTime
        ddir = 1 if (R and not L) else -1 if (L and not R) else (face or 1)
        face = ddir; dcd = p.dashCooldown + p.dashTime
        if not grounded:
            ready = False
        held = False
    if dt_ > 0:
        dt_ -= dt
        vx = ddir * p.dashSpeed; vy = 0.0
        if dt_ <= 0:
            vx = ddir * p.speed
        dashing_now = True
    else:
        if lock > 0:
            lock -= dt
        elif L and not R:
            vx = -p.speed; face = -1
        elif R and not L:
            vx = p.speed; face = 1
        else:
            vx *= 0.72 if grounded else 0.94
            if abs(vx) < 3:
                vx = 0.0
        on_ground = grounded or coy > 0
        if (jp or buf > 0) and on_ground:
            vy = -p.jump; grounded = False; coy = 0.0; buf = 0.0; held = True
        elif jp and p.wall and p.wallJump and (near_wall(world, x, y, p, -1) or near_wall(world, x, y, p, 1)):
            away = 1 if near_wall(world, x, y, p, -1) else -1
            vy = -p.jump * 0.92; vx = away * p.speed * 1.15; face = away
            lock = 0.16; air = p.airJumps; ready = True; buf = 0.0; held = True
        elif jp and air is not None and air > 0:
            vy = -(p.airJump or p.jump); air -= 1; buf = 0.0; held = True
        if held and (vy >= 0 or grounded):
            held = False
        if p.jumpCut and held and not jh:
            vy *= 0.45; held = False
        if p.wall and not grounded and (wl or wr) and (L or R) and vy > 0:
            vy = min(vy, 90.0)
    # ---- physics ----
    if not (dt_ > 0):
        vy = max(-MAX_FALL, min(p.maxFall if p.maxFall > 0 else MAX_FALL, vy + GRAVITY * dt))
    hw, hh = p.w / 2, p.h / 2
    grounded = False
    wl = wr = False
    T = world.T
    x += vx * dt
    hc = world.hit_cols(x, y, p.w, p.h)
    if hc:
        if vx > 0:
            wr = True
        elif vx < 0:
            wl = True
        if vx > 0:
            x = min(hc) * T - hw
        elif vx < 0:
            x = (max(hc) + 1) * T + hw
        vx = 0.0
    y += vy * dt
    hr = world.hit_rows(x, y, p.w, p.h)
    if hr:
        if vy > 0:
            y = min(hr) * T - hh; vy = 0.0; grounded = True
        elif vy < 0:
            y = (max(hr) + 1) * T + hh; vy = 0.0
    # las condiciones de muerte se miran en los eventos del fotograma siguiente: sierras en su posicion siguiente
    tn = t + DTS[frame]
    if world.deadly(x, y, p.w, p.h, tn):
        return None
    return (frame, t, x, y, vx, vy, grounded, coy, buf, air, ready, dcd, dt_, ddir, face, held, wl, wr, lock)


# acciones de la busqueda: (direccion, salto: 0 suelto / 1 pulsar y mantener / 2 mantener, dash)
ACTIONS = [(d, j, k) for d in (-1, 0, 1) for j in (0, 1, 2) for k in (0, 1)]


def frames_of(action, n):
    d, j, k = action
    L, R = d < 0, d > 0
    out = []
    for i in range(n):
        jp = j == 1 and i == 0
        jh = j in (1, 2)
        out.append((L, R, jp, jh, k == 1 and i == 0))
    return out


def solve(st, goal, p, world, macro=4, max_nodes=250000, period=None, weight=2.5):
    """busqueda A* ponderada con macroacciones de 'macro' fotogramas hasta tocar la caja goal (x, y, w, h).
    Devuelve (estado_final, lista_de_entradas_por_fotograma) o (None, None)"""
    gx, gy = goal[0], goal[1]
    vmax = max(p.speed, p.dashSpeed)

    def h(s):
        return math.hypot(s[2] - gx, s[3] - gy) / vmax * 60.0

    def key(s):
        k = (int(s[2] // 6), int(s[3] // 6), int(s[5] // 100), s[6], s[9], s[10], s[12] > 0, s[15], s[11] > 0, s[18] > 0)
        if period:
            k += (int((s[1] % period) / period * 24),)
        return k

    heap = [(h(st), 0, 0)]
    nodes, parent = [st], [(None, None)]
    seen = {key(st)}
    cnt = 0
    while heap and cnt < max_nodes:
        _, g, idx = heapq.heappop(heap)
        s = nodes[idx]
        cnt += 1
        can_dash = p.dash and not (s[12] > 0) and not (s[11] > 0) and s[10] is not False
        can_jump = s[6] or s[7] > 0 or (s[9] is not None and s[9] > 0) or \
            (p.wall and p.wallJump and (near_wall(world, s[2], s[3], p, -1) or near_wall(world, s[2], s[3], p, 1)))
        for a in ACTIONS:
            if a[1] == 2 and not s[15]:
                continue
            if a[1] == 1 and not can_jump:
                continue
            if a[2] == 1 and not can_dash:
                continue
            cur, ins, ok = s, frames_of(a, macro), True
            done = False
            for n_, inp in enumerate(ins):
                cur = step(cur, inp, p, world)
                if cur is None:
                    ok = False
                    break
                if overlap(cur[2], cur[3], p.w, p.h, *goal):
                    ins = ins[:n_ + 1]
                    done = True
                    break
            if not ok:
                continue
            nodes.append(cur)
            parent.append((idx, ins))
            nid = len(nodes) - 1
            if done:
                path, i = [], nid
                while parent[i][0] is not None:
                    path = parent[i][1] + path
                    i = parent[i][0]
                return cur, path
            kk = key(cur)
            if kk in seen:
                continue
            seen.add(kk)
            heapq.heappush(heap, (g + macro + h(cur) * weight, g + macro, nid))
    return None, None
