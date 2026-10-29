#!/usr/bin/env python3
# Crea una clave de firma de prueba (build/firma nueva pide la contrasena por terminal) y escribe su parte publica.
# Uso: S2D_FIRMA_DIR=<carpeta> clave_prueba.py <contrasena>   -> imprime la clave publica en hexadecimal
import os, pty, subprocess, sys, time

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIRMA = os.path.join(RAIZ, "build", "firma")
pid, fd = pty.fork()
if pid == 0:
    os.execv(FIRMA, [FIRMA, "nueva"])
while True:
    try:
        d = os.read(fd, 4096)
    except OSError:
        break
    if not d:
        break
    if d.rstrip().endswith(b":"):
        time.sleep(0.2)
        os.write(fd, sys.argv[1].encode() + b"\n")
os.waitpid(pid, 0)
print(subprocess.run([FIRMA, "publica"], capture_output=True, text=True).stdout.strip().splitlines()[-1])
