#!/usr/bin/env python3
USO = """  admin.py envios | probar ENVIO | aprobar ENVIO | rechazar ENVIO "motivo"      (ENVIO = login/id)
  admin.py publicar-carpeta juegos|sprites|sonidos CARPETA
  admin.py lista TIPO | retirar TIPO ID | destacar TIPO ID | no-destacar TIPO ID
  admin.py denuncias | cerrar-denuncia ID | borrar-comentario TIPO ID COMENTARIO
  admin.py cuentas [texto] | insignia LOGIN TIPO [--quitar] | contribucion LOGIN "idea"
  admin.py banear LOGIN DIAS|siempre "motivo" | desbanear LOGIN
  admin.py solicitudes | aceptar-solicitud LOGIN/ID MB "respuesta" | rechazar-solicitud LOGIN/ID "motivo"
  admin.py noticias ARGUMENTOS-DE-anunciar.py"""

import argparse, datetime, json, os, re, secrets, shutil, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import r2
from anunciar import AUTOR, guardar_y_firmar, huella
from source_play import editor, leer_paquete as leer_juego, slug
from source_store import leer_paquete as leer_store, comprobar as comprobar_store

TIPOS = ("juegos", "sprites", "sonidos")
INSIGNIAS = {"verificada": "verificados", "admin": "admins", "creador": "creadores", "contribuidor": "contribuidores"}


def ahora():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


def leer_json(ruta, privado=False, defecto=None):
    b = r2.leer(ruta, privado)
    if b is None:
        return defecto
    try:
        return json.loads(b.decode("utf-8"))
    except ValueError:
        return defecto


def guardar_json(ruta, valor, privado=False):
    r2.subir(ruta, json.dumps(valor, ensure_ascii=False).encode("utf-8"), privado, "application/json; charset=utf-8")


def notificar(login, tipo, titulo, texto="", enlace=""):
    inv = str(9999999999999 - int(datetime.datetime.now().timestamp() * 1000)).zfill(13)
    nid = f"{inv}-{secrets.token_hex(4)}"
    guardar_json(f"notificaciones/{login.lower()}/{nid}.json",
                 {"id": nid, "tipo": tipo, "titulo": titulo[:140], "texto": texto[:2000], "enlace": enlace, "fecha": ahora()}, True)


def firmado(nombre_r2, contenido, base_r2=""):
    d = tempfile.mkdtemp(prefix="s2d-firma-")
    try:
        guardar_y_firmar(d, {nombre_r2: contenido})
        for f in (nombre_r2, nombre_r2 + ".firma"):
            r2.subir_archivo(base_r2 + f, os.path.join(d, f), False, "application/json; charset=utf-8" if f.endswith(".json") else "text/plain")
    finally:
        shutil.rmtree(d, ignore_errors=True)


def catalogo(tipo):
    c = leer_json(f"{tipo}/catalogo.json", defecto={}) or {}
    lista = "juegos" if tipo == "juegos" else "paquetes"
    for k in (lista, "retirados", "destacados"):
        c.setdefault(k, [])
    c["tipo"] = "catalogo" if tipo == "juegos" else "catalogo-" + tipo
    c["autor"] = AUTOR
    return c, lista


def guardar_catalogo(tipo, c):
    firmado("catalogo.json", c, f"{tipo}/")


def cuentas():
    c = leer_json("cuentas.json", defecto={}) or {}
    for k in list(INSIGNIAS.values()):
        c.setdefault(k, [])
    c.setdefault("contribuciones", {})
    c["tipo"] = "cuentas"
    c["autor"] = AUTOR
    return c


def envio(ref):
    if not re.fullmatch(r"[A-Za-z0-9-]{1,39}/[a-z0-9]{6,16}-[a-f0-9]{8}", ref or ""):
        sys.exit("Envío no válido: escribe login/id (míralo en la lista de envíos)")
    login, eid = ref.split("/")
    base = f"envios/{login.lower()}/{eid}/"
    e = leer_json(base + "estado.json", True)
    if not e:
        sys.exit("No existe ese envío")
    return e, base


def bajar_envio(e, base):
    d = tempfile.mkdtemp(prefix="s2d-envio-")
    for a in e.get("archivos", {}):
        b = r2.leer(base + a, True)
        if b is not None:
            open(os.path.join(d, a), "wb").write(b)
    return d


