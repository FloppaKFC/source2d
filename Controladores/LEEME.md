# Controladores de Source 2D

Programas para llevar lo que se publica (se abren desde la terminal, desde cualquier carpeta):

| Programa | Para qué |
|---|---|
| `Controladores/Noticias.sh` | Noticias de Inicio > Noticias (con encuestas) y publicar versiones nuevas del editor |
| `Controladores/Source-Play.sh` | Source Play: revisar envíos, aprobarlos o rechazarlos, publicar tus juegos, retirar, denuncias |

Los dos firman con tu clave (piden la contraseña). Con `S2D_SIN_SUBIR=1` delante firman pero no suben nada (para probar).

## Preparar Source Play (una vez)

1. **Repositorios** (ya creados): `FloppaKFC/source2d-envios` (llegan los envíos) y `FloppaKFC/source2d-juegos`
   (catálogo firmado y juegos aprobados), clonado en `../source2d-juegos`.
2. **Normas:** sube `Controladores/NORMAS.md` a la raíz de `source2d-envios` (el editor enlaza a
   `https://github.com/FloppaKFC/source2d-envios/blob/main/NORMAS.md`).
3. **Entrar con GitHub desde el editor:** en https://github.com/settings/applications/new crea la aplicación
   *Source 2D* (Homepage y Callback: `https://github.com/FloppaKFC/source2d-versiones`) y marca **Enable Device Flow**.
   Copia el **Client ID** en `kClientId` de `src/editor/envio.cpp` y recompila. Sin eso todo lo demás funciona
   (descargar, jugar, publicar tus juegos con Source-Play.sh), pero los creadores no pueden enviar desde el editor.
