/* Source 2D engine - Steam (extension oficial "Steam"): logros, estadisticas, guardado en la nube, overlay, presencia,
 * nombre e idioma del jugador. Usa la API plana de Steamworks cargada al vuelo: el juego no la necesita para abrir.
 * Para publicar en Steam: junto al ejecutable van libsteam_api.so (Linux) o steam_api64.dll (Windows) del SDK de
 * Steamworks y, para probar fuera de Steam, steam_appid.txt con el numero de la app. Sin ellos, todo dice "no hay
 * Steam" y el juego funciona igual. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "steam.h"
#ifdef _WIN32
#include <windows.h>
#define LIBNAME "steam_api64.dll"
static void *open_lib(const char *p) { return (void *)LoadLibraryA(p); }
static void *sym(void *l, const char *n) { return (void *)GetProcAddress((HMODULE)l, n); }
#else
#include <dlfcn.h>
#define LIBNAME "libsteam_api.so"
static void *open_lib(const char *p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void *sym(void *l, const char *n) { return dlsym(l, n); }
#endif

typedef int (*FnBoolV)(void);
typedef void (*FnVoidV)(void);
typedef void *(*FnPtrV)(void);
static struct {
    void *lib; int tried, ok;
    void *stats, *storage, *friends, *user, *apps, *utils;
    FnVoidV run, shutdown;
} S;

/* accesor de una interfaz: SteamAPI_SteamUserStats_v013... (el numero cambia con cada SDK: se prueba del mas nuevo) */
static void *iface(const char *name) {
    char b[96];
    for (int v = 40; v >= 1; v--) {
        snprintf(b, sizeof b, "SteamAPI_%s_v%03d", name, v);
        FnPtrV f = (FnPtrV)sym(S.lib, b);
        if (f) return f();
    }
    return NULL;
}
#define M(type, name) ((type)sym(S.lib, name))
static void store_stats(void) { int (*f)(void *) = M(int (*)(void *), "SteamAPI_ISteamUserStats_StoreStats"); if (f && S.stats) f(S.stats); }
/* llama f(args) solo si la funcion existe en esta version de la biblioteca */
#define CALL(type, name, fallback, ...) (M(type, name) ? M(type, name)(__VA_ARGS__) : (fallback))

int steam_init(const char *exe_dir) {
    if (S.tried) return S.ok;
    S.tried = 1;
    char path[1024];
    if (exe_dir && *exe_dir) { snprintf(path, sizeof path, "%s%s", exe_dir, LIBNAME); S.lib = open_lib(path); }
    if (!S.lib) S.lib = open_lib(LIBNAME);
    if (!S.lib) return 0;
    int ok = 0;
    typedef int (*FnInitFlat)(char *);
    FnInitFlat initFlat = (FnInitFlat)sym(S.lib, "SteamAPI_InitFlat");
    if (initFlat) { char err[1024] = { 0 }; ok = initFlat(err) == 0; if (!ok) fprintf(stderr, "Source 2D: Steam no arranco: %s\n", err); }
    else {
        FnBoolV init = (FnBoolV)sym(S.lib, "SteamAPI_Init");
        if (!init) init = (FnBoolV)sym(S.lib, "SteamAPI_InitSafe");
        ok = init && init();
    }
    if (!ok) return 0;
    S.run = (FnVoidV)sym(S.lib, "SteamAPI_RunCallbacks");
    S.shutdown = (FnVoidV)sym(S.lib, "SteamAPI_Shutdown");
    S.stats = iface("SteamUserStats");
    S.storage = iface("SteamRemoteStorage");
    S.friends = iface("SteamFriends");
    S.user = iface("SteamUser");
    S.apps = iface("SteamApps");
    S.utils = iface("SteamUtils");
    S.ok = 1;
    fprintf(stderr, "Source 2D: Steam listo\n");
    return 1;
}
void steam_frame(void) { if (S.ok && S.run) S.run(); }
void steam_shutdown(void) { if (S.ok && S.shutdown) S.shutdown(); S.ok = 0; }
int steam_ok(void) { return S.ok; }

