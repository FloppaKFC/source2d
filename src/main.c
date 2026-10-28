/* Source 2D engine - ejecutable nativo de Linux.
 *
 * Estructura del archivo exportado:
 *   [ELF de este programa][payload][uint64 LE len][magic "S2DPAYLD"]
 * El payload es JavaScript (runtime + proyecto + arranque), opcionalmente
 * comprimido con LZSS en formato "PFZ1" (el mismo que usa el editor).
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE        /* realpath */
#endif
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "steam.h"
#include "quickjs.h"
#include "text.h"
#include "gfx_js.h"
#ifdef PLAYER
#include "runtime_js.h"
#include "licencias_jugador.h"   /* --licencias: las de raylib, GLFW, QuickJS y DejaVu, que van dentro de cada juego */
#endif

#define TRAILER_MAGIC "S2DPAYLD"

static JSRuntime *rt;
static JSContext *ctx;
static int first_present = 1;
static int running = 1;
static int fullscreen = 0;
static uint32_t t_start;
static int show_fps = 0;
static int fixed_step = 0;
static int debug_mode = 0;
static int renderer_arg = 0;  /* --vulkan 1, --opengl 2; si no, lo que diga el proyecto ("renderer" en los ajustes) */
/* "renderer": "vulkan" en el JSON del proyecto (sin analizarlo entero: aun no hay QuickJS) */
static int project_wants_vulkan(const char *p, size_t n) {
    static const char key[] = "\"renderer\"";
    for (size_t i = 0; p && i + sizeof key < n; i++) {
        if (p[i] != '"' || memcmp(p + i, key, sizeof key - 1)) continue;
        size_t j = i + sizeof key - 1;
        while (j < n && (p[j] == ' ' || p[j] == ':' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r')) j++;
        return j + 8 <= n && !memcmp(p + j, "\"vulkan\"", 8);
    }
    return 0;
}   /* --debug: depurador del juego (F3), lo usa la vista previa del editor */
static int profile = 0;      /* --profile: el runtime imprime cuanto tarda cada parte del fotograma */   /* --fixed-step: cada fotograma avanza 1/60 s exactos (pruebas repetibles) */
static int uncapped = 0;
static long max_frames = -1;
static uint32_t fps_t0 = 0;
static int fps_frames = 0;

/* ---------------- LZSS "PFZ1" (mismo formato que el editor) ---------------- */
static unsigned char *lzss_unpack(const unsigned char *in, size_t inlen, size_t *outlen) {
    if (inlen < 12 || memcmp(in, "PFZ1", 4) != 0) return NULL;
    size_t rawlen = in[4] | (in[5] << 8) | (in[6] << 16) | ((size_t)in[7] << 24);
    size_t complen = in[8] | (in[9] << 8) | (in[10] << 16) | ((size_t)in[11] << 24);
    if (12 + complen > inlen) return NULL;
    unsigned char *out = malloc(rawlen + 1);
    if (!out) return NULL;
    const unsigned char *src = in + 12, *end = src + complen;
    size_t op = 0;
    while (op < rawlen && src < end) {
        unsigned char flag = *src++;
        for (int bit = 0; bit < 8 && op < rawlen && src < end; bit++) {
            if (flag & (1 << bit)) {
                out[op++] = *src++;
            } else {
                if (src + 2 > end) break;
                unsigned b0 = src[0], b1 = src[1];
                src += 2;
                size_t off = (b0 << 4) | (b1 >> 4);
                size_t len = (b1 & 0x0f) + 3;
                if (off == 0 || off > op) { free(out); return NULL; }
                for (size_t k = 0; k < len && op < rawlen; k++, op++) out[op] = out[op - off];
            }
        }
    }
    if (op != rawlen) { free(out); return NULL; }
    out[rawlen] = 0;
    *outlen = rawlen;
    return out;
}

/* ---------------- lectura del payload desde el propio ejecutable ---------------- */
/* ruta de este ejecutable: /proc/self/exe en Linux, GetModuleFileName en Windows */
#ifdef _WIN32
#include <windows.h>
static const char *self_path(void) {
    static char p[MAX_PATH * 4];
    DWORD n = GetModuleFileNameA(NULL, p, sizeof p);
    return n > 0 && n < sizeof p ? p : NULL;
}
#else
static const char *self_path(void) { return "/proc/self/exe"; }
#endif
#include "s2d_fallback.h"   /* despues de windows.h */

static unsigned char *read_payload(size_t *outlen) {
    const char *sp = self_path();
    FILE *f = sp ? fopen(sp, "rb") : NULL;
    if (!f) return NULL;
    unsigned char tr[16];
    if (fseek(f, -16L, SEEK_END) != 0 || fread(tr, 1, 16, f) != 16 || memcmp(tr + 8, TRAILER_MAGIC, 8) != 0) {
        fclose(f);
        return NULL;
    }
    uint64_t len = 0;
    for (int i = 7; i >= 0; i--) len = (len << 8) | tr[i];
    if (fseek(f, -(long)(16 + len), SEEK_END) != 0) { fclose(f); return NULL; }
    unsigned char *buf = malloc(len + 1);
    if (!buf || fread(buf, 1, len, f) != len) { free(buf); fclose(f); return NULL; }
    buf[len] = 0;
    fclose(f);
    *outlen = len;
    return buf;
}

#ifdef PLAYER
/* Jugador: abre un .source2d.json directamente (runtime embebido + proyecto + arranque) */
static char *read_text_file(const char *path, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = 0;
    fclose(f);
    *outlen = (size_t)sz;
    return buf;
}
/* Proyecto exportado con sus datos en binario ([json][0][datos], src/editor/exporter.cpp): cada marca
   "\u0001S2DB:<inicio>:<largo>:<prefijo>" vuelve a ser prefijo + base64 de esos bytes, como en el proyecto original.
   Devuelve el JSON nuevo (malloc) o el mismo si no hay datos aparte. */
static char *expand_blobs(char *json, size_t *jl) {
    char *z = memchr(json, 0, *jl);
    if (!z) return json;
    static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const char MARK[] = "\\u0001S2DB:";
    const unsigned char *blob = (const unsigned char *)z + 1;
    size_t tl = (size_t)(z - json), bl = *jl - tl - 1, cap = tl + bl / 3 * 4 + bl / 2 + 4096, o = 0;
    char *out = malloc(cap);
    if (!out) return json;
    for (size_t i = 0; i < tl;) {
        if (json[i] == '\\' && i + sizeof MARK - 1 <= tl && !memcmp(json + i, MARK, sizeof MARK - 1)) {
            char *p = json + i + sizeof MARK - 1, *e;
            unsigned long long off = strtoull(p, &e, 10), len = *e == ':' ? strtoull(e + 1, &e, 10) : 0;
            if (*e == ':' && off + len <= bl) {
                char *q = memchr(e + 1, '"', tl - (size_t)(e + 1 - json));
                size_t pl = q ? (size_t)(q - e - 1) : 0, need = o + pl + (len + 2) / 3 * 4 + (tl - i) + 16;
                if (need > cap) { cap = need + cap / 2; char *nb = realloc(out, cap); if (!nb) { free(out); return json; } out = nb; }
                memcpy(out + o, e + 1, pl); o += pl;
                const unsigned char *d = blob + off;
                for (size_t k = 0; k < len; k += 3) {
                    unsigned v = (unsigned)d[k] << 16 | (k + 1 < len ? (unsigned)d[k + 1] << 8 : 0) | (k + 2 < len ? d[k + 2] : 0);
                    out[o++] = B64[v >> 18 & 63]; out[o++] = B64[v >> 12 & 63];
                    out[o++] = k + 1 < len ? B64[v >> 6 & 63] : '='; out[o++] = k + 2 < len ? B64[v & 63] : '=';
                }
                i = (size_t)((q ? q : e) - json);
                continue;
            }
        }
        if (o + 1 > cap) { cap *= 2; char *nb = realloc(out, cap); if (!nb) { free(out); return json; } out = nb; }
        out[o++] = json[i++];
    }
    out[o] = 0;
    free(json);
    *jl = o;
    return out;
}
/* titulo de la ventana: el nombre del proyecto (o del archivo) */
static void project_title(const char *json, const char *path, char *title, size_t n) {
    const char *nm = strstr(json, "\"name\":\"");
    size_t k = 0;
    if (nm) { nm += 8; while (nm[k] && nm[k] != '"' && k < n - 1) { title[k] = nm[k]; k++; } }
    else if (path) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        for (const char *q = base; *q && *q != '.' && k < n - 1; q++) title[k++] = *q;
    }
    title[k] = 0;
    if (!k) snprintf(title, n, "Source 2D");
    for (char *q = title; *q; q++) if (*q == '"' || *q == '\\') *q = ' ';
}
#endif

/* ---------------- helpers JS <-> C ---------------- */
static void report_exception(void) {
    JSValue e = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, e);
    fprintf(stderr, "Source 2D engine: %s\n", s ? s : "error desconocido");
    if (s) JS_FreeCString(ctx, s);
    JSValue st = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;   /* donde paso */
    const char *ss = JS_IsString(st) ? JS_ToCString(ctx, st) : NULL;
    if (ss) { fprintf(stderr, "%s\n", ss); JS_FreeCString(ctx, ss); }
    JS_FreeValue(ctx, st);
    JS_FreeValue(ctx, e);
}

