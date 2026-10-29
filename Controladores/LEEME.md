# Controladores de Source 2D

Programas para llevar lo que se publica (se abren desde la terminal, desde cualquier carpeta):

| Programa | Para qué |
|---|---|
| `Controladores/Noticias.sh` | Noticias de Inicio > Noticias (con encuestas) y publicar versiones nuevas del editor |
| `Controladores/Source-Play.sh` | Source Play: revisar envíos, aprobarlos o rechazarlos, publicar tus juegos, retirar, denuncias |
| `Controladores/Source-Sprite.sh` | Source Store de sprites (sprites, tiles y autotiles): envíos, publicar, retirar, destacados, denuncias |
| `Controladores/Source-Sound.sh` | Source Store de sonidos (música y efectos de Source Sound): lo mismo que Source-Sprite.sh |
| `Controladores/Source-Cuentas.sh` | Cuentas registradas en Source Play e insignias: verificada (azul), admin (dorada) o creador (morada con estrella) |

Los dos firman con tu clave (piden la contraseña). Con `S2D_SIN_SUBIR=1` delante firman pero no suben nada (para probar).

## Preparar Source Play (una vez)

1. **Repositorios** (ya creados): `FloppaKFC/source2d-envios` (llegan los envíos y las denuncias) y
   `FloppaKFC/source2d-juegos` (catálogo y cuentas firmados), clonado en `../source2d-juegos`.
   **Los juegos van como archivos de Releases** de `source2d-juegos` (una por juego y versión): son gratis, cada archivo
   puede pesar hasta 2 GB y no cuentan para el tamaño del repositorio, que solo guarda el catálogo (unos KB).
2. **Normas:** sube `Controladores/NORMAS.md` a la raíz de `source2d-envios` (el editor enlaza a
   `https://github.com/FloppaKFC/source2d-envios/blob/main/NORMAS.md`).
3. **Entrar con GitHub desde el editor:** la aplicación *Source 2D* de GitHub ya está puesta (Client ID
   `Ov23lipmhcQcBtNRYcEX` en `src/editor/envio.cpp`, con Device Flow). Pide los permisos `public_repo` (enviar juegos)
   y `user:follow` (seguir cuentas).

## Cuentas

- **Registrarse:** al entrar con GitHub desde el editor, la cuenta pone una estrella en `FloppaKFC/source2d-juegos`.
  Esa lista de estrellas es la lista de cuentas de Source Play, y la fecha de la estrella es "se unió".
- **Seguir** usa el de GitHub, pero el perfil solo cuenta y muestra las cuentas de Source Play.
- **Denuncias** de juegos y cuentas: llegan como incidencias a `source2d-envios` y se ven en Source-Play.sh > Denuncias
  (con la insignia de quien denuncia y de la cuenta denunciada).
- **Destacados:** Source-Play.sh > Destacados. Salen arriba del todo en Source Play; debajo, los favoritos de cada persona.
- **Insignias:** `cuentas.json` en `source2d-juegos`, firmado con tu clave. Lo cambia `Source-Cuentas.sh`.

## Source Store (sprites y sonidos)

- **Repositorios** (ya creados): `FloppaKFC/source2d-sprites` y `FloppaKFC/source2d-sonidos`, clonados en
  `../source2d-sprites` y `../source2d-sonidos`. Igual que Source Play: catálogo firmado en el repositorio y los paquetes
  (`.s2dpaquete`, comprimidos) como archivos de sus Releases.
- **Envíos:** llegan a `source2d-envios` con el título `Sprites: ...` o `Sonidos: ...`. Se revisan con `Source-Sprite.sh`
  y `Source-Sound.sh`; "probar" saca el contenido a una carpeta para abrirlo en el editor.
- En sonidos solo se aceptan creaciones de Source Sound (notas, no audio grabado): el editor rechaza lo demás.
