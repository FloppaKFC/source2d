#!/usr/bin/env python3
"""Escribe una imagen PNG de prueba (un degradado de 320x200) para la miniatura propia y la foto de perfil.
Uso: png_prueba.py <salida.png>"""
import struct, sys, zlib

W, H = 320, 200
filas = b"".join(b"\x00" + bytes(v for x in range(W) for v in (x * 255 // W, y * 255 // H, 160)) for y in range(H))


def trozo(tipo, datos):
    return struct.pack(">I", len(datos)) + tipo + datos + struct.pack(">I", zlib.crc32(tipo + datos) & 0xFFFFFFFF)


open(sys.argv[1], "wb").write(b"\x89PNG\r\n\x1a\n" + trozo(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
                              trozo(b"IDAT", zlib.compress(filas)) + trozo(b"IEND", b""))