static double *arr_dbl(JSValueConst a, int *n) {
    int32_t len = 0;
    JSValue l = JS_GetPropertyStr(ctx, a, "length");
    JS_ToInt32(ctx, &len, l);
    JS_FreeValue(ctx, l);
    if (len < 0) len = 0;
    double *p = malloc(sizeof(double) * (size_t)(len ? len : 1));
    if (!p) { *n = 0; return NULL; }
    for (int i = 0; i < len; i++) {
        JSValue v = JS_GetPropertyUint32(ctx, a, (uint32_t)i);
        double d = 0;
        JS_ToFloat64(ctx, &d, v);
        JS_FreeValue(ctx, v);
        p[i] = d;
    }
    *n = len;
    return p;
}

static double argd(JSValueConst v) { double d = 0; JS_ToFloat64(ctx, &d, v); return d; }
static int32_t argi(JSValueConst v) { int32_t i = 0; JS_ToInt32(ctx, &i, v); return i; }

static uint8_t *argbuf(JSValueConst v, size_t *len) {
    size_t l = 0;
    uint8_t *p = JS_GetArrayBuffer(ctx, &l, v);
    if (!p) { JS_FreeValue(ctx, JS_GetException(ctx)); l = 0; }
    if (len) *len = l;
    return p;
}

/* ---------------- funciones nativas expuestas como __native ---------------- */
static JSValue n_print(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (s) { printf("%s\n", s); fflush(stdout); JS_FreeCString(c, s); }
    return JS_UNDEFINED;
}
static JSValue n_now(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewFloat64(c, (double)(plat_ticks() - t_start));
}
static JSValue n_textRender(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (!s) return JS_NULL;
    const uint8_t *buf = NULL; int w = 0, h = 0, asc = 0;
    int ok = tx_text_get(s, argd(argv[1]), JS_ToBool(c, argv[2]), argc > 6 ? argi(argv[6]) : 0, argi(argv[3]), argi(argv[4]),
                         argi(argv[5]), &buf, &w, &h, &asc);
    JS_FreeCString(c, s);
    if (!ok) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "buf", JS_NewArrayBufferCopy(c, buf, (size_t)w * h * 4));
    JS_SetPropertyStr(c, o, "w", JS_NewInt32(c, w));
    JS_SetPropertyStr(c, o, "h", JS_NewInt32(c, h));
    JS_SetPropertyStr(c, o, "asc", JS_NewInt32(c, asc));
    return o;
}
static JSValue n_textWidth(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    double w = s ? tx_text_width(s, argd(argv[1]), JS_ToBool(c, argv[2]), argc > 3 ? argi(argv[3]) : 0) : 0;
    if (s) JS_FreeCString(c, s);
    return JS_NewFloat64(c, w);
}
static JSValue n_setFullscreen(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    fullscreen = JS_ToBool(c, argv[0]) ? 1 : 0;
    plat_set_fullscreen(fullscreen);
    return JS_UNDEFINED;
}
static JSValue n_isFullscreen(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewBool(c, fullscreen);
}
static JSValue n_tone(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    plat_tone(argd(argv[0]), argd(argv[1]), argi(argv[2]));
    (void)c;
    return JS_UNDEFINED;
}
/* base64 -> bytes (las pistas llegan como texto desde el proyecto) */
static unsigned char *b64_decode(const char *in, size_t *outlen) {
    size_t n = strlen(in), o = 0;
    unsigned char *out = malloc(n / 4 * 3 + 4);
    if (!out) return NULL;
    unsigned int acc = 0; int bits = -8;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;                 /* saltos, espacios y '=' */
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 0) { out[o++] = (unsigned char)((acc >> bits) & 0xFF); bits -= 8; }
    }
    *outlen = o;
    return out;
}
/* sfx(f0, f1, dur, onda, vol): efecto de sonido con barrido de frecuencia */
static JSValue n_sfx(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_sfx(argd(argv[0]), argd(argv[1]), argd(argv[2]), argi(argv[3]), argd(argv[4]));
    return JS_UNDEFINED;
}
static JSValue n_musicVolume(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_music_volume((float)argd(argv[0]));
    return JS_UNDEFINED;
}
static JSValue n_musicSeek(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_music_seek(argd(argv[0]));
    return JS_UNDEFINED;
}
static JSValue n_musicPlaying(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewBool(c, plat_music_playing());
}

/* localStorage del jugador: un archivo JSON en la carpeta de datos del usuario
   (Linux: $XDG_DATA_HOME o ~/.local/share/source2d/datos.json; Windows: %APPDATA%\Source2D\datos.json).
   S2D_STORE cambia la ruta (las pruebas usan un archivo temporal). */
