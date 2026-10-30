import worker from "../tools/worker/source2d-api.js";

class Bucket {
  constructor() {
    this.m = new Map();
    this.subidas = new Map();
  }
  obj(k) {
    const v = this.m.get(k);
    if (!v) return null;
    const d = v.data;
    return {
      key: k,
      size: d.length,
      uploaded: v.fecha,
      customMetadata: v.meta || {},
      body: new Response(d).body,
      json: async () => JSON.parse(new TextDecoder().decode(d)),
      text: async () => new TextDecoder().decode(d),
    };
  }
  async get(k) { return this.obj(k); }
  async head(k) { return this.obj(k); }
  async bytes(body) {
    if (typeof body === "string") return new TextEncoder().encode(body);
    return new Uint8Array(await new Response(body).arrayBuffer());
  }
  async put(k, body, o = {}) {
    this.m.set(k, { data: await this.bytes(body), meta: o.customMetadata, fecha: new Date() });
    return this.obj(k);
  }
  async delete(ks) { for (const k of [].concat(ks)) this.m.delete(k); }
  async list({ prefix = "", delimiter, cursor, limit = 1000 } = {}) {
    const keys = [...this.m.keys()].filter((k) => k.startsWith(prefix)).sort();
    const objects = [], pre = new Set();
    for (const k of keys) {
      if (delimiter) {
        const r = k.slice(prefix.length), i = r.indexOf(delimiter);
        if (i >= 0) { pre.add(prefix + r.slice(0, i + 1)); continue; }
      }
      objects.push(this.obj(k));
    }
    const start = cursor ? Number(cursor) : 0;
    return { objects: objects.slice(start, start + limit), delimitedPrefixes: [...pre], truncated: start + limit < objects.length, cursor: String(start + limit) };
  }
  async createMultipartUpload(key) {
    const uploadId = "up" + Math.random().toString(16).slice(2);
    this.subidas.set(uploadId, { key, partes: new Map() });
    return { uploadId };
  }
  resumeMultipartUpload(key, uploadId) {
    const s = this.subidas.get(uploadId);
    const b = this;
    return {
      async uploadPart(n, body) { s.partes.set(n, await b.bytes(body)); return { partNumber: n, etag: "e" + n }; },
      async complete(partes) {
        const datos = partes.sort((x, y) => x.partNumber - y.partNumber).map((p) => s.partes.get(p.partNumber));
        const total = new Uint8Array(datos.reduce((a, d) => a + d.length, 0));
        let o = 0;
        for (const d of datos) { total.set(d, o); o += d.length; }
        b.subidas.delete(uploadId);
        return b.put(key, total);
      },
      async abort() { b.subidas.delete(uploadId); },
    };
  }
}

const usuarios = { tok_aaaaaaaaaaaaaaaaaaaaaaaa: "Floppa", tok_bbbbbbbbbbbbbbbbbbbbbbbb: "Otra-Persona", tok_cccccccccccccccccccccccc: "Malo", tok_dddddddddddddddddddddddd: "Lejana" };
const sigueA = new Set(["floppa>otra-persona", "otra-persona>floppa", "floppa>lejana", "malo>floppa", "floppa>malo"]);
globalThis.fetch = async (url, init) => {
  const u = String(url);
  const quien = usuarios[(init?.headers?.authorization || "").slice(7)];
  if (u === "https://gh.test/user") return quien ? new Response(JSON.stringify({ login: quien }), { status: 200 }) : new Response("{}", { status: 401 });
  let m = /^https:\/\/gh\.test\/user\/following\/(.+)$/.exec(u);
  if (m) return new Response(null, { status: sigueA.has(`${quien.toLowerCase()}>${m[1].toLowerCase()}`) ? 204 : 404 });
  m = /^https:\/\/gh\.test\/users\/(.+)\/following\/(.+)$/.exec(u);
  if (m) return new Response(null, { status: sigueA.has(`${m[1].toLowerCase()}>${m[2].toLowerCase()}`) ? 204 : 404 });
  throw new Error("fetch inesperado " + u);
};

