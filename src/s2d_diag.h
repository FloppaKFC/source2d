#ifndef S2D_DIAG_H
#define S2D_DIAG_H
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <gnu/libc-version.h>
#include <sys/utsname.h>
#endif

static FILE *s2d_diag_file = NULL;
static char s2d_diag_path[1024];

static void s2d_diag_trace(int level, const char *fmt, va_list ap) {
    static const char *L[] = { "", "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "" };
    FILE *f = s2d_diag_file ? s2d_diag_file : stderr;
    fprintf(f, "raylib %s: ", L[level >= 0 && level < 8 ? level : 0]);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
}
static void s2d_diag_line(const char *k, const char *v) {
    if (s2d_diag_file) fprintf(s2d_diag_file, "%-22s %s\n", k, v && *v ? v : "(no)");
}
static void s2d_diag_begin(const char *programa) {
    const char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    snprintf(s2d_diag_path, sizeof s2d_diag_path, "%s/source2d-diagnostico.txt", home ? home : ".");
    s2d_diag_file = fopen(s2d_diag_path, "w");
    if (!s2d_diag_file) { snprintf(s2d_diag_path, sizeof s2d_diag_path, "source2d-diagnostico.txt"); s2d_diag_file = fopen(s2d_diag_path, "w"); }
    if (!s2d_diag_file) s2d_diag_file = stderr;
    time_t t = time(NULL);
    fprintf(s2d_diag_file, "Diagnostico de Source 2D (%s) - %s\n", programa, ctime(&t));
#ifndef _WIN32
    struct utsname u;
    if (uname(&u) == 0) { char b[600]; snprintf(b, sizeof b, "%s %s %s", u.sysname, u.release, u.machine); s2d_diag_line("Sistema", b); }
    s2d_diag_line("glibc", gnu_get_libc_version());
    FILE *os = fopen("/etc/os-release", "r");
    char line[512];
    while (os && fgets(line, sizeof line, os)) if (!strncmp(line, "PRETTY_NAME=", 12)) { line[strcspn(line, "\n")] = 0; s2d_diag_line("Distribucion", line + 12); }
    if (os) fclose(os);
    FILE *cpu = fopen("/proc/cpuinfo", "r");
    int got = 0;
    while (cpu && fgets(line, sizeof line, cpu) && got < 2) {
        if (!strncmp(line, "model name", 10)) { line[strcspn(line, "\n")] = 0; s2d_diag_line("Procesador", strchr(line, ':') ? strchr(line, ':') + 2 : line); got++; }
        else if (!strncmp(line, "flags", 5)) {
            s2d_diag_line("SSE4.2 / AVX", strstr(line, " sse4_2") ? (strstr(line, " avx") ? "si / si" : "si / no") : "no / no"); got++;
        }
    }
    if (cpu) fclose(cpu);
    const char *vars[] = { "DISPLAY", "WAYLAND_DISPLAY", "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP", "LIBGL_ALWAYS_SOFTWARE", "S2D_SOFTGL" };
    for (size_t i = 0; i < sizeof vars / sizeof vars[0]; i++) s2d_diag_line(vars[i], getenv(vars[i]));
#else
    s2d_diag_line("Sistema", "Windows");
    s2d_diag_line("PROCESSOR_IDENTIFIER", getenv("PROCESSOR_IDENTIFIER"));
#endif
    fprintf(s2d_diag_file, "\n--- al abrir la ventana ---\n");
    fflush(s2d_diag_file);
    SetTraceLogLevel(LOG_INFO);
    SetTraceLogCallback(s2d_diag_trace);
}
static void s2d_diag_end(int ok, const char *detalle) {
    if (s2d_diag_file && s2d_diag_file != stderr) {
        fprintf(s2d_diag_file, "\nResultado: %s\n%s\n", ok ? "FUNCIONA" : "NO FUNCIONA", detalle ? detalle : "");
        fclose(s2d_diag_file);
    }
    s2d_diag_file = NULL;
    SetTraceLogCallback(NULL);
    SetTraceLogLevel(LOG_WARNING);
    printf("Diagnostico guardado en %s (%s)\n", s2d_diag_path, ok ? "funciona" : "no funciona");
}
#endif
