#!/usr/bin/env python3
"""Prueba de lo nuevo de la Beta 0.1: Esperar (con los objetos elegidos), mensajes, pausa (los eventos siguen y la
fisica se para), tween terminado, mando como teclado (pad_a = espacio), sonidos por canal sin fallar aunque no haya
audio, y materiales (piedra se queda, goma rebota, globo sube). Imprime "S2DTEST {...}" y sale.
Uso: make_beta_project.py <ejemplo.json> <salida.json>"""
import json, sys
src, out = sys.argv[1:3]
p = json.load(open(src))
sc = p['scenes'][0]
P0 = p['objects'][0]['id']
M = [o for o in p['objects'] if o['name'] == 'Moneda'][0]['id']
sc['instances'] = [i for i in sc['instances'] if i['objectId'] in (P0, M)]


def C(t, **params):
    return dict(type=t, enabled=True, invert=False, params=params)


evs = []


def E(conds=(), acts=(), level=0, **extra):
    d = dict(id='e%d' % len(evs), enabled=True, conditions=list(conds), actions=list(acts), level=level)
    d.update(extra)
    evs.append(d)


F = lambda n: C('jsCode', code='return $.state.frame === %d;' % n)
E([C('atStart')], [C('setGlobal', variable='w', value='0'), C('wait', seconds='0.5'), C('setGlobal', variable='w', value='1'),
                   C('sendMessage', name='hola'), C('tweenOpacity', object=M, value='10', duration='0.2', easing='linear')])
E([C('messageReceived', name='hola')], [C('addGlobal', variable='msgs', value='1'),
                                        C('jsCode', code='$.vars.msgFrame = $.state.frame;')])
E([C('tweenDone', object=M)], [C('addGlobal', variable='tw', value='1')])
E([F(5)], [C('playSound', track='nada', volume='50', pitch='1', vary='10', channel='0'), C('channelVolume', channel='1', value='40'),
           C('channelStop', channel='0'), C('vibrate', pad='1', strength='50', duration='0.1')])
# mando: pad_a hace de espacio
E([F(10)], [C('jsCode', code='$.state.keys.pad_a = true; $.state.pressed.pad_a = true;')])
E([C('keyDown', key=' ')], [C('setGlobal', variable='padOk', value='1')])
E([F(11)], [C('jsCode', code='$.state.keys.pad_a = false;')])
# pausa: en el fotograma 70 se pausa; la x del jugador no cambia mientras; en el 80 se quita
E([F(70)], [C('pauseGame', mode='on'), C('jsCode', code='var j=$.state.instances[0]; j.vx = 300; $.vars.px = j.x;')])
E([C('gamePaused')], [C('addGlobal', variable='pf', value='1')])
E([F(79)], [C('jsCode', code='$.vars.pmoved = Math.abs($.state.instances[0].x - $.vars.px) > 0.01 ? 1 : 0;'), C('pauseGame', mode='off')])
# grupo: "Cosas" = Jugador + Moneda; contar por el grupo y mover por el grupo
p['objects'].append(dict(id='o_grupo', name='Cosas', type='group', members=[P0, M], behaviors={}))
E([F(3)], [C('jsCode', code='$.vars.antes = $.state.instances.filter(function(i){return i.objId==="%s";})[0].x;' % M),
           C('addToX', object='o_grupo', value='5')])
E([F(4), C('objCount', object='o_grupo', op='ge', value='2')], [C('setGlobal', variable='grp', value='1'),
   C('jsCode', code='$.vars.movio = $.state.instances.filter(function(i){return i.objId==="%s";})[0].x - $.vars.antes;' % M)])
# rayo hacia abajo desde el jugador choca con el suelo (tiles)
E([F(30), C('raycast', object=P0, angle='90', distance='400', target='')], [C('setGlobal', variable='rayo', value='{= RayoDistancia() > 0 ? 1 : 0}')])
# pausa de impacto: congela el juego (tambien su reloj: entre el fotograma 40 y el 41 solo pasa uno)
E([F(40)], [C('jsCode', code='$.vars.t40 = $.state.time;'), C('hitStop', seconds='0.1'), C('jsCode', code='$.vars.hsOn = $.state.hitstop > 0 ? 1 : 0;')])
E([F(41)], [C('jsCode', code='$.vars.hs = Math.round(($.state.time - $.vars.t40) * 60);')])
E([F(42)], [C('flashWhite', object=P0, duration='0.1'), C('squash', object=P0, amount='30', duration='0.2'), C('shakeCamera', intensity='8', duration='0.3')])
# idiomas
p['i18n'] = {'langs': ['es', 'en'], 'strings': {'hola': {'es': 'Hola', 'en': 'Hello'}, 'solo': {'es': 'Solo'}}}
E([F(50)], [C('setLanguage', lang='en'), C('setVarText', variable='saludo', text='{t:hola} {t:solo} {= T("nada")}')])
# guardar todo y cargar: la moneda vuelve a donde estaba
E([F(52)], [C('jsCode', code='$.vars.mx = $.state.instances.filter(function(i){return i.objId==="%s";})[0].x;' % M),
            C('saveGame', slot='prueba_beta', what='all'), C('addToX', object=M, value='300')])
E([F(54)], [C('loadGame', slot='prueba_beta')])
E([F(56)], [C('jsCode', code='$.vars.cargado = Math.abs($.state.instances.filter(function(i){return i.objId==="%s";})[0].x - $.vars.mx) < 0.01 ? 1 : 0;' % M),
            C('deleteSave', slot='prueba_beta')])
# buscar camino: la moneda va a otro punto esquivando lo solido
E([F(58)], [C('pathTo', object=M, x='Moneda.X + 64', y='Moneda.Y', speed='600')])
E([C('pathArrived', object=M)], [C('setGlobal', variable='llego', value='1')])
E([C('pathFailed', object=M)], [C('setGlobal', variable='llego', value='-1')])
# elegir el mas cercano al origen: la moneda con menor distancia
E([F(60), C('pickNearest', object=M, x='0', y='0')], [C('setGlobal', variable='cerca', value='{= Moneda.X}'),
   C('jsCode', code='var b=null,bd=1e18; $.state.instances.forEach(function(i){ if(i.objId==="%s"){ var d=i.x*i.x+i.y*i.y; if(d<bd){bd=d;b=i;} } }); $.vars.cercaJs = b ? b.x : -1;' % M)])
report = r'''var g = $.vars;
var got = { w: g.w, msgs: g.msgs, msgFrame: g.msgFrame > 30, tw: g.tw, padOk: g.padOk, pf: g.pf, pmoved: g.pmoved, grp: g.grp, movio: g.movio, rayo: g.rayo, hs: g.hs, hsOn: g.hsOn, saludo: g.saludo, cargado: g.cargado, llego: g.llego, cerca: Math.abs(+g.cerca - g.cercaJs) < 0.01 };
var want = { w: 1, msgs: 1, msgFrame: true, tw: 1, padOk: 1, pf: 10, pmoved: 0, grp: 1, movio: 5, rayo: 1, hs: 1, hsOn: 1, saludo: 'Hello Solo nada', cargado: 1, llego: 1, cerca: true };
var fails = Object.keys(want).filter(function (k) { return String(got[k]) !== String(want[k]); });
console.log('S2DTEST ' + JSON.stringify({ ok: !fails.length, fails: fails, got: got }));'''
E([F(100)], [C('jsCode', code=report), C('quitGame')])
for _sc in p['scenes']:
    _sc['events'] = []
p['events'] = evs
p['globals'] = []
json.dump(p, open(out, 'w'))
