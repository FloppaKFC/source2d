/* Source 2D - compresor LZMA sencillo (LZMA1 en crudo, el que lee src/lzdec.h) para el proyecto de los juegos del
 * exportacion ligera. Busqueda con cadenas de hash, eleccion voraz con un paso de espera (lazy) y las cuatro distancias
 * repetidas de LZMA; sin el analisis de precios del compresor oficial, pero queda bastante por debajo de deflate.
 *
 *   size_t lz_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, unsigned props)
 *       props = lc + lp*9 + pb*45; devuelve los bytes escritos o 0 si no cupo en 'cap' (con n + n/2 + 64 siempre cabe)
 *
 * Se puede incluir desde C y desde C++. Comprobado contra lz_decode (el editor descomprime y compara antes de usarlo). */
#ifndef S2D_LZENC_H
#define S2D_LZENC_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lzdec.h"

typedef struct {
    uint64_t low;
    uint32_t range;
    uint8_t cache;
    uint64_t cache_size;
    uint8_t *out;
    size_t pos, cap;
} LzEnc;

static void lze_put(LzEnc *e, uint8_t b) { if (e->pos < e->cap) e->out[e->pos] = b; e->pos++; }

static void lze_shift(LzEnc *e) {
    if ((uint32_t)e->low < 0xFF000000u || (e->low >> 32) != 0) {
        uint8_t t = e->cache;
        do { lze_put(e, (uint8_t)(t + (uint8_t)(e->low >> 32))); t = 0xFF; } while (--e->cache_size != 0);
        e->cache = (uint8_t)(e->low >> 24);
    }
    e->cache_size++;
    e->low = (e->low & 0x00FFFFFFu) << 8;
}

static void lze_bit(LzEnc *e, uint16_t *p, unsigned bit) {
    uint32_t bound = (e->range >> 11) * *p;
    if (!bit) { e->range = bound; *p += ((1u << 11) - *p) >> 5; }
    else { e->low += bound; e->range -= bound; *p -= *p >> 5; }
    while (e->range < (1u << 24)) { e->range <<= 8; lze_shift(e); }
}

static void lze_direct(LzEnc *e, uint32_t v, int bits) {
    while (bits--) {
        e->range >>= 1;
        e->low += e->range & (0u - ((v >> bits) & 1));
        while (e->range < (1u << 24)) { e->range <<= 8; lze_shift(e); }
    }
}

static void lze_tree(LzEnc *e, uint16_t *probs, int bits, unsigned sym) {
    unsigned m = 1;
    while (bits--) { unsigned b = (sym >> bits) & 1; lze_bit(e, probs + m, b); m = (m << 1) | b; }
}

static void lze_rtree(LzEnc *e, uint16_t *probs, int bits, unsigned sym) {
    unsigned m = 1;
    for (int i = 0; i < bits; i++) { unsigned b = sym & 1; sym >>= 1; lze_bit(e, probs + m, b); m = (m << 1) | b; }
}

static void lze_len(LzEnc *e, uint16_t *p, unsigned len, unsigned ps) {   /* len ya sin el minimo de 2 */
    if (len < 8) { lze_bit(e, p + LZ_LEN_CHOICE, 0); lze_tree(e, p + LZ_LEN_LOW + ps * 8, 3, len); return; }
    lze_bit(e, p + LZ_LEN_CHOICE, 1);
    if (len < 16) { lze_bit(e, p + LZ_LEN_CHOICE2, 0); lze_tree(e, p + LZ_LEN_MID + ps * 8, 3, len - 8); return; }
    lze_bit(e, p + LZ_LEN_CHOICE2, 1);
    lze_tree(e, p + LZ_LEN_HIGH, 8, len - 16);
}

#define LZE_MAXLEN 273
#define LZE_HBITS 20
#define LZE_NICE 273       /* largo con el que una coincidencia ya no se puede mejorar: se deja de buscar */
#define LZE_FAR(small, big) (((big) >> 7) > (small))

static unsigned lze_hash(const uint8_t *p) { return ((p[0] << 16 | p[1] << 8 | p[2]) * 2654435761u) >> (32 - LZE_HBITS); }

static unsigned lze_same(const uint8_t *a, const uint8_t *b, unsigned max) {
    unsigned k = 0;
    while (k < max && a[k] == b[k]) k++;
    return k;
}

typedef struct { const uint8_t *in; size_t n; int32_t *head, *prev; int depth; } LzFinder;

