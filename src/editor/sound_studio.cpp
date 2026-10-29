#include "sound_studio.hpp"
#include "school.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "project_docs.hpp"
#include "raylib.h"
#include "sys.hpp"

namespace fs = std::filesystem;

namespace sound {
static const double PI2 = 6.283185307179586;
struct Inst { const char *id, *name; };
static const Inst kInst[] = {{"piano", "Piano"}, {"organo", "Órgano"}, {"bajo", "Bajo"}, {"sierra", "Sintetizador"}, {"chip", "Chip (8 bits)"},
                             {"campana", "Campana"}, {"flauta", "Flauta"}, {"cuerdas", "Cuerdas"}, {"bateria", "Batería"}};
static const char *kDrums[] = {"Bombo", "Caja", "Charles", "Charles abierto", "Palmas", "Tom grave", "Tom agudo", "Platillo"};
static const int kDrumN = 8;
static const char *kNoteNames[] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
static const int kLow = 36, kHigh = 96;    // Do2 .. Do7

json newSong(const std::string &name, bool sfx) {
    json s = {{"format", "source2d-sound"}, {"name", name}, {"mode", sfx ? "sfx" : "music"}, {"bpm", 120}, {"bars", 4},
              {"tracks", json::array({{{"name", "Piano"}, {"inst", "piano"}, {"vol", 0.8}, {"notes", json::array()}},
                                      {{"name", "Batería"}, {"inst", "bateria"}, {"vol", 0.8}, {"notes", json::array()}}})},
              {"sfx", {{"preset", "Salto"}, {"wave", "square"}, {"freq", 300}, {"slide", 2.0}, {"dur", 0.25}, {"attack", 0.0}, {"vib", 0},
                       {"vibSpeed", 6}, {"arp", 0}, {"arpAt", 0.1}, {"duty", 0.5}, {"vol", 0.8}}}};
    return s;
}

/* ---------------- sintesis ---------------- */
static uint32_t g_rng = 22222;
static float noise() { g_rng = g_rng * 1664525u + 1013904223u; return (float)((g_rng >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
static double releaseOf(const std::string &in) {
    return in == "piano" ? 0.35 : in == "campana" ? 0.8 : in == "cuerdas" ? 0.3 : in == "flauta" ? 0.1 : in == "sierra" ? 0.15 : 0.06;
}

// una nota de un instrumento en out (a partir de la muestra at); held: segundos que se mantiene pulsada
static void renderNote(std::vector<float> &out, size_t at, int rate, const std::string &in, int midi, double held, float vol) {
    double f = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    if (in == "bateria") {
        int lane = std::clamp(midi, 0, kDrumN - 1);
        static const double durs[] = {0.35, 0.25, 0.08, 0.4, 0.25, 0.35, 0.3, 1.2};
        size_t n = (size_t)(durs[lane] * rate);
        double ph = 0;
        float prev = 0;
        for (size_t i = 0; i < n && at + i < out.size(); i++) {
            double t = (double)i / rate;
            float v = 0, nz = noise(), hp = nz - prev;
            prev = nz;
            switch (lane) {
            case 0: ph += PI2 * (50 + 110 * std::exp(-t * 30)) / rate; v = (float)(std::sin(ph) * std::exp(-t * 8)); break;
            case 1: ph += PI2 * 185 / rate; v = (float)(std::sin(ph) * 0.45 * std::exp(-t * 20) + nz * 0.6 * std::exp(-t * 14)); break;
            case 2: v = (float)(hp * 0.35 * std::exp(-t * 55)); break;
            case 3: v = (float)(hp * 0.3 * std::exp(-t * 8)); break;
            case 4: { double e = t < 0.03 ? std::exp(-std::fmod(t, 0.01) * 300) : std::exp(-(t - 0.02) * 16); v = (float)(nz * 0.6 * e); break; }
            case 5: ph += PI2 * (100 + 60 * std::exp(-t * 15)) / rate; v = (float)(std::sin(ph) * std::exp(-t * 7)); break;
            case 6: ph += PI2 * (180 + 80 * std::exp(-t * 15)) / rate; v = (float)(std::sin(ph) * std::exp(-t * 8)); break;
            default: v = (float)(hp * 0.28 * std::exp(-t * 3)); break;
            }
            out[at + i] += v * vol;
        }
        return;
    }
    double rel = releaseOf(in), total = held + rel;
    size_t n = (size_t)(total * rate);
    double ph = 0, ph2 = 0, ph3 = 0, lp = 0;
    for (size_t i = 0; i < n && at + i < out.size(); i++) {
        double t = (double)i / rate;
        double env = t < held ? 1.0 : std::exp(-(t - held) / std::max(0.01, rel) * 5);
        double w = PI2 * f / rate, v = 0;
        if (in == "piano") {
            env *= std::min(1.0, t / 0.004) * std::exp(-t * 2.2);
            ph += w;
            v = std::sin(ph) + 0.45 * std::sin(2 * ph) * std::exp(-t * 4) + 0.18 * std::sin(3 * ph) * std::exp(-t * 7);
            v *= 0.6;
        } else if (in == "organo") {
            env *= std::min(1.0, t / 0.01);
            ph += w;
            v = (std::sin(ph) + 0.5 * std::sin(2 * ph) + 0.3 * std::sin(4 * ph)) / 1.8;
        } else if (in == "bajo") {
            env *= std::min(1.0, t / 0.005) * (0.55 + 0.45 * std::exp(-t * 3));
            ph = std::fmod(ph + f / rate, 1.0);
            double tri = 4 * std::fabs(ph - 0.5) - 1, saw = 2 * ph - 1;
            v = tri * 0.8 + saw * 0.25;
        } else if (in == "sierra") {
            env *= std::min(1.0, t / 0.008);
            ph = std::fmod(ph + f / rate, 1.0); ph2 = std::fmod(ph2 + f * 1.005 / rate, 1.0);
            double raw = (2 * ph - 1) * 0.5 + (2 * ph2 - 1) * 0.5;
            lp += (raw - lp) * 0.25;
            v = lp * 0.6;
        } else if (in == "chip") {
            env *= std::min(1.0, t / 0.002);
            ph = std::fmod(ph + f / rate, 1.0);
            v = (ph < 0.25 ? 1 : -1) * 0.35;
        } else if (in == "campana") {
            env *= std::min(1.0, t / 0.002) * std::exp(-t * 1.6);
            ph += w; ph2 += w * 3.5;
            v = std::sin(ph + 2.5 * std::exp(-t * 3) * std::sin(ph2)) * 0.6;
        } else if (in == "flauta") {
            env *= std::min(1.0, t / 0.06);
            double vib = t > 0.25 ? 1 + 0.006 * std::sin(PI2 * 5.5 * t) : 1;
            ph += w * vib;
            v = std::sin(ph) * 0.6 + std::sin(2 * ph) * 0.08 + noise() * 0.05 * std::exp(-t * 8);
        } else {   // cuerdas
            env *= std::min(1.0, t / 0.15);
            ph = std::fmod(ph + f * 0.996 / rate, 1.0); ph2 = std::fmod(ph2 + f / rate, 1.0); ph3 = std::fmod(ph3 + f * 1.004 / rate, 1.0);
            double raw = ((2 * ph - 1) + (2 * ph2 - 1) + (2 * ph3 - 1)) / 3;
            lp += (raw - lp) * 0.12;
            v = lp * 0.7;
        }
        out[at + i] += (float)(v * env * vol);
    }
}

static std::vector<float> renderSfx(const json &p, int rate) {
    std::string wave = jstr(p, "wave", "square");
    double f0 = std::max(10.0, jnum(p, "freq", 440)), slide = jnum(p, "slide", 0), dur = std::clamp(jnum(p, "dur", 0.3), 0.01, 5.0);
    double att = std::clamp(jnum(p, "attack", 0), 0.0, 2.0), vib = jnum(p, "vib", 0), vibS = jnum(p, "vibSpeed", 6);
    double arp = jnum(p, "arp", 0), arpAt = jnum(p, "arpAt", 0.1), duty = std::clamp(jnum(p, "duty", 0.5), 0.05, 0.95), vol = jnum(p, "vol", 0.8);
    size_t n = (size_t)((att + dur) * rate);
    std::vector<float> out(n);
    double ph = 0;
    float hold = 0;
    for (size_t i = 0; i < n; i++) {
        double t = (double)i / rate;
        double f = f0 * std::pow(2.0, slide * t);
        if (arp && t >= arpAt) f *= std::pow(2.0, arp / 12.0);
        if (vib) f *= 1 + vib * 0.05 * std::sin(PI2 * vibS * t);
        f = std::clamp(f, 10.0, (double)rate / 2.2);
        double prevPh = ph;
        ph = std::fmod(ph + f / rate, 1.0);
        double v;
        if (wave == "saw") v = 2 * ph - 1;
        else if (wave == "sine") v = std::sin(PI2 * ph);
        else if (wave == "triangle") v = 4 * std::fabs(ph - 0.5) - 1;
        else if (wave == "noise") { if (ph < prevPh) hold = noise(); v = hold; }
        else v = ph < duty ? 1 : -1;
        double env = t < att ? t / std::max(0.001, att) : std::pow(1 - std::min(1.0, (t - att) / dur), 1.6);
        out[i] = (float)(v * env * vol * 0.6);
    }
    return out;
}

std::vector<int16_t> render(const json &song, int rate) {
    g_rng = 22222;
    std::vector<float> mix;
    if (jstr(song, "mode", "music") == "sfx") mix = renderSfx(song.contains("sfx") ? song["sfx"] : json::object(), rate);
    else {
        double sd = 60.0 / std::clamp(jnum(song, "bpm", 120), 20.0, 400.0) / 4;   // un paso: semicorchea
        int steps = std::max(1, (int)jnum(song, "bars", 4)) * 16;
        size_t len = (size_t)(steps * sd * rate);
        std::vector<float> buf(len + rate * 2);
        if (song.contains("tracks") && song["tracks"].is_array())
            for (auto &tr : song["tracks"]) {
                if (jbool(tr, "mute", false) || !tr.contains("notes") || !tr["notes"].is_array()) continue;
                std::string in = jstr(tr, "inst", "piano");
                float vol = (float)jnum(tr, "vol", 0.8);
                for (auto &nt : tr["notes"]) {
                    if (!nt.is_array() || nt.size() < 3) continue;
                    int st = nt[0].get<int>(), m = nt[1].get<int>(), l = std::max(1, nt[2].get<int>());
                    if (st < 0 || st >= steps) continue;
                    renderNote(buf, (size_t)(st * sd * rate), rate, in, m, l * sd * 0.95, vol);
                }
            }
        // lo que suena despues del final vuelve al principio: la musica se repite sin cortes
        mix.assign(buf.begin(), buf.begin() + len);
        for (size_t i = len; i < buf.size(); i++) mix[(i - len) % len] += buf[i];
    }
    std::vector<int16_t> pcm(mix.size());
    for (size_t i = 0; i < mix.size(); i++) pcm[i] = (int16_t)(std::tanh(mix[i] * 0.9f) * 30000);
    return pcm;
}

std::string wavBytes(const std::vector<int16_t> &pcm, int rate) {
    std::string w;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) w += (char)((v >> (8 * i)) & 255); };
    auto u16 = [&](uint16_t v) { w += (char)(v & 255); w += (char)(v >> 8); };
    uint32_t data = (uint32_t)(pcm.size() * 2);
    w += "RIFF"; u32(36 + data); w += "WAVEfmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    w += "data"; u32(data);
    w.append((const char *)pcm.data(), data);
    return w;
}

bool exportWav(const json &song, const std::string &path, std::string &err) {
    std::string w = wavBytes(render(song, 44100), 44100);
    std::ofstream out(path, std::ios::binary);
    if (!out || !out.write(w.data(), w.size())) { err = "No se pudo escribir " + path; return false; }
    return true;
}

static std::string findTool(const char *name) {
    const char *p = getenv("PATH");
    std::string paths = p ? p : "";
#ifdef _WIN32
    char sep = ';'; std::string ext = ".exe";
#else
    char sep = ':'; std::string ext;
#endif
    for (size_t a = 0; a <= paths.size();) {
        size_t b = paths.find(sep, a);
        std::string d = paths.substr(a, b == std::string::npos ? std::string::npos : b - a);
        std::error_code ec;
        if (!d.empty() && fs::exists(fs::path(d) / (name + ext), ec)) return (fs::path(d) / (name + ext)).string();
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return "";
}
bool mp3Available() { return !findTool("lame").empty() || !findTool("ffmpeg").empty(); }

bool exportMp3(const json &song, const std::string &path, std::string &err) {
    std::string lame = findTool("lame"), ff = findTool("ffmpeg");
    if (lame.empty() && ff.empty()) { err = "Para MP3 hace falta lame o ffmpeg instalado. Exporta en .wav o instala uno de los dos."; return false; }
    std::string tmp = (fs::path(sys::makeTempDir()) / "sonido.wav").string();
    if (!exportWav(song, tmp, err)) return false;
    int rc = !lame.empty() ? sys::run({lame, "--quiet", "-b", "192", tmp, path}) : sys::run({ff, "-y", "-loglevel", "error", "-i", tmp, "-b:a", "192k", path});
    std::error_code ec;
    fs::remove_all(fs::path(tmp).parent_path(), ec);
    if (rc != 0) { err = "No se pudo crear el MP3"; return false; }
    return true;
}

std::string saveToProject(Project &proj, const json &song, const std::string &musicId) {
    if (!proj.doc.contains("music") || !proj.doc["music"].is_array()) proj.doc["music"] = json::array();
    json &mus = proj.doc["music"];
    std::string w = wavBytes(render(song, 22050), 22050);   // 22 kHz: la mitad de peso, de sobra para un juego
    std::string data = "data:audio/wav;base64," + base64Encode((const unsigned char *)w.data(), w.size());
    json *e = musicId.empty() ? nullptr : findById(mus, musicId);
    if (!e) { mus.push_back({{"id", makeUid("m")}, {"name", jstr(song, "name", "Sonido")}}); e = &mus.back(); }
    (*e)["mime"] = "audio/wav"; (*e)["data"] = data; (*e)["song"] = song; (*e)["name"] = jstr(song, "name", "Sonido");
    proj.dirty = true;
    return jstr(*e, "id");
}

/* ---------------- reproduccion ---------------- */
static Sound g_song{}, g_notes[8]{};
static int g_noteNext = 0;
static bool ensureAudio() { if (!IsAudioDeviceReady()) InitAudioDevice(); return IsAudioDeviceReady(); }
static Sound soundOf(const std::vector<int16_t> &pcm, int rate) {
    Wave w{};
    w.frameCount = (unsigned)pcm.size(); w.sampleRate = rate; w.sampleSize = 16; w.channels = 1; w.data = (void *)pcm.data();
    return LoadSoundFromWave(w);
}
void stopAll() {
    if (!IsAudioDeviceReady()) return;
    if (g_song.frameCount) { StopSound(g_song); UnloadSound(g_song); g_song = Sound{}; }
    for (auto &s : g_notes) if (s.frameCount) { StopSound(s); UnloadSound(s); s = Sound{}; }
}

static Editor *g_playing = nullptr;
bool anyPlaying() { return g_playing != nullptr; }
// reproductor de la galeria (Inicio > Source Sound): uno a la vez, como las notas de voz
static std::string g_clipKey;
static double g_clipStart = 0, g_clipLen = 0;
void clipStop() {
    if (!g_clipKey.empty() && g_song.frameCount) { StopSound(g_song); UnloadSound(g_song); g_song = Sound{}; }
    g_clipKey.clear();
}
void clipPlay(const std::string &key, const json &song) {
    if (!ensureAudio()) return;
    if (g_playing) g_playing->stop();
    clipStop();
    std::vector<int16_t> pcm = render(song, 44100);
    if (pcm.empty()) return;
    g_song = soundOf(pcm, 44100);
    PlaySound(g_song);
    g_clipKey = key; g_clipStart = GetTime(); g_clipLen = pcm.size() / 44100.0;
}
std::string clipKey() {
    if (!g_clipKey.empty() && (!g_song.frameCount || !IsSoundPlaying(g_song))) clipStop();   // termino solo
    return g_clipKey;
}
float clipProgress() { return g_clipKey.empty() || g_clipLen <= 0 ? 0 : (float)std::min(1.0, (GetTime() - g_clipStart) / g_clipLen); }
std::vector<float> peaks(const json &song, int n, double *seconds) {
    const int rate = 8000;
    std::vector<int16_t> pcm = render(song, rate);
    if (seconds) *seconds = pcm.size() / (double)rate;
    std::vector<float> out(std::max(1, n), 0.0f);
    if (pcm.empty()) return out;
    float mx = 1;
    for (int i = 0; i < n; i++) {
        size_t a = pcm.size() * i / n, b = std::max(a + 1, pcm.size() * (i + 1) / n);
        int pk = 0;
        for (size_t k = a; k < b && k < pcm.size(); k++) pk = std::max(pk, std::abs((int)pcm[k]));
        out[i] = (float)pk;
        mx = std::max(mx, out[i]);
    }
    for (auto &v : out) v /= mx;
    return out;
}
bool Editor::playing() const { return g_playing == this && g_song.frameCount && IsSoundPlaying(g_song); }
void Editor::stop() {
    if (g_playing == this && g_song.frameCount) { StopSound(g_song); UnloadSound(g_song); g_song = Sound{}; }
    if (g_playing == this) g_playing = nullptr;
    playStart = -1;
}
void Editor::play() {
    if (!ensureAudio()) { if (onStatus) onStatus("No hay salida de audio"); return; }
    g_clipKey.clear();
    // la misma cancion que ya sono (repetir en bucle): no se vuelve a calcular
    if (g_song.frameCount && g_playing == this && song == playedSong) StopSound(g_song);
    else {
        if (g_song.frameCount) { StopSound(g_song); UnloadSound(g_song); }
        g_song = soundOf(render(song, 44100), 44100);
        playedSong = song;
    }
    PlaySound(g_song);
    g_playing = this;
    school::note("sound.play");
    // reloj real tomado despues de calcularla: con el de ImGui el cabezal contaba tambien lo que tardo en calcularse
    // y se veia segundos por delante del sonido
    playStart = GetTime();
}
void Editor::preview(int midi) {
    if (!ensureAudio()) return;
    json tr = song["tracks"][track];
    std::string in = jstr(tr, "inst", "piano");
    std::vector<float> buf((size_t)(22050 * (in == "bateria" ? 1.3 : 0.25 + releaseOf(in))));
    renderNote(buf, 0, 22050, in, midi, 0.22, (float)jnum(tr, "vol", 0.8));
    std::vector<int16_t> pcm(buf.size());
    for (size_t i = 0; i < buf.size(); i++) pcm[i] = (int16_t)(std::tanh(buf[i] * 0.9f) * 30000);
    Sound &s = g_notes[g_noteNext];
    g_noteNext = (g_noteNext + 1) % 8;
    if (s.frameCount) { StopSound(s); UnloadSound(s); }
    s = soundOf(pcm, 22050);
    PlaySound(s);
}

/* ---------------- interfaz ---------------- */
std::string Editor::title(Project &) { return "Sonido: " + jstr(song, "name", "Sonido") + "###snd_" + musicId; }

static std::string noteName(int m) { return std::string(kNoteNames[((m % 12) + 12) % 12]) + std::to_string(m / 12 - 1); }
static bool isBlack(int m) { int k = ((m % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

void Editor::drawTop() {
    bool sfx = jstr(song, "mode", "music") == "sfx";
    if (ui::iconButton("sndplay", playing() ? ui::Icon::Stop : ui::Icon::Play, playing(), playing() ? "Parar (Espacio)" : "Escuchar (Espacio)", 30)) { if (playing()) stop(); else play(); }
    school::anchor("sound.play");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    if (editText("##sname", song, "name")) touched();
    ImGui::SameLine();
    int mode = sfx ? 1 : 0;
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("##mode", &mode, "Música\0Efecto de sonido\0")) { song["mode"] = mode ? "sfx" : "music"; stop(); touched(); }
    if (!sfx) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(90);
        if (editNum("##bpm", song, "bpm", 120, 1, "%.0f BPM", 40, 300)) touched();
        ImGui::SameLine(); ImGui::SetNextItemWidth(100);
        if (editNum("##bars", song, "bars", 4, 0.1f, "%.0f compases", 1, 64)) touched();
        ImGui::SameLine(); ImGui::Checkbox("Repetir", &loop);
        ImGui::SameLine(); ImGui::Checkbox("Grabar", &rec);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mientras suena, lo que toques con el teclado (Z S X D C V... y Q 2 W 3 E R...)\nse apunta en la pista elegida");
    }
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Exportar .wav") && onSaveDialog)
        onSaveDialog("Exportar a WAV", ".wav", jstr(song, "name", "sonido") + ".wav", [this](const std::string &f) {
            std::string err;
            if (onStatus) onStatus(exportWav(song, f, err) ? "Exportado: " + f : err);
        });
    ImGui::SameLine();
    bool mp3 = mp3Available();
    ImGui::BeginDisabled(!mp3);
    if (ImGui::Button("Exportar .mp3") && onSaveDialog)
        onSaveDialog("Exportar a MP3", ".mp3", jstr(song, "name", "sonido") + ".mp3", [this](const std::string &f) {
            std::string err;
            if (onStatus) onStatus(exportMp3(song, f, err) ? "Exportado: " + f : err);
        });
    ImGui::EndDisabled();
    if (!mp3 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Para MP3 instala lame o ffmpeg (el .wav funciona siempre)");
}

void Editor::drawTracks() {
    json &trs = song["tracks"];
    if (!trs.is_array()) trs = json::array();
    if (trs.empty()) trs.push_back({{"name", "Piano"}, {"inst", "piano"}, {"vol", 0.8}, {"notes", json::array()}});
    track = std::clamp(track, 0, (int)trs.size() - 1);
    { ImVec2 a = ImGui::GetCursorScreenPos(); school::anchorRect("sound.tracks", a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + std::min(ImGui::GetContentRegionAvail().y, 260.0f))); }
    ImGui::TextDisabled("Pistas");
    int del = -1;
    for (int i = 0; i < (int)trs.size(); i++) {
        json &t = trs[i];
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, i == track ? ui::kPanel3 : ui::kPanel);
        ImGui::BeginChild("##tr", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) track = i;
        bool mute = jbool(t, "mute", false);
        if (ui::iconButton("mute", mute ? ui::Icon::EyeOff : ui::Icon::Eye, false, mute ? "Activar" : "Silenciar", 22)) { t["mute"] = !mute; touched(); }
        ImGui::SameLine(); ImGui::SetNextItemWidth(-28);
        if (editText("##tn", t, "name")) touched();
        ImGui::SameLine();
        if (trs.size() > 1 && ImGui::SmallButton("x")) del = i;
        std::string in = jstr(t, "inst", "piano"), inName = in;
        for (auto &k : kInst) if (in == k.id) inName = k.name;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##inst", inName.c_str())) {
            for (auto &k : kInst) if (ImGui::Selectable(k.name, in == k.id)) {
                if ((in == "bateria") != (std::string(k.id) == "bateria")) t["notes"] = json::array();   // las filas cambian de sentido
                t["inst"] = k.id; track = i; touched();
            }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(-1);
        if (editNum("##vol", t, "vol", 0.8, 0.01f, "volumen %.2f", 0, 1.5)) touched();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (del >= 0) { trs.erase(trs.begin() + del); track = std::min(track, (int)trs.size() - 1); touched(); }
    if (ui::primaryButton("+ Pista", ImVec2(-1, 0))) {
        trs.push_back({{"name", "Pista " + std::to_string(trs.size() + 1)}, {"inst", "sierra"}, {"vol", 0.7}, {"notes", json::array()}});
        track = (int)trs.size() - 1; touched();
    }
}

void Editor::drawRoll() {
    json &tr = song["tracks"][track];
    if (!tr.contains("notes") || !tr["notes"].is_array()) tr["notes"] = json::array();
    json &notes = tr["notes"];
    bool drums = jstr(tr, "inst") == "bateria";
    int steps = std::max(1, (int)jnum(song, "bars", 4)) * 16;
    int lo = drums ? 0 : kLow, hi = drums ? kDrumN - 1 : kHigh;
    float rowH = drums ? 26 : 14, cellW = 22, keysW = drums ? 120 : 56;
    int rows = hi - lo + 1;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##roll", ImVec2(avail.x, avail.y - 96), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    school::anchorRect("sound.roll", ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + std::min(ImGui::GetWindowSize().x, 700.0f), ImGui::GetWindowPos().y + std::min(ImGui::GetWindowSize().y, 320.0f)));
    if ((scrollY < 0 || track != lastTrack) && !drums) {   // empieza centrado en las notas de la pista (sin notas, cerca del Do central)
        double sum = 0; int k = 0;
        for (auto &nt : notes) if (nt.is_array() && nt.size() >= 3) { sum += nt[1].get<int>(); k++; }
        int mid = k ? (int)std::round(sum / k) : 66;
        ImGui::SetScrollY(std::max(0.0f, (kHigh - mid) * rowH - ImGui::GetWindowHeight() / 2));
    }
    lastTrack = track;
    scrollY = ImGui::GetScrollY();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    float W = keysW + steps * cellW, H = rows * rowH;
    ImGui::InvisibleButton("##grid", ImVec2(W, H), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hov = ImGui::IsItemHovered();
    ImVec2 mp = ImGui::GetIO().MousePos;
    auto rowY = [&](int m) { return o.y + (hi - m) * rowH; };
    // fondo y teclas
    for (int m = lo; m <= hi; m++) {
        float y = rowY(m);
        bool blk = !drums && isBlack(m);
        dl->AddRectFilled(ImVec2(o.x + keysW, y), ImVec2(o.x + W, y + rowH), blk ? IM_COL32(28, 28, 34, 255) : IM_COL32(36, 36, 44, 255));
        dl->AddLine(ImVec2(o.x + keysW, y + rowH), ImVec2(o.x + W, y + rowH), IM_COL32(255, 255, 255, 10));
        dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + keysW - 2, y + rowH - 1), blk ? IM_COL32(30, 30, 36, 255) : IM_COL32(225, 225, 232, 255));
        std::string lbl = drums ? kDrums[m] : (m % 12 == 0 ? noteName(m) : "");
        if (!lbl.empty()) dl->AddText(ImVec2(o.x + 6, y + (rowH - ImGui::GetFontSize()) / 2), drums ? IM_COL32(30, 30, 36, 255) : IM_COL32(60, 60, 70, 255), lbl.c_str());
    }
    for (int s = 0; s <= steps; s++) {
        float x = o.x + keysW + s * cellW;
        dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), s % 16 == 0 ? IM_COL32(255, 255, 255, 70) : s % 4 == 0 ? IM_COL32(255, 255, 255, 30) : IM_COL32(255, 255, 255, 10));
    }
    // notas
    int hit = -1;
    for (int i = 0; i < (int)notes.size(); i++) {
        const json &n = notes[i];
        if (!n.is_array() || n.size() < 3) continue;
        int st = n[0].get<int>(), m = n[1].get<int>(), l = n[2].get<int>();
        if (m < lo || m > hi) continue;
        ImVec2 a(o.x + keysW + st * cellW + 1, rowY(m) + 1), b(o.x + keysW + (st + l) * cellW - 1, rowY(m) + rowH - 1);
        bool over = hov && mp.x >= a.x && mp.x < b.x && mp.y >= a.y && mp.y < b.y;
        if (over) hit = i;
        dl->AddRectFilled(a, b, i == dragNote ? ui::kPrimaryLight : over ? IM_COL32(180, 150, 255, 255) : ui::kPrimary, 3);
        dl->AddRectFilled(ImVec2(b.x - 4, a.y + 2), ImVec2(b.x - 2, b.y - 2), IM_COL32(255, 255, 255, 90));
    }
    // raton: izquierdo pone (y arrastra para alargar) o mueve; derecho borra
    int cs = (int)std::floor((mp.x - o.x - keysW) / cellW), cm = hi - (int)std::floor((mp.y - o.y) / rowH);
    if (hov && mp.x > o.x + keysW && ImGui::IsMouseClicked(0)) {
        if (hit >= 0) {
            float right = o.x + keysW + (notes[hit][0].get<int>() + notes[hit][2].get<int>()) * cellW;
            dragNote = hit; dragMode = mp.x > right - 7 ? 1 : 2;
            dragDX = (float)(cs - notes[hit][0].get<int>());
        } else if (cs >= 0 && cs < steps && cm >= lo && cm <= hi) {
            notes.push_back(json::array({cs, cm, 1}));
            dragNote = (int)notes.size() - 1; dragMode = 1;
            preview(cm); touched();
        }
    } else if (hov && mp.x < o.x + keysW && ImGui::IsMouseClicked(0) && cm >= lo && cm <= hi) preview(cm);   // tocar la tecla
    if (dragNote >= 0 && dragNote < (int)notes.size()) {
        if (!ImGui::IsMouseDown(0)) dragNote = -1;
        else {
            json &n = notes[dragNote];
            if (dragMode == 1) { int l = std::clamp(cs - n[0].get<int>() + 1, 1, steps - n[0].get<int>()); if (l != n[2].get<int>()) { n[2] = l; touched(); } }
            else {
                int ns = std::clamp(cs - (int)dragDX, 0, steps - 1), nm = std::clamp(cm, lo, hi);
                if (ns != n[0].get<int>() || nm != n[1].get<int>()) { if (nm != n[1].get<int>()) preview(nm); n[0] = ns; n[1] = nm; touched(); }
            }
        }
    } else dragNote = -1;
    if (hov && ImGui::IsMouseClicked(1) && hit >= 0) {   // borrar: los indices de detras cambian, asi que se olvidan
        notes.erase(notes.begin() + hit);
        hit = -1; dragNote = -1;
        touched();
    }
    if (hov && hit >= 0) ImGui::SetTooltip("%s  ·  arrastra para mover, desde el borde derecho para alargar  ·  clic derecho borra", drums ? kDrums[notes[hit][1].get<int>()] : noteName(notes[hit][1].get<int>()).c_str());
    // cabezal
    if (playing()) {
        double sd = 60.0 / jnum(song, "bpm", 120) / 4, t = GetTime() - playStart;
        float x = o.x + keysW + (float)(std::fmod(t / sd, (double)steps)) * cellW;
        dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + H), IM_COL32(255, 210, 120, 220), 2);
    }
    ImGui::EndChild();
}

