const VERSION = 3;
const BETA = true;
const MB = 1000000;
const TIPOS = ["juegos", "sprites", "sonidos"];
const ARCHIVOS = {
  juegos: ["info.json", "portada.png", "juego.source2d", "juego.s2dplay", "original.s2dcerrado"],
  sprites: ["info.json", "paquete.s2dpaquete", "portada.png"],
  sonidos: ["info.json", "paquete.s2dpaquete", "portada.png"],
};
const PARTE_MAX = 95 * MB;
const PARTE_MIN = 5 * 1024 * 1024;
const ENVIO_MAX = 100 * MB;
const MAX_PENDIENTES = 3;
const LIMITES = { envios: 5, denuncias: 10, comentarios: 30, feedback: 20, mensajes: 200, solicitudes: 5, nube: 100 };
const HITOS = [10, 30, 50, 100, 250, 500, 1000, 5000, 10000];
const MAX_JSON = 65536;
const CLAVE = /^[a-z0-9][a-z0-9_-]{0,79}$/;
const ID = /^[a-z0-9]{6,16}-[a-f0-9]{8}$/;
const LOGIN = /^[A-Za-z0-9-]{1,39}$/;
const COLOR = /^#[0-9a-fA-F]{6}$/;
const LLAVE = /^[A-Za-z0-9+/]{43}=$/;
const CIFRADO = /^[A-Za-z0-9+/=]{1,12000}$/;
const PROYECTO = /^[A-Za-z0-9 _().áéíóúñÁÉÍÓÚÑ-]{1,60}$/;
const INSIGNIAS = ["", "verificada", "admin", "creador", "contribuidor", "beta"];

