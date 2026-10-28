#!/usr/bin/env python3
"""Extension Steam sin Steam instalado: "Steam esta disponible" es falso, las acciones (logro, nube, estadistica,
overlay) no hacen nada y no fallan, no hay guardado en la nube y SteamNombre() da texto vacio. Imprime S2DTEST.
Uso: make_steam_project.py <ejemplo.json> <salida.json>"""
import json, os, sys
src, out = sys.argv[1:3]
d = json.load(open(src))
raiz = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
d['extensions'] = [{'id': 'ext_steam', 'name': 'Steam', 'code': open(os.path.join(raiz, 'src', 'editor', 'presets', 'steam.js'), encoding='utf-8').read()}]
C = lambda t, **p: dict(type=t, enabled=True, invert=False, params=p)
NOT = lambda c: dict(c, invert=True)
ev = [dict(id='a', enabled=True, level=0, conditions=[NOT(C('ext:ext_steam:disponible'))], actions=[C('setGlobal', variable='sin', value='1')]),
      dict(id='b', enabled=True, level=0, conditions=[C('atStart')],
           actions=[C('ext:ext_steam:logro', logro='ACH_X'), C('ext:ext_steam:guardarNube', ranura='p1'), C('ext:ext_steam:sumarEstadistica', nombre='m', valor='1'),
                    C('ext:ext_steam:overlay', pagina='friends'), C('ext:ext_steam:cargarNube', ranura='p1'), C('setVarText', variable='nom', text='[{= SteamNombre()}]')]),
      dict(id='c', enabled=True, level=0, conditions=[C('ext:ext_steam:hayNube', ranura='p1')], actions=[C('setGlobal', variable='nube', value='1')]),
      dict(id='d', enabled=True, level=0, conditions=[C('jsCode', code='return $.state.frame===10;')],
           actions=[C('jsCode', code="console.log('S2DTEST '+JSON.stringify({ok: $.vars.sin==1 && !$.vars.nube && $.vars.nom==='[]', sin:$.vars.sin, nube:$.vars.nube, nom:$.vars.nom}));"),
                    C('quitGame')])]
for sc in d['scenes']:
    sc['events'] = []
d['scenes'][0]['events'] = ev
json.dump(d, open(out, 'w'))