def caja(args):
    if not shutil.which("bwrap"):
        sys.exit("Para probar juegos de otras personas sin riesgo hace falta bubblewrap: sudo pacman -S bubblewrap")
    run = os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    home = os.path.expanduser("~")
    b = ["bwrap", "--die-with-parent", "--new-session", "--unshare-all", "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
         "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64", "--symlink", "usr/bin", "/bin", "--proc", "/proc",
         "--dev", "/dev", "--tmpfs", "/tmp", "--tmpfs", home, "--setenv", "HOME", home, "--dir", run, "--setenv", "XDG_RUNTIME_DIR", run]
    if os.path.exists("/dev/dri"):
        b += ["--dev-bind", "/dev/dri", "/dev/dri"]
    for s in ("wayland-0", "wayland-1", "pulse", "pipewire-0"):
        if os.path.exists(os.path.join(run, s)):
            b += ["--ro-bind", os.path.join(run, s), os.path.join(run, s)]
    if os.path.isdir("/tmp/.X11-unix"):
        b += ["--ro-bind", "/tmp/.X11-unix", "/tmp/.X11-unix"]
    xa = os.environ.get("XAUTHORITY")
    if xa and os.path.exists(xa):
        b += ["--ro-bind", xa, "/tmp/.Xauthority", "--setenv", "XAUTHORITY", "/tmp/.Xauthority"]
    for a in args:
        if os.path.isabs(a) and os.path.exists(a):
            b += ["--ro-bind", a, a]
    return subprocess.run(b + args).returncode


