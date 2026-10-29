#!/usr/bin/env bash
# Source 2D - menu para publicar en Inicio > Noticias sin recordar opciones: actualizar el juego (version nueva),
# dar una noticia (importante, titular o normal, con imagen, texto de debajo y notificacion), editarla, borrarla o verlas.
# Cada opcion explica en pantalla lo que hace antes de preguntar nada.
# Usa tools/anunciar.py y tools/publicar_version.py, que firman con tu clave (piden la contrasena) y suben con git.
#   tools/noticias.sh            (desde cualquier carpeta)
#   S2D_SIN_SUBIR=1 tools/noticias.sh   firma pero no sube (para probar)
set -u
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PY=python3
ANUNCIAR="$RAIZ/tools/anunciar.py"
PUBLICAR="$RAIZ/tools/publicar_version.py"
EXTRA=()
[ "${S2D_SIN_SUBIR:-}" = "1" ] && EXTRA+=(--sin-subir)

if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; V=$'\e[38;5;141m'; R=$'\e[31m'; N=$'\e[0m'; else B= D= V= R= N=; fi

titulo() { printf '\n%s== %s ==%s\n' "$V$B" "$1" "$N"; }
pedir() {   # pedir "Pregunta" variable [por defecto]
    local p="$1" def="${3:-}" r
    if [ -n "$def" ]; then read -e -r -p "$p [$def]: " r; r="${r:-$def}"; else read -e -r -p "$p: " r; fi
    printf -v "$2" '%s' "$r"
}
si_no() {   # si_no "Pregunta" (s por defecto si $2 = s)
    local r def="${2:-n}"
    read -r -p "$1 [$( [ "$def" = s ] && echo S/n || echo s/N )]: " r
    r="${r:-$def}"
    [[ "$r" =~ ^[sSyY] ]]
}
expandir() {   # ~ y $HOME
    local p="$1"
    p="${p/#\~/$HOME}"
    printf '%s' "$p"
}

# Pide una imagen: acepta un archivo o una carpeta (entonces lista sus imagenes para elegir una)
pedir_imagen() {
    local donde f i lista=()
    IMAGEN=""
    echo "${D}¿Dónde está la imagen? Puedes escribir el archivo (~/Descargas/foto.png) o solo la carpeta (~/Descargas).${N}"
    while :; do
        pedir "Imagen (vacío = sin imagen)" donde
        [ -z "$donde" ] && return 0
        donde="$(expandir "$donde")"
        if [ -d "$donde" ]; then
            lista=()
            while IFS= read -r f; do lista+=("$f"); done < <(find "$donde" -maxdepth 1 -type f \( -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.gif' -o -iname '*.bmp' \) -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -30 | cut -d' ' -f2-)
            if [ ${#lista[@]} -eq 0 ]; then echo "${R}No hay imágenes (.png .jpg .gif .bmp) en $donde${N}"; continue; fi
            echo "Imágenes en $donde (las más nuevas primero):"
            for i in "${!lista[@]}"; do printf '  %2d) %s\n' "$((i + 1))" "$(basename "${lista[$i]}")"; done
            pedir "Número" i
            if [[ "$i" =~ ^[0-9]+$ ]] && [ "$i" -ge 1 ] && [ "$i" -le ${#lista[@]} ]; then IMAGEN="${lista[$((i - 1))]}"; return 0; fi
            echo "${R}Número no válido${N}"
        elif [ -f "$donde" ]; then
            case "${donde,,}" in *.png|*.jpg|*.jpeg|*.gif|*.bmp) IMAGEN="$donde"; return 0 ;; esac
            echo "${R}Tiene que ser .png, .jpg, .gif o .bmp${N}"
        else
            echo "${R}No existe: $donde${N}"
        fi
    done
}

ayuda_texto() {
    echo "${D}Para una lista con puntos escribe {lista:Uno, Dos, Tres}. Lo que quede fuera de las llaves sale como texto normal."
    echo "Ejemplo: {lista:Luces, Autotiles, Capas} Esto es una lista de los nuevos cambios"
    echo "Para una encuesta: {encuesta: \"Pregunta\", \"Opción 1\", \"Opción 2\", \"3d\"} (de 2 a 10 opciones)."
    echo "Lo último es cuánto dura: 30m, 12h, 3d, 2 semanas o una fecha (2026-12-31 18:00); sin eso o con \"\", no se cierra."
    echo "Los votos se ven con la opción 6 del menú.${N}"
}

elegir_tipo() {   # TIPO_FLAGS segun importante / titular / normal, el tema y si notifica
    echo "¿Qué tipo de noticia es?"
    echo "  1) Importante  ${D}sale en una ventana en medio del editor la próxima vez que alguien lo abra (solo una vez)${N}"
    echo "  2) Titular     ${D}se queda fijada arriba del todo en Inicio > Noticias, encima de las demás${N}"
    echo "  3) Normal      ${D}una tarjeta más en Inicio > Noticias, de la más nueva a la más vieja${N}"
    local t; pedir "Tipo" t 3
    TIPO_FLAGS=()
    case "$t" in 1) TIPO_FLAGS=(--importante) ;; 2) TIPO_FLAGS=(--fijar) ;; esac
    echo "¿De qué trata? ${D}(solo cambia el color de la franja y la etiqueta de la tarjeta)${N}"
    echo "  1) Aviso ${D}(naranja)${N}  2) Evento ${D}(morado)${N}  3) Actualización ${D}(verde)${N}  4) Consejo ${D}(azul)${N}"
    local c; pedir "Tema" c 1
    case "$c" in 2) TIPO_FLAGS+=(--tipo evento) ;; 3) TIPO_FLAGS+=(--tipo actualizacion) ;; 4) TIPO_FLAGS+=(--tipo consejo) ;; *) TIPO_FLAGS+=(--tipo aviso) ;; esac
    echo "${D}Notificar: a quien tenga el editor abierto con internet le baja una notificación por arriba con el título"
    echo "(en unos segundos: el editor mira cada 5 s si hay algo nuevo). Al pulsarla va a Inicio > Noticias.${N}"
    si_no "¿Notificar?" && TIPO_FLAGS+=(--notificar)
    return 0
}

