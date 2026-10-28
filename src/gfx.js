/* Source 2D engine - capa nativa del runtime (src/runtime.js) sobre las primitivas de __native (main.c).
   Da al runtime, sin imitar al navegador:
     S2D.surface(w, h)        lienzo en la GPU (render texture) con su contexto 2D: surface.ctx
     S2D.image(w, h, pixels)  imagen de pixeles RGBA (alfa directo) que se sube como textura al dibujarla
     S2D.decodeImage(base64)  PNG, JPG, BMP, GIF o QOI -> imagen
     S2D.display              superficie que se presenta en la ventana cada fotograma
     S2D.onFrame(ms)          lo llama cada fotograma; S2D.onInput(ev) cada tecla o raton (x, y ya en pixeles del juego)
     S2D.store                guardado (archivo JSON del usuario); localStorage es lo mismo para el codigo de los proyectos
   El contexto 2D (Ctx2D) tiene la forma del canvas 2D de siempre porque los proyectos lo usan en su JavaScript
   (shaders de escena en JS), pero todo va a la GPU: imagenes y rectangulos en lote, poligonos en triangulos,
   degradados y filtros CSS con shaders. */
(function () {
'use strict';
const N = __native;
const G = globalThis;

function report(e) { N.print('Source 2D: ' + (e && e.stack ? e.stack : String(e))); }
const say = function () { N.print(Array.prototype.map.call(arguments, String).join(' ')); };
G.console = { log: say, info: say, warn: say, error: say };

/* ---------- temporizadores (los usa el codigo de los proyectos) ---------- */
let tmId = 0, timers = [];
G.setTimeout = (fn, ms) => { timers.push({ id: ++tmId, at: N.now() + (ms || 0), fn, rep: 0 }); return tmId; };
G.setInterval = (fn, ms) => { timers.push({ id: ++tmId, at: N.now() + (ms || 0), fn, rep: ms || 1 }); return tmId; };
G.clearTimeout = (id) => { timers = timers.filter(t => t.id !== id); };
G.clearInterval = G.clearTimeout;
G.performance = { now: () => N.now() };

/* ---------- guardado: un archivo JSON del usuario (__native.storeRead/storeWrite) ---------- */
let store = null;
function storeData() {
  if (store) return store;
  store = {};
  try { const t = N.storeRead(); if (t) store = JSON.parse(t) || {}; } catch (e) { store = {}; }
  return store;
}
function storeSave() { try { N.storeWrite(JSON.stringify(store)); } catch (e) {} }
const Store = {
  getItem(k) { const d = storeData(); k = String(k); return Object.prototype.hasOwnProperty.call(d, k) ? d[k] : null; },
  setItem(k, v) { storeData()[String(k)] = String(v); storeSave(); },
  removeItem(k) { delete storeData()[String(k)]; storeSave(); },
  clear() { store = {}; storeSave(); },
  key(i) { return Object.keys(storeData())[i] ?? null; },
  get length() { return Object.keys(storeData()).length; },
};
G.localStorage = Store;

/* ---------- colores ---------- */
const NAMED = { white: [255, 255, 255, 1], black: [0, 0, 0, 1], red: [255, 0, 0, 1], transparent: [0, 0, 0, 0] };
const colorCache = new Map();
function parseColor(s) {
  if (Array.isArray(s)) return s;
  let c = colorCache.get(s);
  if (c) return c;
  const t = String(s).trim().toLowerCase();
  if (t[0] === '#') {
    let h = t.slice(1);
    if (h.length === 3 || h.length === 4) h = h.split('').map(x => x + x).join('');
    c = [parseInt(h.substr(0, 2), 16) || 0, parseInt(h.substr(2, 2), 16) || 0, parseInt(h.substr(4, 2), 16) || 0,
         h.length === 8 ? parseInt(h.substr(6, 2), 16) / 255 : 1];
  } else {
    const m = /^rgba?\(([^)]*)\)/.exec(t);
    if (m) { const p = m[1].split(',').map(x => parseFloat(x)); c = [p[0] | 0, p[1] | 0, p[2] | 0, p.length > 3 ? p[3] : 1]; }
    else c = NAMED[t] || [0, 0, 0, 1];
  }
  if (colorCache.size > 4096) colorCache.clear();
  colorCache.set(s, c);
  return c;
}
class Gradient {
  constructor(x0, y0, r0, x1, y1, r1) { this.cx = x0; this.cy = y0; this.r0 = r0; this.r1 = r1; this.stops = []; }
  addColorStop(pos, color) {
    const c = parseColor(color);
    this.stops.push([+pos, c[0], c[1], c[2], c[3]]);
    this.stops.sort((a, b) => a[0] - b[0]);
  }
}

/* ---------- geometria ---------- */
function toDev(m, pts) {
  const out = [];
  for (const p of pts) out.push(m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5]);
  return out;
}
function roundPts(x, y, w, h, r) {
  if (r <= 0) return [[x, y], [x + w, y], [x + w, y + h], [x, y + h]];
  const pts = [];
  const seg = (cx, cy, a0) => { for (let i = 0; i <= 6; i++) { const a = a0 + (Math.PI / 2) * i / 6; pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]); } };
  seg(x + w - r, y + r, -Math.PI / 2);
  seg(x + w - r, y + h - r, 0);
  seg(x + r, y + h - r, Math.PI / 2);
  seg(x + r, y + r, Math.PI);
  return pts;
}
/* poligono (x0, y0, x1, y1...) -> abanicos que la GPU rellena: el mismo si es convexo, triangulos (orejas) si no */
function convex(d) {
  const n = d.length / 2;
  let sign = 0;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n, k = (i + 2) % n;
    const cr = (d[2 * j] - d[2 * i]) * (d[2 * k + 1] - d[2 * j + 1]) - (d[2 * j + 1] - d[2 * i + 1]) * (d[2 * k] - d[2 * j]);
    if (Math.abs(cr) < 1e-9) continue;
    if (!sign) sign = cr > 0 ? 1 : -1;
    else if ((cr > 0 ? 1 : -1) !== sign) return false;
  }
  return true;
}
function fans(d) {
  if (d.length < 6) return [];
  if (convex(d)) return [d];
  const idx = [];
  for (let i = 0; i < d.length / 2; i++) idx.push(i);
  let area = 0;
  for (let i = 0; i < idx.length; i++) { const j = (i + 1) % idx.length; area += d[2 * i] * d[2 * j + 1] - d[2 * j] * d[2 * i + 1]; }
  const ccw = area > 0, out = [];
  const X = i => d[2 * i], Y = i => d[2 * i + 1];
  const inTri = (p, a, b, c) => {
    const s1 = (X(b) - X(a)) * (Y(p) - Y(a)) - (Y(b) - Y(a)) * (X(p) - X(a));
    const s2 = (X(c) - X(b)) * (Y(p) - Y(b)) - (Y(c) - Y(b)) * (X(p) - X(b));
    const s3 = (X(a) - X(c)) * (Y(p) - Y(c)) - (Y(a) - Y(c)) * (X(p) - X(c));
    return ccw ? s1 >= 0 && s2 >= 0 && s3 >= 0 : s1 <= 0 && s2 <= 0 && s3 <= 0;
  };
  let guard = 0;
  while (idx.length > 3 && guard++ < 10000) {
    let cut = false;
    for (let i = 0; i < idx.length; i++) {
      const a = idx[(i + idx.length - 1) % idx.length], b = idx[i], c = idx[(i + 1) % idx.length];
      const cr = (X(b) - X(a)) * (Y(c) - Y(b)) - (Y(b) - Y(a)) * (X(c) - X(b));
      if (ccw ? cr <= 0 : cr >= 0) continue;
      let ear = true;
      for (const p of idx) if (p !== a && p !== b && p !== c && inTri(p, a, b, c)) { ear = false; break; }
      if (!ear) continue;
      out.push([X(a), Y(a), X(b), Y(b), X(c), Y(c)]);
      idx.splice(i, 1);
      cut = true;
      break;
    }
    if (!cut) break;                                   /* poligono que se cruza: lo que quede, en abanico */
  }
  if (idx.length >= 3) { const r = []; for (const i of idx) r.push(X(i), Y(i)); out.push(r); }
  return out;
}

