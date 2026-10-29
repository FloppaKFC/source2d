#!/usr/bin/env python3
"""Compara dos carpetas de capturas de test/render_shots.py: por cada imagen, el porcentaje de pixeles que cambian
mas de un umbral y la diferencia media; guarda las diferencias (amplificadas) en <B>/diff/.
Uso: test/render_compare.py <carpeta A> <carpeta B> [umbral=24]"""
import os
import sys

import numpy as np
from PIL import Image

def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    a, b = sys.argv[1], sys.argv[2]
    umbral = int(sys.argv[3]) if len(sys.argv) > 3 else 24
    os.makedirs(os.path.join(b, 'diff'), exist_ok=True)
    peor = 0.0
    for f in sorted(os.listdir(a)):
        if not f.endswith('.png'):
            continue
        pb = os.path.join(b, f)
        if not os.path.exists(pb):
            print('%-26s FALTA' % f)
            continue
        A = np.asarray(Image.open(os.path.join(a, f)).convert('RGB')).astype(np.int16)
        B = np.asarray(Image.open(pb).convert('RGB')).astype(np.int16)
        if A.shape != B.shape:
            print('%-26s TAMANO %s %s' % (f, A.shape, B.shape))
            continue
        d = np.abs(A - B).max(axis=2)
        pct = 100.0 * (d > umbral).mean()
        peor = max(peor, pct)
        print('%-26s %6.2f %% pixeles distintos, media %.2f, max %d' % (f, pct, d.mean(), d.max()))
        if d.max() > 0:
            Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(os.path.join(b, 'diff', f))
    print('peor: %.2f %%' % peor)

if __name__ == '__main__':
    main()