// teclado de abajo (dos octavas desde Do4); tambien con el teclado del ordenador
void Editor::drawKeyboard() {
    json &tr = song["tracks"][track];
    bool drums = jstr(tr, "inst") == "bateria";
    static const ImGuiKey low[] = {ImGuiKey_Z, ImGuiKey_S, ImGuiKey_X, ImGuiKey_D, ImGuiKey_C, ImGuiKey_V, ImGuiKey_G, ImGuiKey_B, ImGuiKey_H, ImGuiKey_N, ImGuiKey_J, ImGuiKey_M};
    static const ImGuiKey high[] = {ImGuiKey_Q, ImGuiKey_2, ImGuiKey_W, ImGuiKey_3, ImGuiKey_E, ImGuiKey_R, ImGuiKey_5, ImGuiKey_T, ImGuiKey_6, ImGuiKey_Y, ImGuiKey_7, ImGuiKey_U};
    int base = drums ? 0 : 60;
    auto played = [&](int m) {
        preview(m);
        if (rec && playing()) {   // grabar: se apunta en el paso que suena
            double sd = 60.0 / jnum(song, "bpm", 120) / 4;
            int steps = std::max(1, (int)jnum(song, "bars", 4)) * 16;
            int st = (int)std::fmod(std::round((GetTime() - playStart) / sd), (double)steps);
            tr["notes"].push_back(json::array({st, m, 1}));
            touched();
        }
    };
    if (!ImGui::GetIO().WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        for (int k = 0; k < 12; k++) {
            if (ImGui::IsKeyPressed(low[k], false)) played(drums ? std::min(k, kDrumN - 1) : base + k);
            if (!drums && ImGui::IsKeyPressed(high[k], false)) played(base + 12 + k);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) { if (playing()) stop(); else play(); }
    }
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    if (drums) {
        for (int i = 0; i < kDrumN; i++) {
            ImGui::PushID(i);
            if (i) ImGui::SameLine();
            if (ImGui::Button(kDrums[i], ImVec2(110, 60))) played(i);
            ImGui::PopID();
        }
        return;
    }
    float kw = 30, kh = 80;
    int white = 0;
    std::vector<std::pair<int, float>> blacks;
    for (int m = base; m < base + 24; m++) {
        if (isBlack(m)) { blacks.push_back({m, o.x + white * kw - kw * 0.3f}); continue; }
        ImVec2 a(o.x + white * kw, o.y), b(a.x + kw - 2, o.y + kh);
        bool over = ImGui::IsMouseHoveringRect(a, b);
        bool onBlack = false;
        for (int bm = m - 1; bm <= m + 1; bm++) if (bm != m && isBlack(bm)) {
            float bx = bm < m ? a.x - kw * 0.3f : b.x + 2 - kw * 0.3f;
            if (ImGui::IsMouseHoveringRect(ImVec2(bx, o.y), ImVec2(bx + kw * 0.6f, o.y + kh * 0.6f))) onBlack = true;
        }
        dl->AddRectFilled(a, b, over && !onBlack ? IM_COL32(200, 190, 255, 255) : IM_COL32(235, 235, 240, 255), 3);
        if (m % 12 == 0) dl->AddText(ImVec2(a.x + 4, b.y - 18), IM_COL32(80, 80, 90, 255), noteName(m).c_str());
        if (over && !onBlack && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) played(m);
        white++;
    }
    for (auto &bk : blacks) {
        ImVec2 a(bk.second, o.y), b(bk.second + kw * 0.6f, o.y + kh * 0.6f);
        bool over = ImGui::IsMouseHoveringRect(a, b);
        dl->AddRectFilled(a, b, over ? IM_COL32(120, 100, 200, 255) : IM_COL32(25, 25, 30, 255), 2);
        if (over && ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) played(bk.first);
    }
    ImGui::Dummy(ImVec2(white * kw, kh));
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Toca con el ratón o con el teclado:\nZ S X D C V G B H N J M (Do4)\nQ 2 W 3 E R 5 T 6 Y 7 U (Do5)");
}