const MODES = { 'source-over': 0, 'lighter': 1, 'copy': 2, 'destination-out': 3 };

/* ---------- cola de dibujos de la GPU ----------
   Imagenes y rectangulos se escriben aqui y se mandan juntos (N.gpuBatch). Antes de cualquier otra llamada a la
   GPU (subir texturas, poligonos, filtros, shaders, leer, presentar) se vacia, para respetar el orden. */
const QCAP = 20 * 8192, Q = new Float64Array(QCAP);
let qn = 0;
function qflush() { if (qn) { N.gpuBatch(Q.buffer, qn); qn = 0; } }
function qDraw(t, k, id, sx, sy, sw, sh, dx, dy, dw, dh, a, b, c, d, e, f, alpha, mode) {
  if (qn + 20 > QCAP) qflush();
  const i = qn;
  Q[i] = 1; Q[i + 1] = t; Q[i + 2] = k; Q[i + 3] = id; Q[i + 4] = sx; Q[i + 5] = sy; Q[i + 6] = sw; Q[i + 7] = sh;
  Q[i + 8] = dx; Q[i + 9] = dy; Q[i + 10] = dw; Q[i + 11] = dh; Q[i + 12] = a; Q[i + 13] = b; Q[i + 14] = c; Q[i + 15] = d;
  Q[i + 16] = e; Q[i + 17] = f; Q[i + 18] = alpha; Q[i + 19] = mode;
  qn = i + 20;
}
function qRect(t, x0, y0, x1, y1, r, g, b, a, mode) {
  if (qn + 11 > QCAP) qflush();
  const i = qn;
  Q[i] = 2; Q[i + 1] = t; Q[i + 2] = x0; Q[i + 3] = y0; Q[i + 4] = x1; Q[i + 5] = y1; Q[i + 6] = r; Q[i + 7] = g; Q[i + 8] = b; Q[i + 9] = a; Q[i + 10] = mode;
  qn = i + 11;
}
/* las demas llamadas de la GPU (tambien las del runtime, como gpuShader) vacian antes la cola */
for (const name of ['gpuTex', 'gpuTexFree', 'gpuFill', 'gpuFillGrad', 'gpuShader', 'gpuColor', 'gpuBlur', 'gpuRead', 'gpuPresent', 'gpuTargetFree', 'gpuTarget']) {
  const f = N[name];
  if (f) N[name] = function () { qflush(); return f.apply(N, arguments); };
}

