#!/usr/bin/env python3
"""LZMA (formato crudo, el que lee src/lzdec.h) con el filtro x86 de las llamadas: lo usan tools/gen_players.py (motores
que lleva el editor) y tools/pack_exe.py (el editor comprimido de la Release)."""
import lzma
import struct
import sys

def x86_encode(d):
    """llamadas y saltos relativos (E8/E9 rel32) a direcciones absolutas; lo deshace lz_x86_decode"""
    b, i, n = bytearray(d), 0, len(d)
    while i + 5 <= n:
        if (b[i] & 0xFE) != 0xE8:
            i += 1
            continue
        if b[i + 4] not in (0, 0xFF):
            i += 5
            continue
        v = (struct.unpack_from('<I', b, i + 1)[0] + i + 5) & 0x1FFFFFF
        if v & 0x1000000:
            v |= 0xFE000000
        struct.pack_into('<I', b, i + 1, v)
        i += 5
    return bytes(b)

def x86_decode(d):
    """lo mismo que lz_x86_decode (src/lzdec.h): se usa para comprobar que el filtro se deshace bien"""
    b, i, n = bytearray(d), 0, len(d)
    while i + 5 <= n:
        if (b[i] & 0xFE) != 0xE8:
            i += 1
            continue
        if b[i + 4] in (0, 0xFF):
            v = (struct.unpack_from('<I', b, i + 1)[0] - (i + 5)) & 0x1FFFFFF
            if v & 0x1000000:
                v |= 0xFE000000
            struct.pack_into('<I', b, i + 1, v)
        i += 5
    return bytes(b)

LC, LP, PB = 3, 0, 0

def pack(d):
    f = dict(id=lzma.FILTER_LZMA1, preset=9 | lzma.PRESET_EXTREME, lc=LC, lp=LP, pb=PB, dict_size=1 << 24, nice_len=273)
    x = x86_encode(d)
    if x86_decode(x) != d:
        sys.exit('lzpack.py: el filtro x86 no se deshace bien')
    z = lzma.compress(x, format=lzma.FORMAT_RAW, filters=[f])
    if lzma.decompress(z, format=lzma.FORMAT_RAW, filters=[f]) != x:
        sys.exit('lzpack.py: LZMA no devuelve lo mismo')
    return z, LC + LP * 9 + PB * 45 + 256
