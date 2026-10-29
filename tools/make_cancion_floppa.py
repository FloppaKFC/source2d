#!/usr/bin/env python3
"""Genera src/editor/presets/cancion_floppa.s2dsound: la cancion de ejemplo de la Escuela (32 s a 120 bpm, 16 compases).
Do - La menor - Fa - Sol: intro con piano y bateria suave, estrofa con melodia de chip, puente con cuerdas y final.
Uso: tools/make_cancion_floppa.py"""
import json, os

C, Am, F, G = (48, [60, 64, 67]), (45, [57, 60, 64]), (41, [57, 60, 65]), (43, [59, 62, 67])
prog = [C, Am, F, G]
piano, bass, lead, strings, drums = [], [], [], [], []

for bar in range(16):
    root, chord = prog[bar % 4]
    s0 = bar * 16
    last = bar == 15
    if last:
        for m in [60, 64, 67, 72]: piano.append([s0, m, 16])
    else:
        for st in (0, 6, 10):
            for m in chord: piano.append([s0 + st, m, 3])
    if 4 <= bar and not last:
        for st, d in ((0, 0), (6, 7), (8, 0), (12, 12)): bass.append([s0 + st, root + d, 3])
    elif last: bass.append([s0, 36, 16])
    if not last:
        for st in range(0, 16, 2): drums.append([s0 + st, 2, 1])
        drums.append([s0, 0, 1]); drums.append([s0 + 8, 0, 1])
        if bar >= 4:
            drums.append([s0 + 4, 1, 1]); drums.append([s0 + 12, 1, 1]); drums.append([s0 + 10, 0, 1])
        if bar == 11:
            for st in (12, 13, 14, 15): drums.append([s0 + st, 1, 1])
        if bar in (4, 12): drums.append([s0, 7, 1])
    else:
        drums.append([s0, 0, 1]); drums.append([s0, 7, 1])

estrofa = [
    [(0, 72, 2), (2, 74, 2), (4, 76, 4), (8, 79, 4), (12, 76, 4)],
    [(0, 76, 2), (2, 74, 2), (4, 72, 4), (8, 69, 6), (14, 72, 2)],
    [(0, 72, 2), (2, 74, 2), (4, 77, 4), (8, 76, 2), (10, 74, 2), (12, 72, 4)],
    [(0, 74, 4), (4, 71, 2), (6, 74, 2), (8, 79, 8)],
]
for k, notes in enumerate(estrofa):
    for st, m, l in notes: lead.append([(4 + k) * 16 + st, m, l])
for k, notes in enumerate(estrofa[:3]):
    for st, m, l in notes: lead.append([(12 + k) * 16 + st, m, l])
for k in range(4):
    root, chord = prog[k]
    for m in chord: strings.append([(8 + k) * 16, m + 12 if m < 60 else m, 16])
puente = [(0, 79, 4), (4, 81, 4), (8, 79, 8), (0, 76, 6), (8, 77, 8), (0, 77, 4), (4, 76, 4), (8, 74, 8), (0, 74, 4), (4, 76, 4), (8, 79, 8)]
for k in range(4):
    for st, m, l in puente[k * 3:(k + 1) * 3] if k < 3 else puente[9:]:
        lead.append([(8 + k) * 16 + st, m, l])
lead.append([14 * 16 + 0, 74, 4]); lead.append([14 * 16 + 4, 76, 4]); lead.append([14 * 16 + 8, 74, 8])

song = {"format": "source2d-sound", "name": "Test", "mode": "music", "bpm": 120, "bars": 16,
        "tracks": [{"name": "Melodía", "inst": "chip", "vol": 0.55, "notes": lead},
                   {"name": "Piano", "inst": "piano", "vol": 0.5, "notes": piano},
                   {"name": "Bajo", "inst": "bajo", "vol": 0.75, "notes": bass},
                   {"name": "Cuerdas", "inst": "cuerdas", "vol": 0.4, "notes": strings},
                   {"name": "Batería", "inst": "bateria", "vol": 0.7, "notes": drums}]}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "editor", "presets", "cancion_floppa.s2dsound")
json.dump(song, open(out, "w"), ensure_ascii=False, separators=(",", ":"))
print(out, sum(len(t["notes"]) for t in song["tracks"]), "notas")
