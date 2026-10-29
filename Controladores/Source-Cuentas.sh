#!/usr/bin/env bash
# Source 2D - cuentas de Source Play: ver las cuentas registradas (las que entraron con GitHub desde el editor) y darles
# o quitarles la insignia de cuenta verificada o de admin. La insignia sale junto a su nombre en el editor (perfil,
# tarjetas de sus juegos). Usa tools/source_cuentas.py: firma cuentas.json con tu clave (pide la contrasena) y lo sube.
#   Controladores/Source-Cuentas.sh                 (desde cualquier carpeta)
#   S2D_SIN_SUBIR=1 Controladores/Source-Cuentas.sh firma pero no sube (para probar)
set -u
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SC=(python3 "$RAIZ/tools/source_cuentas.py")
EXTRA=()
[ "${S2D_SIN_SUBIR:-}" = "1" ] && EXTRA+=(--sin-subir)
if [ -t 1 ]; then B=$'\e[1m'; D=$'\e[2m'; V=$'\e[38;5;141m'; R=$'\e[31m'; N=$'\e[0m'; else B= D= V= R= N=; fi
titulo() { printf '\n%s== %s ==%s\n' "$V$B" "$1" "$N"; }
pedir() {
    local p="$1" def="${3:-}" r
    if [ -n "$def" ]; then read -e -r -p "$p [$def]: " r; r="${r:-$def}"; else read -e -r -p "$p: " r; fi
    printf -v "$2" '%s' "$r"
}

cambiar() {   # cambiar verificar|admin|quitar "texto"
    local u; pedir "Usuario de GitHub (vacío = volver)" u
    [ -z "$u" ] && return
    "${SC[@]}" "$1" "$u" "${EXTRA[@]}"
}

while :; do
    titulo "Cuentas de Source Play"
    echo "  1) Ver cuentas registradas  ${D}todas, de la más nueva a la más vieja, con su insignia${N}"
    echo "  2) Buscar una cuenta        ${D}por parte de su nombre de usuario${N}"
    echo "  3) Verificar una cuenta     ${D}insignia azul: es quien dice ser${N}"
    echo "  4) Hacer admin              ${D}insignia de admin (equipo de Source 2D)${N}"
    echo "  5) Quitar insignia          ${D}vuelve a ser una cuenta normal${N}"
    echo "  6) Ver quién tiene insignia"
    echo "  0) Salir"
    pedir "Elige" op
    case "$op" in
        1) "${SC[@]}" lista | less -FRX ;;
        2) pedir "Texto a buscar" t; [ -n "$t" ] && "${SC[@]}" lista "$t" ;;
        3) titulo "Verificar"; cambiar verificar ;;
        4) titulo "Hacer admin"; cambiar admin ;;
        5) titulo "Quitar insignia"; cambiar quitar ;;
        6) "${SC[@]}" insignias ;;
        0|q|"") exit 0 ;;
        *) echo "${R}Opción no válida${N}" ;;
    esac
done
