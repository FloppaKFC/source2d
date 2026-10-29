#!/usr/bin/env python3
# Source 2D - Source Play: revisar envios, aprobarlos o rechazarlos, y llevar el catalogo firmado.
# Lo usa Controladores/Source-Play.sh (el menu); tambien se puede llamar a mano:
#   source_play.py envios                     envios esperando revision (pull requests de FloppaKFC/source2d-envios)
#   source_play.py probar N                   descarga el envio N, lo comprueba y lo abre en el jugador
#   source_play.py aprobar N                  lo publica en Source Play y cierra el envio con "Aprobado"
#   source_play.py rechazar N "motivo"        cierra el envio con "Rechazado: motivo" (el creador lo ve en el editor)
#   source_play.py anadir CARPETA             publica un paquete guardado con "Guardar en una carpeta" (tus juegos)
#   source_play.py probar-carpeta CARPETA     lo mismo que probar, con una carpeta
#   source_play.py lista                      juegos publicados
#   source_play.py retirar ID                 lo quita de Source Play y de los equipos que lo descargaron
#   source_play.py denuncias                  incidencias "Denuncia:" abiertas en source2d-envios
#   source_play.py destacar ID / no-destacar ID   lo pone (o lo quita) en Destacados, arriba en Source Play
# El catalogo vive en una copia local de FloppaKFC/source2d-juegos (por defecto ../source2d-juegos, o
# S2D_JUEGOS_DIR). Se firma con build/firma (pide la contrasena) y se sube con git. --sin-subir: firma pero no sube.
import argparse, datetime, json, os, re, secrets, shutil, subprocess, sys, tempfile, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from anunciar import AUTOR, RAIZ, guardar_y_firmar, huella  # noqa: E402

ENVIOS = "FloppaKFC/source2d-envios"
MAX_TAM = 95_000_000            # GitHub no deja archivos de mas de 100 MB en un repositorio
ARCHIVOS = ("info.json", "portada.png", "juego.source2d", "juego.s2dplay", "original.s2dcerrado")
CATEGORIAS = ("Plataformas", "Aventura", "Acción", "Puzles", "Ritmo", "Carreras", "Deportes", "Arcade", "Terror", "Otros")


def gh():
    for p in (os.environ.get("S2D_GH"), os.path.join(RAIZ, "build", "toolchain", "gh", "gh"), shutil.which("gh")):
        if p and os.path.exists(p):
            return p
    sys.exit("No encuentro gh (GitHub CLI). Instálalo o déjalo en build/toolchain/gh/gh")


def editor():
    for p in (os.environ.get("S2D_EDITOR"), os.path.join(RAIZ, "build", "source2d"), os.path.join(RAIZ, "Release", "Linux", "Source2D")):
        if p and os.path.exists(p):
            return p
    sys.exit("No encuentro el editor: compila con make (build/source2d)")


def repo():
    d = os.environ.get("S2D_JUEGOS_DIR") or os.path.join(os.path.dirname(RAIZ), "source2d-juegos")
    if not os.path.isdir(d):
        sys.exit(f"No existe {d}. Clona alli el repositorio de juegos:\n"
                 f"  git clone https://github.com/FloppaKFC/source2d-juegos.git \"{d}\"")
    return d


def cargar_catalogo(d):
    p = os.path.join(d, "catalogo.json")
    j = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
    j.setdefault("juegos", [])
    j.setdefault("retirados", [])
    j.setdefault("destacados", [])
    j["tipo"] = "catalogo"
    j["autor"] = AUTOR
    return j


def insignia(login):
    """[creador], [admin], [verificada] o "" segun cuentas.json (Controladores/Source-Cuentas.sh)"""
    p = os.path.join(os.environ.get("S2D_JUEGOS_DIR") or os.path.join(os.path.dirname(RAIZ), "source2d-juegos"), "cuentas.json")
    try:
        c = json.load(open(p, encoding="utf-8"))
    except (OSError, ValueError):
        return ""
    l = login.lower()
    for k, nombre in (("creadores", "creador"), ("admins", "admin"), ("verificados", "verificada")):
        if l in (x.lower() for x in c.get(k, [])):
            return f" [{nombre}]"
    return " [cuenta normal]"


