#!/usr/bin/env python3
import argparse, datetime, json, os, re, secrets, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from anunciar import AUTOR, RAIZ, guardar_y_firmar, huella
from source_play import ENVIOS, MAX_TAM, descargar_envio, editor, gh, insignia, slug

TIPOS = {
    "sprites": {"repo": "FloppaKFC/source2d-sprites", "env": "S2D_SPRITES_DIR", "titulo": "Sprites",
                "categorias": ("Personaje", "Sprites sueltos", "Tiles", "Autotiles", "Mixto")},
    "sonidos": {"repo": "FloppaKFC/source2d-sonidos", "env": "S2D_SONIDOS_DIR", "titulo": "Sonidos",
                "categorias": ("Música", "Efectos", "Mixto")},
}
LICENCIAS = ("Libre", "Libre dando crédito", "Solo juegos no comerciales")
ARCHIVOS = ("info.json", "paquete.s2dpaquete", "portada.png")

def repo(tipo):
    t = TIPOS[tipo]
    d = os.environ.get(t["env"]) or os.path.join(os.path.dirname(RAIZ), "source2d-" + tipo)
    if not os.path.isdir(d):
        sys.exit(f"No existe {d}. Clona alli el repositorio:\n  git clone https://github.com/{t['repo']}.git \"{d}\"")
    return d

def cargar(d, tipo):
    p = os.path.join(d, "catalogo.json")
    j = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
    for k in ("paquetes", "retirados", "destacados"):
        j.setdefault(k, [])
    j["tipo"] = "catalogo-" + tipo
    j["autor"] = AUTOR
    return j

def subir(d, mensaje, sin_subir):
    if sin_subir:
        print("Firmado. No se ha subido (--sin-subir).")
        return
    subprocess.run(["git", "-C", d, "add", "-A", "catalogo.json", "catalogo.json.firma"], check=True)
    if subprocess.run(["git", "-C", d, "diff", "--cached", "--quiet"]).returncode == 0:
        print("No hay cambios que subir.")
        return
    subprocess.run(["git", "-C", d, "commit", "-q", "-m", mensaje], check=True)
    subprocess.run(["git", "-C", d, "push", "-q"], check=True)
    print("Subido. Los editores lo verán en Source Store al abrirse (o en menos de una hora).")

def leer_paquete(carpeta, tipo):
    p = os.path.join(carpeta, "info.json")
    if not os.path.exists(p):
        sys.exit(f"Falta info.json en {carpeta}")
    try:
        info = json.load(open(p, encoding="utf-8"))
    except ValueError:
        sys.exit("info.json está dañado")
    if info.get("formato") != "source2d-envio-paquete" or info.get("tipo") != tipo:
        sys.exit(f"No es un paquete de {tipo} de Source Store")
    for f in os.listdir(carpeta):
        if f not in ARCHIVOS:
            sys.exit(f"Archivo que no debería estar en el paquete: {f}")
        if os.path.getsize(os.path.join(carpeta, f)) > MAX_TAM:
            sys.exit(f"{f} pasa de 95 MB")
    if not os.path.exists(os.path.join(carpeta, "paquete.s2dpaquete")):
        sys.exit("Falta paquete.s2dpaquete")
    if not str(info.get("nombre", "")).strip():
        sys.exit("El paquete no tiene nombre")
    if info.get("categoria") not in TIPOS[tipo]["categorias"]:
        info["categoria"] = TIPOS[tipo]["categorias"][-1]
    if info.get("licencia") not in LICENCIAS:
        info["licencia"] = LICENCIAS[1]
    return info

