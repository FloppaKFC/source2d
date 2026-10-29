#!/usr/bin/env python3
"""Plantillas para el apartado Plantillas de la pagina de inicio: juegos pequenos por genero, con un comentario en
cada evento que explica que hace y por que (para aprender mirandolos por dentro). Usan el arte de
test/proyecto_prueba.source2d.json.
Genera examples_tpl_plataformas, examples_tpl_aventura, examples_tpl_disparos y examples_tpl_clics (.source2d.json).
Uso: make_templates.py [carpeta]"""
import copy
import json
import os
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
OUT = sys.argv[1] if len(sys.argv) > 1 else RAIZ
base = json.load(open(os.path.join(RAIZ, 'test', 'proyecto_prueba.source2d.json')))
OB = {o['name']: o for o in base['objects']}
J, MON, ENE, TXT, BTN = (OB[n]['id'] for n in ('Jugador', 'Moneda', 'Enemigo', 'Puntuacion', 'BotonReiniciar'))
_n = [0]

def uid(p):
    _n[0] += 1
    return '%s_t%04d' % (p, _n[0])

def C(t, **p):
    return dict(type=t, enabled=True, invert=False, params=p)

def NOT(c):
    c['invert'] = True
    return c

def comment(text):
    return dict(id=uid('nt'), kind='comment', text=text)

def ev(conds, acts, level=0, **extra):
    d = dict(id=uid('ev'), enabled=True, level=level, conditions=list(conds), actions=list(acts))
    d.update(extra)
    return d

def inst(obj, x, y, **kw):
    d = dict(id=uid('i'), objectId=obj, x=x, y=y, angle=0, scaleX=1, scaleY=1, opacity=100, frame=0)
    d.update(kw)
    return d

def proyecto(nombre, objetos, escena, eventos, globales=('puntos',), ancho=960, alto=540):
    d = copy.deepcopy(base)
    d['id'] = uid('p')
    d['name'] = nombre
    d['objects'] = objetos
    d['settings']['width'] = ancho
    d['settings']['height'] = alto
    escena['events'] = eventos
    d['scenes'] = [escena]
    d['events'] = []
    d['globals'] = [dict(name=g, type='normal', value=0) for g in globales]
    d['music'] = []
    d['files'] = []
    return d

def texto(oid, nombre, contenido, size=24, color='#ffffff'):
    t = copy.deepcopy(OB['Puntuacion'])
    t.update(id=oid, name=nombre, hud=True)
    t['text'] = dict(content=contenido, size=size, color=color, align='left')
    return t

sc = copy.deepcopy(base['scenes'][0])
sc['name'] = 'Nivel'
sc['instances'] = [i for i in sc['instances'] if i['objectId'] != OB['Ayuda']['id']]
plat = proyecto('Plantilla: Plataformas', [copy.deepcopy(OB[n]) for n in ('Jugador', 'Moneda', 'Enemigo', 'Puntuacion', 'BotonReiniciar')], sc, [
    comment('PLANTILLA DE PLATAFORMAS. El Jugador tiene el comportamiento "Personaje plataformero": camina con las '
            'flechas, salta con espacio y cae con gravedad sin que haga falta ningun evento. Los eventos de abajo '
            'solo ponen las reglas del juego: monedas, enemigos y ganar o perder.'),
    comment('Al empezar: los puntos a cero y la camara sigue al jugador (el suavizado hace que no vaya a tirones).'),
    ev([C('atStart')], [C('setGlobal', variable='puntos', value='0'), C('cameraFollow', object=J, smoothing='0.1', lookahead='0.2', deadzone='0')]),
    comment('Tocar una moneda: se elimina SOLO la moneda tocada (la condicion elige cual) y se suman 10 puntos. '
            'El sonido y la animacion del marcador hacen que se sienta bien.'),
    ev([C('collision', object=J, **{'with': MON})], [C('removeObj', object=MON), C('addGlobal', variable='puntos', value='10'),
                                                     C('tweenScale', object=TXT, x='1.3', y='1.3', duration='0.1', easing='easeOut'),
                                                     C('beep', freq='940', duration='0.07')]),
    comment('Cuando la animacion del marcador termina, vuelve a su tamano.'),
    ev([C('tweenDone', object=TXT)], [C('tweenScale', object=TXT, x='1', y='1', duration='0.15', easing='easeOut')]),
    comment('Tocar un enemigo: la camara tiembla, se congela un instante (pausa de impacto) y se pierde.'),
    ev([C('collision', object=J, **{'with': ENE})], [C('shakeCamera', intensity='14', duration='0.4'), C('hitStop', seconds='0.08'),
                                                     C('endGame', message='Has perdido. Puntos: {g:puntos}. Pulsa R')]),
    comment('Caerse fuera del nivel tambien es perder.'),
    ev([C('objOutside', object=J)], [C('endGame', message='Te has caido. Pulsa R')]),
    comment('Ganar: con 120 puntos (todas las monedas) el marcador lo dice. Cambia la cifra si pones mas monedas.'),
    ev([C('globalVar', variable='puntos', op='ge', value='120'), C('triggerOnce')], [C('setText', object=TXT, text='¡Nivel superado! Puntos: {g:puntos}')]),
    comment('Reiniciar con el boton o con la tecla R.'),
    ev([C('clicked', object=BTN)], [C('restartScene')]),
    ev([C('keyPressed', key='r')], [C('restartScene')]),
])
for o in plat['objects']:
    if o['id'] == TXT:
        o['text']['content'] = 'Puntos: {g:puntos}'