#ifdef _WIN32
#include <direct.h>
#define s2d_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define s2d_mkdir(p) mkdir(p, 0755)
#endif
static const char *store_path(int create_dir) {
    static char path[2048];
    const char *env = getenv("S2D_STORE");
    if (env && *env) return env;
    char dir[1900];
#ifdef _WIN32
    const char *base = getenv("APPDATA");
    if (!base) return NULL;
    snprintf(dir, sizeof dir, "%s\\Source2D", base);
#else
    const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
    char share[1800];
    if (xdg && *xdg) snprintf(share, sizeof share, "%s", xdg);
    else if (home) snprintf(share, sizeof share, "%s/.local/share", home);
    else return NULL;
    if (create_dir) s2d_mkdir(share);
    snprintf(dir, sizeof dir, "%s/source2d", share);
#endif
    if (create_dir) s2d_mkdir(dir);
    snprintf(path, sizeof path, "%s%sdatos.json", dir,
#ifdef _WIN32
             "\\"
#else
             "/"
#endif
    );
    return path;
}
static JSValue n_storeRead(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    const char *p = store_path(0);
    FILE *f = p ? fopen(p, "rb") : NULL;
    if (!f) return JS_NewString(c, "");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = sz > 0 ? malloc((size_t)sz + 1) : NULL;
    size_t got = buf ? fread(buf, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (!buf) return JS_NewString(c, "");
    JSValue r = JS_NewStringLen(c, buf, got);
    free(buf);
    return r;
}
static JSValue n_storeWrite(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    size_t len = 0;
    const char *s = JS_ToCStringLen(c, &len, argv[0]);
    const char *p = store_path(1);
    int ok = 0;
    if (s && p) {
        char tmp[2100];
        snprintf(tmp, sizeof tmp, "%s.tmp", p);
        FILE *f = fopen(tmp, "wb");
        if (f) {
            ok = fwrite(s, 1, len, f) == len;
            ok = fclose(f) == 0 && ok;
            if (ok) { remove(p); ok = rename(tmp, p) == 0; }
        }
    }
    if (s) JS_FreeCString(c, s);
    return JS_NewBool(c, ok);
}

/* ---------------- archivos del usuario (niveles, imagenes, canciones...) ----------------
   Un juego (y su JavaScript) solo puede escribir, borrar o crear carpetas en su carpeta de datos y en Documentos. Leer
   y listar puede en cualquier sitio (otra carpeta, un pendrive, el disco D:) menos en carpetas o archivos ocultos
   (.ssh, .bashrc, .config, .mozilla...) y AppData, donde estan las claves y los perfiles del navegador. Asi un proyecto
   ajeno no puede leer secretos ni borrar o cambiar archivos fuera de sus carpetas. */
static void user_dirs(char home[2048], char data[2100], char docs[2100]);
/* ruta absoluta, con "/", sin "." ni ".." (y en Linux con los enlaces resueltos), para comparar */
static void norm_path(const char *p, char *out, size_t n) {
    char abs[8192], tmp[8192];
#ifdef _WIN32
    int isabs = (p[0] && p[1] == ':') || p[0] == '\\' || p[0] == '/';
#else
    int isabs = p[0] == '/';
#endif
    if (isabs) snprintf(abs, sizeof abs, "%s", p);
    else { char cwd[4096] = "."; if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "."); snprintf(abs, sizeof abs, "%s/%s", cwd, p); }
    for (char *q = abs; *q; q++) if (*q == '\\') *q = '/';
    /* componentes: "." fuera, ".." quita el anterior */
    char *parts[1024]; int np = 0;
    char *save = abs, *prefix = "";
#ifdef _WIN32
    char drive[3] = { 0 };
    if (abs[0] && abs[1] == ':') { drive[0] = (char)tolower((unsigned char)abs[0]); drive[1] = ':'; prefix = drive; save = abs + 2; }
#endif
    for (char *tok = strtok(save, "/"); tok && np < 1024; tok = strtok(NULL, "/")) {
        if (!strcmp(tok, ".") || !*tok) continue;
        if (!strcmp(tok, "..")) { if (np) np--; continue; }
        parts[np++] = tok;
    }
    size_t o = (size_t)snprintf(tmp, sizeof tmp, "%s", prefix);
    for (int i = 0; i < np && o < sizeof tmp; i++) o += (size_t)snprintf(tmp + o, sizeof tmp - o, "/%s", parts[i]);
    if (!np) snprintf(tmp + o, sizeof tmp - o, "/");
#ifdef _WIN32
    for (char *q = tmp; *q; q++) *q = (char)tolower((unsigned char)*q);
    snprintf(out, n, "%s", tmp);
#else
    /* la parte que ya existe, con los enlaces simbolicos resueltos (un enlace en Documentos no lleva a ~/.ssh) */
    char head[8192], res[PATH_MAX];
    snprintf(head, sizeof head, "%s", tmp);
    size_t hl = strlen(head);
    for (;;) {
        if (realpath(head, res)) { snprintf(out, n, "%s%s", res, tmp + hl); return; }
        char *sl = strrchr(head, '/');
        if (!sl || sl == head) break;
        *sl = 0; hl = (size_t)(sl - head);
    }
    snprintf(out, n, "%s", tmp);
#endif
}
static int under(const char *path, const char *root) {
    size_t l = strlen(root);
    while (l > 1 && root[l - 1] == '/') l--;
    return !strncmp(path, root, l) && (path[l] == 0 || path[l] == '/');
}
static int file_allowed(const char *p, int write) {
    if (!p || !*p) return 0;
    char home[2048] = "", data[2100] = "", docs[2100] = "", np[8192], nh[8192], nd[8192], nc[8192];
    user_dirs(home, data, docs);
    norm_path(p, np, sizeof np); norm_path(home, nh, sizeof nh); norm_path(data, nd, sizeof nd); norm_path(docs, nc, sizeof nc);
    if (under(np, nd) || under(np, nc)) return 1;
    (void)nh;
    if (!write) {
        int ok = 1;
        for (const char *q = np; *q && ok; q++)
            if (*q == '/' && (q[1] == '.' || !strncmp(q + 1, "appdata", 7))) ok = 0;   /* ocultas y AppData */
        if (ok) return 1;
    }
    static char last[512];
    if (strncmp(last, p, sizeof last - 1)) {
        if (write) fprintf(stderr, "Source 2D: el juego no puede escribir en %s (solo en su carpeta de datos y en Documentos)\n", p);
        else fprintf(stderr, "Source 2D: el juego no puede leer %s (carpetas ocultas y AppData estan protegidas)\n", p);
        snprintf(last, sizeof last, "%s", p);
    }
    return 0;
}
static const char B64C[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static unsigned char *read_whole(const char *path, size_t *len) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;
    *len = got;
    return buf;
}
/* fileRead(ruta) -> texto o null */
static JSValue n_fileRead(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    size_t len = 0;
    unsigned char *buf = file_allowed(p, 0) ? read_whole(p, &len) : NULL;
    if (p) JS_FreeCString(c, p);
    if (!buf) return JS_NULL;
    JSValue r = JS_NewStringLen(c, (const char *)buf, len);
    free(buf);
    return r;
}
/* fileReadB64(ruta) -> el archivo en base64 o null (canciones e imagenes) */
static JSValue n_fileReadB64(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    size_t n = 0;
    unsigned char *d = file_allowed(p, 0) ? read_whole(p, &n) : NULL;
    if (p) JS_FreeCString(c, p);
    if (!d) return JS_NULL;
    char *out = malloc((n + 2) / 3 * 4 + 1);
    size_t o = 0;
    for (size_t i = 0; out && i < n; i += 3) {
        unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out[o++] = B64C[v >> 18 & 63]; out[o++] = B64C[v >> 12 & 63];
        out[o++] = i + 1 < n ? B64C[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? B64C[v & 63] : '=';
    }
    free(d);
    if (!out) return JS_NULL;
    JSValue r = JS_NewStringLen(c, out, o);
    free(out);
    return r;
}
/* makeDir(ruta): crea la carpeta y las que falten por encima */
static void make_dirs(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *q = tmp + 1; *q; q++) {
        if (*q == '/' || *q == '\\') { char ch = *q; *q = 0; s2d_mkdir(tmp); *q = ch; }
    }
    s2d_mkdir(tmp);
}
static JSValue n_makeDir(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (p) { if (file_allowed(p, 1)) make_dirs(p); JS_FreeCString(c, p); }
    return JS_UNDEFINED;
}
static JSValue n_fileDelete(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    int ok = p && file_allowed(p, 1) && remove(p) == 0;
    if (p) JS_FreeCString(c, p);
    return JS_NewBool(c, ok);
}
/* fileWrite(ruta, texto) -> true si se escribio (crea las carpetas que falten) */
static JSValue n_fileWrite(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    size_t len = 0;
    const char *s = JS_ToCStringLen(c, &len, argv[1]);
    int ok = 0;
    if (p && s && file_allowed(p, 1)) {
        char dir[4096];
        snprintf(dir, sizeof dir, "%s", p);
        char *sl = strrchr(dir, '/'), *bs = strrchr(dir, '\\');
        if (bs > sl) sl = bs;
        if (sl && sl != dir) { *sl = 0; make_dirs(dir); }
        FILE *f = fopen(p, "wb");
        if (f) { ok = fwrite(s, 1, len, f) == len; ok = fclose(f) == 0 && ok; }
    }
    if (p) JS_FreeCString(c, p);
    if (s) JS_FreeCString(c, s);
    return JS_NewBool(c, ok);
}
/* dirList(ruta) -> nombres (las carpetas acaban en "/"), o null si no se puede abrir */
#ifdef _WIN32
static JSValue n_dirList(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_NULL;
    if (!file_allowed(p, 0)) { JS_FreeCString(c, p); return JS_NULL; }
    char pat[4096];
    snprintf(pat, sizeof pat, "%s\\*", p);
    JS_FreeCString(c, p);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return JS_NULL;
    JSValue arr = JS_NewArray(c);
    uint32_t k = 0;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char nm[600];
        snprintf(nm, sizeof nm, "%s%s", fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "/" : "");
        JS_SetPropertyUint32(c, arr, k++, JS_NewString(c, nm));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return arr;
}
#else
static JSValue n_dirList(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *p = JS_ToCString(c, argv[0]);
    if (!p) return JS_NULL;
    if (!file_allowed(p, 0)) { JS_FreeCString(c, p); return JS_NULL; }
    char *list = plat_dir_list(p);
    JS_FreeCString(c, p);
    if (!list) return JS_NULL;
    JSValue arr = JS_NewArray(c);
    uint32_t k = 0;
    for (char *q = list, *e; *q; q = e + 1) {
        e = strchr(q, '\n');
        if (!e) break;
        JS_SetPropertyUint32(c, arr, k++, JS_NewStringLen(c, q, (size_t)(e - q)));
    }
    free(list);
    return arr;
}
#endif
/* paths() -> [carpeta personal, carpeta de datos del jugador, Documentos] */
static void user_dirs(char home[2048], char data[2100], char docs[2100]) {
#ifdef _WIN32
    const char *h = getenv("USERPROFILE"), *ad = getenv("APPDATA");
    snprintf(home, 2048, "%s", h ? h : ".");
    snprintf(data, 2100, "%s\\Source2D", ad ? ad : home);
    snprintf(docs, 2100, "%s\\Documents", home);
#else
    const char *h = getenv("HOME"), *xdg = getenv("XDG_DATA_HOME");
    snprintf(home, 2048, "%s", h ? h : ".");
    if (xdg && *xdg) snprintf(data, 2100, "%s/source2d", xdg); else snprintf(data, 2100, "%s/.local/share/source2d", home);
    struct stat st;
    snprintf(docs, 2100, "%s/Documentos", home);
    if (stat(docs, &st) != 0) snprintf(docs, 2100, "%s/Documents", home);
    if (stat(docs, &st) != 0) snprintf(docs, 2100, "%s", home);
#endif
}
static JSValue n_paths(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    char home[2048] = "", data[2100] = "", docs[2100] = "";
    user_dirs(home, data, docs);
    JSValue vals[3] = { JS_NewString(c, home), JS_NewString(c, data), JS_NewString(c, docs) };
    return JS_NewArrayFrom(c, 3, vals);
}
/* imageDecode(base64) -> [ancho, alto, ArrayBuffer RGBA] o null */
static JSValue n_imageDecode(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *b64 = JS_ToCString(c, argv[0]);
    if (!b64) return JS_NULL;
    size_t len = 0;
    unsigned char *bytes = b64_decode(b64, &len);
    JS_FreeCString(c, b64);
    if (!bytes) return JS_NULL;
    int w = 0, h = 0;
    unsigned char *px = plat_image_decode(bytes, len, &w, &h);
    free(bytes);
    if (!px) return JS_NULL;
    JSValue vals[3] = { JS_NewInt32(c, w), JS_NewInt32(c, h), JS_NewArrayBufferCopy(c, px, (size_t)w * h * 4) };
    free(px);
    return JS_NewArrayFrom(c, 3, vals);
}
static JSValue n_shaderAmount(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_shader_amount((float)argd(argv[0]));
    return JS_UNDEFINED;
}

