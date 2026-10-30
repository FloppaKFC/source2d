#!/usr/bin/env python3
import argparse, datetime, os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import r2
from anunciar import RAIZ, cargar, guardar_y_firmar, huella

REPO = "FloppaKFC/source2d"
NOMBRES = {"linux": "Source2D.x86_64", "windows": "Source2D.exe", "appimage": "Source2D.AppImage"}

def gh():
    local = os.path.join(RAIZ, "build", "toolchain", "gh", "gh")
    return local if os.path.exists(local) else (shutil.which("gh") or sys.exit("Falta gh (GitHub CLI)"))

def main():
    ap = argparse.ArgumentParser(description="Publica una version de Source 2D")
    ap.add_argument("nombre", help='como se ve en el editor, p. ej. "Beta 0.2"')
    ap.add_argument("num", type=int, help="S2D_VERSION_NUM de esa version")
    ap.add_argument("--linux")
    ap.add_argument("--windows")
    ap.add_argument("--appimage")
    ap.add_argument("--extra", action="append", default=[], help="archivo que solo va a la Release (LEEME, licencias...)")
    ap.add_argument("--canal", choices=["estable", "experimental"], default="estable")
    ap.add_argument("--notas", default="")
    ap.add_argument("--sin-noticia", action="store_true")
    ap.add_argument("--importante", action="store_true", help="la noticia sale en una ventana al abrir el editor")
    ap.add_argument("--sin-subir", action="store_true", help="no crea la Release ni hace push (para probar)")
    a = ap.parse_args()
    if not a.linux and not a.windows:
        ap.error("indica al menos --linux o --windows")
    if ".." in a.nombre or "/" in a.nombre:
        ap.error("el nombre no puede llevar '/' ni '..'")

    d = tempfile.mkdtemp(prefix="s2d-novedades-")
    for k, _ in r2.listar("novedades/")[0]:
        open(os.path.join(d, k[len("novedades/"):]), "wb").write(r2.leer(k))
    vj = cargar(d, "versiones.json", "versiones", "versiones")
    nj = cargar(d, "noticias.json", "noticias", "noticias")
    if any(v.get("nombre") == a.nombre for v in vj["versiones"]):
        sys.exit(f"{a.nombre} ya esta publicada")
    if any(v.get("num", 0) >= a.num for v in vj["versiones"] if v.get("canal") == a.canal):
        print(f"Aviso: ya hay una version {a.canal} con numero {a.num} o mayor; los editores no la veran como nueva.")

    etiqueta = a.nombre.replace(" ", "-")
    tmp = tempfile.mkdtemp(prefix="s2d-version-")
    archivos, subidas = {}, []
    for sistema, origen in (("linux", a.linux), ("windows", a.windows), ("appimage", a.appimage)):
        if not origen:
            continue
        dest = os.path.join(tmp, NOMBRES[sistema])
        shutil.copyfile(origen, dest)
        subidas.append(dest)
        archivos[sistema] = {"url": f"https://github.com/{REPO}/releases/download/{etiqueta}/{NOMBRES[sistema]}",
                             "blake2b": huella(dest), "tam": os.path.getsize(dest)}

    for e in a.extra:
        if not os.path.isfile(e):
            sys.exit(f"No existe {e}")
        subidas.append(e)
    if not a.sin_subir:
        subprocess.run([gh(), "release", "create", etiqueta, *subidas, "--repo", REPO, "--title", f"Source 2D {a.nombre}",
                        "--notes", a.notas or f"Source 2D {a.nombre}"] + (["--prerelease"] if a.canal != "estable" else []),
                       check=True)
    shutil.rmtree(tmp, ignore_errors=True)

    hoy = datetime.date.today().isoformat()
    vj["versiones"].append({"nombre": a.nombre, "num": a.num, "canal": a.canal, "fecha": hoy, "notas": a.notas,
                            "archivos": archivos})
    vj["versiones"].sort(key=lambda v: v.get("num", 0), reverse=True)
    if not a.sin_noticia:
        ns = nj["noticias"]
        n = {"id": max([x.get("id", 0) for x in ns], default=0) + 1, "fecha": hoy, "tipo": "actualizacion",
             "titulo": f"Nueva actualización: {a.nombre}" + (" (experimental)" if a.canal != "estable" else ""),
             "texto": a.notas, "version": a.nombre}
        if a.importante:
            n["importante"] = True
        ns.append(n)
    guardar_y_firmar(d, {"versiones.json": vj, "noticias.json": nj})
    if a.sin_subir:
        print(f"Firmado en {d}. No se ha subido (--sin-subir).")
        return
    for f in ("versiones.json", "versiones.json.firma", "noticias.json", "noticias.json.firma"):
        r2.subir_archivo("novedades/" + f, os.path.join(d, f), False, "application/json; charset=utf-8" if f.endswith(".json") else "text/plain")
    shutil.rmtree(d, ignore_errors=True)
    print(f"Publicada {a.nombre}: los editores la verán en Inicio > Noticias y en el selector de versiones.")

if __name__ == "__main__":
    main()
