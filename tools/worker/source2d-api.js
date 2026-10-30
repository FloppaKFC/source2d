const VERSION = 1;
const MB = 1000000;
const ARCHIVOS = {
  juegos: ["info.json", "portada.png", "juego.source2d", "juego.s2dplay", "original.s2dcerrado"],
  sprites: ["info.json", "paquete.s2dpaquete", "portada.png"],
  sonidos: ["info.json", "paquete.s2dpaquete", "portada.png"],
};
const MAX_ARCHIVO = 95 * MB;
const MAX_ENVIO = 200 * MB;
const MAX_PENDIENTES = 3;
const MAX_ENVIOS_DIA = 5;
const MAX_DENUNCIAS_DIA = 10;
const MAX_JSON = 16384;
const CLAVE = /^[a-z0-9][a-z0-9_-]{0,79}$/;
const ID = /^[a-z0-9]{6,16}-[a-f0-9]{8}$/;
const COLOR = /^#[0-9a-fA-F]{6}$/;
const LLAVE = /^[A-Za-z0-9+/]{43}=$/;

const responder = (datos, estado = 200) =>
  new Response(JSON.stringify(datos), {
    status: estado,
    headers: { "content-type": "application/json; charset=utf-8", "cache-control": "no-store" },
  });
const fallo = (estado, mensaje) => responder({ error: mensaje }, estado);
const hoy = () => new Date().toISOString().slice(0, 10);
const ahora = () => new Date().toISOString();
const texto = (v, max) => (typeof v === "string" ? v.trim().slice(0, max) : "");

function aleatorio(n) {
  const b = new Uint8Array(n);
  crypto.getRandomValues(b);
  return [...b].map((x) => x.toString(16).padStart(2, "0")).join("");
}

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

async function usuario(request, env) {
  const m = /^(?:Bearer|token) ([A-Za-z0-9_]{20,255})$/.exec(request.headers.get("authorization") || "");
  if (!m) return null;
  const r = await fetch((env.GITHUB_API || "https://api.github.com") + "/user", {
    headers: { authorization: "Bearer " + m[1], "user-agent": "source2d-api", accept: "application/vnd.github+json" },
  });
  if (r.status !== 200) return null;
  const u = await r.json();
  if (typeof u.login !== "string" || !/^[A-Za-z0-9-]{1,39}$/.test(u.login)) return null;
  return { login: u.login, clave: u.login.toLowerCase() };
}

async function bloqueado(env, u) {
  const b = await leerJSON(env.PRIVADO, "bloqueados.json", {});
  return Array.isArray(b.cuentas) && b.cuentas.map((x) => String(x).toLowerCase()).includes(u.clave);
}

async function gastar(env, u, que, max) {
  const clave = `limites/${u.clave}/${hoy()}.json`;
  const l = await leerJSON(env.PRIVADO, clave, {});
  const n = Number(l[que]) || 0;
  if (n >= max) return false;
  l[que] = n + 1;
  await guardarJSON(env.PRIVADO, clave, l);
  return true;
}

async function contar(env, clave, cuanto) {
  const k = `contadores/${clave}.json`;
  const c = await leerJSON(env.R2, k, {});
  const valor = Math.max(0, (Number(c.value) || 0) + cuanto);
  await guardarJSON(env.R2, k, { value: valor });
  return valor;
}

async function registrados(env, url) {
  const cursor = url.searchParams.get("cursor") || undefined;
  const l = await env.R2.list({ prefix: "cuentas/", cursor, limit: 1000, include: ["customMetadata"] });
  const cuentas = l.objects.map((o) => ({ login: o.customMetadata?.login || o.key.slice(8, -5), unido: o.customMetadata?.unido || "" }));
  return responder({ cuentas, cursor: l.truncated ? l.cursor : null });
}

async function registro(env, u) {
  const clave = `cuentas/${u.clave}.json`;
  const ya = await leerJSON(env.R2, clave, null);
  if (ya) return responder(ya);
  const cuenta = { login: u.login, unido: ahora() };
  await guardarJSON(env.R2, clave, cuenta, { login: u.login, unido: cuenta.unido });
  return responder(cuenta, 201);
}

