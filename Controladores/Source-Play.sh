#!/usr/bin/env bash
set -u
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SP=(python3 "$RAIZ/tools/source_play.py")
EXTRA=()
[ "${S2D_SIN_SUBIR:-}" = "1" ] && EXTRA+=(--sin-subir)

if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; V=$'\e[38;5;141m'; R=$'\e[31m'; N=$'\e[0m'; else B= D= V= R= N=; fi
titulo() { printf '\n%s== %s ==%s\n' "$V$B" "$1" "$N"; }
pedir() {
    local p="$1" def="${3:-}" r
    if [ -n "$def" ]; then read -e -r -p "$p [$def]: " r; r="${r:-$def}"; else read -e -r -p "$p: " r; fi
    printf -v "$2" '%s' "$r"
}
si_no() {
    local r def="${2:-n}"
    read -r -p "$1 [$( [ "$def" = s ] && echo S/n || echo s/N )]: " r
    r="${r:-$def}"
    [[ "$r" =~ ^[sSyY] ]]
}

revisar() {
    titulo "Envíos nuevos"
    echo "${D}Juegos que mandaron los creadores desde el editor (Publicar en Source Play). Elige uno: se descarga, se"
    echo "comprueba que se abre y se abre en el jugador para que lo pruebes. Al cerrarlo decides: aprobar (se publica"
    echo "en Source Play para todos), rechazar (escribes el motivo y el creador lo ve en su editor) o dejarlo para luego.${N}"
    "${SP[@]}" envios || return
    local n; pedir "Número del envío (vacío = volver)" n
    [ -z "$n" ] && return
    "${SP[@]}" probar "$n" || return
    echo "¿Qué hacemos con el envío $n?"
    echo "  1) Aprobar    ${D}se publica en Source Play (firma el catálogo: pide tu contraseña)${N}"
    echo "  2) Rechazar   ${D}se cierra con el motivo que escribas${N}"
    echo "  3) Luego      ${D}lo dejas como está${N}"
    local o; pedir "Elige" o 3
    case "$o" in
        1) "${SP[@]}" aprobar "$n" "${EXTRA[@]}" ;;
        2) local m; pedir "Motivo (lo leerá el creador)" m
           [ -n "$m" ] && "${SP[@]}" rechazar "$n" "$m" ;;
    esac
}

anadir() {
    titulo "Publicar un juego tuyo"
    echo "${D}En el editor: abre tu juego, Inicio > Source Play > Publicar mi juego > \"Guardar en una carpeta\"."
    echo "Aquí eliges esa carpeta (la que tiene info.json dentro): se comprueba, se abre para probarlo si quieres"
    echo "y se publica en Source Play con tu firma.${N}"
    local c; pedir "Carpeta del paquete" c
    [ -z "$c" ] && return
    c="${c/#\~/$HOME}"
    [ -f "$c/info.json" ] || { echo "${R}No hay info.json en $c${N}"; return; }
    si_no "¿Probarlo antes en el jugador?" s && "${SP[@]}" probar-carpeta "$c"
    si_no "¿Publicarlo?" s || return
    "${SP[@]}" anadir "$c" "${EXTRA[@]}"
}

retirar() {
    titulo "Retirar un juego"
    echo "${D}Lo quita de Source Play y, además, se borra de los equipos que lo descargaron (por ejemplo, si resultó"
    echo "tener algo que no cumple las normas). Si el creador lo vuelve a enviar y lo apruebas, vuelve a salir.${N}"
    "${SP[@]}" lista || return
    local id; pedir "Id del juego (vacío = volver)" id
    [ -z "$id" ] && return
    si_no "¿Seguro que quieres retirar $id?" || return
    "${SP[@]}" retirar "$id" "${EXTRA[@]}"
}

destacar() {
    titulo "Destacados"
    echo "${D}Los juegos destacados salen arriba del todo en Source Play, en su propia fila. Pon los que más te gusten"
    echo "o los que quieras enseñar (se puede quitar cuando quieras).${N}"
    "${SP[@]}" lista || return
    local id; pedir "Id del juego (vacío = volver)" id
    [ -z "$id" ] && return
    echo "  1) Ponerlo en Destacados   2) Quitarlo de Destacados"
    local o; pedir "Elige" o 1
    if [ "$o" = 2 ]; then "${SP[@]}" no-destacar "$id" "${EXTRA[@]}"; else "${SP[@]}" destacar "$id" "${EXTRA[@]}"; fi
}

while :; do
    titulo "Source Play"
    echo "  1) Envíos nuevos        ${D}probar los juegos que mandaron y aprobarlos o rechazarlos${N}"
    echo "  2) Publicar uno mío     ${D}un paquete guardado desde el editor${N}"
    echo "  3) Juegos publicados    ${D}lista con id, versión y si es abierto o cerrado${N}"
    echo "  4) Retirar un juego     ${D}quitarlo de Source Play y de los equipos que lo tienen${N}"
    echo "  5) Denuncias            ${D}lo que la gente denunció desde el editor (juegos y cuentas)${N}"
    echo "  6) Destacados           ${D}poner o quitar juegos en la fila de Destacados${N}"
    echo "  0) Salir"
    pedir "Elige" op
    case "$op" in
        1) revisar ;;
        2) anadir ;;
        3) "${SP[@]}" lista ;;
        4) retirar ;;
        5) "${SP[@]}" denuncias ;;
        6) destacar ;;
        0|q|"") exit 0 ;;
        *) echo "${R}Opción no válida${N}" ;;
    esac
done
