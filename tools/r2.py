import datetime
import hashlib
import hmac
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

CONFIG = os.environ.get("S2D_R2_ENV") or os.path.expanduser("~/.config/source2d-r2.env")
NS = "{http://s3.amazonaws.com/doc/2006-03-01/}"


def config():
    if not os.path.exists(CONFIG):
        sys.exit(f"Falta {CONFIG} con las claves de R2 (mira Controladores/LEEME.md)")
    c = {}
    for linea in open(CONFIG, encoding="utf-8"):
        if "=" in linea and not linea.lstrip().startswith("#"):
            k, v = linea.strip().split("=", 1)
            c[k.strip()] = v.strip()
    for k in ("R2_ENDPOINT", "R2_BUCKET", "R2_BUCKET_PRIVADO", "R2_ACCESS_KEY_ID", "R2_SECRET_ACCESS_KEY", "R2_PUBLIC_URL"):
        if not c.get(k):
            sys.exit(f"Falta {k} en {CONFIG}")
    return c


def _firma(clave, texto):
    return hmac.new(clave, texto.encode(), hashlib.sha256).digest()


def pedir(metodo, bucket, ruta="", datos=b"", consulta=None, tipo=None):
    c = config()
    host = urllib.parse.urlparse(c["R2_ENDPOINT"]).netloc
    camino = "/" + bucket + ("/" + urllib.parse.quote(ruta, safe="/-_.~") if ruta else "")
    q = "&".join(f"{urllib.parse.quote(k, safe='-_.~')}={urllib.parse.quote(str(v), safe='-_.~')}" for k, v in sorted((consulta or {}).items()))
    t = datetime.datetime.now(datetime.timezone.utc)
    fecha, dia = t.strftime("%Y%m%dT%H%M%SZ"), t.strftime("%Y%m%d")
    huella = hashlib.sha256(datos).hexdigest()
    cab = {"host": host, "x-amz-content-sha256": huella, "x-amz-date": fecha}
    if tipo:
        cab["content-type"] = tipo
    nombres = ";".join(sorted(cab))
    canon = "\n".join([metodo, camino, q, "".join(f"{k}:{cab[k]}\n" for k in sorted(cab)), nombres, huella])
    ambito = f"{dia}/auto/s3/aws4_request"
    a_firmar = "\n".join(["AWS4-HMAC-SHA256", fecha, ambito, hashlib.sha256(canon.encode()).hexdigest()])
    k = _firma(("AWS4" + c["R2_SECRET_ACCESS_KEY"]).encode(), dia)
    for parte in ("auto", "s3", "aws4_request"):
        k = _firma(k, parte)
    firma = hmac.new(k, a_firmar.encode(), hashlib.sha256).hexdigest()
    cab["authorization"] = f"AWS4-HMAC-SHA256 Credential={c['R2_ACCESS_KEY_ID']}/{ambito}, SignedHeaders={nombres}, Signature={firma}"
    del cab["host"]
    url = c["R2_ENDPOINT"] + camino + ("?" + q if q else "")
    req = urllib.request.Request(url, data=datos if metodo in ("PUT", "POST") else None, method=metodo, headers=cab)
    try:
        with urllib.request.urlopen(req, timeout=600) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def bucket(privado=False):
    c = config()
    return c["R2_BUCKET_PRIVADO"] if privado else c["R2_BUCKET"]


def subir(ruta, datos, privado=False, tipo="application/octet-stream"):
    s, r = pedir("PUT", bucket(privado), ruta, datos, tipo=tipo)
    if s != 200:
        sys.exit(f"No se pudo subir {ruta} a R2 ({s}): {r[:300].decode(errors='replace')}")


def subir_archivo(ruta, archivo, privado=False, tipo="application/octet-stream"):
    with open(archivo, "rb") as f:
        subir(ruta, f.read(), privado, tipo)


def leer(ruta, privado=False):
    s, r = pedir("GET", bucket(privado), ruta)
    if s == 404:
        return None
    if s != 200:
        sys.exit(f"No se pudo leer {ruta} de R2 ({s})")
    return r


def borrar(ruta, privado=False):
    s, r = pedir("DELETE", bucket(privado), ruta)
    if s not in (200, 204, 404):
        sys.exit(f"No se pudo borrar {ruta} de R2 ({s})")


def listar(prefijo="", privado=False, delimitador=None):
    claves, carpetas, token = [], [], None
    while True:
        q = {"list-type": "2", "prefix": prefijo}
        if delimitador:
            q["delimiter"] = delimitador
        if token:
            q["continuation-token"] = token
        s, r = pedir("GET", bucket(privado), consulta=q)
        if s != 200:
            sys.exit(f"No se pudo listar R2 ({s}): {r[:300].decode(errors='replace')}")
        x = ET.fromstring(r)
        claves += [(e.find(NS + "Key").text, int(e.find(NS + "Size").text)) for e in x.findall(NS + "Contents")]
        carpetas += [e.find(NS + "Prefix").text for e in x.findall(NS + "CommonPrefixes")]
        if x.findtext(NS + "IsTruncated") != "true":
            return claves, carpetas
        token = x.findtext(NS + "NextContinuationToken")


def url_publica(ruta):
    return config()["R2_PUBLIC_URL"].rstrip("/") + "/" + urllib.parse.quote(ruta, safe="/-_.~")
