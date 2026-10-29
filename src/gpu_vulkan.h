#ifndef S2D_GPU_VULKAN_H
#define S2D_GPU_VULKAN_H
#include <stddef.h>
#include <stdint.h>
int  vk_init(int w, int h, int uncapped, char *info, size_t ninfo);
void vk_shutdown(void);
int  vk_active(void);
int  vk_target(int w, int h);
void vk_target_free(int id);
int  vk_tex(int id, const uint8_t *rgba, int w, int h);
void vk_tex_free(int id);
void vk_draw(int target, int src_rt, int src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh,
             const double m[6], double alpha, int mode);
void vk_fill(int target, const double *pts, int n, double r, double g, double b, double a, int mode);
int  vk_fill_grad(int target, const double *pts, int n, const double *grad, double alpha, int mode);
int  vk_color(int target, int kind, const float m9[9], const float k2[2], const int rc[4]);
int  vk_blur(int target, int r, const int rc[4]);
int  vk_shader(int target, const char *glsl, double t, const float cam[3], float amount);
void vk_target_size(int id, int *w, int *h);
int  vk_read(int target, int x, int y, int w, int h, uint8_t *out);
void vk_present(int target, int ww, int wh, int pixel_perfect, int *dx, int *dy, int *dw, int *dh);
#endif
