/* Source 2D engine - capa del runtime (src/runtime.js) para el navegador: la exportacion HTML.
   Da lo mismo que src/gfx.js y __native en el jugador nativo, pero con lo del navegador:
     S2D.surface / S2D.image   lienzos <canvas> 2D (el contexto 2D del navegador ya es el que espera el runtime)
     N.gpuShader               shaders GLSL de capas y de pantalla con WebGL
     N.music* / sfx / tone     audio con <audio> y Web Audio
     N.file* / dirList / paths un sistema de archivos virtual guardado en localStorage (partidas, niveles del jugador)
   La pagina (src/editor/exporter.cpp) pone el proyecto en <script type="application/json" id="s2d-proyecto">. */
(function () {
'use strict';
const G = window;

/* ---------- colores (como en gfx.js) ---------- */
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

/* ---------- lienzos ---------- */
const surfaces = [];                 /* numero (_gpu) -> lienzo, para N.gpuShader */
function prepare(cv, w, h) {
  cv.width = Math.max(1, w | 0); cv.height = Math.max(1, h | 0);
  const ctx = cv.getContext('2d', { willReadFrequently: false });
  ctx.imageSmoothingEnabled = false;
  /* el runtime mira ctx.m (la matriz actual) para grosores de contorno y zonas de shaders */
  Object.defineProperty(ctx, 'm', { get() { const t = this.getTransform(); return [t.a, t.b, t.c, t.d, t.e, t.f]; } });
  cv.ctx = ctx;
  Object.defineProperty(cv, '_w', { get() { return this.width; } });
  Object.defineProperty(cv, '_h', { get() { return this.height; } });
  cv._uid = surfaces.length + 1;
  return cv;
}
function surface(w, h) {
  const cv = prepare(document.createElement('canvas'), w, h);
  cv._gpu = surfaces.length;
  surfaces.push(cv);
  cv.resize = (W, H) => { cv.width = Math.max(1, W | 0); cv.height = Math.max(1, H | 0); cv.ctx.imageSmoothingEnabled = false; };
  cv.free = () => { surfaces[cv._gpu] = null; cv.width = cv.height = 1; };
  return cv;
}
/* imagen de pixeles RGBA (alfa directo): data se puede cambiar y luego update() */
function image(w, h, pixels) {
  const cv = prepare(document.createElement('canvas'), w, h);
  const n = cv.width * cv.height * 4;
  cv.data = new Uint8ClampedArray(n);
  if (pixels) cv.data.set(new Uint8Array(pixels.buffer || pixels, pixels.byteOffset || 0, Math.min(n, pixels.byteLength || pixels.length)));
  cv.update = () => cv.ctx.putImageData(new ImageData(cv.data, cv.width, cv.height), 0, 0);
  cv.update();
  cv.free = () => {};
  return cv;
}
/* PNG, JPG... en base64: el navegador decodifica sin esperar; el lienzo toma su tamano al cargar */
function decodeImage(b64) {
  const cv = prepare(document.createElement('canvas'), 1, 1);
  const im = new Image();
  im.onload = () => { cv.width = im.width; cv.height = im.height; cv.ctx.imageSmoothingEnabled = false; cv.ctx.drawImage(im, 0, 0); };
  im.src = String(b64).startsWith('data:') ? b64 : 'data:image/png;base64,' + b64;
  cv.free = () => {};
  return cv;
}

/* ---------- shaders GLSL con WebGL (de un lienzo al mismo lienzo) ---------- */
const VERT = 'attribute vec2 a_pos; varying vec2 v_uv; void main() { v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5); gl_Position = vec4(a_pos, 0.0, 1.0); }';
let gl = null, glCanvas = null, glTex = null, glFailed = false, amount = 1;
const programs = new Map();
function glInit() {
  if (gl || glFailed) return gl;
  glCanvas = document.createElement('canvas');
  try { gl = glCanvas.getContext('webgl', { premultipliedAlpha: true, preserveDrawingBuffer: true, antialias: false, alpha: true }); } catch (e) { gl = null; }
  if (!gl) { glFailed = true; console.warn('Source 2D: este navegador no tiene WebGL; los shaders no se aplican'); return null; }
  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
  glTex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, glTex);
  for (const [p, v] of [[gl.TEXTURE_MIN_FILTER, gl.NEAREST], [gl.TEXTURE_MAG_FILTER, gl.NEAREST], [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]])
    gl.texParameteri(gl.TEXTURE_2D, p, v);
  return gl;
}
function program(code) {
  let p = programs.get(code);
  if (p !== undefined) return p;
  const sh = (type, src) => {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
  };
  try {
    let src = String(code);
    if (!/precision\s+(lowp|mediump|highp)\s+float/.test(src)) src = 'precision mediump float;\n' + src;
    for (const [u, t] of [['u_time', 'float'], ['u_res', 'vec2'], ['u_cam', 'vec3'], ['u_amount', 'float']])
      if (src.indexOf(u) >= 0 && !new RegExp('uniform\\s+\\w+\\s+' + u + '\\b').test(src)) src = src.replace(/(precision[^;]*;)/, '$1\nuniform ' + t + ' ' + u + ';');
    p = gl.createProgram();
    gl.attachShader(p, sh(gl.VERTEX_SHADER, VERT)); gl.attachShader(p, sh(gl.FRAGMENT_SHADER, src));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  } catch (e) { console.warn('Source 2D [shader]: ' + e.message); p = null; }
  programs.set(code, p);
  return p;
}
function gpuShader(id, code, t, cx, cy, zoom) {
  const cv = surfaces[id];
  if (!cv || !glInit()) return false;
  const p = program(code);
  if (!p) return false;
  const w = cv.width, h = cv.height;
  if (glCanvas.width !== w || glCanvas.height !== h) { glCanvas.width = w; glCanvas.height = h; }
  gl.viewport(0, 0, w, h);
  gl.useProgram(p);
  gl.bindTexture(gl.TEXTURE_2D, glTex);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, cv);
  const loc = n => gl.getUniformLocation(p, n);
  gl.uniform1i(loc('u_tex'), 0);
  if (loc('u_time')) gl.uniform1f(loc('u_time'), t || 0);
  if (loc('u_res')) gl.uniform2f(loc('u_res'), w, h);
  if (loc('u_cam')) gl.uniform3f(loc('u_cam'), cx || 0, cy || 0, zoom || 1);
  if (loc('u_amount')) gl.uniform1f(loc('u_amount'), amount);
  const a = gl.getAttribLocation(p, 'a_pos');
  gl.enableVertexAttribArray(a);
  gl.vertexAttribPointer(a, 2, gl.FLOAT, false, 0, 0);
  gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT);
  gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  const c = cv.ctx;
  c.save(); c.setTransform(1, 0, 0, 1, 0, 0); c.globalAlpha = 1; c.globalCompositeOperation = 'copy'; c.filter = 'none';
  c.drawImage(glCanvas, 0, 0);
  c.restore();
  return true;
}