jug = copy.deepcopy(OB['Jugador'])
jug['comportamientos'] = {}
jug['behaviors'].update(movement='topdown', gravity=False, collide=True, speed=200)
ene = copy.deepcopy(OB['Enemigo'])
ene['behaviors'].update(movement='none', gravity=False, collide=True)
ene['behaviorList'] = [dict(type='follow', target='Jugador', speed=70, stop=4, range=260)]
mon = copy.deepcopy(OB['Moneda'])
mon['behaviorList'] = [dict(type='sine', axis='y', amplitude=4, period=1.2, phase=0)]
sc2 = dict(id=uid('sc'), name='Mundo', width=1600, height=1000, background='#2c4a33', instances=[], tilemaps=[], effects=[],
           camera=dict(zoom=1.5, smooth=0.1, limits='scene'))
sc2['instances'] = [inst(J, 200, 200)] + [inst(MON, 300 + 160 * (k % 8), 260 + 180 * (k // 8)) for k in range(24)] + \
                   [inst(ENE, 700, 600), inst(ENE, 1300, 300), inst(ENE, 1100, 850), inst(TXT, 20, 20)]
avent = proyecto('Plantilla: Aventura', [jug, mon, ene, texto(TXT, 'Puntuacion', 'Gemas: {g:puntos} / 24')], sc2, [
    comment('PLANTILLA DE AVENTURA VISTA DESDE ARRIBA. El Jugador tiene "Movimiento cenital": se mueve en 8 '
            'direcciones con flechas o WASD (y con el mando). Las gemas flotan con "Movimiento sinusoidal" y los '
            'enemigos tienen "Seguir": te persiguen si estas a menos de 260 px. Todo sin eventos.'),
    comment('Al empezar: la camara sigue al jugador y mira un poco hacia donde va (anticipacion).'),
    ev([C('atStart')], [C('setGlobal', variable='puntos', value='0'), C('cameraFollow', object=J, smoothing='0.12', lookahead='0.25', deadzone='12')]),
    comment('Recoger una gema: se elimina la tocada, suma uno y salen particulas desde ella.'),
    ev([C('collision', object=J, **{'with': MON})], [C('particles', object=MON, x='0', y='0', count='14', color='#ffe066,#ffffff', size='3',
                                                                    speed='120', angle='-90', spread='360', life='0.5', gravity='0', glow='yes'),
                                                     C('removeObj', object=MON), C('addGlobal', variable='puntos', value='1'), C('beep', freq='1200', duration='0.05')]),
    comment('Un enemigo te toca: destello blanco y vuelves al principio (con los puntos que llevabas).'),
    ev([C('collision', object=J, **{'with': ENE})], [C('flashWhite', object=J, duration='0.15'), C('setPos', object=J, x='200', y='200'),
                                                     C('shakeCamera', intensity='8', duration='0.25')]),
    comment('Ganar: todas las gemas. "Solo una vez" hace que el mensaje no se repita cada fotograma.'),
    ev([C('globalVar', variable='puntos', op='ge', value='24'), C('triggerOnce')], [C('setText', object=TXT, text='¡Todas las gemas! Pulsa R para jugar otra vez')]),
    ev([C('keyPressed', key='r')], [C('restartScene')]),
])

nave = copy.deepcopy(OB['Jugador'])
nave['name'] = 'Nave'
nave['comportamientos'] = {}
nave['behaviors'].update(movement='topdown', gravity=False, collide=False, speed=320)
nave['behaviorList'] = [dict(type='keepOnScreen')]
bala = copy.deepcopy(OB['Moneda'])
bala.update(id='o_bala', name='Bala')
bala['behaviorList'] = [dict(type='destroyOutside', margin=32)]
roca = copy.deepcopy(OB['Enemigo'])
roca.update(name='Roca')
roca['behaviors'].update(movement='none', gravity=False, collide=False)
roca['behaviorList'] = [dict(type='destroyOutside', margin=64), dict(type='health', max=3, start='')]
sc3 = dict(id=uid('sc'), name='Espacio', width=540, height=760, background='#0b1020', instances=[], tilemaps=[], effects=[],
           camera=dict(zoom=1, smooth=0, limits='scene'))
sc3['instances'] = [inst(J, 270, 680), inst(TXT, 16, 16)]
dispar = proyecto('Plantilla: Disparos', [nave, bala, roca, texto(TXT, 'Puntuacion', 'Puntos: {g:puntos}   Vidas: {g:vidas}')], sc3, [
    comment('PLANTILLA DE DISPAROS (VERTICAL). La Nave se mueve con las flechas y "Mantener en pantalla" no la deja '
            'salir. Las Balas y las Rocas tienen "Destruir fuera de la pantalla": no se acumulan aunque salgan miles.'),
    ev([C('atStart')], [C('setGlobal', variable='puntos', value='0'), C('setGlobal', variable='vidas', value='3')]),
    comment('Disparar: con espacio, como mucho cada 0.15 s (el temporizador "disparo" lo controla).'),
    ev([C('keyDown', key=' '), C('timerCompare', name='disparo', op='ge', value='0.15')],
       [C('create', object='o_bala', x='Nave.X', y='Nave.Y - 20'), C('setVY', object='o_bala', value='-600'),
        C('timerCtl', name='disparo', action='reset'), C('beep', freq='1400', duration='0.03')]),
    ev([C('atStart')], [C('timerCtl', name='disparo', action='start')]),
    comment('Cada 0.8 s cae una roca desde arriba en un lugar al azar. "randInt" da un numero entero al azar.'),
    ev([C('every', seconds='0.8')], [C('create', object=ENE, x='randInt(30, 510)', y='-30'), C('setVY', object=ENE, value='randInt(120, 220)'),
                                     C('setSpin', object=ENE, value='randInt(-90, 90)')]),
    comment('Una bala toca una roca: la roca pierde 1 de salud (tiene 3) y la bala desaparece. La condicion elige '
            'justo esa bala y esa roca, no todas.'),
    ev([C('collision', object='o_bala', **{'with': ENE})], [C('damage', object=ENE, value='1'), C('flashWhite', object=ENE, duration='0.06'),
                                                            C('removeObj', object='o_bala')]),
    comment('Roca sin salud: explota en particulas y suma puntos.'),
    ev([C('isDead', object=ENE)], [C('particles', object=ENE, x='0', y='0', count='24', color='#ff9a3c,#ffd27a,#888888', size='4', speed='200',
                                                angle='0', spread='360', life='0.6', gravity='0', glow='yes'),
                                   C('removeObj', object=ENE), C('addGlobal', variable='puntos', value='10')]),
    comment('Una roca choca con la nave: se pierde una vida. Sin vidas, se acaba.'),
    ev([C('collision', object=J, **{'with': ENE})], [C('removeObj', object=ENE), C('addGlobal', variable='vidas', value='-1'),
                                                     C('shakeCamera', intensity='10', duration='0.3'), C('vibrate', pad='1', strength='70', duration='0.2')]),
    ev([C('globalVar', variable='vidas', op='le', value='0'), C('triggerOnce')], [C('endGame', message='Fin. Puntos: {g:puntos}. Pulsa R')]),
], globales=('puntos', 'vidas'), ancho=540, alto=760)

pieza = copy.deepcopy(OB['Moneda'])
pieza.update(name='Pieza')
pieza['behaviorList'] = [dict(type='draggable')]
hueco = copy.deepcopy(OB['Enemigo'])
hueco.update(name='Hueco')
hueco['behaviors'].update(movement='none', gravity=False, collide=False)
sc4 = dict(id=uid('sc'), name='Mesa', width=960, height=540, background='#3a2f4a', instances=[], tilemaps=[], effects=[],
           camera=dict(zoom=1, smooth=0, limits='scene'))
sc4['instances'] = [inst(MON, 150 + 70 * k, 120, scaleX=2.5, scaleY=2.5) for k in range(4)] + \
                   [inst(ENE, 300 + 120 * k, 380, scaleX=2.5, scaleY=2.5, opacity=60) for k in range(4)] + [inst(TXT, 20, 20)]
clics = proyecto('Plantilla: Clics y arrastrar', [pieza, hueco, texto(TXT, 'Puntuacion', 'Lleva cada pieza a un hueco: {g:puntos} / 4')], sc4, [
    comment('PLANTILLA DE PUZLE CON EL RATON. Las Piezas tienen el comportamiento "Arrastrable": se pueden mover con '
            'el raton (o el dedo en un movil) sin eventos.'),
    ev([C('atStart')], [C('setGlobal', variable='puntos', value='0')]),
    comment('Al soltar una pieza encima de un hueco libre: se queda en su centro, ya no se puede arrastrar y suma.'),
    ev([C('dropped', object=MON), C('collision', object=MON, **{'with': ENE})],
       [C('setPos', object=MON, x='Hueco.X', y='Hueco.Y'), C('dragToggle', object=MON, state='off'), C('removeObj', object=ENE),
        C('addGlobal', variable='puntos', value='1'), C('beep', freq='880', duration='0.08')]),
    comment('Clic en una pieza: da un saltito (tween de escala) para que se note que se puede coger.'),
    ev([C('clicked', object=MON)], [C('squash', object=MON, amount='-20', duration='0.15')]),
    ev([C('globalVar', variable='puntos', op='ge', value='4'), C('triggerOnce')], [C('setText', object=TXT, text='¡Completado! Pulsa R')]),
    ev([C('keyPressed', key='r')], [C('restartScene')]),
])

for nombre, doc in (('plataformas', plat), ('aventura', avent), ('disparos', dispar), ('clics', clics)):
    f = os.path.join(OUT, 'examples_tpl_%s.source2d.json' % nombre)
    json.dump(doc, open(f, 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
    print(f)
