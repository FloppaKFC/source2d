// Source 2D - documentos "Capas" (todas las opciones de cada capa, con su oscuridad) y "Autotiles" (crear un
// autotile eligiendo el tile de cada forma y probarlo pintando en una zona de prueba).
#pragma once
#include <string>
#include <vector>
#include "assets.hpp"

class LayersEditor {
public:
    bool open = true, focusMe = true;
    void draw(Project &proj);
};

std::string createAutotile(Project &proj);   // devuelve el id

class AutotilesEditor {
public:
    bool open = true, focusMe = true;
    std::string cur;
    int slot = 0;                            // forma (mascara 0-15) que se esta eligiendo
    std::vector<char> test;                  // zona de prueba: celdas pintadas
    void draw(Project &proj);
};
