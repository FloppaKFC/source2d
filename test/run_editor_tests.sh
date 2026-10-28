#!/bin/sh
# Pruebas automaticas del editor nativo (sprites, escena, eventos, tiles y documentos del proyecto) y del runtime en el jugador nativo.
# Uso: test/run_editor_tests.sh   (desde source2d/, con build/ compilado)
set -e
cd "$(dirname "$0")/.."
# editor y jugador: los de "make" (gcc del sistema) o, si no abren aqui (glibc mas vieja, como en Ubuntu), los
# portables de Makefile.linux; S2D_EDITOR y S2D_PLAYER eligen otros
ED=${S2D_EDITOR:-./build/source2d}
PL=${S2D_PLAYER:-./build/source2d-player}
if [ -z "$S2D_PLAYER" ] && ! "$PL" --help >/dev/null 2>&1 && [ -x build/lnx/source2d-player ]; then
    echo "(los binarios de build/ no abren en este sistema: se usan los de build/lnx)"
    ED=./build/lnx/Source2D; PL=./build/lnx/source2d-player
fi
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cp examples_plataformas.source2d.json "$T/proj.json"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out.json|; s|CAPTURA|$T/editor.png|" test/editor_sprites.txt > "$T/guion.txt"
echo "== editor de sprites"
"$ED" --script "$T/guion.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== escuela (la leccion 1 avanza sola al crear, dibujar, colocar y probar)"
sed "s|CAPTURA|$T/escuela.png|" test/editor_school.txt > "$T/guion_school.txt"
"$ED" --script "$T/guion_school.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== extension Steam (se anade y aparecen sus condiciones y acciones; sin Steam el juego sigue)"
sed "s|PROYECTO|$T/proj.json|" test/editor_steam.txt > "$T/guion_steam.txt"
"$ED" --script "$T/guion_steam.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== Source Sprite: herramientas y efectos nuevos (degradado, contorno, efectos, capas, escalar)"
sed "s|PROYECTO|$T/proj.json|" test/editor_sprites2.txt > "$T/guion_s2.txt"
"$ED" --script "$T/guion_s2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== objetos y escena"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out2.json|; s|CAPTURA_ESCENA|$T/escena.png|; s|CAPTURA_OBJETO|$T/objeto.png|" test/editor_scene.txt > "$T/guion2.txt"
"$ED" --script "$T/guion2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== hoja de eventos"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out3.json|; s|CAPTURA1|$T/d1.png|; s|CAPTURA2|$T/d2.png|; s|CAPTURA3|$T/d3.png|" test/editor_events.txt > "$T/guion3.txt"
"$ED" --script "$T/guion3.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== conjuntos de tiles, musica, ajustes, shaders, extensiones e importar imagen"
python3 test/desktop/make_test_media.py "$T"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out4.json|; s|MEDIA|$T|g; s|CAPTURA_\([A-Z]*\)|$T/\1.png|" test/editor_docs.txt > "$T/guion4.txt"
"$ED" --script "$T/guion4.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== barra superior"
sed "s|PROYECTO|$T/proj.json|; s|CAPTURA_\([A-Z]*\)|$T/\1.png|" test/editor_toolbar.txt > "$T/guion5.txt"
"$ED" --script "$T/guion5.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== proyecto ajeno (aviso de confianza; una extension con un bucle sin fin no cuelga el editor)"
python3 -c "import sys,json; j=json.load(open(sys.argv[1])); j['extensions']=[{'id':'x','name':'Bucle','code':'while(true){}'}]; json.dump(j,open(sys.argv[2],'w'))" examples_plataformas.source2d.json "$T/ajeno.json"
sed "s|PROYECTO|$T/ajeno.json|" test/editor_trust.txt > "$T/guion_ajeno.txt"
timeout 60 "$ED" --script "$T/guion_ajeno.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== trabajar juntos (dos editores por 127.0.0.1: cambios en los dos sentidos, cursores, edicion a la vez, codigo incorrecto)"
cp examples_plataformas.source2d.json "$T/colab.json"
sed "s|PROYECTO|$T/colab.json|; s|CAPTURA_ANA|$T/colab_ana.png|" test/editor_colab_host.txt > "$T/colab_h.txt"
sed "s|CODIGO|482913|; s|CAPTURA_BETO|$T/colab_beto.png|" test/editor_colab_guest.txt > "$T/colab_g.txt"
S2D_COLAB_CODE=482913 timeout 120 "$ED" --script "$T/colab_h.txt" > "$T/colab_h.log" 2>&1 &
timeout 120 "$ED" --script "$T/colab_g.txt" > "$T/colab_g.log" 2>&1
wait
grep -h "S2DEDIT FAIL\|S2DEDIT FIN" "$T/colab_h.log" "$T/colab_g.log"
va=$(grep "colab valor" "$T/colab_h.log" | cut -d' ' -f5-); vb=$(grep "colab valor" "$T/colab_g.log" | cut -d' ' -f5-)
[ -n "$va" ] && [ "$va" = "$vb" ] && echo "edicion a la vez: los dos quedan con $va" || echo "S2DEDIT FAIL edicion a la vez: anfitrion $va, invitado $vb"
echo "== trabajar juntos 2 (trazo en vivo en un sprite, cursor y evento seleccionado en la hoja de eventos)"
cp examples_plataformas.source2d.json "$T/colab2.json"
sed "s|PROYECTO|$T/colab2.json|" test/editor_colab2_host.txt > "$T/colab2_h.txt"
sed "s|CODIGO|482913|" test/editor_colab2_guest.txt > "$T/colab2_g.txt"
S2D_COLAB_CODE=482913 timeout 150 "$ED" --script "$T/colab2_h.txt" > "$T/colab2_h.log" 2>&1 &
timeout 150 "$ED" --script "$T/colab2_g.txt" > "$T/colab2_g.log" 2>&1
wait
grep -h "S2DEDIT FAIL\|S2DEDIT FIN" "$T/colab2_h.log" "$T/colab2_g.log"
echo "== pagina de inicio"
sed "s|CAPTURA_\([A-Z]*\)|$T/\1.png|" test/editor_home.txt > "$T/guion6.txt"
"$ED" --script "$T/guion6.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== Source Sprite (lazo, varita, recortes, cortar hoja, exportar, paletas de Aseprite)"
mkdir -p "$T/studio" && python3 test/studio_data.py "$T/studio"
sed "s|DIR|$T/studio|g" test/editor_studio.txt > "$T/guion11.txt"
"$ED" --script "$T/guion11.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
python3 -c "import sys,json; j=json.load(open(sys.argv[1])); n=len(j['frames']); print('hoja con JSON de Aseprite: %d frames' % n if n == 4 else 'S2DEDIT FAIL JSON de la hoja: %d frames' % n)" "$T/studio/out_h.json"
echo "== exportar (normal y ligera, Linux y Windows; la ligera de un juego pequeno, en menos de 1 MB)"
sed "s|SALIDA_NADA|$T/nada|; s|SALIDA_NORMAL|$T/juego_normal|; s|SALIDA_LIGERO|$T/juego_ligero|; s|SALIDA_WIN|$T/juego_ligero.exe|; s|SALIDA_HTML|$T/juego.html|; s|SALIDA_ZIP|$T/juego.zip|" test/editor_export.txt > "$T/guion10.txt"
"$ED" --script "$T/guion10.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
[ -e "$T/nada" ] && echo "S2DEDIT FAIL exporto sin proyecto abierto"
for f in "$T/juego_ligero" "$T/juego_ligero.exe"; do
    n=$(wc -c < "$f"); [ "$n" -le 1048576 ] && echo "$(basename "$f"): $n bytes (<= 1 MB)" || echo "S2DEDIT FAIL $(basename "$f") ocupa $n bytes"