/* ---------- audio ---------- */
let actx = null;
function audio() {
  if (!actx) { try { actx = new (G.AudioContext || G.webkitAudioContext)(); } catch (e) { actx = null; } }
  if (actx && actx.state === 'suspended') actx.resume();
  return actx;
}
const tracks = {};
let music = null, musicVol = 0.6, pending = null;
const MIME = { ogg: 'audio/ogg', wav: 'audio/wav', mp3: 'audio/mpeg', flac: 'audio/flac', qoa: 'audio/qoa' };
const WAVE = ['sine', 'square', 'triangle', 'sawtooth'];
function playTrack(id, loop) {
  if (!tracks[id]) return;
  if (music) music.pause();
  music = new Audio(tracks[id]);
  music.loop = !!loop; music.volume = musicVol;
  const pr = music.play();
  if (pr && pr.catch) pr.catch(() => { pending = { id, loop }; });    /* sin clic previo el navegador no deja sonar */
}

/* sonidos de archivo con canales (Web Audio): se decodifican al cargar; cada reproduccion es una fuente nueva */
const sounds = {}, chGain = [], chVoices = [[], [], [], [], [], [], [], []];
function chNode(ch) {
  const a = audio();
  if (!a) return null;
  if (!chGain[ch]) { chGain[ch] = a.createGain(); chGain[ch].connect(a.destination); }
  return chGain[ch];
}
function soundLoad(id, b64, ext) {
  if (sounds[id]) return true;
  const a = audio();
  if (!a) return false;
  sounds[id] = { buf: null, playing: 0 };
  try {
    const bin = atob(b64), arr = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
    a.decodeAudioData(arr.buffer, b => { sounds[id].buf = b; }, () => {});
  } catch (e) { return false; }
  return true;
}
function soundPlay(id, vol, pitch, pan, ch) {
  const a = audio(), sd = sounds[id];
  if (!a || !sd || !sd.buf) return -1;
  const src = a.createBufferSource(), g = a.createGain();
  src.buffer = sd.buf; src.playbackRate.value = pitch || 1; g.gain.value = vol;
  let last = g;
  if (a.createStereoPanner) { const pn = a.createStereoPanner(); pn.pan.value = pan || 0; g.connect(pn); last = pn; }
  src.connect(g); last.connect(chNode(ch));
  const list = chVoices[ch];
  list.push(src); sd.playing++;
  src.onended = () => { sd.playing--; const i = list.indexOf(src); if (i >= 0) list.splice(i, 1); };
  src.start();
  return 0;
}
function channel(op, ch, v) {
  const a = audio();
  if (!a) return;
  const chs = ch < 0 ? [0, 1, 2, 3, 4, 5, 6, 7] : [ch];
  for (const c of chs) {
    if (op === 0) chVoices[c].slice().forEach(src => { try { src.stop(); } catch (e) {} });
    else if (op === 3) chNode(c).gain.value = v;
    else if (op === 1) chNode(c).gain.value = 0;                     /* el navegador no pausa una fuente: se silencia */
    else if (op === 2) chNode(c).gain.value = chNode(c)._v != null ? chNode(c)._v : 1;
    if (op === 3) chNode(c)._v = v;
  }
}