const responder = (datos, estado = 200) =>
  new Response(JSON.stringify(datos), {
    status: estado,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
const fallo = (estado, mensaje) => responder({ error: mensaje }, estado);
const hoy = () => new Date().toISOString().slice(0, 10);
const ahora = () => new Date().toISOString();
const texto = (v, max) => (typeof v === "string" ? v.replace(/\r/g, "").trim().slice(0, max) : "");
const invertido = () => String(9999999999999 - Date.now()).padStart(13, "0");
const fechaDeInvertido = (k) => new Date(9999999999999 - Number(k.slice(0, 13))).toISOString();

function aleatorio(n) {
  const b = new Uint8Array(n);
  crypto.getRandomValues(b);
  return [...b].map((x) => x.toString(16).padStart(2, "0")).join("");
}
const nuevoId = () => Date.now().toString(36) + "-" + aleatorio(4);

async function leerJSON(bucket, clave, defecto) {
  const o = await bucket.get(clave);
  if (!o) return defecto;
  try {
    return await o.json();
  } catch {
    return defecto;
  }
}

function guardarJSON(bucket, clave, valor, meta) {
  return bucket.put(clave, JSON.stringify(valor), {
    httpMetadata: { contentType: "application/json; charset=utf-8" },
    customMetadata: meta,
  });
}

async function listarTodo(bucket, prefix, max = 10000) {
  const r = [];
  let cursor;
  do {
    const l = await bucket.list({ prefix, cursor });
    r.push(...l.objects);
    cursor = l.truncated ? l.cursor : undefined;
  } while (cursor && r.length < max);
  return r;
}

async function borrarPrefijo(bucket, prefix) {
  const objs = await listarTodo(bucket, prefix);
  for (let i = 0; i < objs.length; i += 1000) await bucket.delete(objs.slice(i, i + 1000).map((o) => o.key));
}

async function cuerpoJSON(request) {
  const largo = Number(request.headers.get("content-length") || 0);
  if (largo > MAX_JSON) return null;
  const t = await request.text();
  if (t.length > MAX_JSON) return null;
  try {
    const v = JSON.parse(t);
    return v && typeof v === "object" && !Array.isArray(v) ? v : null;
  } catch {
    return null;
  }
}

const github = (env) => env.GITHUB_API || "https://api.github.com";
const cabGitHub = (token) => ({ authorization: "Bearer " + token, "user-agent": "source2d-api", accept: "application/vnd.github+json" });

async function usuario(request, env) {
  const m = /^(?:Bearer|token) ([A-Za-z0-9_]{20,255})$/.exec(request.headers.get("authorization") || "");
  if (!m) return null;
  const r = await fetch(github(env) + "/user", { headers: cabGitHub(m[1]) });
  if (r.status !== 200) return null;
  const u = await r.json();
  if (typeof u.login !== "string" || !LOGIN.test(u.login)) return null;
  return { login: u.login, clave: u.login.toLowerCase(), token: m[1] };
}

async function cuentas(env) {
  const c = await leerJSON(env.R2, "cuentas.json", {});
  const lista = (k) => (Array.isArray(c[k]) ? c[k].map((x) => String(x).toLowerCase()) : []);
  return { creadores: lista("creadores"), admins: lista("admins") };
}

async function baneo(env, clave) {
  const b = await leerJSON(env.PRIVADO, `baneos/${clave}.json`, null);
  if (!b) return null;
  if (b.hasta && b.hasta <= ahora()) return null;
  return { hasta: b.hasta || "", motivo: b.motivo || "", fecha: b.fecha || "" };
}

async function permisos(env, clave) {
  const p = await leerJSON(env.PRIVADO, `permisos/${clave}.json`, {});
  return { grande_mb: Math.max(0, Number(p.grande_mb) || 0), nube_mb: Math.max(0, Number(p.nube_mb) || 0) };
}

async function gastar(env, u, que) {
  const clave = `limites/${u.clave}/${hoy()}.json`;
  const l = await leerJSON(env.PRIVADO, clave, {});
  const n = Number(l[que]) || 0;
  if (n >= LIMITES[que]) return false;
  l[que] = n + 1;
  await guardarJSON(env.PRIVADO, clave, l);
  return true;
}

async function notificar(env, login, n) {
  if (!login || !LOGIN.test(login)) return;
  const clave = login.toLowerCase();
  const id = invertido() + "-" + aleatorio(4);
  await guardarJSON(env.PRIVADO, `notificaciones/${clave}/${id}.json`, {
    id,
    tipo: n.tipo || "aviso",
    titulo: texto(n.titulo, 140),
    texto: texto(n.texto || "", 2000),
    enlace: n.enlace || "",
    fecha: ahora(),
  });
}

async function buscarEnCatalogo(env, tipo, id) {
  if (!TIPOS.includes(tipo) || !CLAVE.test(id)) return null;
  const c = await leerJSON(env.R2, `${tipo}/catalogo.json`, {});
  const lista = Array.isArray(c.juegos) ? c.juegos : Array.isArray(c.paquetes) ? c.paquetes : [];
  return lista.find((x) => x && x.id === id) || null;
}

async function buscarPorContador(env, contador) {
  for (const tipo of TIPOS) {
    const c = await leerJSON(env.R2, `${tipo}/catalogo.json`, {});
    const lista = Array.isArray(c.juegos) ? c.juegos : Array.isArray(c.paquetes) ? c.paquetes : [];
    const e = lista.find((x) => x && x.contador === contador);
    if (e) return { tipo, e };
  }
  return null;
}

async function contar(env, clave, cuanto) {
  const k = `contadores/${clave}.json`;
  const c = await leerJSON(env.R2, k, {});
  const valor = Math.max(0, (Number(c.value) || 0) + cuanto);
  await guardarJSON(env.R2, k, { value: valor });
  return valor;
}

async function yo(env, u) {
  const registrado = await leerJSON(env.R2, `cuentas/${u.clave}.json`, null);
  const ban = await baneo(env, u.clave);
  const perm = await permisos(env, u.clave);
  const leidas = (await leerJSON(env.PRIVADO, `leidas/${u.clave}.json`, {})).hasta || "";
  const l = await env.PRIVADO.list({ prefix: `notificaciones/${u.clave}/`, limit: 100 });
  const sinLeer = l.objects.filter((o) => fechaDeInvertido(o.key.split("/")[2]) > leidas).length;
  const envios = [];
  for (const id of (await idsEnvios(env, u)).slice(-20)) {
    const e = await estadoEnvio(env, u, id);
    if (e) envios.push({ id: e.id, tipo: e.tipo, nombre: e.nombre, estado: e.estado, motivo: e.motivo || "", fecha: e.fecha });
  }
  return responder({ login: u.login, registrado: !!registrado, beta: !!registrado?.beta, unido: registrado?.unido || "", baneo: ban, permisos: perm, sinLeer, envios });
}

async function registrados(env, url) {
  const cursor = url.searchParams.get("cursor") || undefined;
  const l = await env.R2.list({ prefix: "cuentas/", cursor, limit: 1000, include: ["customMetadata"] });
  const lista = l.objects.map((o) => ({
    login: o.customMetadata?.login || o.key.slice(8, -5),
    unido: o.customMetadata?.unido || "",
    beta: o.customMetadata?.beta === "1",
  }));
  return responder({ cuentas: lista, cursor: l.truncated ? l.cursor : null });
}

async function registro(env, u) {
  const clave = `cuentas/${u.clave}.json`;
  const ya = await leerJSON(env.R2, clave, null);
  if (ya) return responder(ya);
  const cuenta = { login: u.login, unido: ahora(), beta: BETA };
  await guardarJSON(env.R2, clave, cuenta, { login: u.login, unido: cuenta.unido, beta: BETA ? "1" : "0" });
  return responder(cuenta, 201);
}

async function perfil(env, u, request) {
  const p = await cuerpoJSON(request);
  if (!p) return fallo(400, "Perfil no válido");
  const limpio = { login: u.login, actualizado: ahora() };
  const frase = texto(p.frase, 140);
  if (frase) limpio.frase = frase;
  if (typeof p.color === "string" && COLOR.test(p.color)) limpio.color = p.color;
  if (["juegos", "sprites", "sonidos", "contribuciones"].includes(p.empezar)) limpio.empezar = p.empezar;
  if (INSIGNIAS.includes(p.insignia)) limpio.insignia = p.insignia;
  if (p.destacados && typeof p.destacados === "object" && !Array.isArray(p.destacados)) {
    limpio.destacados = {};
    for (const t of TIPOS) if (typeof p.destacados[t] === "string" && CLAVE.test(p.destacados[t])) limpio.destacados[t] = p.destacados[t];
  }
  if (typeof p.clavePublica === "string" && LLAVE.test(p.clavePublica)) limpio.clavePublica = p.clavePublica;
  await guardarJSON(env.R2, `perfiles/${u.clave}.json`, limpio);
  return responder(limpio);
}

async function idsEnvios(env, u) {
  const ids = [];
  let cursor;
  do {
    const l = await env.PRIVADO.list({ prefix: `envios/${u.clave}/`, delimiter: "/", cursor });
    for (const p of l.delimitedPrefixes) ids.push(p.split("/")[2]);
    cursor = l.truncated ? l.cursor : undefined;
  } while (cursor);
  return ids.sort();
}

async function estadoEnvio(env, u, id) {
  if (!ID.test(id)) return null;
  return leerJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, null);
}

async function maxEnvio(env, u) {
  const p = await permisos(env, u.clave);
  return Math.max(ENVIO_MAX, p.grande_mb * MB);
}

async function nuevoEnvio(env, u, request) {
  const p = await cuerpoJSON(request);
  if (!p || !TIPOS.includes(p.tipo)) return fallo(400, "Envío no válido");
  const nombre = texto(p.nombre, 80);
  if (!nombre) return fallo(400, "Falta el nombre");
  const tam = Number(p.tam) || 0;
  const max = await maxEnvio(env, u);
  if (tam > max) return fallo(413, `El envío pesa más de ${Math.round(max / MB)} MB. Pide permiso para subir proyectos más grandes.`);
  let abiertos = 0;
  for (const id of await idsEnvios(env, u)) {
    const e = await estadoEnvio(env, u, id);
    if (e && (e.estado === "subiendo" || e.estado === "pendiente")) abiertos++;
  }
  if (abiertos >= MAX_PENDIENTES) return fallo(429, `Ya tienes ${MAX_PENDIENTES} envíos esperando revisión`);
  if (!(await gastar(env, u, "envios"))) return fallo(429, "Has llegado al límite de envíos de hoy");
  const id = nuevoId();
  const e = { id, tipo: p.tipo, nombre, autor: u.login, fecha: ahora(), estado: "subiendo", archivos: {} };
  await guardarJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, e);
  return responder(e, 201);
}

