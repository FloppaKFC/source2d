/* Source 2D engine - Steam (ver steam.c). Todo devuelve 0 / "" si no hay Steam. */
#ifndef S2D_STEAM_H
#define S2D_STEAM_H
#include <stddef.h>
int         steam_init(const char *exe_dir);
void        steam_frame(void);
void        steam_shutdown(void);
int         steam_ok(void);
int         steam_achievement(int op, const char *name, int a, int b);   /* 0 dar, 1 tiene, 2 quitar, 3 progreso a/b */
double      steam_stat(int op, const char *name, double v);              /* 0 leer, 1 poner */
int         steam_cloud(int op, const char *name, const char *data, size_t len, char **out);
const char *steam_text(int op);
int         steam_overlay(int op, const char *a, int b);
int         steam_presence(const char *key, const char *value);          /* key vacia: borrar todo */
#endif