/* ---------- imagenes: pixeles en la CPU, textura en la GPU ---------- */
const texReg = typeof FinalizationRegistry !== 'undefined' ? new FinalizationRegistry(h => {
  if (h.tex >= 0) N.gpuTexFree(h.tex);
  if (h.rt >= 0) N.gpuTargetFree(h.rt);
}) : null;
let uid = 0;
class Img {
  /* pixels: ArrayBuffer o vista con w*h*4 bytes RGBA (alfa directo); sin pixels, transparente */
  constructor(w, h, pixels) {
    this._w = Math.max(1, w | 0); this._h = Math.max(1, h | 0);
    const n = this._w * this._h * 4;
    /* data empieza siempre en el byte 0 de su bufer (asi se sube tal cual) */
    if (pixels instanceof ArrayBuffer && pixels.byteLength === n) this.data = new Uint8ClampedArray(pixels);
    else { this.data = new Uint8ClampedArray(n); if (pixels) this.data.set(new Uint8Array(pixels.buffer || pixels, pixels.byteOffset || 0, n)); }
    this._ver = 1; this._h_ = { tex: -1, rt: -1, ver: 0 }; this._uid = ++uid;
    if (texReg) texReg.register(this, this._h_);
  }
  get width() { return this._w; }
  get height() { return this._h; }
  /* tras cambiar data: la textura se vuelve a subir al dibujarla */
  update() { this._ver++; }
  _tex() {
    const h = this._h_;
    if (h.tex < 0 || h.ver !== this._ver) { h.tex = N.gpuTex(h.tex, this.data.buffer, this._w, this._h); h.ver = this._ver; }
    return h.tex;
  }
}

/* ---------- superficies: lienzos en la GPU ---------- */
class Surface {
  constructor(w, h) {
    this._w = Math.max(1, w | 0); this._h = Math.max(1, h | 0);
    this._h_ = { tex: -1, rt: N.gpuTarget(this._w, this._h) };
    this._gpu = this._h_.rt;
    this._uid = ++uid;
    this.ctx = new Ctx2D(this);
    if (texReg) texReg.register(this, this._h_);
  }
  get width() { return this._w; }
  get height() { return this._h; }
  getContext() { return this.ctx; }
  /* cambiar el tamano la deja transparente */
  resize(w, h) {
    w = Math.max(1, w | 0); h = Math.max(1, h | 0);
    if (w === this._w && h === this._h) return;
    if (this._gpu >= 0) N.gpuTargetFree(this._gpu);
    this._w = w; this._h = h;
    this._gpu = this._h_.rt = N.gpuTarget(w, h);
  }
  free() { if (this._gpu >= 0) N.gpuTargetFree(this._gpu); this._gpu = this._h_.rt = -1; }
}