/* ---------------- dibujo en la GPU (ver plat_gpu_* en platform_raylib.c) ---------------- */
static JSValue n_gpuTarget(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    return JS_NewInt32(c, plat_gpu_target(argi(argv[0]), argi(argv[1])));
}
static JSValue n_gpuTargetFree(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_gpu_target_free(argi(argv[0]));
    return JS_UNDEFINED;
}
/* gpuTex(id, buf, w, h) -> id */
static JSValue n_gpuTex(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    size_t l = 0; uint8_t *buf = argbuf(argv[1], &l);
    int w = argi(argv[2]), h = argi(argv[3]);
    if (!buf || (size_t)w * h * 4 > l) return JS_NewInt32(c, -1);
    return JS_NewInt32(c, plat_gpu_tex(argi(argv[0]), buf, w, h));
}
static JSValue n_gpuTexFree(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_gpu_tex_free(argi(argv[0]));
    return JS_UNDEFINED;
}
/* gpuDraw(target, srcEsLienzo, src, sx, sy, sw, sh, dx, dy, dw, dh, a, b, c, d, e, f, alpha, mode) */
static JSValue n_gpuDraw(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    double m[6] = { argd(argv[11]), argd(argv[12]), argd(argv[13]), argd(argv[14]), argd(argv[15]), argd(argv[16]) };
    plat_gpu_draw(argi(argv[0]), argi(argv[1]), argi(argv[2]), argd(argv[3]), argd(argv[4]), argd(argv[5]), argd(argv[6]),
                  argd(argv[7]), argd(argv[8]), argd(argv[9]), argd(argv[10]), m, argd(argv[17]), argi(argv[18]));
    return JS_UNDEFINED;
}
/* gpuBatch(Float64Array.buffer, n): ordenes seguidas, para no cruzar de JS a C en cada dibujo.
   1 = dibujo (target, esLienzo, src, sx, sy, sw, sh, dx, dy, dw, dh, a, b, c, d, e, f, alpha, mode): 20 numeros
   2 = rectangulo (target, x0, y0, x1, y1, r, g, b, a, mode): 11 numeros */