const env = { R2: new Bucket(), PRIVADO: new Bucket(), GITHUB_API: "https://gh.test" };
const A = "tok_aaaaaaaaaaaaaaaaaaaaaaaa", B = "tok_bbbbbbbbbbbbbbbbbbbbbbbb", C = "tok_cccccccccccccccccccccccc", D = "tok_dddddddddddddddddddddddd";
let fallos = 0, total = 0;
async function q(method, path, { tok, body, raw, len } = {}) {
  const headers = {};
  if (tok) headers.authorization = "Bearer " + tok;
  let b;
  if (body !== undefined) { b = JSON.stringify(body); headers["content-length"] = String(b.length); }
  if (raw !== undefined) { b = raw; headers["content-length"] = String(len ?? raw.length); }
  const r = await worker.fetch(new Request("https://api.test" + path, { method, headers, body: b, duplex: "half" }), env);
  const t = await r.text();
  let j;
  try { j = JSON.parse(t); } catch { j = t; }
  return { s: r.status, j };
}
function ok(nombre, cond, extra) {
  total++;
  if (!cond) { fallos++; console.log("FALLO", nombre, JSON.stringify(extra)?.slice(0, 400)); }
}
const notifs = async (tok) => (await q("GET", "/notificaciones", { tok })).j.notificaciones;
const json = (s) => JSON.stringify(s);

let r = await q("GET", "/"); ok("raiz", r.s === 200 && r.j.version === 2);
r = await q("POST", "/registro"); ok("sin cuenta 401", r.s === 401);
r = await q("GET", "/yo", { tok: "tok_falsofalsofalsofalsofalso" }); ok("token falso 401", r.s === 401);
r = await q("GET", "/yo", { tok: A }); ok("yo sin registro", r.s === 200 && !r.j.registrado && r.j.sinLeer === 0, r);
r = await q("POST", "/registro", { tok: A }); ok("registro beta", r.s === 201 && r.j.beta === true, r);
const unido = r.j.unido;
r = await q("POST", "/registro", { tok: A }); ok("registro repetido", r.j.unido === unido);
for (const t of [B, C, D]) await q("POST", "/registro", { tok: t });
r = await q("GET", "/registrados"); ok("registrados", r.j.cuentas.length === 4 && r.j.cuentas.every((c) => c.beta), r);

r = await q("PUT", "/perfil", { tok: A, body: { frase: "Hola", color: "#ff00aa", empezar: "sprites", insignia: "creador", destacados: { juegos: "mi-juego", sprites: "../x", sonidos: "musica" }, clavePublica: "A".repeat(43) + "=", login: "Otra", admin: true } });
ok("perfil limpio", r.s === 200 && r.j.login === "Floppa" && r.j.insignia === "creador" && r.j.destacados.juegos === "mi-juego" && !r.j.destacados.sprites && !r.j.admin, r);
r = await q("PUT", "/perfil", { tok: A, body: { insignia: "dios" } }); ok("insignia inventada ignorada", !r.j.insignia);

await env.R2.put("juegos/catalogo.json", json({ juegos: [{ id: "floppa-juego", nombre: "Juego de Floppa", autor: "Floppa", contador: "pjuego" }] }));
await env.R2.put("sprites/catalogo.json", json({ paquetes: [{ id: "otra-sprites", nombre: "Sprites", autor: "Otra-Persona", contador: "psprites" }] }));
await env.R2.put("sonidos/catalogo.json", json({ paquetes: [] }));
await env.R2.put("cuentas.json", json({ creadores: ["Floppa"], admins: [] }));

