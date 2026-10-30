#!/bin/sh
set -e
cd "$(dirname "$0")/.."
ED=${S2D_EDITOR:-./build/source2d}
PL=${S2D_PLAYER:-./build/source2d-player}
if [ -z "$S2D_PLAYER" ] && ! "$PL" --help >/dev/null 2>&1 && [ -x build/lnx/source2d-player ]; then
    echo "(los binarios de build/ no abren en este sistema: se usan los de build/lnx)"
    ED=./build/lnx/Source2D; PL=./build/lnx/source2d-player
fi
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
python3 test/desktop/png_prueba.py "$T/imagen.png"
cp test/proyecto_prueba.source2d.json "$T/proj.json"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out.json|; s|CAPTURA|$T/editor.png|" test/editor_sprites.txt > "$T/guion.txt"
echo "== editor de sprites"
"$ED" --script "$T/guion.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== escuela con Floppa (nota Principal del plataformero: pasos con Estoy listo y pasos que avanzan solos)"
sed "s|CAPTURA\([0-9]\)|$T/floppa\1.png|" test/editor_school2.txt > "$T/guion_school2.txt"
"$ED" --script "$T/guion_school2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== creaciones en Inicio (Source Sprite y Source Sound), arrastrar archivos y objetos, .source2d y notificacion"
mkdir -p "$T/crea/Sprites" "$T/crea/Sonidos"
cp test/proyecto_prueba.source2d.json "$T/crea_plat.json"
sed "s|CARPETA|$T/crea|g; s|PLAT|$T/crea_plat.json|; s|CAPTURA\([0-9]\)|$T/crea\1.png|" test/editor_creaciones.txt > "$T/guion_crea.txt"
S2D_PRUEBA_PROYECTOS="$T/crea" "$ED" --script "$T/guion_crea.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== escuela: conoce el editor, monedas y puntos (Hazlo por mi), musica y despedida al cerrar"
sed "s|CAPTURA\([0-9]\)|$T/escuela3_\1.png|" test/editor_school3.txt > "$T/guion_school3.txt"
"$ED" --script "$T/guion_school3.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== Source Sound (musica y efectos; .wav, .mp3 si hay lame o ffmpeg, y dentro del proyecto)"
cp test/proyecto_prueba.source2d.json "$T/snd.json"
sed "s|PROYECTO|$T/snd.json|; s|SALIDA_WAV|$T/m.wav|; s|SALIDA_MP3|$T/m.mp3|; s|SALIDA_PROY|$T/snd_out.json|; s|CAPTURA_\([A-Z]*\)|$T/snd_\1.png|" test/editor_sound.txt > "$T/guion_snd.txt"
if ! command -v lame >/dev/null && ! command -v ffmpeg >/dev/null; then sed -i '/SALIDA_MP3\|m.mp3/d' "$T/guion_snd.txt"; fi
"$ED" --script "$T/guion_snd.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== mensajes cifrados de extremo a extremo (X25519 + XChaCha20-Poly1305)"
XDG_CONFIG_HOME="$T/cifra" "$ED" --prueba-cifrado | grep CIFRADO
echo "== Noticias con encuesta y Source Play (clave de firma, repositorios y contador de prueba, sin internet)"
make -s build/firma
export S2D_FIRMA_DIR="$T/clave" S2D_FIRMA_CONTRASENA=contrasena-de-prueba
PUB=$(python3 test/desktop/clave_prueba.py "$S2D_FIRMA_CONTRASENA")
mkdir -p "$T/news" "$T/r2/contadores" "$T/spcfg" "$T/paq" "$T/juegos"
S2D_VERSIONES_DIR="$T/news" python3 tools/anunciar.py "Encuesta" 'Elige. {encuesta: "¿Qué quieres?", "Luces", "Red", "Mapas", "3d"} Gracias' --sin-subir >/dev/null
K1=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['noticias'][0]['encuesta']['clave'])" "$T/news/noticias.json")
echo '{"value":5}' > "$T/r2/contadores/$K1-0.json"; echo '{"value":2}' > "$T/r2/contadores/$K1-1.json"
XDG_CONFIG_HOME="$T/spcfg" S2D_NOVEDADES_URL="file://$T/news/" S2D_FIRMA_PUBLICA="$PUB" S2D_R2_URL="file://$T/r2/" \
    "$ED" --script test/editor_encuesta.txt | grep "S2DEDIT FAIL\|S2DEDIT FIN"
