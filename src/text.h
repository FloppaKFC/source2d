#ifndef S2D_TEXT_H
#define S2D_TEXT_H
#include <stdint.h>
#include <stddef.h>

int tx_text_get(const char *text, double size, int bold, int font, int r, int g, int b,
                const uint8_t **buf, int *ow, int *oh, int *oasc);
double tx_text_width(const char *text, double size, int bold, int font);
int tx_font_load(const uint8_t *data, size_t len);

#endif
