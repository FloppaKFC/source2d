#!/usr/bin/env bash
set -u
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AD=(python3 "$RAIZ/tools/admin.py")
export S2D_EDITOR="${S2D_EDITOR:-$RAIZ/build/source2d}"

if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; V=$'\e[38;5;141m'; R=$'\e[31m'; N=$'\e[0m'; else B= D= V= R= N=; fi
titulo() { printf '\n%s== %s ==%s\n' "$V$B" "$1" "$N"; }
pedir() {
    local p="$1" def="${3:-}" r
    if [ -n "$def" ]; then read -e -r -p "$p [$def]: " r; r="${r:-$def}"; else read -e -r -p "$p: " r; fi
    printf -v "$2" '%s' "$r"
}
si_no() { local r; read -r -p "$1 [s/N]: " r; [[ "$r" =~ ^[sSyY] ]]; }
tipo() {
    echo "  1) Juegos   2) Sprites   3) Sonidos"
    local t; pedir "Tipo" t 1
    case "$t" in 2) TIPO=sprites ;; 3) TIPO=sonidos ;; *) TIPO=juegos ;; esac
}

[ -f "${S2D_R2_ENV:-$HOME/.config/source2d-r2.env}" ] || [ -n "${S2D_R2_LOCAL:-}" ] || { echo "${R}Falta ~/.config/source2d-r2.env con las claves de R2${N}"; exit 1; }
[ -x "$S2D_EDITOR" ] || echo "${D}Aviso: no está $S2D_EDITOR (compila con make) — hace falta para comprobar juegos y paquetes.${N}"

envios() {
    titulo "Envíos esperando revisión"
    echo "${D}Juegos, sprites y sonidos que mandaron desde el editor. Probar: se descarga, se comprueba y el juego se abre"
    echo "AISLADO (sin internet ni acceso a tus archivos); los paquetes se sacan a una carpeta para ver las imágenes y"
    echo "escuchar los sonidos con el editor. Al aprobar o rechazar le llega una notificación al creador.${N}"
    "${AD[@]}" envios || return
    local e; pedir "Envío (login/id, vacío = volver)" e
    [ -z "$e" ] && return
    "${AD[@]}" probar "$e"
    echo "  1) Aprobar   2) Rechazar   3) Luego"
    local o; pedir "Elige" o 3
    case "$o" in
        1) "${AD[@]}" aprobar "$e" ;;
        2) local m; pedir "Motivo (lo leerá el creador)" m; [ -n "$m" ] && "${AD[@]}" rechazar "$e" "$m" ;;
    esac
}

publicado() {
    titulo "Lo publicado"
    tipo
    "${AD[@]}" lista "$TIPO" || return
    echo "  1) Retirar   2) Destacar   3) Quitar de destacados   4) Volver"
    local o id; pedir "Elige" o 4
    [ "$o" = 4 ] && return
    pedir "Id" id
    [ -z "$id" ] && return
    case "$o" in
        1) si_no "¿Retirar $id? Se borra de la tienda y de los equipos que lo tienen" && "${AD[@]}" retirar "$TIPO" "$id" ;;
        2) "${AD[@]}" destacar "$TIPO" "$id" ;;
        3) "${AD[@]}" no-destacar "$TIPO" "$id" ;;
    esac
}

denuncias() {
    titulo "Denuncias y reportes"
    echo "${D}Los mensajes privados reportados se comprueban con su llave: [COMPROBADO] es el texto auténtico.${N}"
    "${AD[@]}" denuncias || return
    echo "  1) Cerrar una denuncia   2) Borrar un comentario   3) Banear una cuenta   4) Volver"
    local o; pedir "Elige" o 4
    case "$o" in
        1) local id; pedir "Id de la denuncia" id; [ -n "$id" ] && "${AD[@]}" cerrar-denuncia "$id" ;;
        2) tipo; local id c; pedir "Id del juego o paquete" id; pedir "Id del comentario" c; [ -n "$c" ] && "${AD[@]}" borrar-comentario "$TIPO" "$id" "$c" ;;
        3) banear ;;
    esac
}

cuentas() {
    titulo "Cuentas e insignias"
    local q; pedir "Buscar (vacío = todas)" q
    "${AD[@]}" cuentas $q
    echo "  1) Dar insignia   2) Quitar insignia   3) Apuntar una contribución (da la insignia Contribuidor)   4) Volver"
    local o l; pedir "Elige" o 4
    [ "$o" = 4 ] && return
    pedir "Cuenta (login)" l
    [ -z "$l" ] && return
    case "$o" in
        1|2) echo "  verificada · admin · creador · contribuidor"; local i; pedir "Insignia" i
             if [ "$o" = 2 ]; then "${AD[@]}" insignia "$l" "$i" --quitar; else "${AD[@]}" insignia "$l" "$i"; fi ;;
        3) local idea; pedir "Idea que aportó" idea; [ -n "$idea" ] && "${AD[@]}" contribucion "$l" "$idea" ;;
    esac
}