done
grep -q 'id="s2d-proyecto"' "$T/juego.html" && echo "juego.html con el proyecto dentro" || echo "S2DEDIT FAIL juego.html"
unzip -tq "$T/juego.zip" >/dev/null && unzip -l "$T/juego.zip" | grep -q " index.html" && echo "juego.zip con index.html" || echo "S2DEDIT FAIL juego.zip"
for f in juego_normal juego_ligero; do
    S2D_SHOT="$T/$f.png" timeout 20 "$T/$f" --frames 60 >/dev/null 2>&1 && [ -s "$T/$f.png" ] && echo "$f abre" || echo "S2DEDIT FAIL $f no abre"
done
echo "== juegos de prueba (apartado Prueba)"
sed "s|CAPTURA_\([A-Z]*\)|$T/ej_\1.png|" test/editor_examples.txt > "$T/guion8.txt"
"$ED" --script "$T/guion8.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== fuentes (de pixeles e importadas)"
cp examples_plataformas.source2d.json "$T/pf.json"
sed "s|PROYECTO|$T/pf.json|; s|SALIDA|$T/pf_fonts.json|; s|MEDIA|$PWD/third_party/outfit/Outfit-SemiBold.ttf|; s|CAPTURA_\([A-Z]*\)|$T/f_\1.png|" test/editor_fonts.txt > "$T/guion9.txt"
"$ED" --script "$T/guion9.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); k=sorted(f['kind'] for f in d.get('fonts', [])); u=[o['text'].get('font','') for o in d['objects'] if o['type']=='text']; print('fuentes guardadas' if k==['pixel','ttf'] and all(u) else 'S2DEDIT FAIL fuentes: %s %s' % (k, u))" "$T/pf_fonts.json"
echo "== ajedrez en el editor (eventos, escena, guardar)"
sed "s|PROYECTO|$T/ajedrez.json|; s|SALIDA|$T/ajedrez2.json|; s|CAPTURA_\([A-Z]*\)|$T/aj_\1.png|" test/editor_chess.txt > "$T/guion7.txt"
cp examples_ajedrez.source2d.json "$T/ajedrez.json"
"$ED" --script "$T/guion7.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
python3 -c "import json,sys; a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2])); [e.pop('collapsed', None) for d in (a, b) for l in [d['events']] + [x.get('events', []) for x in d['scenes']] for e in l]; print('guardado igual' if a['events']==b['events'] and a['scenes']==b['scenes'] and a['globals']==b['globals'] and a['shaders']==b['shaders'] else 'S2DEDIT FAIL el guardado cambia el proyecto')" "$T/ajedrez.json" "$T/ajedrez2.json"
echo "== runtime: Maxwell (giro 2D por eventos, TiempoDelta, zoom)"
python3 test/desktop/make_maxwell_test.py examples_maxwell.source2d.json "$T/maxwell.json"
timeout 15 "$PL" "$T/maxwell.json" | grep S2DTEST
echo "== runtime: plataformero (doble salto, dash, eventos, pinchos, capas no solidas, caja fija, salir)"
python3 test/desktop/make_platformer_project.py "$T/plat.json"
timeout 30 "$PL" --fixed-step "$T/plat.json" | grep S2DTEST
echo "== runtime: Platform Test: los 3 niveles se completan (rutas de tools/solve_platform.py en el motor real)"
for n in 1 2 3; do
  python3 test/desktop/make_platform_replay.py examples_platform_test.source2d.json test/desktop/platform_test_ruta.json $n "$T/pt$n.json"
  timeout 180 "$PL" --fixed-step "$T/pt$n.json" | grep S2DTEST
