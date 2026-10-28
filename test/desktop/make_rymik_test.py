#!/usr/bin/env python3
"""Pruebas de Project Ry-mik (examples_rymik.source2d.json) en el jugador nativo. Imprimen "S2DTEST {...}".

  nivel N    : en la escena Juego con el nivel N de la biblioteca (archivos iniciales, 0 = el primero) lo juega
               "perfecto" (cada nota en su fotograma, las largas mantenidas, naranjas golpeadas, grises tocadas) y
               comprueba 1.000.000 con rango SS, el record guardado (puntuacion, precision, partidas) y la vuelta a Niveles.
  menu       : Inicio -> Iniciar -> Niveles (flecha derecha) -> Enter: se abre el Juego con el segundo nivel.
  rebobinar  : en el nivel 1, pausa y R: REBOBINANDO, el tiempo vuelve al principio y el nivel empieza otra vez con el destello.
  editor     : abre un nivel de la biblioteca, cambia el nombre, borra las notas, graba 3 notas (corta, larga y otra con Shift, que ya no es naranja) y
               un efecto, exporta el .json (Documentos/Ry-mik y la carpeta de niveles) y lo prueba en el Juego.
  ajustes F  : importa el nivel F (.json), cambia la tecla del carril 1 y quita el nivel importado.

Usar con --fixed-step --uncapped, HOME en una carpeta temporal (ahi se escriben los .json) y S2D_STORE temporal.
Uso: make_rymik_test.py <examples_rymik.source2d.json> <salida.json> <modo> [arg]"""
import json, sys

src, out, mode = sys.argv[1:4]
arg = sys.argv[4] if len(sys.argv) > 4 else None
d = json.load(open(src))
C = lambda t, **p: dict(type=t, enabled=True, invert=False, params=p)


def js(cond, code):
    return dict(id='t_%d' % len(d['events']), enabled=True, level=0, conditions=[C('jsCode', code=cond)] if cond else [],
                actions=[C('jsCode', code=code)])


def start(scene, **gl):
    d['scenes'] = [s for s in d['scenes'] if s['id'] == scene] + [s for s in d['scenes'] if s['id'] != scene]
    for gv in d['globals']:
        if gv['name'] in gl:
            gv['value'] = gl[gv['name']]


def report(cond, body):
    """informe (en los eventos comunes: corre en cualquier escena) y salir"""
    e = js(cond, body)
    e['actions'].append(C('quitGame'))
    d['events'].append(e)


PLAY = r'''var st = $.state, g = st.globals, K = g.Teclas;
if (st.sceneId !== 'sc_juego' || g.Pausa == 1 || g.Rebob == 1) return;
var half = 1 / 120 + 1e-9;
for (var c = 0; c < 4; c++) {
  var down = false, press = false;
  st.instances.forEach(function (n) {
    if (n.objId !== 'o_nota' || n.vars.carril != c) return;
    var dt = n.vars.t - g.TM, dist = Math.abs(dt) * g.V;
    if (n.vars.tipo == 0 && n.vars.esp != 1 && dt <= half && dt > -half) press = true;
    if (n.vars.esp == 1 && n.vars.tipo != 2 && dist <= 60 * g.V / 400 - 2 && !(st.frame % 2)) press = true;
    if (n.vars.tipo != 0 && dt * g.V < 60) down = true;
  });
  if (press) st.pressed[K[c]] = true;
  st.keys[K[c]] = down || press;
}'''
SAVED = r'''var stored = ''; try { stored = localStorage.getItem('source2d.save.rymik2') || ''; } catch (e) {}
var saved = stored ? JSON.parse(stored).globals : {};'''