void Editor::drawSfx() {
    json &p = song["sfx"];
    if (!p.is_object()) p = json::object();
    struct Pre { const char *name, *wave; double freq, slide, dur, vib, vibS, arp, arpAt, duty; };
    static const Pre pre[] = {{"Salto", "square", 300, 2.0, 0.25, 0, 6, 0, 0.1, 0.5}, {"Moneda", "square", 988, 0, 0.3, 0, 6, 5, 0.06, 0.5},
                              {"Disparo", "saw", 900, -4, 0.18, 0, 6, 0, 0.1, 0.5}, {"Explosión", "noise", 120, -1.5, 0.7, 0, 6, 0, 0.1, 0.5},
                              {"Golpe", "noise", 300, -3, 0.12, 0, 6, 0, 0.1, 0.5}, {"Power-up", "square", 260, 1.2, 0.6, 0.6, 18, 0, 0.1, 0.5},
                              {"Láser", "square", 1500, -6, 0.2, 0, 6, 0, 0.1, 0.25}, {"Clic", "sine", 1200, 0, 0.03, 0, 6, 0, 0.1, 0.5},
                              {"Herido", "saw", 400, -2.5, 0.3, 0.3, 12, 0, 0.1, 0.5}};
    ImGui::SeparatorText("Empieza con uno");
    for (int i = 0; i < (int)(sizeof pre / sizeof pre[0]); i++) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(pre[i].name, ImVec2(96, 40))) {
            const Pre &q = pre[i];
            p = {{"preset", q.name}, {"wave", q.wave}, {"freq", q.freq}, {"slide", q.slide}, {"dur", q.dur}, {"attack", 0.0}, {"vib", q.vib},
                 {"vibSpeed", q.vibS}, {"arp", q.arp}, {"arpAt", q.arpAt}, {"duty", q.duty}, {"vol", 0.8}};
            touched(); play();
        }
    }
    ImGui::SameLine();
    if (ui::primaryButton("Aleatorio", ImVec2(110, 40))) {
        static const char *waves[] = {"square", "saw", "sine", "triangle", "noise"};
        auto r = [](double a, double b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0); };
        p["wave"] = waves[GetRandomValue(0, 4)]; p["freq"] = std::round(r(80, 1600)); p["slide"] = std::round(r(-5, 4) * 10) / 10;
        p["dur"] = std::round(r(0.05, 0.6) * 100) / 100; p["vib"] = GetRandomValue(0, 3) == 0 ? std::round(r(0, 1) * 100) / 100 : 0.0;
        p["arp"] = GetRandomValue(0, 3) == 0 ? (double)GetRandomValue(-12, 12) : 0.0; p["duty"] = std::round(r(0.1, 0.9) * 100) / 100;
        p["preset"] = "Aleatorio";
        touched(); play();
    }
    ImGui::SeparatorText("Ajustes");
    auto row = [](const char *l) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(l); ImGui::SameLine(170); ImGui::SetNextItemWidth(320); };
    bool ch = false;
    row("Onda"); ch |= comboOptions("##w", p, "wave", {{"square", "Cuadrada"}, {"saw", "Sierra"}, {"sine", "Seno"}, {"triangle", "Triángulo"}, {"noise", "Ruido"}}, "square");
    row("Tono"); ch |= editNum("##f", p, "freq", 440, 2, "%.0f Hz", 20, 5000);
    row("Desliza"); ch |= editNum("##s", p, "slide", 0, 0.05f, "%.2f octavas/s", -12, 12);
    row("Duración"); ch |= editNum("##d", p, "dur", 0.3, 0.005f, "%.2f s", 0.01, 5);
    row("Entrada suave"); ch |= editNum("##a", p, "attack", 0, 0.005f, "%.2f s", 0, 2);
    row("Vibrato"); ch |= editNum("##v", p, "vib", 0, 0.01f, "%.2f", 0, 2);
    row("Velocidad vibrato"); ch |= editNum("##vs", p, "vibSpeed", 6, 0.1f, "%.1f Hz", 0.5, 40);
    row("Salto de tono"); ch |= editNum("##ar", p, "arp", 0, 0.1f, "%.0f semitonos", -24, 24);
    row("Cuándo salta"); ch |= editNum("##at", p, "arpAt", 0.1, 0.005f, "%.2f s", 0, 5);
    if (jstr(p, "wave", "square") == "square") { row("Ancho del pulso"); ch |= editNum("##du", p, "duty", 0.5, 0.005f, "%.2f", 0.05, 0.95); }
    row("Volumen"); ch |= editNum("##vo", p, "vol", 0.8, 0.01f, "%.2f", 0, 1.5);
    if (ch) touched();
    if (ImGui::IsItemDeactivatedAfterEdit()) play();
    // forma de la onda
    ImGui::SeparatorText("Forma");
    std::vector<int16_t> pcm = render(song, 8000);
    ImVec2 o = ImGui::GetCursorScreenPos(), sz(std::min(ImGui::GetContentRegionAvail().x, 700.0f), 90);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, ImVec2(o.x + sz.x, o.y + sz.y), ui::kPanel, 6);
    if (!pcm.empty()) for (int x = 0; x < (int)sz.x; x++) {
        size_t a = pcm.size() * x / (size_t)sz.x, b = std::max(a + 1, pcm.size() * (x + 1) / (size_t)sz.x);
        int mn = 0, mx = 0;
        for (size_t i = a; i < b && i < pcm.size(); i++) { mn = std::min(mn, (int)pcm[i]); mx = std::max(mx, (int)pcm[i]); }
        dl->AddLine(ImVec2(o.x + x, o.y + sz.y / 2 - mx * sz.y / 2 / 32768.0f), ImVec2(o.x + x, o.y + sz.y / 2 - mn * sz.y / 2 / 32768.0f + 1), ui::kAccent);
    }
    ImGui::Dummy(sz);
    ImGui::TextDisabled("%.2f s", pcm.size() / 8000.0);
}

