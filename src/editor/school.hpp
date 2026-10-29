// Source 2D - Modo escuela (con Floppa, que habla en un globo, señala dónde pulsar y espera "Estoy listo"): lecciones paso a paso dentro del editor. Cada paso dice que hacer y se comprueba solo
// mirando el proyecto (hay un objeto, tiene tal comportamiento, hay un evento con tal condicion, probaste el juego...);
// al cumplirse pasa al siguiente. El progreso se guarda en <config>/escuela.json.
#pragma once
#include <functional>
#include <string>
#include "project.hpp"
#include "imgui.h"

namespace school {
void drawSection();                                   // apartado Escuela de la pagina de inicio (lista de lecciones)
void drawPanel(Project *proj);                        // panel de la leccion en curso, encima del editor (null: sin proyecto)
void projectClosed();                                 // Floppa se despide si habia una leccion
void note(const std::string &what);                   // "preview", "export"... (para los pasos que lo piden)
bool active();
// puntos de la interfaz que Floppa puede senalar: llamar justo despues de dibujar el boton (o con su rectangulo)
void anchor(const char *id);
void anchorRect(const char *id, ImVec2 a, ImVec2 b);
void start(const std::string &lessonId);              // tambien para las pruebas
int stepIndex();
bool autoStep(Project &proj);                         // pruebas: "Hazlo por mi" del paso actual
void pressReady();                                    // pruebas: pulsar "Estoy listo"                                      // paso actual de la leccion en curso (pruebas)
extern std::function<void(const std::string &name)> onNewProject;   // crea un proyecto en blanco para la leccion
extern std::function<void(const std::string &msg)> onNotify;          // aviso en la barra del editor
extern std::function<void()> onAddSong;                               // anade la Cancion de Floppa al proyecto y la abre
extern bool saveProgress;
}