dar_noticia() {
    titulo "Dar noticia"
    echo "${D}Crea una noticia nueva para Inicio > Noticias de todos los editores. Te pide el tipo, el título, el texto,"
    echo "una imagen opcional, el texto de debajo de la imagen y un enlace. Al final la firma con tu clave (pide la"
    echo "contraseña) y la sube al repositorio público; los editores la ven en segundos.${N}"
    elegir_tipo
    local t x inf enl
    while :; do pedir "Título" t; [ -n "$t" ] && break; echo "${R}El título no puede ir vacío${N}"; done
    ayuda_texto
    pedir "Texto (debajo del título)" x
    pedir_imagen
    echo "${D}Inferior: lo que sale debajo de la imagen (también admite {lista:...}).${N}"
    pedir "Inferior (vacío = nada)" inf
    pedir "Enlace https:// (vacío = ninguno)" enl
    local args=("$t" "$x" "${TIPO_FLAGS[@]}")
    [ -n "$IMAGEN" ] && args+=(--imagen "$IMAGEN")
    [ -n "$inf" ] && args+=(--inferior "$inf")
    [ -n "$enl" ] && args+=(--enlace "$enl")
    titulo "Así quedará"
    echo "${B}$t${N}"
    [ -n "$x" ] && echo "$x"
    [ -n "$IMAGEN" ] && echo "${D}[imagen: $IMAGEN]${N}"
    [ -n "$inf" ] && echo "$inf"
    [ -n "$enl" ] && echo "${D}[enlace: $enl]${N}"
    si_no "¿Publicar?" s || { echo "Cancelado."; return; }
    "$PY" "$ANUNCIAR" "${args[@]}" "${EXTRA[@]}"
}

