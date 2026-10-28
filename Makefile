# Source 2D - editor nativo y jugador, sin dependencias externas.
# Resultado: build/source2d (editor, un solo ejecutable) y build/source2d-player (jugador).
# Ambos dependen solo de libc/libm: X11/Wayland y OpenGL se cargan en tiempo de ejecucion (GLFW + dlopen).
#   make            -> editor y jugador PARA ESTE EQUIPO (pruebas): piden la glibc de este sistema, y los juegos de
#                      Linux que exporte este editor tampoco abriran en distribuciones mas viejas (Ubuntu, Mint...)
#   tools/release.sh -> carpeta Release (editores de Linux y Windows, para repartir)
#   make STRICT=1   -> falla en vez de avisar si el jugador incrustado pide una glibc mayor que GLIBC_PORTABLE
QJS_DIR    ?= third_party/quickjs
RAYLIB_DIR  = third_party/raylib/src
IMGUI_DIR   = third_party/imgui
RLIMGUI_DIR = third_party/rlImGui
JSON_DIR    = third_party/json
CC         ?= gcc
CXX        ?= g++
CFLAGS     ?= -O2 -Wall -Wextra -std=c11 -fno-strict-aliasing -ffunction-sections -fdata-sections
CXXFLAGS   ?= -O2 -Wall -Wextra -std=c++17 -ffunction-sections -fdata-sections
RAYLIB_A    = $(RAYLIB_DIR)/libraylib.a
QJS_A       = $(QJS_DIR)/b-os/libqjs.a
LDLIBS      = -lm -ldl -lpthread
LDSTATIC    = -static-libgcc -Wl,--gc-sections -s

PLAYER = build/source2d-player
EDITOR = build/source2d

all: $(PLAYER) $(EDITOR)
player: $(PLAYER)
editor: $(EDITOR)

build build/gen:
	mkdir -p $@

# ---- dependencias propias ----
# raylib con OpenGL 2.1: abre tambien en graficas viejas (Intel GMA y similares); lo moderno sigue igual de rapido
$(RAYLIB_A):
	$(MAKE) -C $(RAYLIB_DIR) PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC GRAPHICS=GRAPHICS_API_OPENGL_21

$(QJS_A):
	cmake -S $(QJS_DIR) -B $(QJS_DIR)/b-os -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
	cmake --build $(QJS_DIR)/b-os --target qjs -j4

# capa nativa del runtime (superficies de la GPU, imagenes, contexto 2D, entrada y fotogramas)
src/gfx_js.h: src/gfx.js tools/gen_gfx.py tools/jsmin.py
	python3 tools/gen_gfx.py src/gfx.js src/gfx_js.h

# capa del runtime para el navegador (exportacion HTML): la lleva el editor
build/gen/gfx_web_js.h: src/web/gfx_web.js tools/gen_gfx.py tools/jsmin.py | build/gen
	python3 tools/gen_gfx.py src/web/gfx_web.js $@ gfx_web_js

# runtime JS del juego embebido en el jugador
src/runtime_js.h: src/runtime.js tools/gen_runtime.py tools/jsmin.py
	python3 tools/gen_runtime.py src/runtime.js src/runtime_js.h

# licencias de terceros: dentro del jugador (./juego --licencias), del editor (Ayuda) y junto a la Release
LIC_SRC = tools/gen_licencias.py third_party/raylib/LICENSE third_party/raylib/src/external/glfw/LICENSE.md third_party/quickjs/LICENSE third_party/vulkan/LICENSE.txt \
          third_party/dejavu/LICENSE.txt third_party/imgui/LICENSE.txt third_party/rlImGui/LICENSE third_party/outfit/OFL.txt
src/licencias_jugador.h: $(LIC_SRC)
	python3 tools/gen_licencias.py jugador $@
build/gen/licencias_editor.h: $(LIC_SRC) | build/gen
	python3 tools/gen_licencias.py editor $@
LICENCIAS-TERCEROS.txt: $(LIC_SRC)
	python3 tools/gen_licencias.py texto $@

# ---- jugador ----
PLAYER_SRC = src/main.c src/platform_raylib.c src/text.c src/gpu_vulkan.c src/steam.c
$(PLAYER): $(PLAYER_SRC) src/platform.h src/text.h src/gfx_js.h src/font_data.h src/runtime_js.h src/licencias_jugador.h src/gpu_vulkan.h src/vk_shaders.h $(RAYLIB_A) $(QJS_A) | build
	$(CC) $(CFLAGS) -Isrc -I$(QJS_DIR) -I$(RAYLIB_DIR) -I$(RAYLIB_DIR)/external -DPLAYER -c src/main.c -o build/main_player.o
	$(CC) $(CFLAGS) -Isrc -I$(RAYLIB_DIR) -c src/platform_raylib.c -o build/platform_raylib.o
	$(CC) $(CFLAGS) -O3 -Wno-unused-function -Isrc -I$(RAYLIB_DIR)/external -c src/text.c -o build/text.o
	$(CC) $(CFLAGS) -Isrc -Ithird_party/vulkan/include -c src/gpu_vulkan.c -o build/gpu_vulkan.o
	$(CC) $(CFLAGS) -Isrc -c src/steam.c -o build/steam.o
	$(CC) build/main_player.o build/platform_raylib.o build/text.o build/gpu_vulkan.o build/steam.o -o $@ $(RAYLIB_A) $(QJS_A) $(LDLIBS) $(LDSTATIC)