/* la coincidencia mas larga en 'pos' (y su distancia - 1 en *dist); mete 'pos' en la cadena */
static unsigned lze_find(LzFinder *f, size_t pos, uint32_t *dist) {
    unsigned best = 0;
    if (pos + 3 > f->n) return 0;
    unsigned h = lze_hash(f->in + pos), max = (unsigned)(f->n - pos < LZE_MAXLEN ? f->n - pos : LZE_MAXLEN);
    int32_t c = f->head[h];
    f->prev[pos] = c;
    f->head[h] = (int32_t)pos;
    for (int d = 0; c >= 0 && d < f->depth; d++, c = f->prev[c]) {
        const uint8_t *a = f->in + c, *b = f->in + pos;
        if (a[best] != b[best]) continue;
        unsigned k = lze_same(a, b, max);
        if (k > best) { best = k; *dist = (uint32_t)(pos - (size_t)c - 1); if (k >= max || k >= LZE_NICE) break; }
    }
    return best;
}

static size_t lz_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, unsigned props) {
    unsigned lc = props % 9, lp = (props / 9) % 5, pb = props / 45;
    if (pb > 4 || lc + lp > 4) return 0;
    uint16_t *P = (uint16_t *)malloc(sizeof(uint16_t) * LZ_PROBS);
    /* profundidad de busqueda segun el tamano: en proyectos grandes (audio e imagenes en base64, casi sin repeticiones)
       buscar mucho apenas mejora y lo vuelve muy lento */
    int depth = n > (8u << 20) ? 16 : n > (2u << 20) ? 48 : 256;
    LzFinder f = { in, n, (int32_t *)malloc(sizeof(int32_t) << LZE_HBITS), (int32_t *)malloc(sizeof(int32_t) * (n + 1)), depth };
    if (!P || !f.head || !f.prev) { free(P); free(f.head); free(f.prev); return 0; }
    for (size_t i = 0; i < LZ_PROBS; i++) P[i] = 1024;
    for (size_t i = 0; i < ((size_t)1 << LZE_HBITS); i++) f.head[i] = -1;
    LzEnc e = { 0, 0xFFFFFFFFu, 0, 1, out, 0, cap };
    uint32_t rep[4] = { 0, 0, 0, 0 };
    unsigned state = 0;
    const unsigned pbm = (1u << pb) - 1, lpm = (1u << lp) - 1;
    size_t pos = 0;
    uint32_t mdist = 0;
    unsigned mlen = lze_find(&f, 0, &mdist);
    while (pos < n) {
        unsigned ps = (unsigned)pos & pbm, max = (unsigned)(n - pos < LZE_MAXLEN ? n - pos : LZE_MAXLEN);
        /* la mejor de las distancias repetidas */
        unsigned rlen = 0, ridx = 0;
        for (unsigned r = 0; r < 4; r++) {
            if (rep[r] >= pos) continue;
            unsigned k = lze_same(in + pos - rep[r] - 1, in + pos, max);
            if (k > rlen) { rlen = k; ridx = r; }
        }
        if (mlen == 2 && mdist >= 128) mlen = 0;
        if (mlen < 2) mlen = 0;
        /* como el modo rapido del compresor oficial: una distancia repetida sale mucho mas barata que una nueva */
        int use_rep = rlen >= 2 && (rlen >= LZE_NICE || rlen + 1 >= mlen || (rlen + 2 >= mlen && mdist >= (1u << 9)) ||
                                    (rlen + 3 >= mlen && mdist >= (1u << 15)));
        unsigned len = use_rep ? rlen : mlen;
        /* espera un byte si el siguiente da una coincidencia mejor */
        uint32_t ndist = 0;
        unsigned nlen = pos + 1 < n ? lze_find(&f, pos + 1, &ndist) : 0;
        if (len >= 2 && !use_rep && len < LZE_NICE) {
            if (nlen >= 2 && ((nlen >= len && ndist < mdist) || (nlen == len + 1 && !LZE_FAR(mdist, ndist)) || nlen > len + 1 ||
                              (nlen + 1 >= len && len >= 3 && LZE_FAR(ndist, mdist))))
                len = 0;
            else {      /* o si en el siguiente byte alguna distancia repetida llega casi tan lejos */
                unsigned lim = len - 1 > 2 ? len - 1 : 2, mx = (unsigned)(n - pos - 1 < LZE_MAXLEN ? n - pos - 1 : LZE_MAXLEN);
                for (unsigned r = 0; r < 4 && len && pos + 1 < n; r++)
                    if (rep[r] < pos + 1 && lze_same(in + pos - rep[r], in + pos + 1, lim) >= lim && lim <= mx) len = 0;
            }
        }
        if (len < 2) {
            uint8_t b = in[pos];
            if (pos > rep[0] && in[pos - rep[0] - 1] == b && state >= 7 && !(nlen > 2)) {   /* repetir un byte (short rep) */
                lze_bit(&e, P + LZ_IS_MATCH + (state << 4) + ps, 1);
                lze_bit(&e, P + LZ_IS_REP + state, 1);
                lze_bit(&e, P + LZ_IS_REP_G0 + state, 0);
                lze_bit(&e, P + LZ_IS_REP0_LONG + (state << 4) + ps, 0);
                state = state < 7 ? 9 : 11;
            } else {
                lze_bit(&e, P + LZ_IS_MATCH + (state << 4) + ps, 0);
                unsigned prev = pos ? in[pos - 1] : 0;
                uint16_t *lit = P + LZ_LITERAL + 0x300 * (((((unsigned)pos & lpm) << lc) + (prev >> (8 - lc))));
                unsigned s = 1, matched = state >= 7 && pos > rep[0];
                unsigned mb = matched ? in[pos - rep[0] - 1] : 0;
                for (int i = 7; i >= 0; i--) {
                    unsigned bit = (b >> i) & 1;
                    if (matched) {
                        unsigned m = (mb >> i) & 1;
                        lze_bit(&e, lit + ((1 + m) << 8) + s, bit);
                        if (m != bit) matched = 0;
                    } else lze_bit(&e, lit + s, bit);
                    s = (s << 1) | bit;
                }
                state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            }
            pos++;
            mlen = nlen; mdist = ndist;
            continue;
        }
        lze_bit(&e, P + LZ_IS_MATCH + (state << 4) + ps, 1);
        if (use_rep) {
            lze_bit(&e, P + LZ_IS_REP + state, 1);
            if (ridx == 0) {
                lze_bit(&e, P + LZ_IS_REP_G0 + state, 0);
                lze_bit(&e, P + LZ_IS_REP0_LONG + (state << 4) + ps, 1);
            } else {
                lze_bit(&e, P + LZ_IS_REP_G0 + state, 1);
                if (ridx == 1) lze_bit(&e, P + LZ_IS_REP_G1 + state, 0);
                else {
                    lze_bit(&e, P + LZ_IS_REP_G1 + state, 1);
                    lze_bit(&e, P + LZ_IS_REP_G2 + state, ridx == 3);
                }
                uint32_t d = rep[ridx];
                for (unsigned r = ridx; r > 0; r--) rep[r] = rep[r - 1];
                rep[0] = d;
            }
            lze_len(&e, P + LZ_REP_LEN_CODER, len - 2, ps);
            state = state < 7 ? 8 : 11;
        } else {
            lze_bit(&e, P + LZ_IS_REP + state, 0);
            lze_len(&e, P + LZ_LEN_CODER, len - 2, ps);
            state = state < 7 ? 7 : 10;
            uint32_t d = mdist;
            unsigned slot;
            if (d < 4) slot = d;
            else { unsigned hb = 31; while (!(d >> hb)) hb--; slot = (hb << 1) | ((d >> (hb - 1)) & 1); }
            lze_tree(&e, P + LZ_POS_SLOT + (len - 2 < 3 ? len - 2 : 3) * 64, 6, slot);
            if (slot >= 4) {
                int nd = (int)(slot >> 1) - 1;
                uint32_t base = (2 | (slot & 1)) << nd, red = d - base;
                if (slot < 14) lze_rtree(&e, P + LZ_SPEC_POS + base - slot, nd, red);
                else { lze_direct(&e, red >> 4, nd - 4); lze_rtree(&e, P + LZ_ALIGN, 4, red & 15); }
            }
            rep[3] = rep[2]; rep[2] = rep[1]; rep[1] = rep[0]; rep[0] = d;
        }
        /* meter en las cadenas las posiciones que se saltan (pos + 1 ya esta) */
        size_t end = pos + len;
        for (size_t k = pos + 2; k < end; k++) { uint32_t dd; lze_find(&f, k, &dd); }
        pos = end;
        mlen = pos < n ? lze_find(&f, pos, &mdist) : 0;
    }
    for (int i = 0; i < 5; i++) lze_shift(&e);
    free(P); free(f.head); free(f.prev);
    return e.pos <= cap ? e.pos : 0;
}
#endif
