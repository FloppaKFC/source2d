#!/usr/bin/env bash
# Source 2D - menu de Source Store de sprites (Source-Sprite.sh) o de sonidos (Source-Sound.sh, que usa este mismo menu):
# revisar los paquetes que mandaron los creadores, sacarlos a una carpeta para mirarlos, aprobarlos o rechazarlos,
# publicar los tuyos, retirar uno, destacados y denuncias. Cada opcion explica lo que hace.
# Usa tools/source_store.py: firma el catalogo con tu clave (pide la contrasena), lo sube con git y usa gh para los envios.
#   Controladores/Source-Sprite.sh               (desde cualquier carpeta)
#   S2D_SIN_SUBIR=1 Controladores/Source-Sprite.sh firma pero no sube (para probar)
set -u
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIPO="${S2D_STORE_TIPO:-sprites}"
if [ "$TIPO" = sonidos ]; then
    NOMBRE="Source Store de sonidos"; COSA="sonidos"; DONDE="Source Sound"; ABRIR="ábrelos en Source Sound"
    CATS="Música, Efectos o Mixto"
else
    NOMBRE="Source Store de sprites"; COSA="sprites, tiles y autotiles"; DONDE="Source Sprite"
    ABRIR="abre los .s2dsprite en Source Sprite y el proyecto de tiles en el editor"; CATS="Personaje, Sprites sueltos, Tiles, Autotiles o Mixto"
fi
ST=(python3 "$RAIZ/tools/source_store.py" "$TIPO")
EXTRA=()
[ "${S2D_SIN_SUBIR:-}" = "1" ] && EXTRA+=(--sin-subir)

if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; V=$'\e[38;5;141m'; R=$'\e[31m'; N=$'\e[0m'; else B= D= V= R= N=; fi
titulo() { printf '\n%s== %s ==%s\n' "$V$B" "$1" "$N"; }
pedir() {   # pedir "Pregunta" variable [por defecto]
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
    echo "${D}Paquetes de $COSA que mandaron los creadores desde $DONDE (Crear paquete). Elige uno: se descarga, el"
    echo "editor comprueba que solo trae lo que toca y saca cada cosa a una carpeta para que lo mires ($ABRIR)."
    echo "Luego decides: aprobar (se publica para todos), rechazar (escribes el motivo y el creador lo ve en su"
    echo "editor) o dejarlo para luego. Mira también que la licencia que eligió tenga sentido.${N}"
    "${ST[@]}" envios || return
    local n; pedir "Número del envío (vacío = volver)" n
    [ -z "$n" ] && return
    "${ST[@]}" probar "$n" || return
    echo "¿Qué hacemos con el envío $n?"
    echo "  1) Aprobar    ${D}se publica en $NOMBRE (firma el catálogo: pide tu contraseña)${N}"
    echo "  2) Rechazar   ${D}se cierra con el motivo que escribas${N}"
    echo "  3) Luego      ${D}lo dejas como está${N}"
    local o; pedir "Elige" o 3
    case "$o" in
        1) "${ST[@]}" aprobar "$n" "${EXTRA[@]}" ;;
        2) local m; pedir "Motivo (lo leerá el creador)" m
           [ -n "$m" ] && "${ST[@]}" rechazar "$n" "$m" ;;
    esac
}

anadir() {
    titulo "Publicar un paquete tuyo"
    echo "${D}En el editor: Inicio > $DONDE > Source Store > Crear paquete > \"Guardar en una carpeta\" (categoría:"
    echo "$CATS). Aquí eliges esa carpeta (la que tiene info.json dentro): se comprueba y se publica con tu firma.${N}"
    local c; pedir "Carpeta del paquete" c
    [ -z "$c" ] && return
    c="${c/#\~/$HOME}"
    [ -f "$c/info.json" ] || { echo "${R}No hay info.json en $c${N}"; return; }
    si_no "¿Sacarlo antes a una carpeta para mirarlo?" n && "${ST[@]}" probar-carpeta "$c"
    si_no "¿Publicarlo?" s || return
    "${ST[@]}" anadir "$c" "${EXTRA[@]}"
}

retirar() {
    titulo "Retirar un paquete"
    echo "${D}Lo quita de $NOMBRE y se borra de los equipos que lo descargaron (lo que ya se añadió a un proyecto se"
    echo "queda en ese proyecto). Si el creador lo vuelve a enviar y lo apruebas, vuelve a salir.${N}"
    "${ST[@]}" lista || return
    local id; pedir "Id del paquete (vacío = volver)" id
    [ -z "$id" ] && return
    si_no "¿Seguro que quieres retirar $id?" || return
    "${ST[@]}" retirar "$id" "${EXTRA[@]}"
}

destacar() {
    titulo "Destacados"
    echo "${D}Los paquetes destacados salen arriba del todo en $NOMBRE, en su propia fila.${N}"
    "${ST[@]}" lista || return
    local id; pedir "Id del paquete (vacío = volver)" id
    [ -z "$id" ] && return
    echo "  1) Ponerlo en Destacados   2) Quitarlo de Destacados"
    local o; pedir "Elige" o 1
    if [ "$o" = 2 ]; then "${ST[@]}" no-destacar "$id" "${EXTRA[@]}"; else "${ST[@]}" destacar "$id" "${EXTRA[@]}"; fi
}

while :; do
    titulo "$NOMBRE"
    echo "  1) Envíos nuevos        ${D}revisar los paquetes que mandaron y aprobarlos o rechazarlos${N}"
    echo "  2) Publicar uno mío     ${D}un paquete guardado desde el editor${N}"
    echo "  3) Paquetes publicados  ${D}lista con id, versión y categoría${N}"
    echo "  4) Retirar un paquete   ${D}quitarlo de la tienda y de los equipos que lo tienen${N}"
    echo "  5) Denuncias            ${D}lo que la gente denunció desde el editor${N}"
    echo "  6) Destacados           ${D}poner o quitar paquetes en la fila de Destacados${N}"
    echo "  0) Salir"
    pedir "Elige" op
    case "$op" in
        1) revisar ;;
        2) anadir ;;
        3) "${ST[@]}" lista ;;
        4) retirar ;;
        5) "${ST[@]}" denuncias ;;
        6) destacar ;;
        0|q|"") exit 0 ;;
        *) echo "${R}Opción no válida${N}" ;;
    esac
done