/* mandos del navegador: los botones llegan como teclas "pad_a"... (como en el motor nativo) */
const PADK = ['a', 'b', 'x', 'y', 'lb', 'rb', 'lt', 'rt', 'select', 'start', 'l3', 'r3', 'up', 'down', 'left', 'right'];
const padPrev = [[], [], [], []];
function padList() { try { return navigator.getGamepads ? navigator.getGamepads() : []; } catch (e) { return []; } }
function pollPads() {
  const pads = padList();
  for (let p = 0; p < 4; p++) {
    const gp = pads[p], now = [];
    if (gp && gp.connected) {
      const b = i => !!(gp.buttons[i] && gp.buttons[i].pressed), ax = gp.axes[0] || 0, ay = gp.axes[1] || 0;
      now.push(b(0), b(1), b(2), b(3), b(4), b(5), b(6), b(7), b(8), b(9), b(10), b(11),
               b(12) || ay < -0.5, b(13) || ay > 0.5, b(14) || ax < -0.5, b(15) || ax > 0.5);
    }
    for (let i = 0; i < 16; i++) {
      const v = !!now[i];
      if (v !== !!padPrev[p][i]) { send(v ? 'keydown' : 'keyup', { key: (p ? 'pad' + (p + 1) + '_' : 'pad_') + PADK[i] }); padPrev[p][i] = v; }
    }
  }
}

/* ---------- archivos: sistema virtual en localStorage ("s2dfs:" + ruta) ---------- */
const FS = 's2dfs:';
const norm = p => '/' + String(p).replace(/\\/g, '/').split('/').filter(x => x && x !== '.').join('/');
function fsGet(p) { try { return localStorage.getItem(FS + norm(p)); } catch (e) { return null; } }
function fsSet(p, v) { try { localStorage.setItem(FS + norm(p), v); return true; } catch (e) { return false; } }

