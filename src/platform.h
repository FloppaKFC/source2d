/* Source 2D engine - capa de plataforma (ventana, entrada, presentacion, audio).
 * Implementacion: platform_raylib.c (raylib 5.5, 2D, OpenGL 2.1).
 * El resto del jugador (QuickJS, text.c, runtime) no sabe cual se usa. */
#ifndef S2D_PLATFORM_H
#define S2D_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

/* Evento de entrada hacia el runtime: mismo contrato que __dispatch(type, key, rep, x, y, btn).
   type: "keydown" | "keyup" | "mousemove" | "mousedown" | "mouseup" | "wheel".
   btn: 0 izquierdo, 1 medio, 2 derecho (en "wheel": -1 arriba, 1 abajo). */
typedef void (*PlatDispatchFn)(const char *type, const char *key, int rep, int x, int y, int btn);

int      plat_init(int w, int h, const char *title, int uncapped);  /* 0 si todo bien */
/* --diagnostico: abre la ventana, prueba la GPU y escribe ~/source2d-diagnostico.txt; resumen para el usuario */
int      plat_diagnose(const char *programa, char *resumen, size_t n);
void     plat_shutdown(void);
uint32_t plat_ticks(void);     /* milisegundos desde el inicio */
double   plat_time_ms(void);   /* reloj de alta resolucion en ms (para medir) */
void     plat_wait_frame(uint32_t frame_start, int uncapped);  /* limita a ~60 fps */
int      plat_poll(PlatDispatchFn fn);              /* 0 si se pidio cerrar la ventana */
void     plat_fit_window(int w, int h);             /* primer fotograma: ajusta ventana (no pantalla completa) */
void     plat_set_fullscreen(int on);
void     plat_set_pixel_perfect(int on);
void     plat_request_vulkan(int on);                  /* antes de plat_init: dibujar con Vulkan si se puede */
const char *plat_renderer(void);                      /* "Vulkan" u "OpenGL" */
const char *plat_renderer_info(void);                 /* GPU de Vulkan o por que no arranco */               /* escala entera al presentar */
void     plat_set_title(const char *s);
void     plat_request_shot(const char *png_path);    /* pruebas: guarda el siguiente fotograma presentado */
void     plat_tone(double freq, double dur, int square);
/* efecto con barrido: onda 0 seno, 1 cuadrada, 2 triangular, 3 sierra; vol 0-1 */
void     plat_sfx(double f0, double f1, double dur, int wave, double vol);

/* Musica en streaming (WAV, OGG, MP3, FLAC). Las pistas se cargan por id y se reproducen por id. */
int      plat_music_load(const char *id, const unsigned char *data, size_t len, const char *ext);  /* 0 ok */
void     plat_music_play(const char *id, int loop);
void     plat_music_stop(void);
void     plat_music_volume(float v);                /* 0-1, tambien para las siguientes */
void     plat_music_seek(double sec);
int      plat_music_playing(void);
void     plat_audio_update(void);                   /* llamar cada fotograma */
/* sonidos de archivo (WAV, OGG...) que pueden sonar varios a la vez, en canales 0-7 con su volumen */
int      plat_sound_load(const char *id, const unsigned char *data, size_t len, const char *ext);  /* 0 ok */
int      plat_sound_play(const char *id, double vol, double pitch, double pan, int ch);
int      plat_sound_playing(const char *id);
void     plat_channel_stop(int ch);                 /* -1 = todos */
void     plat_channel_pause(int ch, int pause);
void     plat_channel_volume(int ch, double vol);
/* mandos (0-3): los botones llegan como teclas "pad_a"...; aqui los ejes y la vibracion */
double   plat_pad_axis(int pad, int axis);
int      plat_pad_connected(int pad);
void     plat_pad_vibrate(int pad, double left, double right, double dur);

char    *plat_dir_list(const char *path);            /* "nombre\n" por entrada (carpetas con /); malloc o NULL */
void     plat_shader_amount(float a);                /* u_amount del siguiente pase de shader (0-1) */
/* dibujo en la GPU: lienzos (render textures) y texturas por numero; alfa premultiplicado; mode 0 normal, 1 lighter, 2 copy */
int      plat_gpu_target(int w, int h);
void     plat_gpu_target_free(int id);
int      plat_gpu_tex(int id, const uint8_t *rgba, int w, int h);     /* id -1 = nueva; devuelve el numero */
void     plat_gpu_tex_free(int id);
void     plat_gpu_draw(int target, int src_rt, int src, double sx, double sy, double sw, double sh,
                       double dx, double dy, double dw, double dh, const double m[6], double alpha, int mode);
void     plat_gpu_fill(int target, const double *pts, int n, double r, double g, double b, double a, int mode);
void     plat_gpu_rect(int target, double x0, double y0, double x1, double y1, double r, double g, double b, double a, int mode);
/* shader GLSL ES 1.0 del proyecto (v_uv, u_tex, u_time, u_res, u_cam = camara x, y y zoom, u_amount) sobre el lienzo;
   0 si se aplico, -1 si no (queda sin shader) */
int      plat_gpu_shader(int target, const char *glsl, double t, const float cam[3]);
/* filtros CSS sobre la zona rc = x0, y0, x1, y1: kind 0 grayscale, 1 sepia, 2 saturate, 3 hue-rotate, 4 invert,
   5 brightness, 6 contrast, 7 opacity; y desenfoque de radio r */
int      plat_gpu_color(int target, int kind, double amt, const int rc[4]);
int      plat_gpu_blur(int target, double radius, const int rc[4]);
/* poligono convexo con degradado radial: grad = cx, cy, r0, r1, paradas, (pos, r, g, b, a)... */
int      plat_gpu_fill_grad(int target, const double *pts, int n, const double *grad, double alpha, int mode);
int      plat_gpu_read(int target, int x, int y, int w, int h, uint8_t *out);
void     plat_gpu_present(int target, int *dx, int *dy, int *dw, int *dh);   /* a la ventana; area del juego en dx..dh */
/* imagen comprimida en memoria -> RGBA (malloc; NULL si no se entiende) */
unsigned char *plat_image_decode(const unsigned char *data, size_t len, int *w, int *h);

#endif
