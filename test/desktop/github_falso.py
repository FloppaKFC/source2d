#!/usr/bin/env python3
import base64, json, os, sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PUERTO, SALIDA = int(sys.argv[1]), sys.argv[2]
estado = {"sondeos": 0, "refs": 0, "registrado": False, "sigo": False}
PERFIL = {"login": "amiga", "name": "Amiga Prueba", "bio": "Hago juegos de ritmo", "created_at": "2020-05-01T10:00:00Z",
          "followers": 3, "following": 1, "avatar_url": "file://" + os.path.abspath(os.path.join(SALIDA, "avatar.png"))}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def responder(self, code, cuerpo):
        b = json.dumps(cuerpo).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def cuerpo(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n).decode() if n else ""

    def autorizado(self):
        if self.headers.get("Authorization") != "Bearer token-de-prueba":
            self.responder(401, {"message": "Bad credentials"})
            return False
        return True

    def do_POST(self):
        b = self.cuerpo()
        if self.path == "/login/device/code":
            if "client_id=cliente-de-prueba" not in b:
                return self.responder(400, {"error": "bad client"})
            return self.responder(200, {"device_code": "dc1", "user_code": "ABCD-1234", "interval": 1, "expires_in": 600,
                                        "verification_uri": f"http://127.0.0.1:{PUERTO}/login/device"})
        if self.path == "/login/oauth/access_token":
            estado["sondeos"] += 1
            if estado["sondeos"] < 2:
                return self.responder(200, {"error": "authorization_pending"})
            return self.responder(200, {"access_token": "token-de-prueba", "token_type": "bearer"})
        if not self.autorizado():
            return
        if self.path == "/repos/FloppaKFC/source2d-envios/forks":
            return self.responder(202, {"full_name": "creador/source2d-envios"})
        if self.path == "/repos/creador/source2d-envios/git/refs":
            j = json.loads(b)
            if j.get("sha") != "abc123" or not j["ref"].startswith("refs/heads/envio-"):
                return self.responder(422, {"message": "bad ref"})
            return self.responder(201, {"ref": j["ref"]})
        if self.path == "/repos/FloppaKFC/source2d-envios/issues":
            j = json.loads(b)
            if not j["title"].startswith("Denuncia: ") or "**Motivo:**" not in j["body"]:
                return self.responder(422, {"message": "bad issue"})
            with open(os.path.join(SALIDA, "denuncia.json"), "w") as f:
                json.dump(j, f)
            return self.responder(201, {"number": 9})
        if self.path == "/repos/FloppaKFC/source2d-envios/pulls":
            j = json.loads(b)
            if not j["head"].startswith("creador:envio-") or j["base"] != "main":
                return self.responder(422, {"message": "bad pr"})
            with open(os.path.join(SALIDA, "pr.json"), "w") as f:
                json.dump(j, f)
            return self.responder(201, {"number": 7, "html_url": "https://github.com/FloppaKFC/source2d-envios/pull/7"})
        self.responder(404, {"message": "Not Found"})

    def do_DELETE(self):
        if not self.autorizado():
            return
        if self.path == "/user/following/amiga":
            estado["sigo"] = False
            return self.responder(204, {})
        self.responder(404, {"message": "Not Found"})

    def do_PUT(self):
        b = self.cuerpo()
        if not self.autorizado():
            return
        if self.path == "/user/starred/FloppaKFC/source2d-juegos":
            estado["registrado"] = True
            return self.responder(204, {})
        if self.path == "/user/following/amiga":
            estado["sigo"] = True
            return self.responder(204, {})
        pre = "/repos/creador/source2d-envios/contents/"
        if self.path.startswith(pre):
            j = json.loads(b)
            ruta = self.path[len(pre):]
            if not ruta.startswith("envios/creador/") or ".." in ruta:
                return self.responder(422, {"message": "bad path"})
            dest = os.path.join(SALIDA, ruta)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(base64.b64decode(j["content"]))
            return self.responder(201, {"content": {"path": ruta}})
        self.responder(404, {"message": "Not Found"})

    def do_GET(self):
        if not self.autorizado():
            return
        ruta, _, q = self.path.partition("?")
        if "page=" in q and "page=1" not in q.split("&"):
            return self.responder(200, [])
        if self.path == "/user":
            return self.responder(200, {"login": "creador"})
        if ruta == "/repos/FloppaKFC/source2d-juegos/stargazers":
            if "star+json" not in (self.headers.get("Accept") or ""):
                return self.responder(200, [{"login": "creador"}])
            gente = [{"starred_at": "2026-10-01T09:00:00Z", "user": {"login": "amiga"}}]
            if estado["registrado"]:
                gente.append({"starred_at": "2026-10-29T12:00:00Z", "user": {"login": "creador"}})
            return self.responder(200, gente)
        if self.path == "/users/amiga":
            return self.responder(200, dict(PERFIL, followers=PERFIL["followers"] + (1 if estado["sigo"] else 0)))
        if self.path == "/users/creador":
            return self.responder(200, {"login": "creador", "name": None, "created_at": "2019-01-01T00:00:00Z", "followers": 0,
                                        "following": 1 if estado["sigo"] else 0, "avatar_url": ""})
        if self.path == "/user/following/amiga":
            return self.responder(204 if estado["sigo"] else 404, {})
        if ruta == "/users/amiga/followers":
            return self.responder(200, [{"login": "uno"}, {"login": "dos"}, {"login": "tres"}] + ([{"login": "creador"}] if estado["sigo"] else []))
        if ruta == "/users/amiga/following":
            return self.responder(200, [{"login": "uno"}, {"login": "creador"}])
        if ruta == "/user/following":
            return self.responder(200, [{"login": "uno"}] + ([{"login": "amiga"}] if estado["sigo"] else []))
        if ruta in ("/users/creador/followers", "/users/creador/following"):
            return self.responder(200, [{"login": "amiga"}] if ruta.endswith("followers") or estado["sigo"] else [])
        if self.path == "/repos/creador/source2d-envios/git/ref/heads/main":
            estado["refs"] += 1
            if estado["refs"] < 2:
                return self.responder(404, {"message": "Not Found"})
            return self.responder(200, {"object": {"sha": "abc123"}})
        if self.path == "/repos/FloppaKFC/source2d-envios/issues/7/comments":
            return self.responder(200, [{"user": {"login": "otro"}, "body": "¡Qué bueno!"},
                                        {"user": {"login": "FloppaKFC"}, "body": "Rechazado: falta un final"}])
        self.responder(404, {"message": "Not Found"})

HTTPServer(("127.0.0.1", PUERTO), H).serve_forever()
