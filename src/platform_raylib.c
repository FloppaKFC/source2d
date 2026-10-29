#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
#include "rlgl.h"
#include "platform.h"
#include "gpu_vulkan.h"

static int want_vk = 0, use_vk = 0;
static char vk_info[256];
void plat_request_vulkan(int on) { want_vk = on; }
const char *plat_renderer(void) { return use_vk ? "Vulkan" : "OpenGL"; }
const char *plat_renderer_info(void) { return vk_info; }

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXV 32
#define SAMPLE_RATE 44100.0

typedef struct { double freq, phase, left; int square; int sweep, wave; double f0, f1, dur, t, vol; } Voice;
static Voice voices[MAXV];
static pthread_mutex_t voice_mx = PTHREAD_MUTEX_INITIALIZER;
static AudioStream tone_stream;
static int audio_ok = 0;

static int uncapped_mode = 0;
static int have_mouse = 0, last_mx = -1, last_my = -1;
static char shot_path[1024];

#ifdef S2D_LITE
static unsigned png_crc(unsigned c, const unsigned char *p, size_t n) {
    c = ~c;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1))); }
    return ~c;
}
static void png_be(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void png_chunk(FILE *f, const char *type, const unsigned char *d, unsigned n) {
    unsigned char h[8];
    png_be(h, n); memcpy(h + 4, type, 4);
    unsigned c = png_crc(png_crc(0, h + 4, 4), d, n);
    fwrite(h, 1, 8, f); fwrite(d, 1, n, f);
    png_be(h, c); fwrite(h, 1, 4, f);
}
static void save_shot_img(Image im) {
    FILE *f = fopen(shot_path, "wb");
    if (f && im.data) {
        unsigned w = (unsigned)im.width, h = (unsigned)im.height, row = w * 4 + 1;
        size_t raw = (size_t)row * h, nb = (raw + 65534) / 65535;
        unsigned char *z = malloc(2 + raw + nb * 5 + 4), *q = z, ihdr[13] = { 0 };
        unsigned a = 1, b = 0;
        *q++ = 0x78; *q++ = 1;
        for (size_t i = 0, blk = 0; i < raw; i += blk) {
            blk = raw - i < 65535 ? raw - i : 65535;
            *q++ = i + blk == raw; *q++ = (unsigned char)blk; *q++ = (unsigned char)(blk >> 8);
            *q++ = (unsigned char)~blk; *q++ = (unsigned char)(~blk >> 8);
            for (size_t k = 0; k < blk; k++) {
                size_t o = i + k, y = o / row, x = o % row;
                unsigned char v = x ? ((unsigned char *)im.data)[y * w * 4 + x - 1] : 0;
                *q++ = v; a = (a + v) % 65521; b = (b + a) % 65521;
            }
        }
        png_be(q, (b << 16) | a); q += 4;
        png_be(ihdr, w); png_be(ihdr + 4, h); ihdr[8] = 8; ihdr[9] = 6;
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        png_chunk(f, "IHDR", ihdr, 13);
        png_chunk(f, "IDAT", z, (unsigned)(q - z));
        png_chunk(f, "IEND", NULL, 0);
        free(z);
    }
    if (f) fclose(f);
    UnloadImage(im);
    shot_path[0] = 0;
}
#else
static void save_shot_img(Image im) {
    ExportImage(im, shot_path);
    UnloadImage(im);
    shot_path[0] = 0;
}
#endif
static void save_shot(void) { save_shot_img(LoadImageFromScreen()); }

static void audio_proc(void *buffer, unsigned int frames) {
    float *out = (float *)buffer;
    pthread_mutex_lock(&voice_mx);
    for (unsigned int i = 0; i < frames; i++) {
        double s = 0;
        for (int v = 0; v < MAXV; v++) {
            Voice *V = &voices[v];
            if (V->left <= 0) continue;
            double w, amp;
            if (V->sweep) {
                double k = V->t / V->dur;
                V->freq = V->f0 * pow(V->f1 / V->f0, k);
                amp = (V->vol > 0.01 ? V->vol * pow(0.01 / V->vol, k) : V->vol * (1 - k)) * 0.3;
                switch (V->wave) {
                    case 1: w = V->phase < 0.5 ? 1.0 : -1.0; break;
                    case 2: w = 1.0 - 4.0 * fabs(V->phase - 0.5); break;
                    case 3: w = 2.0 * V->phase - 1.0; break;
                    default: w = sin(2 * M_PI * V->phase);
                }
                V->t += 1.0 / SAMPLE_RATE;
            } else {
                w = V->square ? (V->phase < 0.5 ? 1.0 : -1.0) : sin(2 * M_PI * V->phase);
                amp = 0.07;
            }
            double env = V->left < 0.01 ? V->left / 0.01 : 1.0;
            s += w * env * amp;
            V->phase += V->freq / SAMPLE_RATE;
            if (V->phase >= 1.0) V->phase -= 1.0;
            V->left -= 1.0 / SAMPLE_RATE;
        }
        out[i] = (float)(s > 1 ? 1 : s < -1 ? -1 : s);
    }
    pthread_mutex_unlock(&voice_mx);
}

void plat_tone(double freq, double dur, int square) {
    if (!audio_ok || dur <= 0) return;
    pthread_mutex_lock(&voice_mx);
    for (int v = 0; v < MAXV; v++) {
        if (voices[v].left <= 0) {
            voices[v].freq = freq > 0 ? freq : 440;
            voices[v].phase = 0;
            voices[v].left = dur;
            voices[v].square = square;
            voices[v].sweep = 0;
            break;
        }
    }
    pthread_mutex_unlock(&voice_mx);
}

void plat_sfx(double f0, double f1, double dur, int wave, double vol) {
    if (!audio_ok || dur <= 0 || vol <= 0) return;
    pthread_mutex_lock(&voice_mx);
    for (int v = 0; v < MAXV; v++) {
        if (voices[v].left <= 0) {
            Voice *V = &voices[v];
            V->sweep = 1; V->wave = wave; V->phase = 0; V->t = 0;
            V->f0 = f0 > 1 ? f0 : 1; V->f1 = f1 > 1 ? f1 : V->f0; V->freq = V->f0;
            V->dur = dur; V->left = dur; V->vol = vol > 1 ? 1 : vol;
            break;
        }
    }
    pthread_mutex_unlock(&voice_mx);
}

#define MAX_TRACKS 64
typedef struct {
    char id[96];
    unsigned char *buf;
    Music music;
    int loaded;
} Track;
static Track tracks[MAX_TRACKS];
static int ntracks = 0;
static int cur_track = -1;
static float music_vol = 0.6f;

static Track *find_track(const char *id) {
    for (int i = 0; i < ntracks; i++) if (strcmp(tracks[i].id, id) == 0) return &tracks[i];
    return NULL;
}

int plat_music_load(const char *id, const unsigned char *data, size_t len, const char *ext) {
    if (!audio_ok || !id || !data || len == 0 || !ext) return -1;
    if (find_track(id)) return 0;
    if (ntracks >= MAX_TRACKS) return -1;
    unsigned char *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, data, len);
    char file_type[12];
    snprintf(file_type, sizeof file_type, ".%s", ext);
    Music m = LoadMusicStreamFromMemory(file_type, copy, (int)len);
    if (!IsMusicValid(m)) { free(copy); return -1; }
    Track *t = &tracks[ntracks++];
    snprintf(t->id, sizeof t->id, "%s", id);
    t->buf = copy;
    t->music = m;
    t->loaded = 1;
    SetMusicVolume(t->music, 0.6f);
    return 0;
}