r = await q("POST", "/envios", { tok: B, body: { tipo: "virus", nombre: "x" } }); ok("tipo malo", r.s === 400);
r = await q("POST", "/envios", { tok: B, body: { tipo: "juegos", nombre: "Grande", tam: 150 * 1000000 } }); ok("envio > 100 MB sin permiso", r.s === 413 && /permiso/.test(r.j.error), r);
r = await q("POST", "/envios", { tok: B, body: { tipo: "juegos", nombre: "Mi juego", tam: 1000 } }); ok("envio creado", r.s === 201, r);
const id = r.j.id;
r = await q("PUT", `/envios/${id}/malware.exe`, { tok: B, raw: "MZ" }); ok("archivo no permitido", r.s === 400);
r = await q("PUT", `/envios/${id}/..%2Festado.json`, { tok: B, raw: "{}" }); ok("ruta rara", r.s >= 400 && r.s < 500, r);
r = await q("PUT", `/envios/${id}/info.json`, { tok: A, raw: "{}" }); ok("otra cuenta no sube al envio", r.s === 404);
r = await q("POST", `/envios/${id}/listo`, { tok: B }); ok("cerrar sin archivos", r.s === 400);
r = await q("PUT", `/envios/${id}/info.json`, { tok: B, raw: '{"nombre":"Mi juego"}' }); ok("sube info", r.s === 200 && r.j.archivos["info.json"] === 21, r);
r = await q("PUT", `/envios/${id}/juego.source2d`, { tok: B, raw: "x", len: 96 * 1000000 }); ok("parte > 95 MB", r.s === 413);
r = await q("PUT", `/envios/${id}/juego.source2d`, { tok: B, raw: "datos" }); ok("sube juego", r.s === 200);
ok("envio privado", env.PRIVADO.m.has(`envios/otra-persona/${id}/juego.source2d`) && ![...env.R2.m.keys()].some((k) => k.startsWith("envios/")));
r = await q("GET", "/yo", { tok: B }); ok("yo ve el envio subiendo", r.j.envios.length === 1 && r.j.envios[0].estado === "subiendo", r);
r = await q("POST", `/envios/${id}/listo`, { tok: B }); ok("cerrar envio", r.j.estado === "pendiente");
r = await q("PUT", `/envios/${id}/portada.png`, { tok: B, raw: "png" }); ok("no sube tras cerrar", r.s === 409);
const est = JSON.parse(new TextDecoder().decode(env.PRIVADO.m.get(`envios/otra-persona/${id}/estado.json`).data));
est.estado = "aprobado";
await env.PRIVADO.put(`envios/otra-persona/${id}/estado.json`, json(est));
r = await q("GET", "/yo", { tok: B }); ok("tiempo real: aprobado sin reiniciar", r.j.envios[0].estado === "aprobado", r);

r = await q("POST", "/solicitudes", { tok: B, body: { tipo: "grande", mb: 300, motivo: "Mi juego tiene mucha música" } }); ok("solicitud grande", r.s === 201 && r.j.estado === "pendiente", r);
r = await q("POST", "/solicitudes", { tok: B, body: { tipo: "grande", mb: 300, motivo: "otra vez" } }); ok("una pendiente por tipo", r.s === 409);
r = await q("POST", "/solicitudes", { tok: B, body: { tipo: "nube", mb: 100 } }); ok("solicitud sin motivo", r.s === 400);
r = await q("POST", "/solicitudes", { tok: B, body: { tipo: "nube", mb: 100, motivo: "Trabajo en dos PCs" } }); ok("solicitud nube", r.s === 201);
r = await q("GET", "/solicitudes", { tok: B }); ok("mis solicitudes", r.j.solicitudes.length === 2);
await env.PRIVADO.put("permisos/otra-persona.json", json({ grande_mb: 300, nube_mb: 1 }));
r = await q("GET", "/yo", { tok: B }); ok("permisos en yo", r.j.permisos.grande_mb === 300 && r.j.permisos.nube_mb === 1, r);

