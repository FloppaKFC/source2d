# Controladores de Source 2D

Todo se lleva desde un solo programa: **`Controladores/admin.sh`** (se abre desde la terminal, desde cualquier carpeta).

| Opción | Para qué |
|---|---|
| Envíos | Probar, aprobar o rechazar juegos, sprites y sonidos. Los juegos se abren **aislados** con bubblewrap (sin internet ni acceso a tus archivos); los paquetes se sacan a una carpeta para ver las imágenes y escuchar los sonidos con el editor |
| Publicar uno mío | Un paquete guardado con "Guardar en una carpeta" |
| Lo publicado | Lista, retirar y destacados |
| Denuncias | Juegos, cuentas, comentarios, chats de Trabajar juntos y mensajes privados (comprobados con su llave) |
| Cuentas | Insignias (verificada, admin, creador, contribuidor) y contribuciones |
| Baneos | 30 días, para siempre o los días que quieras; se levantan solos |
| Solicitudes | Proyectos de más de 100 MB y espacio en la nube (50, 100, 500 MB...) |
| Noticias | Salen en Inicio > Noticias |

Al aprobar, rechazar, banear o dar algo le llega una notificación a la persona.

## Lo que necesita

- `~/.config/source2d-r2.env` con las claves de R2 (los dos buckets: `source2d` público y `source2d-privado`).
- La clave de firma (pide la contraseña al firmar catálogos y cuentas).
- `build/source2d` compilado (`make`) para comprobar juegos y paquetes.
- `bubblewrap` (`sudo pacman -S bubblewrap`) para jugar los juegos de otros sin riesgo.

## Dónde está cada cosa

- **R2 público** (`https://pub-45b3e9e445dc4ba8bc581524cfc68bf0.r2.dev`): catálogos firmados (`juegos/`, `sprites/`, `sonidos/`), archivos publicados, `cuentas.json` firmado, perfiles, comentarios, contadores y `novedades/`.
- **R2 privado**: envíos, denuncias, baneos, permisos, solicitudes, notificaciones, feedback, mensajes cifrados y la nube de cada cuenta.
- **Worker** (`tools/worker/source2d-api.js`, en `https://source2d-api.acontrerasvalensia.workers.dev`): lo que hacen los editores con su cuenta de GitHub. Si cambia, se vuelve a pegar en Cloudflare.
- **GitHub**: solo para entrar y seguir cuentas, y las versiones del editor en Releases de `FloppaKFC/source2d`.