cp test/proyecto_prueba.source2d.json "$T/sp.json"
sed "s|PROYECTO|$T/sp.json|; s|PAQ|$T/paq|g; s|MINIATURA|$T/imagen.png|" test/editor_sourceplay.txt > "$T/guion_sp.txt"
XDG_CONFIG_HOME="$T/spcfg" "$ED" --script "$T/guion_sp.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
python3 -c "import struct,sys; d=open(sys.argv[1],'rb').read(24); w,h=struct.unpack('>II',d[16:24]); print('miniatura propia', w, 'x', h, 'OK' if (w,h)==(768,432) else 'FAIL')" "$T/paq/cerrado/"*/portada.png
S2D_JUEGOS_DIR="$T/juegos" S2D_EDITOR="$ED" python3 tools/source_play.py anadir "$T/paq/abierto/"* --sin-subir | grep "Publicado\|OK"
S2D_JUEGOS_DIR="$T/juegos" S2D_EDITOR="$ED" python3 tools/source_play.py anadir "$T/paq/cerrado/"* --autor Otro --sin-subir | grep "Publicado\|OK"
S2D_JUEGOS_DIR="$T/juegos" python3 tools/source_cuentas.py verificar Floppa --sin-subir | grep -v Firmado
S2D_JUEGOS_DIR="$T/juegos" python3 tools/source_cuentas.py admin Otro --sin-subir | grep -v Firmado
S2D_JUEGOS_DIR="$T/juegos" python3 tools/source_cuentas.py creador FloppaKFC --sin-subir | grep -v Firmado
S2D_JUEGOS_DIR="$T/juegos" python3 tools/source_play.py destacar otro-aventura-de-plataformas --sin-subir | grep -v Firmado
sed "s|CAPTURA|$T/sourceplay.png|" test/editor_sourceplay2.txt > "$T/guion_sp2.txt"
XDG_CONFIG_HOME="$T/spcfg" S2D_TIENDA_URL="file://$T/juegos/" S2D_FIRMA_PUBLICA="$PUB" "$ED" --script "$T/guion_sp2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== Source Store: crear paquetes de sprites y sonidos, publicarlos, descargarlos y anadirlos a un proyecto"
mkdir -p "$T/stpaq" "$T/stsp" "$T/stso" "$T/stcfg" "$T/stcfg2"
cp test/proyecto_prueba.source2d.json "$T/st.json"
sed "s|PROYECTO|$T/st.json|; s|PAQ|$T/stpaq|g" test/editor_store.txt > "$T/guion_st.txt"
XDG_CONFIG_HOME="$T/stcfg" "$ED" --script "$T/guion_st.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
S2D_SPRITES_DIR="$T/stsp" S2D_EDITOR="$ED" python3 tools/source_store.py sprites anadir "$T/stpaq/sprites/"* --sin-subir | grep "Publicado\|CONTENIDO"
S2D_SONIDOS_DIR="$T/stso" S2D_EDITOR="$ED" python3 tools/source_store.py sonidos anadir "$T/stpaq/sonidos/"* --sin-subir | grep "Publicado\|CONTENIDO"
cp test/proyecto_prueba.source2d.json "$T/st2.json"
sed "s|PROYECTO|$T/st2.json|; s|CAPTURA|$T/store.png|" test/editor_store2.txt > "$T/guion_st2.txt"
XDG_CONFIG_HOME="$T/stcfg2" S2D_STORE_SPRITES_URL="file://$T/stsp/" S2D_STORE_SONIDOS_URL="file://$T/stso/" S2D_FIRMA_PUBLICA="$PUB" \
    S2D_R2_URL="file://$T/r2/" "$ED" --script "$T/guion_st2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