/* lienzo auxiliar para dibujar con filtro (uno por tamano; nunca es el destino) */
const temps = new Map();
function tempFor(w, h) {
  const k = w + 'x' + h;
  let t = temps.get(k);
  if (!t) temps.set(k, t = new Surface(w, h));
  return t;
}

/* textos ya rasterizados (text.c) como texturas, los ultimos 512 */
const textTex = new Map();
function textTexture(key, r) {
  let e = textTex.get(key);
  if (e) { textTex.delete(key); textTex.set(key, e); return e; }
  e = { id: N.gpuTex(-1, r.buf, r.w, r.h), w: r.w, h: r.h, asc: r.asc };
  textTex.set(key, e);
  if (textTex.size > 512) { const k0 = textTex.keys().next().value, e0 = textTex.get(k0); textTex.delete(k0); N.gpuTexFree(e0.id); }
  return e;
}

/* ---------- filtros CSS (subconjunto) ---------- */
function pct(arg, def) {
  if (arg === '') return def;
  if (arg.endsWith('%')) return parseFloat(arg) / 100;
  const v = parseFloat(arg);
  return isNaN(v) ? def : v;
}
const clamp01 = v => (v < 0 ? 0 : v > 1 ? 1 : v);
const COLOR_OPS = { grayscale: 0, sepia: 1, saturate: 2, 'hue-rotate': 3, invert: 4, brightness: 5, contrast: 6, opacity: 7 };
const filterCache = new Map();
/* "grayscale(0.5) blur(2px)" -> [[tipo, cantidad]...] (tipo 8 = blur); margen = lo que el desenfoque se extiende */
function parseFilter(str) {
  let f = filterCache.get(str);
  if (f) return f;
  const ops = [], re = /([a-z-]+)\(([^)]*)\)/g;
  let m, margin = 0;
  while ((m = re.exec(str))) {
    const name = m[1], arg = m[2].trim();
    if (name === 'blur') { const r = parseFloat(arg) || 0; if (r > 0) { ops.push([8, r]); margin += Math.ceil(r * 3) + 2; } continue; }
    if (!(name in COLOR_OPS)) continue;                 /* drop-shadow y otros: no soportados */
    const k = COLOR_OPS[name];
    const v = k === 3 ? (parseFloat(arg) || 0) : k <= 1 || k === 4 || k === 7 ? clamp01(pct(arg, 1)) : pct(arg, 1);
    ops.push([k, v]);
  }
  f = { ops, margin };
  if (filterCache.size > 256) filterCache.clear();
  filterCache.set(str, f);
  return f;
}

/* ---------- contexto 2D ---------- */
class Ctx2D {
  constructor(cv) {
    this.canvas = cv;
    this._stack = [];
    this._depth = 0;
    this._path = [];
    this._cur = null;
    this.m = [1, 0, 0, 1, 0, 0];
    this.fillStyle = '#000000';
    this.strokeStyle = '#000000';
    this.globalAlpha = 1;
    this.globalCompositeOperation = 'source-over';
    this.filter = 'none';
    this.lineWidth = 1;
    this.font = '10px sans-serif';
    this.textAlign = 'start';
    this.textBaseline = 'alphabetic';
    this.imageSmoothingEnabled = false;
  }
  /* save/restore sin crear objetos en cada llamada (miles por fotograma): estados reutilizados de una pila */
  save() {
    const d = ++this._depth;
    let st = this._stack[d - 1];
    if (!st) st = this._stack[d - 1] = { m: [1, 0, 0, 1, 0, 0] };
    const m = this.m, sm = st.m;
    sm[0] = m[0]; sm[1] = m[1]; sm[2] = m[2]; sm[3] = m[3]; sm[4] = m[4]; sm[5] = m[5];
    st.fillStyle = this.fillStyle; st.strokeStyle = this.strokeStyle; st.globalAlpha = this.globalAlpha;
    st.globalCompositeOperation = this.globalCompositeOperation; st.filter = this.filter; st.lineWidth = this.lineWidth;
    st.font = this.font; st.textAlign = this.textAlign; st.textBaseline = this.textBaseline; st.imageSmoothingEnabled = this.imageSmoothingEnabled;
  }
  restore() {
    const d = this._depth;
    if (d <= 0) return;
    const st = this._stack[d - 1];
    this._depth = d - 1;
    const m = this.m, sm = st.m;
    m[0] = sm[0]; m[1] = sm[1]; m[2] = sm[2]; m[3] = sm[3]; m[4] = sm[4]; m[5] = sm[5];
    this.fillStyle = st.fillStyle; this.strokeStyle = st.strokeStyle; this.globalAlpha = st.globalAlpha;
    this.globalCompositeOperation = st.globalCompositeOperation; this.filter = st.filter; this.lineWidth = st.lineWidth;
    this.font = st.font; this.textAlign = st.textAlign; this.textBaseline = st.textBaseline; this.imageSmoothingEnabled = st.imageSmoothingEnabled;
  }

