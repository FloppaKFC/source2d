/* Source 2D - lanzador de la exportacion ligera (y del editor comprimido de la Release).
 *
 * Archivo exportado:
 *   [este lanzador][jugador comprimido (LZMA + filtro x86)][proyecto comprimido (LZMA o deflate)][datos tal cual][cola de 56 bytes]
 *   cola = uint64 LE: jugador comprimido | jugador | proyecto comprimido | proyecto entero (con los datos) | props |
 *          datos tal cual, y "S2DMINI3". props: bits 0..7 los del LZMA del jugador, bit 8 = filtro x86; bits 16..23 los
 *          del LZMA del proyecto y bit 24 = el proyecto va en LZMA (si no, en deflate). Los "datos tal cual" son los que
 *          ya venian comprimidos (OGG, PNG...): van detras del proyecto descomprimido (src/editor/exporter.cpp)
 *
 * Al abrirse descomprime el jugador y el proyecto, les pone detras la cola normal del jugador
 * ([proyecto][uint64 len]["S2DPAYLD"]) y lo ejecuta: en Linux desde memoria (memfd), en Windows desde un archivo
 * temporal. El motor puede ser el ligero o el completo (src/editor/exporter.cpp elige).
 * En Windows no usa la biblioteca de C (solo kernel32 y user32): asi el lanzador ocupa unos pocos KB. */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define LZ_NO_MALLOC
#include <windows.h>
/* sin CRT: lo que el compilador y sinfl esperan de string.h */
void *memset(void *d, int c, size_t n) { volatile unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) {
    volatile unsigned char *p = d; const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    volatile unsigned char *p = d; const unsigned char *q = s;
    if (p < q) while (n--) *p++ = *q++;
    else while (n--) p[n] = q[n];
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
    return 0;
}
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
extern char **environ;
#endif
#define SINFL_IMPLEMENTATION
#include "sinfl.h"
#include "lzdec.h"

#define MINI_MAGIC "S2DMINI3"
#define TAIL 56

static uint64_t rd64(const unsigned char *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static void wr64(unsigned char *p, uint64_t v) { for (int i = 0; i < 8; i++) { p[i] = (unsigned char)v; v >>= 8; } }

/* jugador + proyecto + cola del jugador en 'img' (reservado por 'alloc'); devuelve el tamano o 0 y el error en *err */
static size_t unpack(const unsigned char *all, size_t n, unsigned char *(*alloc)(size_t), unsigned char **img, const char **err) {
    *err = "el juego esta incompleto o danado";
    if (n < TAIL || memcmp(all + n - 8, MINI_MAGIC, 8) != 0) return 0;
    const unsigned char *t = all + n - TAIL;
    uint64_t pc = rd64(t), plen = rd64(t + 8), cl = rd64(t + 16), rl = rd64(t + 24), props = rd64(t + 32), raw = rd64(t + 40);
    if (pc + cl + raw + TAIL > n || plen > (256u << 20) || rl > (1024u << 20) || raw > rl) return 0;
    const unsigned char *pz = t - raw - cl - pc, *proj = t - raw - cl;
    size_t total = (size_t)plen + (size_t)rl + 16;
    unsigned char *b = alloc(total);
    *err = "sin memoria";
    if (!b) return 0;
    *err = "no se pudo descomprimir el motor";
    if (lz_decode(b, (size_t)plen, pz, (size_t)pc, (unsigned)(props & 255)) != 0) return 0;
    if (props & 256) lz_x86_decode(b, (size_t)plen);
    *err = "no se pudo descomprimir el juego";
    size_t zl = (size_t)(rl - raw);
    if (!zl) {}                                             /* sin proyecto: el editor comprimido (tools/pack_exe.py) */
    else if ((props >> 16) & 256) { if (lz_decode(b + plen, zl, proj, (size_t)cl, (unsigned)(props >> 16) & 255) != 0) return 0; }
    else if (sinflate(b + plen, (int)zl, proj, (int)cl) != (int)zl) return 0;
    memcpy(b + plen + zl, t - raw, (size_t)raw);
    wr64(b + plen + rl, rl);
    memcpy(b + plen + rl + 8, "S2DPAYLD", 8);
    *img = b;
    return total;
}

#ifdef _WIN32
static unsigned char *walloc(size_t n) { return VirtualAlloc(NULL, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); }
static void fail(const char *m) {
    MessageBoxA(NULL, m, "Source 2D", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}
static void wcat(WCHAR *d, const char *s) { while (*d) d++; while ((*d++ = (WCHAR)(unsigned char)*s++)) {} }

void __stdcall start(void) {
    WCHAR me[MAX_PATH * 2], tmp[MAX_PATH * 2 + 64];
    if (!GetModuleFileNameW(NULL, me, MAX_PATH * 2)) fail("no se encuentra el juego");
    HANDLE f = CreateFileW(me, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    if (f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &sz)) fail("no se pudo leer el juego");
    size_t n = (size_t)sz.QuadPart;
    unsigned char *all = walloc(n);
    DWORD got = 0;
    if (!all || !ReadFile(f, all, (DWORD)n, &got, NULL) || got != n) fail("no se pudo leer el juego");
    CloseHandle(f);
    /* la carpeta de verdad del ejecutable, para el programa que se abre desde la copia temporal */
    WCHAR dir[MAX_PATH * 2];
    int last = -1;
    for (int i = 0; me[i]; i++) { dir[i] = me[i]; dir[i + 1] = 0; if (me[i] == L'\\' || me[i] == L'/') last = i; }
    SetEnvironmentVariableW(L"S2D_EXE", me);
    if (last >= 0) { dir[last + 1] = 0; SetEnvironmentVariableW(L"S2D_EXE_DIR", dir); }
    unsigned char *img = NULL;
    const char *err;
    size_t total = unpack(all, n, walloc, &img, &err);
    if (!total) fail(err);
    VirtualFree(all, 0, MEM_RELEASE);
    /* el motor va a un archivo temporal (se reutiliza si ya esta igual): el nombre lleva un hash de todos los bytes,
       asi una version nueva del juego con el mismo tamano no abre la vieja */
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < total; i++) h = (h ^ img[i]) * 16777619u;
    char name[40] = "s2d_", *p = name + 4;
    for (int i = 7; i >= 0; i--) *p++ = "0123456789abcdef"[(h >> (i * 4)) & 15];
    *p++ = '_';
    for (int i = 7; i >= 0; i--) *p++ = "0123456789abcdef"[(total >> (i * 4)) & 15];
    *p++ = '.'; *p++ = 'e'; *p++ = 'x'; *p++ = 'e'; *p = 0;
    if (!GetTempPathW(MAX_PATH * 2, tmp)) fail("no se pudo preparar el juego");
    wcat(tmp, name);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(tmp, GetFileExInfoStandard, &fa) || fa.nFileSizeLow != (DWORD)total || fa.nFileSizeHigh) {
        f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD put = 0;
        int ok = f != INVALID_HANDLE_VALUE && WriteFile(f, img, (DWORD)total, &put, NULL) && put == total;
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        if (!ok) fail("no se pudo preparar el juego");
    }
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(tmp, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) fail("no se pudo abrir el juego");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    ExitProcess(code);
}
#else
static unsigned char *read_all(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = b ? (size_t)n : 0;
    return b;
}
static unsigned char *lalloc(size_t n) { return malloc(n); }
/* con doble clic no hay terminal: el error tambien sale en una ventanita (zenity, kdialog o xmessage) */
static int fail(const char *m) {
    fprintf(stderr, "Source 2D: %s\n", m);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "zenity --error --title='Source 2D' --text='No se pudo abrir: %s' 2>/dev/null || "
                              "kdialog --error 'No se pudo abrir: %s' 2>/dev/null || xmessage -center 'Source 2D: %s' 2>/dev/null", m, m, m);
    if (system(cmd)) {}
    return 1;
}

