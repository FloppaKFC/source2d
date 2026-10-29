#!/usr/bin/env bash
# Source 2D - menu de Source Store de sonidos (canciones y efectos hechos en Source Sound). Es el mismo menu que
# Source-Sprite.sh, con el catalogo de FloppaKFC/source2d-sonidos.
#   Controladores/Source-Sound.sh                (desde cualquier carpeta)
#   S2D_SIN_SUBIR=1 Controladores/Source-Sound.sh firma pero no sube (para probar)
S2D_STORE_TIPO=sonidos exec "$(dirname "${BASH_SOURCE[0]}")/Source-Sprite.sh" "$@"
