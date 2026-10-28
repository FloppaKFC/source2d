/* Source 2D engine - letras rasterizadas con stb_truetype (RGBA8, alfa directo). */
#ifndef S2D_TEXT_H
#define S2D_TEXT_H
#include <stdint.h>
#include <stddef.h>

/* Texto con cache. Devuelve 1 si ok. El buffer devuelto es interno: copiarlo
   antes de la siguiente llamada. */
int tx_text_get(const char *text, double size, int bold, int font, int r, int g, int b,
                const uint8_t **buf, int *ow, int *oh, int *oasc);
double tx_text_width(const char *text, double size, int bold, int font);
/* carga una fuente TTF/OTF (copia los datos); devuelve su numero para tx_text_* o -1 */
int tx_font_load(const uint8_t *data, size_t len);

#endif