r = await q("POST", "/envios", { tok: B, body: { tipo: "juegos", nombre: "Grande", tam: 150 * 1000000 } }); ok("con permiso se puede > 100 MB", r.s === 201, r);
const gid = r.j.id;
const PARTE = 95 * 1000000;
r = await q("POST", "/partes", { tok: B, body: { destino: "envio", id: gid, archivo: "juego.source2d", tam: 400 * 1000000 } }); ok("partes: pasa del permiso", r.s === 413, r);
r = await q("POST", "/partes", { tok: B, body: { destino: "envio", id: gid, archivo: "juego.source2d", tam: 10 } }); ok("partes: empezar", r.s === 201, r);
const sub = r.j.subida;
r = await q("PUT", `/partes/${sub}/2`, { tok: B, raw: "x" }); ok("partes: parte de mas", r.s === 413, r);
r = await q("PUT", `/partes/${sub}/1`, { tok: A, raw: "x" }); ok("partes: otra cuenta", r.s === 404);
r = await q("PUT", `/partes/${sub}/1`, { tok: B, raw: "abcdefghij" }); ok("partes: subir", r.s === 200 && r.j.etag, r);
r = await q("POST", `/partes/${sub}/fin`, { tok: B, body: { partes: [{ n: 1, etag: r.j.etag }] } }); ok("partes: terminar", r.s === 200 && r.j.archivos["juego.source2d"] === 10, r);
ok("partes: archivo juntado", new TextDecoder().decode(env.PRIVADO.m.get(`envios/otra-persona/${gid}/juego.source2d`).data) === "abcdefghij");
r = await q("POST", "/partes", { tok: B, body: { destino: "envio", id: gid, archivo: "portada.png", tam: 5 } }); const sub2 = r.j.subida;
await q("PUT", `/partes/${sub2}/1`, { tok: B, raw: "123456789" });
r = await q("POST", `/partes/${sub2}/fin`, { tok: B, body: { partes: [{ n: 1, etag: "e1" }] } }); ok("partes: mas grande que lo anunciado", r.s === 413 && !env.PRIVADO.m.has(`envios/otra-persona/${gid}/portada.png`), r);
await q("DELETE", `/envios/${gid}`, { tok: B });

r = await q("GET", "/nube", { tok: A }); ok("nube sin permiso: cupo 0", r.j.cupo === 0);
r = await q("PUT", "/nube/Mi%20juego", { tok: A, raw: "proyecto" }); ok("nube sin permiso", r.s === 403);
r = await q("PUT", "/nube/Mi%20juego", { tok: B, raw: "proyecto" }); ok("nube subir", r.s === 200 && r.j.tam === 8, r);
r = await q("PUT", "/nube/Otro", { tok: B, raw: "x", len: 2 * 1000000 }); ok("nube sin espacio", r.s === 413, r);
r = await q("PUT", "/nube/..%2F..%2Fcuentas", { tok: B, raw: "x" }); ok("nube nombre raro", r.s === 400, r);
r = await q("GET", "/nube", { tok: B }); ok("nube lista", r.j.proyectos.length === 1 && r.j.proyectos[0].nombre === "Mi juego" && r.j.usado === 8, r);
r = await q("GET", "/nube/Mi%20juego", { tok: B }); ok("nube bajar", r.j === "proyecto", r);
r = await q("GET", "/nube/Mi%20juego", { tok: A }); ok("nube de otro no", r.s === 404);
r = await q("DELETE", "/nube/Mi%20juego", { tok: B }); ok("nube borrar", r.s === 200 && (await q("GET", "/nube", { tok: B })).j.usado === 0);