async function prepararSubidaEnvio(env, u, id, archivo, tam) {
  const e = await estadoEnvio(env, u, id);
  if (!e) return { error: fallo(404, "No existe ese envío") };
  if (e.estado !== "subiendo") return { error: fallo(409, "Ese envío ya está cerrado") };
  if (!ARCHIVOS[e.tipo].includes(archivo)) return { error: fallo(400, "Archivo no permitido") };
  const total = Object.entries(e.archivos).reduce((s, [k, v]) => s + (k === archivo ? 0 : v), 0);
  const max = await maxEnvio(env, u);
  if (total + tam > max) return { error: fallo(413, `El envío pasa de ${Math.round(max / MB)} MB. Pide permiso para subir proyectos más grandes.`) };
  return { e, clave: `envios/${u.clave}/${id}/${archivo}` };
}

async function apuntarArchivoEnvio(env, u, e, archivo, tam) {
  e.archivos[archivo] = tam;
  await guardarJSON(env.PRIVADO, `envios/${u.clave}/${e.id}/estado.json`, e);
}

async function subirArchivo(env, u, id, archivo, request) {
  const largo = Number(request.headers.get("content-length"));
  if (!Number.isInteger(largo) || largo <= 0 || !request.body) return fallo(411, "Falta el tamaño del archivo");
  if (largo > PARTE_MAX) return fallo(413, "Archivo demasiado grande para una sola subida: súbelo por partes");
  const s = await prepararSubidaEnvio(env, u, id, archivo, largo);
  if (s.error) return s.error;
  const o = await env.PRIVADO.put(s.clave, request.body);
  await apuntarArchivoEnvio(env, u, s.e, archivo, o.size);
  return responder(s.e);
}

async function cerrarEnvio(env, u, id) {
  const e = await estadoEnvio(env, u, id);
  if (!e) return fallo(404, "No existe ese envío");
  if (e.estado !== "subiendo") return fallo(409, "Ese envío ya está cerrado");
  const principal = e.tipo === "juegos" ? ["juego.source2d", "juego.s2dplay"] : ["paquete.s2dpaquete"];
  if (!e.archivos["info.json"] || !principal.some((a) => e.archivos[a])) return fallo(400, "Faltan archivos del envío");
  e.estado = "pendiente";
  e.fecha = ahora();
  await guardarJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, e);
  return responder(e);
}

async function borrarEnvio(env, u, id) {
  const e = await estadoEnvio(env, u, id);
  if (!e) return fallo(404, "No existe ese envío");
  await borrarPrefijo(env.PRIVADO, `envios/${u.clave}/${id}/`);
  return responder({ borrado: id });
}

async function mios(env, u) {
  const lista = [];
  for (const id of (await idsEnvios(env, u)).slice(-50)) {
    const e = await estadoEnvio(env, u, id);
    if (e) lista.push({ id: e.id, tipo: e.tipo, nombre: e.nombre, fecha: e.fecha, estado: e.estado, motivo: e.motivo || "", enlace: e.enlace || "" });
  }
  lista.sort((a, b) => (a.fecha < b.fecha ? 1 : -1));
  return responder({ envios: lista });
}

