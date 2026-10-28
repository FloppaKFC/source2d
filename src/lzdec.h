/* Source 2D - descompresor LZMA (formato LZMA1 en crudo, el de Python lzma.FORMAT_RAW con FILTER_LZMA1) y filtro
 * x86 para el lanzador de la exportacion ligera y el editor. Los motores se comprimen con LZMA al generar el editor (tools/gen_players.py):
 * ocupa bastante menos que con deflate y deja mas sitio para el juego.
 *
 * Solo descomprime a un bufer que ya tiene el tamano final (el diccionario es la propia salida). Sin memoria
 * dinamica con LZ_NO_MALLOC (el lanzador de Windows, que no lleva CRT); si no, las probabilidades van en el monton
 * y se puede usar desde varios hilos (el editor).
 *
 *   lz_decode(out, out_len, in, in_len, props)  props = lc + lp*9 + pb*45 (como el primer byte de un .lzma)
 *                                               devuelve 0 si salio bien
 *   lz_x86_decode(buf, len)                     deshace el filtro de llamadas x86 (tools/gen_players.py: x86_encode)
 */
#ifndef S2D_LZDEC_H
#define S2D_LZDEC_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *in, *end;
    uint32_t range, code;
} LzRc;

static uint8_t lz_byte(LzRc *rc) { return rc->in < rc->end ? *rc->in++ : 0; }

static void lz_norm(LzRc *rc) {
    if (rc->range < (1u << 24)) { rc->range <<= 8; rc->code = (rc->code << 8) | lz_byte(rc); }
}

static unsigned lz_bit(LzRc *rc, uint16_t *p) {
    uint32_t bound = (rc->range >> 11) * *p;
    unsigned b;
    if (rc->code < bound) { *p += ((1u << 11) - *p) >> 5; rc->range = bound; b = 0; }
    else { *p -= *p >> 5; rc->code -= bound; rc->range -= bound; b = 1; }
    lz_norm(rc);
    return b;
}

static unsigned lz_tree(LzRc *rc, uint16_t *probs, int bits) {
    unsigned m = 1;
    for (int i = 0; i < bits; i++) m = (m << 1) | lz_bit(rc, probs + m);
    return m - (1u << bits);
}

static unsigned lz_rtree(LzRc *rc, uint16_t *probs, int bits) {
    unsigned m = 1, s = 0;
    for (int i = 0; i < bits; i++) { unsigned b = lz_bit(rc, probs + m); m = (m << 1) | b; s |= b << i; }
    return s;
}

static uint32_t lz_direct(LzRc *rc, int bits) {
    uint32_t r = 0;
    while (bits--) {
        rc->range >>= 1;
        rc->code -= rc->range;
        uint32_t t = 0u - (rc->code >> 31);
        rc->code += rc->range & t;
        lz_norm(rc);
        r = (r << 1) + (t + 1);
    }
    return r;
}

/* decodificador de longitudes: choice, choice2, low[16][8], mid[16][8], high[256] */
enum { LZ_LEN_CHOICE = 0, LZ_LEN_CHOICE2 = 1, LZ_LEN_LOW = 2, LZ_LEN_MID = LZ_LEN_LOW + 16 * 8, LZ_LEN_HIGH = LZ_LEN_MID + 16 * 8,
       LZ_LEN_SIZE = LZ_LEN_HIGH + 256 };

static unsigned lz_len(LzRc *rc, uint16_t *p, unsigned ps) {
    if (!lz_bit(rc, p + LZ_LEN_CHOICE)) return lz_tree(rc, p + LZ_LEN_LOW + ps * 8, 3);
    if (!lz_bit(rc, p + LZ_LEN_CHOICE2)) return 8 + lz_tree(rc, p + LZ_LEN_MID + ps * 8, 3);
    return 16 + lz_tree(rc, p + LZ_LEN_HIGH, 8);
}

/* tabla de probabilidades: todo en un bloque (con lc + lp <= 4 cabe en 'probs' de 0x300 << 4) */
enum { LZ_IS_MATCH = 0, LZ_IS_REP = LZ_IS_MATCH + 12 * 16, LZ_IS_REP_G0 = LZ_IS_REP + 12, LZ_IS_REP_G1 = LZ_IS_REP_G0 + 12,
       LZ_IS_REP_G2 = LZ_IS_REP_G1 + 12, LZ_IS_REP0_LONG = LZ_IS_REP_G2 + 12, LZ_POS_SLOT = LZ_IS_REP0_LONG + 12 * 16,
       LZ_SPEC_POS = LZ_POS_SLOT + 4 * 64, LZ_ALIGN = LZ_SPEC_POS + 115, LZ_LEN_CODER = LZ_ALIGN + 16,
       LZ_REP_LEN_CODER = LZ_LEN_CODER + LZ_LEN_SIZE, LZ_LITERAL = LZ_REP_LEN_CODER + LZ_LEN_SIZE, LZ_PROBS = LZ_LITERAL + (0x300 << 4) };