int steam_achievement(int op, const char *name, int a, int b) {
    if (!S.ok || !S.stats || !name) return 0;
    switch (op) {
    case 0: { int r = CALL(int (*)(void *, const char *), "SteamAPI_ISteamUserStats_SetAchievement", 0, S.stats, name); store_stats(); return r; }
    case 1: { int (*f)(void *, const char *, uint8_t *) = M(int (*)(void *, const char *, uint8_t *), "SteamAPI_ISteamUserStats_GetAchievement");
              uint8_t got = 0; return f && f(S.stats, name, &got) && got; }
    case 2: { int r = CALL(int (*)(void *, const char *), "SteamAPI_ISteamUserStats_ClearAchievement", 0, S.stats, name); store_stats(); return r; }
    case 3: { int (*f)(void *, const char *, uint32_t, uint32_t) = M(int (*)(void *, const char *, uint32_t, uint32_t), "SteamAPI_ISteamUserStats_IndicateAchievementProgress");
              return f ? f(S.stats, name, (uint32_t)a, (uint32_t)b) : 0; }
    }
    return 0;
}
double steam_stat(int op, const char *name, double v) {
    if (!S.ok || !S.stats || !name) return 0;
    if (op == 0) {             /* leer (primero como entero, si no como decimal) */
        int32_t i = 0; float f = 0;
        int (*gi)(void *, const char *, int32_t *) = M(int (*)(void *, const char *, int32_t *), "SteamAPI_ISteamUserStats_GetStatInt32");
        if (gi && gi(S.stats, name, &i)) return i;
        int (*gf)(void *, const char *, float *) = M(int (*)(void *, const char *, float *), "SteamAPI_ISteamUserStats_GetStatFloat");
        if (gf && gf(S.stats, name, &f)) return f;
        return 0;
    }
    int (*si)(void *, const char *, int32_t) = M(int (*)(void *, const char *, int32_t), "SteamAPI_ISteamUserStats_SetStatInt32");
    int ok = si && si(S.stats, name, (int32_t)v);
    if (!ok) {
        int (*sf)(void *, const char *, float) = M(int (*)(void *, const char *, float), "SteamAPI_ISteamUserStats_SetStatFloat");
        ok = sf && sf(S.stats, name, (float)v);
    }
    store_stats();
    return ok;
}
/* nube: 0 escribir, 1 leer (malloc), 2 existe, 3 borrar, 4 activada */
int steam_cloud(int op, const char *name, const char *data, size_t len, char **out) {
    if (!S.ok || !S.storage) return 0;
    switch (op) {
    case 0: return CALL(int (*)(void *, const char *, const void *, int32_t), "SteamAPI_ISteamRemoteStorage_FileWrite", 0, S.storage, name, data, (int32_t)len);
    case 1: {
        int32_t n = CALL(int32_t (*)(void *, const char *), "SteamAPI_ISteamRemoteStorage_GetFileSize", 0, S.storage, name);
        if (n <= 0) return 0;
        char *b = malloc((size_t)n + 1);
        if (!b) return 0;
        int32_t got = CALL(int32_t (*)(void *, const char *, void *, int32_t), "SteamAPI_ISteamRemoteStorage_FileRead", 0, S.storage, name, b, n);
        if (got <= 0) { free(b); return 0; }
        b[got] = 0; *out = b;
        return got;
    }
    case 2: return CALL(int (*)(void *, const char *), "SteamAPI_ISteamRemoteStorage_FileExists", 0, S.storage, name);
    case 3: return CALL(int (*)(void *, const char *), "SteamAPI_ISteamRemoteStorage_FileDelete", 0, S.storage, name);
    case 4: return CALL(int (*)(void *), "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForAccount", 0, S.storage) &&
                   CALL(int (*)(void *), "SteamAPI_ISteamRemoteStorage_IsCloudEnabledForApp", 0, S.storage);
    }
    return 0;
}
/* textos del jugador: 0 nombre, 1 idioma del juego, 2 id de Steam, 3 numero de la app */
const char *steam_text(int op) {
    static char b[64];
    if (!S.ok) return "";
    switch (op) {
    case 0: if (S.friends) { const char *(*f)(void *) = M(const char *(*)(void *), "SteamAPI_ISteamFriends_GetPersonaName"); return f ? f(S.friends) : ""; } break;
    case 1: if (S.apps) { const char *(*f)(void *) = M(const char *(*)(void *), "SteamAPI_ISteamApps_GetCurrentGameLanguage"); return f ? f(S.apps) : ""; } break;
    case 2: if (S.user) { uint64_t (*f)(void *) = M(uint64_t (*)(void *), "SteamAPI_ISteamUser_GetSteamID");
                          if (f) { snprintf(b, sizeof b, "%llu", (unsigned long long)f(S.user)); return b; } } break;
    case 3: if (S.utils) { uint32_t (*f)(void *) = M(uint32_t (*)(void *), "SteamAPI_ISteamUtils_GetAppID");
                           if (f) { snprintf(b, sizeof b, "%u", f(S.utils)); return b; } } break;
    }
    return "";
}
/* overlay: 0 abrir un dialogo ("achievements", "friends", "stats"...), 1 abrir una pagina web, 2 abrir la tienda
   (b = app), 3 esta activado */
int steam_overlay(int op, const char *a, int b) {
    if (!S.ok || !S.friends) return 0;
    switch (op) {
    case 0: { void (*f)(void *, const char *) = M(void (*)(void *, const char *), "SteamAPI_ISteamFriends_ActivateGameOverlay"); if (f) f(S.friends, a); return 1; }
    case 1: { void (*f)(void *, const char *, int) = M(void (*)(void *, const char *, int), "SteamAPI_ISteamFriends_ActivateGameOverlayToWebPage"); if (f) f(S.friends, a, 0); return 1; }
    case 2: { void (*f)(void *, uint32_t, int) = M(void (*)(void *, uint32_t, int), "SteamAPI_ISteamFriends_ActivateGameOverlayToStore"); if (f) f(S.friends, (uint32_t)b, 0); return 1; }
    case 3: if (S.utils) { int (*f)(void *) = M(int (*)(void *), "SteamAPI_ISteamUtils_IsOverlayEnabled"); return f && f(S.utils); } return 0;
    }
    return 0;
}
int steam_presence(const char *key, const char *value) {
    if (!S.ok || !S.friends) return 0;
    if (!key || !*key) { void (*c)(void *) = M(void (*)(void *), "SteamAPI_ISteamFriends_ClearRichPresence"); if (c) c(S.friends); return 1; }
    int (*f)(void *, const char *, const char *) = M(int (*)(void *, const char *, const char *), "SteamAPI_ISteamFriends_SetRichPresence");
    return f && f(S.friends, key, value ? value : "");
}