  /* la matriz se cambia en su sitio (this.m no se comparte: save copia los numeros) */
  setTransform(a, b, c, d, e, f) { const m = this.m; m[0] = a; m[1] = b; m[2] = c; m[3] = d; m[4] = e; m[5] = f; }
  resetTransform() { this.setTransform(1, 0, 0, 1, 0, 0); }
  transform(a, b, c, d, e, f) {
    const m = this.m, m0 = m[0], m1 = m[1], m2 = m[2], m3 = m[3];
    m[4] += m0 * e + m2 * f; m[5] += m1 * e + m3 * f;
    m[0] = m0 * a + m2 * b; m[1] = m1 * a + m3 * b;
    m[2] = m0 * c + m2 * d; m[3] = m1 * c + m3 * d;
  }
  translate(x, y) { const m = this.m; m[4] += m[0] * x + m[2] * y; m[5] += m[1] * x + m[3] * y; }
  scale(x, y) { const m = this.m; m[0] *= x; m[1] *= x; m[2] *= y; m[3] *= y; }
  rotate(r) { const c = Math.cos(r), s = Math.sin(r); this.transform(c, s, -s, c, 0, 0); }
  setLineDash() {}
  getLineDash() { return []; }

  beginPath() { this._path = []; this._cur = null; }
  moveTo(x, y) { this._cur = [[x, y]]; this._path.push(this._cur); }
  lineTo(x, y) { if (!this._cur) { this.moveTo(x, y); return; } this._cur.push([x, y]); }
  closePath() {}
  rect(x, y, w, h) { this._path.push([[x, y], [x + w, y], [x + w, y + h], [x, y + h]]); this._cur = null; }
  roundRect(x, y, w, h, r) {
    const rr = Math.max(0, Math.min(Array.isArray(r) ? r[0] : r, w / 2, h / 2));
    this._path.push(roundPts(x, y, w, h, rr));
    this._cur = null;
  }
  arc(cx, cy, r, a0, a1, ccw) {
    let span = a1 - a0;
    if (ccw && span > 0) span -= 2 * Math.PI;
    if (!ccw && span < 0) span += 2 * Math.PI;
    const steps = Math.max(8, Math.ceil(Math.abs(span) * r / 2)), pts = [];
    for (let i = 0; i <= steps; i++) { const a = a0 + span * i / steps; pts.push([cx + r * Math.cos(a), cy + r * Math.sin(a)]); }
    this._path.push(pts);
    this._cur = null;
  }

  _mode() { return MODES[this.globalCompositeOperation] ?? 0; }
  _filtered() { return this.filter && this.filter !== 'none'; }
  /* dibujo con filtro: draw(t, alpha, mode) pinta en un lienzo auxiliar limpio, los filtros se aplican en la GPU a la
     zona bbox (dispositivo) y el resultado se compone en este lienzo con la opacidad y el modo actuales */
  _withFilter(draw, bbox) {
    const cv = this.canvas, w = cv._w, h = cv._h, f = parseFilter(this.filter);
    const mg = f.margin;
    const x0 = Math.max(0, Math.floor(bbox[0]) - mg), y0 = Math.max(0, Math.floor(bbox[1]) - mg);
    const x1 = Math.min(w, Math.ceil(bbox[2]) + mg), y1 = Math.min(h, Math.ceil(bbox[3]) + mg);
    if (x1 <= x0 || y1 <= y0) return;
    const t = tempFor(w, h)._gpu;
    qRect(t, x0, y0, x1, y1, 0, 0, 0, 0, 2);
    draw(t, 1, 0);
    for (const op of f.ops) {
      if (op[0] === 8) N.gpuBlur(t, op[1], x0, y0, x1, y1);
      else N.gpuColor(t, op[0], op[1], x0, y0, x1, y1);
    }
    qDraw(cv._gpu, 1, t, x0, y0, x1 - x0, y1 - y0, x0, y0, x1 - x0, y1 - y0, 1, 0, 0, 1, 0, 0, this.globalAlpha, this._mode());
  }
  _bboxOf(x, y, w, h) {
    const m = this.m;
    const xa = m[0] * x + m[2] * y + m[4], xb = m[0] * (x + w) + m[2] * y + m[4];
    const xc = m[0] * x + m[2] * (y + h) + m[4], xd = m[0] * (x + w) + m[2] * (y + h) + m[4];
    const ya = m[1] * x + m[3] * y + m[5], yb = m[1] * (x + w) + m[3] * y + m[5];
    const yc = m[1] * x + m[3] * (y + h) + m[5], yd = m[1] * (x + w) + m[3] * (y + h) + m[5];
    return [Math.min(xa, xb, xc, xd), Math.min(ya, yb, yc, yd), Math.max(xa, xb, xc, xd) + 1, Math.max(ya, yb, yc, yd) + 1];
  }