static JSValue n_gpuBatch(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    size_t l = 0;
    double *q = (double *)argbuf(argv[0], &l);
    int n = argi(argv[1]);
    if (!q || (size_t)n * sizeof(double) > l) return JS_UNDEFINED;
    for (int i = 0; i < n;) {
        int op = (int)q[i];
        if (op == 1 && i + 20 <= n) {
            const double *a = q + i + 1;
            plat_gpu_draw((int)a[0], (int)a[1], (int)a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a + 11, a[17], (int)a[18]);
            i += 20;
        } else if (op == 2 && i + 11 <= n) {
            const double *a = q + i + 1;
            plat_gpu_rect((int)a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], (int)a[9]);
            i += 11;
        } else break;
    }
    return JS_UNDEFINED;
}
/* gpuFill(target, [x0, y0, x1, y1...], r, g, b, a, mode) */
static JSValue n_gpuFill(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    int np = 0;
    double *pts = arr_dbl(argv[1], &np);
    if (pts) plat_gpu_fill(argi(argv[0]), pts, np / 2, argd(argv[2]), argd(argv[3]), argd(argv[4]), argd(argv[5]), argi(argv[6]));
    free(pts);
    return JS_UNDEFINED;
}
/* gpuRect(target, x0, y0, x1, y1, r, g, b, a, mode) */
static JSValue n_gpuRect(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    plat_gpu_rect(argi(argv[0]), argd(argv[1]), argd(argv[2]), argd(argv[3]), argd(argv[4]), argd(argv[5]), argd(argv[6]), argd(argv[7]), argd(argv[8]), argi(argv[9]));
    return JS_UNDEFINED;
}
/* gpuShader(target, glsl, tiempo, camX, camY, zoom) -> true si se aplico */
static JSValue n_gpuShader(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *code = JS_ToCString(c, argv[1]);
    float cam[3] = { (float)argd(argv[3]), (float)argd(argv[4]), (float)argd(argv[5]) };
    int ok = code ? plat_gpu_shader(argi(argv[0]), code, argd(argv[2]), cam) : -1;
    if (code) JS_FreeCString(c, code);
    return JS_NewBool(c, ok == 0);
}
/* gpuColor(target, tipo, cantidad, x0, y0, x1, y1): filtro de color CSS en esa zona (tipos en platform.h) */
static JSValue n_gpuColor(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    int rc[4] = { argi(argv[3]), argi(argv[4]), argi(argv[5]), argi(argv[6]) };
    plat_gpu_color(argi(argv[0]), argi(argv[1]), argd(argv[2]), rc);
    return JS_UNDEFINED;
}
/* gpuBlur(target, radio, x0, y0, x1, y1) */
static JSValue n_gpuBlur(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    int rc[4] = { argi(argv[2]), argi(argv[3]), argi(argv[4]), argi(argv[5]) };
    plat_gpu_blur(argi(argv[0]), argd(argv[1]), rc);
    return JS_UNDEFINED;
}
/* gpuFillGrad(target, [x0, y0, ...], [cx, cy, r0, r1, paradas, (pos, r, g, b, a)...], alfa, modo) */
static JSValue n_gpuFillGrad(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)c;
    int np = 0, ng = 0;
    double *pts = arr_dbl(argv[1], &np), *grad = arr_dbl(argv[2], &ng);
    if (pts && grad && ng >= 5 && ng >= 5 + 5 * (int)grad[4]) plat_gpu_fill_grad(argi(argv[0]), pts, np / 2, grad, argd(argv[3]), argi(argv[4]));
    free(pts); free(grad);
    return JS_UNDEFINED;
}
/* gpuRead(target, x, y, w, h) -> ArrayBuffer RGBA */
static JSValue n_gpuRead(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    int w = argi(argv[3]), h = argi(argv[4]);
    if (w <= 0 || h <= 0) return JS_NewArrayBufferCopy(c, NULL, 0);
    uint8_t *buf = calloc((size_t)w * h, 4);
    if (!buf) return JS_NULL;
    plat_gpu_read(argi(argv[0]), argi(argv[1]), argi(argv[2]), w, h, buf);
    JSValue r = JS_NewArrayBufferCopy(c, buf, (size_t)w * h * 4);
    free(buf);
    return r;
}
/* gpuPresent(target) -> [dx, dy, dw, dh] */
static JSValue n_gpuPresent(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    int id = argi(argv[0]);
    if (first_present && !fullscreen) {
        plat_fit_window(argi(argv[1]), argi(argv[2]));   /* gpuPresent(lienzo, ancho, alto) */
        first_present = 0;
    }
    int dx = 0, dy = 0, dw = 0, dh = 0;
    plat_gpu_present(id, &dx, &dy, &dw, &dh);
    JSValue vals[4] = { JS_NewInt32(c, dx), JS_NewInt32(c, dy), JS_NewInt32(c, dw), JS_NewInt32(c, dh) };
    return JS_NewArrayFrom(c, 4, vals);
}

