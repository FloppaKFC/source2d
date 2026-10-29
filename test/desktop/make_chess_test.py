#!/usr/bin/env python3
"""Prueba del ajedrez de ejemplo: perft (numero de posiciones a N jugadas) en posiciones conocidas, que
comprueba el generador de jugadas hecho con eventos (enroques, al paso, coronaciones, clavadas), y el tiempo
que tarda el bot en cada nivel. Imprime "S2DTEST {...}" y detiene el juego.
Uso: make_chess_test.py <ajedrez.json> <salida.json> [rapido]"""
import json, sys
src, out = sys.argv[1:3]
quick = len(sys.argv) > 3
p = json.load(open(src))
# las funciones del motor son eventos de la escena Partida: la prueba empieza en ella
p['scenes'] = [x for x in p['scenes'] if x['name'] == 'Partida'] + [x for x in p['scenes'] if x['name'] != 'Partida']

FENS = {
    'inicio': ('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -', [20, 400, 8902]),
    'kiwipete': ('r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -', [48, 2039]),
    'alpaso': ('8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -', [14, 191, 2812]),
    'corona': ('r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -', [6, 264, 9467]),
}
if quick:
    FENS = {k: (f, v[:2]) for k, (f, v) in FENS.items()}

SET_FEN = r'''var fen = %s, g = $.vars, parts = fen.split(' '), rows = parts[0].split('/'), T = { p: 1, n: 2, b: 3, r: 4, q: 5, k: 6 };
g.B = []; for (var i = 0; i < 128; i++) g.B.push(0);
g.KS = [0, 0];
for (var r = 0; r < 8; r++) { var f = 0; for (var c of rows[r]) { if (c >= '1' && c <= '8') { f += +c; continue; }
  var lo = c.toLowerCase(), pc = T[lo] + (c === lo ? 8 : 0), sq = r * 16 + f; g.B[sq] = pc; if (T[lo] === 6) g.KS[c === lo ? 1 : 0] = sq; f++; } }
g.side = parts[1] === 'w' ? 0 : 1;
g.cr = (parts[2].indexOf('K') >= 0 ? 1 : 0) | (parts[2].indexOf('Q') >= 0 ? 2 : 0) | (parts[2].indexOf('k') >= 0 ? 4 : 0) | (parts[2].indexOf('q') >= 0 ? 8 : 0);
g.ep = parts[3] === '-' ? -1 : (8 - +parts[3][1]) * 16 + (parts[3].charCodeAt(0) - 97);
g.ev = 0; for (var s = 0; s < 128; s++) if (!(s & 136)) g.ev += g.PV[g.B[s] * 128 + s];
g.MS = []; g.US = []; g.hm = 0;'''


def C(t, inv=False, **params):
    return dict(type=t, enabled=True, invert=inv, params=params)


def A(t, **params):
    return dict(type=t, enabled=True, invert=False, params=params)


def X(e):
    return C('exprTrue', value=e)


evs = []


def E(conds=(), acts=(), level=0, **extra):
    d = dict(id='t%d' % len(evs), enabled=True, conditions=list(conds), actions=list(acts), level=level)
    d.update(extra)
    evs.append(d)


E(type='function', name='Perft', params='d')
E([X('$d == 0')], [A('returnValue', value='1')], level=1)
E([], [A('localVar', variable='st', value='len($MS)'), A('callFunction', name='Gen', args='$side, 0'), A('localVar', variable='en', value='len($MS)'),
       A('localVar', variable='n', value='0')], level=1)
E([], [A('localVar', variable='m', value='$MS[$st + $i]'), A('callFunction', name='Make', args='$m')], level=1, type='repeat', count='$en - $st', var='i')
E([X('Fn.Att($KS[1 - $side], $side) == 0')], [A('setVar', variable='n', op='add', value='Fn.Perft($d - 1)')], level=2)
E([], [A('callFunction', name='Unmake', args='$m')], level=2)
E([], [A('arrayResize', variable='MS', size='$st'), A('returnValue', value='$n')], level=1)

# cada calculo va en su propio fotograma: el motor corta un fotograma que pase de 3 s (bucles sin fin)
pasos = [[A('jsCode', code='$.vars.R = {}; $.vars.W = {}; $.vars.T = {};')]]
for name, (fen, want) in FENS.items():
    for d, w in enumerate(want, 1):
        pasos.append([A('jsCode', code=SET_FEN % json.dumps(fen)), A('jsCode', code='$.vars.__t = Date.now();'),
                      A('callFunction', name='Perft', args=str(d), result='r'),
                      A('jsCode', code='$.vars.R["%s%d"] = $.vars.r; $.vars.W["%s%d"] = %d; $.vars.T["%s%d"] = Date.now() - $.vars.__t;' % (name, d, name, d, w, name, d))])
# tiempo del bot: posicion inicial y una de medio juego, en los tres niveles
BOT = {'inicio': FENS['inicio'][0], 'medio': 'r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P4/2PBPN2/PP1N1PPP/R1BQ1RK1 w - -'}
for name, fen in BOT.items():
    for lv in (1, 2, 3):
        pasos.append([A('jsCode', code=SET_FEN % json.dumps(fen)), A('setVar', variable='nivel', op='set', value=str(lv)),
                      A('jsCode', code='$.vars.__t = Date.now();'), A('callFunction', name='Think'),
                      A('jsCode', code='$.vars.T["bot_%s_%d"] = Date.now() - $.vars.__t; $.vars.T["nodos_%s_%d"] = $.vars.nodes; $.vars.T["bm_%s_%d"] = $.vars.bm & 16383;' % (name, lv, name, lv, name, lv))])
acts = []
acts.append(A('jsCode', code=r'''var g = $.vars, fails = Object.keys(g.W).filter(function (k) { return g.R[k] !== g.W[k]; });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: g.R, ms: g.T }));'''))
acts.append(A('quitGame'))
pasos[-1] += acts
for k, paso in enumerate(pasos):
    E([C('jsCode', code='return $.state.frame === %d;' % (2 + k))], paso)

p['events'] = p['events'] + evs
json.dump(p, open(out, 'w'))