  _fillPolys(polys, style) {
    const devs = polys.map(p => toDev(this.m, p)).filter(d => d.length >= 6);
    if (!devs.length) return;
    let grad = null, r = 0, g = 0, b = 0, a = 1;
    if (style instanceof Gradient) {
      /* el degradado se define en coordenadas del usuario: centro y radios pasan por la transformacion */
      const m = this.m, sc = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2])) || 1;
      grad = [m[0] * style.cx + m[2] * style.cy + m[4], m[1] * style.cx + m[3] * style.cy + m[5], style.r0 * sc, style.r1 * sc, style.stops.length];
      for (const st of style.stops) grad.push(st[0], st[1], st[2], st[3], st[4]);
    } else { const c = parseColor(style); r = c[0]; g = c[1]; b = c[2]; a = c[3]; }
    const paint = (t, alpha, mode) => {
      for (const d of devs) for (const f of fans(d)) {
        if (grad) N.gpuFillGrad(t, f, grad, alpha, mode);
        else N.gpuFill(t, f, r, g, b, a * alpha, mode);
      }
    };
    if (!this._filtered()) { paint(this.canvas._gpu, this.globalAlpha, this._mode()); return; }
    let bb = null;
    for (const d of devs) for (let i = 0; i < d.length; i += 2) {
      if (!bb) bb = [d[i], d[i + 1], d[i], d[i + 1]];
      bb[0] = Math.min(bb[0], d[i]); bb[2] = Math.max(bb[2], d[i] + 1);
      bb[1] = Math.min(bb[1], d[i + 1]); bb[3] = Math.max(bb[3], d[i + 1] + 1);
    }
    this._withFilter(paint, bb);
  }
  fill() { this._fillPolys(this._path, this.fillStyle); }
  stroke() {}
  /* rectangulo con el color ya en numeros (r, g, b 0-255, a 0-1), sin analizar texto: para dibujar muchos
     (particulas). lighter: suma de color. */
  fillRectRGBA(x, y, w, h, r, g, b, a, lighter) {
    const m = this.m;
    if (m[1] === 0 && m[2] === 0 && !this._filtered()) {
      qRect(this.canvas._gpu, m[0] * x + m[4], m[3] * y + m[5], m[0] * (x + w) + m[4], m[3] * (y + h) + m[5], r, g, b, a * this.globalAlpha, lighter ? 1 : 0);
      return;
    }
    const sv = this.fillStyle, so = this.globalAlpha, sc = this.globalCompositeOperation;
    this.fillStyle = [r, g, b, 1]; this.globalAlpha = so * a; this.globalCompositeOperation = lighter ? 'lighter' : 'source-over';
    this.fillRect(x, y, w, h);
    this.fillStyle = sv; this.globalAlpha = so; this.globalCompositeOperation = sc;
  }
  fillRect(x, y, w, h) {
    const m = this.m;
    if (m[1] === 0 && m[2] === 0 && !(this.fillStyle instanceof Gradient)) {
      const c = parseColor(this.fillStyle);
      const X0 = m[0] * x + m[4], X1 = m[0] * (x + w) + m[4], Y0 = m[3] * y + m[5], Y1 = m[3] * (y + h) + m[5];
      const paint = (t, alpha, mode) => qRect(t, X0, Y0, X1, Y1, c[0], c[1], c[2], c[3] * alpha, mode);
      if (!this._filtered()) paint(this.canvas._gpu, this.globalAlpha, this._mode());
      else this._withFilter(paint, [Math.min(X0, X1), Math.min(Y0, Y1), Math.max(X0, X1), Math.max(Y0, Y1)]);
      return;
    }
    this._fillPolys([[[x, y], [x + w, y], [x + w, y + h], [x, y + h]]], this.fillStyle);
  }
  strokeRect(x, y, w, h) {
    const t = this.lineWidth / 2;
    const q = (x0, y0, x1, y1) => [[x0, y0], [x1, y0], [x1, y1], [x0, y1]];
    this._fillPolys([q(x - t, y - t, x + w + t, y + t), q(x - t, y + h - t, x + w + t, y + h + t),
                     q(x - t, y - t, x + t, y + h + t), q(x + w - t, y - t, x + w + t, y + h + t)], this.strokeStyle);
  }
  clearRect(x, y, w, h) {
    const m = this.m, t = this.canvas._gpu;
    if (m[1] === 0 && m[2] === 0) qRect(t, m[0] * x + m[4], m[3] * y + m[5], m[0] * (x + w) + m[4], m[3] * (y + h) + m[5], 0, 0, 0, 0, 2);
    else N.gpuFill(t, toDev(m, [[x, y], [x + w, y], [x + w, y + h], [x, y + h]]), 0, 0, 0, 0, 2);
  }

  /* sprite centrado en (x, y), girado rad, escalado (sx, sy) y con opacidad alpha: lo mismo que save + translate +
     rotate + scale + drawImage + restore, pero en una sola llamada (son miles por fotograma) */
  drawSprite(img, x, y, rad, sx, sy, alpha) {
    const src = img._gpu !== undefined ? img._gpu : img._tex();
    if (src < 0) return;
    const m = this.m, w = img._w, h = img._h;
    let co = sx, s1 = 0, s2 = 0, ci = sy;
    if (rad) { const c = Math.cos(rad), s = Math.sin(rad); co = c * sx; s1 = s * sx; s2 = -s * sy; ci = c * sy; }
    const a = m[0] * co + m[2] * s1, b = m[1] * co + m[3] * s1, c2 = m[0] * s2 + m[2] * ci, d = m[1] * s2 + m[3] * ci;
    const e = m[4] + m[0] * x + m[2] * y, f = m[5] + m[1] * x + m[3] * y;
    if (this._filtered()) {
      this.save(); this.setTransform(a, b, c2, d, e, f); this.globalAlpha *= alpha;
      this.drawImage(img, -w / 2, -h / 2); this.restore(); return;
    }
    qDraw(this.canvas._gpu, img._gpu !== undefined ? 1 : 0, src, 0, 0, w, h, -w / 2, -h / 2, w, h, a, b, c2, d, e, f, this.globalAlpha * alpha, this._mode());
  }
  /* img: Img (pixeles) o Surface (lienzo de la GPU) */
  drawImage(img, a1, a2, a3, a4, a5, a6, a7, a8) {
    if (!img) return;
    let sx = 0, sy = 0, sw = img._w, sh = img._h, dx, dy, dw, dh;
    /* sin 'arguments': evitar un objeto nuevo en cada llamada (son miles por fotograma) */
    if (a3 === undefined) { dx = a1; dy = a2; dw = sw; dh = sh; }
    else if (a5 === undefined) { dx = a1; dy = a2; dw = a3; dh = a4; }
    else { sx = a1; sy = a2; sw = a3; sh = a4; dx = a5; dy = a6; dw = a7; dh = a8; }
    const rt = img._gpu !== undefined, src = rt ? img._gpu : img._tex();
    if (src < 0) return;
    const m = this.m;
    const paint = (t, alpha, mode) => qDraw(t, rt ? 1 : 0, src, sx, sy, sw, sh, dx, dy, dw, dh, m[0], m[1], m[2], m[3], m[4], m[5], alpha, mode);
    if (!this._filtered()) paint(this.canvas._gpu, this.globalAlpha, this._mode());
    else this._withFilter(paint, this._bboxOf(dx, dy, dw, dh));
  }

  _fontSize() { const m = /(\d+(?:\.\d+)?)px/.exec(this.font); return m ? parseFloat(m[1]) : 10; }
  _bold() { return /bold/.test(this.font); }
  /* "s2d:N" en la familia elige una fuente cargada con __native.fontLoad (0 = la embebida) */
  _fontSlot() { const m = /s2d:(\d+)/.exec(this.font); return m ? +m[1] : 0; }
  measureText(t) { return { width: N.textWidth(String(t), this._fontSize(), this._bold(), this._fontSlot()) }; }
  fillText(text, x, y) {
    const s = String(text);
    if (!s.length) return;
    const c = parseColor(this.fillStyle instanceof Gradient ? '#000' : this.fillStyle);
    const key = s + '|' + this.font + '|' + c[0] + ',' + c[1] + ',' + c[2];
    let tt = textTex.get(key);
    if (!tt) {
      const r = N.textRender(s, this._fontSize(), this._bold(), c[0], c[1], c[2], this._fontSlot());
      if (!r || r.w <= 0 || r.h <= 0) return;
      tt = textTexture(key, r);
    } else { textTex.delete(key); textTex.set(key, tt); }
    let ax = x, ay = y;
    if (this.textAlign === 'center') ax = x - tt.w / 2;
    else if (this.textAlign === 'right' || this.textAlign === 'end') ax = x - tt.w;
    switch (this.textBaseline) {
      case 'middle': ay = y - tt.h / 2; break;
      case 'top': case 'hanging': ay = y; break;
      case 'bottom': case 'ideographic': ay = y - tt.h; break;
      default: ay = y - tt.asc;
    }
    const m = this.m;
    const paint = (t, alpha, mode) => qDraw(t, 0, tt.id, 0, 0, tt.w, tt.h, ax, ay, tt.w, tt.h, m[0], m[1], m[2], m[3], m[4], m[5], alpha * c[3], mode);
    if (!this._filtered()) paint(this.canvas._gpu, this.globalAlpha, this._mode());
    else this._withFilter(paint, this._bboxOf(ax, ay, tt.w, tt.h));
  }

  createRadialGradient(x0, y0, r0, x1, y1, r1) { return new Gradient(x0, y0, r0, x1, y1, r1); }

  /* pixeles: se leen de la GPU (lento: solo para casos raros) */
  getImageData(x, y, w, h) {
    w |= 0; h |= 0;
    return { width: w, height: h, data: new Uint8ClampedArray(N.gpuRead(this.canvas._gpu, x | 0, y | 0, w, h)) };
  }
  createImageData(w, h) { w |= 0; h |= 0; return { width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }; }
  putImageData(img, x, y) {
    const p = new Img(img.width, img.height, img.data);
    qDraw(this.canvas._gpu, 0, p._tex(), 0, 0, p._w, p._h, x | 0, y | 0, p._w, p._h, 1, 0, 0, 1, 0, 0, 1, 2);
  }
}