let quitted = false;
G.__native = {
  print: m => console.log(m),
  hrtime: () => performance.now(),
  profile: false,
  setTitle: t => { document.title = t; },
  quit: () => { quitted = true; },
  shaderAmount: a => { amount = a; },
  gpuShader,
  fontLoad(b64) {
    const slot = (G.__native._fonts = (G.__native._fonts || 0) + 1);
    try {
      const bin = atob(b64), arr = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) arr[i] = bin.charCodeAt(i);
      const ff = new FontFace('s2d:' + slot, arr.buffer);
      document.fonts.add(ff); ff.load();
    } catch (e) { return -1; }
    return slot;
  },
  musicLoad(id, b64, ext) { tracks[id] = 'data:' + (MIME[ext] || 'audio/mpeg') + ';base64,' + b64; return true; },
  musicPlay: playTrack,
  musicStop() { if (music) music.pause(); music = null; pending = null; },
  musicVolume(v) { musicVol = v; if (music) music.volume = v; },
  musicSeek(s) { if (music) try { music.currentTime = s; } catch (e) {} },
  musicPlaying: () => !!(music && !music.paused && !music.ended) || !!pending,
  sfx(f0, f1, dur, wave, vol) {
    const a = audio();
    if (!a) return;
    const o = a.createOscillator(), g = a.createGain(), t = a.currentTime;
    o.type = WAVE[wave] || 'sine';
    o.frequency.setValueAtTime(f0, t); o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t + dur);
    g.gain.setValueAtTime(vol, t); g.gain.exponentialRampToValueAtTime(0.01, t + dur);
    o.connect(g); g.connect(a.destination); o.start(t); o.stop(t + dur);
  },
  tone(freq, dur, square) { G.__native.sfx(freq, freq, dur, square ? 1 : 0, 0.07); },
  soundLoad, soundPlay, channel,
  setPixelPerfect(on) { pixelPerfect = !!on; fit(); },
  soundPlaying: id => !!(sounds[id] && sounds[id].playing > 0),
  pad(op, p, a, b, c) {
    const gp = padList()[p];
    if (op === 1) return !!(gp && gp.connected);
    if (!gp) return 0;
    if (op === 0) {
      if (a >= 4) { const bt = gp.buttons[a === 4 ? 6 : 7]; return bt ? bt.value : 0; }
      const v = gp.axes[a] || 0; return Math.abs(v) < 0.15 ? 0 : v;
    }
    const va = gp.vibrationActuator;
    if (va && va.playEffect) try { va.playEffect('dual-rumble', { duration: c * 1000, strongMagnitude: a, weakMagnitude: b }); } catch (e) {}
    return 0;
  },
  paths: () => ['/usuario', '/datos', '/documentos'],
  fileRead: p => fsGet(p),
  fileReadB64(p) { const t = fsGet(p); if (t == null) return null; try { return btoa(unescape(encodeURIComponent(t))); } catch (e) { return null; } },
  fileWrite: (p, v) => fsSet(p, String(v)),
  makeDir: () => true,
  fileDelete(p) { try { localStorage.removeItem(FS + norm(p)); return true; } catch (e) { return false; } },
  dirList(p) {
    const base = norm(p) + '/', out = new Set();
    try {
      for (let i = 0; i < localStorage.length; i++) {
        const k = localStorage.key(i);
        if (!k || !k.startsWith(FS + base)) continue;
        const rest = k.slice(FS.length + base.length), sl = rest.indexOf('/');
        out.add(sl < 0 ? rest : rest.slice(0, sl + 1));
      }
    } catch (e) { return null; }
    return Array.from(out);
  },
};

/* ---------- S2D: lo que usa el runtime ---------- */
const S2D = G.S2D = {
  surface, image, decodeImage, parseColor,
  isImage: x => x instanceof HTMLCanvasElement,
  display: null, onFrame: null, onInput: null,
  store: G.localStorage,
  setFullscreen(on) {
    try {
      if (on && !document.fullscreenElement) document.documentElement.requestFullscreen();
      else if (!on && document.fullscreenElement) document.exitFullscreen();
    } catch (e) {}
  },
  isFullscreen: () => !!document.fullscreenElement,
};

