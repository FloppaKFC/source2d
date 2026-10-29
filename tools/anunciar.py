#!/usr/bin/env python3
import argparse, datetime, hashlib, json, os, re, secrets, shutil, subprocess, sys, urllib.request

AUTOR = "@floppa-1550"
TIPOS = ["aviso", "evento", "actualizacion", "consejo"]
CONTADOR = os.environ.get("S2D_CONTADOR_URL", "https://abacus.jasoncameron.dev/")
ESPACIO = "source2d-floppa"
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
    h = hashlib.blake2b(digest_size=32)
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
    rutas = [r for r in ("noticias.json", "noticias.json.firma", "versiones.json", "versiones.json.firma", "imagenes", *extra)
             if os.path.exists(os.path.join(d, r))]
    subprocess.run(["git", "-C", d, "add", *rutas], check=True)
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

def duracion(t):
    """segundos hasta el cierre (o fecha absoluta) -> instante unix; None = sin limite"""
    t = t.strip().lower()
    if not t:
        return None
    m = re.fullmatch(r"(\d+)\s*(m|min|minutos?|h|horas?|d|d[ií]as?|s|sem|semanas?)", t)
    if m:
        seg = {"m": 60, "h": 3600, "d": 86400, "s": 7 * 86400}[m.group(2)[0]]
        return int(datetime.datetime.now().timestamp()) + int(m.group(1)) * seg
    for f in ("%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            d = datetime.datetime.strptime(t, f)
            if f == "%Y-%m-%d":
                d += datetime.timedelta(days=1)
            return int(d.timestamp())
        except ValueError:
            pass
    return False

def sacar_encuesta(texto, id_):
    """Busca {encuesta: "P", "A", "B", "3d"} en el texto. Devuelve (texto con {encuesta} en su sitio, encuesta) o (texto, None)."""
    a = texto.find("{encuesta")
    if a < 0:
        return texto, None
    b = a
    dentro = False
    while b < len(texto):
        c = texto[b]
        if c == '"':
            dentro = not dentro
        elif c == "}" and not dentro:
            break
        b += 1
    if b >= len(texto):
        sys.exit("A la encuesta le falta la llave } del final")
    cuerpo = texto[a + len("{encuesta"):b].lstrip()
    if cuerpo.startswith(":"):
        cuerpo = cuerpo[1:]
    if '"' in cuerpo:
        partes = re.findall(r'"([^"]*)"', cuerpo)
    else:
        partes = [x.strip() for x in cuerpo.split(",")]
    if not partes:
        sys.exit("La encuesta esta vacia")
    pregunta, resto = partes[0].strip(), partes[1:]
    cierra = None
    if resto:
        c = duracion(resto[-1])
        if c is not False:
            cierra = c
            resto = resto[:-1]
    opciones = [x.strip() for x in resto if x.strip()]
    if not pregunta:
        sys.exit("La encuesta necesita una pregunta")
    if not 2 <= len(opciones) <= 10:
        sys.exit("La encuesta necesita entre 2 y 10 opciones")
    if cierra is not None and cierra <= datetime.datetime.now().timestamp():
        sys.exit("La encuesta ya estaria cerrada: revisa el tiempo")
    if texto.find("{encuesta", b) >= 0:
        sys.exit("Solo cabe una encuesta por noticia")
    e = {"pregunta": pregunta, "opciones": opciones, "clave": f"n{id_}-{secrets.token_hex(4)}"}
    if cierra is not None:
        e["cierra"] = cierra
    return texto[:a] + "{encuesta}" + texto[b + 1:], e

def votos(e):
    r = []
    for i in range(len(e["opciones"])):
        try:
            with urllib.request.urlopen(f"{CONTADOR}get/{ESPACIO}/{e['clave']}-{i}", timeout=10) as f:
                r.append(int(json.load(f).get("value", 0)))
        except urllib.error.HTTPError as x:
            r.append(0 if x.code == 404 else None)
        except Exception:
            r.append(None)
    return r

def main():
    ap = argparse.ArgumentParser(description="Publica un anuncio en Inicio > Noticias de Source 2D")
    ap.add_argument("titulo", nargs="?")
    ap.add_argument("texto", nargs="?", default="")
    ap.add_argument("--titulo", dest="titulo2")
    ap.add_argument("--texto", dest="texto2")
    ap.add_argument("--tipo", choices=TIPOS)
    ap.add_argument("--imagen")
    ap.add_argument("--enlace")
    ap.add_argument("--inferior", help="texto debajo de la imagen (admite {lista:a, b, c})")
    ap.add_argument("--sin-inferior", action="store_true")
    ap.add_argument("--fijar", action="store_true")
    ap.add_argument("--soltar", action="store_true", help="quita la marca de fijada (con --editar)")
    ap.add_argument("--hasta", type=fecha_valida, help="AAAA-MM-DD: a partir del dia siguiente ya no sale")
    ap.add_argument("--importante", action="store_true", help="sale en una ventana una vez al abrir el editor")
    ap.add_argument("--normal", action="store_true", help="con --editar: ni importante ni fijada")
    ap.add_argument("--notificar", action="store_true", help="sale como notificacion arriba en los editores abiertos con internet")
    ap.add_argument("--sin-notificar", action="store_true", help="con --editar: ya no sale como notificacion")
    ap.add_argument("--para", action="append", help="solo para esta version (\"Beta 0.1\") o sistema (linux, windows); se puede repetir")
    ap.add_argument("--version", help="nombre de una version de versiones.json: la tarjeta trae el boton para bajarla")
    ap.add_argument("--sin-imagen", action="store_true")
    ap.add_argument("--sin-enlace", action="store_true")
    ap.add_argument("--editar", type=int, metavar="ID")
    ap.add_argument("--borrar", type=int, metavar="ID")
    ap.add_argument("--lista", action="store_true")
    ap.add_argument("--sin-encuesta", action="store_true", help="con --editar: quita la encuesta")
    ap.add_argument("--resultados", type=int, metavar="ID", help="votos de la encuesta de esa noticia")
    ap.add_argument("--sin-subir", action="store_true")
    a = ap.parse_args()

    d = repo()
    nj = cargar(d, "noticias.json", "noticias", "noticias")
    ns = nj["noticias"]

    if a.lista:
        hoy = datetime.date.today().isoformat()
        for n in ns:
            marcas = [m for m, on in (("fijada", n.get("fijada")), ("importante", n.get("importante")), ("notificar", n.get("notificar")),
                                      ("encuesta", n.get("encuesta")),
                                      ("caducada", n.get("hasta") and n["hasta"] < hoy)) if on]
            print(f"{n['id']:>4}  {n.get('fecha', '')}  [{n.get('tipo', 'aviso')}] {n.get('titulo', '')}"
                  + (f"  ({', '.join(marcas)})" if marcas else ""))
        if not ns:
            print("Todavia no hay noticias.")
        return

    if a.resultados is not None:
        n = next((x for x in ns if x.get("id") == a.resultados), None)
        if not n or not n.get("encuesta"):
            sys.exit(f"La noticia {a.resultados} no tiene encuesta")
        e = n["encuesta"]
        print(e["pregunta"])
        if e.get("cierra"):
            fin = datetime.datetime.fromtimestamp(e["cierra"])
            print(("Terminó el " if fin < datetime.datetime.now() else "Termina el ") + fin.strftime("%Y-%m-%d %H:%M"))
        else:
            print("Sin límite de tiempo")
        v = votos(e)
        total = sum(x for x in v if x)
        for op, x in zip(e["opciones"], v):
            if x is None:
                print(f"  {op}: (no se pudo leer)")
            else:
                print(f"  {op}: {x} voto{'s' if x != 1 else ''}" + (f"  ({100 * x // total} %)" if total else ""))
        print(f"Total: {total}")
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
        if a.texto2 is not None or (a.editar is None and a.texto):
            texto, e = sacar_encuesta(a.texto2 if a.texto2 is not None else a.texto, n["id"])
            n["texto"] = texto
            if e:
                n["encuesta"] = e
            elif "{encuesta}" not in texto and n.get("encuesta"):
                n["texto"] = texto + " {encuesta}"
        if a.sin_encuesta:
            n.pop("encuesta", None)
            n["texto"] = n.get("texto", "").replace("{encuesta}", "").strip()
        if a.tipo: n["tipo"] = a.tipo
        if a.imagen: n["imagen"] = copiar_imagen(d, a.imagen)
        if a.sin_imagen: n.pop("imagen", None)
        if a.inferior is not None: n["inferior"] = a.inferior
        if a.sin_inferior: n.pop("inferior", None)
        if a.enlace:
            if not a.enlace.startswith("https://"):
                sys.exit("El enlace tiene que empezar por https://")
            n["enlace"] = a.enlace
        if a.sin_enlace: n.pop("enlace", None)
        if a.normal: n.pop("importante", None); n.pop("fijada", None)
        if a.fijar: n["fijada"] = True
        if a.soltar: n.pop("fijada", None)
        if a.hasta: n["hasta"] = a.hasta
        if a.importante: n["importante"] = True
        if a.notificar: n["notificar"] = True
        if a.sin_notificar: n.pop("notificar", None)
        if a.para: n["para"] = a.para
        if a.version: n["version"] = a.version

    archivos = {"noticias.json": nj}
    vp = os.path.join(d, "versiones.json")
    if not os.path.exists(vp):
        archivos["versiones.json"] = cargar(d, "versiones.json", "versiones", "versiones")
    guardar_y_firmar(d, archivos)
    if a.sin_subir:
        print("Firmado. No se ha subido (--sin-subir).")
    else:
        subir(d, mensaje)

if __name__ == "__main__":
    main()
