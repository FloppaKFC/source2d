#!/usr/bin/env python3
# Source 2D - publica anuncios en Inicio > Noticias de todos los editores (firmados por @floppa-1550).
# Trabaja sobre una copia local del repositorio publico FloppaKFC/source2d-versiones (por defecto ../source2d-versiones,
# o S2D_VERSIONES_DIR): cambia noticias.json, lo firma con build/firma (pide tu contrasena) y lo sube con git.
#   anunciar.py "Titulo" "Texto"  [--tipo aviso|evento|actualizacion|consejo] [--imagen foto.png] [--enlace https://...]
#                                 [--fijar] [--hasta 2026-12-31] [--importante] [--para "Beta 0.1" --para windows]
#   anunciar.py --lista
#   anunciar.py --editar 3 [--titulo ...] [--texto ...] [las mismas opciones; --sin-imagen --sin-enlace --soltar ...]
#   anunciar.py --borrar 3
# --sin-subir deja los cambios firmados en la carpeta sin hacer git commit ni push.
import argparse, datetime, hashlib, json, os, shutil, subprocess, sys

AUTOR = "@floppa-1550"
TIPOS = ["aviso", "evento", "actualizacion", "consejo"]
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMA = os.path.join(RAIZ, "build", "firma")


def repo():
    d = os.environ.get("S2D_VERSIONES_DIR") or os.path.join(os.path.dirname(RAIZ), "source2d-versiones")
    if not os.path.isdir(d):
        sys.exit(f"No existe {d}. Clona alli el repositorio publico:\n"
                 f"  git clone https://github.com/FloppaKFC/source2d-versiones.git \"{d}\"\n"
                 "(o indica otra carpeta con S2D_VERSIONES_DIR)")
    return d


def huella(path):
    h = hashlib.blake2b(digest_size=32)   # la misma que crypto_blake2b(.., 32, ..) de Monocypher
    with open(path, "rb") as f:
        for trozo in iter(lambda: f.read(1 << 16), b""):
            h.update(trozo)
    return h.hexdigest()


def cargar(d, nombre, tipo, lista):
    p = os.path.join(d, nombre)
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            j = json.load(f)
    else:
        j = {}
    j.setdefault(lista, [])
    j["tipo"] = tipo
    j["autor"] = AUTOR
    return j


def guardar_y_firmar(d, archivos):
    """archivos: {nombre: json}. Escribe, firma todos con una sola contrasena y comprueba las firmas."""
    if not os.path.exists(FIRMA):
        sys.exit("Falta build/firma: compila con 'make firma'")
    rutas = []
    for nombre, j in archivos.items():
        j["actualizado"] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        p = os.path.join(d, nombre)
        with open(p, "w", encoding="utf-8") as f:
            json.dump(j, f, ensure_ascii=False, indent=1)
            f.write("\n")
        rutas.append(p)
    subprocess.run([FIRMA, "firmar", *rutas], check=True)
    for p in rutas:
        subprocess.run([FIRMA, "verificar", p], check=True)


def subir(d, mensaje, extra=()):
    subprocess.run(["git", "-C", d, "add", "noticias.json", "noticias.json.firma", "versiones.json", "versiones.json.firma",
                    "imagenes", *extra], check=False)
    if subprocess.run(["git", "-C", d, "diff", "--cached", "--quiet"]).returncode == 0:
        print("No hay cambios que subir.")
        return
    subprocess.run(["git", "-C", d, "commit", "-q", "-m", mensaje], check=True)
    subprocess.run(["git", "-C", d, "push", "-q"], check=True)
    print("Subido. Los editores lo veran al abrirse (o en menos de 3 horas si ya estan abiertos).")


def copiar_imagen(d, origen):
    ext = os.path.splitext(origen)[1].lower()
    if ext not in (".png", ".jpg", ".jpeg", ".gif", ".bmp"):
        sys.exit("La imagen tiene que ser .png, .jpg, .gif o .bmp")
    if os.path.getsize(origen) > 8_000_000:
        sys.exit("La imagen pasa de 8 MB: el editor no la descargaria")
    h = huella(origen)
    os.makedirs(os.path.join(d, "imagenes"), exist_ok=True)
    rel = f"imagenes/{h[:16]}{ext}"
    shutil.copyfile(origen, os.path.join(d, rel))
    return {"archivo": rel, "blake2b": h}


def fecha_valida(s):
    datetime.date.fromisoformat(s)
    return s


