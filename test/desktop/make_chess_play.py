#!/usr/bin/env python3
"""Partida de ajedrez simulada: empieza en la escena Partida y hace clics en casillas en fotogramas dados.
Uso: make_chess_play.py <ajedrez.json> <salida.json> <modo> <nivel> "fotograma:casilla ..." [aleatorio | fen=<posicion>]
  casilla en notacion normal (e2) o "btn:Nombre" para pulsar un boton. Con "aleatorio" el jugador humano
  hace jugadas legales al azar hasta que termina la partida y se imprime "S2DTEST {...}"."""
import json, sys
src, out, modo, nivel, sched = sys.argv[1:6]
extra = sys.argv[6] if len(sys.argv) > 6 else ''
rnd = extra == 'aleatorio'
fen = extra[4:] if extra.startswith('fen=') else ''
p = json.load(open(src))
p['scenes'] = [s for s in p['scenes'] if s['name'] == 'Partida'] + [s for s in p['scenes'] if s['name'] != 'Partida']
for g in p['globals']:
    if g['name'] == 'modo':
        g['value'] = int(modo)
    if g['name'] == 'nivel':
        g['value'] = int(nivel)
clicks = {}
for item in sched.split():
    f, what = item.split(':', 1)
    clicks[int(f)] = what
code = r'''var st = $.state, m = st.mouse, f = st.frame, S = %s, w = S[f];
function clickAt(x, y) { m.x = x; m.y = y; m.wx = x / 4; m.wy = y / 4; m.pressed = true; m.button = 0; }
function clickSq(sq) { clickAt((12 + (sq & 7) * 16) * 4, (12 + (sq >> 4) * 16) * 4); }
if (w && w.indexOf('btn:') === 0) {
  var oid = $.instOf(w.slice(4)), b = st.instances.filter(function (i) { return i.objId === oid; })[0];
  if (b) clickAt(b.x, b.y);
} else if (w) clickSq((8 - +w[1]) * 16 + (w.charCodeAt(0) - 97));
else if (m.pressed === false) { m.x = -50; m.y = -50; m.wx = -50; m.wy = -50; }
''' % json.dumps(clicks)
if fen:
    # coloca la posicion al tercer fotograma y vuelve a crear las piezas con la funcion Resync del juego
    code += r'''
if (f === 3) {
  var fen = %s, g = st.globals, parts = fen.split(' '), rows = parts[0].split('/'), T = { p: 1, n: 2, b: 3, r: 4, q: 5, k: 6 };
  g.B = []; for (var i = 0; i < 128; i++) g.B.push(0);
  for (var r = 0; r < 8; r++) { var fl = 0; for (var c of rows[r]) { if (c >= '1' && c <= '8') { fl += +c; continue; }
    var lo = c.toLowerCase(), sq = r * 16 + fl; g.B[sq] = T[lo] + (c === lo ? 8 : 0); if (T[lo] === 6) g.KS[c === lo ? 1 : 0] = sq; fl++; } }
  g.side = parts[1] === 'w' ? 0 : 1; g.cr = 0; g.ep = -1; g.ev = 0;
  for (var s2 = 0; s2 < 128; s2++) if (!(s2 & 136)) g.ev += g.PV[g.B[s2] * 128 + s2];
  g.HM = []; g.HIS = []; $.call('Resync');
}''' % json.dumps(fen)
if rnd:
    code += r'''
var g = st.globals;
if (!g.__plies) g.__plies = 0;
if (g.fase === 0 && (g.modo === 0 || g.side === 0) && g.LM.length && f % 3 === 0) {
  if (g.sel < 0) { var mv = g.LM[Math.floor(Math.random() * g.LM.length)]; g.__mv = mv; clickSq(mv & 127); }
  else { clickSq((g.__mv >> 7) & 127); g.__plies++; }
}
if (g.fase === 3 && f % 3 === 0) { var po = $.instOf('PromoOpcion'), opt = st.instances.filter(function (i) { return i.objId === po; })[0]; if (opt) clickAt(opt.x * 4, opt.y * 4); }
if ((g.fase === 4 || g.__plies > 400) && !g.__done) {
  g.__done = true;
  console.log('S2DTEST ' + JSON.stringify({ ok: g.fase === 4 || g.__plies > 400, plies: g.HM.length, fin: g.t1 + ' / ' + g.t2, historia: g.HIS.join(' ') }));
  $.state.running = false;
}'''
ev = {'id': 'test_clicks', 'enabled': True, 'level': 0, 'conditions': [], 'actions': [{'type': 'jsCode', 'enabled': True, 'invert': False, 'params': {'code': code}}]}
p['events'] = [ev] + p['events']
json.dump(p, open(out, 'w'))
