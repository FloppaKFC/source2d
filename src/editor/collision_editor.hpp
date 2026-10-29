// Source 2D - editor de colisiones: la forma con la que choca una animacion (o un tile). Rectangulo, cuadrado,
// circulo, elipse o puntos (poligono), en pixeles del dibujo con el origen arriba a la izquierda:
//   {"shape": "rect"|"square"|"circle"|"ellipse"|"poly", "x", "y", "w", "h", "points": [[x, y], ...]}
// Sin forma (null o sin "shape") choca con todo el dibujo. El runtime la usa en src/runtime.js (maskOf).
#pragma once
#include "assets.hpp"

// dibuja el editor para un dibujo de sw x sh pixeles (tex: el dibujo de fondo, puede ser null); devuelve true si cambio
bool collisionEditor(const char *id, json &mask, ui::PixTexture *tex, int sw, int sh, float maxSize);