/* musicLoad(id, base64, ext) */
static JSValue n_musicLoad(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    const char *b64 = JS_ToCString(c, argv[1]);
    const char *ext = JS_ToCString(c, argv[2]);
    int ok = -1;
    if (id && b64 && ext) {
        size_t len = 0;
        unsigned char *bytes = b64_decode(b64, &len);
        if (bytes) { ok = plat_music_load(id, bytes, len, ext); free(bytes); }
    }
    if (id) JS_FreeCString(c, id);
    if (b64) JS_FreeCString(c, b64);
    if (ext) JS_FreeCString(c, ext);
    return JS_NewBool(c, ok == 0);
}
/* soundLoad(id, base64, ext) */
static JSValue n_soundLoad(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *id = JS_ToCString(c, argv[0]), *b64 = JS_ToCString(c, argv[1]), *ext = JS_ToCString(c, argv[2]);
    int ok = -1;
    if (id && b64 && ext) {
        size_t len = 0;
        unsigned char *bytes = b64_decode(b64, &len);
        if (bytes) { ok = plat_sound_load(id, bytes, len, ext); free(bytes); }
    }
    if (id) JS_FreeCString(c, id);
    if (b64) JS_FreeCString(c, b64);
    if (ext) JS_FreeCString(c, ext);
    return JS_NewBool(c, ok == 0);
}
static double argnum(JSContext *c, JSValueConst v, double d) { double x; return JS_IsUndefined(v) || JS_ToFloat64(c, &x, v) ? d : x; }
/* soundPlay(id, vol, pitch, pan, canal) */
static JSValue n_soundPlay(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    const char *id = JS_ToCString(c, argv[0]);
    int r = -1;
    if (id) {
        r = plat_sound_play(id, argc > 1 ? argnum(c, argv[1], 1) : 1, argc > 2 ? argnum(c, argv[2], 1) : 1,
                            argc > 3 ? argnum(c, argv[3], 0) : 0, argc > 4 ? (int)argnum(c, argv[4], 0) : 0);
        JS_FreeCString(c, id);
    }
    return JS_NewInt32(c, r);
}
static JSValue n_soundPlaying(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    int r = id ? plat_sound_playing(id) : 0;
    if (id) JS_FreeCString(c, id);
    return JS_NewBool(c, r);
}
/* channel(op, canal, valor): op 0 parar, 1 pausar, 2 seguir, 3 volumen */
static JSValue n_channel(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int op = (int)argnum(c, argv[0], 0), ch = argc > 1 ? (int)argnum(c, argv[1], -1) : -1;
    if (op == 0) plat_channel_stop(ch);
    else if (op == 1) plat_channel_pause(ch, 1);
    else if (op == 2) plat_channel_pause(ch, 0);
    else if (op == 3) plat_channel_volume(ch, argc > 2 ? argnum(c, argv[2], 1) : 1);
    return JS_UNDEFINED;
}
/* pad(op, mando, a, b, c): op 0 eje a, 1 conectado, 2 vibrar (izq a, der b, c segundos) */
static JSValue n_pad(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int op = (int)argnum(c, argv[0], 0), pad = argc > 1 ? (int)argnum(c, argv[1], 0) : 0;
    if (op == 0) return JS_NewFloat64(c, plat_pad_axis(pad, argc > 2 ? (int)argnum(c, argv[2], 0) : 0));
    if (op == 1) return JS_NewBool(c, plat_pad_connected(pad));
    plat_pad_vibrate(pad, argc > 2 ? argnum(c, argv[2], 0.5) : 0.5, argc > 3 ? argnum(c, argv[3], 0.5) : 0.5, argc > 4 ? argnum(c, argv[4], 0.2) : 0.2);
    return JS_UNDEFINED;
}
static JSValue n_setPixelPerfect(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    plat_set_pixel_perfect(JS_ToBool(c, argv[0]));
    return JS_UNDEFINED;
}
/* steam(op, a, b, c): ver steam.c. Se inicia la primera vez que el juego lo usa (la extension Steam). */
static JSValue n_steam(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t;
    int op = (int)argnum(c, argv[0], 0);
    if (!steam_ok() && op != 0) {
        const char *d = getenv("S2D_EXE_DIR");
        steam_init(d);
    }
    const char *s1 = argc > 1 && JS_IsString(argv[1]) ? JS_ToCString(c, argv[1]) : NULL;
    const char *s2 = argc > 2 && JS_IsString(argv[2]) ? JS_ToCString(c, argv[2]) : NULL;
    double n2 = argc > 2 ? argnum(c, argv[2], 0) : 0, n3 = argc > 3 ? argnum(c, argv[3], 0) : 0;
    JSValue r = JS_UNDEFINED;
    switch (op) {
    case 0: r = JS_NewBool(c, steam_init(getenv("S2D_EXE_DIR"))); break;
    case 1: case 2: case 3: r = JS_NewBool(c, steam_achievement(op == 1 ? 0 : op == 2 ? 1 : 2, s1, 0, 0)); break;
    case 4: r = JS_NewBool(c, steam_achievement(3, s1, (int)n2, (int)n3)); break;
    case 5: r = JS_NewFloat64(c, steam_stat(0, s1, 0)); break;
    case 6: r = JS_NewBool(c, steam_stat(1, s1, n2) != 0); break;
    case 7: r = JS_NewBool(c, s1 && s2 && steam_cloud(0, s1, s2, strlen(s2), NULL)); break;
    case 8: { char *out = NULL; int n = s1 ? steam_cloud(1, s1, NULL, 0, &out) : 0; r = n > 0 ? JS_NewStringLen(c, out, (size_t)n) : JS_NULL; free(out); break; }
    case 9: r = JS_NewBool(c, s1 && steam_cloud(2, s1, NULL, 0, NULL)); break;
    case 10: r = JS_NewBool(c, s1 && steam_cloud(3, s1, NULL, 0, NULL)); break;
    case 11: r = JS_NewBool(c, steam_cloud(4, NULL, NULL, 0, NULL)); break;
    case 12: { const char *tx = steam_text((int)argnum(c, argv[1], 0)); r = JS_NewString(c, tx ? tx : ""); break; }
    case 13: r = JS_NewBool(c, s1 && steam_overlay(0, s1, 0)); break;
    case 14: r = JS_NewBool(c, s1 && steam_overlay(1, s1, 0)); break;
    case 15: r = JS_NewBool(c, steam_overlay(2, NULL, (int)argnum(c, argv[1], 0))); break;
    case 16: r = JS_NewBool(c, steam_overlay(3, NULL, 0)); break;
    case 17: r = JS_NewBool(c, steam_presence(s1 ? s1 : "", s2)); break;
    case 18: r = JS_NewBool(c, steam_ok()); break;
    }
    if (s1) JS_FreeCString(c, s1);
    if (s2) JS_FreeCString(c, s2);
    return r;
}
/* musicPlay(id, loop) */
static JSValue n_musicPlay(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *id = JS_ToCString(c, argv[0]);
    if (id) { plat_music_play(id, JS_ToBool(c, argv[1])); JS_FreeCString(c, id); }
    return JS_UNDEFINED;
}
static JSValue n_musicStop(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv; (void)c;
    plat_music_stop();
    return JS_UNDEFINED;
}

/* fontLoad(base64) -> numero de la fuente para textRender/textWidth, o -1 */
static JSValue n_fontLoad(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *b64 = JS_ToCString(c, argv[0]);
    int slot = -1;
    if (b64) {
        size_t len = 0;
        unsigned char *bytes = b64_decode(b64, &len);
        if (bytes) { slot = tx_font_load(bytes, len); free(bytes); }
        JS_FreeCString(c, b64);
    }
    return JS_NewInt32(c, slot);
}

static JSValue n_setTitle(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    plat_set_title(s);
    if (s) JS_FreeCString(c, s);
    return JS_UNDEFINED;
}

static JSValue n_hrtime(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)t; (void)argc; (void)argv;
    return JS_NewFloat64(c, plat_time_ms());
}

/* quit(): cierra el juego al terminar este fotograma (accion "Salir del juego") */
static int quit_requested = 0;
static JSValue n_quit(JSContext *c, JSValueConst t, int argc, JSValueConst *argv) {
    (void)c; (void)t; (void)argc; (void)argv;
    quit_requested = 1;
    return JS_UNDEFINED;
}