void plat_music_play(const char *id, int loop) {
    if (!audio_ok) return;
    plat_music_stop();
    Track *t = find_track(id);
    if (!t) return;
    t->music.looping = loop ? true : false;
    SetMusicVolume(t->music, music_vol);
    PlayMusicStream(t->music);
    for (int i = 0; i < ntracks; i++) if (&tracks[i] == t) cur_track = i;
}

void plat_music_volume(float v) {
    music_vol = v < 0 ? 0 : v > 1 ? 1 : v;
    if (cur_track >= 0) SetMusicVolume(tracks[cur_track].music, music_vol);
}

void plat_music_seek(double sec) {
    if (cur_track >= 0) SeekMusicStream(tracks[cur_track].music, (float)(sec < 0 ? 0 : sec));
}

int plat_music_playing(void) {
    return cur_track >= 0 && IsMusicStreamPlaying(tracks[cur_track].music);
}

void plat_music_stop(void) {
    if (cur_track >= 0) StopMusicStream(tracks[cur_track].music);
    cur_track = -1;
}

void plat_audio_update(void) {
    if (cur_track < 0) return;
    Track *t = &tracks[cur_track];
    UpdateMusicStream(t->music);
    if (!t->music.looping && !IsMusicStreamPlaying(t->music)) cur_track = -1;
}

#define MAX_SOUNDS 256
#define SND_VOICES 8
#define SND_CHANNELS 8
typedef struct { char id[64]; Sound base; Sound voice[SND_VOICES]; float vol[SND_VOICES]; int ch[SND_VOICES]; unsigned age[SND_VOICES]; int ok; } SoundSlot;
static SoundSlot sounds[MAX_SOUNDS];
static int nsounds = 0;
static unsigned snd_clock = 0;
static float chan_vol[SND_CHANNELS] = { 1, 1, 1, 1, 1, 1, 1, 1 };
static SoundSlot *find_sound(const char *id) {
    for (int i = 0; i < nsounds; i++) if (!strcmp(sounds[i].id, id)) return &sounds[i];
    return NULL;
}
int plat_sound_load(const char *id, const unsigned char *data, size_t len, const char *ext) {
    if (!audio_ok || !id || !data || !len || !ext) return -1;
    if (find_sound(id)) return 0;
    if (nsounds >= MAX_SOUNDS) return -1;
    char ft[12];
    snprintf(ft, sizeof ft, ".%s", ext);
    Wave w = LoadWaveFromMemory(ft, data, (int)len);
    if (!IsWaveValid(w)) return -1;
    SoundSlot *s = &sounds[nsounds];
    memset(s, 0, sizeof *s);
    s->base = LoadSoundFromWave(w);
    UnloadWave(w);
    if (!IsSoundValid(s->base)) return -1;
    snprintf(s->id, sizeof s->id, "%s", id);
    s->voice[0] = s->base;
    for (int v = 1; v < SND_VOICES; v++) s->voice[v] = LoadSoundAlias(s->base);
    s->ok = 1;
    nsounds++;
    return 0;
}
int plat_sound_play(const char *id, double vol, double pitch, double pan, int ch) {
    if (!audio_ok) return -1;
    SoundSlot *s = find_sound(id);
    if (!s) return -1;
    if (ch < 0 || ch >= SND_CHANNELS) ch = 0;
    int best = 0;
    for (int v = 0; v < SND_VOICES; v++) {
        if (!IsSoundPlaying(s->voice[v])) { best = v; break; }
        if (s->age[v] < s->age[best]) best = v;
    }
    Sound *snd = &s->voice[best];
    StopSound(*snd);
    s->vol[best] = (float)(vol < 0 ? 0 : vol > 1 ? 1 : vol);
    s->ch[best] = ch;
    s->age[best] = ++snd_clock;
    SetSoundVolume(*snd, s->vol[best] * chan_vol[ch]);
    SetSoundPitch(*snd, (float)(pitch < 0.05 ? 0.05 : pitch > 8 ? 8 : pitch));
    SetSoundPan(*snd, (float)((1 - (pan < -1 ? -1 : pan > 1 ? 1 : pan)) / 2));
    PlaySound(*snd);
    return best;
}
void plat_channel_stop(int ch) {
    for (int i = 0; i < nsounds; i++) for (int v = 0; v < SND_VOICES; v++)
        if (ch < 0 || sounds[i].ch[v] == ch) StopSound(sounds[i].voice[v]);
}
void plat_channel_pause(int ch, int pause) {
    for (int i = 0; i < nsounds; i++) for (int v = 0; v < SND_VOICES; v++)
        if (ch < 0 || sounds[i].ch[v] == ch) { if (pause) PauseSound(sounds[i].voice[v]); else ResumeSound(sounds[i].voice[v]); }
}
void plat_channel_volume(int ch, double vol) {
    if (ch < 0 || ch >= SND_CHANNELS) return;
    chan_vol[ch] = (float)(vol < 0 ? 0 : vol > 1 ? 1 : vol);
    for (int i = 0; i < nsounds; i++) for (int v = 0; v < SND_VOICES; v++)
        if (sounds[i].ch[v] == ch) SetSoundVolume(sounds[i].voice[v], sounds[i].vol[v] * chan_vol[ch]);
}
int plat_sound_playing(const char *id) {
    SoundSlot *s = find_sound(id);
    if (!s) return 0;
    for (int v = 0; v < SND_VOICES; v++) if (IsSoundPlaying(s->voice[v])) return 1;
    return 0;
}