if mode == 'nivel':
    start('sc_juego', Seleccion=int(arg))
    d['events'].insert(0, js('', PLAY))
    report('var g = $.state.globals; return ($.state.sceneId === "sc_niveles" && g.Terminado == 1) || ($.state.frame > 60 * 400 && $.state.sceneId === "sc_juego");',
           SAVED + r'''
var g = $.state.globals, i = g.RecNombre.indexOf(g.NombreNivel);
var got = { nivel: g.NombreNivel, escena: $.state.sceneId, puntuacion: g.Puntuacion, total: g.TotalNotas, perdida: g.SSPerdida,
            record: g.RecPuntos[i], precision: g.RecPrec[i], partidas: g.RecJugadas[i], guardado: saved.RecPuntos ? saved.RecPuntos[saved.RecNombre.indexOf(g.NombreNivel)] : null };
var fails = [];
if (got.escena !== 'sc_niveles') fails.push('no volvio a Niveles');
if (got.puntuacion !== 1000000) fails.push('puntuacion');
if (got.perdida != 0) fails.push('rango SS perdido');
if (got.record !== 1000000 || got.precision !== 100 || got.partidas !== 1 || got.guardado !== 1000000) fails.push('record');
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));''')
elif mode == 'menu':
    start('sc_inicio')
    d['events'].insert(0, js('', r'''var st = $.state, g = st.globals, f = st.frame;
if (st.sceneId === 'sc_inicio' && f === 100) st.pressed['enter'] = true;
if (st.sceneId === 'sc_niveles' && f === 30) st.pressed['arrowright'] = true;
if (st.sceneId === 'sc_niveles' && f === 60) { g._lvSel = g.LvSel; st.pressed['enter'] = true; }'''))
    report('return ($.state.sceneId === "sc_juego" && $.state.frame >= 3) || $.state.frame > 900;', r'''var g = $.state.globals;
var got = { escena: $.state.sceneId, seleccion: g.Seleccion, lvSel: g._lvSel, nivel: g.NombreNivel, notas: (g.T || []).length, biblioteca: g.LvNombre.length };
var ok = got.escena === 'sc_juego' && got.seleccion === 1 && got.nivel === 'Citadel' && got.notas > 1000 && got.biblioteca === 7;
console.log('S2DTEST ' + JSON.stringify({ ok: ok, got: got }));''')
elif mode == 'rebobinar':
    start('sc_juego', Seleccion=1)
    d['events'].insert(0, js('', PLAY))
    d['events'].insert(0, js('', r'''var st = $.state, g = st.globals, f = st.frame;
if (st.sceneId !== 'sc_juego') return;
if (!g._rb && f === 600) st.pressed['p'] = true;
if (!g._rb && f === 640) { st.pressed['r'] = true; g._rb = 1; g._tmAntes = g.TM; }
if (g._rb === 1 && g.Rebob == 1) { g._vioRebob = 1; g._minTM = Math.min(g._minTM == null ? 99 : g._minTM, g.TM); g._notasRb = Math.max(g._notasRb || 0, st.instances.filter(function (i) { return i.objId === 'o_nota'; }).length); }'''))
    report('var g = $.state.globals; return (g._rb === 1 && g._vioRebob === 1 && g.Rebob == 0 && $.state.frame >= 2) || $.state.frame > 2000;', r'''var g = $.state.globals;
var got = { tmAntes: g._tmAntes, tmMin: g._minTM, notasDuranteRebobinado: g._notasRb, tmAhora: g.TM, destello: g.FadeA, crt: g.CrtAmt, frame: $.state.frame };
var ok = g._vioRebob === 1 && got.tmAntes > 5 && got.tmMin <= -2.99 && got.notasDuranteRebobinado > 0 && got.tmAhora < -2.9 && got.destello > 0.5 && got.crt > 0.45;
console.log('S2DTEST ' + JSON.stringify({ ok: ok, got: got }));''')
elif mode == 'editor':
    start('sc_editor', EdAbrir=1)
    d['events'].insert(0, js('', r'''var st = $.state, g = st.globals, f = st.frame;
if (st.sceneId !== 'sc_editor') return;
function key(k) { st.pressed[k] = true; st.lastKey = k; }
if (f === 5) { g.Sel = 9; key('enter'); }
if (f === 10) { g._abiertas = g.EdT.length; g.Sel = 0; key('enter'); }
if (f === 12) st.typed = 'Prueba';
if (f === 14) key('enter');
if (f === 16) { g.Sel = 15; key('enter'); }
if (f === 18) key('enter');
if (f === 20) { g.Sel = 11; key('enter'); }
if (f >= 21 && g.EdModo == 2) {
  var t = g.GT;
  function at(a) { return t >= a && t < a + 1 / 60; }
  if (at(0.5)) { key('d'); st.keys.d = true; }
  if (at(0.6)) { st.keys.d = false; st.released.d = true; }
  if (at(1.0)) { key('f'); st.keys.f = true; }
  if (at(1.8)) { st.keys.f = false; st.released.f = true; }
  if (at(2.2)) { st.keys.shift = true; key('j'); st.keys.j = true; }
  if (at(2.3)) { st.keys.j = false; st.released.j = true; st.keys.shift = false; }
  if (at(2.5)) key('t');
  if (at(3.0)) key('enter');
}
if (f === 420) { g._grabadas = { n: g.EdT.length, fin1: g.EdFin[1], esp2: g.EdEsp[2], ef: g.EdEC[0], nombre: g.EdNombre }; g.Sel = 14; key('enter'); }
if (f === 440) { g._lib = g.LvNombre.length; g._libIni = g.LvIni[g.LvIni.length - 1]; g.Sel = 13; key('enter'); }'''))
    report('return ($.state.sceneId === "sc_juego" && $.state.frame >= 3) || $.state.frame > 1200;', r'''var g = $.state.globals, nt = typeof __native !== 'undefined' ? __native : null;
var p = nt ? nt.paths() : ['', '', ''];
var doc = nt ? nt.fileRead(p[2] + '/Ry-mik/CitadelPrueba.json') : null, dat = nt ? nt.fileRead(p[1] + '/ry-mik/niveles/CitadelPrueba.json') : null;
var lv = doc ? JSON.parse(doc) : {};
var got = { abiertas: g._abiertas, grabadas: g._grabadas, biblioteca: g._lib, importado: g._libIni, escena: $.state.sceneId, pista: g.Pista,
            notasJuego: (g.T || []).length, exportado: !!doc, enNiveles: !!dat, formato: lv.formato, nombre: lv.nombre, notasJson: lv.notas ? lv.notas.t.length : -1,
            conMusica: !!(lv.musica && lv.musica.datos && lv.musica.datos.length > 100000) };
var r = got.grabadas || {};
var ok = got.abiertas > 1000 && r.n === 3 && r.fin1 > 1.7 && r.esp2 === 0 && r.ef === 'Y' && r.nombre === 'CitadelPrueba' && got.exportado && got.enNiveles &&
         got.formato === 'rymik-nivel' && got.notasJson === 3 && got.conMusica && got.biblioteca === 8 && got.importado === 0 &&
         got.escena === 'sc_juego' && got.pista === 'editor' && got.notasJuego === 3;
console.log('S2DTEST ' + JSON.stringify({ ok: ok, got: got }));''')
elif mode == 'ajustes':
    start('sc_ajustes', ArchivoModo='nivel', ArchivoElegido=arg)
    d['events'].insert(0, js('', r'''var st = $.state, g = st.globals, f = st.frame;
if (st.sceneId !== 'sc_ajustes') return;
function key(k) { st.pressed[k] = true; st.lastKey = k; }
if (f === 10) { g._imp = { n: g.LvNombre.length, ini: g.LvIni[g.LvIni.length - 1], nombre: g.LvNombre[g.LvNombre.length - 1], msg: g.Msg, quitar: g.Quitar }; }
if (f === 20) { g.Sel = 0; key('enter'); }
if (f === 40) key('a');
if (f === 50) { g.Sel = 11; key('enter'); }
if (f === 52) key('enter');
if (f === 60) { g._fin = { n: g.LvNombre.length, tecla: g.Teclas[0] }; }'''))
    report('return $.state.frame === 62;', SAVED + r'''var g = $.state.globals;
var got = { importado: g._imp, despues: g._fin, guardada: saved.Teclas ? saved.Teclas[0] : null };
var i = got.importado || {}, f2 = got.despues || {};
var ok = i.n === 8 && i.ini === 0 && i.quitar === 7 && f2.n === 7 && f2.tecla === 'a' && got.guardada === 'a';
console.log('S2DTEST ' + JSON.stringify({ ok: ok, got: got }));''')
json.dump(d, open(out, 'w'))
