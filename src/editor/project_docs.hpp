// Source 2D - documentos del proyecto: ajustes y variables globales, shaders, extensiones y plugins,
// musica (importar y escuchar) e importacion de imagenes como sprites o conjuntos de tiles.
#pragma once
#include <functional>
#include <string>
#include "assets.hpp"

class SettingsEditor {
public:
    bool open = true, focusMe = true;
    void draw(Project &proj);
};

class ShadersEditor {
public:
    bool open = true, focusMe = true;
    std::string cur;
    void draw(Project &proj);
};

class ExtensionsEditor {
public:
    bool open = true, focusMe = true;
    std::string cur;
    void draw(Project &proj);
};

// musica
std::string base64Encode(const unsigned char *data, size_t len);
std::vector<unsigned char> base64Decode(const std::string &s);
std::string importMusic(Project &proj, const std::string &file, std::string &err);   // devuelve el id
bool addInitialFile(Project &proj, const std::string &file, std::string &err);        // a project.files
void musicPreview(Project &proj, const std::string &id);    // '' detiene
std::string musicPlaying();
void musicUpdate();
void musicShutdown();

// imagenes
struct ImportImage {
    bool open = false, asTiles = false;
    std::string path, name, error;
    int fw = 16, fh = 16, imgW = 0, imgH = 0;
    std::vector<Px> px;
    ui::PixTexture tex;
    bool load(const std::string &file);
    std::string commit(Project &proj);                       // id del sprite o del conjunto creado
    void draw(Project &proj, const std::function<void(const std::string &, bool)> &done);
};

std::string addRainShader(Project &proj);
std::string addExtension(Project &proj, bool plugin, bool estilo);
std::string addSteamExtension(Project &proj);