async function perfil(env, u, request) {
  const p = await cuerpoJSON(request);
  if (!p) return fallo(400, "Perfil no válido");
  const limpio = { login: u.login, actualizado: ahora() };
  const frase = texto(p.frase, 140);
  if (frase) limpio.frase = frase;
  if (typeof p.color === "string" && COLOR.test(p.color)) limpio.color = p.color;
  if (["juegos", "sprites", "sonidos"].includes(p.empezar)) limpio.empezar = p.empezar;
  if (Array.isArray(p.destacados)) limpio.destacados = p.destacados.filter((x) => typeof x === "string" && CLAVE.test(x)).slice(0, 3);
  if (typeof p.clavePublica === "string" && LLAVE.test(p.clavePublica)) limpio.clavePublica = p.clavePublica;
  await guardarJSON(env.R2, `perfiles/${u.clave}.json`, limpio);
  return responder(limpio);
}

async function idsDe(env, u) {
  const ids = [];
  let cursor;
  do {
    const l = await env.PRIVADO.list({ prefix: `envios/${u.clave}/`, delimiter: "/", cursor });
    for (const p of l.delimitedPrefixes) ids.push(p.split("/")[2]);
    cursor = l.truncated ? l.cursor : undefined;
  } while (cursor);
  return ids;
}

async function estadoEnvio(env, u, id) {
  if (!ID.test(id)) return null;
  return leerJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, null);
}

async function nuevoEnvio(env, u, request) {
  const p = await cuerpoJSON(request);
  if (!p || !ARCHIVOS[p.tipo]) return fallo(400, "Envío no válido");
  const nombre = texto(p.nombre, 80);
  if (!nombre) return fallo(400, "Falta el nombre");
  let abiertos = 0;
  for (const id of await idsDe(env, u)) {
    const e = await estadoEnvio(env, u, id);
    if (e && (e.estado === "subiendo" || e.estado === "pendiente")) abiertos++;
  }
  if (abiertos >= MAX_PENDIENTES) return fallo(429, `Ya tienes ${MAX_PENDIENTES} envíos esperando revisión`);
  if (!(await gastar(env, u, "envios", MAX_ENVIOS_DIA))) return fallo(429, "Has llegado al límite de envíos de hoy");
  const id = Date.now().toString(36) + "-" + aleatorio(4);
  const e = { id, tipo: p.tipo, nombre, autor: u.login, fecha: ahora(), estado: "subiendo", archivos: {} };
  await guardarJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, e);
  return responder(e, 201);
}

async function subirArchivo(env, u, id, archivo, request) {
  const e = await estadoEnvio(env, u, id);
  if (!e) return fallo(404, "No existe ese envío");
  if (e.estado !== "subiendo") return fallo(409, "Ese envío ya está cerrado");
  if (!ARCHIVOS[e.tipo].includes(archivo)) return fallo(400, "Archivo no permitido");
  const largo = Number(request.headers.get("content-length"));
  if (!Number.isInteger(largo) || largo <= 0 || !request.body) return fallo(411, "Falta el tamaño del archivo");
  if (largo > MAX_ARCHIVO) return fallo(413, "El archivo pasa de 95 MB");
  const total = Object.entries(e.archivos).reduce((s, [k, v]) => s + (k === archivo ? 0 : v), 0);
  if (total + largo > MAX_ENVIO) return fallo(413, "El envío pasa de 200 MB");
  const o = await env.PRIVADO.put(`envios/${u.clave}/${id}/${archivo}`, request.body);
  e.archivos[archivo] = o.size;
  await guardarJSON(env.PRIVADO, `envios/${u.clave}/${id}/estado.json`, e);
  return responder(e);
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
  const l = await env.PRIVADO.list({ prefix: `envios/${u.clave}/${id}/` });
  if (l.objects.length) await env.PRIVADO.delete(l.objects.map((o) => o.key));
  return responder({ borrado: id });
}

async function mios(env, u) {
  const lista = [];
  for (const id of (await idsDe(env, u)).slice(-50)) {
    const e = await estadoEnvio(env, u, id);
    if (e) lista.push({ id: e.id, tipo: e.tipo, nombre: e.nombre, fecha: e.fecha, estado: e.estado, motivo: e.motivo || "", enlace: e.enlace || "" });
  }
  lista.sort((a, b) => (a.fecha < b.fecha ? 1 : -1));
  return responder({ envios: lista });
}