int main(int argc, char **argv) {
    (void)argc;
    size_t n = 0;
    unsigned char *all = read_all("/proc/self/exe", &n), *img = NULL;
    const char *err = "no se pudo leer el juego";
    size_t total = all ? unpack(all, n, lalloc, &img, &err) : 0;
    if (!total) return fail(err);
    free(all);
    /* la carpeta de verdad del ejecutable, para el programa que se abre desde memoria */
    char me[4096];
    ssize_t ml = readlink("/proc/self/exe", me, sizeof me - 1);
    if (ml > 0) {
        me[ml] = 0;
        setenv("S2D_EXE", me, 1);
        char *sl = strrchr(me, '/');
        if (sl) { sl[1] = 0; setenv("S2D_EXE_DIR", me, 1); }
    }
    /* desde memoria; si el nucleo es muy viejo (sin memfd), desde un archivo temporal */
    int fd = (int)syscall(SYS_memfd_create, "source2d", 0);
    if (fd >= 0) {
        if (write(fd, img, total) == (ssize_t)total) {
            char p[64];
            snprintf(p, sizeof p, "/proc/self/fd/%d", fd);
            execve(p, argv, environ);
        }
        close(fd);
    }
    /* sin memfd (nucleo muy viejo): un archivo temporal en la primera carpeta donde se pueda ejecutar */
    const char *home = getenv("HOME");
    char cache[4096];
    snprintf(cache, sizeof cache, "%s/.cache", home ? home : "/tmp");
    const char *dirs[] = { getenv("XDG_RUNTIME_DIR"), "/dev/shm", cache, "/tmp" };
    for (int i = 0; i < 4; i++) {
        if (!dirs[i] || !*dirs[i]) continue;
        char path[4200];
        snprintf(path, sizeof path, "%s/source2d-%d", dirs[i], (int)getpid());
        FILE *f = fopen(path, "wb");
        if (!f) continue;
        int ok = fwrite(img, 1, total, f) == total;
        fclose(f);
        if (!ok) { remove(path); continue; }
        chmod(path, 0700);
        execve(path, argv, environ);
        remove(path);                          /* esa carpeta no deja ejecutar (noexec): la siguiente */
    }
    return fail("no hay ninguna carpeta temporal donde se pueda ejecutar");
}
#endif
