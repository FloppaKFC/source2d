// Source 2D - miniaturas de proyectos para la pagina de inicio: la primera pantalla de la escena inicial,
// dibujada por CPU a partir del JSON (no hace falta abrir el proyecto en los editores).
#pragma once
#include <string>
#include <vector>
#include "project.hpp"
#include "ui.hpp"

constexpr int kThumbW = 384, kThumbH = 216;   // 16:9, como las de GDevelop

// pinta la vista inicial del juego, (0,0)-(ancho,alto de la ventana), de la primera escena en out (w*h pixeles)
void renderProjectThumb(const json &doc, int w, int h, std::vector<Px> &out, int sceneIdx = 0);   // sceneIdx: otra escena

// miniatura de un archivo de proyecto; se rehace si cambia su fecha. Se genera como mucho una por fotograma,
// asi que devuelve nullptr mientras espera turno o si el archivo no se puede leer.
ui::PixTexture *fileThumb(const std::string &path);
std::string fileProjectName(const std::string &path);   // nombre guardado en el archivo (o el del archivo si aun no se leyo)
// miniatura de un documento en memoria (proyecto abierto o plantilla); se rehace cuando cambia rev y, si live,
// cada segundo y medio para seguir los cambios de la escena
ui::PixTexture *docThumb(const std::string &key, const json &doc, int rev, bool live = true, int sceneIdx = 0);
void thumbsNewFrame();   // llamar al principio de cada fotograma