async function usoNube(env, u) {
  const objs = await listarTodo(env.PRIVADO, `nube/${u.clave}/`);
  return { usado: objs.reduce((s, o) => s + o.size, 0), objs };
}

async function nube(env, u) {
  const p = await permisos(env, u.clave);
  const { usado, objs } = await usoNube(env, u);
  const proyectos = objs.map((o) => ({ nombre: decodeURIComponent(o.key.split("/")[2]), tam: o.size, fecha: o.uploaded ? new Date(o.uploaded).toISOString() : "" }));
  return responder({ cupo: p.nube_mb * MB, usado, proyectos });
}

async function prepararSubidaNube(env, u, nombre, tam) {
  if (!PROYECTO.test(nombre)) return { error: fallo(400, "Nombre de proyecto no válido") };
  const p = await permisos(env, u.clave);
  if (!p.nube_mb) return { error: fallo(403, "No tienes espacio en la nube. Pide permiso desde el editor.") };
  const clave = `nube/${u.clave}/${encodeURIComponent(nombre)}`;
  const { usado } = await usoNube(env, u);
  const previo = (await env.PRIVADO.head(clave))?.size || 0;
  if (usado - previo + tam > p.nube_mb * MB) return { error: fallo(413, `No cabe: tu espacio en la nube es de ${p.nube_mb} MB`) };
  return { clave };
}

async function subirNube(env, u, nombre, request) {
  const largo = Number(request.headers.get("content-length"));
  if (!Number.isInteger(largo) || largo <= 0 || !request.body) return fallo(411, "Falta el tamaño del archivo");
  if (largo > PARTE_MAX) return fallo(413, "Archivo demasiado grande para una sola subida: súbelo por partes");
  if (!(await gastar(env, u, "nube"))) return fallo(429, "Has llegado al límite de subidas a la nube de hoy");
  const s = await prepararSubidaNube(env, u, nombre, largo);
  if (s.error) return s.error;
  const o = await env.PRIVADO.put(s.clave, request.body);
  return responder({ nombre, tam: o.size });
}

async function bajarNube(env, u, nombre) {
  if (!PROYECTO.test(nombre)) return fallo(400, "Nombre de proyecto no válido");
  const o = await env.PRIVADO.get(`nube/${u.clave}/${encodeURIComponent(nombre)}`);
  if (!o) return fallo(404, "No existe ese proyecto en tu nube");
  return new Response(o.body, { headers: { "content-type": "application/octet-stream", "content-length": String(o.size), "cache-control": "no-store" } });
}

async function borrarNube(env, u, nombre) {
  if (!PROYECTO.test(nombre)) return fallo(400, "Nombre de proyecto no válido");
  await env.PRIVADO.delete(`nube/${u.clave}/${encodeURIComponent(nombre)}`);
  return responder({ borrado: nombre });
}

async function empezarPartes(env, u, request) {
  const p = await cuerpoJSON(request);
  const tam = Number(p?.tam) || 0;
  if (!p || tam <= 0) return fallo(400, "Subida no válida");
  let s, destino;
  if (p.destino === "envio") {
    s = await prepararSubidaEnvio(env, u, String(p.id || ""), String(p.archivo || ""), tam);
    destino = { destino: "envio", id: p.id, archivo: p.archivo };
  } else if (p.destino === "nube") {
    if (!(await gastar(env, u, "nube"))) return fallo(429, "Has llegado al límite de subidas a la nube de hoy");
    s = await prepararSubidaNube(env, u, String(p.nombre || ""), tam);
    destino = { destino: "nube", nombre: p.nombre };
  } else return fallo(400, "Subida no válida");
  if (s.error) return s.error;
  const m = await env.PRIVADO.createMultipartUpload(s.clave);
  const id = nuevoId();
  await guardarJSON(env.PRIVADO, `subidas/${u.clave}/${id}.json`, { ...destino, clave: s.clave, uploadId: m.uploadId, tam, fecha: ahora() });
  return responder({ subida: id, parteMax: PARTE_MAX, parteMin: PARTE_MIN }, 201);
}

async function subirParte(env, u, id, n, request) {
  if (!ID.test(id) || !/^[0-9]{1,5}$/.test(n) || Number(n) < 1 || Number(n) > 10000) return fallo(400, "Parte no válida");
  const s = await leerJSON(env.PRIVADO, `subidas/${u.clave}/${id}.json`, null);
  if (!s) return fallo(404, "No existe esa subida");
  const largo = Number(request.headers.get("content-length"));
  if (!Number.isInteger(largo) || largo <= 0 || largo > PARTE_MAX || !request.body) return fallo(413, "Parte no válida");
  if (Number(n) > Math.ceil(s.tam / PARTE_MAX)) return fallo(413, "La subida pasa del tamaño anunciado");
  const m = env.PRIVADO.resumeMultipartUpload(s.clave, s.uploadId);
  const parte = await m.uploadPart(Number(n), request.body);
  return responder({ n: Number(n), etag: parte.etag });
}

