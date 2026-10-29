#!/usr/bin/env python3
import math, os, struct, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

W, H = 32, 16
rows = b''
for y in range(H):
    rows += b'\0' + b''.join(bytes((255, 0, 0, 255)) if x < 16 else bytes((0, 0, 255, 255)) for x in range(W))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
open(os.path.join(out, 'dos_colores.png'), 'wb').write(png)

rate, n = 22050, 11025
samples = b''.join(struct.pack('<h', int(8000 * math.sin(2 * math.pi * 440 * i / rate))) for i in range(n))
wav = b'RIFF' + struct.pack('<I', 36 + len(samples)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16) + b'data' + struct.pack('<I', len(samples)) + samples
open(os.path.join(out, 'pitido.wav'), 'wb').write(wav)