def slug(s):
    s = s.lower()
    for a, b in zip("áéíóúñü", "aeiounu"):
        s = s.replace(a, b)
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")[:40].strip("-")
    return s or "juego"


def subir(d, mensaje, sin_subir):
    if sin_subir:
        print("Firmado. No se ha subido (--sin-subir).")
        return
    rutas = [r for r in ("catalogo.json", "catalogo.json.firma", "juegos") if os.path.exists(os.path.join(d, r))]
    subprocess.run(["git", "-C", d, "add", "-A", *rutas], check=True)
    if subprocess.run(["git", "-C", d, "diff", "--cached", "--quiet"]).returncode == 0:
        print("No hay cambios que subir.")
        return
    subprocess.run(["git", "-C", d, "commit", "-q", "-m", mensaje], check=True)
    subprocess.run(["git", "-C", d, "push", "-q"], check=True)
    print("Subido. Los editores lo verán en Source Play al abrirse (o en menos de una hora).")


# ---------- paquetes (lo que manda el editor) ----------
def leer_paquete(carpeta):
    """Comprueba un paquete y devuelve su info. Sale con un mensaje claro si algo no cuadra."""
    p = os.path.join(carpeta, "info.json")
    if not os.path.exists(p):
        sys.exit(f"Falta info.json en {carpeta}")
    try:
        info = json.load(open(p, encoding="utf-8"))
    except ValueError:
        sys.exit("info.json está dañado")
    if info.get("formato") != "source2d-envio":
        sys.exit("No es un paquete de Source Play (info.json sin formato source2d-envio)")
    for f in os.listdir(carpeta):
        if f not in ARCHIVOS:
            sys.exit(f"Archivo que no debería estar en el paquete: {f}")
        if os.path.getsize(os.path.join(carpeta, f)) > MAX_TAM:
            sys.exit(f"{f} pasa de 95 MB")
    abierto = info.get("abierto", True) is not False
    juego = "juego.source2d" if abierto else "juego.s2dplay"
    if not os.path.exists(os.path.join(carpeta, juego)):
        sys.exit(f"Falta {juego}")
    if not abierto:
        if not re.fullmatch(r"[0-9a-f]{64}", info.get("llave", "")):
            sys.exit("Juego cerrado sin llave válida en info.json")
        if not os.path.exists(os.path.join(carpeta, "original.s2dcerrado")):
            sys.exit("Juego cerrado sin original.s2dcerrado")
    if not str(info.get("nombre", "")).strip():
        sys.exit("El juego no tiene nombre")
    if info.get("categoria") not in CATEGORIAS:
        info["categoria"] = "Otros"
    info["_juego"] = juego
    info["_abierto"] = abierto
    return info


def comprobar(carpeta, info, jugar):
    args = [editor(), "--jugar" if jugar else "--comprobar", os.path.join(carpeta, info["_juego"])]
    if not info["_abierto"]:
        args += ["--llave", info["llave"]]
    r = subprocess.run(args)
    if r.returncode != 0 and not jugar:
        sys.exit("El juego no se abre: recházalo o pide al creador que lo revise.")


def mostrar(info):
    print(f"\n  {info['nombre']}  ·  por {info.get('autor', '?')}  ·  {info.get('categoria')}  ·  "
          f"{'abierto (se puede ver por dentro)' if info['_abierto'] else 'cerrado (con clave)'}  ·  motor {info.get('motor', '?')}")
    if info.get("descripcion"):
        print("  " + info["descripcion"].replace("\n", "\n  "))
    print()