async function denunciar(env, u, request) {
  const p = await cuerpoJSON(request);
  const titulo = texto(p?.titulo, 200), cuerpo = texto(p?.cuerpo, 4000);
  if (!titulo) return fallo(400, "Denuncia no válida");
  if (!(await gastar(env, u, "denuncias", MAX_DENUNCIAS_DIA))) return fallo(429, "Has llegado al límite de denuncias de hoy");
  const id = Date.now().toString(36) + "-" + aleatorio(4);
  await guardarJSON(env.PRIVADO, `denuncias/${id}.json`, { id, titulo, cuerpo, autor: u.login, fecha: ahora() });
  return responder({ id }, 201);
}

async function contadores(env, url) {
  const claves = (url.searchParams.get("claves") || "").split(",").filter((c) => CLAVE.test(c)).slice(0, 100);
  const valores = {};
  await Promise.all(claves.map(async (c) => (valores[c] = Number((await leerJSON(env.R2, `contadores/${c}.json`, {})).value) || 0)));
  return responder(valores);
}

async function meGusta(env, u, clave, poner) {
  const marca = `megusta/${clave}/${u.clave}`;
  const ya = await env.PRIVADO.head(marca);
  if (poner && !ya) {
    await env.PRIVADO.put(marca, "1");
    return responder({ value: await contar(env, clave, 1), mio: true });
  }
  if (!poner && ya) {
    await env.PRIVADO.delete(marca);
    return responder({ value: await contar(env, clave, -1), mio: false });
  }
  return responder({ value: Number((await leerJSON(env.R2, `contadores/${clave}.json`, {})).value) || 0, mio: !!ya });
}

async function misMeGusta(env, u, url) {
  const claves = (url.searchParams.get("claves") || "").split(",").filter((c) => CLAVE.test(c)).slice(0, 100);
  const mios = {};
  await Promise.all(claves.map(async (c) => (mios[c] = !!(await env.PRIVADO.head(`megusta/${c}/${u.clave}`)))));
  return responder(mios);
}

async function atender(request, env) {
  const url = new URL(request.url);
  const partes = url.pathname.split("/").filter(Boolean);
  const [ruta, a, b] = partes;
  const m = request.method;

  if (!ruta && m === "GET") return responder({ servicio: "source2d-api", version: VERSION });
  if (ruta === "registrados" && m === "GET") return registrados(env, url);
  if (ruta === "contadores" && m === "GET") return contadores(env, url);
  if (ruta === "contador" && m === "POST" && CLAVE.test(a || "") && partes.length === 2) return responder({ value: await contar(env, a, 1) });

  const rutasConCuenta = ["registro", "perfil", "envios", "mios", "denuncias", "megusta"];
  if (!rutasConCuenta.includes(ruta)) return fallo(404, "No existe");
  const u = await usuario(request, env);
  if (!u) return fallo(401, "Entra con tu cuenta de GitHub");
  if (m !== "GET" && (await bloqueado(env, u))) return fallo(403, "Tu cuenta está bloqueada en Source 2D");

  if (ruta === "registro" && m === "POST" && !a) return registro(env, u);
  if (ruta === "perfil" && m === "PUT" && !a) return perfil(env, u, request);
  if (ruta === "mios" && m === "GET" && !a) return mios(env, u);
  if (ruta === "denuncias" && m === "POST" && !a) return denunciar(env, u, request);
  if (ruta === "megusta" && m === "GET" && !a) return misMeGusta(env, u, url);
  if (ruta === "megusta" && CLAVE.test(a || "") && !b && (m === "POST" || m === "DELETE")) return meGusta(env, u, a, m === "POST");
  if (ruta === "envios") {
    if (m === "POST" && !a) return nuevoEnvio(env, u, request);
    if (m === "PUT" && a && b && partes.length === 3) return subirArchivo(env, u, a, b, request);
    if (m === "POST" && a && b === "listo" && partes.length === 3) return cerrarEnvio(env, u, a);
    if (m === "DELETE" && a && !b) return borrarEnvio(env, u, a);
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
