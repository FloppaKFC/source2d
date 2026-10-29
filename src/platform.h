#ifndef S2D_PLATFORM_H
#define S2D_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

typedef void (*PlatDispatchFn)(const char *type, const char *key, int rep, int x, int y, int btn);

int      plat_init(int w, int h, const char *title, int uncapped);
int      plat_diagnose(const char *programa, char *resumen, size_t n);
void     plat_shutdown(void);
uint32_t plat_ticks(void);
double   plat_time_ms(void);
void     plat_wait_frame(uint32_t frame_start, int uncapped);
int      plat_poll(PlatDispatchFn fn);
void     plat_fit_window(int w, int h);
void     plat_set_fullscreen(int on);
void     plat_set_pixel_perfect(int on);
void     plat_request_vulkan(int on);
const char *plat_renderer(void);
const char *plat_renderer_info(void);
void     plat_set_title(const char *s);
void     plat_request_shot(const char *png_path);
void     plat_tone(double freq, double dur, int square);
void     plat_sfx(double f0, double f1, double dur, int wave, double vol);

int      plat_music_load(const char *id, const unsigned char *data, size_t len, const char *ext);
void     plat_music_play(const char *id, int loop);
void     plat_music_stop(void);
void     plat_music_volume(float v);
void     plat_music_seek(double sec);
int      plat_music_playing(void);
void     plat_audio_update(void);
int      plat_sound_load(const char *id, const unsigned char *data, size_t len, const char *ext);
int      plat_sound_play(const char *id, double vol, double pitch, double pan, int ch);
int      plat_sound_playing(const char *id);
void     plat_channel_stop(int ch);
void     plat_channel_pause(int ch, int pause);
void     plat_channel_volume(int ch, double vol);
double   plat_pad_axis(int pad, int axis);
int      plat_pad_connected(int pad);
void     plat_pad_vibrate(int pad, double left, double right, double dur);

char    *plat_dir_list(const char *path);
void     plat_shader_amount(float a);
int      plat_gpu_target(int w, int h);
void     plat_gpu_target_free(int id);
int      plat_gpu_tex(int id, const uint8_t *rgba, int w, int h);
void     plat_gpu_tex_free(int id);
void     plat_gpu_draw(int target, int src_rt, int src, double sx, double sy, double sw, double sh,
                       double dx, double dy, double dw, double dh, const double m[6], double alpha, int mode);
void     plat_gpu_fill(int target, const double *pts, int n, double r, double g, double b, double a, int mode);
void     plat_gpu_rect(int target, double x0, double y0, double x1, double y1, double r, double g, double b, double a, int mode);
int      plat_gpu_shader(int target, const char *glsl, double t, const float cam[3]);
int      plat_gpu_color(int target, int kind, double amt, const int rc[4]);
int      plat_gpu_blur(int target, double radius, const int rc[4]);
int      plat_gpu_fill_grad(int target, const double *pts, int n, const double *grad, double alpha, int mode);
int      plat_gpu_read(int target, int x, int y, int w, int h, uint8_t *out);
void     plat_gpu_present(int target, int *dx, int *dy, int *dw, int *dh);
unsigned char *plat_image_decode(const unsigned char *data, size_t len, int *w, int *h);

#endif
