#!/usr/bin/env python3
"""Capturas de referencia del dibujo: cada escena de los juegos de prueba y una escena sintetica por cada efecto
(filtros CSS, tinte, contorno, degradado, poligonos, capas con shader...), con paso fijo y azar con semilla, para
comparar dos versiones del jugador pixel a pixel (test/render_compare.py).
Uso: test/render_shots.py <jugador> <carpeta de salida> [filtro]"""
import copy
import json
import os
import subprocess
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FRAMES = 90

C = lambda t, **p: dict(type=t, enabled=True, invert=False, params=p)
# Math.random con semilla (mulberry32), lo primero de cada escena: particulas y ruido salen igual en cada pasada
SEMILLA = ("if (!Math.__s2dSeed) { var s = 12345; Math.random = function () { s |= 0; s = s + 0x6D2B79F5 | 0; "
           "var t = Math.imul(s ^ s >>> 15, 1 | s); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; "
           "return ((t ^ t >>> 14) >>> 0) / 4294967296; }; Math.__s2dSeed = 1; }")


def con_semilla(doc):
    for sc in doc['scenes']:
        sc.setdefault('events', []).insert(0, dict(id='t_seed', enabled=True, level=0, conditions=[],
                                                   actions=[C('jsCode', code=SEMILLA)]))
    return doc


def escena_primero(doc, i):
    d = copy.deepcopy(doc)
    d['scenes'].insert(0, d['scenes'].pop(i))
    d.setdefault('settings', {})['autofullscreen'] = False
    return d


def variantes_efectos():
    base = json.load(open(os.path.join(RAIZ, 'test', 'proyecto_prueba.source2d.json')))
    out = {}
    efectos = {
        'filtro': [dict(type='filter', amount=50, code='grayscale(0.6) sepia(0.3) hue-rotate(40deg) invert(0.1) '
                                                      'brightness(1.2) contrast(1.3) saturate(1.5) opacity(0.9)')],
        'desenfoque': [dict(type='filter', amount=50, code='blur(3px)')],
        'lineas': [dict(type='scanlines', amount=70)],
        'vineta': [dict(type='vignette', amount=80)],
        'ruido': [dict(type='noise', amount=60)],
        'pixelado': [dict(type='pixelate', amount=40)],
        'onda': [dict(type='wave', amount=40)],
        'croma': [dict(type='chroma', amount=60)],
    }
    for k, fx in efectos.items():
        d = copy.deepcopy(base)
        d['scenes'][0]['effects'] = fx
        out['fx_' + k] = d
    # objetos: filtro propio, tinte, contorno, giro y transparencia, texto con contorno, mapa de tiles con filtro
    d = copy.deepcopy(base)
    sc = d['scenes'][0]
    sc['effects'] = []
    ob = {o['name']: o for o in d['objects']}
    ob['Jugador']['shader'] = 'blur(1.5px) saturate(2)'
    ob['Moneda']['outline'] = dict(width=2, color='#ffee00')
    ob['Puntuacion']['outline'] = dict(width=2, color='#ff0000')
    for tm in sc.get('tilemaps') or []:
        tm['shader'] = 'hue-rotate(90deg) brightness(1.3)'
    sc['events'].insert(0, dict(id='t_obj', enabled=True, level=0, conditions=[C('jsCode', code='return $.state.frame === 2;')],
                                actions=[C('jsCode', code="$.all('Enemigo').forEach(function (i, k) { i.tint = [255, 90, 90]; "
                                                          "i.angle = 30 * (k + 1); i.opacity = 70; i.scaleX = 2; i.scaleY = 1.5; });")]))
    out['objetos'] = d
    # shader de escena en JavaScript con el contexto 2D: poligonos, arcos, degradado radial, texto y rectangulos
    d = copy.deepcopy(base)
    d['scenes'][0]['effects'] = []
    d['scenes'][0]['shaderCode'] = (
        "var c = $.ctx; c.save(); c.setTransform(1, 0, 0, 1, 0, 0);"
        "c.fillStyle = 'rgba(255,0,0,0.5)'; c.beginPath(); c.arc(120, 120, 50, 0, Math.PI * 2); c.fill();"
        "c.fillStyle = '#0f0'; c.beginPath(); c.moveTo(300, 60); c.lineTo(380, 180); c.lineTo(220, 180); c.fill();"
        "var g = c.createRadialGradient(560, 140, 10, 560, 140, 90); g.addColorStop(0, '#ffffff'); g.addColorStop(1, 'rgba(0,0,255,0)');"
        "c.fillStyle = g; c.fillRect(460, 40, 200, 200);"
        "c.strokeStyle = '#ff0'; c.lineWidth = 4; c.strokeRect(700, 60, 120, 80);"
        "c.globalCompositeOperation = 'lighter'; c.fillStyle = '#304060'; c.fillRect(100, 260, 300, 120);"
        "c.globalCompositeOperation = 'source-over'; c.globalAlpha = 0.5; c.fillStyle = '#fff'; c.font = 'bold 30px sans-serif';"
        "c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillText('Hola GPU', 480, 300);"
        "c.translate(800, 400); c.rotate(0.5); c.globalAlpha = 1; c.fillStyle = '#f0f'; c.fillRect(-40, -20, 80, 40);"
        "c.restore();")
    out['js_ctx'] = d
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    player, outdir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    filtro = sys.argv[3] if len(sys.argv) > 3 else ''
    os.makedirs(os.path.join(outdir, 'proyectos'), exist_ok=True)
    casos = {}
    for ej in ['ajedrez', 'maxwell', 'plataformas', 'platform_test', 'rpg', 'rymik']:
        doc = json.load(open(os.path.join(RAIZ, 'examples_%s.source2d.json' % ej)))
        for i in range(len(doc['scenes'])):
            casos['%s_%d' % (ej, i)] = escena_primero(doc, i)
    casos.update(variantes_efectos())
    for nombre, doc in casos.items():
        if filtro and filtro not in nombre:
            continue
        pj = os.path.join(outdir, 'proyectos', nombre + '.json')
        json.dump(con_semilla(doc), open(pj, 'w'))
        png = os.path.join(outdir, nombre + '.png')
        if os.path.exists(png):
            os.remove(png)
        env = dict(os.environ, S2D_SHOT=png, S2D_STORE=os.path.join(outdir, 'proyectos', 'datos.json'))
        r = subprocess.run([player, '--fixed-step', '--frames', str(FRAMES), pj], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        estado = 'ok' if os.path.exists(png) else 'SIN CAPTURA'
        err = [l for l in r.stdout.decode('utf-8', 'replace').splitlines() if 'rror' in l or 'Source 2D' in l][:3]
        print('%-22s %s %s' % (nombre, estado, ' | '.join(err)))


if __name__ == '__main__':
    main()
