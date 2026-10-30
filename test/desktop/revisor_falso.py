#!/usr/bin/env python3
import glob, json, os, sys, time

privado, motivo = sys.argv[1], sys.argv[2]
fin = time.time() + 300
while time.time() < fin:
    for p in glob.glob(os.path.join(privado, "envios", "*", "*", "estado.json")):
        try:
            e = json.load(open(p, encoding="utf-8"))
        except ValueError:
            continue
        if e.get("estado") == "pendiente":
            time.sleep(1.5)
            e["estado"] = "rechazado"
            e["motivo"] = motivo
            json.dump(e, open(p, "w", encoding="utf-8"))
    time.sleep(0.3)