banear() {
    titulo "Banear o desbanear"
    local l o; pedir "Cuenta (login)" l
    [ -z "$l" ] && return
    echo "  1) Banear 30 días   2) Banear para siempre   3) Banear X días   4) Desbanear"
    pedir "Elige" o 1
    local d=30
    case "$o" in 2) d=siempre ;; 3) pedir "Días" d 7 ;; 4) "${AD[@]}" desbanear "$l"; return ;; esac
    local m; pedir "Motivo (lo verá la persona)" m
    "${AD[@]}" banear "$l" "$d" "${m:-No cumplir las normas}"
}

solicitudes() {
    titulo "Solicitudes de espacio"
    echo "${D}Proyectos de más de 100 MB y espacio en la nube. Si no le ves futuro, recházala y di por qué.${N}"
    "${AD[@]}" solicitudes || return
    local s; pedir "Solicitud (login/id, vacío = volver)" s
    [ -z "$s" ] && return
    echo "  1) Aceptar   2) Rechazar"
    local o; pedir "Elige" o 1
    if [ "$o" = 1 ]; then
        local mb r; pedir "MB que le das (50, 100, 500...)" mb 100; pedir "Mensaje (opcional)" r
        "${AD[@]}" aceptar-solicitud "$s" "$mb" "$r"
    else
        local m; pedir "Motivo" m; [ -n "$m" ] && "${AD[@]}" rechazar-solicitud "$s" "$m"
    fi
}

noticias() {
    titulo "Noticias"
    echo "${D}Salen en Inicio > Noticias de todos los editores.${N}"
    echo "  1) Publicar una noticia   2) Ver las noticias   3) Borrar una   4) Resultados de una encuesta   5) Volver"
    local o; pedir "Elige" o 1
    case "$o" in
        2) "${AD[@]}" noticias --lista; return ;;
        3) "${AD[@]}" noticias --lista; local id; pedir "Id de la noticia" id; [ -n "$id" ] && si_no "¿Borrar la noticia $id?" && "${AD[@]}" noticias --borrar "$id"; return ;;
        4) local id; pedir "Id de la noticia" id; [ -n "$id" ] && "${AD[@]}" noticias --resultados "$id"; return ;;
        5) return ;;
    esac
    local t x tp im en hs
    pedir "Título" t
    [ -z "$t" ] && return
    echo "${D}Texto. Para una lista: {lista:uno, dos, tres}. Para una encuesta: {encuesta: \"Pregunta\", \"Opción 1\", \"Opción 2\"}${N}"
    pedir "Texto" x
    echo "  Tipo: 1) Aviso   2) Evento   3) Actualización   4) Consejo"
    pedir "Tipo" tp 1
    local args=("$t" "$x")
    case "$tp" in 2) args+=(--tipo evento) ;; 3) args+=(--tipo actualizacion) ;; 4) args+=(--tipo consejo) ;; *) args+=(--tipo aviso) ;; esac
    pedir "Imagen (ruta, vacío = sin imagen)" im
    [ -n "$im" ] && args+=(--imagen "${im/#\~/$HOME}")
    pedir "Enlace (https://..., vacío = sin enlace)" en
    [ -n "$en" ] && args+=(--enlace "$en")
    si_no "¿Importante? (sale en una ventana al abrir el editor)" && args+=(--importante)
    si_no "¿Fijarla arriba del todo?" && args+=(--fijar)
    si_no "¿Notificarla? (aviso emergente en los editores abiertos)" && args+=(--notificar)
    pedir "Que deje de salir después del día (AAAA-MM-DD, vacío = nunca)" hs
    [ -n "$hs" ] && args+=(--hasta "$hs")
    "${AD[@]}" noticias "${args[@]}"
}

while :; do
    titulo "Source 2D · admin"
    echo "  1) Envíos           ${D}probar, aprobar o rechazar juegos, sprites y sonidos${N}"
    echo "  2) Publicar uno mío ${D}un paquete guardado con \"Guardar en una carpeta\"${N}"
    echo "  3) Lo publicado     ${D}lista, retirar, destacados${N}"
    echo "  4) Denuncias        ${D}juegos, cuentas, comentarios, chats y mensajes${N}"
    echo "  5) Cuentas          ${D}insignias y contribuciones${N}"
    echo "  6) Baneos           ${D}30 días, para siempre o los días que quieras${N}"
    echo "  7) Solicitudes      ${D}más de 100 MB y espacio en la nube${N}"
    echo "  8) Noticias"
    echo "  0) Salir"
    pedir "Elige" op
    case "$op" in
        1) envios ;;
        2) tipo; pedir "Carpeta (la que tiene info.json)" c; [ -n "$c" ] && "${AD[@]}" publicar-carpeta "$TIPO" "${c/#\~/$HOME}" ;;
        3) publicado ;;
        4) denuncias ;;
        5) cuentas ;;
        6) banear ;;
        7) solicitudes ;;
        8) noticias ;;
        0|q|"") exit 0 ;;
        *) echo "${R}Opción no válida${N}" ;;
    esac
done