def main():
    ap = argparse.ArgumentParser(description="Publica un anuncio en Inicio > Noticias de Source 2D")
    ap.add_argument("titulo", nargs="?")
    ap.add_argument("texto", nargs="?", default="")
    ap.add_argument("--titulo", dest="titulo2")
    ap.add_argument("--texto", dest="texto2")
    ap.add_argument("--tipo", choices=TIPOS)
    ap.add_argument("--imagen")
    ap.add_argument("--enlace")
    ap.add_argument("--fijar", action="store_true")
    ap.add_argument("--soltar", action="store_true", help="quita la marca de fijada (con --editar)")
    ap.add_argument("--hasta", type=fecha_valida, help="AAAA-MM-DD: a partir del dia siguiente ya no sale")
    ap.add_argument("--importante", action="store_true", help="sale en una ventana una vez al abrir el editor")
    ap.add_argument("--para", action="append", help="solo para esta version (\"Beta 0.1\") o sistema (linux, windows); se puede repetir")
    ap.add_argument("--version", help="nombre de una version de versiones.json: la tarjeta trae el boton para bajarla")
    ap.add_argument("--sin-imagen", action="store_true")
    ap.add_argument("--sin-enlace", action="store_true")
    ap.add_argument("--editar", type=int, metavar="ID")
    ap.add_argument("--borrar", type=int, metavar="ID")
    ap.add_argument("--lista", action="store_true")
    ap.add_argument("--sin-subir", action="store_true")
    a = ap.parse_args()

    d = repo()
    nj = cargar(d, "noticias.json", "noticias", "noticias")
    ns = nj["noticias"]

    if a.lista:
        hoy = datetime.date.today().isoformat()
        for n in ns:
            marcas = [m for m, on in (("fijada", n.get("fijada")), ("importante", n.get("importante")),
                                      ("caducada", n.get("hasta") and n["hasta"] < hoy)) if on]
            print(f"{n['id']:>4}  {n.get('fecha', '')}  [{n.get('tipo', 'aviso')}] {n.get('titulo', '')}"
                  + (f"  ({', '.join(marcas)})" if marcas else ""))
        if not ns:
            print("Todavia no hay noticias.")
        return

    if a.borrar is not None:
        antes = len(ns)
        nj["noticias"] = [n for n in ns if n.get("id") != a.borrar]
        if len(nj["noticias"]) == antes:
            sys.exit(f"No hay ninguna noticia con id {a.borrar}")
        mensaje = f"Borrar noticia {a.borrar}"
    else:
        if a.editar is not None:
            n = next((x for x in ns if x.get("id") == a.editar), None)
            if not n:
                sys.exit(f"No hay ninguna noticia con id {a.editar}")
            mensaje = f"Editar noticia {a.editar}"
        else:
            titulo = a.titulo2 or a.titulo
            if not titulo:
                ap.error("falta el titulo")
            n = {"id": max([x.get("id", 0) for x in ns], default=0) + 1, "fecha": datetime.date.today().isoformat(),
                 "tipo": "aviso", "titulo": "", "texto": ""}
            ns.append(n)
            mensaje = f"Noticia: {titulo}"
        if a.titulo2 or (a.editar is None and a.titulo): n["titulo"] = a.titulo2 or a.titulo
        if a.texto2 is not None or (a.editar is None and a.texto): n["texto"] = a.texto2 if a.texto2 is not None else a.texto
        if a.tipo: n["tipo"] = a.tipo
        if a.imagen: n["imagen"] = copiar_imagen(d, a.imagen)
        if a.sin_imagen: n.pop("imagen", None)
        if a.enlace:
            if not a.enlace.startswith("https://"):
                sys.exit("El enlace tiene que empezar por https://")
            n["enlace"] = a.enlace
        if a.sin_enlace: n.pop("enlace", None)
        if a.fijar: n["fijada"] = True
        if a.soltar: n.pop("fijada", None)
        if a.hasta: n["hasta"] = a.hasta
        if a.importante: n["importante"] = True
        if a.para: n["para"] = a.para
        if a.version: n["version"] = a.version

    archivos = {"noticias.json": nj}
    vp = os.path.join(d, "versiones.json")
    if not os.path.exists(vp):   # el editor espera los dos archivos
        archivos["versiones.json"] = cargar(d, "versiones.json", "versiones", "versiones")
    guardar_y_firmar(d, archivos)
    if a.sin_subir:
        print("Firmado. No se ha subido (--sin-subir).")
    else:
        subir(d, mensaje)


if __name__ == "__main__":
    main()
