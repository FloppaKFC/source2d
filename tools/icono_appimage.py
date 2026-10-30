#!/usr/bin/env python3
import struct, sys, zlib

N = 256
filas = []
for y in range(N):
    f = bytearray([0])
    for x in range(N):
        r = 56
        dx = max(r - x, 0, x - (N - 1 - r))
        dy = max(r - y, 0, y - (N - 1 - r))
        dentro = dx * dx + dy * dy <= r * r
        px, py = (x - 128) / 128, (y - 128) / 128
        tri = px > -0.32 and px - 0.45 < -abs(py) * 1.3 + 0.13 and abs(py) < 0.5
        if not dentro:
            f += bytes([0, 0, 0, 0])
        elif tri:
            f += bytes([255, 255, 255, 255])
        else:
            t = y / N
            f += bytes([int(139 - 40 * t), int(92 - 30 * t), int(246 - 20 * t), 255])
    filas.append(bytes(f))


def trozo(tipo, datos):
    return struct.pack(">I", len(datos)) + tipo + datos + struct.pack(">I", zlib.crc32(tipo + datos) & 0xFFFFFFFF)


png = b"\x89PNG\r\n\x1a\n" + trozo(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) + trozo(b"IDAT", zlib.compress(b"".join(filas), 9)) + trozo(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
