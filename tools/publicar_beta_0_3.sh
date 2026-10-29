#!/usr/bin/env bash
# Source 2D - publica la Beta 0.3 en Noticias y en el selector de versiones (firma con tu clave: pide la contrasena).
# Antes: tools/release.sh (genera Release/Linux/Source2D y Release/Windows/Source2D.exe con la Beta 0.3).
#   tools/publicar_beta_0_3.sh                  canal experimental (aun falta probarla)
#   CANAL=estable tools/publicar_beta_0_3.sh    para todos
#   S2D_SIN_SUBIR=1 tools/publicar_beta_0_3.sh  firma pero no sube (para probar)
set -eu
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CANAL="${CANAL:-experimental}"
LNX="$RAIZ/Release/Linux/Source2D"
WIN="$RAIZ/Release/Windows/Source2D.exe"

VH="$RAIZ/src/editor/version.hpp"
NOM="$(sed -n 's/.*S2D_VERSION "\(.*\)".*/\1/p' "$VH" | head -1)"
NUM="$(sed -n 's/^#define S2D_VERSION_NUM \([0-9]*\).*/\1/p' "$VH" | head -1)"
if [ "$NOM" != "Beta 0.3" ]; then echo "version.hpp dice \"$NOM\", no \"Beta 0.3\"."; exit 1; fi
for f in "$LNX" "$WIN"; do
    [ -f "$f" ] || { echo "Falta $f: ejecuta tools/release.sh primero."; exit 1; }
    [ "$f" -nt "$VH" ] || { echo "$f es anterior a la Beta 0.3: ejecuta tools/release.sh."; exit 1; }
done

NOTAS="{lista:Los tiles ahora son objetos (se colocan y editan como cualquier otro), Autotiles, Luces y oscuridad por capa con la nueva ventana Capas, Source Sound para hacer sonidos y música (con la canción Test de ejemplo), Colisiones por animación y en los tiles (rampas y escalones), Escuela nueva con Floppa: Conoce el editor y Monedas y puntos con el botón Hazlo por mí, Archivos .source2d mucho más pequeños, Exportar a una carpeta (motor + datos), Encuestas en Noticias, Source Play: la tienda de juegos gratis (descarga, juega y publica el tuyo con GitHub; juegos cerrados con clave), Trabajar juntos más seguro (el invitado no se queda el proyecto), Los juegos con funciones de eventos van mucho más rápido, El editor gasta menos cuando no lo usas} Versión de prueba: puede tener fallos."

EXTRA=()
[ "${S2D_SIN_SUBIR:-}" = "1" ] && EXTRA+=(--sin-subir)

echo "Se publicará $NOM (número $NUM) en el canal $CANAL."
read -r -p "¿Seguir? [S/n]: " r
[[ "${r:-s}" =~ ^[sSyY] ]] || { echo "Cancelado."; exit 0; }
python3 "$RAIZ/tools/publicar_version.py" "$NOM" "$NUM" --canal "$CANAL" --notas "$NOTAS" \
    --linux "$LNX" --windows "$WIN" "${EXTRA[@]}"