def descargar_envio(n, carpeta="envios", archivos=ARCHIVOS):
    """Baja los archivos del pull request N a una carpeta temporal. Devuelve (carpeta, autor)."""
    out = subprocess.run([gh(), "pr", "view", str(n), "-R", ENVIOS, "--json",
                          "author,headRefName,headRepositoryOwner,headRepository,files,state"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"No se pudo leer el envío {n}: {out.stderr.strip()}")
    pr = json.loads(out.stdout)
    if pr["state"] != "OPEN":
        sys.exit(f"El envío {n} ya está cerrado")
    autor = pr["author"]["login"]
    dueno, rama, nombre_repo = pr["headRepositoryOwner"]["login"], pr["headRefName"], pr["headRepository"]["name"]
    rutas = [f["path"] for f in pr["files"]]
    carpetas = {os.path.dirname(r) for r in rutas}
    if len(carpetas) != 1 or not re.fullmatch(re.escape(carpeta) + r"/[A-Za-z0-9-]+/[a-z0-9-]+", next(iter(carpetas))):
        sys.exit(f"El envío toca archivos fuera de su carpeta {carpeta}/<usuario>/<nombre>/: recházalo")
    if next(iter(carpetas)).split("/")[1].lower() != autor.lower():
        sys.exit("La carpeta del envío no es la de su autor: recházalo")
    tmp = tempfile.mkdtemp(prefix="source-play-")
    for r in rutas:
        nm = os.path.basename(r)
        if nm not in archivos:
            sys.exit(f"Archivo raro en el envío: {nm}")
        url = f"https://raw.githubusercontent.com/{dueno}/{nombre_repo}/{rama}/{r}"
        with urllib.request.urlopen(url, timeout=120) as f, open(os.path.join(tmp, nm), "wb") as o:
            o.write(f.read(MAX_TAM + 1))
    return tmp, autor


# ---------- catalogo ----------
JUEGOS = "FloppaKFC/source2d-juegos"
RELEASES = f"https://github.com/{JUEGOS}/releases/download/"


def releases_de(gid):
    out = subprocess.run([gh(), "release", "list", "-R", JUEGOS, "--limit", "1000", "--json", "tagName"], capture_output=True, text=True)
    if out.returncode != 0:
        return []
    return [r["tagName"] for r in json.loads(out.stdout) if re.fullmatch(re.escape(gid) + r"-v\d+", r["tagName"])]


def publicar(carpeta, info, autor, sin_subir):
    """Los archivos van como una Release de source2d-juegos (etiqueta <id>-v<version>): gratis, hasta 2 GB cada uno y
    sin contar para el tamano del repositorio. Con --sin-subir (pruebas) se quedan en la carpeta del repositorio."""
    d = repo()
    cj = cargar_catalogo(d)
    gid = slug(f"{autor}-{info['nombre']}")
    previo = next((j for j in cj["juegos"] if j["id"] == gid), None)
    version = previo["version"] + 1 if previo else 1
    tag = f"{gid}-v{version}"
    nombres = [info["_juego"]] + [n for n in ("portada.png", "original.s2dcerrado") if os.path.exists(os.path.join(carpeta, n))]
    if sin_subir:
        dest_rel = f"juegos/{gid}/v{version}"
        os.makedirs(os.path.join(d, dest_rel), exist_ok=True)
        for n in nombres:
            shutil.copyfile(os.path.join(carpeta, n), os.path.join(d, dest_rel, n))
    else:
        subprocess.run([gh(), "release", "create", tag, "-R", JUEGOS, "--title", f"{info['nombre'].strip()[:80]} (versión {version})",
                        "--notes", f"Juego de Source Play por @{autor}. Lo descarga el editor de Source 2D.",
                        *[os.path.join(carpeta, n) for n in nombres]], check=True)

    def archivo(nombre):
        p = os.path.join(carpeta, nombre)
        a = {"archivo": nombre, "blake2b": huella(p), "tam": os.path.getsize(p)}
        if sin_subir:
            a["archivo"] = f"juegos/{gid}/v{version}/{nombre}"
        else:
            a["url"] = RELEASES + tag + "/" + nombre
        return a

    e = {"id": gid, "nombre": info["nombre"].strip()[:80], "autor": autor, "descripcion": str(info.get("descripcion", ""))[:600],
         "categoria": info["categoria"], "version": version, "fecha": datetime.date.today().isoformat(),
         "motor": info.get("motor", ""), "abierto": info["_abierto"],
         "contador": previo["contador"] if previo else "j" + secrets.token_hex(6),
         "juego": archivo(info["_juego"])}
    if "portada.png" in nombres:
        e["portada"] = archivo("portada.png")
    if not info["_abierto"]:
        e["llave"] = info["llave"]
        e["fuente"] = archivo("original.s2dcerrado")
    cj["juegos"] = [j for j in cj["juegos"] if j["id"] != gid] + [e]
    cj["retirados"] = [r for r in cj["retirados"] if r != gid]
    guardar_y_firmar(d, {"catalogo.json": cj})
    subir(d, f"Source Play: {e['nombre']} v{version}", sin_subir)
    # las versiones viejas se borran (los editores ya ven la nueva como "Actualizar")
    if sin_subir:
        for v in os.listdir(os.path.join(d, "juegos", gid)):
            if v != f"v{version}":
                shutil.rmtree(os.path.join(d, "juegos", gid, v), ignore_errors=True)
    else:
        for t in releases_de(gid):
            if t != tag:
                subprocess.run([gh(), "release", "delete", t, "-R", JUEGOS, "--yes", "--cleanup-tag"])
    print(f"Publicado: {e['nombre']} (id {gid}, versión {version})")
    return e


def main():
    ap = argparse.ArgumentParser(description="Source Play: revisar y publicar juegos")
    ap.add_argument("orden", choices=["envios", "probar", "probar-carpeta", "aprobar", "rechazar", "anadir", "lista", "retirar", "denuncias", "destacar", "no-destacar"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("motivo", nargs="?")
    ap.add_argument("--sin-subir", action="store_true")
    ap.add_argument("--autor", help="con anadir: autor que sale en la tienda (por defecto el de info.json)")
    a = ap.parse_args()

    if a.orden == "envios":
        out = subprocess.run([gh(), "pr", "list", "-R", ENVIOS, "--json", "number,title,author,createdAt"], capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(out.stderr.strip())
        prs = [p for p in json.loads(out.stdout) if p["title"].startswith("Envío:")]   # los de sprites y sonidos van aparte
        if not prs:
            print("No hay envíos esperando revisión.")
        for p in prs:
            print(f"{p['number']:>4}  {p['createdAt'][:10]}  @{p['author']['login']}{insignia(p['author']['login'])}  {p['title']}")
        return
    if a.orden == "denuncias":
        out = subprocess.run([gh(), "issue", "list", "-R", ENVIOS, "--search", "Denuncia in:title", "--json", "number,title,author,createdAt,url,body"],
                             capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(out.stderr.strip())
        iss = json.loads(out.stdout)
        if not iss:
            print("No hay denuncias abiertas.")
        for i in iss:
            # quien denuncia y, si es una cuenta, que insignia tiene la denunciada
            m = re.search(r"cuenta @([A-Za-z0-9-]+)", i["title"])
            print(f"{i['number']:>4}  {i['createdAt'][:10]}  {i['title']}" + (insignia(m.group(1)) if m else ""))
            print(f"      denuncia @{i['author']['login']}{insignia(i['author']['login'])}")
            for linea in (i.get("body") or "").split("---")[0].strip().splitlines():
                if linea.strip():
                    print("      " + linea.strip())
            print(f"      {i['url']}")
        return
    if a.orden == "lista":
        cj = cargar_catalogo(repo())
        for j in cj["juegos"]:
            print(f"  {j['id']:<40} v{j['version']}  {j['fecha']}  {'abierto' if j['abierto'] else 'cerrado'}  {j['nombre']}"
                  + ("   [destacado]" if j["id"] in cj["destacados"] else ""))
        if not cj["juegos"]:
            print("Todavía no hay juegos publicados.")
        if cj["retirados"]:
            print("Retirados: " + ", ".join(cj["retirados"]))
        return
    if not a.arg:
        ap.error("falta el número del envío, la carpeta o el id")

    if a.orden in ("probar", "probar-carpeta", "anadir"):
        if a.orden == "probar":
            carpeta, autor = descargar_envio(a.arg)
        else:
            carpeta, autor = a.arg, None
        info = leer_paquete(carpeta)
        mostrar(info)
        if autor:
            print(f"  Enviado por @{autor}{insignia(autor)}\n")
        comprobar(carpeta, info, False)
        if a.orden == "anadir":
            publicar(carpeta, info, a.autor or info.get("autor") or "anónimo", a.sin_subir)
            return
        print("Abriendo el juego... (ciérralo para volver)")
        comprobar(carpeta, info, True)
        if a.orden == "probar":
            shutil.rmtree(carpeta, ignore_errors=True)
        return
    if a.orden == "aprobar":
        carpeta, autor = descargar_envio(a.arg)
        info = leer_paquete(carpeta)
        comprobar(carpeta, info, False)
        e = publicar(carpeta, info, autor, a.sin_subir)
        shutil.rmtree(carpeta, ignore_errors=True)
        if not a.sin_subir:
            msg = f"Aprobado. ¡Gracias! {e['nombre']} ya está en Source Play (versión {e['version']})."
            subprocess.run([gh(), "pr", "close", a.arg, "-R", ENVIOS, "--comment", msg], check=True)
        return
    if a.orden == "rechazar":
        if not a.motivo:
            ap.error("falta el motivo")
        subprocess.run([gh(), "pr", "close", a.arg, "-R", ENVIOS, "--comment", f"Rechazado: {a.motivo}"], check=True)
        print("Rechazado. El creador verá el motivo en el editor.")
        return
    if a.orden in ("destacar", "no-destacar"):
        d = repo()
        cj = cargar_catalogo(d)
        if not any(j["id"] == a.arg for j in cj["juegos"]):
            sys.exit(f"No hay ningún juego con id {a.arg} (mira la lista)")
        cj["destacados"] = [x for x in cj["destacados"] if x != a.arg] + ([a.arg] if a.orden == "destacar" else [])
        guardar_y_firmar(d, {"catalogo.json": cj})
        subir(d, f"Source Play: {'destacar' if a.orden == 'destacar' else 'quitar de destacados'} {a.arg}", a.sin_subir)
        print(f"{a.arg} {'está en Destacados' if a.orden == 'destacar' else 'ya no está en Destacados'}.")
        return
    if a.orden == "retirar":
        d = repo()
        cj = cargar_catalogo(d)
        if not any(j["id"] == a.arg for j in cj["juegos"]):
            sys.exit(f"No hay ningún juego con id {a.arg} (mira la lista)")
        cj["juegos"] = [j for j in cj["juegos"] if j["id"] != a.arg]
        cj["destacados"] = [x for x in cj["destacados"] if x != a.arg]
        if a.arg not in cj["retirados"]:
            cj["retirados"].append(a.arg)
        if re.fullmatch(r"[a-z0-9-]+", a.arg):
            shutil.rmtree(os.path.join(d, "juegos", a.arg), ignore_errors=True)
            if not a.sin_subir:
                for t in releases_de(a.arg):
                    subprocess.run([gh(), "release", "delete", t, "-R", JUEGOS, "--yes", "--cleanup-tag"])
        guardar_y_firmar(d, {"catalogo.json": cj})
        subir(d, f"Source Play: retirar {a.arg}", a.sin_subir)
        print(f"Retirado {a.arg}: desaparece de Source Play y se borra de los equipos que lo tenían.")


if __name__ == "__main__":
    main()
