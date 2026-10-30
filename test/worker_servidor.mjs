import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import worker from "../tools/worker/source2d-api.js";

const [puerto, publico, privado, githubApi] = process.argv.slice(2);

class Carpeta {
  constructor(raiz) {
    this.raiz = raiz;
    this.meta = path.join(raiz, "..", path.basename(raiz) + "-meta");
    this.subidas = new Map();
    fs.mkdirSync(raiz, { recursive: true });
    fs.mkdirSync(this.meta, { recursive: true });
  }
  ruta(k) { return path.join(this.raiz, ...k.split("/")); }
  rutaMeta(k) { return path.join(this.meta, encodeURIComponent(k)); }
  obj(k) {
    const r = this.ruta(k);
    if (!fs.existsSync(r) || !fs.statSync(r).isFile()) return null;
    const d = fs.readFileSync(r);
    const m = fs.existsSync(this.rutaMeta(k)) ? JSON.parse(fs.readFileSync(this.rutaMeta(k), "utf8")) : {};
    return {
      key: k, size: d.length, uploaded: fs.statSync(r).mtime, customMetadata: m,
      body: new Response(d).body,
      json: async () => JSON.parse(d.toString("utf8")),
      text: async () => d.toString("utf8"),
    };
  }
  async get(k) { return this.obj(k); }
  async head(k) { return this.obj(k); }
  async bytes(body) {
    if (typeof body === "string") return Buffer.from(body);
    if (body instanceof Uint8Array) return Buffer.from(body);
    return Buffer.from(await new Response(body).arrayBuffer());
  }
  async put(k, body, o = {}) {
    const r = this.ruta(k);
    fs.mkdirSync(path.dirname(r), { recursive: true });
    fs.writeFileSync(r, await this.bytes(body));
    if (o.customMetadata) fs.writeFileSync(this.rutaMeta(k), JSON.stringify(o.customMetadata));
    return this.obj(k);
  }
  async delete(ks) {
    for (const k of [].concat(ks)) {
      fs.rmSync(this.ruta(k), { force: true });
      fs.rmSync(this.rutaMeta(k), { force: true });
    }
  }
  todas() {
    const r = [];
    const andar = (d, pre) => {
      if (!fs.existsSync(d)) return;
      for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        if (e.isDirectory()) andar(path.join(d, e.name), pre + e.name + "/");
        else r.push(pre + e.name);
      }
    };
    andar(this.raiz, "");
    return r.sort();
  }
  async list({ prefix = "", delimiter, cursor, limit = 1000 } = {}) {
    const objects = [], pre = new Set();
    for (const k of this.todas().filter((k) => k.startsWith(prefix))) {
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
    const s = this.subidas.get(uploadId), b = this;
    return {
      async uploadPart(n, body) { s.partes.set(n, await b.bytes(body)); return { partNumber: n, etag: "e" + n }; },
      async complete(partes) {
        const datos = Buffer.concat(partes.sort((x, y) => x.partNumber - y.partNumber).map((p) => s.partes.get(p.partNumber)));
        b.subidas.delete(uploadId);
        return b.put(key, datos);
      },
      async abort() { b.subidas.delete(uploadId); },
    };
  }
}

const env = { R2: new Carpeta(publico), PRIVADO: new Carpeta(privado), GITHUB_API: githubApi };
http.createServer(async (req, res) => {
  const trozos = [];
  for await (const t of req) trozos.push(t);
  const cuerpo = Buffer.concat(trozos);
  const cab = {};
  for (const [k, v] of Object.entries(req.headers)) cab[k] = Array.isArray(v) ? v.join(",") : v;
  cab["content-length"] = String(cuerpo.length);
  delete cab["transfer-encoding"];
  delete cab["expect"];
  const r = await worker.fetch(new Request("http://127.0.0.1" + req.url, { method: req.method, headers: cab, body: ["GET", "HEAD"].includes(req.method) ? undefined : cuerpo, duplex: "half" }), env);
  const salida = Buffer.from(await r.arrayBuffer());
  res.writeHead(r.status, Object.fromEntries(r.headers));
  res.end(salida);
}).listen(Number(puerto), "127.0.0.1", () => console.log("worker de prueba en " + puerto));