r = await q("POST", "/comentarios/juegos/floppa-juego", { tok: B, body: { texto: "¡Qué buen juego!" } }); ok("comentar", r.s === 201, r);
r = await q("POST", "/comentarios/juegos/no-existe", { tok: B, body: { texto: "hola" } }); ok("comentar juego que no existe", r.s === 404);
r = await q("POST", "/comentarios/juegos/floppa-juego", { tok: B, body: { texto: "   " } }); ok("comentario vacio", r.s === 400);
const com = JSON.parse(new TextDecoder().decode(env.R2.m.get("comentarios/juegos/floppa-juego.json").data));
ok("comentario publico", com.comentarios.length === 1 && com.comentarios[0].autor === "Otra-Persona");
r = await q("POST", "/feedback/juegos/floppa-juego", { tok: B, body: { texto: "El salto es muy alto" } }); ok("feedback", r.s === 201);
ok("feedback privado", ![...env.R2.m.keys()].some((k) => k.startsWith("feedback/")));
r = await q("GET", "/feedback", { tok: A }); ok("creador lee su feedback", r.j.feedback.length === 1 && r.j.feedback[0].de === "Otra-Persona", r);
r = await q("GET", "/feedback", { tok: B }); ok("otro no ve ese feedback", r.j.feedback.length === 0);
let ns = await notifs(A);
ok("notificaciones de comentario y feedback", ns.some((n) => n.tipo === "comentario") && ns.some((n) => n.tipo === "feedback"), ns);
r = await q("GET", "/yo", { tok: A }); ok("sin leer", r.j.sinLeer === 2, r);
await q("POST", "/notificaciones/leidas", { tok: A });
r = await q("GET", "/yo", { tok: A }); ok("leidas", r.j.sinLeer === 0, r);

for (let i = 0; i < 10; i++) await q("POST", "/megusta/pjuego-g", { tok: [A, B, C, D][i % 4] });
r = await q("GET", "/contadores?claves=pjuego-g"); ok("me gusta una vez por cuenta", r.j["pjuego-g"] === 4, r);
for (let i = 0; i < 10; i++) await env.PRIVADO.put(`megusta/pjuego-g/falsa${i}`, "1");
await env.R2.put("contadores/pjuego-g.json", json({ value: 9 }));
await env.PRIVADO.delete("megusta/pjuego-g/floppa");
await q("POST", "/megusta/pjuego-g", { tok: A });
await q("DELETE", "/megusta/pjuego-g", { tok: A });
await q("POST", "/megusta/pjuego-g", { tok: A });
ns = await notifs(A);
ok("hito de 10 me gusta, una sola vez", ns.filter((n) => n.tipo === "hito").length === 1 && /10 me gusta/.test(ns.find((n) => n.tipo === "hito").titulo), ns);
r = await q("POST", "/contador/pjuego-d"); ok("contador", r.j.value === 1);
r = await q("POST", "/contador/..%2Fx"); ok("contador clave mala", r.s === 404);

const llave = "B".repeat(43) + "=";
r = await q("POST", "/mensajes/Lejana", { tok: B, body: { cifrado: "abc" } }); ok("mensaje sin seguirse", r.s === 403, r);
r = await q("POST", "/mensajes/Nadie", { tok: B, body: { cifrado: "abc" } }); ok("mensaje a cuenta inexistente", r.s === 404);
r = await q("POST", "/mensajes/Floppa", { tok: B, body: { cifrado: "<script>" } }); ok("mensaje no cifrado", r.s === 400);
r = await q("POST", "/mensajes/Floppa", { tok: B, body: { cifrado: "aG9sYQ==" } }); ok("primer mensaje = solicitud", r.s === 201 && r.j.estado === "solicitud", r);
const conv = "floppa__otra-persona";
r = await q("POST", "/mensajes/Floppa", { tok: B, body: { cifrado: "b3Rybw==" } }); ok("no mas hasta aceptar", r.s === 409 && /Espera/.test(r.j.error), r);
ns = await notifs(A); ok("aviso de solicitud", ns.some((n) => n.tipo === "solicitud"));
r = await q("POST", `/mensajes/${conv}/aceptar`, { tok: B }); ok("no puede aceptar quien pide", r.s === 409);
r = await q("POST", `/mensajes/${conv}/aceptar`, { tok: A }); ok("aceptar", r.j.estado === "aceptada");
r = await q("POST", "/mensajes/Otra-Persona", { tok: A, body: { cifrado: "Y29udGVzdG8=" } }); ok("contestar", r.s === 201);
r = await q("GET", `/mensajes/${conv}`, { tok: B }); ok("leer conversacion", r.j.mensajes.length === 2, r);
r = await q("GET", `/mensajes/${conv}`, { tok: C }); ok("terceros no leen", r.s === 404);
r = await q("DELETE", `/mensajes/${conv}`, { tok: A }); ok("no se borran mensajes", r.s === 405);
r = await q("GET", "/mensajes", { tok: A }); ok("bandeja", r.j.conversaciones.length === 1 && r.j.conversaciones[0].con === "Otra-Persona", r);
r = await q("POST", "/mensajes/Lejana", { tok: A, body: { cifrado: "YXZpc28=" } }); ok("Creador escribe sin seguirse ni solicitud", r.s === 201 && r.j.estado === "aceptada", r);
ns = await notifs(D); ok("aviso del creador", ns.some((n) => n.tipo === "mensaje" && /Creador/.test(n.titulo)), ns);
r = await q("POST", "/mensajes/Lejana", { tok: B, body: { cifrado: "aG9sYQ==" } }); ok("otros siguen sin poder", r.s === 403);
r = await q("POST", "/mensajes/Floppa", { tok: C, body: { cifrado: "aG9sYQ==" } }); ok("Malo pide hablar", r.s === 201);
r = await q("POST", "/mensajes/floppa__malo/bloquear", { tok: A }); ok("bloquear", r.j.estado === "bloqueada");
r = await q("POST", "/mensajes/Floppa", { tok: C, body: { cifrado: "aG9sYQ==" } }); ok("bloqueado no escribe", r.s === 403);