def abrir(ruta):
    if shutil.which("xdg-open"):
        subprocess.Popen(["xdg-open", ruta], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def probar(ref):
    e, base = envio(ref)
    print(f"\n  {e['nombre']}  ·  {e['tipo']}  ·  por @{e['autor']}  ·  {e['estado']}\n")
    d = bajar_envio(e, base)
    if e["tipo"] == "juegos":
        info = leer_juego(d)
        print(f"  {info.get('descripcion', '')}\n  Categoría: {info['categoria']}  ·  {'abierto' if info['_abierto'] else 'cerrado'}")
        args = [os.path.abspath(editor()), "--comprobar", os.path.join(d, info["_juego"])] + ([] if info["_abierto"] else ["--llave", info["llave"]])
        if subprocess.run(args).returncode != 0:
            print("  El juego NO se abre: recházalo.")
        elif input("  ¿Jugarlo aislado (sin internet ni acceso a tus archivos)? [S/n]: ").strip().lower() in ("", "s", "si", "sí"):
            args[1] = "--jugar"
            caja(args)
    else:
        info = leer_store(d, e["tipo"])
        fuera = os.path.join(d, "contenido")
        comprobar_store(d, e["tipo"], fuera)
        print(f"  Licencia: {info.get('licencia')}  ·  Categoría: {info.get('categoria')}")
        vista = os.path.join(fuera, "vista previa")
        print(f"  Vista previa en {vista} (imágenes PNG de cada sprite y .wav de cada sonido)")
        abrir(vista)
        wavs = sorted(f for f in os.listdir(vista) if f.endswith(".wav")) if os.path.isdir(vista) else []
        rep = shutil.which("pw-play") or shutil.which("paplay") or shutil.which("aplay")
        if wavs and rep and input("  ¿Escuchar los sonidos aquí? [S/n]: ").strip().lower() in ("", "s", "si", "sí"):
            for w in wavs:
                print(f"  ♪ {w[:-4]}  (Ctrl+C para pasar al siguiente)")
                try:
                    subprocess.run([rep, os.path.join(vista, w)])
                except KeyboardInterrupt:
                    pass
    if os.path.exists(os.path.join(d, "portada.png")):
        abrir(os.path.join(d, "portada.png"))
    print(f"\n  Archivos del envío en {d}")


def publicar(tipo, d, autor, contador_previo=None):
    c, lista = catalogo(tipo)
    if tipo == "juegos":
        info = leer_juego(d)
        principal = info["_juego"]
    else:
        info = leer_store(d, tipo)
        principal = "paquete.s2dpaquete"
        contenido = comprobar_store(d, tipo)
    pid = slug(f"{autor}-{info['nombre']}")
    previo = next((x for x in c[lista] if x["id"] == pid), None)
    version = previo["version"] + 1 if previo else 1
    ruta = f"{pid}/v{version}"
    nombres = [principal] + [n for n in ("portada.png", "original.s2dcerrado") if os.path.exists(os.path.join(d, n))]
    for n in nombres:
        print(f"  subiendo {n}...")
        r2.subir_archivo(f"{tipo}/{ruta}/{n}", os.path.join(d, n))

    def archivo(n):
        p = os.path.join(d, n)
        return {"archivo": f"{ruta}/{n}", "blake2b": huella(p), "tam": os.path.getsize(p)}

    e = {"id": pid, "nombre": info["nombre"].strip()[:80], "autor": autor, "descripcion": str(info.get("descripcion", ""))[:600],
         "categoria": info.get("categoria"), "version": version, "fecha": datetime.date.today().isoformat(),
         "contador": previo["contador"] if previo else ("j" if tipo == "juegos" else "p") + secrets.token_hex(6)}
    if tipo == "juegos":
        e.update({"motor": info.get("motor", ""), "abierto": info["_abierto"], "juego": archivo(principal)})
        if not info["_abierto"]:
            e["llave"] = info["llave"]
            e["fuente"] = archivo("original.s2dcerrado")
    else:
        e.update({"licencia": info.get("licencia"), "contenido": contenido, "paquete": archivo(principal)})
    if os.path.exists(os.path.join(d, "portada.png")):
        e["portada"] = archivo("portada.png")
    c[lista] = [x for x in c[lista] if x["id"] != pid] + [e]
    c["retirados"] = [x for x in c["retirados"] if x != pid]
    guardar_catalogo(tipo, c)
    if previo:
        for k, _ in r2.listar(f"{tipo}/{pid}/")[0]:
            if not k.startswith(f"{tipo}/{ruta}/"):
                r2.borrar(k)
    print(f"Publicado: {e['nombre']} (id {pid}, versión {version})")
    return e


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "noticias":
        d = tempfile.mkdtemp(prefix="s2d-noticias-")
        claves, _ = r2.listar("novedades/")
        for k, _ in claves:
            p = os.path.join(d, k[len("novedades/"):])
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").write(r2.leer(k))

        def todos():
            return {os.path.relpath(os.path.join(r, f), d).replace(os.sep, "/"): os.path.join(r, f) for r, _, fs in os.walk(d) for f in fs}
        antes = {k: open(p, "rb").read() for k, p in todos().items()}
        r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "anunciar.py")] + sys.argv[2:] + ["--sin-subir"],
                           env=dict(os.environ, S2D_VERSIONES_DIR=d))
        if r.returncode == 0:
            for f, p in todos().items():
                if antes.get(f) != open(p, "rb").read():
                    tipo = "application/json; charset=utf-8" if f.endswith(".json") else "image/png" if f.endswith(".png") else "image/jpeg" if f.endswith((".jpg", ".jpeg")) else "application/octet-stream"
                    r2.subir_archivo("novedades/" + f, p, False, tipo)
                    print(f"  subido novedades/{f}")
        shutil.rmtree(d, ignore_errors=True)
        sys.exit(r.returncode)
    ap = argparse.ArgumentParser(description="Administración de Source 2D")
    ap.add_argument("orden")
    ap.add_argument("args", nargs="*")
    ap.add_argument("--quitar", action="store_true")
    ap.add_argument("--autor")
    a = ap.parse_args()
    o, x = a.orden, a.args

    if o == "envios":
        filas = []
        for k, _ in r2.listar("envios/", True)[0]:
            if k.endswith("/estado.json"):
                e = leer_json(k, True)
                if e and e.get("estado") == "pendiente" and (not x or e.get("tipo") == x[0]):
                    filas.append((e["fecha"], f"{e['autor'].lower()}/{e['id']}", e))
        for f, ref, e in sorted(filas):
            mb = sum(e.get("archivos", {}).values()) / 1e6
            print(f"  {ref:<40} {e['tipo']:<8} {f[:10]}  {mb:6.1f} MB  {e['nombre']}")
        if not filas:
            print("No hay envíos esperando revisión.")
    elif o == "probar":
        probar(x[0])
    elif o == "aprobar":
        e, base = envio(x[0])
        d = bajar_envio(e, base)
        pub = publicar(e["tipo"], d, a.autor or e["autor"])
        e.update({"estado": "aprobado", "motivo": "", "fecha": ahora()})
        guardar_json(base + "estado.json", e, True)
        for k in e.get("archivos", {}):
            r2.borrar(base + k, True)
        que = "Tu juego" if e["tipo"] == "juegos" else "Tu paquete"
        notificar(e["autor"], "aprobado", f"¡{que} {e['nombre']} fue aceptado!", "Ya está publicado para todo el mundo.", f"{e['tipo']}/{pub['id']}")
        shutil.rmtree(d, ignore_errors=True)
    elif o == "rechazar":
        e, base = envio(x[0])
        motivo = " ".join(x[1:]).strip() or sys.exit("Falta el motivo")
        e.update({"estado": "rechazado", "motivo": motivo, "fecha": ahora()})
        guardar_json(base + "estado.json", e, True)
        for k in e.get("archivos", {}):
            r2.borrar(base + k, True)
        notificar(e["autor"], "rechazado", f"{'Tu juego' if e['tipo'] == 'juegos' else 'Tu paquete'} {e['nombre']} fue rechazado", "Motivo: " + motivo)
        print("Rechazado. Le llega la notificación con el motivo.")
    elif o == "publicar-carpeta":
        tipo, d = x[0], os.path.expanduser(x[1])
        info = json.load(open(os.path.join(d, "info.json"), encoding="utf-8"))
        publicar(tipo, d, a.autor or info.get("autor") or "FloppaKFC")
    elif o in ("lista", "retirar", "destacar", "no-destacar"):
        tipo = x[0]
        c, lista = catalogo(tipo)
        if o == "lista":
            for e in c[lista]:
                print(f"  {e['id']:<42} v{e['version']}  {e['fecha']}  {e.get('categoria', ''):<14} {e['nombre']}" + ("  [destacado]" if e["id"] in c["destacados"] else ""))
            if not c[lista]:
                print("Todavía no hay nada publicado.")
            return
        pid = x[1]
        if not any(e["id"] == pid for e in c[lista]):
            sys.exit(f"No hay nada con id {pid}")
        if o == "retirar":
            c[lista] = [e for e in c[lista] if e["id"] != pid]
            c["destacados"] = [i for i in c["destacados"] if i != pid]
            c["retirados"] = sorted(set(c["retirados"]) | {pid})
            for k, _ in r2.listar(f"{tipo}/{pid}/")[0]:
                r2.borrar(k)
        else:
            c["destacados"] = [i for i in c["destacados"] if i != pid] + ([pid] if o == "destacar" else [])
        guardar_catalogo(tipo, c)
        print("Hecho.")
    elif o == "denuncias":
        ds = [leer_json(k, True) for k, _ in r2.listar("denuncias/", True)[0]]
        for dn in sorted([d for d in ds if d], key=lambda d: d["fecha"]):
            print(f"\n  [{dn['id']}] {dn['fecha'][:10]}  {dn['titulo']}\n      denuncia @{dn['autor']}" + (f" · contra @{dn['objetivo']}" if dn.get("objetivo") else ""))
            for linea in dn.get("cuerpo", "").splitlines():
                if linea.strip():
                    print("      " + linea)
            for m in dn.get("mensajes", []):
                marca = ""
                if dn.get("tipo") == "mensaje" and m.get("llave") and dn.get("conversacion"):
                    guardado = leer_json(f"conversaciones/{dn['conversacion']}/m/{m['id']}.json", True)
                    r = subprocess.run([editor(), "--verificar-mensaje", (guardado or {}).get("cifrado", ""), m["llave"]], capture_output=True, text=True) if guardado else None
                    marca = " [COMPROBADO]" if r and r.returncode == 0 and r.stdout.rstrip("\n") == m["texto"] else " [NO SE PUEDE COMPROBAR]"
                print(f"      > {m['de']}: {m['texto']}{marca}")
        if not any(ds):
            print("No hay denuncias.")
    elif o == "cerrar-denuncia":
        r2.borrar(f"denuncias/{x[0]}.json", True)
        print("Denuncia cerrada.")
    elif o == "borrar-comentario":
        tipo, pid, cid = x
        c = leer_json(f"comentarios/{tipo}/{pid}.json", defecto={"comentarios": []})
        c["comentarios"] = [k for k in c.get("comentarios", []) if k.get("id") != cid]
        guardar_json(f"comentarios/{tipo}/{pid}.json", c)
        print("Comentario borrado.")
    elif o == "cuentas":
        c = cuentas()
        for k, _ in r2.listar("cuentas/")[0]:
            cu = leer_json(k) or {}
            login = cu.get("login", "")
            if x and x[0].lower() not in login.lower():
                continue
            ins = [n for n, lista in INSIGNIAS.items() if login.lower() in (l.lower() for l in c[lista])]
            ban = leer_json(f"baneos/{login.lower()}.json", True)
            print(f"  {login:<40} se unió {cu.get('unido', '')[:10]}" + (f"  [{', '.join(ins)}]" if ins else "") + ("  [BANEADA]" if ban else ""))
    elif o == "insignia":
        login, tipo = x[0], x[1]
        if tipo not in INSIGNIAS:
            sys.exit("Insignias: " + ", ".join(INSIGNIAS))
        c = cuentas()
        lista = [l for l in c[INSIGNIAS[tipo]] if l.lower() != login.lower()]
        c[INSIGNIAS[tipo]] = lista if a.quitar else lista + [login]
        firmado("cuentas.json", c)
        if not a.quitar:
            notificar(login, "aviso", f"¡Tienes una insignia nueva: {tipo}!", "Ya sale en tu perfil.")
        print("Hecho. Los editores la verán en menos de un minuto.")
    elif o == "contribucion":
        login, idea = x[0], " ".join(x[1:]).strip()
        c = cuentas()
        c["contribuciones"].setdefault(login.lower(), []).append(idea)
        if login.lower() not in (l.lower() for l in c["contribuidores"]):
            c["contribuidores"].append(login)
        firmado("cuentas.json", c)
        notificar(login, "aviso", "¡Gracias por tu idea! Tienes la insignia Contribuidor", idea)
        print("Contribución apuntada.")
    elif o == "banear":
        login, dias, motivo = x[0], x[1], " ".join(x[2:]).strip() or "No cumplir las normas"
        hasta = "" if dias == "siempre" else (datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=int(dias))).strftime("%Y-%m-%dT%H:%M:%S.000Z")
        guardar_json(f"baneos/{login.lower()}.json", {"hasta": hasta, "motivo": motivo, "fecha": ahora()}, True)
        notificar(login, "baneo", "Tu cuenta está baneada " + ("para siempre" if not hasta else f"hasta el {hasta[:10]}"), "Motivo: " + motivo)
        print("Baneada.")
    elif o == "desbanear":
        r2.borrar(f"baneos/{x[0].lower()}.json", True)
        notificar(x[0], "aviso", "Tu cuenta ya no está baneada")
        print("Desbaneada.")
    elif o == "solicitudes":
        n = 0
        for k, _ in r2.listar("solicitudes/", True)[0]:
            s = leer_json(k, True)
            if s and s.get("estado") == "pendiente":
                n += 1
                print(f"\n  {s['autor'].lower()}/{s['id']}  {'MÁS DE 100 MB' if s['tipo'] == 'grande' else 'NUBE'}  pide {s['mb']} MB  ·  {s['fecha'][:10]}"
                      + (f"  ·  proyecto: {s['proyecto']}" if s.get("proyecto") else "") + f"\n      {s['motivo']}")
        if not n:
            print("No hay solicitudes pendientes.")
    elif o in ("aceptar-solicitud", "rechazar-solicitud"):
        login, sid = x[0].split("/")
        k = f"solicitudes/{login.lower()}/{sid}.json"
        s = leer_json(k, True) or sys.exit("No existe esa solicitud")
        if o == "aceptar-solicitud":
            mb = int(x[1])
            p = leer_json(f"permisos/{login.lower()}.json", True, {}) or {}
            p["grande_mb" if s["tipo"] == "grande" else "nube_mb"] = mb
            guardar_json(f"permisos/{login.lower()}.json", p, True)
            s.update({"estado": "aceptada", "respuesta": " ".join(x[2:]).strip(), "mb": mb})
            notificar(login, "aprobado", f"Te dieron {mb} MB {'para tu proyecto grande' if s['tipo'] == 'grande' else 'en la nube'}", s["respuesta"])
        else:
            s.update({"estado": "rechazada", "respuesta": " ".join(x[1:]).strip()})
            notificar(login, "rechazado", "Tu solicitud de espacio fue rechazada", "Motivo: " + s["respuesta"])
        guardar_json(k, s, True)
        print("Hecho.")
    else:
        sys.exit("Orden desconocida. Uso:\n" + USO)


if __name__ == "__main__":
    main()