async function terminarPartes(env, u, id, request) {
  if (!ID.test(id)) return fallo(400, "Subida no válida");
  const s = await leerJSON(env.PRIVADO, `subidas/${u.clave}/${id}.json`, null);
  if (!s) return fallo(404, "No existe esa subida");
  const p = await cuerpoJSON(request);
  const partes = Array.isArray(p?.partes) ? p.partes.filter((x) => Number.isInteger(x?.n) && typeof x?.etag === "string").map((x) => ({ partNumber: x.n, etag: x.etag })) : [];
  if (!partes.length) return fallo(400, "Faltan las partes");
  const m = env.PRIVADO.resumeMultipartUpload(s.clave, s.uploadId);
  const o = await m.complete(partes);
  await env.PRIVADO.delete(`subidas/${u.clave}/${id}.json`);
  if (o.size > s.tam) {
    await env.PRIVADO.delete(s.clave);
    return fallo(413, "La subida pasa del tamaño anunciado");
  }
  if (s.destino === "envio") {
    const e = await estadoEnvio(env, u, s.id);
    if (!e || e.estado !== "subiendo") {
      await env.PRIVADO.delete(s.clave);
      return fallo(409, "Ese envío ya está cerrado");
    }
    await apuntarArchivoEnvio(env, u, e, s.archivo, o.size);
    return responder(e);
  }
  return responder({ nombre: s.nombre, tam: o.size });
}

async function cancelarPartes(env, u, id) {
  if (!ID.test(id)) return fallo(400, "Subida no válida");
  const s = await leerJSON(env.PRIVADO, `subidas/${u.clave}/${id}.json`, null);
  if (!s) return fallo(404, "No existe esa subida");
  try {
    await env.PRIVADO.resumeMultipartUpload(s.clave, s.uploadId).abort();
  } catch {}
  await env.PRIVADO.delete(`subidas/${u.clave}/${id}.json`);
  return responder({ cancelada: id });
}

function limpiarMensajesDenuncia(lista) {
  if (!Array.isArray(lista)) return [];
  return lista.slice(0, 50).map((m) => ({
    id: texto(m?.id, 40),
    de: texto(m?.de, 39),
    texto: texto(m?.texto, 2000),
    fecha: texto(m?.fecha, 30),
    llave: typeof m?.llave === "string" && /^[A-Za-z0-9+/=]{1,100}$/.test(m.llave) ? m.llave : "",
  }));
}

async function denunciar(env, u, request) {
  const p = await cuerpoJSON(request);
  const titulo = texto(p?.titulo, 200), cuerpo = texto(p?.cuerpo, 4000);
  if (!titulo) return fallo(400, "Denuncia no válida");
  const tipo = ["juego", "paquete", "cuenta", "comentario", "chat", "mensaje"].includes(p.tipo) ? p.tipo : "juego";
  if (!(await gastar(env, u, "denuncias"))) return fallo(429, "Has llegado al límite de denuncias de hoy");
  const id = nuevoId();
  const d = { id, tipo, titulo, cuerpo, objetivo: texto(p.objetivo, 120), conversacion: texto(p.conversacion, 100), mensajes: limpiarMensajesDenuncia(p.mensajes), autor: u.login, fecha: ahora() };
  await guardarJSON(env.PRIVADO, `denuncias/${id}.json`, d);
  return responder({ id }, 201);
}

async function contadores(env, url) {
  const claves = (url.searchParams.get("claves") || "").split(",").filter((c) => CLAVE.test(c)).slice(0, 100);
  const valores = {};
  await Promise.all(claves.map(async (c) => (valores[c] = Number((await leerJSON(env.R2, `contadores/${c}.json`, {})).value) || 0)));
  return responder(valores);
}

async function avisarHito(env, clave, valor) {
  if (!HITOS.includes(valor) || !clave.endsWith("-g")) return;
  const marca = `hitos/${clave}-${valor}`;
  if (await env.PRIVADO.head(marca)) return;
  await env.PRIVADO.put(marca, "1");
  const r = await buscarPorContador(env, clave.slice(0, -2));
  if (!r) return;
  const que = r.tipo === "juegos" ? "Tu juego" : "Tu paquete";
  await notificar(env, r.e.autor, { tipo: "hito", titulo: `¡${que} ${r.e.nombre} tiene ${valor} me gusta!`, enlace: `${r.tipo}/${r.e.id}` });
}

async function meGusta(env, u, clave, poner) {
  const marca = `megusta/${clave}/${u.clave}`;
  const ya = await env.PRIVADO.head(marca);
  if (poner && !ya) {
    await env.PRIVADO.put(marca, "1");
    const valor = await contar(env, clave, 1);
    await avisarHito(env, clave, valor);
    return responder({ value: valor, mio: true });
  }
  if (!poner && ya) {
    await env.PRIVADO.delete(marca);
    return responder({ value: await contar(env, clave, -1), mio: false });
  }
  return responder({ value: Number((await leerJSON(env.R2, `contadores/${clave}.json`, {})).value) || 0, mio: !!ya });
}

async function misMeGusta(env, u, url) {
  const claves = (url.searchParams.get("claves") || "").split(",").filter((c) => CLAVE.test(c)).slice(0, 100);
  const r = {};
  await Promise.all(claves.map(async (c) => (r[c] = !!(await env.PRIVADO.head(`megusta/${c}/${u.clave}`)))));
  return responder(r);
}