#define PADS 4
#define PAD_KEYS 16
static const char *pad_key_names[PAD_KEYS] = { "a", "b", "x", "y", "lb", "rb", "lt", "rt", "select", "start", "l3", "r3", "up", "down", "left", "right" };
static unsigned char pad_prev[PADS][PAD_KEYS];
static void poll_pads(PlatDispatchFn fn) {
    static const int btn[12] = { GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
                                 GAMEPAD_BUTTON_RIGHT_FACE_UP, GAMEPAD_BUTTON_LEFT_TRIGGER_1, GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
                                 GAMEPAD_BUTTON_LEFT_TRIGGER_2, GAMEPAD_BUTTON_RIGHT_TRIGGER_2, GAMEPAD_BUTTON_MIDDLE_LEFT,
                                 GAMEPAD_BUTTON_MIDDLE_RIGHT, GAMEPAD_BUTTON_LEFT_THUMB, GAMEPAD_BUTTON_RIGHT_THUMB };
    for (int p = 0; p < PADS; p++) {
        unsigned char now[PAD_KEYS] = { 0 };
        if (IsGamepadAvailable(p)) {
            for (int i = 0; i < 12; i++) now[i] = IsGamepadButtonDown(p, btn[i]) ? 1 : 0;
            float ax = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
            if (GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f) now[6] = 1;
            if (GetGamepadAxisMovement(p, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f) now[7] = 1;
            now[12] = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.5f;
            now[13] = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.5f;
            now[14] = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f;
            now[15] = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f;
        }
        for (int i = 0; i < PAD_KEYS; i++) if (now[i] != pad_prev[p][i]) {
            char name[24];
            if (p == 0) snprintf(name, sizeof name, "pad_%s", pad_key_names[i]);
            else snprintf(name, sizeof name, "pad%d_%s", p + 1, pad_key_names[i]);
            fn(now[i] ? "keydown" : "keyup", name, 0, 0, 0, 0);
            pad_prev[p][i] = now[i];
        }
    }
}
double plat_pad_axis(int pad, int axis) {
    if (pad < 0 || pad >= PADS || !IsGamepadAvailable(pad)) return 0;
    static const int ax[6] = { GAMEPAD_AXIS_LEFT_X, GAMEPAD_AXIS_LEFT_Y, GAMEPAD_AXIS_RIGHT_X, GAMEPAD_AXIS_RIGHT_Y, GAMEPAD_AXIS_LEFT_TRIGGER, GAMEPAD_AXIS_RIGHT_TRIGGER };
    if (axis < 0 || axis > 5) return 0;
    double v = GetGamepadAxisMovement(pad, ax[axis]);
    if (axis >= 4) return (v + 1) / 2;
    return fabs(v) < 0.15 ? 0 : v;
}
int plat_pad_connected(int pad) { return pad >= 0 && pad < PADS && IsGamepadAvailable(pad); }
void plat_pad_vibrate(int pad, double left, double right, double dur) {
    if (pad >= 0 && pad < PADS && IsGamepadAvailable(pad)) SetGamepadVibration(pad, (float)left, (float)right, (float)dur);
}

char *plat_dir_list(const char *path) {
    DIR *d = opendir(path);
    if (!d) return NULL;
    size_t cap = 4096, n = 0;
    char *out = malloc(cap);
    struct dirent *e;
    while (out && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char full[4400];
        snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat st;
        int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        size_t l = strlen(e->d_name) + 3;
        if (n + l + 1 > cap) { cap = (n + l + 1) * 2; char *nb = realloc(out, cap); if (!nb) { free(out); out = NULL; break; } out = nb; }
        n += (size_t)snprintf(out + n, cap - n, "%s%s\n", e->d_name, isdir ? "/" : "");
    }
    closedir(d);
    if (out) out[n] = 0;
    return out;
}

unsigned char *plat_image_decode(const unsigned char *data, size_t len, int *w, int *h) {
    static const char *types[] = { ".png", ".jpg", ".bmp", ".gif", ".qoi" };
    const char *t = ".png";
    if (len > 3 && data[0] == 0xFF && data[1] == 0xD8) t = types[1];
    else if (len > 2 && data[0] == 'B' && data[1] == 'M') t = types[2];
    else if (len > 3 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F') t = types[3];
    else if (len > 4 && memcmp(data, "qoif", 4) == 0) t = types[4];
    Image img = LoadImageFromMemory(t, data, (int)len);
    if (!img.data) return NULL;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    size_t n = (size_t)img.width * img.height * 4;
    unsigned char *out = malloc(n);
    if (out) { memcpy(out, img.data, n); *w = img.width; *h = img.height; }
    UnloadImage(img);
    return out;
}

static char *replace_all(const char *s, const char *from, const char *to) {
    size_t fl = strlen(from), tl = strlen(to), count = 0;
    for (const char *p = s; (p = strstr(p, from)) != NULL; p += fl) count++;
    char *out = malloc(strlen(s) + count * tl + 1);
    if (!out) return NULL;
    char *w = out;
    const char *p = s;
    const char *q;
    while ((q = strstr(p, from)) != NULL) {
        memcpy(w, p, (size_t)(q - p)); w += q - p;
        memcpy(w, to, tl); w += tl;
        p = q + fl;
    }
    strcpy(w, p);
    return out;
}
static char *glsl_to_330_mode(const char *src, int gpu) {
    int gl21 = rlGetVersion() == RL_OPENGL_21;
    static const char *HEADER33 =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec4 fragColor;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform float u_time;\n"
        "uniform vec2 u_res;\n"
        "uniform vec3 u_cam;\n"
        "uniform float u_amount;\n"
        "out vec4 finalColor;\n";
    static const char *HEADER21 =
        "#version 120\n"
        "varying vec2 fragTexCoord;\n"
        "varying vec4 fragColor;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform float u_time;\n"
        "uniform vec2 u_res;\n"
        "uniform vec3 u_cam;\n"
        "uniform float u_amount;\n";
    const char *HEADER = gl21 ? HEADER21 : HEADER33;
    char *a = replace_all(src, "precision highp float;", "");
    if (!a) return NULL;
    char *b;
    const char *drop[] = { "precision mediump float;", "varying vec2 v_uv;", "uniform sampler2D u_tex;",
                           "uniform float u_time;", "uniform vec2 u_res;", "uniform vec3 u_cam;", "uniform float u_amount;" };
    for (size_t i = 0; i < sizeof drop / sizeof drop[0]; i++) {
        b = replace_all(a, drop[i], ""); free(a); a = b; if (!a) return NULL;
    }
    const char *pairs[][2] = { { "texture2D(", gl21 ? "texture2D(" : "texture(" }, { "gl_FragColor", gl21 ? "gl_FragColor" : "finalColor" },
                               { "v_uv", "fragTexCoord" }, { "u_tex", "texture0" } };
    const char *pairs_gpu[][2] = { { "texture2D(", "s2d_tex(" }, { "gl_FragColor", gl21 ? "gl_FragColor" : "finalColor" },
                                   { "v_uv", "S2D_UV" }, { "u_tex", "texture0" } };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        b = gpu ? replace_all(a, pairs_gpu[i][0], pairs_gpu[i][1]) : replace_all(a, pairs[i][0], pairs[i][1]);
        free(a); a = b; if (!a) return NULL;
    }
    const char *GPU_EXTRA = gl21 ?
        "#define S2D_UV vec2(fragTexCoord.x, 1.0 - fragTexCoord.y)\n"
        "vec4 s2d_tex(sampler2D s, vec2 p) { return texture2D(s, vec2(p.x, 1.0 - p.y)); }\n" :
        "#define S2D_UV vec2(fragTexCoord.x, 1.0 - fragTexCoord.y)\n"
        "vec4 s2d_tex(sampler2D s, vec2 p) { return texture(s, vec2(p.x, 1.0 - p.y)); }\n";
    size_t hl = strlen(HEADER), el = gpu ? strlen(GPU_EXTRA) : 0, al = strlen(a);
    char *out = malloc(hl + el + al + 1);
    if (!out) { free(a); return NULL; }
    memcpy(out, HEADER, hl); if (el) memcpy(out + hl, GPU_EXTRA, el); memcpy(out + hl + el, a, al + 1);
    free(a);
    return out;
}

typedef struct {
    char *src;
    Shader sh;
    int ok, loc_time, loc_res, loc_cam, loc_amount, gpu;
} PassShader;
static PassShader pass_shaders[32];
static int npass_shaders = 0;
static PassShader *pass_shader_mode(const char *glsl, int gpu) {
    for (int i = 0; i < npass_shaders; i++) if (pass_shaders[i].gpu == gpu && strcmp(pass_shaders[i].src, glsl) == 0) return &pass_shaders[i];
    if (npass_shaders >= 32) return NULL;
    PassShader *ps = &pass_shaders[npass_shaders++];
    ps->src = strdup(glsl);
    ps->ok = 0;
    ps->gpu = gpu;
    char *fs = glsl_to_330_mode(glsl, gpu);
    if (fs) {
        ps->sh = LoadShaderFromMemory(NULL, fs);
        free(fs);
        if (IsShaderValid(ps->sh)) {
            ps->ok = 1;
            ps->loc_time = GetShaderLocation(ps->sh, "u_time");
            ps->loc_res = GetShaderLocation(ps->sh, "u_res");
            ps->loc_cam = GetShaderLocation(ps->sh, "u_cam");
            ps->loc_amount = GetShaderLocation(ps->sh, "u_amount");
        } else {
            fprintf(stderr, "Source 2D engine: shader de capa no valido (se dibuja sin shader)\n");
        }
    }
    return ps;
}
static float pass_amount = 1.0f;
void plat_shader_amount(float a) { pass_amount = a; }

#include "s2d_diag.h"
int plat_diagnose(const char *programa, char *resumen, size_t n) {
    s2d_diag_begin(programa);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(320, 200, "Source 2D - diagnostico");
    if (!IsWindowReady()) {
        snprintf(resumen, n, "No se pudo abrir la ventana: falta OpenGL 2.1 (controladores de video) o el servidor grafico (X11).");
        s2d_diag_end(0, resumen);
        return 1;
    }
    int t = plat_gpu_target(64, 64);
    int sh = t >= 0 ? plat_gpu_shader(t, "precision mediump float; varying vec2 v_uv; uniform sampler2D u_tex; void main() { gl_FragColor = texture2D(u_tex, v_uv); }", 0, (float[3]){ 0, 0, 1 }) : -1;
    int rc[4] = { 0, 0, 64, 64 };
    int fx = t >= 0 ? plat_gpu_blur(t, 2, rc) : -1;
    InitAudioDevice();
    int au = IsAudioDeviceReady();
    snprintf(resumen, n, "Ventana y OpenGL: bien. Lienzos de la GPU: %s. Shaders: %s. Filtros: %s. Audio: %s.",
             t >= 0 ? "bien" : "FALLA", sh == 0 ? "bien" : "FALLA", fx == 0 ? "bien" : "FALLA", au ? "bien" : "sin dispositivo de audio");
    s2d_diag_end(t >= 0 && sh == 0, resumen);
    if (au) CloseAudioDevice();
    CloseWindow();
    return t >= 0 && sh == 0 ? 0 : 1;
}

int plat_init(int w, int h, const char *title, int uncapped) {
    uncapped_mode = uncapped;
    if (!getenv("S2D_LOG")) SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | (uncapped ? 0u : FLAG_VSYNC_HINT));
    InitWindow(w, h, title);
    if (!IsWindowReady()) {
        fprintf(stderr, "Source 2D engine: no se pudo abrir la ventana (raylib)\n");
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(0);
    if (want_vk) {
        use_vk = vk_init(GetRenderWidth(), GetRenderHeight(), uncapped, vk_info, sizeof vk_info) == 0;
        if (use_vk) fprintf(stderr, "Source 2D engine: dibujo con Vulkan (%s)\n", vk_info);
        else { fprintf(stderr, "Source 2D engine: Vulkan no esta disponible (%s); se usa OpenGL\n", vk_info[0] ? vk_info : "no arranco"); vk_shutdown(); }
    }
    InitAudioDevice();
    if (IsAudioDeviceReady()) {
        tone_stream = LoadAudioStream((unsigned int)SAMPLE_RATE, 32, 1);
        if (IsAudioStreamValid(tone_stream)) {
            AttachAudioStreamProcessor(tone_stream, audio_proc);
            PlayAudioStream(tone_stream);
            audio_ok = 1;
        }
    }
    return 0;
}

void plat_shutdown(void) {
    for (int i = 0; i < ntracks; i++) {
        if (tracks[i].loaded) UnloadMusicStream(tracks[i].music);
        free(tracks[i].buf);
        tracks[i].buf = NULL;
    }
    ntracks = 0;
    cur_track = -1;
    if (audio_ok) {
        DetachAudioStreamProcessor(tone_stream, audio_proc);
        UnloadAudioStream(tone_stream);
        audio_ok = 0;
    }
    if (IsAudioDeviceReady()) CloseAudioDevice();
    for (int i = 0; i < npass_shaders; i++) {
        if (pass_shaders[i].ok) UnloadShader(pass_shaders[i].sh);
        free(pass_shaders[i].src);
        pass_shaders[i].src = NULL;
    }
    npass_shaders = 0;
    if (use_vk) { vk_shutdown(); use_vk = 0; }
    CloseWindow();
}

uint32_t plat_ticks(void) { return (uint32_t)(GetTime() * 1000.0); }
double plat_time_ms(void) { return GetTime() * 1000.0; }

void plat_wait_frame(uint32_t frame_start, int uncapped) {
    if (uncapped || uncapped_mode) return;
    static double target = 0;
    if (target <= 0) {
        int hz = GetMonitorRefreshRate(GetCurrentMonitor());
        if (hz < 30 || hz > 500) hz = 60;
        target = 1000.0 / hz - 1.0;
    }
    double el = (double)plat_ticks() - (double)frame_start;
    if (el < target) WaitTime((target - el) / 1000.0);
}

static const char *key_name(int k, char *buf) {
    switch (k) {
    case KEY_SPACE: return " ";
    case KEY_ENTER: case KEY_KP_ENTER: return "Enter";
    case KEY_ESCAPE: return "Escape";
    case KEY_BACKSPACE: return "Backspace";
    case KEY_TAB: return "Tab";
    case KEY_LEFT_SHIFT: case KEY_RIGHT_SHIFT: return "Shift";
    case KEY_LEFT_CONTROL: case KEY_RIGHT_CONTROL: return "Control";
    case KEY_LEFT_ALT: case KEY_RIGHT_ALT: return "Alt";
    case KEY_UP: return "ArrowUp";
    case KEY_DOWN: return "ArrowDown";
    case KEY_LEFT: return "ArrowLeft";
    case KEY_RIGHT: return "ArrowRight";
    case KEY_DELETE: return "Delete";
    case KEY_HOME: return "Home";
    case KEY_END: return "End";
    default: break;
    }
    if (k >= KEY_F1 && k <= KEY_F12) { snprintf(buf, 8, "F%d", k - KEY_F1 + 1); return buf; }
    if (k >= 'A' && k <= 'Z') { buf[0] = (char)(k + 32); buf[1] = 0; return buf; }
    if (k >= 32 && k < 127) { buf[0] = (char)k; buf[1] = 0; return buf; }
    return "";
}

#define KEY_MAX_SCAN 350

int plat_poll(PlatDispatchFn fn) {
    char kb[8];
    if (WindowShouldClose()) return 0;
    for (int k = 1; k < KEY_MAX_SCAN; k++) {
        if (IsKeyPressed(k) || IsKeyPressedRepeat(k)) {
            int rep = IsKeyPressedRepeat(k) ? 1 : 0;
            fn("keydown", key_name(k, kb), rep, 0, 0, 0);
        }
        if (IsKeyReleased(k)) fn("keyup", key_name(k, kb), 0, 0, 0, 0);
    }
    poll_pads(fn);
    int mx = GetMouseX(), my = GetMouseY();
    if (!have_mouse || mx != last_mx || my != last_my) {
        fn("mousemove", "", 0, mx, my, 0);
        last_mx = mx; last_my = my; have_mouse = 1;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) fn("mousedown", "", 0, mx, my, 0);
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) fn("mousedown", "", 0, mx, my, 1);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) fn("mousedown", "", 0, mx, my, 2);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) fn("mouseup", "", 0, mx, my, 0);
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) fn("mouseup", "", 0, mx, my, 1);
    if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) fn("mouseup", "", 0, mx, my, 2);
    float wh = GetMouseWheelMove();
    if (wh != 0) fn("wheel", "", 0, mx, my, wh > 0 ? -1 : 1);
    return 1;
}

