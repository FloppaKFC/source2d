#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "assets.hpp"

namespace sound {
bool anyPlaying();
json newSong(const std::string &name, bool sfx = false);
std::vector<int16_t> render(const json &song, int rate);
std::string wavBytes(const std::vector<int16_t> &pcm, int rate);
bool exportWav(const json &song, const std::string &path, std::string &err);
bool exportMp3(const json &song, const std::string &path, std::string &err);
bool mp3Available();
std::string saveToProject(Project &proj, const json &song, const std::string &musicId);
void stopAll();
void clipPlay(const std::string &key, const json &song);
void clipStop();
std::string clipKey();
float clipProgress();
std::vector<float> peaks(const json &song, int n, double *seconds);

class Editor {
public:
    json song;
    std::string musicId;
    std::string path;
    bool open = true, focusMe = true, dirty = false;
    std::function<void(const char *, const char *, const std::string &, std::function<void(const std::string &)>)> onSaveDialog;
    std::function<void(const std::string &)> onStatus;
    std::function<void()> onChanged;

    ~Editor() { stop(); }
    void drawBody();
    void draw(Project &proj);
    std::string title(Project &proj);
    void play();
    void stop();
    bool playing() const;
    int track = 0;
private:
    double playStart = -1, editedAt = -1;
    float scrollY = -1;
    int dragNote = -1, dragMode = 0;
    int lastTrack = -1;
    json playedSong;
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