async function comentar(env, u, tipo, id, request) {
  const e = await buscarEnCatalogo(env, tipo, id);
  if (!e) return fallo(404, "No existe");
  const p = await cuerpoJSON(request);
  const t = texto(p?.texto, 500);
  if (!t) return fallo(400, "Escribe algo");
  if (!(await gastar(env, u, "comentarios"))) return fallo(429, "Has llegado al límite de comentarios de hoy");
  const k = `comentarios/${tipo}/${id}.json`;
  const c = await leerJSON(env.R2, k, { comentarios: [] });
  const nuevo = { id: nuevoId(), autor: u.login, texto: t, fecha: ahora() };
  c.comentarios = [...(Array.isArray(c.comentarios) ? c.comentarios : []), nuevo].slice(-300);
  await guardarJSON(env.R2, k, c);
  if (String(e.autor).toLowerCase() !== u.clave)
    await notificar(env, e.autor, { tipo: "comentario", titulo: `${u.login} comentó en ${e.nombre}`, texto: t, enlace: `${tipo}/${id}` });
  return responder(nuevo, 201);
}

async function borrarComentario(env, u, tipo, id, cid) {
  if (!TIPOS.includes(tipo) || !CLAVE.test(id)) return fallo(404, "No existe");
  const c = await cuentas(env);
  if (!c.creadores.includes(u.clave) && !c.admins.includes(u.clave)) return fallo(403, "Solo el equipo de Source 2D puede borrar comentarios");
  const k = `comentarios/${tipo}/${id}.json`;
  const hilo = await leerJSON(env.R2, k, { comentarios: [] });
  hilo.comentarios = (Array.isArray(hilo.comentarios) ? hilo.comentarios : []).filter((x) => x && x.id !== cid);
  await guardarJSON(env.R2, k, hilo);
  return responder({ borrado: cid });
}

async function darFeedback(env, u, tipo, id, request) {
  const e = await buscarEnCatalogo(env, tipo, id);
  if (!e || !LOGIN.test(String(e.autor))) return fallo(404, "No existe");
  const p = await cuerpoJSON(request);
  const t = texto(p?.texto, 1000);
  if (!t) return fallo(400, "Escribe algo");
  if (!(await gastar(env, u, "feedback"))) return fallo(429, "Has llegado al límite de feedback de hoy");
  const fid = invertido() + "-" + aleatorio(4);
  await guardarJSON(env.PRIVADO, `feedback/${String(e.autor).toLowerCase()}/${fid}.json`, { id: fid, de: u.login, tipo, juego: id, nombre: e.nombre, texto: t, fecha: ahora() });
  await notificar(env, e.autor, { tipo: "feedback", titulo: `Feedback nuevo de ${u.login} sobre ${e.nombre}`, texto: t, enlace: `${tipo}/${id}` });
  return responder({ id: fid }, 201);
}

async function feedbackRecibido(env, u) {
  const l = await env.PRIVADO.list({ prefix: `feedback/${u.clave}/`, limit: 100 });
  const lista = await Promise.all(l.objects.map((o) => leerJSON(env.PRIVADO, o.key, null)));
  return responder({ feedback: lista.filter(Boolean) });
}

async function notificaciones(env, u) {
  const l = await env.PRIVADO.list({ prefix: `notificaciones/${u.clave}/`, limit: 50 });
  const lista = await Promise.all(l.objects.map((o) => leerJSON(env.PRIVADO, o.key, null)));
  const leidas = (await leerJSON(env.PRIVADO, `leidas/${u.clave}.json`, {})).hasta || "";
  return responder({ notificaciones: lista.filter(Boolean), leidasHasta: leidas });
}

async function marcarLeidas(env, u) {
  const hasta = ahora();
  await guardarJSON(env.PRIVADO, `leidas/${u.clave}.json`, { hasta });
  return responder({ hasta });
}

async function solicitar(env, u, request) {
  const p = await cuerpoJSON(request);
  if (!p || !["grande", "nube"].includes(p.tipo)) return fallo(400, "Solicitud no válida");
  const motivo = texto(p.motivo, 1500);
  if (!motivo) return fallo(400, "Explica para qué lo necesitas");
  const mb = Math.min(100000, Math.max(1, Math.round(Number(p.mb) || 0)));
  const previas = await listarTodo(env.PRIVADO, `solicitudes/${u.clave}/`);
  for (const o of previas) {
    const s = await leerJSON(env.PRIVADO, o.key, null);
    if (s && s.tipo === p.tipo && s.estado === "pendiente") return fallo(409, "Ya tienes una solicitud de este tipo esperando respuesta");
  }
  if (!(await gastar(env, u, "solicitudes"))) return fallo(429, "Has llegado al límite de solicitudes de hoy");
  const id = nuevoId();
  const s = { id, tipo: p.tipo, mb, motivo, proyecto: texto(p.proyecto, 80), autor: u.login, estado: "pendiente", fecha: ahora() };
  await guardarJSON(env.PRIVADO, `solicitudes/${u.clave}/${id}.json`, s);
  return responder(s, 201);
}

async function misSolicitudes(env, u) {
  const objs = await listarTodo(env.PRIVADO, `solicitudes/${u.clave}/`);
  const lista = (await Promise.all(objs.map((o) => leerJSON(env.PRIVADO, o.key, null)))).filter(Boolean);
  lista.sort((a, b) => (a.fecha < b.fecha ? 1 : -1));
  return responder({ solicitudes: lista });
}

const conversacion = (a, b) => [a.toLowerCase(), b.toLowerCase()].sort().join("__");