static void install_natives(void) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue nat = JS_NewObject(ctx);
#define REG(name, fn, len) JS_SetPropertyStr(ctx, nat, name, JS_NewCFunction(ctx, fn, name, len))
    REG("print", n_print, 1);
    REG("now", n_now, 0);
    REG("textRender", n_textRender, 7);
    REG("textWidth", n_textWidth, 4);
    REG("fontLoad", n_fontLoad, 1);
    REG("setFullscreen", n_setFullscreen, 1);
    REG("isFullscreen", n_isFullscreen, 0);
    REG("tone", n_tone, 3);
    REG("setTitle", n_setTitle, 1);
    REG("musicLoad", n_musicLoad, 3);
    REG("musicPlay", n_musicPlay, 2);
    REG("musicStop", n_musicStop, 0);
    REG("musicVolume", n_musicVolume, 1);
    REG("musicSeek", n_musicSeek, 1);
    REG("musicPlaying", n_musicPlaying, 0);
    REG("sfx", n_sfx, 5);
    REG("soundLoad", n_soundLoad, 3);
    REG("soundPlay", n_soundPlay, 5);
    REG("soundPlaying", n_soundPlaying, 1);
    REG("channel", n_channel, 3);
    REG("pad", n_pad, 5);
    REG("setPixelPerfect", n_setPixelPerfect, 1);
    REG("steam", n_steam, 4);
    REG("storeRead", n_storeRead, 0);
    REG("storeWrite", n_storeWrite, 1);
    REG("fileRead", n_fileRead, 1);
    REG("fileReadB64", n_fileReadB64, 1);
    REG("fileWrite", n_fileWrite, 2);
    REG("makeDir", n_makeDir, 1);
    REG("fileDelete", n_fileDelete, 1);
    REG("dirList", n_dirList, 1);
    REG("paths", n_paths, 0);
    REG("imageDecode", n_imageDecode, 1);
    REG("shaderAmount", n_shaderAmount, 1);
    REG("gpuTarget", n_gpuTarget, 2);
    REG("gpuTargetFree", n_gpuTargetFree, 1);
    REG("gpuTex", n_gpuTex, 4);
    REG("gpuTexFree", n_gpuTexFree, 1);
    REG("gpuDraw", n_gpuDraw, 19);
    REG("gpuFill", n_gpuFill, 7);
    REG("gpuFillGrad", n_gpuFillGrad, 5);
    REG("gpuRect", n_gpuRect, 10);
    REG("gpuBatch", n_gpuBatch, 2);
    REG("gpuShader", n_gpuShader, 6);
    REG("gpuColor", n_gpuColor, 7);
    REG("gpuBlur", n_gpuBlur, 6);
    REG("gpuRead", n_gpuRead, 5);
    REG("gpuPresent", n_gpuPresent, 3);
    REG("quit", n_quit, 0);
    REG("hrtime", n_hrtime, 0);
    JS_SetPropertyStr(ctx, nat, "profile", JS_NewBool(ctx, profile));
    JS_SetPropertyStr(ctx, nat, "debug", JS_NewBool(ctx, debug_mode));
#undef REG
    JS_SetPropertyStr(ctx, g, "__native", nat);
    JS_FreeValue(ctx, g);
}

/* tiempo limite del JavaScript: un bucle sin fin en el codigo de un juego (while (true) {} en "Codigo JavaScript")
   ya no lo congela. Se corta a los 3 s de un fotograma (20 s al cargar el juego); tres fotogramas seguidos asi y el
   juego se cierra con un mensaje. */
static double js_deadline = 0;
static int js_interrupted = 0;
static int js_interrupt(JSContext *c, void *op) {
    (void)c; (void)op;
    if (js_deadline > 0 && plat_time_ms() > js_deadline) { js_interrupted = 1; return 1; }
    return 0;
}
#define S2D_BUCLE "El código JavaScript del juego lleva demasiado tiempo sin terminar y se detuvo.\n\n" \
                  "Seguramente hay un bucle sin fin (por ejemplo while (true) {} sin break) en una acción \"Código JavaScript\" o en una extensión."

static void call_global(const char *name, int argc, JSValue *argv) {
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, name);
    if (JS_IsFunction(ctx, fn)) {
        JSValue r = JS_Call(ctx, fn, g, argc, argv);
        if (JS_IsException(r)) report_exception();
        JS_FreeValue(ctx, r);
    }
    for (int i = 0; i < argc; i++) JS_FreeValue(ctx, argv[i]);
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, g);
}

static void dispatch(const char *type, const char *key, int rep, int x, int y, int btn) {
    JSValue argv[6] = {
        JS_NewStringLen(ctx, type, strlen(type)),
        JS_NewStringLen(ctx, key, strlen(key)),
        JS_NewBool(ctx, rep),
        JS_NewInt32(ctx, x), JS_NewInt32(ctx, y), JS_NewInt32(ctx, btn),
    };
    call_global("__dispatch", 6, argv);
}

int main(int argc, char **argv) {
    const char *project_path = NULL;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--fps") == 0) show_fps = 1;
        else if (strcmp(argv[ai], "--fixed-step") == 0) fixed_step = 1;
        else if (strcmp(argv[ai], "--profile") == 0) profile = 1;
        else if (strcmp(argv[ai], "--debug") == 0) debug_mode = 1;
        else if (strcmp(argv[ai], "--vulkan") == 0) renderer_arg = 1;
        else if (strcmp(argv[ai], "--opengl") == 0) renderer_arg = 2;
        else if (strcmp(argv[ai], "--uncapped") == 0) { show_fps = 1; uncapped = 1; }
        else if (strcmp(argv[ai], "--frames") == 0 && ai + 1 < argc) max_frames = atol(argv[++ai]);
        else if (strcmp(argv[ai], "--help") == 0 || strcmp(argv[ai], "-h") == 0) {
#ifdef PLAYER
            printf("Source 2D - jugador\n  ./source2d-player <proyecto.source2d.json>   abre y ejecuta un proyecto\n  --fps       muestra los fps en la terminal\n  --uncapped  sin límite de fps (medición)\n  --profile   tiempo de cada parte del fotograma (lógica, capas, shaders, efectos)\n  --licencias licencias de las bibliotecas de terceros\n  --diagnostico  informe de por que no abre (en ~/source2d-diagnostico.txt)\n");
#else
            printf("Source 2D engine - juego nativo\n  ./<juego>            ejecuta el juego (máx. 60 fps)\n  ./<juego> --fps      muestra los fps en la terminal\n  ./<juego> --uncapped sin límite de fps (medición)\n  ./<juego> --licencias licencias de las bibliotecas de terceros\n");
#endif
            return 0;
        }
        else if (strcmp(argv[ai], "--licencias") == 0 || strcmp(argv[ai], "--licenses") == 0) { fputs(licencias_jugador, stdout); return 0; }
        else if (strcmp(argv[ai], "--diagnostico") == 0) {
            char res[400], msg[700];
            int r = plat_diagnose("juego", res, sizeof res);
            snprintf(msg, sizeof msg, "%s\n\nEl informe completo quedó en source2d-diagnostico.txt, en tu carpeta personal.", res);
            s2d_message(msg);
            return r;
        }
        else if (argv[ai][0] != '-') project_path = argv[ai];
    }
    size_t plen = 0;
    unsigned char *payload = read_payload(&plen);
    char *proj = NULL;                      /* el proyecto (JSON): el runtime ya va en este jugador */
    size_t proj_len = 0;
    char title[128] = "Source 2D";
