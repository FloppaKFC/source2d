// Source 2D - exportar el juego como un unico ejecutable para Linux o Windows.
// El editor lleva dentro los motores (tools/gen_players.py), comprimidos con LZMA:
//   normal: el motor completo tal cual + el proyecto. Abre al instante.
//   ligera: un lanzador de pocos KB + el motor comprimido + el proyecto comprimido (imagenes con paleta y LZMA).
//           Usa el motor ligero si al proyecto no le falta nada en el (ver needsFull) y si no el completo, tambien
//           comprimido. Al abrirse se descomprime en memoria (Linux) o en un archivo temporal (Windows).
// Nada pierde calidad: las imagenes, el audio y el codigo van igual, solo empaquetados sin perdida.
#pragma once
#include <string>
#include <vector>
#include "project.hpp"

namespace exporter {
bool available(const std::string &os, bool lite);     // el editor lleva lo necesario para esa exportacion
// true si el proyecto usa algo que el motor ligero no tiene (why: que); entonces la ligera usa el completo comprimido
bool needsFull(const json &doc, std::string &why);
// escribe el juego; info: tamano y motor usado, para avisar al usuario
bool exportGame(const json &doc, const std::string &out, const std::string &os, bool lite, std::string &err, std::string &info);
// a una carpeta dir: <name>.exe / <name>.x86_64 (el motor) y datos/juego.source2d.json; html: index.html
bool exportFolder(const json &doc, const std::string &dir, const std::string &os, const std::string &name, std::string &err, std::string &info);
// motor completo de un sistema, descomprimido (para probar el juego desde el editor)
bool unpackPlayer(const std::string &os, std::vector<unsigned char> &out);
unsigned long playerSize(const std::string &os);      // tamano del motor completo descomprimido (0 si no esta)
// el proyecto como va en el juego: imagenes de pixeles con paleta y los datos en base64 pasados a binario (soft: se
// comprimen bien; hard: ya vienen comprimidos, como OGG o PNG). El jugador lo vuelve a armar sin perder nada.
struct Packed { std::string json, soft, hard; };
Packed packed(const json &doc, bool pixels = true);   // pixels: imagenes de pixeles con paleta (solo para el jugador)
// .source2d: el proyecto en LZMA (y los datos en binario) para pasarlo a otro Source 2D
std::string sourcePack(const json &doc);
bool isSourcePack(const std::string &bytes);
bool sourceUnpack(const std::string &bytes, json &doc, std::string &err);
std::string kb(long bytes);
}
