#!/usr/bin/env python3
"""Archivos para test/editor_studio.txt: paletas en los formatos que lee el editor de sprites (los de Aseprite) y una
hoja de 4 frames de 16x16. Uso: studio_data.py <carpeta>"""
import struct, sys, zlib
d = sys.argv[1]
cols = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (10, 20, 30)]
ent = b''.join(struct.pack('<HBBBB', 0, r, g, b, 255) for r, g, b in cols)
pal = struct.pack('<III8x', len(cols), 0, len(cols) - 1) + ent
chunk = struct.pack('<IH', 6 + len(pal), 0x2019) + pal
frame = struct.pack('<IHHH2xI', 16 + len(chunk), 0xF1FA, 1, 100, 1) + chunk
hdr = struct.pack('<IHHHHHIH8xB3xH', 128 + len(frame), 0xA5E0, 1, 16, 16, 32, 1, 100, 0, len(cols)).ljust(128, b'\0')
open(d + '/p.aseprite', 'wb').write(hdr + frame)
open(d + '/p.gpl', 'w').write('GIMP Palette\nName: x\nColumns: 4\n#\n255   0   0\tRojo\n  0 255   0\n  0   0 255\n')
open(d + '/p.hex', 'w').write('ff0000\n00ff00\n0000ff\nffffff\n')
open(d + '/p.pal', 'w').write('JASC-PAL\r\n0100\r\n2\r\n1 2 3\r\n4 5 6\r\n')
open(d + '/p.txt', 'w').write(';paint.net\nFFFF0000\nFF00FF00\n')
a = bytearray(772); a[0:6] = bytes([9, 8, 7, 6, 5, 4]); a[768:770] = (2).to_bytes(2, 'big')
open(d + '/p.act', 'wb').write(a)

def swatch(r, g, b):
    name = 'c'.encode('utf-16-be') + b'\0\0'
    body = struct.pack('>H', 2) + name + b'RGB ' + struct.pack('>fff', r, g, b) + struct.pack('>H', 2)
    return struct.pack('>HI', 1, len(body)) + body

open(d + '/p.ase', 'wb').write(b'ASEF' + struct.pack('>HHI', 1, 0, 2) + swatch(1, 0, 0) + swatch(0, 0.5, 1))
W, H = 64, 16
rows = b''
for y in range(H):
    row = b'\0'
    for x in range(W):
        i = x // 16
        inside = 4 <= y < 12 and 4 <= x % 16 < 12
        row += bytes([(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)][i]) if inside else b'\0\0\0\0'
    rows += row

def ck(t, b):
    return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))

open(d + '/hoja.png', 'wb').write(b'\x89PNG\r\n\x1a\n' + ck(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) +
                                  ck(b'IDAT', zlib.compress(rows)) + ck(b'IEND', b''))
