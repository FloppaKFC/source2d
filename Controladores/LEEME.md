# Controladores de Source 2D

Programas para llevar lo que se publica (se abren desde la terminal, desde cualquier carpeta):

| Programa | Para qué |
|---|---|
| `Controladores/Noticias.sh` | Noticias de Inicio > Noticias (con encuestas) y publicar versiones nuevas del editor |
| `Controladores/Source-Play.sh` | Source Play: revisar envíos, aprobarlos o rechazarlos, publicar tus juegos, retirar, denuncias |
| `Controladores/Source-Cuentas.sh` | Cuentas registradas en Source Play e insignias: cuenta verificada (azul) o admin (dorada) |

Los dos firman con tu clave (piden la contraseña). Con `S2D_SIN_SUBIR=1` delante firman pero no suben nada (para probar).

## Preparar Source Play (una vez)

1. **Repositorios** (ya creados): `FloppaKFC/source2d-envios` (llegan los envíos) y `FloppaKFC/source2d-juegos`
   (catálogo firmado y juegos aprobados), clonado en `../source2d-juegos`.
2. **Normas:** sube `Controladores/NORMAS.md` a la raíz de `source2d-envios` (el editor enlaza a
   `https://github.com/FloppaKFC/source2d-envios/blob/main/NORMAS.md`).
3. **Entrar con GitHub desde el editor:** la aplicación *Source 2D* de GitHub ya está puesta (Client ID
   `Ov23lipmhcQcBtNRYcEX` en `src/editor/envio.cpp`, con Device Flow). Pide los permisos `public_repo` (enviar juegos)
   y `user:follow` (seguir cuentas).

## Cuentas

- **Registrarse:** al entrar con GitHub desde el editor, la cuenta pone una estrella en `FloppaKFC/source2d-juegos`.
  Esa lista de estrellas es la lista de cuentas de Source Play, y la fecha de la estrella es "se unió".
- **Seguir** es el de GitHub: los seguidores y seguidos que se ven en el perfil son los de GitHub.
- **Insignias:** `cuentas.json` en `source2d-juegos`, firmado con tu clave. Lo cambia `Source-Cuentas.sh`.