editar_noticia() {
    titulo "Editar noticia"
    echo "${D}Cambia una noticia ya publicada: elige su número de la lista y escribe solo lo que quieras cambiar"
    echo "(lo que dejes vacío se queda igual). Se vuelve a firmar y a subir.${N}"
    "$PY" "$ANUNCIAR" --lista || return
    local id; pedir "Id de la noticia (vacío = volver)" id
    [ -z "$id" ] && return
    local args=(--editar "$id") v
    echo "Deja vacío lo que no quieras cambiar."
    pedir "Título nuevo" v; [ -n "$v" ] && args+=(--titulo "$v")
    ayuda_texto
    pedir "Texto nuevo" v; [ -n "$v" ] && args+=(--texto "$v")
    if si_no "¿Cambiar el tipo (importante / titular / normal, tema y notificar)?"; then
        elegir_tipo
        case " ${TIPO_FLAGS[*]} " in
            *--importante*) args+=(--soltar "${TIPO_FLAGS[@]}") ;;
            *--fijar*) args+=(--normal "${TIPO_FLAGS[@]}") ;;
            *) args+=(--normal "${TIPO_FLAGS[@]}") ;;
        esac
        case " ${TIPO_FLAGS[*]} " in *--notificar*) ;; *) args+=(--sin-notificar) ;; esac
    fi
    if si_no "¿Cambiar la imagen?"; then
        pedir_imagen
        if [ -n "$IMAGEN" ]; then args+=(--imagen "$IMAGEN"); else args+=(--sin-imagen); fi
    fi
    pedir "Inferior nuevo (- para quitarlo)" v
    if [ "$v" = "-" ]; then args+=(--sin-inferior); elif [ -n "$v" ]; then args+=(--inferior "$v"); fi
    pedir "Enlace nuevo (- para quitarlo)" v
    if [ "$v" = "-" ]; then args+=(--sin-enlace); elif [ -n "$v" ]; then args+=(--enlace "$v"); fi
    [ ${#args[@]} -le 2 ] && { echo "Nada que cambiar."; return; }
    "$PY" "$ANUNCIAR" "${args[@]}" "${EXTRA[@]}"
}

borrar_noticia() {
    titulo "Borrar noticia"
    echo "${D}Quita una noticia de todos los editores (desaparece la próxima vez que miren, en unos segundos).${N}"
    "$PY" "$ANUNCIAR" --lista || return
    local id; pedir "Id de la noticia a borrar (vacío = volver)" id
    [ -z "$id" ] && return
    si_no "¿Seguro que quieres borrar la noticia $id?" || return
    "$PY" "$ANUNCIAR" --borrar "$id" "${EXTRA[@]}"
}

actualizar_juego() {
    titulo "Actualizar juego (versión nueva)"
    echo "${D}Publica una versión nueva del editor: sube los ejecutables de Release/ (Linux y Windows) como Release de"
    echo "GitHub, la añade al selector de versiones de todos y crea la noticia \"Nueva actualización\". Antes hay que"
    echo "compilar con tools/release.sh. Estable la ven todos; experimental, solo quien activa + Experimentales.${N}"
    local vh="$RAIZ/src/editor/version.hpp" nom num canal notas lnx win
    local nom0 num0
    nom0="$(sed -n 's/.*S2D_VERSION "\(.*\)".*/\1/p' "$vh" | head -1)"
    num0="$(sed -n 's/^#define S2D_VERSION_NUM \([0-9]*\).*/\1/p' "$vh" | head -1)"
    echo "${D}La versión y el número salen de src/editor/version.hpp (compila antes con tools/release.sh).${N}"
    pedir "Nombre de la versión" nom "$nom0"
    pedir "Número (S2D_VERSION_NUM)" num "$num0"
    echo "Canal: 1) Estable  2) Experimental"
    pedir "Canal" canal 1
    [ "$canal" = 2 ] && canal=experimental || canal=estable
    ayuda_texto
    pedir "Notas (qué trae)" notas
    lnx="$RAIZ/Release/Linux/Source2D"; win="$RAIZ/Release/Windows/Source2D.exe"
    local args=("$nom" "$num" --canal "$canal" --notas "$notas")
    [ -f "$lnx" ] && args+=(--linux "$lnx") || echo "${R}Falta $lnx${N}"
    [ -f "$win" ] && args+=(--windows "$win") || echo "${R}Falta $win${N}"
    if [ ! -f "$lnx" ] && [ ! -f "$win" ]; then echo "No hay ejecutables: ejecuta tools/release.sh primero."; return; fi
    si_no "¿La noticia de la actualización sale en ventana (importante)?" && args+=(--importante)
    si_no "¿Publicar $nom ($canal)?" s || { echo "Cancelado."; return; }
    "$PY" "$PUBLICAR" "${args[@]}" "${EXTRA[@]}"
}

ver_encuesta() {
    titulo "Ver una encuesta"
    "$PY" "$ANUNCIAR" --lista || return
    local id; pedir "Id de la noticia con encuesta (vacío = volver)" id
    [ -z "$id" ] && return
    "$PY" "$ANUNCIAR" --resultados "$id"
}

while :; do
    titulo "Noticias de Source 2D"
    echo "  1) Actualizar juego     ${D}publicar una versión nueva del editor (ejecutables + noticia)${N}"
    echo "  2) Dar noticia          ${D}anuncio nuevo: importante, titular o normal, con imagen y notificación${N}"
    echo "  3) Editar una noticia   ${D}cambiar título, texto, tipo, imagen o enlace de una ya publicada${N}"
    echo "  4) Borrar una noticia   ${D}quitarla de todos los editores${N}"
    echo "  5) Ver las noticias     ${D}lista con número, fecha, tema y marcas (fijada, importante, notificar, encuesta)${N}"
    echo "  6) Ver una encuesta     ${D}los votos de cada opción y cuánto le queda${N}"
    echo "  0) Salir"
    pedir "Elige" op
    case "$op" in
        1) actualizar_juego ;;
        2) dar_noticia ;;
        3) editar_noticia ;;
        4) borrar_noticia ;;
        5) "$PY" "$ANUNCIAR" --lista ;;
        6) ver_encuesta ;;
        0|q|"") exit 0 ;;
        *) echo "${R}Opción no válida${N}" ;;
    esac
done
