// Source 2D - Modo escuela: lecciones paso a paso dentro del editor. Cada paso dice que hacer y se comprueba solo
// mirando el proyecto (hay un objeto, tiene tal comportamiento, hay un evento con tal condicion, probaste el juego...);
// al cumplirse pasa al siguiente. El progreso se guarda en <config>/escuela.json.
#pragma once
#include <functional>
#include <string>
#include "project.hpp"

namespace school {
void drawSection();                                   // apartado Escuela de la pagina de inicio (lista de lecciones)
void drawPanel(Project &proj);                        // panel de la leccion en curso, encima del editor
void note(const std::string &what);                   // "preview", "export"... (para los pasos que lo piden)
bool active();
void start(const std::string &lessonId);              // tambien para las pruebas
int stepIndex();                                      // paso actual de la leccion en curso (pruebas)
extern std::function<void(const std::string &name)> onNewProject;   // crea un proyecto en blanco para la leccion
extern std::function<void(const std::string &msg)> onNotify;          // aviso en la barra del editor
extern bool saveProgress;
}
