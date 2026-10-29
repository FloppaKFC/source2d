#!/usr/bin/env python3
"""Juego que intenta escribir en Documentos y en su carpeta de datos. Con --tienda <id> (Source Play) lo primero no
debe poder y la carpeta de datos tiene que ser <datos>/tienda/<id>. Imprime "S2DTEST {...}".
Uso: make_sandbox_project.py <salida.json>"""
import json, sys

def C(t, **p):
    return dict(type=t, enabled=True, invert=False, params=p)

code = ("var n=__native,P=n.paths();var a=n.fileWrite(P[2]+'/s2d_prueba.txt','x');var b=n.fileWrite(P[1]+'/partida.txt','y');"
        "console.log('S2DTEST '+JSON.stringify({ok:!a&&b&&/tienda.mi-juego$/.test(P[1]),docs:a,datos:b,dir:P[1]}));")
p = dict(id='p', name='Sandbox', version=3, settings=dict(width=320, height=180), globals=[], sprites=[], tilesets=[], objects=[],
         scenes=[dict(id='s', name='S', width=320, height=180, background='#000', instances=[], tilemaps=[],
                      events=[dict(id='e', conditions=[C('atStart')], actions=[C('jsCode', code=code), C('quitGame')])])], events=[])
json.dump(p, open(sys.argv[1], 'w'))
