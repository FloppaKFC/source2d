#!/bin/sh
# Genera la carpeta Release con el motor listo para usar (un solo ejecutable por sistema, sin instaladores):
#   Release/Linux/Source2D        editor para Linux (cualquier distribucion con glibc 2.28 o mas nueva)
#   Release/Windows/Source2D.exe  editor para Windows
# Los dos editores llevan dentro los jugadores de Linux y de Windows, asi que desde cualquiera se exporta para ambos.
# Windows y Linux se compilan con zig (trae su propio mingw y glibc antiguas). Usa $ZIG, o build/toolchain/zig/zig, o el
# zig del sistema. Linux va contra glibc 2.28 (Makefile.linux): con el gcc de este sistema pediria su glibc (la de Arch)
# y no abriria en distribuciones mas viejas (Linux Lite, Ubuntu LTS, Mint...).
# Uso: tools/release.sh   (desde source2d/)
set -e
cd "$(dirname "$0")/.."
ZIG=${ZIG:-}
[ -z "$ZIG" ] && [ -x build/toolchain/zig/zig ] && ZIG=$PWD/build/toolchain/zig/zig
[ -z "$ZIG" ] && ZIG=zig
printf 'int main(void){return 0;}\n' > /tmp/s2d_zigprueba.c
if ! "$ZIG" cc -target x86_64-windows-gnu /tmp/s2d_zigprueba.c -o /tmp/s2d_zigprueba.exe >/dev/null 2>&1; then
    echo "No se puede compilar para Windows con '$ZIG'."
    echo "Repara zig (por ejemplo: sudo pacman -S zig) o descarga zig de https://ziglang.org/download/"
    echo "y descomprimelo en build/toolchain/zig/ (o indica la ruta con ZIG=/ruta/a/zig)."
    exit 1
fi
rm -f /tmp/s2d_zigprueba.c /tmp/s2d_zigprueba.exe
# como mucho 4 a la vez: con 12 compilaciones de C++ en paralelo systemd-oomd llego a cerrar la terminal
J=$(nproc 2>/dev/null || echo 4); [ "$J" -gt 4 ] && J=4
echo "== cabeceras generadas (gcc del sistema)"
make -j"$J" build/source2d-player src/gfx_js.h src/runtime_js.h src/licencias_jugador.h build/gen/instructions_json.h build/gen/presets.h build/gen/editor_fonts.h build/gen/licencias_editor.h build/gen/gfx_web_js.h
echo "== motores de Linux (glibc 2.28): completo, ligero y lanzador"
make -f Makefile.linux -j"$J" ZIG="$ZIG" player lite
echo "== motores de Windows: completo, ligero y lanzador"
make -f Makefile.win -j"$J" ZIG="$ZIG" player lite
echo "== editores (llevan los motores de los dos sistemas para exportar)"
rm -f build/gen/players_portable.c
make -f Makefile.linux -j"$J" ZIG="$ZIG" editor
make -f Makefile.win -j"$J" ZIG="$ZIG" editor
make -f Makefile.linux ZIG="$ZIG" check-glibc
rm -rf Release
mkdir -p Release/Linux Release/Windows
echo "== editores comprimidos (un solo archivo que se descomprime al abrirse)"
python3 tools/pack_exe.py build/lnx/lite/stub build/lnx/Source2D Release/Linux/Source2D
python3 tools/pack_exe.py build/win/lite/stub.exe build/win/Source2D.exe Release/Windows/Source2D.exe
make LICENCIAS-TERCEROS.txt
cp LICENCIAS-TERCEROS.txt Release/LICENCIAS-TERCEROS.txt
rm -rf Release/Idiomas && cp -r Idiomas Release/Idiomas
cat > Release/LEEME.txt <<'TXT'
Source 2D @VERSION@ - motor de juegos 2D con eventos

Linux:   Release/Linux/Source2D        (dale permiso de ejecucion si hace falta: chmod +x Source2D)
Windows: Release/Windows/Source2D.exe  (Windows 10 u 11; en 7 y 8.1 hace falta la actualizacion KB2999226 de Microsoft)

Cada uno es un unico archivo: no hay que instalar nada. Los proyectos se guardan solos en
Documentos/Source 2D (se puede cambiar en la pagina de inicio).
Desde cualquiera de los dos se exporta el juego para Linux o para Windows con el boton Publicar:
el resultado es un solo ejecutable que no necesita nada mas. Un juego para Linux exportado desde Windows llega sin
permiso de ejecucion (Windows no lo guarda): en Linux hay que darselo una vez (chmod +x juego, o en sus propiedades).

Exportar: ligera (ocupa poco), pesada (abre al instante) u optimizada (ligera y sin lo que no se usa), para Windows,
Linux y la web (un .zip con index.html para itch.io y las game jams), varios a la vez.
Escuela (pagina de inicio): lecciones paso a paso que se comprueban solas. Plantillas: juegos con cada evento explicado.
Idiomas/: los textos del editor. Para traducirlo, copia Español.txt con el nombre del idioma (por ejemplo Ingles.txt),
traduce lo que va despues de " = " y elige el idioma en Archivo > Opciones.
Steam: en Extensiones, "+ Steam (oficial)". Al publicar en Steam pon junto al juego libsteam_api.so o steam_api64.dll
(del SDK de Steamworks) y, para probarlo fuera de Steam, steam_appid.txt con el numero de tu juego.

Versiones: cada version que abres se guarda; en la pagina de inicio (abajo a la izquierda) puedes elegir otra.
En "+ Experimentales" esta "@VERSION@ (Vulkan)": los juegos se prueban y exportan dibujando con Vulkan (si un equipo no
tiene Vulkan, siguen con OpenGL solos). Un juego tambien se puede abrir con ./juego --vulkan o --opengl.
Al probar un juego desde el editor: F3 muestra el depurador, F6 pausa y F7 avanza un fotograma.
Si no abre en un equipo: ./Source2D --diagnostico (o ./juego --diagnostico) escribe source2d-diagnostico.txt.
Los mandos funcionan solos (la cruceta y el stick son las flechas, A es espacio, Start es Enter).

Al publicar, la "Exportacion ligera" comprime el motor y el juego sin perder calidad (un juego pequeño ocupa
unos 550 KB). La normal ocupa mas, pero abre un instante antes.

Source 2D usa raylib, GLFW, QuickJS, Dear ImGui, rlImGui, nlohmann/json y las fuentes DejaVu Sans y Outfit:
sus licencias estan en LICENCIAS-TERCEROS.txt. Los juegos exportados las muestran con: ./juego --licencias
TXT
VERSION="$(sed -n 's/.*S2D_VERSION "\(.*\)".*/\1/p' src/editor/version.hpp | head -1)"
sed -i "s/@VERSION@/$VERSION/g" Release/LEEME.txt
ls -la Release/Linux Release/Windows