# ---- editor ----
# El editor lleva los motores para exportar a Linux y a Windows (tools/gen_players.py, todos en LZMA): el completo y,
# para la exportacion ligera, el ligero y el lanzador. Los de Windows, el ligero y los lanzadores los compilan
# Makefile.win y Makefile.linux (tools/release.sh); los que falten no se ofrecen al exportar.
# Este editor de pruebas lleva el jugador de Linux del gcc del sistema (ver check-portable).
PLAYERS_IN = build/lnx/lite/stub $(PLAYER) build/lnx/lite/source2d-lite build/win/lite/stub.exe build/win/source2d-player.exe build/win/lite/source2d-lite.exe
build/gen/players_bin.c: $(PLAYER) $(wildcard $(PLAYERS_IN)) tools/gen_players.py | build/gen
	python3 tools/gen_players.py $@ $(PLAYERS_IN)
build/gen/players_bin.h: build/gen/players_bin.c
build/gen/%.o: build/gen/%.c
	$(CC) -c $< -o $@

# presets pequenos incrustados (shader de lluvia, plugin de estilo, plantilla de extension)
build/gen/presets.h: $(wildcard src/editor/presets/*) tools/bin2h.py | build/gen
	python3 tools/bin2h.py src/editor/presets/lluvia.glsl preset_lluvia build/gen/preset_lluvia.h
	python3 tools/bin2h.py src/editor/presets/estilo.js preset_estilo build/gen/preset_estilo.h
	python3 tools/bin2h.py src/editor/presets/extension.js preset_extension build/gen/preset_extension.h
	python3 tools/bin2h.py src/editor/presets/steam.js preset_steam build/gen/preset_steam.h
	printf '#pragma once\n#include "preset_lluvia.h"\n#include "preset_estilo.h"\n#include "preset_extension.h"\n#include "preset_steam.h"\n' > $@

# juegos de prueba: no van dentro del editor, sino en la carpeta Ejemplos (tools/release.sh). El editor de pruebas los
# encuentra tambien aqui, en la raiz. Cada uno lo genera su guion:
#   examples_ajedrez.source2d.json         tools/make_chess.py (con tools/chess_art.py)
#   examples_maxwell.source2d.json         tools/make_maxwell.py (animacion de 36 imagenes en tools/maxwell_giro.png)
#   examples_platform_test.source2d.json   tools/make_platform_test.py (nivel en tools/platform_level.py, arte en tools/platform_art.py)
#   examples_rymik.source2d.json           tools/make_rymik.py (desde el proyecto de GDevelop de la carpeta "Proyect Ry-mik")
#   examples_rpg.source2d.json             tools/make_rpg.py (arte en tools/rpg_art.py, letras de tools/chess_font.py)
#   examples_benchmark.source2d.json       tools/make_benchmark.py (prueba de rendimiento: CPU, GPU y fisica)
examples_ajedrez.source2d.json: tools/make_chess.py tools/chess_art.py
	python3 tools/make_chess.py $@
examples_maxwell.source2d.json: tools/make_maxwell.py tools/maxwell_giro.png
	python3 tools/make_maxwell.py $@
examples_platform_test.source2d.json: tools/make_platform_test.py tools/platform_level.py tools/platform_art.py
	python3 tools/make_platform_test.py $@
# el proyecto original de GDevelop no siempre esta (no va en las copias del motor): sin el se usa el .json ya generado
RYMIK_SRC = $(wildcard Proyect\ Ry-mik/Project\ Ry-mik.json)
ifneq ($(RYMIK_SRC),)
examples_rymik.source2d.json: tools/make_rymik.py Proyect\ Ry-mik/Project\ Ry-mik.json src/editor/presets/lluvia.glsl
	python3 tools/make_rymik.py "Proyect Ry-mik" $@
endif
examples_rpg.source2d.json: tools/make_rpg.py tools/rpg_art.py tools/chess_font.py
	python3 tools/make_rpg.py $@
examples_benchmark.source2d.json: tools/make_benchmark.py examples_plataformas.source2d.json
	python3 tools/make_benchmark.py $@
templates: tools/make_templates.py examples_plataformas.source2d.json
	python3 tools/make_templates.py
idioma: tools/gen_idioma.py src/editor/instructions.json
	python3 tools/gen_idioma.py

# definiciones de condiciones y acciones (frases, parametros, categorias) incrustadas en el editor
build/gen/instructions_json.h: src/editor/instructions.json tools/bin2h.py | build/gen
	python3 tools/bin2h.py src/editor/instructions.json instructions_json $@

# fuente de la interfaz del editor: Outfit (OFL, ver third_party/outfit/OFL.txt)
build/gen/editor_fonts.h: third_party/outfit/Outfit-Medium.ttf third_party/outfit/Outfit-SemiBold.ttf tools/bin2h.py | build/gen
	python3 tools/bin2h.py third_party/outfit/Outfit-Medium.ttf outfit_medium build/gen/outfit_medium.h
	python3 tools/bin2h.py third_party/outfit/Outfit-SemiBold.ttf outfit_semibold build/gen/outfit_semibold.h
	printf '#pragma once\n#include "outfit_medium.h"\n#include "outfit_semibold.h"\n' > $@

IMGUI_SRC = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp $(RLIMGUI_DIR)/rlImGui.cpp
IMGUI_OBJ = $(patsubst %.cpp,build/imgui/%.o,$(notdir $(IMGUI_SRC)))
EDITOR_SRC = $(wildcard src/editor/*.cpp)
EDITOR_OBJ = $(patsubst src/editor/%.cpp,build/editor/%.o,$(EDITOR_SRC))
EDITOR_INC = -Isrc -Isrc/editor -Ibuild/gen -I$(RAYLIB_DIR) -I$(IMGUI_DIR) -I$(RLIMGUI_DIR) -I$(JSON_DIR) -I$(QJS_DIR)

build/imgui/%.o: $(IMGUI_DIR)/%.cpp | build
	@mkdir -p build/imgui
	$(CXX) $(CXXFLAGS) -w -I$(IMGUI_DIR) -I$(RAYLIB_DIR) -c $< -o $@
build/imgui/rlImGui.o: $(RLIMGUI_DIR)/rlImGui.cpp | build
	@mkdir -p build/imgui
	$(CXX) $(CXXFLAGS) -w -I$(IMGUI_DIR) -I$(RAYLIB_DIR) -c $< -o $@

build/editor/%.o: src/editor/%.cpp $(wildcard src/editor/*.hpp) src/runtime_js.h build/gen/gfx_web_js.h build/gen/players_bin.h build/gen/instructions_json.h build/gen/presets.h build/gen/editor_fonts.h build/gen/licencias_editor.h | build
	@mkdir -p build/editor
	$(CXX) $(CXXFLAGS) $(EDITOR_INC) -c $< -o $@

$(EDITOR): $(EDITOR_OBJ) $(IMGUI_OBJ) build/gen/players_bin.o $(RAYLIB_A) $(QJS_A)
	$(CXX) $(EDITOR_OBJ) $(IMGUI_OBJ) build/gen/players_bin.o -o $@ $(RAYLIB_A) $(QJS_A) $(LDLIBS) -static-libstdc++ $(LDSTATIC)
	@$(MAKE) --no-print-directory check-portable || { rm -f $@; exit 1; }

# avisa (o falla con STRICT=1) si el jugador que lleva el editor pide una glibc mas nueva que GLIBC_PORTABLE
GLIBC_PORTABLE = 2.28
check-portable: $(PLAYER)
	@v=$$(objdump -T $(PLAYER) 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -1); \
	if [ -n "$$v" ] && [ "$$(printf '%s\n%s\n' "$$v" "$(GLIBC_PORTABLE)" | sort -V | tail -1)" != "$(GLIBC_PORTABLE)" ]; then \
	  echo ""; \
	  echo "  AVISO: $(EDITOR) lleva un jugador de Linux que pide GLIBC $$v (la de este sistema)."; \
	  echo "  Sirve para probar aqui, pero los juegos que exporte no abriran en distribuciones con una glibc"; \
	  echo "  mas vieja (Ubuntu, Debian, Mint...). Para repartir el editor o los juegos usa tools/release.sh"; \
	  echo "  (o make -f Makefile.linux editor), que lo compila con zig contra glibc $(GLIBC_PORTABLE)."; \
	  echo ""; \
	  if [ -n "$(STRICT)" ]; then exit 1; fi; \
	fi

# comprueba que los ejecutables no dependen de bibliotecas del sistema (solo libc/libm)
check-deps: $(PLAYER) $(EDITOR)
	@for f in $(PLAYER) $(EDITOR); do echo "== $$f"; ldd $$f; done

clean:
	rm -rf build

.PHONY: all player editor check-deps check-portable clean
