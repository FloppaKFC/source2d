#ifndef S2D_FALLBACK_H
#define S2D_FALLBACK_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef _WINDOWS_
#ifdef __cplusplus
extern "C"
#endif
__declspec(dllimport) int __stdcall MessageBoxA(void *wnd, const char *text, const char *caption, unsigned type);
#endif
#else
#include <unistd.h>
#ifdef __cplusplus
extern "C"
#endif
int setenv(const char *name, const char *value, int overwrite);
#endif

#define S2D_SIN_VENTANA \
    "No se pudo abrir la ventana de Source 2D.\n\n" \
    "Hace falta OpenGL 2.1 o mas nuevo. Instala o actualiza los controladores de video\n" \
    "(en Linux, los paquetes de Mesa: por ejemplo libgl1-mesa-dri en Ubuntu, Mint o Linux Lite)."

static void s2d_message(const char *msg) {
    fprintf(stderr, "%s\n", msg);
#ifndef _WIN32
    const char *cmds[] = {
        "zenity --error --title='Source 2D' --text=\"%s\" 2>/dev/null",
        "kdialog --title 'Source 2D' --error \"%s\" 2>/dev/null",
        "xmessage -center \"%s\" 2>/dev/null",
    };
    for (int i = 0; i < 3; i++) {
        char cmd[1024];
        snprintf(cmd, sizeof cmd, cmds[i], msg);
        if (system(cmd) == 0) break;
    }
#else
    MessageBoxA(0, msg, "Source 2D", 0x10);
#endif
}

static void s2d_window_failed(char **argv) {
#ifndef _WIN32
    if (!getenv("S2D_SOFTGL") && argv) {
        setenv("S2D_SOFTGL", "1", 1);
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        fprintf(stderr, "Source 2D: sin OpenGL por hardware; se prueba OpenGL por software...\n");
        execv("/proc/self/exe", argv);
    }
    s2d_message(S2D_SIN_VENTANA);
#else
    (void)argv;
    s2d_message(S2D_SIN_VENTANA);
#endif
}
#endif
