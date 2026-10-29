// Source 2D - Source Sound: crear musica (piano roll con instrumentos y bateria) o efectos de sonido (como sfxr),
// todo sintetizado aqui mismo. Se guarda como .s2dsound (la cancion en JSON), se exporta a .wav o .mp3 (con lame o
// ffmpeg si estan instalados) y en un proyecto queda en "Musica y sonidos" como WAV, con la cancion al lado para
// poder seguir editandola.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "assets.hpp"

namespace sound {
bool anyPlaying();                        // algun editor de Source Sound esta sonando (el editor no baja los fps)
json newSong(const std::string &name, bool sfx = false);
std::vector<int16_t> render(const json &song, int rate);            // la cancion o el efecto entero
std::string wavBytes(const std::vector<int16_t> &pcm, int rate);     // archivo .wav (16 bits, mono)
bool exportWav(const json &song, const std::string &path, std::string &err);
bool exportMp3(const json &song, const std::string &path, std::string &err);   // necesita lame o ffmpeg
bool mp3Available();
// en el proyecto: la entrada de doc["music"] con su WAV y la cancion ("song"); devuelve el id
std::string saveToProject(Project &proj, const json &song, const std::string &musicId);
void stopAll();
// galeria (Inicio > Source Sound): escuchar un sonido sin abrirlo, de uno en uno; key identifica cual suena
void clipPlay(const std::string &key, const json &song);
void clipStop();
std::string clipKey();                                               // el que suena ("" si ninguno)
float clipProgress();                                                // de 0 a 1
std::vector<float> peaks(const json &song, int n, double *seconds);  // forma de onda (n barras de 0 a 1) y duracion

class Editor {
public:
    json song;
    std::string musicId;                  // entrada de "Musica y sonidos" (vacio: fuera de un proyecto)
    std::string path;                     // .s2dsound (Source Sound suelto)
    bool open = true, focusMe = true, dirty = false;
    // dialogos de archivos (los pone el editor principal): titulo, extension, nombre propuesto, al elegir
    std::function<void(const char *, const char *, const std::string &, std::function<void(const std::string &)>)> onSaveDialog;
    std::function<void(const std::string &)> onStatus;
    std::function<void()> onChanged;      // en un proyecto: se guarda en el proyecto (tras un momento sin tocar nada)

    ~Editor() { stop(); }
    void drawBody();                      // sin ventana
    void draw(Project &proj);             // documento acoplado (dentro de un proyecto)
    std::string title(Project &proj);
    void play();
    void stop();
    bool playing() const;
    int track = 0;                        // pista elegida
private:
    double playStart = -1, editedAt = -1;
    float scrollY = -1;
    int dragNote = -1, dragMode = 0;      // 1: alargar, 2: mover
    int lastTrack = -1;
    json playedSong;                      // la que esta cargada para sonar (si no cambia, no se recalcula)                   // al cambiar de pista, la vista va a sus notas
    float dragDX = 0;
    bool rec = false, loop = false;
    void drawTop();
    void drawTracks();
    void drawRoll();
    void drawKeyboard();
    void drawSfx();
    void preview(int midi);
    void touched() { dirty = true; editedAt = ImGui::GetTime(); }
};
}
