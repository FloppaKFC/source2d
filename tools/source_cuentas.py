#!/usr/bin/env python3
# Source 2D - cuentas de Source Play: quien esta registrado y quien tiene insignia (verificada o admin).
# Las cuentas registradas son las que pusieron estrella a FloppaKFC/source2d-juegos (el editor la pone al entrar con
# GitHub); la fecha de la estrella es cuando se unieron. Las insignias van en cuentas.json, firmado con tu clave en la
# copia local del repositorio de juegos (../source2d-juegos o S2D_JUEGOS_DIR), y el editor las muestra junto al nombre.
#   source_cuentas.py lista [texto]        cuentas registradas (las mas nuevas primero), con su insignia
#   source_cuentas.py verificar LOGIN      insignia de cuenta verificada
#   source_cuentas.py admin LOGIN          insignia de admin
#   source_cuentas.py quitar LOGIN         sin insignia
#   source_cuentas.py insignias            solo las cuentas con insignia
# --sin-subir: firma pero no sube.
import argparse, json, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from anunciar import AUTOR, guardar_y_firmar  # noqa: E402
from source_play import gh, repo  # noqa: E402

JUEGOS = "FloppaKFC/source2d-juegos"


def cargar(d):
    p = os.path.join(d, "cuentas.json")
    j = json.load(open(p, encoding="utf-8")) if os.path.exists(p) else {}
    j.setdefault("verificados", [])
    j.setdefault("admins", [])
    j["tipo"] = "cuentas"
    j["autor"] = AUTOR
    return j


def insignia(c, login):
    l = login.lower()
    if l in (x.lower() for x in c["admins"]):
        return "admin"
    if l in (x.lower() for x in c["verificados"]):
        return "verificada"
    return ""


def registrados():
    out = subprocess.run([gh(), "api", "-H", "Accept: application/vnd.github.star+json", "--paginate",
                          f"repos/{JUEGOS}/stargazers?per_page=100"], capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(out.stderr.strip() or "No se pudo leer la lista de cuentas")
    # --paginate junta varias listas JSON seguidas: "][" entre paginas
    txt = re.sub(r"\]\s*\[", ",", out.stdout.strip()) or "[]"
    r = [(x["user"]["login"], x.get("starred_at", "")[:10]) for x in json.loads(txt)]
    return sorted(r, key=lambda x: x[1], reverse=True)


def subir(d, mensaje, sin_subir):
    if sin_subir:
        print("Firmado. No se ha subido (--sin-subir).")
        return
    subprocess.run(["git", "-C", d, "add", "cuentas.json", "cuentas.json.firma"], check=True)
    if subprocess.run(["git", "-C", d, "diff", "--cached", "--quiet"]).returncode == 0:
        print("No hay cambios que subir.")
        return
    subprocess.run(["git", "-C", d, "commit", "-q", "-m", mensaje], check=True)
    subprocess.run(["git", "-C", d, "push", "-q"], check=True)
    print("Subido. Los editores verán la insignia al abrirse (o en menos de una hora).")


def main():
    ap = argparse.ArgumentParser(description="Cuentas de Source Play")
    ap.add_argument("orden", choices=["lista", "verificar", "admin", "quitar", "insignias"])
    ap.add_argument("arg", nargs="?")
    ap.add_argument("--sin-subir", action="store_true")
    a = ap.parse_args()
    d = repo()
    c = cargar(d)
    if a.orden == "lista":
        r = registrados()
        if a.arg:
            r = [x for x in r if a.arg.lower() in x[0].lower()]
        for i, (login, fecha) in enumerate(r, 1):
            ins = insignia(c, login)
            print(f"{i:>5}  {login:<40} se unió {fecha}" + (f"   [{ins}]" if ins else ""))
        print(f"\n{len(r)} cuenta(s)" + (f" con \"{a.arg}\"" if a.arg else " registradas"))
        return
    if a.orden == "insignias":
        for login in c["admins"]:
            print(f"  {login:<40} [admin]")
        for login in c["verificados"]:
            print(f"  {login:<40} [verificada]")
        if not c["admins"] and not c["verificados"]:
            print("Nadie tiene insignia todavía.")
        return
    if not a.arg or not re.fullmatch(r"[A-Za-z0-9-]{1,39}", a.arg):
        ap.error("falta el usuario de GitHub (letras, números y guiones)")
    login = a.arg
    c["verificados"] = [x for x in c["verificados"] if x.lower() != login.lower()]
    c["admins"] = [x for x in c["admins"] if x.lower() != login.lower()]
    if a.orden == "verificar":
        c["verificados"].append(login)
    elif a.orden == "admin":
        c["admins"].append(login)
    guardar_y_firmar(d, {"cuentas.json": c})
    que = {"verificar": "ahora es cuenta verificada", "admin": "ahora es admin", "quitar": "ya no tiene insignia"}[a.orden]
    subir(d, f"Cuentas: {login} {que}", a.sin_subir)
    print(f"{login} {que}.")


if __name__ == "__main__":
    main()