void plat_fit_window(int w, int h) {
    int mon = GetCurrentMonitor();
    int mw = GetMonitorWidth(mon), mh = GetMonitorHeight(mon);
    double fit = 1.0;
    if (mw > 0 && mh > 0) fit = fmin(1.0, fmin(mw * 0.95 / w, mh * 0.90 / h));
    int nw = (int)floor(w * fit), nh = (int)floor(h * fit);
    if (nw < 1) nw = w;
    if (nh < 1) nh = h;
    SetWindowSize(nw, nh);
    Vector2 mp = GetMonitorPosition(mon);
    SetWindowPosition((int)mp.x + (mw - nw) / 2, (int)mp.y + (mh - nh) / 2);
}

#define GPU_TARGETS 1024
#define GPU_TEXTURES 16384
typedef struct { RenderTexture2D rt, alt; int w, h, used, has_alt; } GpuTarget;
static GpuTarget gtargets[GPU_TARGETS];
static Texture2D gtex[GPU_TEXTURES];
static int gtex_used[GPU_TEXTURES], gtex_next = 0;
static int gcur = -1, gblend = -1;
static uint8_t *gpu_tmp = NULL;
static size_t gpu_tmp_cap = 0;

static uint8_t *gpu_scratch(size_t n) {
    if (n > gpu_tmp_cap) { free(gpu_tmp); gpu_tmp = malloc(n); gpu_tmp_cap = gpu_tmp ? n : 0; }
    return gpu_tmp;
}
static void gpu_flush(void) { rlDrawRenderBatchActive(); }
static void gpu_unbind(void) {
    if (gcur >= 0) { gpu_flush(); EndTextureMode(); }
    gcur = -1;
}
static int gpu_bind(int id) {
    if (id < 0 || id >= GPU_TARGETS || !gtargets[id].used) return 0;
    if (gcur == id) return 1;
    gpu_unbind();
    BeginTextureMode(gtargets[id].rt);
    rlDisableBackfaceCulling();
    gcur = id;
    gblend = -1;
    return 1;
}
static void gpu_blend(int mode) {
    if (mode == gblend) return;
    gpu_flush();
    if (mode == 1) rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
    else if (mode == 2) rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    else if (mode == 3) rlSetBlendFactors(RL_ZERO, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
    else rlSetBlendFactors(RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    gblend = mode;
}
static void gpu_tex_params(Texture2D t) {
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    SetTextureWrap(t, TEXTURE_WRAP_CLAMP);
}
static void gpu_clear_rt(RenderTexture2D rt) {
    BeginTextureMode(rt);
    ClearBackground(BLANK);
    EndTextureMode();
}

int plat_gpu_target(int w, int h) {
    if (!IsWindowReady() || w <= 0 || h <= 0) return -1;
    if (use_vk) return vk_target(w, h);
    for (int i = 0; i < GPU_TARGETS; i++) {
        if (gtargets[i].used) continue;
        gpu_unbind();
        GpuTarget *t = &gtargets[i];
        t->rt = LoadRenderTexture(w, h);
        if (!t->rt.id) return -1;
        gpu_tex_params(t->rt.texture);
        gpu_clear_rt(t->rt);
        t->w = w; t->h = h; t->used = 1; t->has_alt = 0;
        return i;
    }
    return -1;
}
void plat_gpu_target_free(int id) {
    if (use_vk) { vk_target_free(id); return; }
    if (id < 0 || id >= GPU_TARGETS || !gtargets[id].used) return;
    if (gcur == id) gpu_unbind();
    UnloadRenderTexture(gtargets[id].rt);
    if (gtargets[id].has_alt) UnloadRenderTexture(gtargets[id].alt);
    gtargets[id].used = 0;
}

int plat_gpu_tex(int id, const uint8_t *rgba, int w, int h) {
    if (!IsWindowReady() || !rgba || w <= 0 || h <= 0) return -1;
    if (use_vk) return vk_tex(id, rgba, w, h);
    size_t n = (size_t)w * h * 4;
    uint8_t *pm = gpu_scratch(n);
    if (!pm) return -1;
    for (size_t i = 0; i < n; i += 4) {
        unsigned a = rgba[i + 3];
        if (a == 255) { memcpy(pm + i, rgba + i, 4); continue; }
        pm[i] = (uint8_t)((rgba[i] * a + 127) / 255); pm[i + 1] = (uint8_t)((rgba[i + 1] * a + 127) / 255);
        pm[i + 2] = (uint8_t)((rgba[i + 2] * a + 127) / 255); pm[i + 3] = (uint8_t)a;
    }
    gpu_flush();
    if (id >= 0 && id < GPU_TEXTURES && gtex_used[id] && gtex[id].width == w && gtex[id].height == h) {
        UpdateTexture(gtex[id], pm);
        return id;
    }
    if (id >= 0 && id < GPU_TEXTURES && gtex_used[id]) { UnloadTexture(gtex[id]); gtex_used[id] = 0; }
    int slot = -1;
    for (int k = 0; k < GPU_TEXTURES; k++) {
        int i = (gtex_next + k) % GPU_TEXTURES;
        if (!gtex_used[i]) { slot = i; break; }
    }
    if (slot < 0) return -1;
    Image img = { pm, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    gtex[slot] = LoadTextureFromImage(img);
    if (!gtex[slot].id) return -1;
    gpu_tex_params(gtex[slot]);
    gtex_used[slot] = 1;
    gtex_next = slot + 1;
    return slot;
}
void plat_gpu_tex_free(int id) {
    if (use_vk) { vk_tex_free(id); return; }
    if (id < 0 || id >= GPU_TEXTURES || !gtex_used[id]) return;
    gpu_flush();
    UnloadTexture(gtex[id]);
    gtex_used[id] = 0;
}

void plat_gpu_draw(int target, int src_rt, int src, double sx, double sy, double sw, double sh,
                   double dx, double dy, double dw, double dh, const double m[6], double alpha, int mode) {
    if (use_vk) { vk_draw(target, src_rt, src, sx, sy, sw, sh, dx, dy, dw, dh, m, alpha, mode); return; }
    if (!gpu_bind(target)) return;
    Texture2D t;
    if (src_rt) { if (src < 0 || src >= GPU_TARGETS || !gtargets[src].used || src == target) return; t = gtargets[src].rt.texture; }
    else { if (src < 0 || src >= GPU_TEXTURES || !gtex_used[src]) return; t = gtex[src]; }
    if (alpha <= 0) return;
    gpu_blend(mode);
    float u0 = (float)(sx / t.width), u1 = (float)((sx + sw) / t.width);
    float v0 = (float)(sy / t.height), v1 = (float)((sy + sh) / t.height);
    if (src_rt) { v0 = 1.0f - v0; v1 = 1.0f - v1; }
    float X[4], Y[4];
    double px[4] = { dx, dx, dx + dw, dx + dw }, py[4] = { dy, dy + dh, dy + dh, dy };
    for (int i = 0; i < 4; i++) { X[i] = (float)(m[0] * px[i] + m[2] * py[i] + m[4]); Y[i] = (float)(m[1] * px[i] + m[3] * py[i] + m[5]); }
    unsigned char a = (unsigned char)(alpha >= 1 ? 255 : alpha * 255 + 0.5);
    rlSetTexture(t.id);
    rlBegin(RL_QUADS);
    rlColor4ub(a, a, a, a);
    rlTexCoord2f(u0, v0); rlVertex2f(X[0], Y[0]);
    rlTexCoord2f(u0, v1); rlVertex2f(X[1], Y[1]);
    rlTexCoord2f(u1, v1); rlVertex2f(X[2], Y[2]);
    rlTexCoord2f(u1, v0); rlVertex2f(X[3], Y[3]);
    rlEnd();
    rlSetTexture(0);
}
void plat_gpu_fill(int target, const double *pts, int n, double r, double g, double b, double a, int mode) {
    if (use_vk) { vk_fill(target, pts, n, r, g, b, a, mode); return; }
    if (!gpu_bind(target) || n < 3) return;
    if (a <= 0 && mode != 2) return;
    gpu_blend(mode);
    if (a > 1) a = 1;
    unsigned char R = (unsigned char)(r * a), G = (unsigned char)(g * a), B = (unsigned char)(b * a), A = (unsigned char)(a * 255 + 0.5);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(R, G, B, A);
    for (int i = 1; i + 1 < n; i++) {
        rlVertex2f((float)pts[0], (float)pts[1]);
        rlVertex2f((float)pts[2 * i + 2], (float)pts[2 * i + 3]);
        rlVertex2f((float)pts[2 * i], (float)pts[2 * i + 1]);
    }
    rlEnd();
}
void plat_gpu_rect(int target, double x0, double y0, double x1, double y1, double r, double g, double b, double a, int mode) {
    double p[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
    plat_gpu_fill(target, p, 4, r, g, b, a, mode);
}
int plat_gpu_shader(int target, const char *glsl, double t, const float cam[3]) {
    if (use_vk) return vk_shader(target, glsl, t, cam, pass_amount);
    if (target < 0 || target >= GPU_TARGETS || !gtargets[target].used || !glsl) return -1;
    PassShader *ps = pass_shader_mode(glsl, 1);
    if (!ps || !ps->ok) return -1;
    GpuTarget *g = &gtargets[target];
    gpu_unbind();
    if (!g->has_alt) { g->alt = LoadRenderTexture(g->w, g->h); gpu_tex_params(g->alt.texture); g->has_alt = 1; }
    float tf = (float)t, res[2] = { (float)g->w, (float)g->h }, camv[3] = { cam[0], cam[1], cam[2] ? cam[2] : 1.0f };
    BeginTextureMode(g->alt);
    rlDisableBackfaceCulling();
    ClearBackground(BLANK);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    BeginShaderMode(ps->sh);
    if (ps->loc_time >= 0) SetShaderValue(ps->sh, ps->loc_time, &tf, SHADER_UNIFORM_FLOAT);
    if (ps->loc_res >= 0) SetShaderValue(ps->sh, ps->loc_res, res, SHADER_UNIFORM_VEC2);
    if (ps->loc_cam >= 0) SetShaderValue(ps->sh, ps->loc_cam, camv, SHADER_UNIFORM_VEC3);
    if (ps->loc_amount >= 0) SetShaderValue(ps->sh, ps->loc_amount, &pass_amount, SHADER_UNIFORM_FLOAT);
    DrawTexturePro(g->rt.texture, (Rectangle){ 0.0f, 0.0f, (float)g->w, (float)-g->h },
                   (Rectangle){ 0.0f, 0.0f, (float)g->w, (float)g->h }, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndTextureMode();
    RenderTexture2D tmp = g->rt; g->rt = g->alt; g->alt = tmp;
    gblend = -1;
    return 0;
}
static const char *FS_COLOR =
    "uniform vec3 u_m0;\n"
    "uniform vec3 u_m1;\n"
    "uniform vec3 u_m2;\n"
    "uniform vec2 u_k;\n"
    "void main() {\n"
    "  vec4 c = texture2D(u_tex, v_uv);\n"
    "  if (c.a <= 0.0) { gl_FragColor = c; return; }\n"
    "  vec3 s = c.rgb / c.a; float a = c.a, k = u_k.y;\n"
    "  if (u_k.x < 0.5) s = vec3(dot(u_m0, s), dot(u_m1, s), dot(u_m2, s));\n"
    "  else if (u_k.x < 1.5) s = s + k * (1.0 - 2.0 * s);\n"
    "  else if (u_k.x < 2.5) s = s * k;\n"
    "  else if (u_k.x < 3.5) s = (s - 0.5) * k + 0.5;\n"
    "  else a = clamp(a * k, 0.0, 1.0);\n"
    "  s = clamp(s, 0.0, 1.0);\n"
    "  gl_FragColor = vec4(s * a, a);\n"
    "}\n";
static const char *FS_BLUR =
    "uniform vec4 u_rect;\n"
    "uniform vec3 u_blur;\n"
    "void main() {\n"
    "  vec2 p = v_uv * u_res; vec4 sum = vec4(0.0); float r = u_blur.z;\n"
    "  for (int i = -64; i <= 64; i++) {\n"
    "    float f = float(i);\n"
    "    if (f < -r || f > r) continue;\n"
    "    vec2 q = p + u_blur.xy * f;\n"
    "    if (q.x < u_rect.x || q.y < u_rect.y || q.x >= u_rect.z || q.y >= u_rect.w) continue;\n"
    "    sum += texture2D(u_tex, q / u_res);\n"
    "  }\n"
    "  gl_FragColor = sum / (2.0 * r + 1.0);\n"
    "}\n";
static const char *FS_GRAD =
    "uniform vec4 u_g;\n"
    "uniform vec4 u_col[8];\n"
    "uniform float u_pos[8];\n"
    "uniform vec2 u_n;\n"
    "void main() {\n"
    "  float d = distance(fragTexCoord, u_g.xy), span = u_g.w - u_g.z;\n"
    "  float t = span > 0.000001 ? (d - u_g.z) / span : (d >= u_g.w ? 1.0 : 0.0);\n"
    "  int n = int(u_n.x + 0.5);\n"
    "  vec4 c = u_col[0];\n"
    "  for (int i = 1; i < 8; i++) {\n"
    "    if (i >= n) break;\n"
    "    if (t >= u_pos[i]) c = u_col[i];\n"
    "    else if (t > u_pos[i - 1]) {\n"
    "      float sp = u_pos[i] - u_pos[i - 1];\n"
    "      c = mix(u_col[i - 1], u_col[i], sp > 0.000001 ? (t - u_pos[i - 1]) / sp : 0.0);\n"
    "      break;\n"
    "    } else break;\n"
    "  }\n"
    "  float a = c.a * u_n.y;\n"
    "  gl_FragColor = vec4(c.rgb * a, a);\n"
    "}\n";
typedef struct { Shader sh; int ok, tried; int loc[6]; } Builtin;
static Builtin b_color, b_blur, b_grad;
static Builtin *builtin(Builtin *b, const char *body, const char *const names[], int nn) {
    if (b->tried) return b->ok ? b : NULL;
    b->tried = 1;
    char *src = malloc(strlen(body) + 128);
    if (!src) return NULL;
    sprintf(src, "precision mediump float;\nvarying vec2 v_uv;\nuniform sampler2D u_tex;\nuniform vec2 u_res;\n%s", body);
    char *fs = glsl_to_330_mode(src, 1);
    free(src);
    if (!fs) return NULL;
    b->sh = LoadShaderFromMemory(NULL, fs);
    free(fs);
    if (!IsShaderValid(b->sh)) { fprintf(stderr, "Source 2D engine: no se pudo compilar un shader interno\n"); return NULL; }
    for (int i = 0; i < nn; i++) b->loc[i] = GetShaderLocation(b->sh, names[i]);
    b->ok = 1;
    return b;
}
static int gpu_zone_pass(int target, Shader sh, const int rc[4], void (*set)(Shader, const void *), const void *arg) {
    if (target < 0 || target >= GPU_TARGETS || !gtargets[target].used) return -1;
    GpuTarget *g = &gtargets[target];
    int x0 = rc[0] < 0 ? 0 : rc[0], y0 = rc[1] < 0 ? 0 : rc[1], x1 = rc[2] > g->w ? g->w : rc[2], y1 = rc[3] > g->h ? g->h : rc[3];
    if (x1 <= x0 || y1 <= y0) return 0;
    gpu_unbind();
    if (!g->has_alt) { g->alt = LoadRenderTexture(g->w, g->h); gpu_tex_params(g->alt.texture); g->has_alt = 1; }
    float res[2] = { (float)g->w, (float)g->h };
    Rectangle flip = { 0.0f, 0.0f, (float)g->w, (float)-g->h }, full = { 0.0f, 0.0f, (float)g->w, (float)g->h };
    BeginTextureMode(g->alt);
    rlDisableBackfaceCulling();
    BeginScissorMode(x0, y0, x1 - x0, y1 - y0);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    BeginShaderMode(sh);
    int lr = GetShaderLocation(sh, "u_res");
    if (lr >= 0) SetShaderValue(sh, lr, res, SHADER_UNIFORM_VEC2);
    set(sh, arg);
    DrawTexturePro(g->rt.texture, flip, full, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndShaderMode();
    EndScissorMode();
    EndTextureMode();
    BeginTextureMode(g->rt);
    rlDisableBackfaceCulling();
    BeginScissorMode(x0, y0, x1 - x0, y1 - y0);
    rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    DrawTexturePro(g->alt.texture, flip, full, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    EndScissorMode();
    EndTextureMode();
    gblend = -1;
    return 0;
}
typedef struct { float m[9], k[2]; } ColorArgs;
static void set_color(Shader sh, const void *p) {
    const ColorArgs *a = p;
    for (int i = 0; i < 3; i++) if (b_color.loc[i] >= 0) SetShaderValue(sh, b_color.loc[i], a->m + 3 * i, SHADER_UNIFORM_VEC3);
    if (b_color.loc[3] >= 0) SetShaderValue(sh, b_color.loc[3], a->k, SHADER_UNIFORM_VEC2);
}
int plat_gpu_color(int target, int kind, double amt, const int rc[4]) {
    static const char *const names[] = { "u_m0", "u_m1", "u_m2", "u_k" };
    ColorArgs a = { { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, { 0, (float)amt } };
    double t = 1.0 - amt, M[9] = { 0 };
    switch (kind) {
    case 0: memcpy(M, (double[9]){ 0.2126 + 0.7874 * t, 0.7152 - 0.7152 * t, 0.0722 - 0.0722 * t,
                             0.2126 - 0.2126 * t, 0.7152 + 0.2848 * t, 0.0722 - 0.0722 * t,
                             0.2126 - 0.2126 * t, 0.7152 - 0.7152 * t, 0.0722 + 0.9278 * t }, sizeof M); break;
    case 1: memcpy(M, (double[9]){ 0.393 + 0.607 * t, 0.769 - 0.769 * t, 0.189 - 0.189 * t,
                             0.349 - 0.349 * t, 0.686 + 0.314 * t, 0.168 - 0.168 * t,
                             0.272 - 0.272 * t, 0.534 - 0.534 * t, 0.131 + 0.869 * t }, sizeof M); break;
    case 2: memcpy(M, (double[9]){ 0.213 + 0.787 * amt, 0.715 - 0.715 * amt, 0.072 - 0.072 * amt,
                             0.213 - 0.213 * amt, 0.715 + 0.285 * amt, 0.072 - 0.072 * amt,
                             0.213 - 0.213 * amt, 0.715 - 0.715 * amt, 0.072 + 0.928 * amt }, sizeof M); break;
    case 3: {
        double r = amt * M_PI / 180.0, co = cos(r), si = sin(r);
        memcpy(M, (double[9]){ 0.213 + co * 0.787 - si * 0.213, 0.715 - co * 0.715 - si * 0.715, 0.072 - co * 0.072 + si * 0.928,
                         0.213 - co * 0.213 + si * 0.143, 0.715 + co * 0.285 + si * 0.140, 0.072 - co * 0.072 - si * 0.283,
                         0.213 - co * 0.213 - si * 0.787, 0.715 - co * 0.715 + si * 0.715, 0.072 + co * 0.928 + si * 0.072 }, sizeof M);
        break; }
    case 4: case 5: case 6: case 7: a.k[0] = (float)(kind - 3); break;
    default: return -1;
    }
    if (kind <= 3) for (int i = 0; i < 9; i++) a.m[i] = (float)M[i];
    if (use_vk) return vk_color(target, kind, a.m, a.k, rc);
    Builtin *b = builtin(&b_color, FS_COLOR, names, 4);
    if (!b) return -1;
    return gpu_zone_pass(target, b->sh, rc, set_color, &a);
}
typedef struct { float rect[4], blur[3]; } BlurArgs;
static void set_blur(Shader sh, const void *p) {
    const BlurArgs *a = p;
    if (b_blur.loc[0] >= 0) SetShaderValue(sh, b_blur.loc[0], a->rect, SHADER_UNIFORM_VEC4);
    if (b_blur.loc[1] >= 0) SetShaderValue(sh, b_blur.loc[1], a->blur, SHADER_UNIFORM_VEC3);
}
int plat_gpu_blur(int target, double radius, const int rc[4]) {
    static const char *const names[] = { "u_rect", "u_blur" };
    int r = (int)floor(radius + 0.5);
    if (r < 1) return 0;
    if (r > 64) r = 64;
    if (use_vk) return vk_blur(target, r, rc);
    Builtin *b = builtin(&b_blur, FS_BLUR, names, 2);
    if (!b) return -1;
    BlurArgs a = { { (float)rc[0], (float)rc[1], (float)rc[2], (float)rc[3] }, { 1, 0, (float)r } };
    for (int pass = 0; pass < 2; pass++) {
        a.blur[0] = 1; a.blur[1] = 0;
        if (gpu_zone_pass(target, b->sh, rc, set_blur, &a)) return -1;
        a.blur[0] = 0; a.blur[1] = 1;
        if (gpu_zone_pass(target, b->sh, rc, set_blur, &a)) return -1;
    }
    return 0;
}
int plat_gpu_fill_grad(int target, const double *pts, int n, const double *grad, double alpha, int mode) {
    static const char *const names[] = { "u_g", "u_col", "u_pos", "u_n" };
    if (use_vk) return vk_fill_grad(target, pts, n, grad, alpha, mode);
    if (n < 3 || !grad || !gpu_bind(target)) return -1;
    Builtin *b = builtin(&b_grad, FS_GRAD, names, 4);
    if (!b) return -1;
    int ns = (int)grad[4];
    if (ns < 1) return 0;
    if (ns > 8) ns = 8;
    float g4[4] = { (float)grad[0], (float)grad[1], (float)grad[2], (float)grad[3] }, col[32] = { 0 }, pos[8] = { 0 };
    float nn[2] = { (float)ns, (float)(alpha > 1 ? 1 : alpha) };
    for (int i = 0; i < ns; i++) {
        const double *st = grad + 5 + 5 * i;
        pos[i] = (float)st[0];
        col[4 * i] = (float)(st[1] / 255.0); col[4 * i + 1] = (float)(st[2] / 255.0); col[4 * i + 2] = (float)(st[3] / 255.0); col[4 * i + 3] = (float)st[4];
    }
    gpu_flush();
    gpu_blend(mode);
    BeginShaderMode(b->sh);
    if (b->loc[0] >= 0) SetShaderValue(b->sh, b->loc[0], g4, SHADER_UNIFORM_VEC4);
    if (b->loc[1] >= 0) SetShaderValueV(b->sh, b->loc[1], col, SHADER_UNIFORM_VEC4, 8);
    if (b->loc[2] >= 0) SetShaderValueV(b->sh, b->loc[2], pos, SHADER_UNIFORM_FLOAT, 8);
    if (b->loc[3] >= 0) SetShaderValue(b->sh, b->loc[3], nn, SHADER_UNIFORM_VEC2);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(255, 255, 255, 255);
    for (int i = 1; i + 1 < n; i++) {
        const double *v[3] = { pts, pts + 2 * i + 2, pts + 2 * i };
        for (int k = 0; k < 3; k++) { rlTexCoord2f((float)v[k][0], (float)v[k][1]); rlVertex2f((float)v[k][0], (float)v[k][1]); }
    }
    rlEnd();
    EndShaderMode();
    return 0;
}
int plat_gpu_read(int target, int x, int y, int w, int h, uint8_t *out) {
    if (use_vk) return vk_read(target, x, y, w, h, out);
    if (target < 0 || target >= GPU_TARGETS || !gtargets[target].used || !out) return -1;
    gpu_unbind();
    GpuTarget *g = &gtargets[target];
    Image im = LoadImageFromTexture(g->rt.texture);
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const uint8_t *src = im.data;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) {
        int sx = x + i, sy = y + j;
        uint8_t *d = out + ((size_t)j * w + i) * 4;
        if (!src || sx < 0 || sy < 0 || sx >= g->w || sy >= g->h) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
        const uint8_t *p = src + ((size_t)(g->h - 1 - sy) * g->w + sx) * 4;
        int a = p[3];
        if (a == 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
        for (int c = 0; c < 3; c++) { int v = a == 255 ? p[c] : (p[c] * 255 + a / 2) / a; d[c] = (uint8_t)(v > 255 ? 255 : v); }
        d[3] = (uint8_t)a;
    }
    UnloadImage(im);
    return 0;
}
static int pixel_perfect = 0;
void plat_set_pixel_perfect(int on) { pixel_perfect = on ? 1 : 0; }
void plat_gpu_present(int target, int *dx, int *dy, int *dw, int *dh) {
    if (use_vk) {
        if (shot_path[0]) {
            int sw = 0, sh = 0;
            vk_target_size(target, &sw, &sh);
            unsigned char *px = sw > 0 ? malloc((size_t)sw * sh * 4) : NULL;
            if (px && vk_read(target, 0, 0, sw, sh, px) == 0) {
                Image im = { px, sw, sh, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
                for (size_t i = 3; i < (size_t)sw * sh * 4; i += 4) px[i] = 255;
                save_shot_img(im);
            } else free(px);
            shot_path[0] = 0;
        }
        vk_present(target, GetRenderWidth(), GetRenderHeight(), pixel_perfect, dx, dy, dw, dh);
        PollInputEvents();
        return;
    }
    if (target < 0 || target >= GPU_TARGETS || !gtargets[target].used) return;
    gpu_unbind();
    GpuTarget *g = &gtargets[target];
    int w = g->w, h = g->h, ww = GetRenderWidth(), wh = GetRenderHeight();
    double sc = fmin((double)ww / w, (double)wh / h);
    if (pixel_perfect && sc >= 1) sc = floor(sc);
    int W = (int)floor(w * sc), H = (int)floor(h * sc);
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    *dw = W; *dh = H; *dx = (ww - W) / 2; *dy = (wh - H) / 2;
    BeginDrawing();
    ClearBackground(BLACK);
    rlSetBlendFactors(RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD);
    rlSetBlendMode(RL_BLEND_CUSTOM);
    DrawTexturePro(g->rt.texture, (Rectangle){ 0.0f, 0.0f, (float)w, (float)-h },
                   (Rectangle){ (float)*dx, (float)*dy, (float)W, (float)H }, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
    if (shot_path[0]) {
        rlDrawRenderBatchActive();
        save_shot();
    }
    EndDrawing();
    gblend = -1;
}

void plat_request_shot(const char *png_path) {
    snprintf(shot_path, sizeof shot_path, "%s", png_path ? png_path : "");
}

void plat_set_fullscreen(int on) {
    int now = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? 1 : 0;
    if (on != now) ToggleBorderlessWindowed();
}

void plat_set_title(const char *s) {
    if (s) SetWindowTitle(s);
}

#ifdef S2D_LITE
int TextFindIndex(const char *text, const char *find) {
    const char *p = text && find ? strstr(text, find) : NULL;
    return p ? (int)(p - text) : -1;
}
#endif