def comprobar(carpeta, tipo, extraer=None):
    """El editor abre el paquete y comprueba que solo trae lo que toca (en sonidos: solo creaciones de Source Sound).
    Devuelve el contenido {"sprites": n, ...}; con extraer, saca cada cosa a esa carpeta (.s2dsprite / .s2dsound)."""
    args = [editor(), "--paquete", os.path.join(carpeta, "paquete.s2dpaquete"), tipo] + ([extraer] if extraer else [])
    r = subprocess.run(args, capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        sys.exit((r.stderr.strip() or "El paquete no es válido") + ": recházalo o pide al creador que lo revise.")
    m = re.search(r"CONTENIDO (\{.*\})", r.stdout)
    return json.loads(m.group(1)) if m else {}

def mostrar(info):
    print(f"\n  {info['nombre']}  ·  por {info.get('autor', '?')}  ·  {info.get('categoria')}  ·  licencia: {info.get('licencia')}")
    if info.get("descripcion"):
        print("  " + info["descripcion"].replace("\n", "\n  "))
    print()

def publicar(carpeta, info, autor, tipo, contenido, sin_subir):
    d = repo(tipo)
    cj = cargar(d, tipo)
    pid = slug(f"{autor}-{info['nombre']}")
    previo = next((p for p in cj["paquetes"] if p["id"] == pid), None)
    version = previo["version"] + 1 if previo else 1
    tag = f"{pid}-v{version}"
    nombres = ["paquete.s2dpaquete"] + (["portada.png"] if os.path.exists(os.path.join(carpeta, "portada.png")) else [])
    if sin_subir:
        dest_rel = f"paquetes/{pid}/v{version}"
        os.makedirs(os.path.join(d, dest_rel), exist_ok=True)
        for n in nombres:
            shutil.copyfile(os.path.join(carpeta, n), os.path.join(d, dest_rel, n))
    else:
        subprocess.run([gh(), "release", "create", tag, "-R", TIPOS[tipo]["repo"], "--title", f"{info['nombre'].strip()[:80]} (versión {version})",
                        "--notes", f"Paquete de Source Store por @{autor}.", *[os.path.join(carpeta, n) for n in nombres]], check=True)

    def archivo(nombre):
        p = os.path.join(carpeta, nombre)
        a = {"archivo": nombre, "blake2b": huella(p), "tam": os.path.getsize(p)}
        if sin_subir:
            a["archivo"] = f"paquetes/{pid}/v{version}/{nombre}"
        else:
            a["url"] = f"https://github.com/{TIPOS[tipo]['repo']}/releases/download/{tag}/{nombre}"
        return a

    e = {"id": pid, "nombre": info["nombre"].strip()[:80], "autor": autor, "descripcion": str(info.get("descripcion", ""))[:600],
         "categoria": info["categoria"], "licencia": info["licencia"], "version": version, "fecha": datetime.date.today().isoformat(),
         "contador": previo["contador"] if previo else "p" + secrets.token_hex(6), "contenido": contenido,
         "paquete": archivo("paquete.s2dpaquete")}
    if "portada.png" in nombres:
        e["portada"] = archivo("portada.png")
    cj["paquetes"] = [p for p in cj["paquetes"] if p["id"] != pid] + [e]
    cj["retirados"] = [r for r in cj["retirados"] if r != pid]
    guardar_y_firmar(d, {"catalogo.json": cj})
    subir(d, f"Source Store ({tipo}): {e['nombre']} v{version}", sin_subir)
    if not sin_subir:
        out = subprocess.run([gh(), "release", "list", "-R", TIPOS[tipo]["repo"], "--limit", "1000", "--json", "tagName"], capture_output=True, text=True)
        for r in (json.loads(out.stdout) if out.returncode == 0 else []):
            if re.fullmatch(re.escape(pid) + r"-v\d+", r["tagName"]) and r["tagName"] != tag:
                subprocess.run([gh(), "release", "delete", r["tagName"], "-R", TIPOS[tipo]["repo"], "--yes", "--cleanup-tag"])
    print(f"Publicado: {e['nombre']} (id {pid}, versión {version})")
    return e

def main():
    ap = argparse.ArgumentParser(description="Source Store: revisar y publicar paquetes")
    ap.add_argument("tipo", choices=list(TIPOS))
    ap.add_argument("orden", choices=["envios", "probar", "probar-carpeta", "aprobar", "rechazar", "anadir", "lista", "retirar",
                                      "destacar", "no-destacar", "denuncias"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("motivo", nargs="?")
    ap.add_argument("--sin-subir", action="store_true")
    ap.add_argument("--autor")
    a = ap.parse_args()
    tipo, titulo = a.tipo, TIPOS[a.tipo]["titulo"]

    if a.orden == "envios":
        out = subprocess.run([gh(), "pr", "list", "-R", ENVIOS, "--json", "number,title,author,createdAt"], capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(out.stderr.strip())
        prs = [p for p in json.loads(out.stdout) if p["title"].startswith(titulo + ":")]
        if not prs:
            print("No hay envíos esperando revisión.")
        for p in prs:
            print(f"{p['number']:>4}  {p['createdAt'][:10]}  @{p['author']['login']}{insignia(p['author']['login'])}  {p['title']}")
        return
    if a.orden == "denuncias":
        out = subprocess.run([gh(), "issue", "list", "-R", ENVIOS, "--search", f"Denuncia paquete in:title", "--json", "number,title,author,createdAt,url,body"],
                             capture_output=True, text=True)
        if out.returncode != 0:
            sys.exit(out.stderr.strip())
        iss = [i for i in json.loads(out.stdout) if f"({tipo}," in i["title"]]
        if not iss:
            print("No hay denuncias abiertas.")
        for i in iss:
            print(f"{i['number']:>4}  {i['createdAt'][:10]}  {i['title']}\n      denuncia @{i['author']['login']}{insignia(i['author']['login'])}")
            for linea in (i.get("body") or "").split("---")[0].strip().splitlines():
                if linea.strip():
                    print("      " + linea.strip())
            print(f"      {i['url']}")
        return
    if a.orden == "lista":
        cj = cargar(repo(tipo), tipo)
        for p in cj["paquetes"]:
            print(f"  {p['id']:<40} v{p['version']}  {p['fecha']}  {p['categoria']:<16} {p['nombre']}" + ("   [destacado]" if p["id"] in cj["destacados"] else ""))
        if not cj["paquetes"]:
            print("Todavía no hay paquetes publicados.")
        return
    if not a.arg:
        ap.error("falta el número del envío, la carpeta o el id")
    if a.orden in ("probar", "probar-carpeta", "anadir"):
        if a.orden == "probar":
            carpeta, autor = descargar_envio(a.arg, tipo, ARCHIVOS)
        else:
            carpeta, autor = a.arg, None
        info = leer_paquete(carpeta, tipo)
        mostrar(info)
        if autor:
            print(f"  Enviado por @{autor}{insignia(autor)}\n")
        if a.orden == "anadir":
            contenido = comprobar(carpeta, tipo)
            publicar(carpeta, info, a.autor or info.get("autor") or "anónimo", tipo, contenido, a.sin_subir)
            return
        fuera = tempfile.mkdtemp(prefix="source-store-")
        comprobar(carpeta, tipo, fuera)
        print(f"\nEl contenido está en {fuera}: ábrelo con el editor (Source Sprite o Source Sound) para revisarlo.")
        return
    if a.orden == "aprobar":
        carpeta, autor = descargar_envio(a.arg, tipo, ARCHIVOS)
        info = leer_paquete(carpeta, tipo)
        contenido = comprobar(carpeta, tipo)
        e = publicar(carpeta, info, autor, tipo, contenido, a.sin_subir)
        shutil.rmtree(carpeta, ignore_errors=True)
        if not a.sin_subir:
            subprocess.run([gh(), "pr", "close", a.arg, "-R", ENVIOS, "--comment", f"Aprobado. ¡Gracias! {e['nombre']} ya está en Source Store (versión {e['version']})."], check=True)
        return
    if a.orden == "rechazar":
        if not a.motivo:
            ap.error("falta el motivo")
        subprocess.run([gh(), "pr", "close", a.arg, "-R", ENVIOS, "--comment", f"Rechazado: {a.motivo}"], check=True)
        print("Rechazado. El creador verá el motivo en el editor.")
        return
    d = repo(tipo)
    cj = cargar(d, tipo)
    if not any(p["id"] == a.arg for p in cj["paquetes"]):
        sys.exit(f"No hay ningún paquete con id {a.arg} (mira la lista)")
    if a.orden in ("destacar", "no-destacar"):
        cj["destacados"] = [x for x in cj["destacados"] if x != a.arg] + ([a.arg] if a.orden == "destacar" else [])
        msg = f"{a.arg} {'está en Destacados' if a.orden == 'destacar' else 'ya no está en Destacados'}."
    else:
        cj["paquetes"] = [p for p in cj["paquetes"] if p["id"] != a.arg]
        cj["destacados"] = [x for x in cj["destacados"] if x != a.arg]
        if a.arg not in cj["retirados"]:
            cj["retirados"].append(a.arg)
        if re.fullmatch(r"[a-z0-9-]+", a.arg):
            shutil.rmtree(os.path.join(d, "paquetes", a.arg), ignore_errors=True)
        msg = f"Retirado {a.arg}: desaparece de Source Store y se borra de los equipos que lo tenían."
    guardar_y_firmar(d, {"catalogo.json": cj})
    subir(d, f"Source Store ({tipo}): {a.orden} {a.arg}", a.sin_subir)
    print(msg)

if __name__ == "__main__":
    main()
