# Source 2D (Beta 0.1)

Motor de juegos 2D con eventos. Es un editor nativo en un solo ejecutable, sin instaladores ni dependencias.
Exporta cada juego como un único ejecutable para Linux o Windows; con la **exportación ligera**, un juego pequeño
ocupa unos 550 KB con el motor incluido.

## Novedades de la Beta 0.1

- **Entrada:** mando (los botones llegan como teclas `pad_a`, `pad_left`...; la cruceta y A valen por las flechas y el
  espacio en cualquier juego), ejes y vibración; táctil en la web; comportamiento *Botón táctil*.
- **Sonido:** sonidos de archivo que suenan a la vez, en 8 canales con volumen, tono, variación al azar y posición.
- **Eventos:** *Esperar*, mensajes entre eventos, pausa (los objetos con "sigue en pausa" se animan), tween terminado,
  grupos de objetos, elegir el más cercano / lejano / al azar, eventos dentro de las animaciones (`3:paso`).
- **Física:** materiales (piedra, goma, papel, globo...), masa, peso, resistencia del aire, choques con reparto de
  velocidad, cuerpos que duermen, paso fijo con interpolación (Ajustes), rayos (`RayoX()`, `RayoDistancia()`).
- **Movimiento:** buscar caminos (A*), *Patrullar*, *Seguir*, *Mantener en pantalla*.
- **Visual:** luces 2D (oscuridad de la escena y comportamiento *Luz*), nine-slice, texto con `[color=]`, `[wave]` y
  `[shake]`, partículas con imagen y giro, destello blanco, estirar y aplastar, pausa de impacto, temblor suave y
  cámara con anticipación y zona muerta, escala entera (píxel perfecto).
- **Juego:** guardar la partida completa (objetos, cámara, temporizadores), idiomas (`{t:clave}`, `T("clave")`).
- **Editor:** iconos en todas las condiciones y acciones, autotiles, depurador en la vista previa (F3, F6, F7),
  aviso de instrucciones con objetos borrados, selector de versiones en la página de inicio.

## Escuela, plantillas, idiomas y Steam

- **Escuela** (página de inicio): lecciones paso a paso (`src/editor/school.cpp`); cada paso se comprueba solo mirando
  el proyecto. El progreso se guarda en `<config>/escuela.json`.
- **Plantillas**: juegos pequeños con un comentario en cada evento (`tools/make_templates.py`).
- **Idiomas**: `Idiomas/Español.txt` (`tools/gen_idioma.py`) tiene cada texto del editor como `original = traducción`.
  Otro archivo en esa carpeta (Inglés.txt...) aparece en Opciones > Idioma. La traducción se aplica al dibujar el
  texto (gancho `GImGuiTranslate` en `imgui_draw.cpp`); las frases de los eventos se traducen con sus `{huecos}`.
- **Steam**: extensión oficial (`src/editor/presets/steam.js`) sobre `src/steam.c`, que carga la API plana de
  Steamworks al vuelo (logros, estadísticas, nube, overlay, presencia). Sin la biblioteca de Steam, todo responde
  que no y el juego funciona igual.
- **Editor de código**: colores, números de línea, errores en su línea (GLSL compilado con el OpenGL del editor, JS
  con QuickJS) y autocompletado con Tab (`src/editor/code_editor.cpp`); los shaders tienen vista previa en vivo.
- **Versiones del proyecto**: cada guardado deja una copia con fecha en `.versiones/` junto al proyecto (Archivo >
  Versiones del proyecto). Experimental: guardado a prueba de apagones (Opciones).

## Experimental: Beta 0.1 (Vulkan)

En la página de inicio, **Versión > + Experimentales > Beta 0.1 (Vulkan)** abre el editor en modo Vulkan: los juegos
que se prueban y se exportan desde ahí dibujan con Vulkan (`src/gpu_vulkan.c`) en vez de OpenGL. Vulkan se carga al
arrancar (libvulkan.so.1 / vulkan-1.dll); si no está o falla, el juego sigue con OpenGL. Los shaders propios del
proyecto se compilan con shaderc si está instalado (si no, esas capas van sin shader). El editor sigue con OpenGL.
Cualquier juego acepta `--vulkan` / `--opengl` (o `S2D_RENDERER=vulkan`). Los shaders internos están en `tools/vk/`
y se compilan a `src/vk_shaders.h` con `tools/gen_vk_shaders.py` (hace falta glslc solo para cambiarlos).

## Compilar

