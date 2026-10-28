// Source 2D - editor de codigo (shaders GLSL y JavaScript), como un VS Code pequeno: colores por tipo de palabra,
// numeros de linea, errores marcados en su linea (con el mensaje al pasar el raton) y autocompletado con Tab.
// Por debajo es el campo de texto de ImGui (cursor, seleccion, copiar y pegar, deshacer); el color se pinta encima.
#pragma once
#include <string>
#include <vector>
#include "imgui.h"

enum class CodeLang { GLSL, JS };
struct CodeError { int line = 0; std::string msg; };     // line empieza en 1 (0 = sin linea)

// devuelve true si el texto cambio; errs se marcan en sus lineas
bool codeEditor(const char *id, std::string &text, ImVec2 size, CodeLang lang, const std::vector<CodeError> &errs);
// comprobaciones (rapidas: se pueden llamar cada vez que cambia el texto, con un poco de espera)
std::vector<CodeError> checkGLSL(const std::string &code);                 // shader de capa (GLSL ES 1.0 del editor)
std::vector<CodeError> checkJS(const std::string &code, bool functionBody); // codigo de eventos (cuerpo de funcion) o extension
// campo de codigo de un json (o[key]) con su comprobacion: los errores se buscan medio segundo despues de dejar de
// escribir y se muestran en sus lineas y debajo. Devuelve true si cambio. errsOut: los errores actuales (o nullptr)
#include "project.hpp"
bool codeField(const char *id, json &o, const char *key, ImVec2 size, CodeLang lang, bool functionBody = true, std::vector<CodeError> *errsOut = nullptr);
bool codeFieldStr(const char *id, std::string &text, ImVec2 size, CodeLang lang, bool functionBody = true, std::vector<CodeError> *errsOut = nullptr);
// version de un shader del editor para el OpenGL del editor (para la vista previa); "" si no compila
std::string glslForEditor(const std::string &code);