NODE=${S2D_NODE:-$(command -v node || true)}
if [ -n "$NODE" ]; then
echo "== Worker de Source 2D (R2 y GitHub de mentira)"
"$NODE" test/worker_prueba.mjs
echo "== Publicar con el Worker de verdad en local (entrar con GitHub de mentira, envio, rechazo en tiempo real, registro, perfil, seguir y denunciar)"
python3 test/desktop/github_falso.py 47790 "$T/ghsub" 2>/dev/null & GHP=$!
mkdir -p "$T/ghsub" "$T/ghcfg" "$T/wr2/cuentas" "$T/wr2-meta" "$T/wpriv"; cp "$T/imagen.png" "$T/ghsub/avatar.png"
echo '{"login":"amiga","unido":"2026-10-01T09:00:00Z","beta":true}' > "$T/wr2/cuentas/amiga.json"
echo '{"login":"amiga","unido":"2026-10-01T09:00:00Z","beta":"1"}' > "$T/wr2-meta/cuentas%2Famiga.json"
"$NODE" test/worker_servidor.mjs 47791 "$T/wr2" "$T/wpriv" http://127.0.0.1:47790 >/dev/null 2>&1 & WKP=$!
python3 test/desktop/revisor_falso.py "$T/wpriv" "falta un final" & RVP=$!
sleep 0.8
sed "s|PROYECTO|$T/sp.json|; s|CAPTURA|$T/perfil.png|" test/editor_envio.txt > "$T/guion_envio.txt"
XDG_CONFIG_HOME="$T/ghcfg" S2D_GITHUB_API=http://127.0.0.1:47790 S2D_GITHUB_WEB=http://127.0.0.1:47790 S2D_GITHUB_CLIENT_ID=cliente-de-prueba \
    S2D_API_URL=http://127.0.0.1:47791 S2D_R2_URL="file://$T/wr2/" "$ED" --script "$T/guion_envio.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