#ifndef LZ_NO_MALLOC
#include <stdlib.h>
static int lz_run(uint8_t *out, size_t out_len, const uint8_t *in, size_t in_len, unsigned lc, unsigned lp, unsigned pb, uint16_t *P);
#endif
static int lz_decode(uint8_t *out, size_t out_len, const uint8_t *in, size_t in_len, unsigned props) {
    unsigned lc = props % 9, lp = (props / 9) % 5, pb = props / 45;
    if (pb > 4 || lc + lp > 4) return -1;
#ifdef LZ_NO_MALLOC
    static uint16_t P[LZ_PROBS];          /* lanzador de Windows, sin CRT (un solo hilo) */
#else
    uint16_t *P = (uint16_t *)malloc(sizeof(uint16_t) * LZ_PROBS);
    if (!P) return -1;
    int r = lz_run(out, out_len, in, in_len, lc, lp, pb, P);
    free(P);
    return r;
}
static int lz_run(uint8_t *out, size_t out_len, const uint8_t *in, size_t in_len, unsigned lc, unsigned lp, unsigned pb, uint16_t *P) {
#endif
    for (size_t i = 0; i < LZ_PROBS; i++) P[i] = 1024;
    LzRc rc = { in, in + in_len, 0xFFFFFFFFu, 0 };
    if (lz_byte(&rc) != 0) return -1;
    for (int i = 0; i < 4; i++) rc.code = (rc.code << 8) | lz_byte(&rc);
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    unsigned state = 0;
    size_t pos = 0;
    const unsigned pbm = (1u << pb) - 1, lpm = (1u << lp) - 1;
    while (pos < out_len) {
        unsigned ps = (unsigned)pos & pbm;
        if (!lz_bit(&rc, P + LZ_IS_MATCH + (state << 4) + ps)) {
            unsigned prev = pos ? out[pos - 1] : 0;
            uint16_t *lit = P + LZ_LITERAL + 0x300 * (((((unsigned)pos & lpm) << lc) + (prev >> (8 - lc))));
            unsigned s = 1;
            if (state >= 7) {
                unsigned mb = out[pos - rep0 - 1];
                do {
                    unsigned m = (mb >> 7) & 1;
                    mb <<= 1;
                    unsigned b = lz_bit(&rc, lit + ((1 + m) << 8) + s);
                    s = (s << 1) | b;
                    if (m != b) break;
                } while (s < 0x100);
            }
            while (s < 0x100) s = (s << 1) | lz_bit(&rc, lit + s);
            out[pos++] = (uint8_t)s;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        unsigned len;
        if (lz_bit(&rc, P + LZ_IS_REP + state)) {
            if (!pos) return -1;
            if (!lz_bit(&rc, P + LZ_IS_REP_G0 + state)) {
                if (!lz_bit(&rc, P + LZ_IS_REP0_LONG + (state << 4) + ps)) {
                    state = state < 7 ? 9 : 11;
                    out[pos] = out[pos - rep0 - 1];
                    pos++;
                    continue;
                }
            } else {
                uint32_t d;
                if (!lz_bit(&rc, P + LZ_IS_REP_G1 + state)) d = rep1;
                else {
                    if (!lz_bit(&rc, P + LZ_IS_REP_G2 + state)) d = rep2;
                    else { d = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = d;
            }
            len = lz_len(&rc, P + LZ_REP_LEN_CODER, ps);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = lz_len(&rc, P + LZ_LEN_CODER, ps);
            state = state < 7 ? 7 : 10;
            unsigned slot = lz_tree(&rc, P + LZ_POS_SLOT + (len < 3 ? len : 3) * 64, 6);
            if (slot < 4) rep0 = slot;
            else {
                int nd = (int)(slot >> 1) - 1;
                rep0 = (2 | (slot & 1)) << nd;
                if (slot < 14) rep0 += lz_rtree(&rc, P + LZ_SPEC_POS + rep0 - slot, nd);
                else rep0 += (lz_direct(&rc, nd - 4) << 4) + lz_rtree(&rc, P + LZ_ALIGN, 4);
                if (rep0 == 0xFFFFFFFFu) break;        /* marca de fin */
            }
            if (rep0 >= pos) return -1;
        }
        len += 2;
        if (len > out_len - pos) return -1;
        for (unsigned k = 0; k < len; k++, pos++) out[pos] = out[pos - rep0 - 1];
    }
    return pos == out_len ? 0 : -1;
}

/* filtro x86: en cada E8/E9 (call/jmp rel32) cuyo desplazamiento cabe en 25 bits con signo, el compresor lo guarda como
 * direccion absoluta (mas repetida, comprime mejor); aqui se vuelve a relativa. Tras cualquier E8/E9 se saltan siempre
 * 5 bytes: asi el recorrido solo depende de bytes que el filtro no toca y es el mismo al comprimir y al descomprimir. */
static void lz_x86_decode(uint8_t *b, size_t n) {
    for (size_t i = 0; i + 5 <= n;) {
        if ((b[i] & 0xFE) != 0xE8) { i++; continue; }
        if (b[i + 4] != 0 && b[i + 4] != 0xFF) { i += 5; continue; }
        uint32_t v = (uint32_t)b[i + 1] | (uint32_t)b[i + 2] << 8 | (uint32_t)b[i + 3] << 16 | (uint32_t)b[i + 4] << 24;
        v = (v - (uint32_t)(i + 5)) & 0x1FFFFFFu;
        if (v & 0x1000000u) v |= 0xFE000000u;
        b[i + 1] = (uint8_t)v; b[i + 2] = (uint8_t)(v >> 8); b[i + 3] = (uint8_t)(v >> 16); b[i + 4] = (uint8_t)(v >> 24);
        i += 5;
    }
}
#endif
