#!/usr/bin/env python3
"""Demuestra que los niveles de Platform Test se pueden completar: busca con tools/platform_sim.py una ruta de un punto
de control al siguiente hasta la meta, con solo las habilidades que el jugador tiene en cada tramo (doble salto
siempre; el dash desde su santuario del nivel 2; la pared desde su santuario del nivel 3), y guarda las entradas
fotograma a fotograma de cada nivel (para reproducirlas en el jugador real).
Uso: solve_platform.py [salida.json] [niveles...]      p. ej.  solve_platform.py test/desktop/platform_test_ruta.json
La salida es {"1": [[izq, der, salto, mantener, dash], ...], "2": ..., "3": ...}."""
import json, sys, time
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import platform_levels as pl
import platform_sim as ps


def abilities(L, seg):
    """habilidades en el tramo 'seg' (0 = del inicio al primer farol)"""
    dash = L.num >= 3
    wall = False
    for s in L.shrines:
        if seg > s['cp']:
            if s['ability'] == 'dash':
                dash = True
            if s['ability'] == 'wall':
                wall = True
    return dict(pl.PLAYER, dash=dash, wall=wall, wallJump=wall)


def solve_level(L, max_nodes=400000):
    saws = [dict(s, w=pl.SAW_HIT, h=pl.SAW_HIT) for s in L.saws]
    world = ps.World([''.join(r) for r in L.g], saws, pl.T)
    st = ps.start_state(*L.start)
    p = ps.P(**abilities(L, 0))
    st = ps.step(st, (0, 0, 0, 0, 0), p, world)   # el primer fotograma no lleva entradas
    goals = [(x, y, 32, 48) for x, y in L.checkpoints] + [(L.goal[0], L.goal[1], 64, 64)]
    inputs = [[0, 0, 0, 0, 0]]
    for i, gbox in enumerate(goals):
        t0 = time.time()
        p = ps.P(**abilities(L, i))
        x0, x1 = min(st[2], gbox[0]) - 400, max(st[2], gbox[0]) + 400
        near = [sw for sw in saws if x0 <= sw['x'] <= x1]
        end, path = ps.solve(st, gbox, p, world, period=max(sw['period'] for sw in near) if near else None, max_nodes=max_nodes)
        name = 'meta' if i == len(goals) - 1 else 'farol %d' % (i + 1)
        if end is None:
            print('  nivel %d: SIN RUTA hasta %s (desde x=%.0f y=%.0f) %.1fs' % (L.num, name, st[2], st[3], time.time() - t0))
            return None
        print('  nivel %d: %-9s %5d fotogramas  %.1fs' % (L.num, name, len(path), time.time() - t0))
        inputs += path
        st = end
        if any(s['cp'] == i for s in L.shrines):
            # en el motor el santuario da la habilidad en los eventos del fotograma siguiente (despues del
            # movimiento): unos fotogramas sin tocar nada antes de seguir con ella
            for _ in range(4):
                st = ps.step(st, (0, 0, 0, 0, 0), p, world)
                inputs.append([0, 0, 0, 0, 0])
            if st is None:
                print('  nivel %d: muere esperando en el santuario' % L.num)
                return None
    print('  nivel %d: total %d fotogramas (%.1f s de juego)' % (L.num, len(inputs), len(inputs) / 60))
    return inputs


if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].endswith('.json') else None
    which = [a for a in sys.argv[1:] if a.isdigit()]
    res, ok = {}, True
    for L in pl.build_all():
        if which and str(L.num) not in which:
            continue
        r = solve_level(L)
        if r is None:
            ok = False
            continue
        res[str(L.num)] = [[int(a) for a in f] for f in r]
    if out and ok:
        json.dump(res, open(out, 'w'))
    sys.exit(0 if ok else 1)