#ifdef PLAYER
    if (payload && plen && payload[0] == '{') {      /* juego exportado (normal o ligero) */
        proj = expand_blobs((char *)payload, &plen);
        proj_len = plen; payload = NULL;
        project_title(proj, NULL, title, sizeof title);
    } else if (!payload && project_path) {           /* source2d-player <proyecto.source2d.json> */
        proj = read_text_file(project_path, &proj_len);
        if (proj) project_title(proj, project_path, title, sizeof title);
    }
    if (!payload && !proj) {
        fprintf(stderr, "Uso: source2d-player <proyecto.source2d.json>  (no se pudo leer el proyecto)\n");
        return 1;
    }
#else
    (void)project_path;
    if (!payload) {
        fprintf(stderr, "Source 2D engine: este ejecutable no contiene un juego.\n");
        return 1;
    }
#endif
    size_t srclen = plen;
    unsigned char *src = payload ? lzss_unpack(payload, plen, &srclen) : NULL;
    if (src) { free(payload); payload = src; plen = srclen; }

    {
        const char *ev = getenv("S2D_RENDERER");
        int vk = renderer_arg ? renderer_arg == 1 : ev && *ev ? strcmp(ev, "vulkan") == 0 : project_wants_vulkan(proj, proj_len);
        plat_request_vulkan(vk);
    }
    if (plat_init(960, 540, "Source 2D engine", uncapped)) { s2d_window_failed(argv); return 1; }
    t_start = plat_ticks();

    rt = JS_NewRuntime();
#ifdef S2D_LITE
    /* jugador ligero: sin Date, Proxy ni performance de QuickJS (gfx.js da su propio performance.now) */
    ctx = JS_NewContextRaw(rt);
    if (ctx && (JS_AddIntrinsicBaseObjects(ctx) || JS_AddIntrinsicEval(ctx) || JS_AddIntrinsicRegExp(ctx) ||
                JS_AddIntrinsicJSON(ctx) || JS_AddIntrinsicMapSet(ctx) || JS_AddIntrinsicTypedArrays(ctx) ||
                JS_AddIntrinsicPromise(ctx) || JS_AddIntrinsicWeakRef(ctx) || JS_AddIntrinsicAToB(ctx)))
        ctx = NULL;
#else
    ctx = JS_NewContext(rt);
#endif
    install_natives();
    JS_SetInterruptHandler(rt, js_interrupt, NULL);
    js_deadline = plat_time_ms() + 20000;

    JSValue r = JS_Eval(ctx, (const char *)gfx_js, gfx_js_len, "<source2d>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) { report_exception(); return 1; }
    JS_FreeValue(ctx, r);

#ifdef PLAYER
    if (proj) {
        /* el proyecto se lee como datos (JSON), nunca como codigo: uno danado da un mensaje claro y uno preparado no
           puede ejecutar nada al abrirse */
        r = JS_Eval(ctx, (const char *)runtime_js, runtime_js_len, "<runtime>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) { report_exception(); return 1; }
        JS_FreeValue(ctx, r);
        JSValue pj = JS_ParseJSON(ctx, proj, proj_len, "<proyecto>");
        free(proj);
        if (JS_IsException(pj)) {
            JSValue ex = JS_GetException(ctx);
            const char *m = JS_ToCString(ctx, ex);
            char msg[600];
            snprintf(msg, sizeof msg, "No se pudo abrir el juego: el proyecto está dañado o no es de Source 2D.\n\n(%s)", m ? m : "JSON no válido");
            if (m) JS_FreeCString(ctx, m);
            JS_FreeValue(ctx, ex);
            s2d_message(msg);
            return 1;
        }
        JSValue scs = JS_IsObject(pj) ? JS_GetPropertyStr(ctx, pj, "scenes") : JS_UNDEFINED;
        int64_t nsc = 0;
        if (JS_IsArray(scs)) JS_GetLength(ctx, scs, &nsc);
        JS_FreeValue(ctx, scs);
        if (nsc <= 0) { s2d_message("Este proyecto no tiene escenas: no hay nada que jugar."); return 1; }
        JSValue g = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, g, "PROJECT", pj);
        JS_FreeValue(ctx, g);
        char boot[512];
        snprintf(boot, sizeof boot,
            "(function () {\n"
            "  __native.setTitle(\"%s\");\n"
            "  var g = S2D.createGame(PROJECT);\n"
            "  g.start();\n"
            "  if (PROJECT.settings && PROJECT.settings.autofullscreen) setTimeout(function () { g.toggleFullscreen(); }, 300);\n"
            "})();\n", title);
        r = JS_Eval(ctx, boot, strlen(boot), "<arranque>", JS_EVAL_TYPE_GLOBAL);
    } else
#endif
    {
        r = JS_Eval(ctx, (const char *)payload, plen, "<juego>", JS_EVAL_TYPE_GLOBAL);   /* juegos exportados antes (JS) */
        free(payload);
    }
    if (JS_IsException(r)) {
        report_exception();
        return 1;
    }
    JS_FreeValue(ctx, r);

    long frame_no = 0;
    while (running) {
        if (max_frames >= 0 && frame_no++ >= max_frames) break;
        if (max_frames >= 0 && frame_no == max_frames && getenv("S2D_SHOT")) plat_request_shot(getenv("S2D_SHOT"));
        uint32_t frame_start = plat_ticks();
        running = plat_poll(dispatch);
        if (!running) break;
        static long step_no = 0;
        JSValue now = JS_NewFloat64(ctx, fixed_step ? 1000.0 / 60.0 * (double)++step_no : (double)(plat_ticks() - t_start));
        static int stuck = 0;
        js_interrupted = 0;
        js_deadline = plat_time_ms() + 3000;
        call_global("__frame", 1, &now);
        JSContext *jc;
        while (JS_ExecutePendingJob(rt, &jc) > 0) {}
        js_deadline = 0;
        if (js_interrupted) {
            fprintf(stderr, "Source 2D: el JavaScript de este fotograma tardo mas de 3 s y se detuvo (un bucle sin fin?)\n");
            if (++stuck >= 3) { s2d_message(S2D_BUCLE); break; }
        } else stuck = 0;
        if (quit_requested) break;
        if (show_fps) {
            fps_frames++;
            if (frame_start - fps_t0 >= 2000) {
                if (fps_t0) fprintf(stderr, "Source 2D engine: %.1f fps\n", fps_frames * 1000.0 / (frame_start - fps_t0));
                fps_t0 = frame_start; fps_frames = 0;
            }
        }
        plat_audio_update();
        steam_frame();                         /* avisos de Steam (solo si el juego lo usa) */
        plat_wait_frame(frame_start, uncapped);
    }

    steam_shutdown();
    plat_shutdown();
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