done
echo "== runtime: Project Ry-mik (menu y casetes, Terra perfecto con record, rebobinar, editor de niveles y exportar, ajustes e importar)"
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); f=[x for x in d['files'] if 'Citadel' in x['name']][0]; open(sys.argv[2],'w').write(f['text'])" examples_rymik.source2d.json "$T/citadel.json"
for m in "menu" "nivel 3" "rebobinar" "editor" "ajustes $T/citadel.json"; do
  H=$(mktemp -d -p "$T")
  python3 test/desktop/make_rymik_test.py examples_rymik.source2d.json "$H/t.json" $m
  env -u XDG_DATA_HOME HOME="$H" S2D_STORE="$H/store.json" timeout 600 "$PL" --fixed-step --uncapped "$H/t.json" | grep S2DTEST
done
echo "== rendimiento (Prueba de rendimiento: fps sin limite en CPU, GPU y fisica; solo informa, no falla)"
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); d['globals']=[dict(g, value=1) if g['name']=='salir' else g for g in d['globals']]; json.dump(d,open(sys.argv[2],'w'))" examples_benchmark.source2d.json "$T/bench.json"
timeout 120 "$PL" --uncapped "$T/bench.json" | grep S2DBENCH
echo "== runtime: Beta 0.1 (esperar, mensajes, pausa, tweens, mando, grupos, rayos, pausa de impacto; normal y con paso fijo)"
python3 test/desktop/make_beta_project.py examples_plataformas.source2d.json "$T/beta.json"
timeout 60 "$PL" --fixed-step "$T/beta.json" | grep S2DTEST
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); d['settings']['fixedStep']=True; json.dump(d,open(sys.argv[2],'w'))" "$T/beta.json" "$T/beta_fx.json"
timeout 60 "$PL" --fixed-step "$T/beta_fx.json" | grep S2DTEST
echo "== runtime: extension Steam sin Steam (todo responde que no, nada falla)"
python3 test/desktop/make_steam_project.py examples_plataformas.source2d.json "$T/steam.json"
timeout 60 "$PL" --fixed-step "$T/steam.json" | grep S2DTEST
echo "== runtime: objetos y eventos por escena"
python3 test/desktop/make_scenes_project.py "$T/escenas.json"
timeout 15 "$PL" "$T/escenas.json" | grep S2DTEST
echo "== runtime: eventos (fase 2) y comportamientos (fase 3)"
echo "== runtime: lenguaje de eventos (funciones, locales, arrays) y ajedrez (perft y bot)"
python3 test/desktop/make_lang_project.py examples_plataformas.source2d.json "$T/lang.json"
timeout 15 "$PL" "$T/lang.json" | grep S2DTEST
python3 test/desktop/make_chess_test.py examples_ajedrez.source2d.json "$T/chess.json"
timeout 120 "$PL" "$T/chess.json" | grep S2DTEST
python3 test/desktop/make_events_project.py examples_plataformas.source2d.json "$T/ev.json"
python3 test/desktop/make_behaviors_project.py examples_plataformas.source2d.json "$T/bh.json"
for p in ev bh; do timeout 15 "$PL" "$T/$p.json" | grep S2DTEST; done
