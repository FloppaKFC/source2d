#!/usr/bin/env python3
# Source 2D - publica una version nueva para que los editores la vean en Noticias y en el selector de versiones.
# Sube los ejecutables a una Release de FloppaKFC/source2d-versiones (con gh), apunta su huella BLAKE2b en
# versiones.json, crea la noticia "Nueva actualizacion" y lo firma todo (pide tu contrasena una vez).
#   publicar_version.py "Beta 0.2" 2 --linux Release/Linux/Source2D --windows Release/Windows/Source2D.exe
#                       [--canal estable|experimental] [--notas "Que trae"] [--sin-noticia] [--importante] [--sin-subir]
# El numero (2) es S2D_VERSION_NUM de src/editor/version.hpp: tiene que ser mayor que el de las versiones anteriores.
import argparse, datetime, os, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from anunciar import RAIZ, cargar, guardar_y_firmar, huella, repo, subir

REPO = "FloppaKFC/source2d-versiones"
NOMBRES = {"linux": "Source2D-linux", "windows": "Source2D-windows.exe"}


def gh():
    local = os.path.join(RAIZ, "build", "toolchain", "gh", "gh")
    return local if os.path.exists(local) else (shutil.which("gh") or sys.exit("Falta gh (GitHub CLI)"))


def main():
    ap = argparse.ArgumentParser(description="Publica una version de Source 2D")
    ap.add_argument("nombre", help='como se ve en el editor, p. ej. "Beta 0.2"')
    ap.add_argument("num", type=int, help="S2D_VERSION_NUM de esa version")
    ap.add_argument("--linux")
    ap.add_argument("--windows")
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

    d = repo()
    vj = cargar(d, "versiones.json", "versiones", "versiones")
    nj = cargar(d, "noticias.json", "noticias", "noticias")
    if any(v.get("nombre") == a.nombre for v in vj["versiones"]):
        sys.exit(f"{a.nombre} ya esta publicada")
    if any(v.get("num", 0) >= a.num for v in vj["versiones"] if v.get("canal") == a.canal):
        print(f"Aviso: ya hay una version {a.canal} con numero {a.num} o mayor; los editores no la veran como nueva.")

    etiqueta = a.nombre.replace(" ", "-")
    tmp = tempfile.mkdtemp(prefix="s2d-version-")
    archivos, subidas = {}, []
    for sistema, origen in (("linux", a.linux), ("windows", a.windows)):
        if not origen:
            continue
        dest = os.path.join(tmp, NOMBRES[sistema])
        shutil.copyfile(origen, dest)
        subidas.append(dest)
        archivos[sistema] = {"url": f"https://github.com/{REPO}/releases/download/{etiqueta}/{NOMBRES[sistema]}",
                             "blake2b": huella(dest), "tam": os.path.getsize(dest)}

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
        print("Firmado. No se ha subido (--sin-subir).")
    else:
        subir(d, f"Version {a.nombre}")


if __name__ == "__main__":
    main()