Necesitas gcc/g++, make, cmake, python3 y las cabeceras de X11. Para la Release hace falta además
[zig](https://ziglang.org/download/), del sistema o descomprimido en `build/toolchain/zig/`.

| Orden | Resultado |
|---|---|
| `make` | `build/source2d` (editor) y `build/source2d-player` (jugador), **solo para probar en este equipo** |
| `tools/release.sh` | carpeta `Release/` con los editores de Linux (glibc 2.28+) y Windows, para repartir |
| `test/run_editor_tests.sh` | pruebas automáticas del editor y del runtime |

`make` usa el gcc del sistema, así que los ejecutables piden su glibc. En Arch es la última, y los juegos de Linux
que exporte ese editor no abrirán en Ubuntu, Debian o Mint. `make` avisa de esto al terminar. Con `make STRICT=1`
falla en vez de avisar. Los editores de `Makefile.linux` y `Makefile.win` (los que usa `tools/release.sh`) siempre
llevan el jugador compilado con zig contra glibc 2.28.

La Release lleva:

- `Linux/Source2D` y `Windows/Source2D.exe`: el editor, comprimido (`tools/pack_exe.py`): un solo archivo de unos
  5 MB que se descomprime al abrirse (en Linux en memoria; en Windows en una copia temporal que reutiliza).
- `Ejemplos/`: los juegos de prueba. No van dentro del editor; los busca en `Ejemplos` junto a él o junto a su carpeta.

En Windows, los `.exe` hechos con zig piden la UCRT (`api-ms-win-crt-*.dll`): viene en Windows 10 y 11; en 7 y 8.1
hace falta la actualización KB2999226 y en Vista no existe. Si descomprimes llvm-mingw en su variante `msvcrt` en
`build/toolchain/llvm-mingw/`, `Makefile.win` la usa y los `.exe` piden `msvcrt.dll`, que trae cualquier Windows.
Los `.exe` llevan un manifiesto (`src/win/`) para que las rutas con tildes funcionen.

## Exportar

*Publicar* hace un único ejecutable para Linux o Windows, desde cualquiera de los dos sistemas:

- **Ligera** (por defecto): un lanzador de pocos KB + el motor y el juego comprimidos con LZMA, sin perder calidad.
  Las imágenes de píxeles van con paleta, y las imágenes, audio y fuentes incrustados pasan de base64 a binario (los ya
  comprimidos, como OGG o PNG, van tal cual). Usa el motor ligero si el juego no necesita nada que le falte (JPG, MP3,
  FLAC, QOA, `Date`/`Proxy`/`BigInt` en el código o letras fuera del alfabeto latino); si no, el completo, comprimido.
- **Normal**: el motor completo sin comprimir y el juego. Ocupa más, pero abre un instante antes.

## Estructura

- `src/main.c`, `src/platform_raylib.c` y `src/text.c`: el jugador (raylib + QuickJS). Todo se dibuja en la GPU:
  imágenes, rellenos, degradados, filtros CSS y shaders. `text.c` solo rasteriza letras (stb_truetype).
- `src/runtime.js`: el runtime de los juegos (eventos, física, dibujo y audio). Corre sobre `src/gfx.js`, que le da
  superficies de la GPU, imágenes, un contexto 2D, la entrada y el bucle de fotogramas.
- `test/render_shots.py` y `test/render_compare.py`: capturas de cada escena de los juegos de prueba y de cada efecto
  para comparar dos versiones del jugador píxel a píxel.
- `src/editor/`: el editor (Dear ImGui sobre raylib).
- `tools/`: generadores de cabeceras, de los juegos de prueba (`make_*.py`) y el script de la Release.
- `examples_*.source2d.json`: los juegos de prueba de la página de inicio. Cada uno lo genera su `tools/make_*.py`.
- `third_party/`: raylib, QuickJS, Dear ImGui, rlImGui, nlohmann/json y las fuentes Outfit y DejaVu.

## Trabajar juntos

El botón de las dos personas (barra superior) comparte el proyecto abierto; en la página de inicio, *Unirse a una
sesión* entra en el de otra persona. Cada uno ve los cambios de los demás al momento, sus cursores y lo que tienen
seleccionado en la escena, y quién está en la sesión. No hay servidores ni cuentas: una PC comparte y las demás se
conectan directo a ella (TCP 47610, avisos por UDP 47611), con un código de 6 cifras.

- **Misma red WiFi** (o cable): la sesión aparece sola en *Unirse*.
- **Zona WiFi**: una PC crea su propia red (en Linux con un botón, por NetworkManager; en Windows desde
  Configuración) y las demás se conectan a ella. No hace falta router ni internet.
- **Tailscale**: para trabajar a distancia. Se entra con la dirección 100.x.x.x o el nombre del anfitrión.
- **Bluetooth**: por la red Bluetooth del sistema (emparejar y unirse a la red de área personal). Funciona igual que
  la WiFi, pero lento.

Si dos personas cambian lo mismo a la vez, gana el último cambio que llega al anfitrión, y todos quedan iguales.
El anfitrión guarda el proyecto; quien se une trabaja sobre una copia sin guardar. En Linux con `ufw` hay que abrir
los puertos (la ventana muestra la orden).

## Seguridad

Un proyecto lleva eventos y JavaScript (extensiones, acciones de código y expresiones). Al probarlo o exportarlo,
ese código corre en el jugador, que puede leer, escribir y borrar archivos. Antes de abrir un proyecto que no es
tuyo, el editor pide confirmación. Un proyecto se considera tuyo si está en la carpeta de proyectos o si ya lo
abriste o guardaste antes. La lista de proyectos de confianza está en `confiables.txt`, en la carpeta de
configuración del editor.

## Licencias

Las licencias de terceros se reúnen en `LICENCIAS-TERCEROS.txt` (`make LICENCIAS-TERCEROS.txt`). El editor las
muestra en *Ayuda > Licencias de terceros* y cada juego exportado con `./juego --licencias`.