/* ---------- pantalla, entrada y fotogramas ---------- */
let shown = null;
function present() {
  const d = S2D.display;
  if (!d || d === shown) return;
  shown = d;
  d.id = 's2d-pantalla';
  d.tabIndex = 0;
  document.body.appendChild(d);
  fit();
}
/* el juego llena la ventana sin deformarse (bandas negras) y con los pixeles nitidos */
let pixelPerfect = false;
function fit() {
  const d = shown;
  if (!d) return;
  let s = Math.min(innerWidth / d.width, innerHeight / d.height);
  if (pixelPerfect && s >= 1) s = Math.floor(s);
  d.style.width = Math.floor(d.width * s) + 'px';
  d.style.height = Math.floor(d.height * s) + 'px';
}
addEventListener('resize', fit);
function send(type, e, extra) {
  if (!S2D.onInput) return;
  const d = shown, r = d ? d.getBoundingClientRect() : { left: 0, top: 0, width: 1, height: 1 };
  const ev = Object.assign({ type, key: e.key || '', repeat: !!e.repeat, button: e.button | 0,
    x: d ? (e.clientX - r.left) / (r.width || 1) * d.width : 0, y: d ? (e.clientY - r.top) / (r.height || 1) * d.height : 0 }, extra || {});
  try { S2D.onInput(ev); } catch (err) { console.error(err); }
}
function gesture() {
  audio();
  if (pending) { const p = pending; pending = null; playTrack(p.id, p.loop); }
}
addEventListener('keydown', e => {
  if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' ', 'Tab'].indexOf(e.key) >= 0) e.preventDefault();
  gesture(); send('keydown', e);
});
addEventListener('keyup', e => send('keyup', e));
addEventListener('mousedown', e => { if (e.target === shown) { gesture(); send('mousedown', e); } });
addEventListener('mouseup', e => send('mouseup', e));
addEventListener('mousemove', e => { if (e.target === shown) send('mousemove', e); });
addEventListener('wheel', e => { if (e.target === shown) send('wheel', e, { button: e.deltaY < 0 ? -1 : 1 }); }, { passive: true });
addEventListener('contextmenu', e => { if (e.target === shown) e.preventDefault(); });
/* tactil: el primer dedo es el raton (tocar = clic, arrastrar = mover); sin desplazar ni hacer zoom en la pagina */
addEventListener('touchstart', e => { if (e.target !== shown) return; e.preventDefault(); const t = e.changedTouches[0]; gesture(); send('mousemove', t); send('mousedown', t); }, { passive: false });
addEventListener('touchmove', e => { if (e.target !== shown) return; e.preventDefault(); send('mousemove', e.changedTouches[0]); }, { passive: false });
addEventListener('touchend', e => { if (e.target === shown) e.preventDefault(); send('mouseup', e.changedTouches[0]); }, { passive: false });

function frame(now) {
  if (quitted) { if (shown) shown.style.opacity = '0.4'; return; }
  pollPads();
  if (S2D.onFrame) { try { S2D.onFrame(now); } catch (e) { console.error(e); } }
  present();
  requestAnimationFrame(frame);
}

/* arranque: el proyecto viene en la pagina como JSON (datos, no codigo) */
G.addEventListener('DOMContentLoaded', () => {
  const el = document.getElementById('s2d-proyecto');
  let project = null;
  try { project = JSON.parse(el.textContent); }
  catch (e) { document.body.textContent = 'No se pudo abrir el juego: el proyecto está dañado.'; return; }
  document.title = project.name || 'Source 2D';
  const g = S2D.createGame(project);
  g.start();
  if (project.settings && project.settings.autofullscreen) setTimeout(() => g.toggleFullscreen(), 300);
  /* pruebas: "#s2dtest=N" ejecuta N fotogramas de 1/60 s antes de que termine de cargar (captura sin esperar) */
  const m = /s2dtest=(\d+)/.exec(location.hash);
  if (m) for (let i = 1; i <= +m[1] && !quitted; i++) { S2D.onFrame(i * 1000 / 60); present(); }
  else requestAnimationFrame(frame);
});
})();