/* ---------- S2D: lo que usa el runtime ---------- */
let lastRect = null;          /* [x, y, w, h] del area de juego en la ventana */
const S2D = G.S2D = {
  surface: (w, h) => new Surface(w, h),
  image: (w, h, pixels) => new Img(w, h, pixels),
  decodeImage(b64) {
    let r = null;
    try { r = N.imageDecode(String(b64 || '')); } catch (e) { r = null; }
    return r ? new Img(r[0], r[1], r[2]) : null;
  },
  isImage: x => x instanceof Img || x instanceof Surface,
  parseColor,
  display: null,
  onFrame: null,
  onInput: null,
  store: Store,
  setFullscreen: on => N.setFullscreen(!!on),
  isFullscreen: () => !!N.isFullscreen(),
};

/* ---------- puntos de entrada llamados desde C ---------- */
G.__dispatch = function (type, key, rep, x, y, btn) {
  if (!S2D.onInput) return;
  const d = S2D.display, r = lastRect;
  let gx = 0, gy = 0, inside = false;
  if (d && r && r[2] > 0 && r[3] > 0) {
    gx = (x - r[0]) / r[2] * d._w; gy = (y - r[1]) / r[3] * d._h;
    inside = x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3];
  }
  /* el raton solo cuenta dentro del area de juego (salvo al soltar el boton) */
  if ((type === 'mousedown' || type === 'mousemove' || type === 'wheel') && !inside) return;
  try { S2D.onInput({ type, key, repeat: !!rep, x: gx, y: gy, button: btn }); } catch (e) { report(e); }
};

G.__frame = function (now) {
  if (timers.length) {
    const due = timers.filter(t => t.at <= now);
    timers = timers.filter(t => t.at > now);
    for (const t of due) {
      if (t.rep) { t.at = now + t.rep; timers.push(t); }
      try { t.fn(); } catch (e) { report(e); }
    }
  }
  if (S2D.onFrame) { try { S2D.onFrame(now); } catch (e) { report(e); } }
  const d = S2D.display;
  if (d && d._gpu >= 0) lastRect = N.gpuPresent(d._gpu, d._w, d._h) || null;
  else qflush();
};
})();