async function sigue(env, u, a, b) {
  const url = a.toLowerCase() === u.clave ? `${github(env)}/user/following/${b}` : `${github(env)}/users/${a}/following/${b}`;
  const r = await fetch(url, { headers: cabGitHub(u.token) });
  return r.status === 204;
}

async function apuntarBandeja(env, clave, conv, con, fecha, de) {
  await guardarJSON(env.PRIVADO, `bandeja/${clave}/${conv}.json`, { conversacion: conv, con, ultimo: fecha, de });
}

async function enviarMensaje(env, u, para, request) {
  if (!LOGIN.test(para)) return fallo(400, "Cuenta no válida");
  const destino = await leerJSON(env.R2, `cuentas/${para.toLowerCase()}.json`, null);
  if (!destino) return fallo(404, "Esa cuenta no está en Source 2D");
  if (para.toLowerCase() === u.clave) return fallo(400, "No puedes escribirte a ti");
  const p = await cuerpoJSON(request);
  if (!p || typeof p.cifrado !== "string" || !CIFRADO.test(p.cifrado)) return fallo(400, "Mensaje no válido");
  const conv = conversacion(u.login, destino.login);
  const ek = `conversaciones/${conv}/estado.json`;
  let est = await leerJSON(env.PRIVADO, ek, null);
  const creador = (await cuentas(env)).creadores.includes(u.clave);
  if (est && est.estado === "bloqueada") return fallo(403, "No puedes escribir a esta cuenta");
  if (!creador && !(est && est.estado === "aceptada")) {
    if (est && est.estado === "solicitud") {
      return fallo(409, est.de.toLowerCase() === u.clave ? "Espera a que acepte tu solicitud" : "Acepta su solicitud para contestar");
    }
    if (!(await sigue(env, u, u.login, destino.login)) || !(await sigue(env, u, destino.login, u.login)))
      return fallo(403, "Solo puedes escribir a cuentas que te siguen y a las que sigues");
  }
  if (!(await gastar(env, u, "mensajes"))) return fallo(429, "Has llegado al límite de mensajes de hoy");
  const fecha = ahora();
  if (!est) {
    est = { a: u.login, b: destino.login, estado: creador ? "aceptada" : "solicitud", de: u.login, fecha, aviso: creador };
    await guardarJSON(env.PRIVADO, ek, est);
    if (!creador) await notificar(env, destino.login, { tipo: "solicitud", titulo: `${u.login} quiere hablar contigo`, enlace: `mensajes/${conv}` });
  }
  const id = String(Date.now()).padStart(13, "0") + "-" + aleatorio(4);
  const m = { id, de: u.login, cifrado: p.cifrado, fecha };
  await guardarJSON(env.PRIVADO, `conversaciones/${conv}/m/${id}.json`, m);
  await apuntarBandeja(env, u.clave, conv, destino.login, fecha, u.login);
  await apuntarBandeja(env, destino.login.toLowerCase(), conv, u.login, fecha, u.login);
  if (creador) await notificar(env, destino.login, { tipo: "mensaje", titulo: `Mensaje de ${u.login} (Creador de Source 2D)`, enlace: `mensajes/${conv}` });
  return responder({ ...m, estado: est.estado }, 201);
}

async function bandeja(env, u) {
  const objs = await listarTodo(env.PRIVADO, `bandeja/${u.clave}/`, 500);
  const lista = [];
  for (const o of objs) {
    const b = await leerJSON(env.PRIVADO, o.key, null);
    if (!b) continue;
    const est = await leerJSON(env.PRIVADO, `conversaciones/${b.conversacion}/estado.json`, {});
    lista.push({ ...b, estado: est.estado || "", solicitante: est.de || "", aviso: !!est.aviso });
  }
  lista.sort((a, b) => (a.ultimo < b.ultimo ? 1 : -1));
  return responder({ conversaciones: lista });
}

function enConversacion(u, conv) {
  return /^[a-z0-9-]{1,39}__[a-z0-9-]{1,39}$/.test(conv) && conv.split("__").includes(u.clave);
}

async function leerConversacion(env, u, conv, url) {
  if (!enConversacion(u, conv)) return fallo(404, "No existe");
  const est = await leerJSON(env.PRIVADO, `conversaciones/${conv}/estado.json`, null);
  if (!est) return fallo(404, "No existe");
  const desde = url.searchParams.get("desde") || "";
  const objs = (await listarTodo(env.PRIVADO, `conversaciones/${conv}/m/`, 5000)).filter((o) => o.key.split("/").pop().slice(0, -5) > desde).slice(-200);
  const mensajes = (await Promise.all(objs.map((o) => leerJSON(env.PRIVADO, o.key, null)))).filter(Boolean);
  return responder({ estado: est, mensajes });
}

async function responderSolicitud(env, u, conv, aceptar) {
  if (!enConversacion(u, conv)) return fallo(404, "No existe");
  const ek = `conversaciones/${conv}/estado.json`;
  const est = await leerJSON(env.PRIVADO, ek, null);
  if (!est) return fallo(404, "No existe");
  if (aceptar) {
    if (est.estado !== "solicitud" || est.de.toLowerCase() === u.clave) return fallo(409, "No hay solicitud que aceptar");
    est.estado = "aceptada";
    await notificar(env, est.de, { tipo: "solicitud", titulo: `${u.login} aceptó tu solicitud de mensajes`, enlace: `mensajes/${conv}` });
  } else {
    est.estado = "bloqueada";
    est.bloqueadaPor = u.login;
  }
  est.fecha = ahora();
  await guardarJSON(env.PRIVADO, ek, est);
  return responder(est);
}