ls "$T/wpriv/denuncias/"*.json >/dev/null 2>&1 && echo "denuncia guardada en el bucket privado" || echo "S2DEDIT FAIL la denuncia no llego"
[ -s "$T/wr2/cuentas/creador.json" ] && echo "cuenta registrada en R2" || echo "S2DEDIT FAIL registro"
kill $GHP $WKP $RVP 2>/dev/null || true
else echo "(sin Node: no se prueban el Worker ni los envios; S2D_NODE elige uno)"; fi
echo "== admin.py: aprobar un envío, insignias, contribución, baneo y solicitudes (R2 local)"
A="$T/adm"; EID=mvaaaaaa-0a1b2c3d
mkdir -p "$A/privado/envios/floppa/$EID"
cp "$T/paq/abierto/"*/* "$A/privado/envios/floppa/$EID/"
python3 -c "import json,os,sys; d=sys.argv[1]; json.dump({'id':sys.argv[2],'tipo':'juegos','nombre':'Juego de prueba','autor':'Floppa','fecha':'2026-10-29T10:00:00Z','estado':'pendiente','archivos':{f:os.path.getsize(os.path.join(d,f)) for f in os.listdir(d)}},open(os.path.join(d,'estado.json'),'w'))" "$A/privado/envios/floppa/$EID" $EID
export S2D_R2_LOCAL="$A" S2D_EDITOR="$ED"
python3 tools/admin.py envios | grep -q "floppa/$EID" && echo "envío pendiente listado" || echo "S2DEDIT FAIL admin envios"
python3 tools/admin.py aprobar "floppa/$EID" | grep "Publicado"
python3 tools/admin.py insignia Floppa verificada | grep -v "firma\|Firmado"
python3 tools/admin.py contribucion Floppa "Propuso la prueba de admin" | grep -v "firma\|Firmado"
python3 tools/admin.py banear Floppa 3 "Prueba"
python3 -c "import json,sys; e=json.load(open(sys.argv[1])); print('envío aprobado' if e['estado']=='aprobado' else 'S2DEDIT FAIL estado '+e['estado'])" "$A/privado/envios/floppa/$EID/estado.json"
ls "$A/privado/notificaciones/floppa/" | wc -l | grep -q "^4$" && echo "4 notificaciones (aprobado, insignia, contribución, baneo)" || echo "S2DEDIT FAIL notificaciones"
[ -s "$A/privado/baneos/floppa.json" ] && echo "baneo guardado" || echo "S2DEDIT FAIL baneo"
python3 tools/admin.py desbanear Floppa
cp "$A/publico/cuentas.json" "$A/publico/cuentas.json.firma" "$A/publico/juegos/"
printf 'home.section 1\nwait 20\ntienda.espera\ntienda.expect 1\ntienda.insignia Floppa 4\nquit\n' > "$T/guion_adm.txt"
XDG_CONFIG_HOME="$T/admcfg" S2D_TIENDA_URL="file://$A/publico/juegos/" S2D_FIRMA_PUBLICA="$PUB" "$ED" --script "$T/guion_adm.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
unset S2D_R2_LOCAL S2D_EDITOR
unset S2D_FIRMA_DIR S2D_FIRMA_CONTRASENA
echo "== extension Steam (se anade y aparecen sus condiciones y acciones; sin Steam el juego sigue)"
sed "s|PROYECTO|$T/proj.json|" test/editor_steam.txt > "$T/guion_steam.txt"
"$ED" --script "$T/guion_steam.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== Source Sprite: herramientas y efectos nuevos (degradado, contorno, efectos, capas, escalar)"
sed "s|PROYECTO|$T/proj.json|" test/editor_sprites2.txt > "$T/guion_s2.txt"
"$ED" --script "$T/guion_s2.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== objetos y escena"
sed "s|PROYECTO|$T/proj.json|; s|SALIDA|$T/out2.json|; s|CAPTURA_ESCENA|$T/escena.png|; s|CAPTURA_OBJETO|$T/objeto.png|; s|CAPTURA_ANIM|$T/anim.png|" test/editor_scene.txt > "$T/guion2.txt"
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
python3 -c "import sys,json; j=json.load(open(sys.argv[1])); j['extensions']=[{'id':'x','name':'Bucle','code':'while(true){}'}]; json.dump(j,open(sys.argv[2],'w'))" test/proyecto_prueba.source2d.json "$T/ajeno.json"
sed "s|PROYECTO|$T/ajeno.json|" test/editor_trust.txt > "$T/guion_ajeno.txt"
timeout 60 "$ED" --script "$T/guion_ajeno.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== trabajar juntos (dos editores por 127.0.0.1: cambios en los dos sentidos, cursores, edicion a la vez, codigo incorrecto)"
cp test/proyecto_prueba.source2d.json "$T/colab.json"
sed "s|PROYECTO|$T/colab.json|; s|CAPTURA_ANA|$T/colab_ana.png|" test/editor_colab_host.txt > "$T/colab_h.txt"
sed "s|CODIGO|482913|; s|CAPTURA_BETO|$T/colab_beto.png|; s|GUARDAR_BETO|$T/beto_copia.json|" test/editor_colab_guest.txt > "$T/colab_g.txt"
S2D_COLAB_CODE=482913 timeout 120 "$ED" --script "$T/colab_h.txt" > "$T/colab_h.log" 2>&1 &
timeout 120 "$ED" --script "$T/colab_g.txt" > "$T/colab_g.log" 2>&1
wait
grep -h "S2DEDIT FAIL\|S2DEDIT FIN" "$T/colab_h.log" "$T/colab_g.log"
va=$(grep "colab valor" "$T/colab_h.log" | cut -d' ' -f5-); vb=$(grep "colab valor" "$T/colab_g.log" | cut -d' ' -f5-)
[ -n "$va" ] && [ "$va" = "$vb" ] && echo "edicion a la vez: los dos quedan con $va" || echo "S2DEDIT FAIL edicion a la vez: anfitrion $va, invitado $vb"
echo "== trabajar juntos 2 (trazo en vivo en un sprite, cursor y evento seleccionado en la hoja de eventos)"
cp test/proyecto_prueba.source2d.json "$T/colab2.json"
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
sed "s|SALIDA_NADA|$T/nada|; s|SALIDA_NORMAL|$T/juego_normal|; s|SALIDA_LIGERO|$T/juego_ligero|; s|SALIDA_WIN|$T/juego_ligero.exe|; s|SALIDA_HTML|$T/juego.html|; s|SALIDA_ZIP|$T/juego.zip|; s|SALIDA_CARPETA|$T/Carpeta|" test/editor_export.txt > "$T/guion10.txt"
"$ED" --script "$T/guion10.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
[ -e "$T/nada" ] && echo "S2DEDIT FAIL exporto sin proyecto abierto"
for f in "$T/juego_ligero"; do
    n=$(wc -c < "$f"); [ "$n" -le 1048576 ] && echo "$(basename "$f"): $n bytes (<= 1 MB)" || echo "S2DEDIT FAIL $(basename "$f") ocupa $n bytes"
done
for f in "$T/juego_ligero.exe"; do
    n=$(wc -c < "$f")
    if tail -c 8 "$f" | grep -q S2DPAYLD && [ "$n" -le 2621440 ]; then echo "$(basename "$f"): $n bytes, sin lanzador"; else echo "S2DEDIT FAIL $(basename "$f") ($n bytes) lleva lanzador o es demasiado grande"; fi
done
grep -q 'id="s2d-proyecto"' "$T/juego.html" && echo "juego.html con el proyecto dentro" || echo "S2DEDIT FAIL juego.html"
unzip -tq "$T/juego.zip" >/dev/null && unzip -l "$T/juego.zip" | grep -q " index.html" && echo "juego.zip con index.html" || echo "S2DEDIT FAIL juego.zip"
for f in juego_normal juego_ligero Carpeta-linux/Carpeta.x86_64; do
    S2D_SHOT="$T/$(basename "$f").png" timeout 20 "$T/$f" --frames 60 >/dev/null 2>&1 && [ -s "$T/$(basename "$f").png" ] && echo "$f abre" || echo "S2DEDIT FAIL $f no abre"
done
[ -s "$T/Carpeta-linux/datos/juego.source2d.json" ] && echo "carpeta con datos/juego.source2d.json" || echo "S2DEDIT FAIL exportar a carpeta"
echo "== plantillas (Ver por dentro)"
sed "s|CAPTURA_\([A-Z]*\)|$T/ej_\1.png|" test/editor_examples.txt > "$T/guion8.txt"
"$ED" --script "$T/guion8.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
echo "== fuentes (de pixeles e importadas)"
cp test/proyecto_prueba.source2d.json "$T/pf.json"
sed "s|PROYECTO|$T/pf.json|; s|SALIDA|$T/pf_fonts.json|; s|MEDIA|$PWD/third_party/outfit/Outfit-SemiBold.ttf|; s|CAPTURA_\([A-Z]*\)|$T/f_\1.png|" test/editor_fonts.txt > "$T/guion9.txt"
"$ED" --script "$T/guion9.txt" | grep "S2DEDIT FAIL\|S2DEDIT FIN"
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); k=sorted(f['kind'] for f in d.get('fonts', [])); u=[o['text'].get('font','') for o in d['objects'] if o['type']=='text']; print('fuentes guardadas' if k==['pixel','ttf'] and all(u) else 'S2DEDIT FAIL fuentes: %s %s' % (k, u))" "$T/pf_fonts.json"
echo "== runtime: plataformero (doble salto, dash, eventos, pinchos, capas no solidas, caja fija, salir)"
python3 test/desktop/make_platformer_project.py "$T/plat.json"
timeout 30 "$PL" --fixed-step "$T/plat.json" | grep S2DTEST
echo "== runtime: Beta 0.1 (esperar, mensajes, pausa, tweens, mando, grupos, rayos, pausa de impacto; normal y con paso fijo)"
python3 test/desktop/make_beta_project.py test/proyecto_prueba.source2d.json "$T/beta.json"
timeout 60 "$PL" --fixed-step "$T/beta.json" | grep S2DTEST
python3 -c "import json,sys; d=json.load(open(sys.argv[1])); d['settings']['fixedStep']=True; json.dump(d,open(sys.argv[2],'w'))" "$T/beta.json" "$T/beta_fx.json"
timeout 60 "$PL" --fixed-step "$T/beta_fx.json" | grep S2DTEST
echo "== runtime: extension Steam sin Steam (todo responde que no, nada falla)"
python3 test/desktop/make_steam_project.py test/proyecto_prueba.source2d.json "$T/steam.json"
timeout 60 "$PL" --fixed-step "$T/steam.json" | grep S2DTEST
echo "== runtime: colisiones con forma (caja de agachado, circulos exactos, rampa de tiles)"
python3 test/desktop/make_collision_project.py "$T/colisiones.json"
timeout 30 "$PL" --fixed-step "$T/colisiones.json" | grep S2DTEST
echo "== runtime: juego de Source Play (--tienda): no escribe en Documentos y sus partidas van aparte"
python3 test/desktop/make_sandbox_project.py "$T/sandbox.json"
mkdir -p "$T/sbhome/Documents"
HOME="$T/sbhome" XDG_DATA_HOME="$T/sbhome/.local/share" timeout 15 "$PL" --tienda mi-juego --stdin < "$T/sandbox.json" 2>/dev/null | grep S2DTEST
echo "== runtime: objetos y eventos por escena"
python3 test/desktop/make_scenes_project.py "$T/escenas.json"
timeout 15 "$PL" "$T/escenas.json" | grep S2DTEST
echo "== runtime: eventos (fase 2) y comportamientos (fase 3)"
echo "== runtime: lenguaje de eventos (funciones, locales, arrays)"
python3 test/desktop/make_lang_project.py test/proyecto_prueba.source2d.json "$T/lang.json"
timeout 15 "$PL" "$T/lang.json" | grep S2DTEST
python3 test/desktop/make_events_project.py test/proyecto_prueba.source2d.json "$T/ev.json"
python3 test/desktop/make_behaviors_project.py test/proyecto_prueba.source2d.json "$T/bh.json"
for p in ev bh; do timeout 15 "$PL" "$T/$p.json" | grep S2DTEST; done
