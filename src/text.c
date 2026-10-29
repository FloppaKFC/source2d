#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#ifdef S2D_LITE
#include "font_data_mini.h"
#else
#include "font_data.h"
#endif

static inline void put_px(uint8_t *p, int r, int g, int b, int a8) {
    if (a8 <= 0) return;
    if (a8 >= 255) { p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255; return; }
    int da = p[3];
    if (da == 255) {
        int inv = 255 - a8;
        p[0] = (uint8_t)((r * a8 + p[0] * inv + 127) / 255);
        p[1] = (uint8_t)((g * a8 + p[1] * inv + 127) / 255);
        p[2] = (uint8_t)((b * a8 + p[2] * inv + 127) / 255);
        return;
    }
    if (da == 0) { p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a8; return; }
    int inv = 255 - a8;
    int dk = (da * inv) / 255;
    int oa = a8 + dk;
    if (oa <= 0) { p[0] = p[1] = p[2] = p[3] = 0; return; }
    p[0] = (uint8_t)((r * a8 + p[0] * dk) / oa);
    p[1] = (uint8_t)((g * a8 + p[1] * dk) / oa);
    p[2] = (uint8_t)((b * a8 + p[2] * dk) / oa);
    p[3] = (uint8_t)oa;
}

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#define MAX_FONTS 32
static stbtt_fontinfo g_fonts[MAX_FONTS];
static int g_font_ok[MAX_FONTS];
static int g_nfonts = 1;

static stbtt_fontinfo *ft_get(int slot) {
    if (slot < 0 || slot >= g_nfonts || (slot > 0 && !g_font_ok[slot])) slot = 0;
    if (slot == 0 && !g_font_ok[0]) {
        if (!stbtt_InitFont(&g_fonts[0], font_data, stbtt_GetFontOffsetForIndex(font_data, 0))) return NULL;
        g_font_ok[0] = 1;
    }
    return &g_fonts[slot];
}

int tx_font_load(const uint8_t *data, size_t len) {
    if (!data || len < 12 || g_nfonts >= MAX_FONTS) return -1;
    uint8_t *copy = malloc(len);
    if (!copy) return -1;
    memcpy(copy, data, len);
    int off = stbtt_GetFontOffsetForIndex(copy, 0);
    if (off < 0 || !stbtt_InitFont(&g_fonts[g_nfonts], copy, off)) { free(copy); return -1; }
    g_font_ok[g_nfonts] = 1;
    return g_nfonts++;
}

static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c;
    int n = 1;
    if (p[0] < 0x80) { c = p[0]; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); n = 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) { c = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); n = 3; }
    else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        c = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); n = 4;
    } else { c = '?'; }
    *s += n;
    return c;
}

static int text_px(double size) {
    int px = (int)(size + 0.5);
    return px < 1 ? 1 : px;
}

static float font_scale(stbtt_fontinfo *f, double size) { return stbtt_ScaleForMappingEmToPixels(f, (float)text_px(size)); }

double tx_text_width(const char *text, double size, int bold, int font) {
    stbtt_fontinfo *f = ft_get(font);
    if (!text || !f) return 0;
    float sc = font_scale(f, size);
    double w = 0;
    const char *s = text;
    while (*s) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f, (int)utf8_next(&s), &adv, &lsb);
        w += adv * sc;
    }
    return w + (bold ? 1 : 0);
}

static int text_render(const char *text, double size, int bold, int font, int r, int g, int b,
                       uint8_t **out, int *ow, int *oh, int *oasc) {
    stbtt_fontinfo *f = ft_get(font);
    if (!f) return 0;
    float sc = font_scale(f, size);
    int fa, fd, gap;
    stbtt_GetFontVMetrics(f, &fa, &fd, &gap);
    int asc = (int)ceil(fa * sc) + 1;
    int desc = (int)ceil(-fd * sc) + 1;
    int h = asc + desc + 2;
    int w = (int)ceil(tx_text_width(text, size, bold, font)) + 6;
    if (w < 1) w = 1;
    uint8_t *buf = calloc((size_t)w * h, 4);
    if (!buf) return 0;
    double pen = 2;
    const char *s = text;
    while (*s) {
        int cp = (int)utf8_next(&s), adv, lsb, x0, y0, x1, y1;
        stbtt_GetCodepointHMetrics(f, cp, &adv, &lsb);
        stbtt_GetCodepointBitmapBox(f, cp, sc, sc, &x0, &y0, &x1, &y1);
        int bw = x1 - x0, bh = y1 - y0;
        if (bw > 0 && bh > 0) {
            unsigned char *bm = malloc((size_t)bw * bh);
            if (bm) {
                stbtt_MakeCodepointBitmap(f, bm, bw, bh, bw, sc, sc, cp);
                int gx = (int)(pen + 0.5) + x0, gy = asc + y0;
                for (int pass = 0; pass < (bold ? 2 : 1); pass++) {
                    for (int yy = 0; yy < bh; yy++) {
                        for (int xx = 0; xx < bw; xx++) {
                            int px = gx + xx + pass, py = gy + yy;
                            if (px < 0 || py < 0 || px >= w || py >= h) continue;
                            int cov = bm[yy * bw + xx];
                            if (!cov) continue;
                            put_px(buf + ((size_t)py * w + px) * 4, r, g, b, cov);
                        }
                    }
                }
                free(bm);
            }
        }
        pen += adv * sc;
    }
    *out = buf; *ow = w; *oh = h; *oasc = asc;
    return 1;
}

#define TEXT_CACHE 256
typedef struct {
    char *text; double size; int bold, font, r, g, b;
    uint8_t *buf; int w, h, asc;
} TextEntry;
static TextEntry tcache[TEXT_CACHE];
static int tcache_next = 0;

int tx_text_get(const char *text, double size, int bold, int font, int r, int g, int b,
                const uint8_t **buf, int *ow, int *oh, int *oasc) {
    if (!text) return 0;
    for (int i = 0; i < TEXT_CACHE; i++) {
        TextEntry *e = &tcache[i];
        if (e->text && e->size == size && e->bold == bold && e->font == font && e->r == r && e->g == g && e->b == b &&
            strcmp(e->text, text) == 0) {
            *buf = e->buf; *ow = e->w; *oh = e->h; *oasc = e->asc;
            return 1;
        }
    }
    uint8_t *nb = NULL; int w = 0, h = 0, asc = 0;
    if (!text_render(text, size, bold, font, r, g, b, &nb, &w, &h, &asc)) return 0;
    TextEntry *e = &tcache[tcache_next];
    tcache_next = (tcache_next + 1) % TEXT_CACHE;
    free(e->text); free(e->buf);
    e->text = strdup(text);
    e->size = size; e->bold = bold; e->font = font; e->r = r; e->g = g; e->b = b;
    e->buf = nb; e->w = w; e->h = h; e->asc = asc;
    *buf = nb; *ow = w; *oh = h; *oasc = asc;
    return 1;
}