r = await q("POST", "/denuncias", { tok: A, body: { tipo: "chat", titulo: "Insultos en Trabajar juntos", cuerpo: "Me insultó", objetivo: "Malo", mensajes: [{ de: "Malo", texto: "eres malo", fecha: "2026-10-29T20:00:00Z" }, { de: "x", texto: "y", llave: "no valida!!" }] } });
ok("reportar chat", r.s === 201);
const den = [...env.PRIVADO.m.entries()].find(([k]) => k.startsWith("denuncias/"));
const dj = JSON.parse(new TextDecoder().decode(den[1].data));
ok("denuncia con los mensajes elegidos", dj.tipo === "chat" && dj.mensajes.length === 2 && dj.mensajes[0].texto === "eres malo" && dj.mensajes[1].llave === "", dj);
ok("denuncia privada", ![...env.R2.m.keys()].some((k) => k.startsWith("denuncias/")));

await env.PRIVADO.put("baneos/malo.json", json({ hasta: "2999-01-01T00:00:00Z", motivo: "Insultos" }));
r = await q("POST", "/comentarios/juegos/floppa-juego", { tok: C, body: { texto: "hola" } }); ok("baneado no comenta", r.s === 403 && r.j.baneo.motivo === "Insultos", r);
r = await q("GET", "/yo", { tok: C }); ok("yo dice baneo", r.j.baneo && r.j.baneo.hasta.startsWith("2999"), r);
await env.PRIVADO.put("baneos/malo.json", json({ hasta: "", motivo: "Para siempre" }));
r = await q("PUT", "/perfil", { tok: C, body: {} }); ok("baneo permanente", r.s === 403 && /siempre/.test(r.j.error), r);
await env.PRIVADO.put("baneos/malo.json", json({ hasta: "2000-01-01T00:00:00Z", motivo: "viejo" }));
r = await q("PUT", "/perfil", { tok: C, body: {} }); ok("baneo caducado", r.s === 200, r);

for (let i = 0; i < 30; i++) await q("POST", "/comentarios/juegos/floppa-juego", { tok: D, body: { texto: "spam " + i } });
r = await q("POST", "/comentarios/juegos/floppa-juego", { tok: D, body: { texto: "uno mas" } }); ok("limite de comentarios", r.s === 429);
r = await q("GET", "/nada"); ok("404", r.s === 404);
r = await q("DELETE", "/perfil", { tok: A }); ok("405", r.s === 405);

console.log(fallos ? `FALLOS: ${fallos} de ${total}` : `Worker: ${total} comprobaciones, todo bien`);
process.exit(fallos ? 1 : 0);