void Editor::drawBody() {
    if (!song.is_object() || jstr(song, "format") != "source2d-sound") song = newSong("Sonido");
    if (playing() == false && g_playing == this && loop && jstr(song, "mode", "music") == "music" && g_song.frameCount && !IsSoundPlaying(g_song)) play();
    else if (g_playing == this && g_song.frameCount && !IsSoundPlaying(g_song) && !loop) { g_playing = nullptr; playStart = -1; }
    drawTop();
    ImGui::Spacing();
    if (jstr(song, "mode", "music") == "sfx") {
        ImGui::BeginChild("##sfx");
        drawSfx();
        ImGui::EndChild();
    } else {
        ImVec2 av = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##tracks", ImVec2(210, av.y));
        drawTracks();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##main", ImVec2(0, av.y));
        drawRoll();
        drawKeyboard();
        ImGui::EndChild();
    }
    // en un proyecto: se guarda solo un momento despues del ultimo cambio
    if (editedAt >= 0 && ImGui::GetTime() - editedAt > 0.8 && !ImGui::IsMouseDown(0)) { editedAt = -1; if (onChanged) onChanged(); }
}

void Editor::draw(Project &proj) {
    if (focusMe) { ImGui::SetNextWindowFocus(); focusMe = false; }
    ImGui::SetNextWindowSize(ImVec2(1000, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title(proj).c_str(), &open)) { ImGui::End(); return; }
    ui::noteFocus("sound", musicId);
    drawBody();
    ImGui::End();
    if (!open) stop();
}
}