async function atender(request, env) {
  const url = new URL(request.url);
  const partes = url.pathname.split("/").filter(Boolean).map((p) => decodeURIComponent(p));
  const [ruta, a, b, c] = partes;
  const m = request.method;
  const n = partes.length;

  if (!ruta && m === "GET") return responder({ servicio: "source2d-api", version: VERSION });
  if (ruta === "registrados" && m === "GET") return registrados(env, url);
  if (ruta === "contadores" && m === "GET") return contadores(env, url);
  if (ruta === "contador" && m === "POST" && n === 2 && CLAVE.test(a)) return responder({ value: await contar(env, a, 1) });

  const conCuenta = ["yo", "registro", "perfil", "envios", "mios", "denuncias", "megusta", "comentarios", "feedback", "notificaciones", "solicitudes", "nube", "partes", "mensajes"];
  if (!conCuenta.includes(ruta)) return fallo(404, "No existe");
  const u = await usuario(request, env);
  if (!u) return fallo(401, "Entra con tu cuenta de GitHub");
  if (ruta === "yo" && m === "GET" && n === 1) return yo(env, u);
  if (m !== "GET") {
    const ban = await baneo(env, u.clave);
    if (ban) return responder({ error: ban.hasta ? `Tu cuenta está baneada hasta el ${ban.hasta.slice(0, 10)}` : "Tu cuenta está baneada para siempre", baneo: ban }, 403);
  }

  if (ruta === "registro" && m === "POST" && n === 1) return registro(env, u);
  if (ruta === "perfil" && m === "PUT" && n === 1) return perfil(env, u, request);
  if (ruta === "mios" && m === "GET" && n === 1) return mios(env, u);
  if (ruta === "denuncias" && m === "POST" && n === 1) return denunciar(env, u, request);
  if (ruta === "megusta" && m === "GET" && n === 1) return misMeGusta(env, u, url);
  if (ruta === "megusta" && n === 2 && CLAVE.test(a) && (m === "POST" || m === "DELETE")) return meGusta(env, u, a, m === "POST");
  if (ruta === "comentarios" && m === "POST" && n === 3) return comentar(env, u, a, b, request);
  if (ruta === "comentarios" && m === "DELETE" && n === 4) return borrarComentario(env, u, a, b, c);
  if (ruta === "feedback" && m === "GET" && n === 1) return feedbackRecibido(env, u);
  if (ruta === "feedback" && m === "POST" && n === 3) return darFeedback(env, u, a, b, request);
  if (ruta === "notificaciones" && m === "GET" && n === 1) return notificaciones(env, u);
  if (ruta === "notificaciones" && m === "POST" && n === 2 && a === "leidas") return marcarLeidas(env, u);
  if (ruta === "solicitudes" && m === "GET" && n === 1) return misSolicitudes(env, u);
  if (ruta === "solicitudes" && m === "POST" && n === 1) return solicitar(env, u, request);
  if (ruta === "nube" && m === "GET" && n === 1) return nube(env, u);
  if (ruta === "nube" && n === 2 && m === "GET") return bajarNube(env, u, a);
  if (ruta === "nube" && n === 2 && m === "PUT") return subirNube(env, u, a, request);
  if (ruta === "nube" && n === 2 && m === "DELETE") return borrarNube(env, u, a);
  if (ruta === "partes" && m === "POST" && n === 1) return empezarPartes(env, u, request);
  if (ruta === "partes" && m === "PUT" && n === 3) return subirParte(env, u, a, b, request);
  if (ruta === "partes" && m === "POST" && n === 3 && b === "fin") return terminarPartes(env, u, a, request);
  if (ruta === "partes" && m === "DELETE" && n === 2) return cancelarPartes(env, u, a);
  if (ruta === "mensajes" && m === "GET" && n === 1) return bandeja(env, u);
  if (ruta === "mensajes" && m === "GET" && n === 2) return leerConversacion(env, u, a, url);
  if (ruta === "mensajes" && m === "POST" && n === 2 && LOGIN.test(a)) return enviarMensaje(env, u, a, request);
  if (ruta === "mensajes" && m === "POST" && n === 3 && b === "aceptar") return responderSolicitud(env, u, a, true);
  if (ruta === "mensajes" && m === "POST" && n === 3 && b === "bloquear") return responderSolicitud(env, u, a, false);
  if (ruta === "envios") {
    if (m === "POST" && n === 1) return nuevoEnvio(env, u, request);
    if (m === "PUT" && n === 3) return subirArchivo(env, u, a, b, request);
    if (m === "POST" && n === 3 && b === "listo") return cerrarEnvio(env, u, a);
    if (m === "DELETE" && n === 2) return borrarEnvio(env, u, a);
  }
  return fallo(405, "Método no permitido");
}

export default {
  async fetch(request, env) {
    try {
      return await atender(request, env);
    } catch (e) {
      return fallo(500, "Error interno");
    }
  },
};
