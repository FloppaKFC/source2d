/* Source 2D engine - runtime de los juegos: eventos, comportamientos, fisica, camara, sonido y dibujo.
   Corre sobre src/gfx.js (S2D: superficies de la GPU, imagenes, entrada y fotogramas) y __native (main.c). */
(function () {
  'use strict';
  var GRAVITY = 1900, MAX_FALL = 2400;
  var N = __native;
  /* errores que no deben parar el juego (un audio que no carga, un guardado roto...): se avisan en la consola una vez
     por mensaje, para que quede rastro */
  var quietSeen = {};
  function quiet(e) {
    var m = String(e && e.message || e);
    if (quietSeen[m]) return;
    quietSeen[m] = true;
    console.warn('Source 2D: ' + m);
  }

  function n(v, d) { var x = parseFloat(v); return isNaN(x) ? (d || 0) : x; }
  function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }

  /* al exportar, cada imagen de pixeles va como paleta (pal) + un caracter por pixel (ix); aqui vuelve a
     ser la lista de colores de siempre (el editor la empaqueta en src/editor/mini.cpp) */
  function unpackPixels(o) {
    if (Array.isArray(o)) { for (var i = 0; i < o.length; i++) if (o[i] && typeof o[i] === 'object') unpackPixels(o[i]); return; }
    if (typeof o.ix === 'string' && Array.isArray(o.pal)) {
      var ix = o.ix, pal = o.pal, px = new Array(ix.length);
      for (var k = 0; k < ix.length; k++) { var c = ix.charCodeAt(k); px[k] = pal[c - (c > 92 ? 36 : 35)]; }
      o.pixels = px; delete o.ix; delete o.pal;
    }
    for (var key in o) if (o[key] && typeof o[key] === 'object') unpackPixels(o[key]);
  }

  /* imagen desde la lista de colores de un sprite, tile o glifo ('' o null = transparente); color fuerza un color */
  function pixelImage(w, h, pixels, color) {
    var img = S2D.image(w, h), d = img.data, last = null, c = color ? S2D.parseColor(color) : null;
    if (pixels) for (var i = 0, n = w * h; i < n; i++) {
      var col = pixels[i];
      if (!col) continue;
      if (!color && col !== last) { c = S2D.parseColor(col); last = col; }
      var k = i * 4;
      d[k] = c[0]; d[k + 1] = c[1]; d[k + 2] = c[2]; d[k + 3] = Math.round(c[3] * 255);
    }
    img.update();                 /* los pixeles cambiaron (en el navegador se copian al lienzo aqui) */
    return img;
  }
  function buildSpriteFrame(sprite, idx) {
    var w = Math.max(1, (sprite && sprite.width) | 0 || 16);
    var h = Math.max(1, (sprite && sprite.height) | 0 || 16);
    var frames = (sprite && sprite.frames) || [];
    var f = frames[idx] || frames[0];
    return pixelImage(w, h, f && f.pixels);
  }
  function buildTileCanvas(ts, idx) {
    var T = Math.max(1, (ts && ts.tileSize) | 0 || 16);
    var tile = (ts && ts.tiles) ? ts.tiles[idx] : null;
    return pixelImage(T, T, tile && tile.pixels);
  }

  /* ---------------- curvas de suavizado ---------------- */
  var EASINGS = {
    linear: function (t) { return t; },
    easeIn: function (t) { return t * t; },
    easeOut: function (t) { return t * (2 - t); },
    easeInOut: function (t) { return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t; },
    back: function (t) { var s = 1.70158; return t * t * ((s + 1) * t - s); },
    backOut: function (t) { var s = 1.70158; t -= 1; return t * t * ((s + 1) * t + s) + 1; },
    bounce: function (t) {
      if (t < 0.3636) return 7.5625 * t * t;
      if (t < 0.7272) { t -= 0.5454; return 7.5625 * t * t + 0.75; }
      if (t < 0.909) { t -= 0.8181; return 7.5625 * t * t + 0.9375; }
      t -= 0.9545; return 7.5625 * t * t + 0.984375;
    },
    elastic: function (t) {
      return t === 0 || t === 1 ? t : Math.pow(2, -10 * t) * Math.sin((t - 0.075) * (2 * Math.PI) / 0.3) + 1;
    }
  };
  function ease(name, t) { return (EASINGS[name] || EASINGS.linear)(clamp(t, 0, 1)); }

  function createGame(project, options) {
    options = options || {};
    if (project && typeof project === 'object') unpackPixels(project);
    var settings = project.settings || {};
    /* como en el editor: de 160x120 a 4096x4096 (una resolucion enorme no cabe en memoria ni en la GPU) */
    var VW = clamp(Math.round(n(settings.width, 960)) || 960, 160, 4096);
    var VH = clamp(Math.round(n(settings.height, 540)) || 540, 120, 4096);

    /* la pantalla (lo que se presenta en la ventana), el lienzo auxiliar y las capas con shader son superficies de la GPU */
    var canvas = S2D.surface(VW, VH), ctx = canvas.ctx;
    function shaderCodeOf(id) {
      var out = '';
      (project.shaders || []).forEach(function (x) { if (x.id === id) out = x.code || ''; });
      return out;
    }
    function sceneShaderSrc() {
      var sc = scenes[state.sceneId];
      return sc && sc.shaderId ? shaderCodeOf(sc.shaderId) : '';
    }
    /* shaders de pantalla: el de la escena y los que se activan con la accion "Shader de pantalla" */
    function screenShaderList() {
      var out = [], s0 = sceneShaderSrc(), i;
      if (s0) out.push({ code: s0, amount: 1 });
      for (i = 0; i < (state.screenShaders || []).length; i++) {
        var sid = state.screenShaders[i], c = shaderCodeOf(sid), am = state.shaderAmt && sid in state.shaderAmt ? state.shaderAmt[sid] : 1;
        if (c && am > 0) out.push({ code: c, amount: am });
      }
      return out;
    }
    var scratch = S2D.surface(VW, VH), sctx = scratch.ctx;

    var sprites = {}, objects = {}, scenes = {}, tilesets = {};
    (project.sprites || []).forEach(function (s) { sprites[s.id] = s; });
    (project.objects || []).forEach(function (o) { objects[o.id] = compileObj(o); });
    var layers = {};
    (project.layers && project.layers.length ? project.layers : []).forEach(function (L) { layers[L.id] = L; });
    if (!layers.base) layers.base = { id: 'base', name: 'Principal', z: 0, parallax: 1, solid: true };
    function layerIdOf(inst) { var c = objDef(inst).capa; return (c && layers[c]) ? c : 'base'; }
    function layerOf(inst) { return layers[layerIdOf(inst)]; }
    (project.scenes || []).forEach(function (s) { scenes[s.id] = s; });
    (project.tilesets || []).forEach(function (t) { tilesets[t.id] = t; });
    /* objetos Tile: cada instancia lleva su mapa (it.tm, con la esquina en x, y) y se juega como un mapa de tiles
       en la capa del objeto */
    (project.scenes || []).forEach(function (s) {
      if (s._tileInst) return;
      s._tileInst = true;
      var keep = [];
      (s.instances || []).forEach(function (it) {
        var o = objects[it.objectId];
        if (!o || o.type !== 'tile' || !it.tm || !it.tm.data) { keep.push(it); return; }
        (s.tilemaps || (s.tilemaps = [])).push({ id: 'ti_' + it.id, objectId: it.objectId, tilesetId: o.tilesetId, x: n(it.x), y: n(it.y),
          cols: n(it.tm.cols), rows: n(it.tm.rows), data: it.tm.data, layer: o.capa || 'base', shader: it.tm.shader || '' });
      });
      s.instances = keep;
    });

    var frameCache = {}, tileCache = {};
    var frameBy = {};
    function spriteFrame(sid, i) {
      var L = frameBy[sid] || (frameBy[sid] = []), f = L[i];
      if (f) return f;
      var key = sid + '#' + i;
      if (!frameCache[key]) frameCache[key] = buildSpriteFrame(sprites[sid], i);
      return (L[i] = frameCache[key]);
    }
    function tileFrame(tid, i) {
      var key = tid + '#' + i;
      if (!tileCache[key]) tileCache[key] = buildTileCanvas(tilesets[tid], i);
      return tileCache[key];
    }

    var state = {
      view: { w: VW, h: VH },
      sceneId: null, instances: [], globals: {}, time: 0, frame: 0, dt: 0,
      keys: {}, pressed: {}, released: {}, lastKey: '', typed: '', waits: [], msg: {}, msgNext: {}, paused: false,
      mouse: { x: 0, y: 0, wx: 0, wy: 0, down: false, pressed: false, released: false, button: 0 },
      camera: {
        x: 0, y: 0, zoom: 1, followId: null, smooth: 0, moveTo: null,
        shakeT: 0, shakeMag: 0, limits: null
      },
      timers: {}, timeScale: 1, wheelUp: false, wheelDown: false,
      selection: {}, boxStart: null, transition: null, shaderAmt: {},
      eventState: [], running: true, ended: false, endMessage: '', sceneChanged: false, nextId: 1
    };
    (project.globals || []).forEach(function (g) {
      var tp = g.type || 'normal';
      if (tp === 'parent') { (g.children || []).forEach(function (c) { state.globals[g.name + '.' + c.name] = c.value; }); return; }
      if (tp === 'bool') { state.globals[g.name] = g.value === true || g.value === 'true'; return; }
      if (tp === 'text') { state.globals[g.name] = g.value == null ? '' : String(g.value); return; }
      if (tp === 'array') { state.globals[g.name] = Array.isArray(g.value) ? g.value.slice() : []; return; }
      state.globals[g.name] = g.value;
    });

    /* ---------------- sonido ----------------
       La musica la reproduce la plataforma (raylib, en streaming); los pitidos y efectos son voces sintetizadas. */
    function normMime(m) {
      var x = String(m || '').toLowerCase();
      if (/mp3|mpeg/.test(x)) return 'audio/mpeg';
      if (/wav|wave/.test(x)) return 'audio/wav';
      if (/ogg/.test(x)) return 'audio/ogg';
      if (/m4a|mp4|aac/.test(x)) return 'audio/mp4';
      if (/flac/.test(x)) return 'audio/flac';
      if (/qoa/.test(x)) return 'audio/qoa';
      return 'audio/mpeg';
    }
    var runtimeTracks = {};
    function findTrack(name) {
      if (runtimeTracks[name]) return runtimeTracks[name];
      var list = (project && project.music) || [], i;
      for (i = 0; i < list.length; i++) if (list[i].name === name || list[i].id === name) return list[i];
      return null;
    }
    var nativeLoaded = {};
    function trackBase64(tr) {
      var raw = String(tr.data || ''), i = raw.indexOf(',');
      return i >= 0 ? raw.slice(i + 1) : raw;
    }
    function trackExt(tr) {
      var m = normMime(tr.mime);
      return m === 'audio/ogg' ? 'ogg' : m === 'audio/wav' ? 'wav' : m === 'audio/flac' ? 'flac' : m === 'audio/qoa' ? 'qoa' : 'mp3';
    }
    function stopMusicTrack() { try { N.musicStop(); } catch (e) { quiet(e); } }
    var musicVol = 0.6;
    /* volumen (0-1) de la musica: se aplica a la que suena y a las siguientes */
    function setMusicVolume(v) {
      musicVol = clamp(v, 0, 1);
      try { N.musicVolume(musicVol); } catch (e) { quiet(e); }
    }
    /* salta a ese segundo de la musica que suena */
    function seekMusic(sec) { try { N.musicSeek(Math.max(0, sec)); } catch (e) { quiet(e); } }
    function musicIsPlaying() { try { return !!N.musicPlaying(); } catch (e) { return false; } }
    /* sonidos de archivo (la lista de audio del proyecto) que pueden sonar varios a la vez, en canales con su volumen.
       vari: variacion al azar del tono (0.1 = +-10 %), para que un sonido repetido no canse. */
    var soundLoaded = {};
    function playSoundTrack(name, vol, pitch, pan, ch, vari) {
      var tr = findTrack(name);
      if (!tr || !N.soundPlay) return;
      try {
        if (!soundLoaded[tr.id]) { soundLoaded[tr.id] = N.soundLoad(tr.id, trackBase64(tr), trackExt(tr)) ? 1 : -1; }
        if (soundLoaded[tr.id] < 0) return;
        if (vari > 0) pitch *= 1 + (Math.random() * 2 - 1) * vari;
        N.soundPlay(tr.id, clamp(vol, 0, 1), pitch > 0 ? pitch : 1, clamp(pan, -1, 1), clamp(ch | 0, 0, 7));
      } catch (e) { quiet(e); }
    }
    /* sonido en la posicion de un objeto: mas bajo cuanto mas lejos del centro de la pantalla y a la izquierda o derecha */
    function playSoundAt(name, inst, vol, range, ch) {
      var cam = state.camera, z = cam.zoom || 1, cx = cam.x + VW / z / 2, cy = cam.y + VH / z / 2;
      var dx = inst.x - cx, dy = inst.y - cy, d = Math.sqrt(dx * dx + dy * dy), r = Math.max(1, range || 600);
      if (d >= r) return;
      playSoundTrack(name, vol * (1 - d / r), 1, clamp(dx / (VW / z / 2), -1, 1), ch, 0.05);
    }
    function channelOp(op, ch, v) { try { if (N.channel) N.channel(op, ch, v); } catch (e) { quiet(e); } }
    function playMusicTrack(name, loop) {
      var tr = findTrack(name);
      stopMusicTrack();
      if (!tr) return;
      try {
        if (!nativeLoaded[tr.id]) { N.musicLoad(tr.id, trackBase64(tr), trackExt(tr)); nativeLoaded[tr.id] = true; }
        N.musicPlay(tr.id, !!loop);
        N.musicVolume(musicVol);
      } catch (e) { quiet(e); }
    }
    /* pitido de onda cuadrada */
    function beep(freq, dur) { try { N.tone(freq || 440, Math.max(0.02, (dur || 0.12) + 0.03), 1); } catch (e) { quiet(e); } }

    /* efecto de sonido sintetizado: onda (sine, square, triangle, sawtooth) cuya frecuencia va de f0 a f1
       (curva exponencial) mientras el volumen (0-1) se apaga hasta casi cero en dur segundos */
    var WAVES = { sine: 0, square: 1, triangle: 2, sawtooth: 3, saw: 3 };
    function sfxTone(wave, f0, f1, dur, vol) {
      f0 = Math.max(1, f0 || 440); f1 = Math.max(1, f1 || f0); dur = Math.max(0.01, dur || 0.1); vol = clamp(vol, 0, 1);
      if (vol <= 0) return;
      try { N.sfx(f0, f1, dur, WAVES[wave] || 0, vol); } catch (e) { quiet(e); }
    }

    /* ---------------- entrada ----------------
       S2D.onInput recibe teclas y raton ya en pixeles del juego (gfx.js); el raton solo dentro del area de juego. */
    function keyName(e) { return e.key === ' ' ? ' ' : String(e.key).toLowerCase(); }
    function toggleFullscreen() { S2D.setFullscreen(!S2D.isFullscreen()); }
    function onKeyDown(e) {
      var k = keyName(e);
      if (k === 'f11') { toggleFullscreen(); return; }
      if (N.debug && (k === 'f3' || k === 'f6' || k === 'f7')) {
        if (k === 'f3') dbg.on = !dbg.on;
        else if (k === 'f6') dbg.pause = !dbg.pause;
        else { dbg.pause = true; dbg.stepOne = true; }
        return;
      }
      if (!state.keys[k]) { state.pressed[k] = true; state.lastKey = k; }
      if (String(e.key).length === 1) state.typed += state.keys.shift ? String(e.key).toUpperCase() : e.key;   /* para escribir textos (nombres...) */
      state.keys[k] = true;
    }
    function onKeyUp(e) { var k = keyName(e); state.keys[k] = false; state.released[k] = true; }
    function updateMouse(e) {
      state.mouse.x = e.x;
      state.mouse.y = e.y;
      state.mouse.wx = state.mouse.x / state.camera.zoom + state.camera.x;
      state.mouse.wy = state.mouse.y / state.camera.zoom + state.camera.y;
    }
    function onInput(e) {
      switch (e.type) {
        case 'keydown': onKeyDown(e); break;
        case 'keyup': onKeyUp(e); break;
        case 'mousemove': updateMouse(e); break;
        case 'mousedown': updateMouse(e); state.mouse.down = true; state.mouse.pressed = true; state.mouse.button = e.button; break;
        case 'mouseup': updateMouse(e); state.mouse.down = false; state.mouse.released = true; state.mouse.button = e.button; break;
        case 'wheel': if (e.button < 0) state.wheelUp = true; else state.wheelDown = true; break;
      }
    }

    /* el mando cuenta como estas teclas (cruceta = flechas, A = espacio, Start = Enter...): cualquier juego hecho con
       teclado se juega tambien con mando sin cambiar nada */
    var PAD_ALIAS = { arrowleft: 'pad_left', arrowright: 'pad_right', arrowup: 'pad_up', arrowdown: 'pad_down', ' ': 'pad_a',
                      enter: 'pad_start', escape: 'pad_select', shift: 'pad_x', control: 'pad_b' };
    function key(k) { return !!state.keys[k] || (PAD_ALIAS[k] !== undefined && !!state.keys[PAD_ALIAS[k]]); }
    /* la tecla puede venir de una variable ("{= $TeclaSaltar}"): asi los controles se pueden cambiar en el juego */
    function keyParam(k) { k = String(k == null ? '' : k); return k.indexOf('{') >= 0 ? resolveStr(k).toLowerCase() : k; }
    function pressedKey(k) { return !!state.pressed[k] || (PAD_ALIAS[k] !== undefined && !!state.pressed[PAD_ALIAS[k]]); }

    /* ---------------- objetos e instancias ---------------- */
    /* la definicion del objeto se guarda en la instancia al crearla (_od): se pide cientos de miles de veces por fotograma */
    var NODEF = Object.freeze({});
    function objDef(inst) { return inst._od || objects[inst.objId] || NODEF; }
    function behav(inst) { return (inst._od || objects[inst.objId] || NODEF).behaviors || NODEF; }
    function animFind(obj, name) {
      var L = (obj && obj.animations) || [];
      for (var i = 0; i < L.length; i++) if (String(L[i].name).toLowerCase() === String(name).toLowerCase()) return L[i];
      return null;
    }
    function kname(v) { v = String(v == null ? '' : v).toLowerCase(); return (v === 'space' || v === 'espacio') ? ' ' : v; }
    /* "#rrggbb" o "#rgb" -> [r, g, b] */
    function hexRGB(c) {
      var h = String(c || '#ffffff').replace('#', '');
      if (h.length === 3) h = h.charAt(0) + h.charAt(0) + h.charAt(1) + h.charAt(1) + h.charAt(2) + h.charAt(2);
      var v = parseInt(h.slice(0, 6), 16) || 0;
      return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
    }
    function compileObj(o) {
      var c = o.comportamientos;
      if (!c || (!c.plataformero && !c.plataforma)) return o;
      var copy = Object.assign({}, o), b = Object.assign({}, o.behaviors || {});
      if (c.plataformero) {
        var pf = c.plataformero;
        b.movement = 'platform'; b.gravity = true; b.collide = true; b.solid = true;
        if (pf.keyLeft) b.keyLeft = kname(pf.keyLeft);
        if (pf.keyRight) b.keyRight = kname(pf.keyRight);
        if (pf.keyJump) b.keyJump = kname(pf.keyJump);
        if (pf.keyRun) b.keyRun = kname(pf.keyRun);
        if (pf.keyDown) b.keyDown = kname(pf.keyDown);
        b.run = !!pf.run; b.runSpeed = n(pf.runSpeed, 0); b.crouch = !!pf.crouch;
        b.wall = !!pf.wall; b.wallJump = !!pf.wallJump; b.climbSpeed = n(pf.climb, 0);
        /* doble salto, dash, salto variable, tiempo de coyote, bufer de salto y caida maxima */
        b.airJumps = Math.max(0, n(pf.airJumps, 0) | 0); b.airJump = n(pf.airJump, 0);
        b.dash = !!pf.dash; b.keyDash = pf.keyDash ? kname(pf.keyDash) : ''; b.dashSpeed = n(pf.dashSpeed, 720);
        b.dashTime = n(pf.dashTime, 0.16); b.dashCooldown = n(pf.dashCooldown, 0.35);
        b.jumpCut = !!pf.jumpCut; b.coyote = n(pf.coyote, 0); b.jumpBuffer = n(pf.jumpBuffer, 0); b.maxFall = n(pf.maxFall, 0);
      } else if (c.plataforma) {
        b.solid = true; b.movement = 'none'; b.gravity = false; b.collide = false;
        var pl = c.plataforma;
        copy.platMove = { dx: n(pl.dx), dy: n(pl.dy), speed: n(pl.speed, 60) };
      }
      copy.behaviors = b;
      return copy;
    }
    function updatePlatforms(dt) {
      for (var i = 0; i < state.instances.length; i++) {
        var inst = state.instances[i], pm = objDef(inst).platMove;
        if (!pm || inst.platOff) continue;
        var L = Math.sqrt(pm.dx * pm.dx + pm.dy * pm.dy);
        if (!L || !pm.speed) continue;
        inst.pp += inst.pdir * dt * pm.speed / L;
        if (inst.pp >= 1) { inst.pp = 1; inst.pdir = -1; }
        else if (inst.pp <= 0) { inst.pp = 0; inst.pdir = 1; }
        inst.x = inst.ox + pm.dx * inst.pp;
        inst.y = inst.oy + pm.dy * inst.pp;
      }
    }
    /* ---------------- comportamientos de la lista (estilo GDevelop) ---------------- */
    function bhOf(inst, type) {
      var l = objDef(inst).behaviorList || [];
      for (var i = 0; i < l.length; i++) if (l[i].type === type) return l[i];
      return null;
    }
    function instContains(inst, x, y) {
      var sz = instSize(inst);
      return Math.abs(x - inst.x) * 2 <= sz.w && Math.abs(y - inst.y) * 2 <= sz.h;
    }
    /* antes de los eventos: temporizadores de objeto, arrastrar, sinusoidal, parpadeo y enfriamientos */
    /* patrullar, seguir y mantener en pantalla */
    function behaviorsMove(inst, dt) {
      var pt = bhOf(inst, 'patrol');
      if (pt) {
        if (!inst.patDir) inst.patDir = 1;
        if ((inst.patDir > 0 && inst.wallR) || (inst.patDir < 0 && inst.wallL)) inst.patDir = -inst.patDir;
        else if (pt.edges !== 'fall' && inst.grounded) {
          var sz = instSize(inst), fx = inst.x + inst.patDir * (sz.w / 2 + 2), fy = inst.y + sz.h / 2 + 4;
          var near = solidsNear(buildInstanceSolids(), fx - 1, fy - 1, fx + 1, fy + 1), ground = false;
          for (var k = 0; k < near.length; k++) if (near[k].src !== inst && Math.abs(fx - near[k].x) * 2 <= near[k].w && Math.abs(fy - near[k].y) * 2 <= near[k].h) ground = true;
          if (!ground) inst.patDir = -inst.patDir;
        }
        inst.vx = inst.patDir * n(pt.speed, 60); inst.faceX = inst.patDir;
      }
      var tk = bhOf(inst, 'touchKey');
      if (tk) {
        var m = state.mouse, hud = (inst._od || objDef(inst)).hud, on = m.down && instContains(inst, hud ? m.x : m.wx, hud ? m.y : m.wy);
        var kk = kname(tk.key || ' ');
        if (on && !inst.tkDown) { state.keys[kk] = true; state.pressed[kk] = true; }
        else if (!on && inst.tkDown) { state.keys[kk] = false; state.released[kk] = true; }
        inst.tkDown = on;
        inst.opacity = on ? 100 : Math.min(inst.opacity, 70);
      }
      var fw = bhOf(inst, 'follow');
      if (fw) {
        var tid = objIdNamed(String(fw.target || '')), tl = tid ? objInstances(tid) : [], best = null, bd = 1e18;
        for (var j = 0; j < tl.length; j++) { var dd = (tl[j].x - inst.x) * (tl[j].x - inst.x) + (tl[j].y - inst.y) * (tl[j].y - inst.y); if (tl[j] !== inst && dd < bd) { bd = dd; best = tl[j]; } }
        var rg = n(fw.range, 0), d = Math.sqrt(bd);
        if (best && d > n(fw.stop, 24) && (!rg || d <= rg)) {
          var sp = n(fw.speed, 90), b = behav(inst);
          inst.vx = (best.x - inst.x) / d * sp; if (!b.gravity) inst.vy = (best.y - inst.y) / d * sp;
          inst.faceX = inst.vx < 0 ? -1 : 1;
        } else if (best) { inst.vx = 0; if (!behav(inst).gravity) inst.vy = 0; }
      }
      if (bhOf(inst, 'keepOnScreen')) {
        var cam = state.camera, z = cam.zoom || 1, s2 = instSize(inst);
        inst.x = clamp(inst.x, cam.x + s2.w / 2, cam.x + VW / z - s2.w / 2);
        inst.y = clamp(inst.y, cam.y + s2.h / 2, cam.y + VH / z - s2.h / 2);
      }
    }
    function updateBehaviorsPre(dt) {
      var m = state.mouse, i, inst;
      for (i = 0; i < state.instances.length; i++) {
        inst = state.instances[i];
        if (inst.timersOn) for (var tk in inst.timers) if (!inst.timers[tk].paused) inst.timers[tk].t += dt;
        if (inst.fireCD > 0) inst.fireCD -= dt;
        var hasBl = !inst._od || (inst._od.behaviorList && inst._od.behaviorList.length);
        var sn = hasBl ? bhOf(inst, 'sine') : null;
        if (hasBl) behaviorsMove(inst, dt);
        if (sn) {
          inst.sineT = (inst.sineT || 0) + dt;
          var per = Math.max(0.05, n(sn.period, 2)), amp = n(sn.amplitude, 40);
          var off = amp * Math.sin(2 * Math.PI * (inst.sineT / per + n(sn.phase, 0) / 360));
          var d = off - (inst.sineOff || 0);
          inst.sineOff = off;
          if (sn.axis === 'y') inst.y += d; else if (sn.axis === 'angle') inst.angle = n(inst.angle) + d; else inst.x += d;
        }
        if (inst.whiteT > 0) inst.whiteT -= dt;
        if (inst.sqT > 0) inst.sqT -= dt;
        if (inst._od && inst._od.squash) {
          if (inst.evJump === state.frame || inst.evWallJump === state.frame) startSquash(inst, -0.25, 0.18);
          else if (inst.evLand === state.frame) startSquash(inst, 0.3, 0.16);
        }
        if (inst.flashT > 0) {
          inst.flashT -= dt;
          var iv = Math.max(0.02, n(inst.flashIv, 0.1));
          inst.flashHide = inst.flashT > 0 && Math.floor(inst.flashT / iv) % 2 === 0;
        } else inst.flashHide = false;
      }
      /* arrastrar con el raton (el de mas arriba bajo el cursor) */
      if (m.pressed && m.button === 0 && state.dragId == null) {
        for (i = state.instances.length - 1; i >= 0; i--) {
          inst = state.instances[i];
          if (!inst.visible || inst.dragOff || !bhOf(inst, 'draggable')) continue;
          var hud = objDef(inst).hud, px = hud ? m.x : m.wx, py = hud ? m.y : m.wy;
          if (instContains(inst, px, py)) { state.dragId = inst.id; state.dragDX = inst.x - px; state.dragDY = inst.y - py; break; }
        }
      }
      if (state.dragId != null) {
        var di = null;
        for (i = 0; i < state.instances.length; i++) if (state.instances[i].id === state.dragId) di = state.instances[i];
        if (!di) state.dragId = null;
        else if (!m.down || m.released) { di.droppedFrame = state.frame; state.dragId = null; }
        else {
          var dh = objDef(di).hud;
          di.x = (dh ? m.x : m.wx) + state.dragDX; di.y = (dh ? m.y : m.wy) + state.dragDY;
          di.vx = 0; di.vy = 0;
        }
      }
    }
    /* despues de la fisica: destruir fuera de la pantalla */
    var anyOnUpdate = (project.objects || []).some(function (o) { return o.hooks && String(o.hooks.onUpdate || '').trim(); });
    var anyDestroyOutside = (project.objects || []).some(function (o) { return (o.behaviorList || []).some(function (x) { return x.type === 'destroyOutside'; }); });
    function updateBehaviorsPost() {
      if (!anyDestroyOutside) return;
      var cam = state.camera, z = cam.zoom || 1;
      state.instances = state.instances.filter(function (inst) {
        var bo = bhOf(inst, 'destroyOutside');
        if (!bo) return true;
        var sz = instSize(inst), mg = n(bo.margin, 32), sp = instScreenPos(inst);
        var hw = sz.w * z / 2 + mg, hh = sz.h * z / 2 + mg;
        return !(sp.x + hw < 0 || sp.x - hw > VW || sp.y + hh < 0 || sp.y - hh > VH);
      });
    }
    function fireBullet(shooter, bulletId, angDeg, speed) {
      var fb = bhOf(shooter, 'fireBullet');
      if (fb && shooter.fireCD > 0) return null;
      var b = createInstance(bulletId, shooter.x, shooter.y);
      if (!b) return null;
      var a = angDeg * Math.PI / 180;
      b.vx = Math.cos(a) * speed; b.vy = Math.sin(a) * speed; b.angle = angDeg;
      if (fb) shooter.fireCD = Math.max(0, n(fb.cooldown, 0.25));
      return b;
    }
    /* quien esta de pie sobre algo que se movio se mueve con ello (una pasada: cada uno mira solo su suelo) */
    function carryRiders() {
      var L = state.instances, moved = [], i;
      for (i = 0; i < L.length; i++) {
        var r = L[i], p = r.groundRef;
        if (p && r.grounded && p !== r && (p.x !== p.prevX || p.y !== p.prevY)) moved.push(r, p.x - p.prevX, p.y - p.prevY);
      }
      for (i = 0; i < moved.length; i += 3) { moved[i].x += moved[i + 1]; moved[i].y += moved[i + 2]; }
    }
    function slotAnim(obj, slot) {
      var id = obj.animSlots && obj.animSlots[slot];
      if (!id) return null;
      for (var i = 0; i < obj.animations.length; i++) if (obj.animations[i].id === id) return obj.animations[i];
      return null;
    }
    function pickAnim(inst, obj, b, moving) {
      if (inst.animForce) { var f = animFind(obj, inst.animForce); if (f) return f; }
      var chain = [];
      if (b.movement === 'platform') {
        if (inst.dashT > 0) chain = ['dash', 'saltar', 'caer'];
        else if (inst.climbing) chain = ['escalera', 'caminar'];
        else if (!inst.grounded) chain = inst.vy < 0 ? ['saltar', 'caer'] : ['caer', 'saltar'];
        else if (inst.wallSliding) chain = ['pared'];
        else if (inst.crouching) chain = ['agachado'];
        else if (inst.running) chain = ['correr', 'caminar'];
        else if (moving) chain = ['caminar'];
      } else if (moving) chain = ['caminar'];
      if (moving) chain.push('caminar');
      chain.push('quieto');
      for (var i = 0; i < chain.length; i++) {
        var a = slotAnim(obj, chain[i]);
        if (a) return a;
      }
      if (moving && animFind(obj, 'walk')) return animFind(obj, 'walk');
      return animFind(obj, 'idle') || obj.animations[0];
    }
    function updateAnims(dt) {
      for (var i = 0; i < state.instances.length; i++) {
        var inst = state.instances[i], obj = inst._od || objDef(inst);
        if (inst.dropT) inst.dropT = Math.max(0, inst.dropT - dt);
        if (!obj || !obj.animations || !obj.animations.length) continue;
        var b = obj.behaviors || NODEF;
        var moving = Math.abs(inst.vx) > 4 || (inst.climbing && Math.abs(inst.vy) > 4);
        if (obj.flipOnMove !== false && inst.faceX) inst.flipX = inst.faceX < 0 ? -1 : 1;
        var want = pickAnim(inst, obj, b, moving);
        if (want.name !== inst.animName) { inst.animName = want.name; inst.animT = 0; }
        inst._mask = want.collision || obj.collision || null; inst._maskSp = want.spriteId;   /* su colision */
        if (!inst.animPaused) inst.animT += dt * (inst.animSpeed != null ? inst.animSpeed : 1);
        inst.animLen = Math.max(1, sprites[want.spriteId] && sprites[want.spriteId].frames ? sprites[want.spriteId].frames.length : 1);
        inst.animDone = want.loop === false && Math.floor(inst.animT * n(want.fps, 8)) >= inst.animLen;
        var sp = sprites[want.spriteId], len = sp && sp.frames ? sp.frames.length : 1;
        var idx = Math.floor(inst.animT * n(want.fps, 8));
        var nf = want.loop === false ? Math.min(idx, len - 1) : idx % len;
        /* eventos de la animacion ("3:paso, 7:golpe"): se avisan al entrar en ese fotograma */
        if (want.events && (nf !== inst.animFrame || inst.animEvName !== want.name)) {
          var em = want._ev;
          if (!em) {
            em = want._ev = {};
            String(want.events).split(',').forEach(function (pp) { var m = /^\s*(\d+)\s*:\s*(.+?)\s*$/.exec(pp); if (m) (em[+m[1]] || (em[+m[1]] = [])).push(m[2]); });
          }
          if (em[nf]) { inst.animEv = em[nf]; inst.animEvF = state.frame; }
        }
        inst.animEvName = want.name;
        inst.animFrame = nf;
      }
    }

    /* ---------------- fuentes del proyecto ----------------
       "ttf": TTF/OTF importada (data en base64). "pixel": dibujada en el editor, un glifo por caracter de
       chars, de cellW x cellH; con tint (por defecto) los pixeles toman el color del texto. */
    var fontDefs = {};
    (project.fonts || []).forEach(function (f) { if (f && f.id) fontDefs[f.id] = f; });
    function fontB64(f) { var raw = String(f.data || ''), i = raw.indexOf(','); return i >= 0 ? raw.slice(i + 1) : raw; }
    function ttfFamily(f) {
      if (f._family !== undefined) return f._family;
      f._family = null;
      var slot = N.fontLoad(fontB64(f));
      if (slot > 0) f._family = '"s2d:' + slot + '"';
      else console.warn('Source 2D: no se pudo cargar la fuente ' + f.name);
      return f._family;
    }
    function fontCss(fontId, size, bold) {
      var f = fontId && fontDefs[fontId], fam = f && f.kind !== 'pixel' ? ttfFamily(f) : null;
      return (bold ? 'bold ' : '') + size + 'px ' + (fam || '"Segoe UI",sans-serif');
    }
    function pixelFont(fontId) { var f = fontId && fontDefs[fontId]; return f && f.kind === 'pixel' && f.glyphs ? f : null; }
    function pfIndex(f, ch) {
      if (!f._map) {
        f._map = {};
        Array.from(String(f.chars || '')).forEach(function (c, i) { if (!(c in f._map)) f._map[c] = i; });
      }
      if (ch in f._map) return f._map[ch];
      var alt = ch.toUpperCase() === ch ? ch.toLowerCase() : ch.toUpperCase();
      return alt in f._map ? f._map[alt] : -1;
    }
    function pfAdvance(f, gi) {
      if (!f._adv) f._adv = [];
      if (f._adv[gi] !== undefined) return f._adv[gi];
      if (f.glyphs[gi] && f.glyphs[gi].adv != null) return (f._adv[gi] = n(f.glyphs[gi].adv));   /* avance propio del glifo */
      var W = Math.max(1, f.cellW | 0), H = Math.max(1, f.cellH | 0), px = (f.glyphs[gi] && f.glyphs[gi].pixels) || [], right = -1, x, y;
      for (y = 0; y < H; y++) for (x = 0; x < W; x++) if (px[y * W + x] && x > right) right = x;
      return (f._adv[gi] = f.mono ? W : right < 0 ? Math.max(1, Math.round(W / 2)) : right + 1);
    }
    function pfWidth(f, text) {
      var w = 0, sp = f.spacing != null ? n(f.spacing) : 1, cs = Array.from(String(text)), i;
      for (i = 0; i < cs.length; i++) {
        var gi = pfIndex(f, cs[i]);
        w += (gi < 0 ? Math.max(1, Math.round((f.cellW | 0) / 2)) : pfAdvance(f, gi)) + (i + 1 < cs.length ? sp : 0);
      }
      return w;
    }
    function pfCanvas(f, gi, color) {
      if (!f._cv) f._cv = {};
      var tint = f.tint !== false, key = (tint ? color : '') + '#' + gi;
      if (f._cv[key]) return f._cv[key];
      var W = Math.max(1, f.cellW | 0), H = Math.max(1, f.cellH | 0);
      return (f._cv[key] = pixelImage(W, H, (f.glyphs[gi] && f.glyphs[gi].pixels) || [], tint ? color : null));
    }
    /* texto de pixeles con contorno en una imagen (cache): w y h sin el margen pad del contorno.
       El color del texto no va en la clave: si cambia (un titulo que cambia de color cada fotograma) se redibuja la
       misma imagen y en la GPU solo se actualiza su textura, sin crear una nueva cada vez. */
    var olTextCache = {}, olTextCount = 0;
    function outlinedText(f, text, size, color, ocolor, od) {
      var key = f.id + '|' + size + '|' + ocolor + '|' + od + '|' + text, e = olTextCache[key];
      if (e && e.color === color) return e;
      var sc = size / Math.max(1, f.cellH | 0), w = pfWidth(f, text) * sc, h = f.cellH * sc, pad = Math.ceil(od) + 1;
      var cv;
      if (e) { cv = e.cv; cv.ctx.setTransform(1, 0, 0, 1, 0, 0); cv.ctx.clearRect(0, 0, cv.width, cv.height); }
      else {
        if (++olTextCount > 400) { for (var ok in olTextCache) olTextCache[ok].cv.free(); olTextCache = {}; olTextCount = 1; }
        cv = S2D.surface(Math.ceil(w + pad * 2), Math.ceil(h + pad * 2));
      }
      var main = ctx;
      ctx = cv.ctx;
      for (var i = 0; i < 8; i++) {
        ctx.setTransform(1, 0, 0, 1, pad + OL_DX[i] * od, pad + h / 2 + OL_DY[i] * od);
        drawPixelText(f, text, size, ocolor, 'left');
      }
      ctx.setTransform(1, 0, 0, 1, pad, pad + h / 2);
      drawPixelText(f, text, size, color, 'left');
      ctx = main;
      return (olTextCache[key] = { cv: cv, w: w, h: h, pad: pad, color: color });
    }
    /* texto con fuente de pixeles centrado en vertical en (0, 0) con la transformacion actual */
    function drawPixelText(f, text, size, color, align) {
      var sc = size / Math.max(1, f.cellH | 0), w = pfWidth(f, text) * sc, x = align === 'center' ? -w / 2 : align === 'right' ? -w : 0;
      var y = -f.cellH * sc / 2, sp = f.spacing != null ? n(f.spacing) : 1, cs = Array.from(String(text));
      for (var i = 0; i < cs.length; i++) {
        var gi = pfIndex(f, cs[i]);
        if (gi < 0) { x += (Math.max(1, Math.round((f.cellW | 0) / 2)) + sp) * sc; continue; }
        ctx.drawImage(pfCanvas(f, gi, color), x, y, f.cellW * sc, f.cellH * sc);
        x += (pfAdvance(f, gi) + sp) * sc;
      }
    }

    /* tamano de una instancia; el de los sprites se guarda en la instancia mientras no cambien su escala ni su sprite */
    function instSize(inst) {
      var obj = objDef(inst), c = inst._sz;
      if (c && c.obj === obj && c.sx === inst.scaleX && c.sy === inst.scaleY && c.mk === inst._mask && c.fx === inst.flipX) return c;
      var r = instSizeCalc(inst, obj);
      if (!obj.type || obj.type === 'sprite') { r.obj = obj; r.sx = inst.scaleX; r.sy = inst.scaleY; r.mk = inst._mask; r.fx = inst.flipX; inst._sz = r; }
      return r;
    }
    /* Colision con forma propia (pestana Animaciones > Colision): la de la animacion que se ve o, si no tiene, la del
       objeto. {shape: rect|square|circle|ellipse|poly, x, y, w, h, points: [[x, y]...]} en pixeles del sprite, con el
       origen arriba a la izquierda. Para moverse y chocar con el escenario cuenta su caja (que puede no estar centrada:
       agachado, la caja baja); las condiciones de colision entre objetos usan la forma exacta. */
    function maskOf(inst, obj) {
      var m = inst._mask !== undefined ? inst._mask : obj.collision;
      return m && m.shape && m.shape !== 'none' ? m : null;
    }
    function maskBounds(m) {
      if (m.shape === 'poly' && m.points && m.points.length) {
        var x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        for (var i = 0; i < m.points.length; i++) {
          var q = m.points[i];
          x0 = Math.min(x0, n(q[0], 0)); y0 = Math.min(y0, n(q[1], 0)); x1 = Math.max(x1, n(q[0], 0)); y1 = Math.max(y1, n(q[1], 0));
        }
        return [x0, y0, x1, y1];
      }
      var x = n(m.x, 0), y = n(m.y, 0);
      return [x, y, x + Math.max(0, n(m.w, 0)), y + Math.max(0, n(m.h, 0))];
    }
    /* contorno de la forma en pixeles del sprite (circulos y elipses como poligonos de 16 lados) */
    function maskOutline(m) {
      if (m.shape === 'poly' && m.points && m.points.length >= 3) return m.points.map(function (q) { return [n(q[0], 0), n(q[1], 0)]; });
      var b = maskBounds(m);
      if (m.shape === 'circle' || m.shape === 'ellipse') {
        var cx = (b[0] + b[2]) / 2, cy = (b[1] + b[3]) / 2, rx = (b[2] - b[0]) / 2, ry = (b[3] - b[1]) / 2, out = [];
        for (var k = 0; k < 16; k++) out.push([cx + rx * Math.cos(k * Math.PI / 8), cy + ry * Math.sin(k * Math.PI / 8)]);
        return out;
      }
      return [[b[0], b[1]], [b[2], b[1]], [b[2], b[3]], [b[0], b[3]]];
    }
    function instSizeCalc(inst, obj) {
      var w = 32, h = 32;
      if (obj.type === 'text') {
        var content = (inst.textOverride != null ? inst.textOverride : (obj.text && obj.text.content) || 'Texto');
        var size = n(obj.text && obj.text.size, 24), pf = pixelFont(obj.text && obj.text.font);
        var tl = resolveStr(content).replace(/\[\/?(color|wave|shake)(=[^\]]*)?\]/g, '').split('\n'), tw = 0;
        /* ancho de verdad (la misma medida que al dibujar), no una estimacion por numero de letras */
        var of = ctx.font;
        if (!pf) ctx.font = fontCss(obj.text && obj.text.font, size, !obj.text || obj.text.bold !== false);
        for (var tk = 0; tk < tl.length; tk++) tw = Math.max(tw, pf ? pfWidth(pf, tl[tk]) * size / Math.max(1, pf.cellH | 0) : ctx.measureText(tl[tk]).width);
        ctx.font = of;
        w = tw; h = pf ? size * tl.length : size * 1.35 + size * 1.25 * (tl.length - 1);
      } else if (obj.type === 'button') {
        var bt = obj.button || {};
        w = n(bt.width, 160); h = n(bt.height, 42);
      } else {
        var sp = sprites[inst._maskSp] || sprites[obj.spriteId];
        if (sp) { w = sp.width; h = sp.height; }
        var mk = maskOf(inst, obj);
        if (mk) {
          var bb = maskBounds(mk), sx = inst.scaleX || 1, sy = inst.scaleY || 1, fl = inst.flipX === -1 ? -1 : 1;
          return { w: Math.abs((bb[2] - bb[0]) * sx), h: Math.abs((bb[3] - bb[1]) * sy), ox: ((bb[0] + bb[2]) / 2 - w / 2) * sx * fl,
                   oy: ((bb[1] + bb[3]) / 2 - h / 2) * sy, mask: mk, sw: w, sh: h };
        }
      }
      /* caja de colision propia (ancho/alto en pixeles, centrada; 0 = la del sprite). No cambia con la escala: asi
         estirar y aplastar al saltar o aterrizar es solo visual y no altera la fisica */
      var hb = obj.hitbox, hw = hb ? n(hb.w, 0) : 0, hh = hb ? n(hb.h, 0) : 0;
      return { w: hw > 0 ? hw : Math.abs(w * (inst.scaleX || 1)), h: hh > 0 ? hh : Math.abs(h * (inst.scaleY || 1)) };
    }
    function boxesOverlap(ax, ay, aw, ah, bx, by, bw, bh) {
      return Math.abs(ax - bx) * 2 < aw + bw && Math.abs(ay - by) * 2 < ah + bh;
    }
    /* centro de la caja de colision (con forma propia puede no estar en el centro del dibujo) */
    function cX(inst, s) { return s.ox ? inst.x + s.ox : inst.x; }
    function cY(inst, s) { return s.oy ? inst.y + s.oy : inst.y; }
    /* la forma en el mundo (girada con la instancia) */
    function worldShape(inst, s) {
      var pts, sx = inst.scaleX || 1, sy = inst.scaleY || 1, fl = inst.flipX === -1 ? -1 : 1;
      if (s.mask) pts = maskOutline(s.mask).map(function (q) { return [(q[0] - s.sw / 2) * sx * fl, (q[1] - s.sh / 2) * sy]; });
      else { var w2 = s.w / 2, h2 = s.h / 2; pts = [[-w2, -h2], [w2, -h2], [w2, h2], [-w2, h2]]; }
      var a = s.mask ? n(inst.angle, 0) * Math.PI / 180 : 0, co = Math.cos(a), si = Math.sin(a);
      for (var i = 0; i < pts.length; i++) {
        var x = pts[i][0], y = pts[i][1];
        pts[i] = [inst.x + x * co - y * si, inst.y + x * si + y * co];
      }
      return pts;
    }
    /* separacion de ejes entre dos poligonos convexos (los de puntos sueltos cuentan como su envolvente) */
    function polysOverlap(A, B) {
      for (var pass = 0; pass < 2; pass++) {
        var P = pass ? B : A;
        for (var i = 0; i < P.length; i++) {
          var j = (i + 1) % P.length, nx = P[i][1] - P[j][1], ny = P[j][0] - P[i][0];
          var a0 = 1e18, a1 = -1e18, b0 = 1e18, b1 = -1e18, k, d;
          for (k = 0; k < A.length; k++) { d = A[k][0] * nx + A[k][1] * ny; if (d < a0) a0 = d; if (d > a1) a1 = d; }
          for (k = 0; k < B.length; k++) { d = B[k][0] * nx + B[k][1] * ny; if (d < b0) b0 = d; if (d > b1) b1 = d; }
          if (a1 <= b0 || b1 <= a0) return false;
        }
      }
      return true;
    }
    /* dos instancias se tocan: por su caja y, si alguna tiene forma propia, por la forma exacta */
    function instOverlap(a, b) {
      var sa = instSize(a), sb = instSize(b);
      if (!boxesOverlap(cX(a, sa), cY(a, sa), sa.w, sa.h, cX(b, sb), cY(b, sb), sb.w, sb.h)) return false;
      if (!sa.mask && !sb.mask) return true;
      return polysOverlap(worldShape(a, sa), worldShape(b, sb));
    }
    function instScreenPos(inst) {
      if (objDef(inst).hud) return { x: inst.x, y: inst.y };
      return {
        x: (inst.x - state.camera.x) * state.camera.zoom,
        y: (inst.y - state.camera.y) * state.camera.zoom
      };
    }
    function hitTest(inst, mx, my) {
      var p = instScreenPos(inst), r = objDef(inst).hud ? 0 : n(state.camera.rot, 0) * Math.PI / 180;
      if (r) {                                   /* camara girada: el raton se lleva al marco sin giro (alrededor del centro) */
        var dx = mx - VW / 2, dy = my - VH / 2, co = Math.cos(r), si = Math.sin(r);
        mx = co * dx + si * dy + VW / 2; my = -si * dx + co * dy + VH / 2;
      }
      var s = instSize(inst);
      var z = objDef(inst).hud ? 1 : state.camera.zoom;
      return Math.abs(mx - p.x) * 2 < s.w * z && Math.abs(my - p.y) * 2 < s.h * z;
    }
    /* Textos con {g:var} (variable) y {= expresion}; cada plantilla se analiza una sola vez. */
    var tplCache = Object.create(null);
    function fmtVal(v) {
      if (v == null) return '';
      if (typeof v === 'number') return v === Math.floor(v) ? String(v) : String(Math.round(v * 1e4) / 1e4);
      return String(v);
    }
    function compileTpl(s) {
      if (s.indexOf('{') < 0) return s;
      var parts = [], i = 0, lit = '';
      while (i < s.length) {
        if (s.substr(i, 3) === '{t:') {                          /* {t:clave}: el texto de esa clave en el idioma actual */
          var tj = s.indexOf('}', i);
          if (tj > i) { if (lit) { parts.push(lit); lit = ''; } parts.push({ v: null, e: 'T(' + JSON.stringify(s.slice(i + 3, tj).trim()) + ')' }); i = tj + 1; continue; }
        }
        if (s.charAt(i) === '{' && (s.substr(i, 3) === '{g:' || s.charAt(i + 1) === '=')) {
          var isVar = s.charAt(i + 1) === 'g', j = i + (isVar ? 3 : 2), depth = 1, q = '';
          for (; j < s.length; j++) {
            var ch = s.charAt(j);
            if (q) { if (ch === '\\') j++; else if (ch === q) q = ''; continue; }
            if (ch === '"' || ch === "'") q = ch;
            else if (ch === '{') depth++;
            else if (ch === '}' && --depth === 0) break;
          }
          if (j >= s.length) { lit += s.slice(i); break; }
          if (lit) { parts.push(lit); lit = ''; }
          parts.push({ v: isVar ? s.slice(i + 3, j) : null, e: isVar ? null : s.slice(i + 2, j) });
          i = j + 1;
        } else lit += s.charAt(i++);
      }
      if (lit) parts.push(lit);
      return parts.length === 1 && typeof parts[0] === 'string' ? parts[0] : parts;
    }
    function resolveStr(s) {
      if (s == null) return '';
      if (typeof s === 'number') return String(s);
      var parts = tplCache[s];
      if (parts === undefined) parts = tplCache[s] = compileTpl(String(s));
      if (typeof parts === 'string') return parts;
      var out = '';
      for (var i = 0; i < parts.length; i++) {
        var pt = parts[i];
        if (typeof pt === 'string') out += pt;
        else if (pt.v !== null) { var vv = varHome(pt.v)[pt.v]; out += vv == null ? '' : fmtVal(vv); }
        else out += fmtVal(evalExpr(pt.e));
      }
      return out;
    }
    function compare(a, op, b) {
      var na = parseFloat(a), nb = parseFloat(b);
      var numeric = !isNaN(na) && !isNaN(nb) && String(a).trim() !== '' && String(b).trim() !== '';
      var A = numeric ? na : String(a), B = numeric ? nb : String(b);
      switch (op) {
        case 'ne': return A != B;
        case 'lt': return A < B;
        case 'le': return A <= B;
        case 'gt': return A > B;
        case 'ge': return A >= B;
        default: return A == B;
      }
    }
    function createInstance(objId, x, y, extra) {
      var obj = objects[objId]; if (!obj) return null;
      extra = extra || {};
      var inst = {
        id: state.nextId++, objId: objId, _od: obj, x: n(x), y: n(y), vx: 0, vy: 0,
        angle: n(extra.angle), scaleX: extra.scaleX != null ? n(extra.scaleX, 1) : 1,
        scaleY: extra.scaleY != null ? n(extra.scaleY, 1) : 1,
        opacity: extra.opacity != null ? n(extra.opacity, 100) : 100, z: n(extra.z, 0),
        visible: true, frame: extra.frame ? n(extra.frame) : 0,
        grounded: false, vars: {}, textOverride: null,
        moveTarget: null, moveSpeed: 0, spin: n(extra.spin) || n((obj.behaviors || {}).spin), tweens: [], effectFilter: null,
        animName: '', animT: 0, animFrame: 0, flipX: 1, faceX: 1,
        ox: n(x), oy: n(y), pp: 0, pdir: 1, prevX: n(x), prevY: n(y), groundRef: null
      };
      (obj.vars || []).forEach(function (v) { inst.vars[v.name] = v.value; });
      if (extra.vars) for (var vk in extra.vars) inst.vars[vk] = extra.vars[vk];   /* variables de la instancia en la escena */
      inst.timers = {}; inst.fireCD = 0; inst.sim = {}; inst.simNext = {};
      var hb = (obj.behaviorList || []).filter(function (x) { return x.type === 'health'; })[0];
      if (hb) { inst.hpMax = n(hb.max, 100); inst.hp = hb.start != null && hb.start !== '' ? n(hb.start, inst.hpMax) : inst.hpMax; }
      state.instances.push(inst);
      if (obj.hooks && obj.hooks.onCreate) runCode(obj.hooks.onCreate, inst, 'onCreate');
      return inst;
    }
    /* Mientras se evalua una condicion, condScope limita las instancias candidatas a las ya
       seleccionadas por condiciones anteriores (o por el evento padre), como en GDevelop. */
    var condScope = null;
    function objInstances(objId) {
      instIndex();
      var all = ixByObj[objId] || groupInstances(objId);
      var set = condScope && condScope[objId];
      return set ? all.filter(function (i) { return set[i.id]; }) : all.slice();
    }
    function loadScene(id, keepTransition) {
      state.timers = {}; state.timeScale = 1; state.waits = []; state.paused = false; state.darkness = null; state.darkColor = null; state.layerDark = {};
      clearParticles(); invalidateTiles();
      var sc = scenes[id]; if (!sc) return;
      state.sceneId = id; state.instances = []; state.frame = 0;
      /* variables de escena: se quitan al salir de la escena anterior y se cargan las de esta */
      (state.sceneVarNames || []).forEach(function (k) { delete state.globals[k]; });
      state.sceneVarNames = [];
      (sc.vars || []).forEach(function (g) {
        if (!g || !g.name) return;
        var tp = g.type || 'normal', k = g.name;
        state.sceneVarNames.push(k);
        if (tp === 'bool') state.globals[k] = g.value === true || g.value === 'true';
        else if (tp === 'text') state.globals[k] = g.value == null ? '' : String(g.value);
        else if (tp === 'array') state.globals[k] = Array.isArray(g.value) ? g.value.slice() : [];
        else state.globals[k] = g.value;
      });
      (sc.instances || []).forEach(function (it) { createInstance(it.objectId, it.x, it.y, it); });
      /* eventos comunes (de todas las escenas) y luego los de esta escena */
      state.eventState = (project.events || []).concat(sc.events || []).filter(function (ev) { return !ev.kind; }).map(function (ev) { return { ref: ev, acc: {}, once: {} }; });
      objIdByName = null;
      /* las funciones (eventos de tipo "function" de primer nivel) salen del arbol y se guardan por nombre */
      fnTable = {};
      state.eventTree = buildEventTree(state.eventState).filter(function (node) {
        var ev = node.es.ref;
        if (ev.type !== 'function') return true;
        node.pnames = String(ev.params || '').split(',').map(function (x) { return x.trim().replace(/^\$/, ''); }).filter(Boolean);
        if (ev.name && ev.enabled !== false) fnTable[String(ev.name).trim()] = node;
        return false;
      });
      linkFunctions();
      var cam = state.camera;
      cam.x = 0; cam.y = 0; cam.followId = null; cam.moveTo = null;
      cam.zoom = n(sc.camera && sc.camera.zoom, 1) || 1;
      cam.smooth = n(sc.camera && sc.camera.smooth, 0);
      cam.shakeT = 0; cam.rot = 0;
      state.bg = null; state.screenShaders = []; state.shaderAmt = state.shaderAmt || {};
      cam.limits = (sc.camera && sc.camera.limits === 'custom')
        ? { x: n(sc.camera.lx), y: n(sc.camera.ly), w: n(sc.camera.lw, VW), h: n(sc.camera.lh, VH) }
        : sc.camera && sc.camera.limits === 'none' ? 'none' : null;
      state.selection = {}; state.boxStart = null;
      state.ended = false; state.endMessage = ''; state.sceneChanged = true;
      if (!keepTransition) state.transition = null;
    }

    /* ---------------- codigo personalizado ---------------- */
    var codeCache = {};
    function compile(code) {
      if (codeCache[code] === undefined) {
        try { codeCache[code] = new Function('$', code); }
        catch (e) { codeCache[code] = null; console.warn('Source 2D: error de sintaxis en codigo JavaScript: ' + e.message); }
      }
      return codeCache[code];
    }
    function apiFor(inst) {
      var cam = state.camera;
      return {
        state: state, project: project, inst: inst, time: state.time, dt: state.dt,
        vars: state.globals,
        mouse: state.mouse,
        keys: state.keys,
        Math: Math, random: Math.random,
        log: function (m) { console.log(m); },
        num: n, clamp: clamp,
        instOf: function (name) {
          return objIdNamed(name);
        },
        all: function (name) {
          var id = objIdNamed(name);
          return id ? objInstances(id) : [];
        },
        byId: function (id) { return objects[id]; },
        listOf: function (name) {
          var id = objIdNamed(name);
          return id ? objInstances(id) : [];
        },
        create: function (name, x, y) {
          var id = objIdNamed(name);
          return id ? createInstance(id, x, y) : null;
        },
        remove: function (i) {
          state.instances = state.instances.filter(function (x) { return x !== i; });
        },
        collide: function (a, b) { return instOverlap(a, b); },
        camera: {
          get x() { return cam.x; }, get y() { return cam.y; }, get zoom() { return cam.zoom; },
          follow: function (i, smooth) { cam.followId = i ? i.id : null; if (smooth != null) cam.smooth = smooth; },
          moveTo: function (x, y, speed) { cam.moveTo = { x: x, y: y, speed: speed || 600 }; },
          zoomTo: function (z) { cam.zoom = clamp(z, 0.2, 4); },
          shake: function (mag, dur) { cam.shakeMag = mag || 10; cam.shakeT = dur || 0.3; }
        },
        scene: {
          change: function (name, transition, dur) { changeScene(name, transition, dur); },
          restart: function () { loadScene(state.sceneId, true); }
        },
        tween: function (i, prop, to, dur, easing) { startTween(i, prop, to, dur, easing); },
        call: function (name) { return callFn(name, Array.prototype.slice.call(arguments, 1)); },   /* funcion de eventos */
        beep: beep,
        playMusic: playMusicTrack,
        stopMusic: stopMusicTrack
      };
    }
    function runCode(code, inst, label) {
      var fn = compile(code);
      if (!fn) return undefined;
      try { return fn(apiFor(inst)); }
      catch (e) {
        console.warn('Source 2D [' + (label || 'codigo') + ']: ' + e.message);
        return undefined;
      }
    }

    /* ---------------- variables: locales de la funcion en curso o globales ----------------
       Una variable se busca primero entre las locales de la funcion que se esta ejecutando y si no, en las
       globales (que incluyen las de la escena). "nombre[i]" es un elemento de un array. */
    var frames = [];
    function varHome(name) {
      if (frames.length) { var f = frames[frames.length - 1].vars; if (name in f) return f; }
      return state.globals;
    }
    var lvCache = Object.create(null);
    function lvalue(raw) {
      var c = lvCache[raw];
      if (c) return c;
      var s = String(raw == null ? '' : raw).trim();
      if (s.charAt(0) === '$') s = s.slice(1);
      var m = /^([^\[\]]+?)\s*\[(.*)\]$/.exec(s);
      c = lvCache[raw] = m ? { name: m[1].trim(), idx: m[2] } : { name: s, idx: null };
      return c;
    }
    function readVar(raw) {
      var lv = lvalue(raw), v = varHome(lv.name)[lv.name];
      if (lv.idx === null) return v;
      return v && typeof v === 'object' ? v[resolveVal(lv.idx) | 0] : undefined;
    }
    function writeVar(raw, val) {
      var lv = lvalue(raw), h = varHome(lv.name);
      if (lv.idx === null) { h[lv.name] = val; return; }
      var a = h[lv.name];
      if (!Array.isArray(a)) a = h[lv.name] = [];
      a[resolveVal(lv.idx) | 0] = val;
    }
    function arrayVar(raw) {
      var lv = lvalue(raw), h = varHome(lv.name), a = h[lv.name];
      if (!Array.isArray(a)) a = h[lv.name] = [];
      return a;
    }
    /* los textos que son numeros se guardan como numeros (las comparaciones y los arrays van mas rapido) */
    function autoNum(s) { return s !== '' && /^\s*-?(\d+\.?\d*|\.\d+)\s*$/.test(s) ? parseFloat(s) : s; }

    /* ---------------- funciones de eventos ----------------
       Un evento de tipo "function" con nombre y parametros no se ejecuta solo: se llama con la accion
       "Llamar a la funcion" o con Fn.nombre(...) en una expresion. Cada llamada tiene sus variables locales
       (los parametros incluidos), asi que puede ser recursiva. */
    var FLOW_BREAK = 2, FLOW_RETURN = 3, flow = 0, fnTable = {}, warnedFn = {};
    function callFn(name, args) {
      var node = fnTable[name];
      if (!node) {
        if (!warnedFn[name]) { warnedFn[name] = true; console.warn('Source 2D: la funcion "' + name + '" no existe'); }
        return 0;
      }
      if (node.js) return node.js.apply(null, args || []);
      if (frames.length >= 400) return 0;
      var vars = Object.create(null), pn = node.pnames, fr = { vars: vars, ret: 0 };
      for (var i = 0; i < pn.length; i++) vars[pn[i]] = args && i < args.length ? args[i] : 0;
      var savedPicks = exprPicks;
      frames.push(fr);
      var fp = node.fast;
      if (fp === undefined) fp = node.fast = fastPlan(node, true);
      try { if (fp) runFast(node, fp, {}); else runBody(node, {}); } finally { frames.pop(); flow = 0; exprPicks = savedPicks; }
      return fr.ret;
    }

    /* ---------------- expresiones ----------------
       Cada texto se traduce una sola vez a una funcion de JavaScript y se guarda: $var, $array[i] (o el nombre
       sin $), {g:var}, Objeto.Propiedad, Fn.funcion(...), cursor.X(), textos entre comillas, las funciones
       de EXPR_FN y los operadores de JS (tambien los de bits: & | ^ ~ << >>). */
    var EXPR_FN = {
      abs: Math.abs, min: Math.min, max: Math.max, floor: Math.floor, ceil: Math.ceil, round: Math.round, sqrt: Math.sqrt,
      sin: Math.sin, cos: Math.cos, tan: Math.tan, atan2: Math.atan2, pow: Math.pow, PI: Math.PI, random: Math.random, clamp: clamp,
      sign: Math.sign, int: Math.trunc, num: function (v) { return n(v); }, str: fmtVal,
      len: function (v) { return v == null ? 0 : (v.length | 0); },
      substr: function (s, i, k) { return String(s == null ? '' : s).substr(i, k); },
      randInt: function (a, b) { return a + Math.floor(Math.random() * (b - a + 1)); },
      T: function (k) { return translate(k); },
      Idioma: function () { return curLang(); },
      RayoX: function () { return state.ray ? state.ray.x : 0; },
      SteamNombre: function () { try { return N.steam ? N.steam(12, 0) : ''; } catch (e) { return ''; } },
      SteamIdioma: function () { try { return N.steam ? N.steam(12, 1) : ''; } catch (e) { return ''; } },
      SteamEstadistica: function (k) { try { return N.steam ? N.steam(5, String(k)) : 0; } catch (e) { return 0; } },
      RayoY: function () { return state.ray ? state.ray.y : 0; },
      RayoDistancia: function () { return state.ray ? state.ray.dist : 0; },
      EjeMando: function (pad, axis) { try { return N.pad ? N.pad(0, (pad || 1) - 1, axis || 0) : 0; } catch (e) { return 0; } },
      lerp: function (a, b, t) { return a + (b - a) * t; },
      /* segundos que duro el ultimo fotograma (para mover o girar a velocidad constante) */
      TiempoDelta: function () { return state.dt; },
      /* teclado: nombre de la tecla que se acaba de pulsar (o "") y letras escritas en este fotograma */
      UltimaTecla: function () { return state.lastKey || ''; },
      Tecleado: function () { return state.typed || ''; },
      /* textos */
      lower: function (s) { return String(s == null ? '' : s).toLowerCase(); },
      upper: function (s) { return String(s == null ? '' : s).toUpperCase(); },
      trim: function (s) { return String(s == null ? '' : s).trim(); },
      indexOf: function (s, t) { return String(s == null ? '' : s).indexOf(String(t)); },
      lastIndexOf: function (s, t) { return String(s == null ? '' : s).lastIndexOf(String(t)); },
      startsWith: function (s, t) { return String(s == null ? '' : s).indexOf(String(t)) === 0; },
      endsWith: function (s, t) { s = String(s == null ? '' : s); t = String(t); return s.length >= t.length && s.slice(s.length - t.length) === t; },
      replace: function (s, a, b) { return String(s == null ? '' : s).split(String(a)).join(String(b)); },
      padStart: function (s, k, c) { s = String(s == null ? '' : s); c = String(c == null ? ' ' : c) || ' '; while (s.length < k) s = c + s; return s; },
      fixed: function (v, d) { return Number(v || 0).toFixed(d | 0); },
      /* numero de un texto hexadecimal ("ff" -> 255); con i, el byte i de un color "#rrggbb" */
      toHex: function (v, d) { var t = Math.max(0, Math.round(Number(v) || 0)).toString(16); while (t.length < (d || 0)) t = '0' + t; return t; },
      hex: function (s, i) { s = String(s == null ? '' : s).replace(/^#/, ''); if (i != null) s = s.substr((i | 0) * 2, 2); var v = parseInt(s, 16); return isNaN(v) ? 0 : v; },
      /* carpetas del usuario (en el jugador nativo; en el navegador, "") */
      DirUsuario: function () { return nativePaths()[0]; },
      DirDatos: function () { return nativePaths()[1]; },
      DirDocumentos: function () { return nativePaths()[2]; },
      ArchivoExiste: function (path) { var nt = nativeFS(); try { return !!(nt && nt.fileRead(String(path)) != null); } catch (e) { return false; } }
    };
    var pathsCache = null;
    function nativeFS() { return N; }
    function nativePaths() {
      if (!pathsCache) { var nt = nativeFS(); try { pathsCache = nt ? nt.paths() : ['', '', '']; } catch (e) { pathsCache = ['', '', '']; } }
      return pathsCache;
    }
    /* texto de un archivo inicial del proyecto (project.files: { name, text } o { name, data: base64 }) */
    function utf8Decode(bin) {
      var out = '', i = 0, c;
      while (i < bin.length) {
        c = bin.charCodeAt(i++);
        if (c < 0x80) out += String.fromCharCode(c);
        else if (c < 0xe0) out += String.fromCharCode(((c & 31) << 6) | (bin.charCodeAt(i++) & 63));
        else if (c < 0xf0) { out += String.fromCharCode(((c & 15) << 12) | ((bin.charCodeAt(i) & 63) << 6) | (bin.charCodeAt(i + 1) & 63)); i += 2; }
        else { var cp = ((c & 7) << 18) | ((bin.charCodeAt(i) & 63) << 12) | ((bin.charCodeAt(i + 1) & 63) << 6) | (bin.charCodeAt(i + 2) & 63); i += 3; cp -= 0x10000; out += String.fromCharCode(0xd800 + (cp >> 10), 0xdc00 + (cp & 1023)); }
      }
      return out;
    }
    function initialFile(name) {
      var list = project.files || [], i;
      for (i = 0; i < list.length; i++) if (list[i].name === name || list[i].id === name) {
        var f = list[i];
        if (f.text != null) return String(f.text);
        var raw = String(f.data || ''), k = raw.indexOf(',');
        try { return utf8Decode(atob(k >= 0 ? raw.slice(k + 1) : raw)); } catch (e) { return ''; }
      }
      return null;
    }
    /* JSON <-> variables: los objetos se aplanan en variables hijas (prefijo.clave.subclave); los arrays quedan como arrays */
    function clearVars(prefix) {
      var pre = prefix + '.', k;
      for (k in state.globals) if (k.indexOf(pre) === 0) delete state.globals[k];
    }
    function flattenInto(prefix, v) {
      if (v && typeof v === 'object' && !Array.isArray(v)) { for (var k in v) flattenInto(prefix + '.' + k, v[k]); return; }
      state.globals[prefix] = typeof v === 'boolean' ? (v ? 1 : 0) : v == null ? '' : v;
    }
    function varsToObject(prefix) {
      var pre = prefix + '.', out = {}, k;
      for (k in state.globals) {
        if (k.indexOf(pre) !== 0) continue;
        var parts = k.slice(pre.length).split('.'), o = out;
        for (var i = 0; i < parts.length - 1; i++) { if (!o[parts[i]] || typeof o[parts[i]] !== 'object' || Array.isArray(o[parts[i]])) o[parts[i]] = {}; o = o[parts[i]]; }
        o[parts[parts.length - 1]] = state.globals[k];
      }
      return out;
    }
    /* imagen (PNG/JPG en base64) para una instancia: se decodifica una vez por clave */
    var imageCache = {};
    function imageFromB64(key, b64) {
      if (key && imageCache[key]) return imageCache[key];
      var raw = String(b64 || ''), k = raw.indexOf(','), data = k >= 0 && raw.slice(0, 5) === 'data:' ? raw.slice(k + 1) : raw;
      if (!data) return null;
      var img = S2D.decodeImage(data);
      if (img && key) imageCache[key] = img;
      return img;
    }
    var FN_NAMES = Object.keys(EXPR_FN), FN_VALS = FN_NAMES.map(function (k) { return EXPR_FN[k]; });
    var IDENT = /[A-Za-z0-9_À-ɏ]/;
    var exprCache = Object.create(null), objNameSet = null;
    function exprVar(name) { var v = varHome(name)[name]; return v === undefined ? 0 : v; }
    function exprElem(name, i) { var a = varHome(name)[name]; if (!a || typeof a !== 'object') return 0; var v = a[i]; return v === undefined ? 0 : v; }
    function exprObj(name, prop) { var inst = exprInst(name); return inst ? exprProp(inst, prop) : 0; }
    function matchClose(s, i, open, close) {
      for (var d = 0, q = ''; i < s.length; i++) {
        var c = s.charAt(i);
        if (q) { if (c === '\\') i++; else if (c === q) q = ''; continue; }
        if (c === '"' || c === "'") q = c;
        else if (c === open) d++;
        else if (c === close && --d === 0) return i;
      }
      return -1;
    }
    /* cx (opcional): traduccion para una funcion compilada a JS (ver compileFunctionJS): las variables locales
       son variables de JS (cx.loc[nombre]) y el resto se lee de G (las globales). */
    function exVar(name, cx) {
      if (!cx) return 'V(' + JSON.stringify(name) + ')';
      return cx.loc[name] || '(G[' + JSON.stringify(name) + '] ?? 0)';
    }
    function exElem(name, idx, cx) {
      if (!cx) return 'E(' + JSON.stringify(name) + ',' + idx + ')';
      return '(' + (cx.loc[name] || 'G[' + JSON.stringify(name) + ']') + '?.[' + idx + '] ?? 0)';
    }
    function translateExpr(s, cx) {
      if (!objNameSet) { objNameSet = {}; project.objects.forEach(function (o) { if (o.name) objNameSet[o.name] = true; }); }
      var out = '', i = 0, L = s.length, j, k;
      while (i < L) {
        var c = s.charAt(i);
        if (c === '"' || c === "'") {
          for (j = i + 1; j < L && s.charAt(j) !== c; j++) if (s.charAt(j) === '\\') j++;
          out += s.slice(i, j + 1); i = j + 1; continue;
        }
        if (c === '{' && s.substr(i, 3) === '{g:') {
          j = s.indexOf('}', i); if (j < 0) j = L;
          out += exVar(s.slice(i + 3, j), cx); i = j + 1; continue;
        }
        var dollar = c === '$';
        if (dollar || (IDENT.test(c) && !/[0-9]/.test(c))) {
          j = dollar ? i + 1 : i;
          while (j < L && IDENT.test(s.charAt(j))) j++;
          var word = s.slice(dollar ? i + 1 : i, j);
          /* el nombre de una variable puede llevar puntos (variables hijas): $padre.hija */
          if (dollar) while (s.charAt(j) === '.' && IDENT.test(s.charAt(j + 1) || '')) { k = j + 1; while (k < L && IDENT.test(s.charAt(k))) k++; word = s.slice(i + 1, k); j = k; }
          var prop = '';
          if (!dollar && s.charAt(j) === '.' && IDENT.test(s.charAt(j + 1) || '')) {
            k = j + 1; while (k < L && IDENT.test(s.charAt(k))) k++;
            prop = s.slice(j + 1, k);
          }
          if (prop && word === 'Fn' && s.charAt(k) === '(') {
            var e1 = matchClose(s, k, '(', ')'); if (e1 < 0) throw new Error('falta cerrar un parentesis');
            var fargs = translateExpr(s.slice(k + 1, e1), cx);
            if (!cx) out += 'F(' + JSON.stringify(prop) + ',[' + fargs + '])';
            else { if (!fnTable[prop]) throw new Error('la funcion ' + prop + ' no existe'); out += 'FN[' + JSON.stringify(prop) + '](' + fargs + ')'; }
            i = e1 + 1; continue;
          }
          if (prop && word === 'cursor') {
            out += 'C(' + JSON.stringify(prop) + ')'; i = k;
            if (s.substr(i, 2) === '()') i += 2;
            continue;
          }
          if (prop && word === 'Math') { i = j + 1; continue; }
          if (prop && objNameSet[word]) {
            if (cx) cx.obj = true;
            if (!cx && hoistCtx) {                     /* cada objeto se busca una sola vez por evaluacion (o0, o1...) */
              var hi = hoistCtx.indexOf(word);
              if (hi < 0) { hi = hoistCtx.length; hoistCtx.push(word); }
              out += propCode('o' + hi, prop);
            } else out += 'O(' + JSON.stringify(word) + ',' + JSON.stringify(prop) + ')';
            i = k; continue;
          }
          if (!dollar && (word in EXPR_FN || word === 'true' || word === 'false')) { out += word; i = j; continue; }
          if (s.charAt(j) === '[') {
            var e2 = matchClose(s, j, '[', ']'); if (e2 < 0) throw new Error('falta cerrar un corchete');
            out += exElem(word, translateExpr(s.slice(j + 1, e2), cx), cx); i = e2 + 1; continue;
          }
          out += exVar(word, cx); i = j; continue;
        }
        if (/[0-9.]/.test(c)) { j = i; while (j < L && /[0-9A-Za-z_.]/.test(s.charAt(j))) j++; out += s.slice(i, j); i = j; continue; }
        out += c; i++;
      }
      return out;
    }
    /* Objeto.Prop: X/Y directos, propiedades del motor por exprProp y variables de instancia directas */
    var hoistCtx = null;
    var BUILTIN_PROPS = { X: 1, x: 1, Y: 1, y: 1, Angulo: 1, angle: 1, Ancho: 1, width: 1, Alto: 1, height: 1, Opacidad: 1, opacity: 1,
      VelX: 1, vx: 1, VelY: 1, vy: 1, Salud: 1, health: 1 };
    function propCode(v, prop) {
      if (prop === 'X' || prop === 'x') return '(' + v + ' ? ' + v + '.x : 0)';
      if (prop === 'Y' || prop === 'y') return '(' + v + ' ? ' + v + '.y : 0)';
      return (BUILTIN_PROPS[prop] ? 'PR(' : 'PV(') + v + ',' + JSON.stringify(prop) + ')';
    }
    function exprPR(inst, prop) { return inst ? exprProp(inst, prop) : 0; }
    function exprPV(inst, prop) {
      if (!inst || !inst.vars) return 0;
      var vv = inst.vars[prop];
      if (typeof vv === 'number') return vv;
      if (vv === undefined && !Object.prototype.hasOwnProperty.call(inst.vars, prop)) return 0;
      return autoNum(String(vv));
    }
    var ZERO = function () { return 0; };
    function compileExpr(raw) {
      var f = exprCache[raw];
      if (f !== undefined) return f;
      var s = String(raw).trim();
      if (s === '') f = ZERO;
      else if (/^[-+]?(\d+\.?\d*|\.\d+)(e[-+]?\d+)?$/i.test(s)) { var cv = parseFloat(s); f = function () { return cv; }; }
      else {
        try {
          hoistCtx = [];
          var body;
          try { body = translateExpr(s); } finally { var hnames = hoistCtx; hoistCtx = null; }
          var decl = hnames.map(function (nm, hi) { return 'o' + hi + ' = OI(' + JSON.stringify(nm) + ')'; }).join(', ');
          f = new Function('V', 'E', 'F', 'O', 'C', 'OI', 'PR', 'PV', FN_NAMES.join(','),
                           '"use strict"; return function () { ' + (decl ? 'var ' + decl + '; ' : '') + 'return (' + body + '); };')
            .apply(null, [exprVar, exprElem, callFn, exprObj, cursorValue, exprInst, exprPR, exprPV].concat(FN_VALS));
        } catch (e) {
          f = null;
          console.warn('Source 2D: expresion no valida "' + s + '": ' + e.message);
        }
      }
      exprCache[raw] = f;
      return f;
    }
    /* valor de una expresion tal cual (numero o texto) */
    function evalExpr(raw) {
      if (typeof raw === 'number') return raw;
      var f = compileExpr(raw == null ? '' : raw);
      if (!f) return 0;
      try { return f(); } catch (e) { return 0; }
    }
    function resolveVal(raw) {
      if (typeof raw === 'number') return raw;
      var v = evalExpr(raw);
      if (typeof v === 'number') return v - v === 0 ? v : 0;   /* descarta NaN e infinitos */
      if (typeof v === 'boolean') return v ? 1 : 0;
      return n(v);
    }
    function evalNum(raw) { return resolveVal(raw); }
    /* valor de una expresion que no cambia de una instancia a otra: se calcula una vez por accion en vez de una vez por
       instancia (con miles de objetos se nota). Con azar o funciones de eventos se sigue calculando en cada una. */
    function once(raw) {
      if (typeof raw === 'number') return raw;
      return /rand|Fn\./.test(String(raw == null ? '' : raw)) ? null : resolveVal(raw);
    }
    /* lista de argumentos "a, b, c" de una llamada */
    function evalArgs(raw) {
      if (raw == null || String(raw).trim() === '') return [];
      var v = evalExpr('[' + raw + ']');
      return Array.isArray(v) ? v : [v];
    }
    /* Objeto.Prop en una expresion: la primera instancia elegida por las condiciones del evento (o por
       "Para cada objeto"); si no hay ninguna elegida, la primera de la escena. */
    var exprPicks = null, objIdByName = null;
    /* nombre de objeto -> id: primero los de la escena actual, luego los globales y por ultimo los de otras escenas */
    function objIdNamed(name) {
      if (!objIdByName) {
        objIdByName = {};
        var rank = {};
        project.objects.forEach(function (o) {
          if (!o.name) return;
          var r = o.scene === state.sceneId ? 2 : !o.scene ? 1 : 0;
          if (!(o.name in rank) || r > rank[o.name]) { rank[o.name] = r; objIdByName[o.name] = o.id; }
        });
      }
      return objIdByName[name] || null;
    }
    /* indice de instancias (por id y por objeto); se rehace cuando cambia la lista de instancias */
    var ixArr = null, ixLen = -1, ixById = null, ixByObj = null;
    /* grupo: las instancias de todos sus objetos (se guarda en el indice hasta que cambien las instancias) */
    function objMatches(it, id) {
      if (it.objId === id) return true;
      var g = objects[id];
      return !!g && g.type === 'group' && (g.members || []).indexOf(it.objId) >= 0;
    }
    function groupInstances(gid) {
      var g = objects[gid];
      if (!g || g.type !== 'group') return [];
      var out = [];
      (g.members || []).forEach(function (m) { var l = ixByObj[m]; if (l) out = out.concat(l); });
      return (ixByObj[gid] = out);
    }
    function instIndex() {
      if (ixArr === state.instances && ixLen === state.instances.length) return;
      ixById = Object.create(null); ixByObj = Object.create(null);
      for (var i = 0; i < state.instances.length; i++) {
        var it = state.instances[i];
        ixById[it.id] = it;
        (ixByObj[it.objId] || (ixByObj[it.objId] = [])).push(it);
      }
      ixArr = state.instances; ixLen = state.instances.length;
    }
    function onlyKey(set) { var k1 = null; for (var k in set) { if (k1 !== null) return null; k1 = k; } return k1; }
    /* la instancia unica de una seleccion, memorizada por conjunto (pick() la olvida al cambiarlo) */
    var oneMemo = typeof WeakMap !== 'undefined' ? new WeakMap() : null;
    function onlyKeyMemo(set) {
      if (!oneMemo) return onlyKey(set);
      var k = oneMemo.get(set);
      if (k === undefined) { k = onlyKey(set); oneMemo.set(set, k); }
      return k;
    }
    function exprInst(name) {
      var id = objIdNamed(name), set = exprPicks && id ? exprPicks[id] : null, j;
      if (!id) return null;
      if (set) {                                     /* lo habitual: una sola instancia elegida (Para cada objeto) */
        var k1 = onlyKeyMemo(set);
        if (k1 !== null) { instIndex(); var one = ixById[k1]; if (one && objMatches(one, id)) return one; }
      }
      for (j = 0; j < state.instances.length; j++) {
        var it = state.instances[j];
        if (objMatches(it, id) && (!set || set[it.id])) return it;
      }
      return null;
    }
    function exprProp(inst, prop) {
      var sz;
      switch (prop) {
        case 'X': case 'x': return inst.x;
        case 'Y': case 'y': return inst.y;
        case 'Angulo': case 'angle': return n(inst.angle, 0);
        case 'Ancho': case 'width': sz = instSize(inst); return sz.w;
        case 'Alto': case 'height': sz = instSize(inst); return sz.h;
        case 'Opacidad': case 'opacity': return n(inst.opacity, 100);
        case 'VelX': case 'vx': return inst.vx;
        case 'VelY': case 'vy': return inst.vy;
        case 'Salud': case 'health': return n(inst.hp, 0);
      }
      if (!inst.vars || !Object.prototype.hasOwnProperty.call(inst.vars, prop)) return 0;
      var vv = inst.vars[prop];
      return typeof vv === 'number' ? vv : autoNum(String(vv));
    }
    function cursorValue(k) {
      var sc = scenes[state.sceneId], gw = Math.max(1, n(sc && sc.gridW, 16)), gh = Math.max(1, n(sc && sc.gridH, 16));
      var wx = state.mouse.wx, wy = state.mouse.wy;
      if (k === 'X') return wx;
      if (k === 'Y') return wy;
      if (k === 'SX') return state.mouse.x;      /* en la pantalla (sin camara) */
      if (k === 'SY') return state.mouse.y;
      if (k === 'CX') return Math.floor(wx / gw);
      return Math.floor(wy / gh);
    }

    /* ---------------- extensiones (JavaScript propio) ---------------- */
    var extCache = null;
    function allExtRt() {
      return (project.extensions || []).concat((project.plugins || []).filter(function (p) { return p.enabled !== false; }));
    }
    function extDefs() {
      if (extCache) return extCache;
      extCache = { act: {}, cond: {} };
      allExtRt().forEach(function (x) {
        var out = null;
        try { out = new Function('api', x.code || '')({}); }
        catch (e) { console.warn('Source 2D [extension ' + (x.name || '') + ']: ' + e.message); return; }
        if (!out) return;
        Object.keys(out.actions || {}).forEach(function (id) { extCache.act['ext:' + x.id + ':' + id] = out.actions[id]; });
        Object.keys(out.conditions || {}).forEach(function (id) { extCache.cond['ext:' + x.id + ':' + id] = out.conditions[id]; });
      });
      return extCache;
    }
    function extApi(inst, p) {
      var a = apiFor(inst);
      a.params = p || {};
      a.text = function (k) { return resolveStr((p || {})[k]); };
      a.arg = function (k) { return evalNum(resolveStr((p || {})[k])); };
      a.snapshot = snapshotGame;                             /* la partida entera como texto (extension Steam: nube) */
      a.restore = restoreGame;
      return a;
    }
    /* acciones y condiciones que este motor no conoce (de un editor mas nuevo, o una extension que ya no esta): se avisa
       una vez por tipo en la consola, en vez de ignorarlas en silencio */
    var warnedType = {};
    function unknownType(kind, t) {
      if (!t || warnedType[kind + t]) return;
      warnedType[kind + t] = true;
      console.warn('Source 2D: ' + kind + ' desconocida "' + t + '" (se ignora; ¿el juego es de una versión más nueva del editor o falta una extensión?)');
    }
    function extCond(t, p) {
      var d = extDefs().cond[t];
      if (!d || typeof d.test !== 'function') { unknownType('condición', t); return t.indexOf('ext:') === 0 ? false : true; }
      try { return !!d.test(extApi(null, p)); }
      catch (e) { console.warn('Source 2D [extension]: ' + e.message); return false; }
    }
    function extAct(t, p, list) {
      if (t.indexOf('ext:') !== 0) { unknownType('acción', t); return; }
      var d = extDefs().act[t];
      if (!d || typeof d.run !== 'function') { unknownType('acción', t); return; }
      try { d.run(extApi(list && list.length ? list[0] : null, p), list || []); }
      catch (e) { console.warn('Source 2D [extension]: ' + e.message); }
    }

    /* ---------------- tweens ---------------- */
    function startTween(inst, prop, to, dur, easing) {
      if (!inst) return;
      to = n(to);
      dur = Math.max(0.01, n(dur, 1));
      var from;
      if (prop === 'x' || prop === 'y' || prop === 'angle' || prop === 'scaleX' || prop === 'scaleY') from = n(inst[prop]);
      else if (prop === 'opacity') from = n(inst.opacity);
      else return;
      inst.tweens = inst.tweens.filter(function (t) { return t.prop !== prop; });
      inst.tweens.push({ prop: prop, from: from, to: to, t: 0, dur: dur, ease: easing || 'easeOut' });
    }
    function stopTweens(inst, prop) {
      if (!inst) return;
      inst.tweens = prop ? inst.tweens.filter(function (t) { return t.prop !== prop; }) : [];
    }
    function updateTweens(dt, onlyUnpausable) {
      for (var i = 0; i < state.instances.length; i++) {
        var inst = state.instances[i];
        if (!inst.tweens.length) continue;
        if (onlyUnpausable && !objDef(inst).ignorePause) continue;
        var keep = [];
        for (var j = 0; j < inst.tweens.length; j++) {
          var tw = inst.tweens[j];
          tw.t += dt;
          var k = clamp(tw.t / tw.dur, 0, 1);
          var v = tw.from + (tw.to - tw.from) * ease(tw.ease, k);
          if (tw.prop === 'opacity') inst.opacity = clamp(v, 0, 100);
          else inst[tw.prop] = v;
          if (k < 1) keep.push(tw); else inst.tweenEnd = state.frame;
        }
        inst.tweens = keep;
      }
    }

    /* ---------------- colision con tiles ---------------- */
    /* Solidos. Los de los mapas de tiles se calculan una vez por escena (filas de tiles juntas en una caja) y se
       guardan por zonas de 128 px: cada objeto solo mira los de su zona. Las acciones de tiles invalidan la cache.
       Solo chocan los mapas de capas solidas (layer.solid !== false). */
    var TILE_CHUNK = 128, tileSolids = null, solidStamp = 0;
    /* clave numerica de una celda de la rejilla (mas rapida que un texto "fila,columna") */
    function GK(r, c) { return (r + 4096) * 8192 + c + 4096; }
    function invalidateTiles() { tileSolids = null; tileRev++; }
    function tilemapSolid(tm) { var L = layers[tm.layer || 'base']; return !L || L.solid !== false; }
    function buildTileCache() {
      var boxes = [], ladders = [], danger = [], sc = scenes[state.sceneId], tms = (sc && sc.tilemaps) || [];
      for (var t = 0; t < tms.length; t++) {
        var tm = tms[t], ts = tilesets[tm.tilesetId];
        if (!ts || !tm.data || !tilemapSolid(tm)) continue;
        var T = ts.tileSize, tiles = ts.tiles || [];
        for (var r = 0; r < tm.rows; r++) {
          var start = -1;
          for (var c = 0; c <= tm.cols; c++) {
            var idx = c < tm.cols ? tm.data[r * tm.cols + c] : -1, tl = idx >= 0 ? tiles[idx] : null;
            var shaped = !!(tl && tl.collision && tl.collision.shape && tl.collision.shape !== 'none');
            var solid = !!(tl && tl.solid && !tl.oneway && !shaped);
            if (solid && start < 0) start = c;
            else if (!solid && start >= 0) {
              var count = c - start;
              boxes.push({ x: tm.x + (start + count / 2) * T, y: tm.y + (r + 0.5) * T, w: count * T, h: T, oneway: false });
              start = -1;
            }
            if (!tl || c >= tm.cols) continue;
            var cx = tm.x + (c + 0.5) * T, cy = tm.y + (r + 0.5) * T;
            if (shaped) {   /* forma propia: escalones y rampas (tiras horizontales); escalera y peligro, su caja */
              var tx0 = tm.x + c * T, ty0 = tm.y + r * T, tsz = T, k = 1;
              if (tl.solid || tl.oneway) tileSlices(tl.collision, tsz).forEach(function (sl) {
                if (!tl.oneway) anySteps = true;
                boxes.push({ x: tx0 + sl[0] * k, y: ty0 + sl[1] * k, w: sl[2] * k, h: sl[3] * k, oneway: !!tl.oneway, step: tl.oneway ? 0 : Math.max(4, T / 2 + 0.5) });
              });
              var mb = maskBounds(tl.collision);
              var mbox = { x: tx0 + (mb[0] + mb[2]) / 2 * k, y: ty0 + (mb[1] + mb[3]) / 2 * k, w: (mb[2] - mb[0]) * k, h: (mb[3] - mb[1]) * k, src: null };
              if (tl.ladder) ladders.push(mbox);
              if (tl.danger) danger.push(mbox);
              continue;
            }
            if (tl.oneway) boxes.push({ x: cx, y: cy, w: T, h: T, oneway: true });
            if (tl.ladder) ladders.push({ x: cx, y: cy, w: T, h: T, src: null });
            /* los peligros (pinchos) perdonan un poco: su caja es mas pequena que el tile */
            if (tl.danger) danger.push({ x: cx, y: cy, w: T * 0.7, h: T * 0.7 });
          }
        }
      }
      var grid = {};
      for (var i = 0; i < boxes.length; i++) {
        var bx = boxes[i];
        var c0 = Math.floor((bx.x - bx.w / 2) / TILE_CHUNK), c1 = Math.floor((bx.x + bx.w / 2) / TILE_CHUNK);
        var r0 = Math.floor((bx.y - bx.h / 2) / TILE_CHUNK), r1 = Math.floor((bx.y + bx.h / 2) / TILE_CHUNK);
        for (var gr = r0; gr <= r1; gr++) for (var gc = c0; gc <= c1; gc++) (grid[GK(gr, gc)] || (grid[GK(gr, gc)] = [])).push(bx);
      }
      tileSolids = { boxes: boxes, grid: grid, ladders: ladders, danger: danger, sceneId: state.sceneId };
      return tileSolids;
    }
    /* forma de un tile en tiras horizontales [centro x, centro y, ancho, alto] (en pixeles del tile): asi una rampa o
       unos escalones son cajas, y el personaje sube por ellos (ver "step" en la fisica) */
    var STEP_MAX = 40, anySteps = false;   /* margen de busqueda para los escalones (el limite de cada caja va en box.step) */
    function tileSlices(m, T) {
      var N = Math.max(2, Math.min(16, Math.round(T / 2))), hS = T / N, out = [], pts = maskOutline(m), b = maskBounds(m);
      var ell = m.shape === 'circle' || m.shape === 'ellipse', rect = m.shape === 'rect' || m.shape === 'square';
      if (rect) return [[(b[0] + b[2]) / 2, (b[1] + b[3]) / 2, b[2] - b[0], b[3] - b[1]]];
      for (var k = 0; k < N; k++) {
        var y = (k + 0.5) * hS, x0 = 1e9, x1 = -1e9;
        if (ell) {
          var cy = (b[1] + b[3]) / 2, ry = (b[3] - b[1]) / 2, rx = (b[2] - b[0]) / 2, t = ry > 0 ? (y - cy) / ry : 2;
          if (Math.abs(t) < 1) { var hx = rx * Math.sqrt(1 - t * t); x0 = (b[0] + b[2]) / 2 - hx; x1 = (b[0] + b[2]) / 2 + hx; }
        } else for (var i = 0; i < pts.length; i++) {
          var p0 = pts[i], p1 = pts[(i + 1) % pts.length];
          if ((p0[1] <= y) === (p1[1] <= y)) continue;
          var x = p0[0] + (y - p0[1]) * (p1[0] - p0[0]) / (p1[1] - p0[1]);
          x0 = Math.min(x0, x); x1 = Math.max(x1, x);
        }
        if (x1 - x0 < 0.5) continue;
        var last = out[out.length - 1];
        if (last && Math.abs(last[0] - (x0 + x1) / 2) < 0.01 && Math.abs(last[2] - (x1 - x0)) < 0.01) { last[1] += hS / 2; last[3] += hS; }   /* misma tira: se juntan */
        else out.push([(x0 + x1) / 2, y, x1 - x0, hS]);
      }
      return out;
    }
    function tilesNow() { return tileSolids && tileSolids.sceneId === state.sceneId ? tileSolids : buildTileCache(); }
    /* objetos solidos de este fotograma, tambien en una rejilla de celdas de TILE_CHUNK px (como los tiles): asi cada
       objeto que se mueve solo mira los solidos de su zona, no todos (con cientos de cajas eran n x n comparaciones) */
    function buildInstanceSolids() {
      var out = [], grid = {};
      for (var i = 0; i < state.instances.length; i++) {
        var inst = state.instances[i], b = behav(inst);
        if (!b.solid || layerOf(inst).solid === false) continue;
        var s = instSize(inst), box = { x: cX(inst, s), y: cY(inst, s), w: s.w, h: s.h, oneway: !!b.oneway, src: inst, stamp: 0 };
        out.push(box);
        var c0 = Math.floor((box.x - s.w / 2) / TILE_CHUNK), c1 = Math.floor((box.x + s.w / 2) / TILE_CHUNK);
        var r0 = Math.floor((box.y - s.h / 2) / TILE_CHUNK), r1 = Math.floor((box.y + s.h / 2) / TILE_CHUNK);
        for (var r = r0; r <= r1; r++) for (var c = c0; c <= c1; c++) { var k = GK(r, c); (grid[k] || (grid[k] = [])).push(box); }
      }
      out.grid = grid;
      return out;
    }
    function solidsNear(instSolids, x0, y0, x1, y1) {
      var tc = tilesNow(), stamp = ++solidStamp;
      var c0 = Math.floor(x0 / TILE_CHUNK), c1 = Math.floor(x1 / TILE_CHUNK), r0 = Math.floor(y0 / TILE_CHUNK), r1 = Math.floor(y1 / TILE_CHUNK);
      var out = instSolids.grid ? [] : instSolids.slice();
      if (instSolids.grid)
        for (var ir = r0; ir <= r1; ir++) for (var ic = c0; ic <= c1; ic++) {
          var il = instSolids.grid[GK(ir, ic)];
          if (il) for (var ik = 0; ik < il.length; ik++) if (il[ik].stamp !== stamp) { il[ik].stamp = stamp; out.push(il[ik]); }
        }
      for (var r = r0; r <= r1; r++) for (var c = c0; c <= c1; c++) {
        var list = tc.grid[GK(r, c)];
        if (!list) continue;
        for (var k = 0; k < list.length; k++) if (list[k].stamp !== stamp) { list[k].stamp = stamp; out.push(list[k]); }
      }
      return out;
    }
    /* todos los solidos (para quien los necesite enteros) */
    function buildSolidBoxes() { return buildInstanceSolids().concat(tilesNow().boxes); }
    /* toca un tile con la marca indicada (danger, solid, ladder, oneway) */
    function touchesTileFlag(inst, flag) {
      var s = instSize(inst), tc = tilesNow(), i, ix = cX(inst, s), iy = cY(inst, s);
      if (flag === 'danger') {
        for (i = 0; i < tc.danger.length; i++) { var d = tc.danger[i]; if (boxesOverlap(ix, iy, s.w, s.h, d.x, d.y, d.w, d.h)) return true; }
        return false;
      }
      if (flag === 'ladder') {
        for (i = 0; i < tc.ladders.length; i++) { var l = tc.ladders[i]; if (boxesOverlap(ix, iy, s.w, s.h, l.x, l.y, l.w, l.h)) return true; }
        return false;
      }
      var near = solidsNear([], ix - s.w / 2 - 1, iy - s.h / 2 - 1, ix + s.w / 2 + 1, iy + s.h / 2 + 1);
      for (i = 0; i < near.length; i++) {
        var b = near[i];
        if ((flag === 'oneway') !== !!b.oneway) continue;
        if (boxesOverlap(ix, iy, s.w + 2, s.h + 2, b.x, b.y, b.w, b.h)) return true;
      }
      return false;
    }

    /* ---------------- fisica realista ---------------- */
    /* cuerpo libre (cae o choca y no lo mueve el teclado): entre dos cuerpos libres el choque reparte la velocidad segun
       la masa (una piedra empuja a una pelota; una pelota apenas mueve a una piedra) */
    function freeBody(o) { var b = behav(o); return (b.gravity || b.collide) && (!b.movement || b.movement === 'none'); }
    /* choque en un eje entre dos cuerpos libres: conserva el momento, rebote = el mayor de los dos */
    function impulse(A, B, axis, bounceA) {
      var mA = Math.max(0.01, n(behav(A).mass, 1)), mB = Math.max(0.01, n(behav(B).mass, 1));
      var e = Math.max(bounceA, clamp(n(behav(B).bounce, 0), 0, 1));
      var vA = A[axis], vB = B[axis], P = mA * vA + mB * vB;
      if (Math.abs(vA - vB) < 60) e = 0;                           /* choque suave: quedan juntos (apilar sin temblar) */
      A[axis] = (P + mB * e * (vB - vA)) / (mA + mB);
      B[axis] = (P + mA * e * (vA - vB)) / (mA + mB);
    }
    /* rayo desde el centro de un objeto: avanza a pasos de 4 px y para en el primer solido (tiles y objetos solidos) o
       en la primera instancia del objeto pedido. Devuelve { x, y, dist } del choque o null. */
    function castRay(from, dx, dy, dist, target) {
      var solids = target ? null : buildInstanceSolids(), others = target ? objInstances(target) : null;
      for (var d = 4; d <= dist; d += 4) {
        var x = from.x + dx * d, y = from.y + dy * d, k;
        if (others) {
          for (k = 0; k < others.length; k++) if (others[k] !== from && instContains(others[k], x, y)) return { x: x, y: y, dist: d };
        } else {
          var near = solidsNear(solids, x - 1, y - 1, x + 1, y + 1);
          for (k = 0; k < near.length; k++) {
            var s = near[k];
            if (s.src === from || s.oneway) continue;
            if (Math.abs(x - s.x) * 2 <= s.w && Math.abs(y - s.y) * 2 <= s.h) return { x: x, y: y, dist: d };
          }
        }
      }
      return null;
    }
    /* ---------------- idiomas ----------------
       project.i18n = { langs: ['es', 'en'], strings: { clave: { es: '...', en: '...' } } }. En textos: {t:clave};
       en expresiones: T("clave"). Si falta la traduccion se usa la del primer idioma y si no, la clave. */
    function curLang() {
      var i18 = project.i18n || {}, langs = i18.langs || [];
      return state.lang || (project.settings && project.settings.lang) || langs[0] || '';
    }
    function translate(k) {
      var i18 = project.i18n || {}, row = (i18.strings || {})[k];
      if (!row) return String(k);
      var v = row[curLang()];
      return v != null && v !== '' ? v : (row[(i18.langs || [])[0]] != null ? row[(i18.langs || [])[0]] : String(k));
    }

    /* ---------------- buscar caminos (A*) ----------------
       Rejilla del tamano de los tiles de la escena (o 32 px); una celda esta bloqueada si toca un tile solido o un
       objeto solido que no se mueve. Diagonales sin cortar esquinas. Devuelve la lista de centros de celda o null. */
    var anyPath = false;
    function findPath(inst, tx, ty) {
      var sc = scenes[state.sceneId], tms = (sc && sc.tilemaps) || [], cs = 32;
      for (var q = 0; q < tms.length; q++) { var tsd = tilesets[tms[q].tilesetId]; if (tsd && tsd.tileSize) { cs = tsd.tileSize; break; } }
      var W = Math.max(1, Math.ceil(n(sc && sc.width, VW) / cs)), H = Math.max(1, Math.ceil(n(sc && sc.height, VH) / cs));
      if (W * H > 250000) return null;
      var solids = buildInstanceSolids(), blocked = new Uint8Array(W * H), me = instSize(inst), pad = Math.min(me.w, me.h) / 2 - 1;
      for (var y = 0; y < H; y++) for (var x = 0; x < W; x++) {
        var x0 = x * cs, y0 = y * cs, near = solidsNear(solids, x0 - pad, y0 - pad, x0 + cs + pad, y0 + cs + pad);
        for (var k = 0; k < near.length; k++) {
          var s = near[k];
          if (s.src === inst || s.oneway || (s.src && freeBody(s.src))) continue;
          if (s.x + s.w / 2 > x0 - pad && s.x - s.w / 2 < x0 + cs + pad && s.y + s.h / 2 > y0 - pad && s.y - s.h / 2 < y0 + cs + pad) { blocked[y * W + x] = 1; break; }
        }
      }
      var sx = clamp(Math.floor(inst.x / cs), 0, W - 1), sy = clamp(Math.floor(inst.y / cs), 0, H - 1);
      var gx = clamp(Math.floor(tx / cs), 0, W - 1), gy = clamp(Math.floor(ty / cs), 0, H - 1), goal = gy * W + gx, start = sy * W + sx;
      if (blocked[goal]) return null;
      blocked[start] = 0;
      var g = new Float64Array(W * H).fill(Infinity), from = new Int32Array(W * H).fill(-1), closed = new Uint8Array(W * H);
      var open = [start], f = new Float64Array(W * H);
      g[start] = 0; f[start] = Math.hypot(gx - sx, gy - sy);
      var DIRS = [[1, 0, 1], [-1, 0, 1], [0, 1, 1], [0, -1, 1], [1, 1, 1.414], [1, -1, 1.414], [-1, 1, 1.414], [-1, -1, 1.414]], steps = 0;
      while (open.length && steps++ < 60000) {
        var bi = 0;
        for (var oi = 1; oi < open.length; oi++) if (f[open[oi]] < f[open[bi]]) bi = oi;
        var cur = open[bi]; open[bi] = open[open.length - 1]; open.pop();
        if (cur === goal) {
          var path = [];
          for (var c = goal; c !== start && c >= 0; c = from[c]) path.push({ x: (c % W) * cs + cs / 2, y: Math.floor(c / W) * cs + cs / 2 });
          path.reverse();
          if (path.length) path[path.length - 1] = { x: tx, y: ty };
          return path;
        }
        if (closed[cur]) continue;
        closed[cur] = 1;
        var cx = cur % W, cy = (cur / W) | 0;
        for (var di = 0; di < 8; di++) {
          var nx = cx + DIRS[di][0], ny = cy + DIRS[di][1];
          if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
          var ni = ny * W + nx;
          if (blocked[ni] || closed[ni]) continue;
          if (DIRS[di][2] > 1 && (blocked[cy * W + nx] || blocked[ny * W + cx])) continue;   /* sin cortar esquinas */
          var ng = g[cur] + DIRS[di][2];
          if (ng < g[ni]) { g[ni] = ng; from[ni] = cur; f[ni] = ng + Math.hypot(gx - nx, gy - ny); open.push(ni); }
        }
      }
      return null;
    }
    /* sigue su camino: va al siguiente punto a su velocidad */
    function updatePaths(dt) {
      if (!anyPath) return;
      anyPath = false;
      for (var i = 0; i < state.instances.length; i++) {
        var o = state.instances[i];
        if (!o.path) continue;
        anyPath = true;
        var wp = o.path[0];
        if (!wp) { o.path = null; o.pathEnd = state.frame; o.vx = 0; if (!behav(o).gravity) o.vy = 0; continue; }
        var dx = wp.x - o.x, dy = wp.y - o.y, d = Math.sqrt(dx * dx + dy * dy), sp = o.pathSpeed || 120;
        if (d <= sp * dt + 0.5) { o.x = wp.x; o.y = wp.y; o.path.shift(); o.vx = 0; o.vy = 0; continue; }
        o.x += dx / d * sp * dt; o.y += dy / d * sp * dt; o.faceX = dx < 0 ? -1 : 1;
      }
    }
    /* la partida como texto: variables y escena; con all, tambien los objetos, la camara y los temporizadores */
    function snapshotGame(all) {
      var sv = { globals: state.globals, scene: state.sceneId };
      if (all) { sv.instances = state.instances; sv.camera = state.camera; sv.timers = state.timers; sv.nextId = state.nextId; sv.lang = state.lang; }
      return JSON.stringify(sv, function (k, v) { return k.charAt(0) === '_' || k === 'groundRef' || k === 'img' ? undefined : v; });
    }
    function restoreGame(ld) {
      if (typeof ld === 'string') { try { ld = JSON.parse(ld); } catch (e) { return false; } }
      if (!ld || typeof ld !== 'object') return false;
      Object.keys(ld.globals || {}).forEach(function (k) { state.globals[k] = ld.globals[k]; });
      if (ld.instances && scenes[ld.scene]) {
        var lfr = state.frame;
        loadScene(ld.scene, true);
        state.instances = ld.instances.filter(function (o) { return objects[o.objId]; });
        state.instances.forEach(function (o) { o._od = objects[o.objId]; o.groundRef = null; });
        if (ld.camera) Object.keys(ld.camera).forEach(function (k) { state.camera[k] = ld.camera[k]; });
        state.timers = ld.timers || {}; state.nextId = Math.max(state.nextId, n(ld.nextId, 1)); if (ld.lang) state.lang = ld.lang;
        Object.keys(ld.globals || {}).forEach(function (k) { state.globals[k] = ld.globals[k]; });
        state.frame = Math.max(2, lfr);                         /* el reloj sigue; no repite "al inicio de la escena" */
      } else if (ld.scene && ld.scene !== state.sceneId) changeScene(ld.scene);
      return true;
    }
    function physics(dt) {
      var instSolids = buildInstanceSolids();
      for (var q = 0; q < state.instances.length; q++) {
        var inst = state.instances[q], b = behav(inst);
        if (inst.spin) inst.angle += inst.spin * dt;
        /* cuerpo dormido: libre, en reposo en el suelo desde hace un rato y sin que nada lo toque. No se calcula (una pila
           de cajas quieta no cuesta nada). Despierta si algo le da velocidad, si se mueve su suelo, si cambian los tiles,
           o cada 20 fotogramas para comprobar que su suelo sigue ahi. */
        if (inst.sleepT > 0.5 && inst.vx === 0 && inst.vy === 0 && inst.sleepRev === tileRev && (state.frame + inst.id) % 20) {
          var sg = inst.groundRef;
          if (!sg || (sg.x === sg.prevX && sg.y === sg.prevY)) { inst.grounded = true; continue; }
        }
        var x0 = inst.x, y0 = inst.y;
        /* peso: multiplica la gravedad (0.2 = cae como una hoja, negativo = sube como un globo); resistencia del aire
           (drag): frena la caida, el papel o una pluma llegan pronto a su velocidad maxima */
        if (b.gravity && !inst.climbing && !(inst.dashT > 0)) {
          var wgt = b.weight != null ? n(b.weight, 1) : 1;
          inst.vy = clamp(inst.vy + GRAVITY * wgt * dt, -MAX_FALL, b.maxFall > 0 ? b.maxFall : MAX_FALL);
        }
        if (b.drag > 0) { var dr = Math.pow(1 - clamp(n(b.drag, 0), 0, 0.99), dt * 60); inst.vy *= dr; if (!inst.grounded) inst.vx *= dr; }
        if (!b.gravity && !b.collide) {
          inst.x += inst.vx * dt; inst.y += inst.vy * dt;
          continue;
        }
        var friction = b.friction != null ? n(b.friction, 0.85) : 0.85;
        var air = b.airFriction != null ? n(b.airFriction, 0.99) : 0.99;
        if (b.movement !== 'platform') {
          inst.vx *= Math.pow(inst.grounded ? friction : air, dt * 60);
          if (!b.gravity) inst.vy *= Math.pow(friction, dt * 60);
        }
        var bounce = clamp(n(b.bounce, 0), 0, 1), free = freeBody(inst);
        var wasGrounded = inst.grounded;
        inst.grounded = false; inst.groundRef = null; inst.wallL = false; inst.wallR = false;
        var sz = instSize(inst), hw = sz.w / 2, hh = sz.h / 2, ox = sz.ox || 0, oy = sz.oy || 0;
        var prevBottom = inst.y + oy + hh;
        /* solo los solidos cerca del recorrido de este fotograma (los mapas de tiles van por zonas) */
        var mx = Math.abs(inst.vx * dt) + 4, my = Math.abs(inst.vy * dt) + 4 + (wasGrounded && anySteps ? STEP_MAX : 0);
        var solids = solidsNear(instSolids, inst.x + ox - hw - mx, inst.y + oy - hh - my, inst.x + ox + hw + mx, inst.y + oy + hh + my);

        inst.x += inst.vx * dt;
        for (var i = 0; i < solids.length; i++) {
          var s = solids[i];
          if (s.oneway || s.src === inst) continue;
          if (boxesOverlap(inst.x + ox, inst.y + oy, sz.w, sz.h, s.x, s.y, s.w, s.h)) {
            /* escalones y rampas de tiles con forma: si el borde de arriba esta a poca altura de los pies, se sube */
            var rise = inst.y + oy + hh - (s.y - s.h / 2);
            if (s.step && wasGrounded && inst.vy >= 0 && rise > 0 && rise <= s.step) { inst.y -= rise + 0.01; continue; }
            if (inst.vx > 0) inst.wallR = true; else if (inst.vx < 0) inst.wallL = true;
            if (inst.vx > 0) inst.x = s.x - s.w / 2 - hw - ox;
            else if (inst.vx < 0) inst.x = s.x + s.w / 2 + hw - ox;
            if (free && s.src && freeBody(s.src)) impulse(inst, s.src, 'vx', bounce);
            else inst.vx = bounce > 0 && Math.abs(inst.vx * bounce) > 20 ? -inst.vx * bounce : 0;
          }
        }
        inst.y += inst.vy * dt;
        for (var j = 0; j < solids.length; j++) {
          var u = solids[j];
          if (u.src === inst) continue;
          if (boxesOverlap(inst.x + ox, inst.y + oy, sz.w, sz.h, u.x, u.y, u.w, u.h)) {
            if (u.oneway && !(inst.dropT > 0)) {
              if (!(inst.vy > 0 && prevBottom <= u.y - u.h / 2 + 6)) continue;
            }
            if (inst.vy > 0) {
              inst.y = u.y - u.h / 2 - hh - oy;
              inst.groundRef = u.src || null;
              /* contra otro cuerpo libre en el aire: se reparten la velocidad segun la masa. Si el de abajo esta apoyado, el
                 suelo se lleva el golpe y este cae encima como sobre algo fijo (asi las pilas se quedan quietas) */
              if (free && u.src && freeBody(u.src) && !u.src.grounded) {
                impulse(inst, u.src, 'vy', bounce);
                if (inst.vy >= u.src.vy - 1) { inst.vy = u.src.vy; inst.grounded = true; if (!wasGrounded) inst.evLandPending = true; }
              }
              /* rebota mientras el rebote se note; los saltitos muy pequenos se quedan quietos (no tiembla en el suelo) */
              else if (bounce > 0.08 && inst.vy * bounce > 40) { inst.vy = -inst.vy * bounce; }
              else { inst.vy = 0; inst.grounded = true; if (!wasGrounded) inst.evLandPending = true; }
            } else if (inst.vy < 0) {
              inst.y = u.y + u.h / 2 + hh - oy;
              if (free && u.src && freeBody(u.src)) impulse(inst, u.src, 'vy', bounce);
              else inst.vy = bounce > 0.08 ? -inst.vy * bounce : 0;
            }
          }
        }
        /* bajando una rampa o unos escalones: se queda pegado al suelo en vez de ir dando saltitos */
        if (wasGrounded && !inst.grounded && inst.vy >= 0 && !(inst.dropT > 0)) {
          var feet = inst.y + oy + hh, best = null;
          for (var q2 = 0; q2 < solids.length; q2++) {
            var g = solids[q2], top = g.y - g.h / 2;
            if (!g.step || g.src === inst || top < feet - 0.5 || top > feet + g.step) continue;
            if (Math.abs(inst.x + ox - g.x) * 2 >= sz.w + g.w) continue;
            if (!best || top < best.y - best.h / 2) best = g;
          }
          if (best) { inst.y = best.y - best.h / 2 - hh - oy; inst.vy = 0; inst.grounded = true; }
        }
        if (free) {
          if (inst.grounded && Math.abs(inst.vx) < 2) inst.vx = 0;
          if (inst.grounded && inst.vx === 0 && inst.vy === 0 && Math.abs(inst.x - x0) + Math.abs(inst.y - y0) < 0.05) {
            inst.sleepT = (inst.sleepT || 0) + dt; inst.sleepRev = tileRev;
          } else inst.sleepT = 0;
        }
      }
    }

    function pxOf(v, inst) { return behav(inst).pixel ? Math.round(v) : v; }
    var anyLadderObj = (project.objects || []).some(function (o) { return o.behaviors && o.behaviors.ladder; });
    function buildLadderBoxes() {
      var out = [], i;
      if (anyLadderObj) for (i = 0; i < state.instances.length; i++) {
        var it = state.instances[i];
        if (!behav(it).ladder) continue;
        var s = instSize(it);
        out.push({ x: cX(it, s), y: cY(it, s), w: s.w, h: s.h, src: it });
      }
      return out.concat(tilesNow().ladders);
    }
    function onLadder(inst, sz, ladders) {
      for (var i = 0; i < ladders.length; i++) {
        var L = ladders[i];
        if (L.src === inst) continue;
        if (boxesOverlap(cX(inst, sz), cY(inst, sz), sz.w, sz.h, L.x, L.y, L.w, L.h)) return true;
      }
      return false;
    }

    /* toca una pared solida (de tiles) por el lado dir (-1 izquierda, 1 derecha) a media altura */
    function nearWall(inst, dir) {
      var s = instSize(inst), x = cX(inst, s) + dir * 3, y = cY(inst, s), h = s.h * 0.6;
      var near = solidsNear([], x - s.w / 2 - 1, y - h / 2, x + s.w / 2 + 1, y + h / 2);
      for (var i = 0; i < near.length; i++) {
        var q = near[i];
        if (!q.oneway && !q.step && boxesOverlap(x, y, s.w, h, q.x, q.y, q.w, q.h)) return true;
      }
      return false;
    }

    /* ---------------- movimiento automatico ---------------- */
    var NOSIM = Object.freeze({});
    function applyMovement() {
      var ladders = null;                      /* solo si alguien se mueve con el teclado */
      for (var i = 0; i < state.instances.length; i++) {
        var inst = state.instances[i], b = behav(inst);
        /* objetos sin movimiento propio (lo mas comun: monedas, bloques, clones): nada que hacer */
        if (!b.movement || b.movement === 'none') {
          if (inst.simNext) inst.simNext = null;
          if (inst.climbing || inst.wallSliding || inst.running || inst.crouching) { inst.climbing = false; inst.wallSliding = false; inst.running = false; inst.crouching = false; }
          continue;
        }
        var speed = inst.speedOverride != null ? inst.speedOverride : n(b.speed, 150), used = false;
        var sim = inst.simNext || NOSIM;
        inst.simNext = null;
        inst.climbing = false; inst.wallSliding = false; inst.running = false; inst.crouching = false;
        if (b.movement === 'platform') {
          var L = key(b.keyLeft || 'arrowleft') || (!b.keyLeft && key('a')) || !!sim.left;
          var R = key(b.keyRight || 'arrowright') || (!b.keyRight && key('d')) || !!sim.right;
          var up = key('arrowup') || key('w');
          var dn = key(b.keyDown || 'arrowdown') || (!b.keyDown && key('s'));
          var jumpPressed = pressedKey(b.keyJump || ' ') || (!b.keyJump && (pressedKey('arrowup') || pressedKey('w'))) || !!sim.jump;
          var jumpHeld = key(b.keyJump || ' ') || (!b.keyJump && (key('arrowup') || key('w'))) || !!sim.jump || !!sim.jumpHeld;
          /* "Detener la plataforma" sobre un plataformero: sin control (dialogos, escenas, muerte); la gravedad sigue */
          if (inst.platOff) { L = R = up = dn = jumpPressed = jumpHeld = false; sim = {}; }
          var pdt = state.dt || 0;
          /* habilidades que los eventos activan o quitan durante el juego (accion "Habilidad del plataformero") */
          var ab = inst.abil || {}, canDash = ab.dash != null ? ab.dash : b.dash, wallOn = ab.wall != null ? ab.wall : b.wall;
          var airN = ab.air === false ? 0 : ab.air === true ? Math.max(1, b.airJumps) : b.airJumps;
          if (inst.grounded) { inst.coyoteT = b.coyote; inst.airJumpsLeft = airN; inst.dashReady = true; }
          else inst.coyoteT = Math.max(0, n(inst.coyoteT) - pdt);
          inst.jumpBufT = jumpPressed ? b.jumpBuffer : Math.max(0, n(inst.jumpBufT) - pdt);
          if (inst.dashCD > 0) inst.dashCD -= pdt;
          /* dash: rapido, recto y sin gravedad; en el aire solo uno hasta volver a tocar el suelo */
          if (canDash && !inst.platOff && !(inst.dashT > 0) && !(inst.dashCD > 0) && inst.dashReady !== false &&
              (pressedKey(b.keyDash || 'shift') || !!sim.dash)) {
            inst.dashT = b.dashTime; inst.dashDir = R && !L ? 1 : L && !R ? -1 : (inst.faceX || 1);
            inst.faceX = inst.dashDir; inst.dashCD = b.dashCooldown + b.dashTime;
            if (!inst.grounded) inst.dashReady = false;
            inst.evDash = state.frame; inst.jumpHeldPhase = false;
          }
          if (inst.dashT > 0) {
            inst.dashT -= pdt;
            inst.vx = inst.dashDir * b.dashSpeed; inst.vy = 0;
            if (inst.dashT <= 0) inst.vx = inst.dashDir * speed;
            inst.moveTarget = null;
            continue;
          }
          if (sim.down) dn = true;
          var sz = instSize(inst);
          var ladder = onLadder(inst, sz, ladders || (ladders = buildLadderBoxes()));
          inst.onLadder = ladder;
          inst.crouching = !!(b.crouch && inst.grounded && dn);
          inst.running = !!(b.run && !inst.crouching && key(b.keyRun || 'shift') && (L || R));
          var sp = inst.running ? (n(b.runSpeed, 0) || speed * 1.8) : speed;
          if (inst.crouching) sp = speed * 0.4;
          var climb = ladder && (up || dn) && !jumpPressed;
          if (climb) {
            var cs = n(b.climbSpeed, 0) || speed * 0.8;
            inst.climbing = true;
            inst.vy = up && !dn ? -cs : (dn && !up ? cs : 0);
            inst.vx = L && !R ? -sp * 0.5 : (R && !L ? sp * 0.5 : 0);
            if (L && !R) inst.faceX = -1; else if (R && !L) inst.faceX = 1;
            used = true;
          } else {
            /* tras un salto de pared, un momento sin control horizontal (el impulso aleja de la pared) */
            if (inst.wallLockT > 0) { inst.wallLockT -= pdt; used = true; }
            else if (L && !R) { inst.vx = -sp; used = true; inst.faceX = -1; }
            else if (R && !L) { inst.vx = sp; used = true; inst.faceX = 1; }
            else {
              inst.vx *= inst.grounded ? 0.72 : 0.94;
              if (Math.abs(inst.vx) < 3) inst.vx = 0;
            }
            var jumpV = inst.jumpOverride != null ? inst.jumpOverride : n(b.jump, 620);
            var onGround = inst.grounded || ladder || inst.coyoteT > 0;
            if (jumpPressed && dn && inst.grounded) {
              inst.dropT = 0.2; inst.grounded = false; inst.vy = 80;
            } else if ((jumpPressed || inst.jumpBufT > 0) && onGround) {
              inst.vy = -jumpV; inst.grounded = false; inst.coyoteT = 0; inst.jumpBufT = 0;
              inst.evJump = state.frame; inst.jumpHeldPhase = true;
            } else if (jumpPressed && wallOn && b.wallJump && (nearWall(inst, -1) || nearWall(inst, 1))) {
              /* salto de pared: arriba y lejos de la pared; recupera los saltos en el aire y el dash */
              var away = nearWall(inst, -1) ? 1 : -1;
              inst.vy = -jumpV * 0.92; inst.vx = away * sp * 1.15; inst.faceX = away;
              inst.wallLockT = 0.16; inst.airJumpsLeft = airN; inst.dashReady = true; inst.jumpBufT = 0;
              inst.evWallJump = state.frame; inst.jumpHeldPhase = true;
            } else if (jumpPressed && inst.airJumpsLeft > 0) {
              inst.vy = -(b.airJump || jumpV); inst.airJumpsLeft--; inst.jumpBufT = 0;
              inst.evAirJump = state.frame; inst.jumpHeldPhase = true;
            }
            /* salto variable: al soltar el salto mientras sube, se corta */
            if (inst.jumpHeldPhase && (inst.vy >= 0 || inst.grounded)) inst.jumpHeldPhase = false;
            if (b.jumpCut && inst.jumpHeldPhase && !jumpHeld) { inst.vy *= 0.45; inst.jumpHeldPhase = false; }
            if (wallOn && !inst.grounded && !ladder && (inst.wallL || inst.wallR) && (L || R) && inst.vy > 0) {
              inst.wallSliding = true; inst.vy = Math.min(inst.vy, 90);
            }
          }
          if (used) inst.moveTarget = null;
        } else if (b.movement === 'topdown') {
          var tdx = (key('arrowright') || key('d') ? 1 : 0) - (key('arrowleft') || key('a') ? 1 : 0);
          var tdy = (key('arrowdown') || key('s') ? 1 : 0) - (key('arrowup') || key('w') ? 1 : 0);
          if (tdx) inst.faceX = tdx;
          if (b.grid) {
            var cell = n(b.cell, 16) || 16;
            if (!inst.moveTarget && (tdx || tdy)) {
              var gx = Math.round(inst.x / cell) * cell, gy = Math.round(inst.y / cell) * cell;
              inst.moveTarget = tdx ? { x: gx + tdx * cell, y: gy } : { x: gx, y: gy + tdy * cell };
              inst.moveSpeed = speed;
            }
          } else if (tdx || tdy) {
            if (tdx && tdy) { tdx *= 0.7071; tdy *= 0.7071; }
            inst.vx = tdx * speed; inst.vy = tdy * speed;
            inst.moveTarget = null; used = true;
          }
        }
        if (!used) {
          if (inst.moveTarget) {
            var spd = inst.moveSpeed || speed;
            var tx = inst.moveTarget.x - inst.x, ty = inst.moveTarget.y - inst.y;
            var d = Math.sqrt(tx * tx + ty * ty);
            if (d < 4) {
              if (!b.gravity) { inst.x = inst.moveTarget.x; inst.y = inst.moveTarget.y; }
              inst.moveTarget = null; inst.vx = 0;
              if (!b.gravity) inst.vy = 0;
            } else {
              inst.vx = tx / d * spd;
              if (!b.gravity) inst.vy = ty / d * spd;
            }
          } else if (b.movement === 'topdown' || b.movement === 'rts') {
            inst.vx = 0;
            if (!b.gravity) inst.vy = 0;
          }
        }
      }
    }

    /* ---------------- seleccion y ordenes (RTS) ---------------- */
    var anyRts = (project.objects || []).some(function (o) { return o.behaviors && o.behaviors.movement === 'rts'; });
    function rtsUnits() {
      if (!anyRts) return [];
      return state.instances.filter(function (i) { return behav(i).movement === 'rts'; });
    }
    function handleRTSInput() {
      if (!rtsUnits().length) return;
      var m = state.mouse, wx = m.wx, wy = m.wy;
      if (m.pressed && m.button === 0) {
        var hit = null, list = rtsUnits();
        for (var i = list.length - 1; i >= 0; i--) {
          if (hitTest(list[i], m.x, m.y)) { hit = list[i]; break; }
        }
        if (hit) {
          if (!state.selection[hit.id]) state.selection = {};
          state.selection[hit.id] = true;
          state.boxStart = null;
        } else {
          state.selection = {};
          state.boxStart = { x: wx, y: wy };
        }
      }
      if (state.boxStart && m.released) {
        var x1 = Math.min(state.boxStart.x, wx), x2 = Math.max(state.boxStart.x, wx);
        var y1 = Math.min(state.boxStart.y, wy), by2 = Math.max(state.boxStart.y, wy);
        if (x2 - x1 > 4 || by2 - y1 > 4) {
          rtsUnits().forEach(function (u) {
            if (u.x >= x1 && u.x <= x2 && u.y >= y1 && u.y <= by2) state.selection[u.id] = true;
          });
        }
        state.boxStart = null;
      }
      if (m.pressed && m.button === 2) {
        var k = 0;
        state.instances.forEach(function (u) {
          if (!state.selection[u.id]) return;
          var col = k % 6, row = Math.floor(k / 6); k++;
          u.moveTarget = { x: wx + col * 30 - 75, y: wy + row * 30 - 30 };
          u.moveSpeed = behav(u).speed || 150;
        });
        if (k) beep(660, 0.05);
      }
    }

    /* ---------------- camara ---------------- */
    function updateCamera(dt) {
      var sc = scenes[state.sceneId]; if (!sc) return;
      var cam = state.camera;
      var vw = VW / cam.zoom, vh = VH / cam.zoom;
      var tx = cam.x, ty = cam.y;
      if (cam.followId != null) {
        var inst = null;
        for (var i = 0; i < state.instances.length; i++) if (state.instances[i].id === cam.followId) inst = state.instances[i];
        if (inst) {
          /* anticipacion: mira hacia donde va (velocidad x segundos); zona muerta: no se mueve si el objeto no sale de ella */
          var la = cam.look || 0, lx = la ? clamp(inst.vx * la, -vw / 3, vw / 3) : 0, ly = la ? clamp(inst.vy * la * 0.5, -vh / 4, vh / 4) : 0;
          cam.lookX = (cam.lookX || 0) + (lx - (cam.lookX || 0)) * Math.min(1, dt * 4);
          cam.lookY = (cam.lookY || 0) + (ly - (cam.lookY || 0)) * Math.min(1, dt * 4);
          tx = inst.x + cam.lookX - vw / 2; ty = inst.y + cam.lookY - vh / 2;
          var dz = cam.dead || 0;
          if (dz > 0) {
            tx = Math.abs(tx - cam.x) <= dz ? cam.x : tx - Math.sign(tx - cam.x) * dz;
            ty = Math.abs(ty - cam.y) <= dz * 0.6 ? cam.y : ty - Math.sign(ty - cam.y) * dz * 0.6;
          }
        }
      }
      if (cam.moveTo) {
        tx = cam.moveTo.x - vw / 2; ty = cam.moveTo.y - vh / 2;
        if (Math.abs(tx - cam.x) < 2 && Math.abs(ty - cam.y) < 2) cam.moveTo = null;
      }
      var k = cam.smooth > 0 ? 1 - Math.pow(1 - clamp(cam.smooth, 0.01, 1), dt * 60) : 1;
      cam.x += (tx - cam.x) * k;
      cam.y += (ty - cam.y) * k;
      /* sin limites ("none"): la camara puede salirse de la escena (zoom, temblores y giros quedan centrados) */
      var lim = cam.limits || { x: 0, y: 0, w: sc.width, h: sc.height };
      if (lim !== 'none') {
        cam.x = clamp(cam.x, lim.x, Math.max(lim.x, lim.x + lim.w - vw));
        cam.y = clamp(cam.y, lim.y, Math.max(lim.y, lim.y + lim.h - vh));
      }
      if (cam.shakeT > 0) cam.shakeT -= dt;
    }

    /* ---------------- cambio de escena con transicion ---------------- */
    function changeScene(name, transition, dur) {
      var target = null;
      for (var i = 0; i < (project.scenes || []).length; i++) {
        if (project.scenes[i].id === name || project.scenes[i].name === name) target = project.scenes[i];
      }
      if (!target) return;
      if (!transition || transition === 'none') { loadScene(target.id); return; }
      state.transition = {
        type: transition, phase: 'out', t: 0,
        dur: Math.max(0.05, n(dur, 0.5)), nextId: target.id
      };
    }

    /* ---------------- condiciones ---------------- */
    function pick(picks, objId, instId) {
      if (!picks[objId]) picks[objId] = {};
      else if (oneMemo) oneMemo.delete(picks[objId]);
      picks[objId][instId] = true;
    }
    function mouseBtn(b) { return b === 'right' ? 2 : b === 'middle' ? 1 : 0; }
    function saveKey(slot) { return 'source2d.save.' + slot; }
    function readSave(slot) {
      try { return JSON.parse(localStorage.getItem(saveKey(slot))); } catch (e) { return null; }
    }
    function condResult(cond, picks) {
      var p = cond.params || {}, t = cond.type, i, list, found;
      switch (t) {
        case 'always': return true;
        /* --- fase 3: objetos, animaciones, temporizadores y comportamientos --- */
        case 'distance': {
          var dl = objInstances(p.object), dw = objInstances(p.with), dok = false, dmax = resolveVal(p.value);
          for (i = 0; i < dl.length; i++) for (var dj = 0; dj < dw.length; dj++) {
            if (dl[i] === dw[dj]) continue;
            var ddx = dl[i].x - dw[dj].x, ddy = dl[i].y - dw[dj].y;
            if (compare(Math.sqrt(ddx * ddx + ddy * ddy), p.op, dmax)) { pick(picks, p.object, dl[i].id); pick(picks, p.with, dw[dj].id); dok = true; }
          }
          return dok;
        }
        case 'pointInside': {
          list = objInstances(p.object); found = false;
          var ptx = resolveVal(p.x), pty = resolveVal(p.y);
          for (i = 0; i < list.length; i++) if (instContains(list[i], ptx, pty)) { pick(picks, p.object, list[i].id); found = true; }
          return found;
        }
        case 'animEvent': {
          list = objInstances(p.object); found = false;
          var aen = resolveStr(p.name);
          for (i = 0; i < list.length; i++) if (list[i].animEvF === state.frame && list[i].animEv && list[i].animEv.indexOf(aen) >= 0) { pick(picks, p.object, list[i].id); found = true; }
          return found;
        }
        case 'pickNearest': case 'pickFarthest': {
          list = objInstances(p.object);
          var px = resolveVal(p.x), py = resolveVal(p.y), bi = null, bd = 0;
          for (i = 0; i < list.length; i++) {
            var dd = (list[i].x - px) * (list[i].x - px) + (list[i].y - py) * (list[i].y - py);
            if (bi === null || (t === 'pickNearest' ? dd < bd : dd > bd)) { bi = list[i]; bd = dd; }
          }
          if (bi) pick(picks, p.object, bi.id);
          return !!bi;
        }
        case 'pickRandom': {
          list = objInstances(p.object);
          if (!list.length) return false;
          pick(picks, p.object, list[(Math.random() * list.length) | 0].id);
          return true;
        }
        case 'raycast': {
          list = objInstances(p.object); found = false;
          var rang = resolveVal(p.angle) * Math.PI / 180, rdist = Math.max(1, resolveVal(p.distance));
          for (i = 0; i < list.length; i++) {
            var rh = castRay(list[i], Math.cos(rang), Math.sin(rang), rdist, p.target || '');
            if (rh) { state.ray = rh; pick(picks, p.object, list[i].id); found = true; }
          }
          return found;
        }
        case 'animIs': case 'animEnded': case 'objTimer': case 'isDragged': case 'dropped': case 'health': case 'isDead':
        case 'isFlashing': case 'platJumping': case 'platFalling': case 'canFire': case 'tweenDone': case 'isTweening':
        case 'pathArrived': case 'onPath': case 'pathFailed': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            var it = list[i], ok;
            switch (t) {
              case 'animIs': ok = String(it.animName || '').toLowerCase() === resolveStr(p.name).toLowerCase(); break;
              case 'animEnded': ok = !!it.animDone; break;
              case 'objTimer': { var ot = it.timers[resolveStr(p.name)]; ok = compare(ot ? ot.t : 0, p.op, resolveVal(p.value)); break; }
              case 'isDragged': ok = state.dragId === it.id; break;
              case 'dropped': ok = it.droppedFrame === state.frame; break;
              case 'health': ok = compare(n(it.hp, 0), p.op, resolveVal(p.value)); break;
              case 'isDead': ok = it.hp != null && it.hp <= 0; break;
              case 'isFlashing': ok = it.flashT > 0; break;
              case 'tweenDone': ok = it.tweenEnd != null && it.tweenEnd >= state.frame - 1; break;
              case 'isTweening': ok = it.tweens.length > 0; break;
              case 'pathArrived': ok = it.pathEnd != null && it.pathEnd >= state.frame - 1; break;
              case 'onPath': ok = !!it.path; break;
              case 'pathFailed': ok = it.pathFail != null && it.pathFail >= state.frame - 1; break;
              case 'platJumping': ok = !it.grounded && it.vy < 0; break;
              case 'platFalling': ok = !it.grounded && it.vy > 0 && !it.climbing; break;
              default: ok = !(it.fireCD > 0);
            }
            if (ok) { pick(picks, p.object, it.id); found = true; }
          }
          return found;
        }
        case 'atStart': return state.frame <= 1;
        case 'keyDown': return key(keyParam(p.key));
        case 'boolVar': return !!readVar(p.variable);
        case 'arrayContains': { var av = readVar(p.variable), aw = resolveStr(p.value); if (!Array.isArray(av)) return false; for (i = 0; i < av.length; i++) if (String(av[i]) === aw) return true; return false; }
        case 'arrayLength': { var al = readVar(p.variable); return compare(Array.isArray(al) ? al.length : 0, p.op, resolveVal(p.value)); }
        case 'textContains': return resolveStr(p.a).indexOf(resolveStr(p.b)) >= 0;
        case 'btnDown': return btnHit(p.object, picks, !!state.mouse.down);
        case 'btnReleased': return btnHit(p.object, picks, !!state.mouse.released);
        case 'textIs': {
          var til = objInstances(p.object), tfound = false, ti;
          for (ti = 0; ti < til.length; ti++) {
            var tdef = objDef(til[ti]).text || {};
            var tcur = resolveStr(til[ti].textOverride != null ? til[ti].textOverride : (tdef.content || ''));
            if (tcur === resolveStr(p.text)) { pick(picks, p.object, til[ti].id); tfound = true; }
          }
          return tfound;
        }
        case 'tileHas': return tileCells(p.object, tnum(p.tile), p.dir, p.what, tnum(p.other)).length > 0;
        case 'cursorTile': { var ct = cellAtCursor(p.object); return !!ct && ct.tm.data[ct.i] === tnum(p.tile); }
        case 'objectExists': return objInstances(p.object).length > 0;
        case 'sceneIs': return state.sceneId === p.scene;
        case 'musicPlaying': return musicIsPlaying();
        case 'soundPlaying': { var spt = findTrack(resolveStr(p.track || '')); try { return !!spt && !!N.soundPlaying && !!N.soundPlaying(spt.id); } catch (e) { return false; } }
        case 'gamePaused': return !!state.paused;
        case 'languageIs': return curLang() === resolveStr(p.lang);
        case 'messageReceived': return !!state.msg[resolveStr(p.name)];
        case 'padConnected': try { return !!N.pad && !!N.pad(1, n(p.pad, 1) - 1); } catch (e) { return false; }
        case 'padAxis': { var pav = 0; try { pav = N.pad ? N.pad(0, n(p.pad, 1) - 1, n(p.axis, 0)) : 0; } catch (e) { quiet(e); } return compare(pav, p.op, resolveVal(p.value)); }
        case 'anyKeyPressed': return Object.keys(state.pressed).some(function (k) { return !!state.pressed[k]; });
        case 'anyKeyDown': return Object.keys(state.keys).some(function (k) { return !!state.keys[k]; });
        case 'mouseReleased': return state.mouse.released && mouseBtn(p.button) === state.mouse.button;
        case 'wheelUp': return !!state.wheelUp;
        case 'wheelDown': return !!state.wheelDown;
        case 'cmpNum': return compare(resolveVal(p.a), p.op, resolveVal(p.b));
        case 'exprTrue': { var ev0 = evalExpr(p.value); return !!ev0 && ev0 !== '0'; }
        case 'cmpStr': return compare(resolveStr(p.a), p.op, resolveStr(p.b));
        case 'timerCompare': {
          var tmr = state.timers[resolveStr(p.name)];
          return compare(tmr ? tmr.t : 0, p.op, resolveVal(p.value));
        }
        case 'saveExists': return readSave(resolveStr(p.slot)) != null;
        case 'keyPressed': return pressedKey(keyParam(p.key));
        case 'keyReleased': { var kr = keyParam(p.key); return !!state.released[kr] || (PAD_ALIAS[kr] !== undefined && !!state.released[PAD_ALIAS[kr]]); }
        case 'mouseDown': return state.mouse.down && mouseBtn(p.button) === state.mouse.button;
        case 'mousePressed': return state.mouse.pressed && mouseBtn(p.button) === state.mouse.button;
        case 'mouseInRect': {
          var mm = state.mouse.wx, my = state.mouse.wy;
          return mm >= n(p.x) && mm <= n(p.x) + n(p.w) && my >= n(p.y) && my <= n(p.y) + n(p.h);
        }
        case 'mouseOn': case 'btnHover': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            if (hitTest(list[i], state.mouse.x, state.mouse.y)) { pick(picks, p.object, list[i].id); found = true; }
          }
          return found;
        }
        case 'clicked': case 'btnClicked': {
          list = objInstances(p.object); found = false;
          if (state.mouse.pressed && state.mouse.button === 0) {
            for (i = 0; i < list.length; i++) {
              if (list[i].visible && hitTest(list[i], state.mouse.x, state.mouse.y)) {
                pick(picks, p.object, list[i].id); found = true;
              }
            }
          }
          return found;
        }
        case 'onWall': {
          var lwl = objInstances(p.object); found = false;
          for (i = 0; i < lwl.length; i++) if (lwl[i].wallL || lwl[i].wallR) { pick(picks, p.object, lwl[i].id); found = true; }
          return found;
        }
        case 'onGround': {
          var lg = objInstances(p.object); found = false;
          for (i = 0; i < lg.length; i++) if (lg[i].grounded) { pick(picks, p.object, lg[i].id); found = true; }
          return found;
        }
        case 'standsOn': {
          var lo = objInstances(p.object), lw = objInstances(p.with); found = false;
          for (i = 0; i < lo.length; i++) {
            var gr = lo[i].groundRef;
            if (!gr) continue;
            for (var k2 = 0; k2 < lw.length; k2++) {
              if (lw[k2] === gr) { pick(picks, p.object, lo[i].id); pick(picks, p.with, lw[k2].id); found = true; }
            }
          }
          return found;
        }
        case 'collision': {
          var la = objInstances(p.object), lb = objInstances(p.with); found = false;
          for (i = 0; i < la.length; i++) {
            for (var j = 0; j < lb.length; j++) {
              if (la[i].id === lb[j].id) continue;
              if (instOverlap(la[i], lb[j])) {
                pick(picks, p.object, la[i].id); pick(picks, p.with, lb[j].id); found = true;
              }
            }
          }
          return found;
        }
        case 'objVar': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            if (compare(list[i].vars[p.variable], p.op, resolveStr(p.value))) {
              pick(picks, p.object, list[i].id); found = true;
            }
          }
          return found;
        }
        case 'globalVar': return compare(readVar(p.variable), p.op, resolveStr(p.value));
        case 'varCmp': return compare(readVar(p.variable), p.op, evalExpr(p.value));
        case 'objCount': return compare(objInstances(p.object).length, p.op, p.value);
        case 'objPos': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            var v = p.axis === 'y' ? list[i].y : list[i].x;
            if (compare(v, p.op, resolveStr(p.value))) { pick(picks, p.object, list[i].id); found = true; }
          }
          return found;
        }
        case 'objAngle': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            if (compare(list[i].angle, p.op, resolveStr(p.value))) { pick(picks, p.object, list[i].id); found = true; }
          }
          return found;
        }
        case 'platEvent': case 'touchesTile': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            var pe = list[i], okp;
            if (t === 'touchesTile') okp = touchesTileFlag(pe, p.flag || 'danger');
            else if (p.what === 'dashing') okp = pe.dashT > 0;
            else okp = (p.what === 'air' ? pe.evAirJump : p.what === 'dash' ? pe.evDash : p.what === 'land' ? pe.evLand : p.what === 'wall' ? pe.evWallJump : pe.evJump) === state.frame;
            if (okp) { pick(picks, p.object, pe.id); found = true; }
          }
          return found;
        }
        case 'objInRect': {
          list = objInstances(p.object); found = false;
          var rx = n(p.x), ry = n(p.y), rw = n(p.w), rh = n(p.h);
          for (i = 0; i < list.length; i++) {
            if (list[i].x >= rx && list[i].x <= rx + rw && list[i].y >= ry && list[i].y <= ry + rh) {
              pick(picks, p.object, list[i].id); found = true;
            }
          }
          return found;
        }
        case 'objOutside': {
          var sc2 = scenes[state.sceneId] || { width: VW, height: VH };
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            var s3 = instSize(list[i]);
            if (list[i].x + s3.w / 2 < 0 || list[i].x - s3.w / 2 > sc2.width ||
                list[i].y + s3.h / 2 < 0 || list[i].y - s3.h / 2 > sc2.height) {
              pick(picks, p.object, list[i].id); found = true;
            }
          }
          return found;
        }
        case 'objVisible': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) if (list[i].visible) { pick(picks, p.object, list[i].id); found = true; }
          return found;
        }
        case 'objGrounded': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) if (list[i].grounded) { pick(picks, p.object, list[i].id); found = true; }
          return found;
        }
        case 'objMoving': {
          list = objInstances(p.object); found = false;
          for (i = 0; i < list.length; i++) {
            if (Math.abs(list[i].vx) > 1 || Math.abs(list[i].vy) > 1) { pick(picks, p.object, list[i].id); found = true; }
          }
          return found;
        }
        case 'timeFromStart': return compare(state.time, p.op, resolveStr(p.value));
        case 'random': return Math.random() * 100 < n(p.percent, 50);
        case 'jsCode': {
          var r = runCode(p.code, null, 'condicion');
          return !!r;
        }
        default: return extCond(t, p);
      }
    }

    /* ---------------- acciones ---------------- */
    function targets(objId, picks) {
      var set = picks[objId];
      if (set) {
        instIndex();
        var k1 = onlyKeyMemo(set);
        var grp = objects[objId] && objects[objId].type === 'group';
        if (k1 !== null) { var one = ixById[k1]; if (one && (one.objId === objId || (grp && objects[objId].members.indexOf(one.objId) >= 0))) return [one]; }
        var out = (ixByObj[objId] || groupInstances(objId)).filter(function (i) { return set[i.id]; });
        if (out.length) return out;
      }
      return objInstances(objId);
    }
    function findObjId(name) { return objIdNamed(name); }
    function typeTick(dt) {
      for (var i = 0; i < state.instances.length; i++) {
        var t = state.instances[i];
        if (t.typeFull == null) continue;
        t.typeT = (t.typeT || 0) + dt * (t.typeSpeed || 20);
        var k = Math.min(t.typeFull.length, Math.floor(t.typeT));
        t.textOverride = t.typeFull.slice(0, k);
        if (k >= t.typeFull.length) t.typeFull = null;
      }
    }
    function tileMapsOf(objId) {
      var o = null, i, sc = scenes[state.sceneId], tms = (sc && sc.tilemaps) || [], out = [];
      for (i = 0; i < project.objects.length; i++) if (project.objects[i].id === objId) o = project.objects[i];
      for (i = 0; i < tms.length; i++) {
        if (!tms[i].data || !tilesets[tms[i].tilesetId]) continue;
        if (!o || !o.tilesetId || tms[i].tilesetId === o.tilesetId || tms[i].objectId === objId) out.push(tms[i]);
      }
      return out;
    }
    function neighborOk(tm, r, c, dir, what, other) {
      var nr = r, nc = c;
      if (dir === 'up') nr--; else if (dir === 'down') nr++; else if (dir === 'left') nc--; else nc++;
      var v = (nr < 0 || nc < 0 || nr >= tm.rows || nc >= tm.cols) ? -1 : tm.data[nr * tm.cols + nc];
      if (what === 'empty') return v < 0;
      if (what === 'any') return v >= 0;
      return v === n(other, 0);
    }
    function tileCells(objId, tile, dir, what, other) {
      var out = [], maps = tileMapsOf(objId), m, r, c, idx;
      for (m = 0; m < maps.length; m++) for (r = 0; r < maps[m].rows; r++) for (c = 0; c < maps[m].cols; c++) {
        idx = r * maps[m].cols + c;
        if (maps[m].data[idx] === tile && neighborOk(maps[m], r, c, dir, what, other)) out.push({ tm: maps[m], i: idx });
      }
      return out;
    }
    function cellAtCursor(objId) {
      var maps = tileMapsOf(objId), m, ts, c, r;
      for (m = 0; m < maps.length; m++) {
        ts = tilesets[maps[m].tilesetId];
        c = Math.floor((state.mouse.wx - maps[m].x) / ts.tileSize);
        r = Math.floor((state.mouse.wy - maps[m].y) / ts.tileSize);
        if (c >= 0 && r >= 0 && c < maps[m].cols && r < maps[m].rows) return { tm: maps[m], i: r * maps[m].cols + c };
      }
      return null;
    }
    function tnum(v) { return v == null ? 0 : n(resolveVal(v), 0); }
    function btnHit(objId, picks, ok) {
      var bdl = objInstances(objId), bfound = false, bi;
      if (ok && state.mouse.button === 0) for (bi = 0; bi < bdl.length; bi++) if (bdl[bi].visible && hitTest(bdl[bi], state.mouse.x, state.mouse.y)) { pick(picks, objId, bdl[bi].id); bfound = true; }
      return bfound;
    }
    function doAction(act, picks) {
      var p = act.params || {}, t = act.type, i;
      var oid = p.object != null ? p.object : null;
      var list = oid ? targets(oid, picks) : [];
      switch (t) {
        case 'setVX': for (i = 0; i < list.length; i++) { list[i].vx = resolveVal(p.value); list[i].moveTarget = null; } break;
        case 'setVY': for (i = 0; i < list.length; i++) list[i].vy = resolveVal(p.value); break;
        case 'addVX': for (i = 0; i < list.length; i++) list[i].vx += resolveVal(p.value); break;
        case 'addVY': for (i = 0; i < list.length; i++) list[i].vy += resolveVal(p.value); break;
        case 'addForce':
          for (i = 0; i < list.length; i++) {
            var mass = Math.max(0.01, n(behav(list[i]).mass, 1));
            list[i].vx += resolveVal(p.x) / mass;
            list[i].vy += resolveVal(p.y) / mass;
          }
          break;
        case 'setPos': { var _x = once(p.x); var _y = once(p.y); for (i = 0; i < list.length; i++) { list[i].x = (_x !== null ? _x : resolveVal(p.x)); list[i].y = (_y !== null ? _y : resolveVal(p.y)); list[i].moveTarget = null; } break; }
        case 'setX': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].x = (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'setBool': writeVar(p.variable, resolveStr(p.value) === 'true'); break;
        case 'toggleBool': writeVar(p.variable, !readVar(p.variable)); break;
        case 'arrayPush': arrayVar(p.variable).push(autoNum(resolveStr(p.value))); break;
        case 'arrayPop': { var pp = readVar(p.variable); if (Array.isArray(pp)) pp.pop(); break; }
        case 'arrayClear': writeVar(p.variable, []); break;
        /* --- variables con expresiones, arrays y funciones --- */
        case 'setVar': {
          var sv = evalExpr(p.value), op = p.op || 'set';
          if (op !== 'set') {
            var cur = n(readVar(p.variable)), num = typeof sv === 'number' ? sv : n(sv);
            sv = op === 'add' ? cur + num : op === 'sub' ? cur - num : op === 'mul' ? cur * num : op === 'div' ? (num ? cur / num : 0) : op === 'mod' ? (num ? cur % num : 0) : num;
          }
          writeVar(p.variable, sv);
          break;
        }
        case 'setVarText': writeVar(p.variable, resolveStr(p.text)); break;
        case 'localVar': {
          var home = frames.length ? frames[frames.length - 1].vars : state.globals;
          home[lvalue(p.variable).name] = p.value == null || p.value === '' ? 0 : evalExpr(p.value);
          break;
        }
        case 'arrayFill': {
          var fsz = Math.max(0, Math.min(1000000, resolveVal(p.size) | 0)), fv = evalExpr(p.value), fa = new Array(fsz);
          for (i = 0; i < fsz; i++) fa[i] = fv;
          writeVar(p.variable, fa);
          break;
        }
        case 'arrayPushNum': arrayVar(p.variable).push(evalExpr(p.value)); break;
        case 'arrayResize': {
          var ra = arrayVar(p.variable), rn = Math.max(0, Math.min(1000000, resolveVal(p.size) | 0));
          if (rn < ra.length) ra.length = rn; else while (ra.length < rn) ra.push(0);
          break;
        }
        case 'arrayCopy': writeVar(p.to, (Array.isArray(readVar(p.from)) ? readVar(p.from) : []).slice()); break;
        case 'arrayAppend': { var dst = arrayVar(p.to), srcA = readVar(p.from); if (Array.isArray(srcA)) for (i = 0; i < srcA.length; i++) dst.push(srcA[i]); break; }
        case 'arrayRemoveAt': { var rma = readVar(p.variable), rmi = resolveVal(p.index) | 0; if (Array.isArray(rma) && rmi >= 0 && rmi < rma.length) rma.splice(rmi, 1); break; }
        case 'arrayInsertAt': { var ia = arrayVar(p.variable), ii = Math.max(0, Math.min(ia.length, resolveVal(p.index) | 0)); ia.splice(ii, 0, evalExpr(p.value)); break; }
        case 'arraySort': {
          var so = readVar(p.variable);
          if (Array.isArray(so)) {
            var desc = p.order === 'desc';
            so.sort(function (x, y) {
              var nx = typeof x === 'number', ny = typeof y === 'number';
              var r = nx && ny ? x - y : String(x) < String(y) ? -1 : String(x) > String(y) ? 1 : 0;
              return desc ? -r : r;
            });
          }
          break;
        }
        case 'callFunction': { var cr = callFn(String(p.name || '').trim(), evalArgs(p.args)); if (p.result) writeVar(p.result, cr); break; }
        case 'returnValue': if (frames.length) frames[frames.length - 1].ret = p.value == null || p.value === '' ? 0 : evalExpr(p.value); flow = FLOW_RETURN; break;
        case 'breakLoop': flow = FLOW_BREAK; break;
        case 'timerCtl': {
          var tn = resolveStr(p.name), ta = p.action || 'start';
          if (ta === 'remove') delete state.timers[tn];
          else if (ta === 'reset') state.timers[tn] = { t: 0, paused: false };
          else {
            if (!state.timers[tn]) state.timers[tn] = { t: 0, paused: false };
            state.timers[tn].paused = ta === 'pause';
          }
          break;
        }
        case 'timeScale': { var tsv = resolveVal(p.value); state.timeScale = isFinite(tsv) ? Math.max(0, tsv) : 1; break; }
        case 'saveGame': {
          try { localStorage.setItem(saveKey(resolveStr(p.slot)), snapshotGame(p.what === 'all')); } catch (e) { quiet(e); }
          break;
        }
        case 'loadGame': {
          restoreGame(readSave(resolveStr(p.slot)));
          break;
        }
        case 'deleteSave': {
          try { localStorage.removeItem(saveKey(resolveStr(p.slot))); } catch (e) { quiet(e); }
          break;
        }
        case 'hideCursor': if (canvas) canvas.style.cursor = 'none'; break;
        case 'showCursor': if (canvas) canvas.style.cursor = ''; break;
        case 'mulGlobal': writeVar(p.variable, n(readVar(p.variable)) * resolveVal(p.value)); break;
        case 'setY': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].y = (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'addToX': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].x += (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'addToY': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].y += (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'moveTo':
          for (i = 0; i < list.length; i++) {
            list[i].moveTarget = { x: resolveVal(p.x), y: resolveVal(p.y) };
            list[i].moveSpeed = p.speed ? resolveVal(p.speed) : 0;
          }
          break;
        case 'moveToObj': {
          var tg = p.target ? targets(p.target, picks) : [];
          for (i = 0; i < list.length; i++) {
            if (!tg.length) break;
            var best = tg[0], bd = Infinity;
            for (var k = 0; k < tg.length; k++) {
              var dd = Math.pow(tg[k].x - list[i].x, 2) + Math.pow(tg[k].y - list[i].y, 2);
              if (dd < bd) { bd = dd; best = tg[k]; }
            }
            list[i].moveTarget = { x: best.x, y: best.y };
            list[i].moveSpeed = p.speed ? resolveVal(p.speed) : 0;
          }
          break;
        }
        case 'stopMove':
          for (i = 0; i < list.length; i++) {
            list[i].moveTarget = null; list[i].vx = 0;
            if (!behav(list[i]).gravity) list[i].vy = 0;
          }
          break;
        case 'moveInDirection': {
          var ang = resolveVal(p.angle) * Math.PI / 180, sp = resolveVal(p.speed);
          for (i = 0; i < list.length; i++) {
            list[i].vx = Math.cos(ang) * sp; list[i].vy = Math.sin(ang) * sp;
            list[i].moveTarget = null;
          }
          break;
        }
        case 'jump':
          for (i = 0; i < list.length; i++) {
            if (list[i].grounded || p.always === 'yes') list[i].vy = -Math.abs(resolveVal(p.value));
            list[i].grounded = false;
          }
          break;
        case 'lookAt': {
          var lx = resolveVal(p.x), ly = resolveVal(p.y);
          for (i = 0; i < list.length; i++) list[i].angle = Math.atan2(ly - list[i].y, lx - list[i].x) * 180 / Math.PI;
          break;
        }
        case 'setAngle': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].angle = (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'rotateBy': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].angle += (_value !== null ? _value : resolveVal(p.value)); break; }
        case 'setSpin': for (i = 0; i < list.length; i++) list[i].spin = resolveVal(p.value); break;
        case 'setScale': { var _x = once(p.x); var _y = once(p.y); for (i = 0; i < list.length; i++) { list[i].scaleX = (_x !== null ? _x : resolveVal(p.x)); list[i].scaleY = (_y !== null ? _y : resolveVal(p.y)); } break; }
        case 'setOpacity': { var _value = once(p.value); for (i = 0; i < list.length; i++) list[i].opacity = clamp((_value !== null ? _value : resolveVal(p.value)), 0, 100); break; }
        case 'show': for (i = 0; i < list.length; i++) list[i].visible = true; break;
        case 'hide': for (i = 0; i < list.length; i++) list[i].visible = false; break;
        case 'setFrame': for (i = 0; i < list.length; i++) list[i].frame = resolveVal(p.value); break;
        case 'platOn': for (i = 0; i < list.length; i++) list[i].platOff = false; break;
        case 'platOff': for (i = 0; i < list.length; i++) list[i].platOff = true; break;
        case 'platAbility': for (i = 0; i < list.length; i++) { list[i].abil = list[i].abil || {}; list[i].abil[p.ability || 'dash'] = p.state !== 'off'; } break;
        case 'platFlip': for (i = 0; i < list.length; i++) list[i].pdir = -(list[i].pdir || 1); break;
        case 'jumpNow': for (i = 0; i < list.length; i++) { list[i].vy = -Math.abs(resolveVal(p.value)); list[i].grounded = false; } break;
        case 'setAnim': for (i = 0; i < list.length; i++) list[i].animForce = resolveStr(p.name) || null; break;
        case 'setFlip': for (i = 0; i < list.length; i++) list[i].faceX = resolveVal(p.value) < 0 ? -1 : 1; break;
        case 'nextFrame': for (i = 0; i < list.length; i++) list[i].frame++; break;
        case 'setEffect':
          for (i = 0; i < list.length; i++) {
            list[i].effectFilter = p.filter ? resolveStr(p.filter) : null;
          }
          break;
        case 'create': {
          var cid = p.object && objects[p.object] ? p.object : findObjId(p.object);
          var ni = createInstance(cid, resolveVal(p.x), resolveVal(p.y));
          if (ni) { if (p.z != null && p.z !== '') ni.z = resolveVal(p.z); picks[cid] = {}; picks[cid][ni.id] = true; }
          break;
        }
        case 'removeObj': {
          var dead = {};
          list.forEach(function (x) { dead[x.id] = true; });
          state.instances = state.instances.filter(function (x) { return !dead[x.id]; });
          break;
        }
        case 'setObjVar': for (i = 0; i < list.length; i++) list[i].vars[p.variable] = autoNum(resolveStr(p.value)); break;
        case 'setZ': for (i = 0; i < list.length; i++) list[i].z = resolveVal(p.value); break;
        case 'addObjVar': for (i = 0; i < list.length; i++) list[i].vars[p.variable] = n(list[i].vars[p.variable]) + resolveVal(p.value); break;
        case 'setGlobal': writeVar(p.variable, resolveStr(p.value)); break;
        case 'addGlobal': writeVar(p.variable, n(readVar(p.variable)) + resolveVal(p.value)); break;
        case 'setText': for (i = 0; i < list.length; i++) { list[i].textOverride = resolveStr(p.text); list[i].typeFull = null; } break;   /* corta la maquina de escribir */
        case 'textAppend': for (i = 0; i < list.length; i++) list[i].textOverride = (list[i].textOverride != null ? list[i].textOverride : resolveStr((objDef(list[i]).text || {}).content || '')) + resolveStr(p.text); break;
        case 'typewriter': for (i = 0; i < list.length; i++) { list[i].typeFull = resolveStr(p.text); list[i].typeT = 0; list[i].typeSpeed = Math.max(1, tnum(p.speed) || 20); list[i].textOverride = ''; } break;
        case 'setTextColor': for (i = 0; i < list.length; i++) list[i].textColor = resolveStr(p.color); break;
        case 'setButtonText': for (i = 0; i < list.length; i++) list[i].btnText = resolveStr(p.text); break;
        case 'setButtonColor': for (i = 0; i < list.length; i++) list[i].btnBg = resolveStr(p.color); break;
        case 'setButtonFg': for (i = 0; i < list.length; i++) list[i].btnFg = resolveStr(p.color); break;
        case 'particles': {
          var pcount = Math.max(0, Math.min(500, resolveVal(p.count) | 0)), pox = resolveVal(p.x), poy = resolveVal(p.y);
          var popt = { color: resolveStr(p.color) || '#ffffff', speed: resolveVal(p.speed), spread: resolveVal(p.spread), angle: resolveVal(p.angle),
                       life: Math.max(0.02, resolveVal(p.life)), size: Math.max(1, resolveVal(p.size)), gravity: resolveVal(p.gravity), glow: p.glow === 'yes',
                       image: p.image && objects[p.image] ? objects[p.image].spriteId : null, spin: resolveVal(p.spin || 0) };
          if (oid) for (i = 0; i < list.length; i++) emitParticles(list[i].x + pox, list[i].y + poy, pcount, popt);
          else emitParticles(pox, poy, pcount, popt);
          break;
        }
        case 'quitGame': state.running = false; N.quit(); break;
        case 'tilePlaceCursor': { var tpc = cellAtCursor(oid); if (tpc) tpc.tm.data[tpc.i] = tnum(p.tile); invalidateTiles(); break; }
        case 'tileEraseCursor': { var tec = cellAtCursor(oid); if (tec) tec.tm.data[tec.i] = -1; invalidateTiles(); break; }
        case 'tilePlace': {
          var tpm = tileMapsOf(oid)[0], tpc2 = tnum(p.col), tpr = tnum(p.row);
          if (tpm && tpc2 >= 0 && tpr >= 0 && tpc2 < tpm.cols && tpr < tpm.rows) tpm.data[tpr * tpm.cols + tpc2] = tnum(p.tile);
          invalidateTiles();
          break;
        }
        case 'tileConvert': {
          var tcs = tileCells(oid, tnum(p.from), p.dir, p.what, tnum(p.other)), to = tnum(p.to), q;
          for (q = 0; q < tcs.length; q++) tcs[q].tm.data[tcs[q].i] = to;
          invalidateTiles();
          break;
        }
        case 'setButtonText':
          for (i = 0; i < list.length; i++) {
            var o = objDef(list[i]);
            if (!o.button) o.button = {};
            o.button.text = resolveStr(p.text);
          }
          break;
        case 'setGravity': for (i = 0; i < list.length; i++) behav(list[i]).gravity = p.state !== 'off'; break;
        case 'setSolid': for (i = 0; i < list.length; i++) behav(list[i]).solid = p.state !== 'off'; break;
        case 'setFriction': for (i = 0; i < list.length; i++) behav(list[i]).friction = resolveVal(p.value); break;
        case 'setBounce': for (i = 0; i < list.length; i++) behav(list[i]).bounce = resolveVal(p.value); break;
        case 'tweenPos':
          for (i = 0; i < list.length; i++) {
            startTween(list[i], 'x', resolveVal(p.x), p.duration, p.easing);
            startTween(list[i], 'y', resolveVal(p.y), p.duration, p.easing);
          }
          break;
        case 'tweenScale':
          for (i = 0; i < list.length; i++) {
            startTween(list[i], 'scaleX', resolveVal(p.x), p.duration, p.easing);
            startTween(list[i], 'scaleY', resolveVal(p.y), p.duration, p.easing);
          }
          break;
        case 'tweenAngle': for (i = 0; i < list.length; i++) startTween(list[i], 'angle', resolveVal(p.value), p.duration, p.easing); break;
        case 'tweenOpacity': for (i = 0; i < list.length; i++) startTween(list[i], 'opacity', resolveVal(p.value), p.duration, p.easing); break;
        case 'tweenStop': for (i = 0; i < list.length; i++) stopTweens(list[i]); break;
        case 'cameraFollow':
          state.camera.followId = list.length ? list[list.length - 1].id : null;
          if (p.smoothing != null) state.camera.smooth = n(p.smoothing);
          state.camera.look = Math.max(0, n(p.lookahead, 0)); state.camera.dead = Math.max(0, n(p.deadzone, 0));
          state.camera.moveTo = null;
          break;
        case 'cameraMoveTo': state.camera.moveTo = { x: resolveVal(p.x), y: resolveVal(p.y) }; state.camera.followId = null; break;
        case 'cameraSetPos': state.camera.x = resolveVal(p.x); state.camera.y = resolveVal(p.y); state.camera.moveTo = null; break;
        case 'cameraZoom': state.camera.zoom = clamp(resolveVal(p.value), 0.2, 4); break;
        case 'cameraLimits':
          state.camera.limits = p.mode === 'custom'
            ? { x: resolveVal(p.x), y: resolveVal(p.y), w: resolveVal(p.w), h: resolveVal(p.h) }
            : p.mode === 'none' ? 'none' : null;
          break;
        case 'cameraFree': state.camera.followId = null; state.camera.moveTo = null; break;
        case 'shakeCamera': state.camera.shakeT = state.camera.shakeDur = resolveVal(p.duration) || 0.3; state.camera.shakeMag = resolveVal(p.intensity) || 10; break;
        case 'hitStop': state.hitstop = Math.max(0, resolveVal(p.seconds)); break;
        case 'flashWhite': for (i = 0; i < list.length; i++) list[i].whiteT = Math.max(0.01, resolveVal(p.duration)); break;
        case 'squash': for (i = 0; i < list.length; i++) startSquash(list[i], resolveVal(p.amount) / 100, resolveVal(p.duration)); break;
        case 'changeScene': changeScene(String(p.scene || '').indexOf('{') >= 0 ? resolveStr(p.scene) : p.scene, p.transition, p.duration); break;
        case 'restartScene': loadScene(state.sceneId, true); break;
        case 'nextScene': {
          var arr = project.scenes || [], idx = -1;
          for (i = 0; i < arr.length; i++) if (arr[i].id === state.sceneId) idx = i;
          if (idx >= 0 && idx + 1 < arr.length) changeScene(arr[idx + 1].id, p.transition, p.duration);
          break;
        }
        case 'endGame': state.ended = true; state.endMessage = resolveStr(p.message); break;
        case 'fullscreen': toggleFullscreen(); break;
        case 'beep': beep(resolveVal(p.freq), resolveVal(p.duration) || 0.12); break;
        case 'playMusic': playMusicTrack(resolveStr(p.track || ''), String(p.loop || 'yes') !== 'no'); break;
        case 'playSound': playSoundTrack(resolveStr(p.track || ''), resolveVal(p.volume == null ? 100 : p.volume) / 100, resolveVal(p.pitch == null ? 1 : p.pitch), resolveVal(p.pan || 0), n(p.channel, 0), resolveVal(p.vary || 0) / 100); break;
        case 'playSoundAt': for (i = 0; i < list.length; i++) playSoundAt(resolveStr(p.track || ''), list[i], resolveVal(p.volume == null ? 100 : p.volume) / 100, resolveVal(p.range || 600), n(p.channel, 0)); break;
        case 'channelStop': channelOp(0, n(p.channel, 0)); break;
        case 'channelPause': channelOp(String(p.mode) === 'resume' ? 2 : 1, n(p.channel, 0)); break;
        case 'channelVolume': channelOp(3, n(p.channel, 0), clamp(resolveVal(p.value) / 100, 0, 1)); break;
        case 'vibrate': try { if (N.pad) N.pad(2, n(p.pad, 1) - 1, clamp(resolveVal(p.strength) / 100, 0, 1), clamp(resolveVal(p.strength) / 100, 0, 1), Math.max(0.01, resolveVal(p.duration))); } catch (e) { quiet(e); } break;
        case 'pauseGame': state.paused = String(p.mode) === 'toggle' ? !state.paused : String(p.mode) !== 'off'; if (N.channel) channelOp(state.paused ? 1 : 2, -1); break;
        case 'sendMessage': state.msgNext[resolveStr(p.name)] = true; break;
        case 'setLanguage': state.lang = resolveStr(p.lang); break;
        case 'setDarkness': state.darkness = clamp(resolveVal(p.value), 0, 100); if (p.color) state.darkColor = resolveStr(p.color); break;
        case 'layerDarkness': {
          var ldn = resolveStr(p.layer), ldk = null;
          for (var lk in layers) if (lk === ldn || layers[lk].name === ldn) ldk = lk;
          if (ldk) (state.layerDark || (state.layerDark = {}))[ldk] = { v: clamp(resolveVal(p.value), 0, 100), c: p.color ? resolveStr(p.color) : null };
          break;
        }
        case 'pathTo': {
          var ptx = resolveVal(p.x), pty = resolveVal(p.y);
          anyPath = true;
          for (i = 0; i < list.length; i++) { list[i].path = findPath(list[i], ptx, pty); list[i].pathSpeed = Math.max(1, resolveVal(p.speed)); if (!list[i].path) list[i].pathFail = state.frame; }
          break;
        }
        case 'pathStop': for (i = 0; i < list.length; i++) list[i].path = null; break;
        case 'musicVolume': setMusicVolume(resolveVal(p.value) / 100); break;
        case 'musicSeek': seekMusic(resolveVal(p.seconds)); break;
        case 'sfx': sfxTone(String(p.wave || 'sine'), resolveVal(p.freqFrom), resolveVal(p.freqTo), resolveVal(p.duration), resolveVal(p.volume) / 100); break;
        case 'setColor': {
          var tc = parseRGB(resolveStr(p.color));
          if (tc && tc[0] === 255 && tc[1] === 255 && tc[2] === 255) tc = null;
          for (i = 0; i < list.length; i++) list[i].tint = tc;
          break;
        }
        case 'setBgColor': { var bc = parseRGB(resolveStr(p.color)); state.bg = bc ? 'rgb(' + bc.join(',') + ')' : null; break; }
        case 'cameraRotate': state.camera.rot = resolveVal(p.value); break;
        case 'screenShader': {
          var sid = String(p.shader || ''), on = state.screenShaders.indexOf(sid) >= 0, want = p.state === 'on' ? true : p.state === 'off' ? false : !on;
          if (want && !on) state.screenShaders.push(sid);
          if (!want && on) state.screenShaders.splice(state.screenShaders.indexOf(sid), 1);
          break;
        }
        case 'setOutline': {
          var ow = resolveVal(p.width), oc = resolveStr(p.color || '#000000');
          for (i = 0; i < list.length; i++) list[i].outline = ow > 0 ? { width: ow, color: oc } : { width: 0 };
          break;
        }
        case 'fileRead': { var nfr = nativeFS(), tx = null; try { tx = nfr ? nfr.fileRead(resolveStr(p.path)) : null; } catch (e) { quiet(e); } writeVar(p.variable, tx == null ? '' : tx); break; }
        case 'fileReadB64': { var nfb = nativeFS(), tb = null; try { tb = nfb ? nfb.fileReadB64(resolveStr(p.path)) : null; } catch (e) { quiet(e); } writeVar(p.variable, tb == null ? '' : tb); break; }
        case 'fileWrite': { var nfw = nativeFS(); try { if (nfw) nfw.fileWrite(resolveStr(p.path), String(readVar(p.source) == null ? '' : readVar(p.source))); } catch (e) { quiet(e); } break; }
        case 'fileDelete': { var nfd = nativeFS(); try { if (nfd && nfd.fileDelete) nfd.fileDelete(resolveStr(p.path)); } catch (e) { quiet(e); } break; }
        case 'makeDir': { var nfm = nativeFS(); try { if (nfm) nfm.makeDir(resolveStr(p.path)); } catch (e) { quiet(e); } break; }
        case 'dirList': {
          var nfl = nativeFS(), lst = null;
          try { lst = nfl ? nfl.dirList(resolveStr(p.path)) : null; } catch (e) { lst = null; }
          lst = (lst || []).slice().sort(function (a, b) {
            var da = a.charAt(a.length - 1) === '/', db = b.charAt(b.length - 1) === '/';
            return da !== db ? (da ? -1 : 1) : a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0;
          });
          writeVar(p.variable, lst);
          break;
        }
        case 'initialFiles': writeVar(p.variable, (project.files || []).map(function (f) { return f.name; })); break;
        case 'initialFileRead': { var ift = initialFile(resolveStr(p.name)); writeVar(p.variable, ift == null ? '' : ift); break; }
        case 'jsonToVars': {
          var jp = String(p.prefix || '').replace(/^\$/, ''), jt = readVar(p.source), jo = null;
          try { jo = JSON.parse(String(jt == null ? '' : jt)); } catch (e) { jo = null; }
          clearVars(jp);
          if (jo && typeof jo === 'object' && !Array.isArray(jo)) for (var jk in jo) flattenInto(jp + '.' + jk, jo[jk]);
          state.globals[jp + '.__ok'] = jo ? 1 : 0;
          break;
        }
        case 'varsToJson': {
          var vp = String(p.prefix || '').replace(/^\$/, ''), vo = varsToObject(vp);
          delete vo.__ok;
          writeVar(p.variable, JSON.stringify(vo));
          break;
        }
        case 'varsClear': clearVars(String(p.prefix || '').replace(/^\$/, '')); break;
        case 'musicFromData': {
          var mid = resolveStr(p.id), mraw = String(readVar(p.source) || ''), mmime = resolveStr(p.mime || 'audio/mpeg');
          if (mid && mraw) {
            runtimeTracks[mid] = { id: 'rt_' + mid, name: mid, mime: mmime, data: mraw };
            delete nativeLoaded['rt_' + mid];
          }
          break;
        }
        case 'imageFromData': {
          var ikey = resolveStr(p.key), icv = imageFromB64(ikey, readVar(p.source));
          for (i = 0; i < list.length; i++) list[i].img = icv;
          break;
        }
        case 'shaderAmount': state.shaderAmt[String(p.shader || '')] = clamp(resolveVal(p.value), 0, 1); break;
        case 'saveVars': {
          var sv = {};
          String(p.vars || '').split(',').forEach(function (x) { x = x.trim().replace(/^\$/, ''); if (x && x in state.globals) sv[x] = state.globals[x]; });
          try { localStorage.setItem(saveKey(resolveStr(p.slot)), JSON.stringify({ globals: sv })); } catch (e) { quiet(e); }
          break;
        }
        case 'stopMusic': stopMusicTrack(); break;
        case 'stopGame': state.running = false; break;
        case 'jsCode': runCode(p.code, list.length ? list[0] : null, 'accion'); break;
        /* --- fase 3 --- */
        case 'objTimerCtl': {
          var otn = resolveStr(p.name);
          for (i = 0; i < list.length; i++) {
            var tms = list[i].timers; list[i].timersOn = true;
            if (p.action === 'remove') delete tms[otn];
            else if (p.action === 'reset') tms[otn] = { t: 0, paused: false };
            else { if (!tms[otn]) tms[otn] = { t: 0, paused: false }; tms[otn].paused = p.action === 'pause'; }
          }
          break;
        }
        case 'rotateToward':
          for (i = 0; i < list.length; i++) {
            var want = Math.atan2(resolveVal(p.y) - list[i].y, resolveVal(p.x) - list[i].x) * 180 / Math.PI;
            var rs = resolveVal(p.speed), cur = n(list[i].angle), diff = ((want - cur) % 360 + 540) % 360 - 180;
            list[i].angle = rs <= 0 ? want : cur + Math.max(-rs * state.dt, Math.min(rs * state.dt, diff));
          }
          break;
        case 'separate': {
          var others = objInstances(p.with);
          for (i = 0; i < list.length; i++) for (var so = 0; so < others.length; so++) {
            var A = list[i], B = others[so];
            if (A === B) continue;
            var sa = instSize(A), sb = instSize(B), ax = cX(A, sa), ay = cY(A, sa), bx2 = cX(B, sb), by2 = cY(B, sb);
            var ox = (sa.w + sb.w) / 2 - Math.abs(ax - bx2), oy = (sa.h + sb.h) / 2 - Math.abs(ay - by2);
            if (ox <= 0 || oy <= 0) continue;
            if (ox < oy) A.x += ax < bx2 ? -ox : ox; else A.y += ay < by2 ? -oy : oy;
          }
          break;
        }
        case 'animPause': for (i = 0; i < list.length; i++) list[i].animPaused = p.state === 'pause'; break;
        case 'animSpeed': for (i = 0; i < list.length; i++) list[i].animSpeed = Math.max(0, resolveVal(p.value)); break;
        case 'platSim': for (i = 0; i < list.length; i++) (list[i].simNext || (list[i].simNext = {}))[p.control || 'right'] = true; break;
        case 'setMoveSpeed': for (i = 0; i < list.length; i++) list[i].speedOverride = resolveVal(p.value); break;
        case 'setJumpForce': for (i = 0; i < list.length; i++) list[i].jumpOverride = resolveVal(p.value); break;
        case 'damage': case 'heal': case 'setHealth':
          for (i = 0; i < list.length; i++) {
            var hi = list[i], hv = resolveVal(p.value), hmax = hi.hpMax != null ? hi.hpMax : Infinity;
            if (hi.hp == null) { hi.hp = 100; hi.hpMax = 100; hmax = 100; }
            hi.hp = t === 'damage' ? Math.max(0, hi.hp - hv) : t === 'heal' ? Math.min(hmax, hi.hp + hv) : Math.max(0, Math.min(hmax, hv));
          }
          break;
        case 'flash': for (i = 0; i < list.length; i++) { list[i].flashT = Math.max(0, resolveVal(p.duration)); list[i].flashIv = 0.1; } break;
        case 'fireAngle': for (i = 0; i < list.length; i++) fireBullet(list[i], p.bullet, resolveVal(p.angle), resolveVal(p.speed)); break;
        case 'fireAt':
          for (i = 0; i < list.length; i++) {
            var fa = Math.atan2(resolveVal(p.y) - list[i].y, resolveVal(p.x) - list[i].x) * 180 / Math.PI;
            fireBullet(list[i], p.bullet, fa, resolveVal(p.speed));
          }
          break;
        case 'dragToggle': for (i = 0; i < list.length; i++) list[i].dragOff = p.state === 'off'; break;
        default: extAct(t, p, list); break;
      }
    }

    /* ---------------- instrucciones compiladas ----------------
       Las condiciones y acciones de variables, arrays y funciones (las que mas se repiten en bucles) se
       convierten una vez en funciones y se guardan en la instruccion (_f); el resto pasa por condResult/doAction. */
    function numOf(v) { return typeof v === 'number' ? (v - v === 0 ? v : 0) : typeof v === 'boolean' ? (v ? 1 : 0) : n(v); }
    function cmpFn(op) {
      switch (op) {
        case 'ne': return function (a, b) { return a !== b; };
        case 'lt': return function (a, b) { return a < b; };
        case 'le': return function (a, b) { return a <= b; };
        case 'gt': return function (a, b) { return a > b; };
        case 'ge': return function (a, b) { return a >= b; };
        default: return function (a, b) { return a === b; };
      }
    }
    function varGetter(raw) {
      var lv = lvalue(raw), name = lv.name;
      if (lv.idx === null) return function () { return varHome(name)[name]; };
      var fi = compileExpr(lv.idx) || ZERO;
      return function () { var a = varHome(name)[name]; return a && typeof a === 'object' ? a[numOf(fi()) | 0] : undefined; };
    }
    function compileCond(c) {
      var p = c.params || {};
      if (c.type === 'atStart') return function () { return state.frame <= 1; };
      if (c.type === 'exprTrue') {
        var fe = compileExpr(p.value == null ? '' : p.value);
        if (!fe) return null;
        return function () { var v = fe(); return !!v && v !== '0'; };
      }
      if (c.type === 'cmpNum') {
        var fa = compileExpr(p.a == null ? '' : p.a), fb = compileExpr(p.b == null ? '' : p.b), cmp = cmpFn(p.op);
        if (!fa || !fb) return null;
        return function () { return cmp(numOf(fa()), numOf(fb())); };
      }
      return null;
    }
    function compileAction(act) {
      var p = act.params || {}, f;
      switch (act.type) {
        case 'setVar': {
          var lv = lvalue(p.variable), name = lv.name, fv = compileExpr(p.value == null ? '' : p.value) || ZERO, op = p.op || 'set';
          var fi = lv.idx === null ? null : (compileExpr(lv.idx) || ZERO);
          var combine = op === 'set' ? null : op === 'add' ? function (a, b) { return a + b; } : op === 'sub' ? function (a, b) { return a - b; }
            : op === 'mul' ? function (a, b) { return a * b; } : op === 'div' ? function (a, b) { return b ? a / b : 0; }
            : op === 'mod' ? function (a, b) { return b ? a % b : 0; } : function (a, b) { return b; };
          return function () {
            var h = varHome(name), v = fv();
            if (fi === null) { h[name] = combine ? combine(numOf(h[name]), numOf(v)) : v; return; }
            var arr = h[name];
            if (!Array.isArray(arr)) arr = h[name] = [];
            var k = numOf(fi()) | 0;
            arr[k] = combine ? combine(numOf(arr[k]), numOf(v)) : v;
          };
        }
        case 'setGlobal': {
          if (lvalue(p.variable).idx !== null) return null;
          var sgn = lvalue(p.variable).name, sgv = p.value;
          return function () { varHome(sgn)[sgn] = resolveStr(sgv); };
        }
        case 'addGlobal': {
          if (lvalue(p.variable).idx !== null) return null;
          var agn = lvalue(p.variable).name, agf = compileExpr(p.value == null ? '' : p.value) || ZERO;
          return function () {
            var h = varHome(agn), v = agf();
            v = typeof v === 'number' ? (v - v === 0 ? v : 0) : typeof v === 'boolean' ? (v ? 1 : 0) : n(v);
            h[agn] = n(h[agn]) + v;
          };
        }
        case 'localVar': {
          var ln = lvalue(p.variable).name, lf = compileExpr(p.value == null ? '' : p.value) || ZERO;
          return function () { (frames.length ? frames[frames.length - 1].vars : state.globals)[ln] = lf(); };
        }
        case 'arrayPushNum': {
          var an = lvalue(p.variable).name, af = compileExpr(p.value == null ? '' : p.value) || ZERO;
          if (lvalue(p.variable).idx !== null) return null;
          return function () { var h = varHome(an), a = h[an]; if (!Array.isArray(a)) a = h[an] = []; a.push(af()); };
        }
        case 'arrayResize': {
          if (lvalue(p.variable).idx !== null) return null;
          var rnm = lvalue(p.variable).name, rf = compileExpr(p.size == null ? '' : p.size) || ZERO;
          return function () {
            var h = varHome(rnm), a = h[rnm], k = Math.max(0, numOf(rf()) | 0);
            if (!Array.isArray(a)) a = h[rnm] = [];
            if (k < a.length) a.length = k; else while (a.length < k) a.push(0);
          };
        }
        case 'callFunction': {
          var fname = String(p.name || '').trim(), fargs = compileExpr('[' + (p.args == null ? '' : p.args) + ']');
          if (!fargs) return null;
          var set = p.result ? lvalue(p.result) : null;
          if (set && set.idx !== null) return null;
          return function () { var r = callFn(fname, fargs()); if (set) varHome(set.name)[set.name] = r; };
        }
        case 'returnValue': {
          var rv = compileExpr(p.value == null ? '' : p.value) || ZERO;
          return function () { if (frames.length) frames[frames.length - 1].ret = rv(); flow = FLOW_RETURN; };
        }
        case 'breakLoop': return function () { flow = FLOW_BREAK; };
      }
      return f || null;
    }

    /* ---------------- funciones compiladas a JavaScript ----------------
       Una funcion de eventos cuyas instrucciones son todas de variables, arrays, comparaciones, bucles y
       llamadas (sin objetos ni textos con plantilla) se traduce entera a una funcion de JS: sus variables
       locales pasan a ser variables de JS, "Repetir" y "Mientras" son bucles for, "Salir del bucle" es break
       y "Devolver" es return. Va decenas de veces mas rapido que interpretarla (el bot de ajedrez la usa).
       Si algo no se puede traducir, la funcion se sigue interpretando como siempre. */
    var FN = {}, jsDepth = { d: 0 };
    function rtResize(a, k) { if (!Array.isArray(a)) a = []; k = Math.max(0, Math.min(1000000, k | 0)); if (k < a.length) a.length = k; else while (a.length < k) a.push(0); return a; }
    function rtFill(k, v) { k = Math.max(0, Math.min(1000000, k | 0)); var a = new Array(k); for (var i = 0; i < k; i++) a[i] = v; return a; }
    function rtSort(a, desc) {
      if (Array.isArray(a)) a.sort(function (x, y) {
        var r = typeof x === 'number' && typeof y === 'number' ? x - y : String(x) < String(y) ? -1 : String(x) > String(y) ? 1 : 0;
        return desc ? -r : r;
      });
    }
    function rtAppend(d, src) { if (!Array.isArray(d)) d = []; if (Array.isArray(src)) for (var i = 0; i < src.length; i++) d.push(src[i]); return d; }
    var JS_ACTS = { setVar: 1, localVar: 1, arrayPushNum: 1, arrayResize: 1, arrayFill: 1, arraySort: 1, arrayAppend: 1, arrayCopy: 1,
                    arrayPop: 1, arrayRemoveAt: 1, arrayInsertAt: 1, callFunction: 1, returnValue: 1, breakLoop: 1 };
    function compileFunctionJS(node) {
      var cx = { loc: {}, n: 0, obj: false }, order = [];
      function addLocal(nm) { if (nm && !cx.loc[nm]) { cx.loc[nm] = 'L' + order.length; order.push(nm); } }
      node.pnames.forEach(addLocal);
      (function collect(nd) {
        var ev = nd.es.ref;
        if (ev['var'] && ev.type === 'repeat') addLocal(lvalue(ev['var']).name);
        (ev.actions || []).forEach(function (a) { if (a.enabled !== false && a.type === 'localVar') addLocal(lvalue(a.params && a.params.variable).name); });
        nd.kids.forEach(collect);
      })(node);
      function ex(raw) { return '(' + translateExpr(String(raw == null || String(raw).trim() === '' ? '0' : raw), cx) + ')'; }
      function target(raw) { var lv = lvalue(raw); return { base: cx.loc[lv.name] || 'G[' + JSON.stringify(lv.name) + ']', idx: lv.idx }; }
      function arrOf(raw) { var t = target(raw); if (t.idx !== null) throw 0; return t.base; }
      function condJS(c) {
        var p = c.params || {}, js;
        if (c.sub) throw 0;
        if (c.type === 'exprTrue') js = 'TV' + ex(p.value);
        else if (c.type === 'cmpNum') {
          var ops = { eq: '===', ne: '!==', lt: '<', le: '<=', gt: '>', ge: '>=' };
          js = '(N' + ex(p.a) + ' ' + (ops[p.op] || '===') + ' N' + ex(p.b) + ')';
        } else throw 0;
        return c.invert ? '!' + js : js;
      }
      function actJS(a, inLoop) {
        var p = a.params || {}, t, v;
        switch (a.type) {
          case 'setVar': {
            t = target(p.variable); v = ex(p.value);
            var op = p.op || 'set', comb = function (old) {
              return op === 'add' ? 'N(' + old + ') + N' + v : op === 'sub' ? 'N(' + old + ') - N' + v : op === 'mul' ? 'N(' + old + ') * N' + v
                : op === 'div' ? '(N' + v + ' ? N(' + old + ') / N' + v + ' : 0)' : op === 'mod' ? '(N' + v + ' ? N(' + old + ') % N' + v + ' : 0)' : v;
            };
            if (t.idx === null) return t.base + ' = ' + (op === 'set' ? v : comb(t.base)) + ';';
            return '{ var a$ = ' + t.base + '; if (!Array.isArray(a$)) a$ = ' + t.base + ' = []; var i$ = ' + ex(t.idx) + ' | 0; a$[i$] = ' + (op === 'set' ? v : comb('a$[i$]')) + '; }';
          }
          case 'localVar': return cx.loc[lvalue(p.variable).name] + ' = ' + ex(p.value) + ';';
          case 'arrayPushNum': t = arrOf(p.variable); return '(Array.isArray(' + t + ') ? ' + t + ' : (' + t + ' = [])).push' + ex(p.value) + ';';
          case 'arrayResize': t = arrOf(p.variable); return t + ' = RZ(' + t + ', N' + ex(p.size) + ');';
          case 'arrayFill': t = arrOf(p.variable); return t + ' = FILL(N' + ex(p.size) + ', ' + ex(p.value) + ');';
          case 'arraySort': return 'SORT(' + arrOf(p.variable) + ', ' + (p.order === 'desc') + ');';
          case 'arrayAppend': t = arrOf(p.to); return t + ' = APP(' + t + ', ' + arrOf(p.from) + ');';
          case 'arrayCopy': t = arrOf(p.to); v = arrOf(p.from); return t + ' = Array.isArray(' + v + ') ? ' + v + '.slice() : [];';
          case 'arrayPop': t = arrOf(p.variable); return 'if (Array.isArray(' + t + ')) ' + t + '.pop();';
          case 'arrayRemoveAt': t = arrOf(p.variable); return '{ var i$ = ' + ex(p.index) + ' | 0; if (Array.isArray(' + t + ') && i$ >= 0 && i$ < ' + t + '.length) ' + t + '.splice(i$, 1); }';
          case 'arrayInsertAt': t = arrOf(p.variable); return '{ if (!Array.isArray(' + t + ')) ' + t + ' = []; var i$ = Math.max(0, Math.min(' + t + '.length, ' + ex(p.index) + ' | 0)); ' + t + '.splice(i$, 0, ' + ex(p.value) + '); }';
          case 'callFunction': {
            var nm = String(p.name || '').trim();
            if (!fnTable[nm]) throw 0;
            var callJs = 'FN[' + JSON.stringify(nm) + '](' + (p.args == null || String(p.args).trim() === '' ? '' : translateExpr(String(p.args), cx)) + ')';
            if (!p.result) return callJs + ';';
            t = target(p.result); if (t.idx !== null) throw 0;
            return t.base + ' = ' + callJs + ';';
          }
          case 'returnValue': return 'return ' + ex(p.value) + ';';
          case 'breakLoop': return inLoop ? 'break;' : 'return 0;';
        }
        throw 0;
      }
      function evJS(nd, inLoop, asBody) {
        var ev = nd.es.ref, tp = asBody ? '' : ev.type || '', i, conds = [], body = '';
        if (ev.enabled === false) return '';
        if (tp && tp !== 'repeat' && tp !== 'while') throw 0;
        (ev.conditions || []).forEach(function (c) { if (c.enabled !== false) conds.push(condJS(c)); });
        var loop = tp === 'repeat' || tp === 'while' || inLoop;
        (ev.actions || []).forEach(function (a) { if (a.enabled === false) return; if (!JS_ACTS[a.type]) throw 0; body += actJS(a, loop) + '\n'; });
        for (i = 0; i < nd.kids.length; i++) body += evJS(nd.kids[i], loop);
        var inner = conds.length ? 'if (' + conds.join(' && ') + ') {\n' + body + '}\n' : '{\n' + body + '}\n';
        var k = 'k' + (cx.n++) + '$';
        if (tp === 'repeat') {
          var cv = ev['var'] ? cx.loc[lvalue(ev['var']).name] : '';
          return 'for (var ' + k + ' = 0, n' + k + ' = Math.max(0, Math.min(1000000, Math.floor(N' + ex(ev.count) + '))); ' + k + ' < n' + k + '; ' + k + '++) {\n' +
            (cv ? cv + ' = ' + k + ';\n' : '') + inner + '}\n';
        }
        if (tp === 'while') {
          var wc = [];
          (ev.whileConds || []).forEach(function (c) { if (c.enabled !== false) wc.push(condJS(c)); });
          return 'for (var ' + k + ' = 0; ' + k + ' < 100000; ' + k + '++) {\n' + (wc.length ? 'if (!(' + wc.join(' && ') + ')) break;\n' : '') + inner + '}\n';
        }
        return inner;
      }
      var src;
      try {
        var params = node.pnames.map(function (nm) { return cx.loc[nm]; }), others = order.slice(node.pnames.length).map(function (nm) { return cx.loc[nm] + ' = 0'; });
        var body = evJS(node, false, true);
        if (cx.obj) return null;
        src = '"use strict";\nfunction body(' + params.join(', ') + ') {\n' + (others.length ? 'var ' + others.join(', ') + ';\n' : '') + body + 'return 0;\n}\n' +
          'return function (' + params.join(', ') + ') { if (D.d >= 400) return 0; D.d++; var r = body(' + params.join(', ') + '); D.d--; return r; };';
      } catch (e) {
        if (e !== 0) console.warn('Source 2D: la funcion ' + node.es.ref.name + ' se interpreta (' + (e && e.message) + ')');
        return null;
      }
      try {
        // TV (verdadero o no) no puede llamarse T: T("clave") es la traduccion de las expresiones (FN_NAMES)
        return new Function('G', 'FN', 'N', 'TV', 'O', 'C', 'D', 'RZ', 'FILL', 'SORT', 'APP', FN_NAMES.join(','), src)
          .apply(null, [state.globals, FN, numOf, function (v) { return !!v && v !== '0'; }, exprObj, cursorValue, jsDepth, rtResize, rtFill, rtSort, rtAppend].concat(FN_VALS));
      } catch (e2) {
        console.warn('Source 2D: no se pudo compilar la funcion ' + node.es.ref.name + ': ' + e2.message);
        return null;
      }
    }
    /* al cargar la escena: FN[nombre] llama a la version compilada o, si no la hay, al interprete */
    function linkFunctions() {
      Object.keys(FN).forEach(function (k) { delete FN[k]; });
      Object.keys(fnTable).forEach(function (name) {
        FN[name] = function () { return callFn(name, Array.prototype.slice.call(arguments)); };
      });
      Object.keys(fnTable).forEach(function (name) {
        var node = fnTable[name];
        node.js = compileFunctionJS(node);
        if (node.js) FN[name] = node.js;
      });
    }

    /* ---------------- eventos ---------------- */
    /* La lista plana de eventos (con .level) se convierte en arbol: cada evento con sus subeventos. */
    function buildEventTree(list) {
      var root = [], stack = [{ lvl: -1, kids: root }];
      list.forEach(function (es) {
        var l = es.ref.level || 0;
        while (stack.length > 1 && stack[stack.length - 1].lvl >= l) stack.pop();
        var node = { es: es, kids: [] };
        stack[stack.length - 1].kids.push(node);
        stack.push({ lvl: l, kids: node.kids });
      });
      return root;
    }
    function clonePicks(p) {
      var o = {};
      for (var k in p) { var set = {}, src = p[k]; for (var id in src) set[id] = true; o[k] = set; }
      return o;
    }
    var STOP = 1;
    function stopped() { return state.sceneChanged || state.ended || !state.running; }
    /* Evalua una condicion. Si es verdadera (y no invertida) deja en picks las instancias que la cumplen. */
    function evalCond(cond, picks, es, path) {
      if (cond.enabled === false) return true;
      var t = cond.type, r, i, cf, subs = cond.sub || [];
      if (t === 'or') {
        var union = {};
        r = false;
        for (i = 0; i < subs.length; i++) {
          if (subs[i].enabled === false) continue;
          var po = clonePicks(picks);
          if (evalCond(subs[i], po, es, path + '.' + i)) {
            r = true;
            for (var ko in po) { if (!union[ko]) union[ko] = {}; for (var io in po[ko]) union[ko][io] = true; }
          }
        }
        if (cond.invert) return !r;
        if (r) for (var ku in union) picks[ku] = union[ku];
        return r;
      }
      if (t === 'and' || t === 'not') {
        var pa = clonePicks(picks), all = true;
        for (i = 0; i < subs.length; i++) if (!evalCond(subs[i], pa, es, path + '.' + i)) { all = false; break; }
        r = t === 'not' ? !all : all;
        if (cond.invert) return !r;
        if (r && t === 'and') for (var ka in pa) picks[ka] = pa[ka];
        return r;
      }
      if (t === 'every') {
        es.acc[path] = (es.acc[path] || 0) + state.dt;
        var period = Math.max(0.0001, n(cond.params && cond.params.seconds, 1));
        r = false;
        if (es.acc[path] >= period) { es.acc[path] -= period; r = true; }
      } else if (t === 'triggerOnce') {
        /* verdadera solo el primer fotograma en que se llega aqui seguido */
        var prev = es.once[path];
        r = prev !== state.frame - 1 && prev !== state.frame;
        es.once[path] = state.frame;
      } else if ((cf = cond._f !== undefined ? cond._f : (cond._f = compileCond(cond)))) {
        r = cf();
      } else {
        var fresh = {}, oid = cond.params && cond.params.object, cands = null;
        condScope = picks;
        try {
          if (cond.invert && oid) cands = objInstances(oid);
          r = condResult(cond, fresh);
        } finally { condScope = null; }
        if (cond.invert && oid && fresh[oid]) {
          /* condicion de objeto invertida: se quedan las instancias que NO la cumplen */
          var rest = {}, any = false;
          for (i = 0; i < cands.length; i++) if (!fresh[oid][cands[i].id]) { rest[cands[i].id] = true; any = true; }
          if (any) picks[oid] = rest;
          return any;
        }
        if (r && !cond.invert) for (var kf in fresh) picks[kf] = fresh[kf];
      }
      return cond.invert ? !r : r;
    }
    function evalConds(list, picks, es, prefix) {
      for (var i = 0; i < list.length; i++) if (!evalCond(list[i], picks, es, prefix + i)) return false;
      return true;
    }
    function runBody(node, picks) {
      var ev = node.es.ref;
      exprPicks = picks;
      if (!evalConds(ev.conditions || [], picks, node.es, 'c')) return 0;
      return runActs(node, picks, 0);
    }
    /* "Esperar N segundos": las acciones que siguen (y los subeventos) se apuntan y se hacen despues, con los mismos
       objetos elegidos. Dentro de una funcion no espera (una funcion devuelve su valor en el momento). */
    function runWaits(dt) {
      if (!state.waits.length) return;
      var due = [], keep = [];
      for (var i = 0; i < state.waits.length; i++) { var w = state.waits[i]; w.t -= dt; (w.t <= 0 ? due : keep).push(w); }
      state.waits = keep;
      for (var j = 0; j < due.length && !state.sceneChanged; j++) {
        runActs(due[j].node, due[j].picks, due[j].from);
        flow = 0;
      }
    }
    function runActs(node, picks, from) {
      var ev = node.es.ref;
      exprPicks = picks;
      var acts = ev.actions || [];
      for (var a = from; a < acts.length; a++) {
        var act = acts[a];
        if (act.enabled === false) continue;
        if (act.type === 'wait') {
          if (frames.length) continue;
          state.waits.push({ t: Math.max(0, resolveVal((act.params || {}).seconds)), node: node, from: a + 1, picks: clonePicks(picks) });
          return 0;
        }
        var cf = act._f;
        if (cf === undefined) cf = act._f = compileAction(act);
        if (cf) cf(); else doAction(act, picks);
        if (flow) return flow;               /* salir del bucle o devolver valor */
        if (stopped()) return STOP;
      }
      return node.kids.length ? runNodes(node.kids, picks) : 0;
    }
    /* Via rapida: un evento cuyas condiciones y acciones estan todas compiladas no elige objetos, asi que
       no hace falta copiar la seleccion ni pasar por evalCond. node.fast guarda las listas ya preparadas. */
    function fastPlan(node, asBody) {
      var ev = node.es.ref, t = asBody ? '' : ev.type || '';
      if (t && t !== 'repeat' && t !== 'while') return null;
      var cs = [], as = [], lists = [ev.conditions || []], li, i;
      if (t === 'while') lists.push(ev.whileConds || []);
      for (li = 0; li < lists.length; li++) {
        var out = [];
        for (i = 0; i < lists[li].length; i++) {
          var c = lists[li][i];
          if (c.enabled === false) continue;
          if (c.sub) return null;
          var f = c._f !== undefined ? c._f : (c._f = compileCond(c));
          if (!f) return null;
          out.push(c.invert ? function (g) { return function () { return !g(); }; }(f) : f);
        }
        if (li === 0) cs = out; else node.fastWhile = out;
      }
      var acts = ev.actions || [];
      for (i = 0; i < acts.length; i++) {
        if (acts[i].enabled === false) continue;
        var af = acts[i]._f !== undefined ? acts[i]._f : (acts[i]._f = compileAction(acts[i]));
        if (!af) return null;
        as.push(af);
      }
      return { c: cs, a: as };
    }
    function runFast(node, fp, picks) {
      var cs = fp.c, as = fp.a, i;
      exprPicks = picks;
      for (i = 0; i < cs.length; i++) if (!cs[i]()) return 0;
      for (i = 0; i < as.length; i++) {
        as[i]();
        if (flow) return flow;
        if (stopped()) return STOP;
      }
      return node.kids.length ? runNodes(node.kids, picks) : 0;
    }
    /* resultado de una vuelta de bucle: true si hay que dejar el bucle (y r != 0 se propaga) */
    function loopExit(r) {
      if (r === FLOW_BREAK) { flow = 0; return true; }
      return r !== 0;
    }
    function runNode(node, parentPicks) {
      var ev = node.es.ref, k, r;
      if (ev.enabled === false || ev.type === 'function') return 0;
      /* un bucle con "Al empezar la escena" no da vueltas despues del primer fotograma (la condicion no cambia en el bucle) */
      if (node.atStart === undefined) node.atStart = (ev.type === 'repeat' || ev.type === 'while') && (ev.conditions || []).some(function (c) { return c.type === 'atStart' && c.enabled !== false && !c.invert && !c.sub; });
      if (node.atStart && state.frame > 1) return 0;
      var fp = node.fast;
      if (fp === undefined) fp = node.fast = fastPlan(node);
      if (fp) {
        if (ev.type === 'repeat') {
          var fcount = Math.max(0, Math.min(1000000, Math.floor(resolveVal(ev.count))));
          var fcv = ev['var'] ? lvalue(ev['var']).name : '', fhome = fcv ? (frames.length ? frames[frames.length - 1].vars : state.globals) : null;
          for (k = 0; k < fcount; k++) {
            if (fhome) fhome[fcv] = k;
            r = runFast(node, fp, parentPicks);
            if (r && loopExit(r)) return r === FLOW_BREAK ? 0 : r;
          }
          return 0;
        }
        if (ev.type === 'while') {
          var wc = node.fastWhile || [];
          for (k = 0; k < 100000; k++) {
            for (var q = 0; q < wc.length; q++) if (!wc[q]()) return 0;
            r = runFast(node, fp, parentPicks);
            if (r && loopExit(r)) return r === FLOW_BREAK ? 0 : r;
          }
          return 0;
        }
        return runFast(node, fp, parentPicks);
      }
      if (ev.type === 'repeat') {
        var count = Math.max(0, Math.min(1000000, Math.floor(resolveVal(ev.count))));
        /* contador opcional: variable local si se esta dentro de una funcion, si no global */
        var cv = ev['var'] ? lvalue(ev['var']).name : '', home = cv ? (frames.length ? frames[frames.length - 1].vars : state.globals) : null;
        for (k = 0; k < count; k++) {
          if (home) home[cv] = k;
          r = runBody(node, clonePicks(parentPicks));
          if (r && loopExit(r)) return r === FLOW_BREAK ? 0 : r;
        }
        return 0;
      }
      if (ev.type === 'forEach') {
        condScope = parentPicks;
        var insts = ev.object ? objInstances(ev.object) : [];
        condScope = null;
        for (k = 0; k < insts.length; k++) {
          if (state.instances.indexOf(insts[k]) < 0) continue;      /* eliminada en una vuelta anterior */
          var pf = clonePicks(parentPicks);
          pf[ev.object] = {}; pf[ev.object][insts[k].id] = true;
          r = runBody(node, pf);
          if (r && loopExit(r)) return r === FLOW_BREAK ? 0 : r;
        }
        return 0;
      }
      if (ev.type === 'while') {
        for (k = 0; k < 100000; k++) {                              /* tope para no colgar el juego */
          var pw = clonePicks(parentPicks);
          if (!evalConds(ev.whileConds || [], pw, node.es, 'w')) break;
          r = runBody(node, pw);
          if (r && loopExit(r)) return r === FLOW_BREAK ? 0 : r;
        }
        return 0;
      }
      return runBody(node, clonePicks(parentPicks));
    }
    function runNodes(nodes, picks) {
      for (var i = 0; i < nodes.length; i++) { var r = runNode(nodes[i], picks); if (r) return r; }
      return 0;
    }
    var folderNames = null;
    function runEvents() {
      var nodes = state.eventTree || [];
      jsDepth.d = 0;
      if (PROF && !folderNames) {                  /* --profile: tiempo de cada carpeta de eventos */
        folderNames = {};
        (project.events || []).concat.apply(project.events || [], (project.scenes || []).map(function (s) { return s.events || []; }))
          .forEach(function (e) { if (e.kind === 'folder') folderNames[e.id] = e.name; });
      }
      for (var i = 0; i < nodes.length; i++) {
        var t0 = PROF ? profT() : 0;
        var r = runNode(nodes[i], {});
        if (PROF) profAdd('    carpeta ' + (folderNames[nodes[i].es.ref.folder] || '(sin carpeta)'), t0);
        if (r === STOP) return;
        flow = 0;                            /* "salir del bucle" fuera de un bucle solo corta su evento */
      }
    }

    /* ---------------- shaders y efectos ---------------- */
    function applyFilterDraw(drawFn, filter) {
      if (!filter) { drawFn(); return; }
      ctx.save();
      try { ctx.filter = filter; } catch (e) { quiet(e); }
      drawFn();
      ctx.restore();
    }
    var noiseCanvas = null;
    function getNoise() {
      if (!noiseCanvas) {
        noiseCanvas = S2D.image(128, 128);
        var d = noiseCanvas.data;
        for (var i = 0; i < d.length; i += 4) {
          var v = Math.random() * 255;
          d[i] = d[i + 1] = d[i + 2] = v;
          d[i + 3] = 30;
        }
        noiseCanvas.update();
      }
      return noiseCanvas;
    }
    var vignetteCache = null;
    /* --profile (jugador nativo): ms medios por parte del fotograma, cada 120 fotogramas */
    var PROF = N.profile ? {} : null, profN = 0;
    function profT() { return PROF ? N.hrtime() : 0; }
    function profAdd(k, t0) { if (PROF) PROF[k] = (PROF[k] || 0) + N.hrtime() - t0; }
    function profFrame() {
      if (!PROF || ++profN < 120) return;
      var keys = Object.keys(PROF).sort(function (a, b) { return PROF[b] - PROF[a]; });
      console.log('perfil (ms por fotograma): ' + keys.map(function (k) { return k + ' ' + (PROF[k] / profN).toFixed(2); }).join(' | ') +
                  ' | particulas ' + particles.length);
      PROF = {}; profN = 0;
    }
    function applySceneEffects(effects) {
      if (!effects || !effects.length) return;
      for (var i = 0; i < effects.length; i++) {
        var fx = effects[i], type = fx.type, amt = n(fx.amount, 50);
        if (type === 'filter') {
          sctx.setTransform(1, 0, 0, 1, 0, 0);
          sctx.clearRect(0, 0, VW, VH);
          sctx.drawImage(ctx.canvas, 0, 0);
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.clearRect(0, 0, VW, VH);
          ctx.filter = fx.code || 'none';
          ctx.drawImage(scratch, 0, 0);
          ctx.filter = 'none';
        } else if (type === 'scanlines') {
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.fillStyle = 'rgba(0,0,0,' + (amt / 300) + ')';
          var gap = Math.max(2, Math.round(6 - amt / 25));
          for (var y = 0; y < VH; y += gap) ctx.fillRect(0, y, VW, 1);
        } else if (type === 'vignette') {
          /* el degradado se pinta una vez en un lienzo (por intensidad) y luego solo se compone */
          if (!vignetteCache || vignetteCache.amt !== amt) {
            var vc = vignetteCache ? vignetteCache.cv : S2D.surface(VW, VH);
            var vg = vc.ctx, grad = vg.createRadialGradient(VW / 2, VH / 2, VH / 3, VW / 2, VH / 2, VH * 0.85);
            vg.clearRect(0, 0, VW, VH);
            grad.addColorStop(0, 'rgba(0,0,0,0)');
            grad.addColorStop(1, 'rgba(0,0,0,' + (amt / 100) + ')');
            vg.fillStyle = grad; vg.fillRect(0, 0, VW, VH);
            vignetteCache = { amt: amt, cv: vc };
          }
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.drawImage(vignetteCache.cv, 0, 0);
        } else if (type === 'noise') {
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.globalAlpha = amt / 150;
          ctx.drawImage(getNoise(), 0, 0, VW, VH);
          ctx.globalAlpha = 1;
        } else if (type === 'pixelate') {
          var size = Math.max(2, Math.round(amt / 6));
          sctx.setTransform(1, 0, 0, 1, 0, 0);
          sctx.clearRect(0, 0, VW, VH);
          sctx.drawImage(ctx.canvas, 0, 0, VW / size, VH / size);
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.clearRect(0, 0, VW, VH);
          ctx.imageSmoothingEnabled = false;
          ctx.drawImage(scratch, 0, 0, VW / size, VH / size, 0, 0, VW, VH);
        } else if (type === 'wave') {
          sctx.setTransform(1, 0, 0, 1, 0, 0);
          sctx.clearRect(0, 0, VW, VH);
          sctx.drawImage(ctx.canvas, 0, 0);
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.clearRect(0, 0, VW, VH);
          var amp = amt / 3, slice = 6;
          for (var yy = 0; yy < VH; yy += slice) {
            var off = Math.sin((yy / 22) + state.time * 4) * amp;
            ctx.drawImage(scratch, 0, yy, VW, slice, off, yy, VW, slice);
          }
        } else if (type === 'chroma') {
          sctx.setTransform(1, 0, 0, 1, 0, 0);
          sctx.clearRect(0, 0, VW, VH);
          sctx.drawImage(ctx.canvas, 0, 0);
          ctx.setTransform(1, 0, 0, 1, 0, 0);
          ctx.clearRect(0, 0, VW, VH);
          ctx.globalCompositeOperation = 'lighter';
          var d = amt / 22;
          ctx.globalAlpha = 0.6;
          ctx.drawImage(scratch, -d, 0);
          ctx.drawImage(scratch, d, 0);
          ctx.globalAlpha = 0.35;
          ctx.drawImage(scratch, 0, 0);
          ctx.globalAlpha = 1;
          ctx.globalCompositeOperation = 'source-over';
        }
      }
    }
    function runSceneShader(code) {
      if (!code) return;
      var fn = compile(code);
      if (!fn) return;
      try {
        fn({ ctx: ctx, canvas: ctx.canvas, w: VW, h: VH, time: state.time, dt: state.dt, Math: Math, camera: state.camera });
      } catch (e) {
        console.warn('Source 2D [shader]: ' + e.message);
      }
    }

    /* ---------------- dibujo ---------------- */
    /* rectangulo de pantalla con lo que se dibuja en una capa con shader (para procesar solo esa zona) */
    var drawBox = null;
    function trackBox(wx0, wy0, wx1, wy1) {
      if (!drawBox) return;
      var m = ctx.m || [1, 0, 0, 1, 0, 0];
      var X0 = m[0] * wx0 + m[4], X1 = m[0] * wx1 + m[4], Y0 = m[3] * wy0 + m[5], Y1 = m[3] * wy1 + m[5];
      if (X0 < drawBox[0]) drawBox[0] = X0; if (Y0 < drawBox[1]) drawBox[1] = Y0;
      if (X1 > drawBox[2]) drawBox[2] = X1; if (Y1 > drawBox[3]) drawBox[3] = Y1;
    }
    /* mapas de tiles de una capa (tm.layer; sin capa = la principal), solo las casillas que se ven con su parallax */
    /* Los mapas de tiles se dibujan por trozos (de unos 256 px) ya pintados en una imagen: unos pocos dibujos por capa
       en vez de uno por tile. Un trozo se rehace si cambia algun tile (tileRev); se guardan los ultimos CHUNK_MAX. */
    var tileRev = 0, chunkList = [], CHUNK_MAX = 240;
    function tileChunk(tm, ts, cx, cy, K) {
      var map = tm._chunks || (tm._chunks = {}), key = cx + ',' + cy, e = map[key], T = ts.tileSize;
      if (e && e.rev === tileRev && e.ts === ts) { e.used = state.frame; return e; }
      var cv = e && e.cv ? e.cv : null, g = null, any = false, r, c;
      for (r = cy * K; r < Math.min(tm.rows, (cy + 1) * K) && !any; r++)
        for (c = cx * K; c < Math.min(tm.cols, (cx + 1) * K); c++) { var i0 = tm.data[r * tm.cols + c]; if (i0 != null && i0 >= 0) { any = true; break; } }
      if (!any && cv) { cv.free(); cv = null; }
      if (any) {
        if (!cv) cv = S2D.surface(K * T, K * T);
        g = cv.ctx;
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.clearRect(0, 0, K * T, K * T);
        for (r = cy * K; r < Math.min(tm.rows, (cy + 1) * K); r++)
          for (c = cx * K; c < Math.min(tm.cols, (cx + 1) * K); c++) {
            var idx = tm.data[r * tm.cols + c];
            if (idx == null || idx < 0) continue;
            var tcv = tileFrame(tm.tilesetId, idx);
            if (tcv) g.drawImage(tcv, (c - cx * K) * T, (r - cy * K) * T);
          }
      }
      if (!e) chunkList.push({ tm: tm, key: key });
      e = map[key] = { cv: any ? cv : null, rev: tileRev, ts: ts, used: state.frame };
      if (chunkList.length > CHUNK_MAX) {
        chunkList.sort(function (a, b) { var ea = a.tm._chunks[a.key], eb = b.tm._chunks[b.key]; return (ea ? ea.used : -1) - (eb ? eb.used : -1); });
        while (chunkList.length > CHUNK_MAX * 0.75) {
          var old = chunkList.shift(), oe = old.tm._chunks && old.tm._chunks[old.key];
          if (oe) { if (oe.cv) oe.cv.free(); delete old.tm._chunks[old.key]; }
        }
      }
      return e;
    }
    function drawTilemaps(lid, par) {
      var sc = scenes[state.sceneId];
      var tms = (sc && sc.tilemaps) || [];
      var cam = state.camera, vx = cam.x * par, vy = cam.y * par;
      for (var t = 0; t < tms.length; t++) {
        var tm = tms[t], ts = tilesets[tm.tilesetId];
        if (!ts || !tm.data) continue;
        var tl = tm.layer && layers[tm.layer] ? tm.layer : 'base';
        if (tl !== lid) continue;
        var T = ts.tileSize;
        var c0 = Math.max(0, Math.floor((vx - tm.x) / T)), c1 = Math.min(tm.cols, Math.ceil((vx + VW / cam.zoom - tm.x) / T));
        var r0 = Math.max(0, Math.floor((vy - tm.y) / T)), r1 = Math.min(tm.rows, Math.ceil((vy + VH / cam.zoom - tm.y) / T));
        var filter = tm.shader || null;
        if (filter) { ctx.save(); try { ctx.filter = filter; } catch (e) { quiet(e); } }
        var K = Math.max(4, Math.round(256 / T)), bx0 = 1e9, bx1 = -1e9, by0 = 1e9, by1 = -1e9;
        for (var cy = Math.floor(r0 / K); cy * K < r1; cy++) {
          for (var cx = Math.floor(c0 / K); cx * K < c1; cx++) {
            var ch = tileChunk(tm, ts, cx, cy, K);
            if (!ch.cv) continue;
            var px = tm.x + cx * K * T, py = tm.y + cy * K * T;
            ctx.drawImage(ch.cv, px, py);
            if (px < bx0) bx0 = px; if (py < by0) by0 = py; if (px + K * T > bx1) bx1 = px + K * T; if (py + K * T > by1) by1 = py + K * T;
          }
        }
        if (drawBox && bx1 > bx0) trackBox(bx0, by0, bx1, by1);
        if (filter) ctx.restore();
      }
    }


    /* ---------------- particulas ----------------
       Cuadrados que salen en abanico (angulo y dispersion en grados), con gravedad, se encogen y se desvanecen.
       color admite varios separados por comas (cada particula toma uno). glow: se suman a lo de debajo (brillo). */
    /* las particulas viven en arreglos de numeros (sin un objeto por particula: nada que recoger de la memoria) */
    var PMAX = 8000, PF = 10;
    var pf = new Float32Array(PMAX * PF), pc = new Uint8Array(PMAX * 4), pimg = new Int16Array(PMAX), pn = 0, pimgs = [];
    var particles = { get length() { return pn; } };
    function clearParticles() { pn = 0; }
    var pcolCache = {};
    function emitParticles(x, y, count, o) {
      var key = String(o.color), cols = pcolCache[key];
      if (!cols) cols = pcolCache[key] = key.split(',').map(function (c) { return c.trim(); }).filter(Boolean).map(hexRGB);
      if (!cols.length) cols = [[255, 255, 255]];
      var ang = n(o.angle), spr = n(o.spread), spd = n(o.speed), g = n(o.gravity), glow = o.glow ? 1 : 0, im = -1;
      if (o.image && sprites[o.image]) { im = pimgs.indexOf(o.image); if (im < 0) { im = pimgs.length; pimgs.push(o.image); } }
      var spin = n(o.spin, 0);
      for (var i = 0; i < count && pn < PMAX; i++) {
        var a = (ang + (Math.random() - 0.5) * spr) * Math.PI / 180, sp = spd * (0.35 + Math.random() * 0.65);
        var life = o.life * (0.6 + Math.random() * 0.4), rgb = cols[(Math.random() * cols.length) | 0], j = pn * PF, c = pn * 4;
        pf[j] = x; pf[j + 1] = y; pf[j + 2] = Math.cos(a) * sp; pf[j + 3] = Math.sin(a) * sp; pf[j + 4] = g;
        pf[j + 5] = life; pf[j + 6] = life; pf[j + 7] = o.size * (0.6 + Math.random() * 0.6);
        pc[c] = rgb[0]; pc[c + 1] = rgb[1]; pc[c + 2] = rgb[2]; pc[c + 3] = glow;
        pimg[pn] = im; pf[j + 8] = Math.random() * 360; pf[j + 9] = spin * (Math.random() < 0.5 ? -1 : 1);
        pn++;
      }
    }
    function updateParticles(dt) {
      var k = 0, drag = Math.pow(0.985, dt * 60);
      for (var i = 0; i < pn; i++) {
        var j = i * PF, life = pf[j + 5] - dt;
        if (life <= 0) continue;
        var vx = pf[j + 2] * drag, vy = (pf[j + 3] + pf[j + 4] * dt) * drag, o = k * PF;
        pf[o] = pf[j] + vx * dt; pf[o + 1] = pf[j + 1] + vy * dt; pf[o + 2] = vx; pf[o + 3] = vy; pf[o + 4] = pf[j + 4];
        pf[o + 5] = life; pf[o + 6] = pf[j + 6]; pf[o + 7] = pf[j + 7]; pf[o + 8] = pf[j + 8] + pf[j + 9] * dt; pf[o + 9] = pf[j + 9];
        if (k !== i) { var c = i * 4, d = k * 4; pc[d] = pc[c]; pc[d + 1] = pc[c + 1]; pc[d + 2] = pc[c + 2]; pc[d + 3] = pc[c + 3]; pimg[k] = pimg[i]; }
        k++;
      }
      pn = k;
    }
    function drawParticles() {
      if (!pn) return;
      var cam = state.camera, x0 = cam.x - 16, y0 = cam.y - 16, x1 = cam.x + VW / cam.zoom + 16, y1 = cam.y + VH / cam.zoom + 16;
      var fast = !!ctx.fillRectRGBA;
      ctx.save();
      for (var i = 0; i < pn; i++) {
        var j = i * PF, c = i * 4, x = pf[j], y = pf[j + 1];
        if (x < x0 || x > x1 || y < y0 || y > y1) continue;
        var f = pf[j + 5] / pf[j + 6], sz = Math.max(1, Math.round(pf[j + 7] * (0.4 + 0.6 * f))), al = Math.min(1, f * 1.4);
        var px0 = Math.round(x - sz / 2), py0 = Math.round(y - sz / 2);
        if (pimg[i] >= 0 && ctx.drawSprite) {                           /* particula con imagen: girando y desvaneciendose */
          var fr = spriteFrame(pimgs[pimg[i]], 0), psc = sz / Math.max(1, fr.width);
          ctx.globalCompositeOperation = pc[c + 3] ? 'lighter' : 'source-over';
          ctx.drawSprite(fr, x, y, pf[j + 8] * Math.PI / 180, psc, psc, al);
          ctx.globalCompositeOperation = 'source-over';
          continue;
        }
        if (fast) { ctx.fillRectRGBA(px0, py0, sz, sz, pc[c], pc[c + 1], pc[c + 2], al, pc[c + 3]); continue; }
        ctx.globalAlpha = al;
        ctx.globalCompositeOperation = pc[c + 3] ? 'lighter' : 'source-over';
        ctx.fillStyle = 'rgb(' + pc[c] + ',' + pc[c + 1] + ',' + pc[c + 2] + ')';
        ctx.fillRect(px0, py0, sz, sz);
      }
      ctx.restore();
    }

    /* sprite con tinte: cada pixel se multiplica por el color (como el tinte de GDevelop); se guarda en cache */
    var tintCache = {}, tintCount = 0;
    function tintedFrame(cv, key, t) {
      var k = key + '|' + t[0] + ',' + t[1] + ',' + t[2], c = tintCache[k];
      if (c) return c;
      if (++tintCount > 600) { tintCache = {}; tintCount = 1; }
      /* las imagenes de pixeles se leen directamente; un lienzo de la GPU se lee de vuelta (lento, raro) */
      var src = cv.data || cv.ctx.getImageData(0, 0, cv.width, cv.height).data;
      c = S2D.image(cv.width, cv.height, src);
      var d = c.data;
      for (var i = 0; i < d.length; i += 4) { d[i] = d[i] * t[0] / 255 | 0; d[i + 1] = d[i + 1] * t[1] / 255 | 0; d[i + 2] = d[i + 2] * t[2] / 255 | 0; }
      c.update();
      return (tintCache[k] = c);
    }
    /* "r;g;b", "#rrggbb" o "#rgb" -> [r, g, b] (enteros 0-255), o null si no se entiende */
    function parseRGB(v) {
      var s = String(v == null ? '' : v).trim(), m;
      if ((m = /^(-?[\d.]+)\s*[;,]\s*(-?[\d.]+)\s*[;,]\s*(-?[\d.]+)$/.exec(s))) return [m[1], m[2], m[3]].map(function (x) { return clamp(Math.round(+x), 0, 255); });
      if ((m = /^#?([0-9a-f]{6})$/i.exec(s))) return [0, 2, 4].map(function (o) { return parseInt(m[1].substr(o, 2), 16); });
      if ((m = /^#?([0-9a-f]{3})$/i.exec(s))) return [0, 1, 2].map(function (o) { return parseInt(m[1].charAt(o) + m[1].charAt(o), 16); });
      return null;
    }
    /* contorno (como el efecto Outline de GDevelop): obj.outline o el de la instancia (accion "Poner contorno"),
       en pixeles de pantalla, dibujado en 8 direcciones */
    var OL_DX = [-1, 0, 1, -1, 1, -1, 0, 1], OL_DY = [-1, -1, -1, 0, 0, 1, 1, 1];
    /* texto enriquecido: [color=#f55]rojo[/color], [wave]ondula[/wave], [shake]tiembla[/shake] */
    var richRe = /\[(\/?)(color|wave|shake)(=[^\]]*)?\]/;
    function richSegments(line) {
      var out = [], col = [], wave = 0, shake = 0, re = /\[(\/?)(color|wave|shake)(?:=([^\]]*))?\]/g, last = 0, m;
      while ((m = re.exec(line))) {
        if (m.index > last) out.push({ t: line.slice(last, m.index), c: col[col.length - 1], w: wave > 0, s: shake > 0 });
        if (m[2] === 'color') { if (m[1]) col.pop(); else col.push(m[3] || '#ffffff'); }
        else if (m[2] === 'wave') wave += m[1] ? -1 : 1;
        else shake += m[1] ? -1 : 1;
        last = re.lastIndex;
      }
      if (last < line.length) out.push({ t: line.slice(last), c: col[col.length - 1], w: wave > 0, s: shake > 0 });
      return out;
    }
    function drawRichLine(line, ly, baseCol, align) {
      var segs = richSegments(line), total = 0, i, j;
      for (i = 0; i < segs.length; i++) total += (segs[i].wd = ctx.measureText(segs[i].t).width);
      var x = align === 'center' ? -total / 2 : align === 'right' ? -total : 0, t = state.time || 0;
      ctx.textAlign = 'left';
      for (i = 0; i < segs.length; i++) {
        var sg = segs[i];
        ctx.fillStyle = sg.c || baseCol;
        if (!sg.w && !sg.s) { ctx.fillText(sg.t, x, ly); x += sg.wd; continue; }
        for (j = 0; j < sg.t.length; j++) {                       /* letra a letra */
          var ch = sg.t.charAt(j), cw = ctx.measureText(ch).width, dy = 0, dx = 0;
          if (sg.w) dy = Math.sin(t * 6 + (x + cw / 2) / 9) * 3;
          if (sg.s) { dx = Math.sin(t * 53 + j * 7.1) * 1.2; dy += Math.cos(t * 47 + j * 5.3) * 1.2; }
          ctx.fillText(ch, x + dx, ly + dy);
          x += cw;
        }
      }
      ctx.textAlign = align;
    }
    function outlineOf(inst, obj) {
      var o = inst.outline || obj.outline;
      if (!o || !(n(o.width, 0) > 0)) return null;
      var rgb = parseRGB(o.color || '#000000') || [0, 0, 0];
      return { width: n(o.width, 0), color: 'rgb(' + rgb.join(',') + ')', rgb: rgb };
    }
    /* estirar (a < 0) o aplastar (a > 0) un momento: solo se ve, no cambia la caja de colision */
    function startSquash(inst, a, dur) { inst.sqA = clamp(a, -0.9, 0.9); inst.sqD = inst.sqT = Math.max(0.02, dur || 0.15); }
    function squashK(inst) { return inst.sqT > 0 ? inst.sqA * Math.sin(Math.PI * inst.sqT / inst.sqD) : 0; }
    /* silueta blanca del fotograma (destello al recibir un golpe), en cache */
    var whiteCache = {};
    function whiteFrame(cv, key) {
      var c = whiteCache[key];
      if (c) return c;
      var src = cv.data || cv.ctx.getImageData(0, 0, cv.width, cv.height).data;
      c = S2D.image(cv.width, cv.height, src);
      var d = c.data;
      for (var i = 0; i < d.length; i += 4) { d[i] = d[i + 1] = d[i + 2] = 255; }
      c.update();
      return (whiteCache[key] = c);
    }
    function drawInstance(inst) {
      var obj = objDef(inst);
      if (obj.type === 'light') return;                              /* las luces solo se ven en la oscuridad */
      if (!inst.visible || inst.opacity <= 0 || inst.flashHide) return;
      /* lo mas comun (un sprite sin filtro, tinte, contorno ni imagen propia): una sola llamada de dibujo */
      if ((!obj.type || obj.type === 'sprite') && !inst.img && !inst.tint && !inst.effectFilter && !obj.shader && !inst.outline && !obj.outline && !obj.nine && ctx.drawSprite) {
        var fan = inst.animName ? animFind(obj, inst.animName) : null;
        var fsid = fan ? fan.spriteId : obj.spriteId, ffr = fan ? inst.animFrame | 0 : inst.frame | 0;
        var fcv = spriteFrame(fsid, ffr);
        if (inst.whiteT > 0) fcv = whiteFrame(fcv, fsid + '#' + ffr);
        var fsx = inst.scaleX || 1, fsy = inst.scaleY || 1;
        if (inst.sqT > 0) { var sk = squashK(inst); fsx *= 1 + sk; fsy *= 1 - sk; }
        if (inst.flipX === -1) fsx = -fsx;
        ctx.drawSprite(fcv, pxOf(inst.x, inst), pxOf(inst.y, inst), inst.angle ? inst.angle * Math.PI / 180 : 0, fsx, fsy, inst.opacity >= 100 ? 1 : clamp(inst.opacity / 100, 0, 1));
        return;
      }
      var hud = !!obj.hud;
      var filter = inst.effectFilter || obj.shader || null;
      if (filter) { ctx.save(); try { ctx.filter = filter; } catch (e) { quiet(e); } }
      ctx.save();
      ctx.globalAlpha = clamp(inst.opacity / 100, 0, 1);
      if (hud) {
        ctx.translate(pxOf(inst.x, inst), pxOf(inst.y, inst));
      } else {
        ctx.translate(pxOf(inst.x, inst), pxOf(inst.y, inst));
      }
      if (inst.angle) ctx.rotate(inst.angle * Math.PI / 180);
      ctx.scale(inst.scaleX || 1, inst.scaleY || 1);
      if (obj.type === 'text') {
        var content = inst.textOverride != null ? inst.textOverride : ((obj.text && obj.text.content) || 'Texto');
        content = resolveStr(content);
        var size = n(obj.text && obj.text.size, 24), tcol = inst.textColor || (obj.text && obj.text.color) || '#ffffff';
        var tpf = pixelFont(obj.text && obj.text.font), lines = String(content).split('\n'), lh = tpf ? size : size * 1.25, ly;
        /* varias lineas (\n): el bloque queda centrado en vertical en la posicion del objeto; mientras escribe la
           maquina de escribir cuenta ya las lineas del texto completo, asi las primeras no se mueven */
        var nlines = inst.typeFull != null ? Math.max(lines.length, String(inst.typeFull).split('\n').length) : lines.length;
        for (var li = 0; li < lines.length; li++) {
          ly = (li - (nlines - 1) / 2) * lh;
          if (tpf) {
            if (ly) ctx.translate(0, ly);
            var tol = outlineOf(inst, obj), talign = (obj.text && obj.text.align) || 'left';
            if (tol) {
              /* con contorno: el texto ya dibujado (9 pasadas) se guarda en una imagen y se reutiliza */
              var tm = ctx.m || [1, 0, 0, 1, 0, 0], tpx = Math.max(0.0001, Math.sqrt(tm[0] * tm[0] + tm[1] * tm[1])), tdd = Math.round(tol.width / tpx * 4) / 4;
              var tim = outlinedText(tpf, lines[li], size, tcol, tol.color, tdd);
              var tx0 = talign === 'center' ? -tim.w / 2 : talign === 'right' ? -tim.w : 0;
              ctx.drawImage(tim.cv, tx0 - tim.pad, -tim.h / 2 - tim.pad);
            } else drawPixelText(tpf, lines[li], size, tcol, talign);
            if (ly) ctx.translate(0, -ly);
          }
          else {
            ctx.font = fontCss(obj.text && obj.text.font, size, !obj.text || obj.text.bold !== false);
            ctx.textAlign = (obj.text && obj.text.align) || 'left';
            ctx.textBaseline = 'middle';
            var fol = outlineOf(inst, obj);
            if (fol) {
              var fm = ctx.m || [1, 0, 0, 1, 0, 0], fpx = Math.max(0.0001, Math.sqrt(fm[0] * fm[0] + fm[1] * fm[1])), fdd = fol.width / fpx;
              ctx.fillStyle = fol.color;
              for (var foi = 0; foi < 8; foi++) ctx.fillText(lines[li], OL_DX[foi] * fdd, ly + OL_DY[foi] * fdd);
            }
            if (lines[li].indexOf('[') >= 0 && richRe.test(lines[li])) drawRichLine(lines[li], ly, tcol, ctx.textAlign);
            else { ctx.fillStyle = tcol; ctx.fillText(lines[li], 0, ly); }
          }
        }
      } else if (obj.type === 'button') {
        var bt = obj.button || {};
        var bw = n(bt.width, 160), bh = n(bt.height, 42), rad = n(bt.radius, 6);
        ctx.fillStyle = inst.btnBg || bt.bg || '#4a76c4';
        ctx.beginPath();
        if (ctx.roundRect) ctx.roundRect(-bw / 2, -bh / 2, bw, bh, rad);
        else ctx.rect(-bw / 2, -bh / 2, bw, bh);
        ctx.fill();
        if (hitTest(inst, state.mouse.x, state.mouse.y)) {
          ctx.fillStyle = 'rgba(255,255,255,0.13)';
          ctx.fill();
        }
        var bcol = inst.btnFg || bt.fg || '#ffffff', btxt = resolveStr(inst.btnText != null ? inst.btnText : (bt.text || 'Boton')), bpf = pixelFont(bt.font);
        if (bpf) drawPixelText(bpf, btxt, n(bt.fontSize, 16), bcol, 'center');
        else {
          ctx.fillStyle = bcol;
          ctx.font = fontCss(bt.font, n(bt.fontSize, 16), bt.bold !== false);
          ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
          ctx.fillText(btxt, 0, 0);
        }
      } else {
        var anim = inst.animName ? animFind(obj, inst.animName) : null;
        var ckey = inst.img ? 'img' + inst.img._uid : anim ? anim.spriteId + '#' + (inst.animFrame | 0) : obj.spriteId + '#' + (inst.frame | 0);
        var cv = inst.img || (anim ? spriteFrame(anim.spriteId, inst.animFrame | 0) : spriteFrame(obj.spriteId, inst.frame | 0));
        if (inst.img && obj.spriteId && sprites[obj.spriteId]) {        /* imagen propia: ocupa el tamano del sprite del objeto */
          var bsp = sprites[obj.spriteId];
          ctx.scale(bsp.width / Math.max(1, cv.width), bsp.height / Math.max(1, cv.height));
        }
        if (inst.tint) cv = tintedFrame(cv, ckey, inst.tint);
        if (inst.flipX === -1) ctx.scale(-1, 1);
        var sol = outlineOf(inst, obj);
        if (sol) {
          var sil = tintedFrame(inst.img || (anim ? spriteFrame(anim.spriteId, inst.animFrame | 0) : spriteFrame(obj.spriteId, inst.frame | 0)), ckey + '#ol', sol.rgb);
          var sm = ctx.m || [1, 0, 0, 1, 0, 0], spx = Math.max(0.0001, Math.sqrt(sm[0] * sm[0] + sm[1] * sm[1])), od = sol.width / spx;
          for (var oi = 0; oi < 8; oi++) ctx.drawImage(sil, -cv.width / 2 + OL_DX[oi] * od, -cv.height / 2 + OL_DY[oi] * od);
        }
        var nine = obj.nine;
        if (nine && (n(nine.l, 0) || n(nine.r, 0) || n(nine.t, 0) || n(nine.b, 0))) {
          /* nine-slice: las esquinas no se estiran; los bordes solo a lo largo y el centro en los dos sentidos */
          var nsx = inst.scaleX || 1, nsy = inst.scaleY || 1;
          ctx.scale(1 / nsx, 1 / nsy);
          drawNine(cv, cv.width * Math.abs(nsx), cv.height * Math.abs(nsy), nine);
        } else ctx.drawImage(cv, -cv.width / 2, -cv.height / 2);
      }
      ctx.restore();
      if (filter) ctx.restore();
    }
    function drawNine(cv, W, H, nine) {
      var w = cv.width, h = cv.height;
      var l = clamp(n(nine.l, 0), 0, w / 2), r = clamp(n(nine.r, 0), 0, w / 2), t = clamp(n(nine.t, 0), 0, h / 2), b = clamp(n(nine.b, 0), 0, h / 2);
      var sxs = [0, l, w - r, w], sys = [0, t, h - b, h];
      var dl = Math.min(l, W / 2), dr = Math.min(r, W / 2), dt = Math.min(t, H / 2), db = Math.min(b, H / 2);
      var dxs = [-W / 2, -W / 2 + dl, W / 2 - dr, W / 2], dys = [-H / 2, -H / 2 + dt, H / 2 - db, H / 2];
      for (var yi = 0; yi < 3; yi++) for (var xi = 0; xi < 3; xi++) {
        var sw = sxs[xi + 1] - sxs[xi], sh = sys[yi + 1] - sys[yi], dw = dxs[xi + 1] - dxs[xi], dh = dys[yi + 1] - dys[yi];
        if (sw > 0 && sh > 0 && dw > 0 && dh > 0) ctx.drawImage(cv, sxs[xi], sys[yi], sw, sh, dxs[xi], dys[yi], dw, dh);
      }
    }

    /* ---------------- luces 2D ----------------
       La escena se oscurece (Escena > Luz: oscuridad y color, o la accion "Oscuridad de la escena"); cada objeto con
       el comportamiento Luz abre un circulo con borde suave en esa oscuridad y suma un brillo de su color. */
    var lightSurf = null, lightImg = null;
    /* circulo blanco con borde suave (alfa 1 en el centro y 0 en el borde) para abrir la oscuridad */
    function lightTex() {
      if (lightImg) return lightImg;
      var N2 = 128, img = S2D.image(N2, N2), d = img.data;
      for (var y = 0; y < N2; y++) for (var x = 0; x < N2; x++) {
        var dx = (x + 0.5) / N2 * 2 - 1, dy = (y + 0.5) / N2 * 2 - 1, r = Math.sqrt(dx * dx + dy * dy), a = r >= 1 ? 0 : 1 - r * r;
        a = a * a * (3 - 2 * a);
        var i = (y * N2 + x) * 4;
        d[i] = d[i + 1] = d[i + 2] = 255; d[i + 3] = Math.round(a * 255);
      }
      img.update();
      return (lightImg = img);
    }
    /* oscuridad de una capa (la de la accion o la de la capa en el editor); null si no tiene */
    function layerDarkness(lid) {
      var o = state.layerDark && state.layerDark[lid], L = layers[lid];
      if (o) return o.v > 0 ? o : null;
      return L && n(L.darkness, 0) > 0 ? { v: n(L.darkness, 0), c: L.darkColor } : null;
    }
    /* la oscuridad tapa lo dibujado hasta ahora (la capa y las de debajo); las luces le abren agujeros. Sin argumentos:
       la oscuridad de la escena (proyectos de antes y la accion "Oscuridad de la escena") */
    function drawLighting(shX, shY, darkV, darkC) {
      var sc = scenes[state.sceneId], lt = (sc && sc.light) || {};
      var dark = darkV != null ? darkV : state.darkness != null ? state.darkness : n(lt.darkness, 0);
      if (!(dark > 0)) return;
      if (!lightSurf) lightSurf = S2D.surface(VW, VH);
      var main = ctx, lc = lightSurf.ctx, cam = state.camera, z = cam.zoom || 1, rgb = hexRGB(darkV != null ? (darkC || '#05060f') : (state.darkColor || lt.color || '#05060f'));
      lc.setTransform(1, 0, 0, 1, 0, 0);
      lc.globalCompositeOperation = 'copy';
      lc.globalAlpha = 1;
      lc.fillStyle = 'rgba(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ',' + clamp(dark / 100, 0, 1) + ')';
      lc.fillRect(0, 0, VW, VH);
      lc.setTransform(z, 0, 0, z, -cam.x * z + shX, -cam.y * z + shY);
      var glows = [];
      for (var i = 0; i < state.instances.length; i++) {
        var o = state.instances[i], od = o._od || objDef(o);
        if (!o.visible) continue;
        var L = od.type === 'light' ? (od.light || {}) : od.behaviorList && od.behaviorList.length ? bhOf(o, 'light') : null;
        if (!L) continue;
        var r = Math.max(4, n(L.radius, 160)), it = clamp(n(L.intensity, 1), 0, 1), fl = clamp(n(L.flicker, 0), 0, 1);
        if (fl) { var w = Math.sin(state.time * 17 + o.id) * 0.5 + Math.sin(state.time * 31 + o.id * 3) * 0.5; r *= 1 - fl * 0.08 * (w + 1); it *= 1 - fl * 0.15 * (w + 1); }
        lc.globalCompositeOperation = 'destination-out';
        lc.globalAlpha = it;
        lc.drawImage(lightTex(), o.x - r, o.y - r, r * 2, r * 2);
        lc.globalAlpha = 1;
        glows.push(o.x, o.y, r, it, L.color || '#ffd9a0');
      }
      lc.globalCompositeOperation = 'source-over';
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalAlpha = 1;
      ctx.drawImage(lightSurf, 0, 0, VW, VH);
      /* brillo de color de cada luz, sumado */
      ctx.setTransform(z, 0, 0, z, -cam.x * z + shX, -cam.y * z + shY);
      ctx.globalCompositeOperation = 'lighter';
      for (var k = 0; k < glows.length; k += 5) {
        var c = hexRGB(glows[k + 4]), gg = ctx.createRadialGradient(glows[k], glows[k + 1], 0, glows[k], glows[k + 1], glows[k + 2]);
        gg.addColorStop(0, 'rgba(' + c[0] + ',' + c[1] + ',' + c[2] + ',' + glows[k + 3] * 0.28 * clamp(dark / 100, 0, 1) + ')');
        gg.addColorStop(1, 'rgba(' + c[0] + ',' + c[1] + ',' + c[2] + ',0)');
        ctx.fillStyle = gg;
        ctx.fillRect(glows[k] - glows[k + 2], glows[k + 1] - glows[k + 2], glows[k + 2] * 2, glows[k + 2] * 2);
      }
      ctx.restore();
      ctx = main;
    }

    /* ---------------- capas con shader (GLSL) ----------------
       Una capa con shader se dibuja en su propio lienzo de la GPU, pasa por el shader (N.gpuShader) y se compone. */
    var layerFx = {};
    function layerShaderCode(lid) {
      var L = layers[lid]; if (!L || !L.shaderId) return '';
      var code = '';
      (project.shaders || []).forEach(function (x) { if (x.id === L.shaderId) code = x.code || ''; });
      return code;
    }
    function layerFxOf(lid) {
      if (layerFx[lid]) return layerFx[lid];
      var c2 = S2D.surface(VW, VH);
      return (layerFx[lid] = { c2: c2, x2: c2.ctx });
    }
    /* Sprites fuera de la vista (con el parallax de su capa) no se dibujan. Texto y botones siempre. */
    function offView(inst, obj, par) {
      if (obj.type && obj.type !== 'sprite') return false;
      var sp = sprites[obj.spriteId];
      if (!sp || inst.img) return false;
      var cam = state.camera, z = cam.zoom || 1;
      var r = Math.max(sp.width, sp.height) * Math.max(Math.abs(inst.scaleX || 1), Math.abs(inst.scaleY || 1)) * 0.75 + 8;
      var vw = VW / z, vh = VH / z, cx = cam.x * par + vw / 2, cy = cam.y * par + vh / 2;
      if (cam.rot) { var rad = Math.sqrt(vw * vw + vh * vh) / 2; vw = vh = rad * 2; }
      return Math.abs(inst.x - cx) > vw / 2 + r || Math.abs(inst.y - cy) > vh / 2 + r;
    }
    /* objetos de cada capa (sin los del HUD), ya en orden de z: se reparten una vez por fotograma; solo se ordena
       una capa si sus z no estan ya en orden (lo normal es que si) */
    function layerBuckets() {
      var out = {}, list = state.instances;
      for (var i = 0; i < list.length; i++) {
        var inst = list[i], od = objDef(inst);
        if (od.hud) continue;
        var c = od.capa, lid = (c && layers[c]) ? c : 'base', bk = out[lid];
        if (!bk) bk = out[lid] = { list: [], z: -Infinity, sorted: true };
        var z = typeof inst.z === 'number' ? inst.z : n(inst.z);
        if (z < bk.z) bk.sorted = false; else bk.z = z;
        bk.list.push(inst);
      }
      for (var k in out) if (!out[k].sorted) {
        var L = out[k].list;
        for (var j = 0; j < L.length; j++) L[j]._ord = j;            /* desempate estable: orden de creacion */
        L.sort(function (a, b) { return (n(a.z) - n(b.z)) || (a._ord - b._ord); });
      }
      return out;
    }
    function drawLayerInstances(lid, buckets) {
      var par = n(layers[lid] && layers[lid].parallax, 1), bk = buckets[lid];
      if (!bk) return;
      var drawList = bk.list, sel = state.selection, anySel = false;
      for (var sk in sel) { anySel = true; break; }
      if (!anySel && !drawBox) {                                    /* lo normal en un juego: dibujar lo que se ve */
        for (var fi = 0; fi < drawList.length; fi++) if (!offView(drawList[fi], drawList[fi]._od || objDef(drawList[fi]), par)) drawInstance(drawList[fi]);
        return;
      }
      for (var i = 0; i < drawList.length; i++) {
        var inst = drawList[i], od = objDef(inst);
        if (!drawBox && !state.selection[inst.id] && offView(inst, od, par)) continue;
        if (state.selection[inst.id]) {
          var sz2 = instSize(inst);
          ctx.strokeStyle = 'rgba(143,184,255,0.95)';
          ctx.lineWidth = 2 / state.camera.zoom;
          ctx.strokeRect(inst.x - sz2.w / 2 - 3, inst.y - sz2.h / 2 - 3, sz2.w + 6, sz2.h + 6);
        }
        if (drawBox) { var isz = instSize(inst), rr = Math.max(isz.w, isz.h) * 0.75 + 4; trackBox(inst.x - rr, inst.y - rr, inst.x + rr, inst.y + rr); }
        drawInstance(inst);
      }
    }
    function layerHasContent(lid, buckets) {
      var i, sc = scenes[state.sceneId], tms = (sc && sc.tilemaps) || [];
      if (buckets[lid] && buckets[lid].list.length) return true;
      for (i = 0; i < tms.length; i++) if ((tms[i].layer && layers[tms[i].layer] ? tms[i].layer : 'base') === lid) return true;
      return lid === 'base' && particles.length > 0;
    }
    function drawLayerShaded(lid, par, shX, shY, cam, drawList, code) {
      var fx = layerFxOf(lid), main = ctx;
      ctx = fx.x2;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, VW, VH);
      /* shaderContentOnly: el shader solo se aplica en el rectangulo donde la capa tiene algo (mucho mas rapido;
         no sirve para shaders que pintan por toda la pantalla, como la lluvia) */
      var only = !!layers[lid].shaderContentOnly;
      drawBox = only ? [1e9, 1e9, -1e9, -1e9] : null;
      ctx.save();
      camTransform(par, shX, shY);
      drawTilemaps(lid, par);
      drawLayerInstances(lid, drawList);
      if (lid === 'base') drawParticles();
      ctx.restore();
      ctx = main;
      var box = drawBox;
      drawBox = null;
      if (box) {
        if (box[2] <= box[0]) return;                               /* la capa no tiene nada a la vista */
        var mg = 12;                                                /* margen para lo que mueva el shader */
        box = [Math.max(0, Math.floor(box[0]) - mg), Math.max(0, Math.floor(box[1]) - mg), Math.min(VW, Math.ceil(box[2]) + mg), Math.min(VH, Math.ceil(box[3]) + mg)];
        if (box[2] <= box[0] || box[3] <= box[1]) return;
      }
      var src = fx.c2, t = state.time || 0;
      var ts0 = profT();
      try { N.gpuShader(fx.c2._gpu, code, t, cam.x * par, cam.y * par, cam.zoom); }   /* si falla, la capa queda sin shader */
      catch (e) { console.warn('Source 2D [capa shader]: ' + e.message); }
      profAdd('shader de ' + lid + ' (GPU)', ts0);
      ctx.save();
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      if (box) ctx.drawImage(src, box[0], box[1], box[2] - box[0], box[3] - box[1], box[0], box[1], box[2] - box[0], box[3] - box[1]);
      else ctx.drawImage(src, 0, 0, VW, VH);
      ctx.restore();
    }

    /* transformacion de la camara para una capa con ese parallax; con rotacion (camera.rot, en grados) gira
       alrededor del centro de la pantalla */
    function camTransform(par, shX, shY) {
      var cam = state.camera, z = cam.zoom, tx = -cam.x * par * z + shX, ty = -cam.y * par * z + shY, r = n(cam.rot, 0) * Math.PI / 180;
      if (!r) { ctx.setTransform(z, 0, 0, z, tx, ty); return; }
      var co = Math.cos(r), si = Math.sin(r), cx = VW / 2, cy = VH / 2;
      ctx.setTransform(co * z, si * z, -si * z, co * z, co * (tx - cx) - si * (ty - cy) + cx, si * (tx - cx) + co * (ty - cy) + cy);
    }
    function draw() {
      var sc = scenes[state.sceneId] || { width: VW, height: VH, background: '#202329' };
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.clearRect(0, 0, VW, VH);
      ctx.fillStyle = state.bg || sc.background || '#202329';
      ctx.fillRect(0, 0, VW, VH);

      var cam = state.camera;
      var shX = 0, shY = 0;
      if (cam.shakeT > 0) {
        /* trauma: fuerza al cuadrado del tiempo que queda y movimiento con ondas mezcladas (suave, no a saltos) */
        var tr = clamp(cam.shakeT / (cam.shakeDur || cam.shakeT), 0, 1), tm = state.time * 37;
        shX = cam.shakeMag * tr * tr * (Math.sin(tm) * 0.6 + Math.sin(tm * 2.3 + 1.7) * 0.4);
        shY = cam.shakeMag * tr * tr * (Math.sin(tm * 1.3 + 0.5) * 0.6 + Math.sin(tm * 2.9 + 3.1) * 0.4);
      }
      ctx.save();
      camTransform(1, shX, shY);

      if (state.boxStart && state.mouse.down) {
        ctx.strokeStyle = 'rgba(143,184,255,0.9)';
        ctx.lineWidth = 2 / cam.zoom;
        ctx.strokeRect(Math.min(state.boxStart.x, state.mouse.wx), Math.min(state.boxStart.y, state.mouse.wy),
          Math.abs(state.mouse.wx - state.boxStart.x), Math.abs(state.mouse.wy - state.boxStart.y));
      }
      var layerOrder = Object.keys(layers).sort(function (a, b) { return n(layers[a].z) - n(layers[b].z); });
      var drawList = layerBuckets();
      for (var li = 0; li < layerOrder.length; li++) {
        var lid = layerOrder[li], par = n(layers[lid].parallax, 1);
        if (layers[lid].hidden) continue;
        var lcode = layerShaderCode(lid);
          var tl0 = profT();
        if (lcode && !layerHasContent(lid, drawList)) {                /* capa vacia: sin pase de shader */
          profAdd('capa ' + lid, tl0);
          var lde = layerDarkness(lid);
          if (lde) drawLighting(shX, shY, lde.v, lde.c);
          continue;
        }
        if (lcode) {
          drawLayerShaded(lid, par, shX, shY, cam, drawList, lcode); profAdd('capa ' + lid + ' (con shader)', tl0);
          var lds = layerDarkness(lid);
          if (lds) drawLighting(shX, shY, lds.v, lds.c);
          continue;
        }
        ctx.save();
        camTransform(par, shX, shY);
        drawTilemaps(lid, par);
        drawLayerInstances(lid, drawList);
        profAdd('capa ' + lid, tl0);
        if (lid === 'base') { tl0 = profT(); drawParticles(); profAdd('particulas', tl0); }   /* encima de los objetos de la capa principal */
        ctx.restore();
        var ldk = layerDarkness(lid);
        if (ldk) drawLighting(shX, shY, ldk.v, ldk.c);
      }
      var tlg = profT();
      drawLighting(shX, shY);
      profAdd('luces', tlg);
      ctx.restore();

      ctx.setTransform(1, 0, 0, 1, 0, 0);
      for (var j = 0; j < state.instances.length; j++) {
        if (objDef(state.instances[j]).hud) drawInstance(state.instances[j]);
      }

      var te0 = profT();
      applySceneEffects(sc.effects);
      runSceneShader(sc.shaderCode);
      profAdd('efectos de escena', te0);

      if (state.transition) {
        var tr = state.transition;
        var a = tr.phase === 'out' ? clamp(tr.t / tr.dur, 0, 1) : 1 - clamp(tr.t / tr.dur, 0, 1);
        if (tr.type === 'blur') {
          ctx.fillStyle = 'rgba(10,12,18,' + a * 0.9 + ')';
        } else {
          ctx.fillStyle = 'rgba(6,8,14,' + a + ')';
        }
        ctx.fillRect(0, 0, VW, VH);
      }
      if (state.ended) {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.fillStyle = 'rgba(24,26,32,0.82)';
        ctx.fillRect(0, 0, VW, VH);
        ctx.fillStyle = '#ffffff';
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.font = 'bold 40px "Segoe UI",sans-serif';
        ctx.fillText(state.endMessage || 'Fin del juego', VW / 2, VH / 2 - 18);
        ctx.font = '19px "Segoe UI",sans-serif';
        ctx.fillStyle = '#8fb8ff';
        ctx.fillText('Pulsa R para reiniciar', VW / 2, VH / 2 + 32);
      }
    }

    /* ---------------- bucle ---------------- */
    var last = 0;
    function step(dt) {
      dt = dt * state.timeScale;
      state.dt = dt;
      state.frame++;
      state.time += dt;
      /* pausa de impacto: el juego se congela un momento (golpes con peso) */
      if (state.hitstop > 0) { state.hitstop -= dt / (state.timeScale || 1); state.frame--; state.time -= dt; return; }
      /* mensajes: los enviados en el fotograma anterior se pueden recibir en este */
      state.msg = state.msgNext; state.msgNext = {};
      var paused = !!state.paused;
      if (!paused) Object.keys(state.timers).forEach(function (k) { if (!state.timers[k].paused) state.timers[k].t += dt; });

      if (state.transition) {
        var tr = state.transition;
        tr.t += dt;
        if (tr.t >= tr.dur) {
          if (tr.phase === 'out') {
            loadScene(tr.nextId, true);
            state.frame = 1;     /* este paso ya cuenta como el primero: "al inicio de la escena" una sola vez */
            state.transition = { type: tr.type, phase: 'in', t: 0, dur: tr.dur, nextId: tr.nextId };
          } else {
            state.transition = null;
          }
        }
      }

      if (state.ended) {
        if (pressedKey('r')) loadScene(state.sceneId, true);
        return;
      }
      for (var pi = 0; pi < state.instances.length; pi++) {
        var pin = state.instances[pi];
        pin.prevX = pin.x;
        pin.prevY = pin.y;
        if (pin.evLandPending) { pin.evLand = state.frame; pin.evLandPending = false; }
      }
      state.camera.prevX = state.camera.x; state.camera.prevY = state.camera.y;
      state.mouse.wx = state.mouse.x / state.camera.zoom + state.camera.x;
      state.mouse.wy = state.mouse.y / state.camera.zoom + state.camera.y;
      var tq = profT();
      if (!paused) {
        handleRTSInput();
        applyMovement();
        updatePlatforms(dt);
        carryRiders();
        updateAnims(dt);
        typeTick(dt);
        updateBehaviorsPre(dt);
        updatePaths(dt);
        if (anyOnUpdate) for (var i = 0; i < state.instances.length; i++) {
          var h = objDef(state.instances[i]).hooks;
          if (h && h.onUpdate) runCode(h.onUpdate, state.instances[i], 'onUpdate');
        }
      } else typeTick(dt);
      profAdd('  movimiento y comportamientos', tq);
      state.sceneChanged = false;
      tq = profT();
      runWaits(dt);
      if (!state.sceneChanged) runEvents();
      profAdd('  eventos', tq);
      if (state.sceneChanged) { state.sceneChanged = false; return; }
      /* en pausa: los eventos siguen (para el menu de pausa) y los tweens de los objetos que "siguen en pausa" */
      if (state.paused) { updateTweens(dt, true); updateCamera(dt); return; }
      updateTweens(dt);
      tq = profT();
      physics(dt);
      profAdd('  fisica', tq);
      updateBehaviorsPost();
      updateCamera(dt);
      tq = profT();
      updateParticles(dt);
      profAdd('  particulas (mover)', tq);
    }
    /* ---------------- depurador (vista previa del editor: --debug) ----------------
       F3 muestra fps, tiempos, objetos, variables y lo que hay bajo el raton (y las cajas de colision); F6 pausa y
       F7 avanza un fotograma. */
    var dbg = { on: false, pause: false, stepOne: false, fps: 0, tStep: 0, tDraw: 0, lastT: 0, frames: 0, acc: 0 };
    function hrNow() { try { return N.hrtime ? N.hrtime() : Date.now(); } catch (e) { return Date.now(); } }
    function drawDebug() {
      var cam = state.camera, z = cam.zoom || 1, i, L = state.instances;
      ctx.save();
      /* cajas de colision */
      ctx.setTransform(z, 0, 0, z, -cam.x * z, -cam.y * z);
      ctx.lineWidth = 1 / z;
      var hover = null;
      for (i = 0; i < L.length; i++) {
        var o = L[i], od = o._od || objDef(o);
        if (od.hud || od.type === 'text' || od.type === 'button') continue;
        var sz = instSize(o);
        if (Math.abs(o.x - cam.x - VW / z / 2) > VW / z || Math.abs(o.y - cam.y - VH / z / 2) > VH / z) continue;
        var b = od.behaviors || {};
        ctx.strokeStyle = b.solid ? 'rgba(255,170,60,0.9)' : (b.gravity || b.collide) ? 'rgba(90,200,255,0.9)' : 'rgba(160,255,160,0.6)';
        ctx.strokeRect(o.x - sz.w / 2, o.y - sz.h / 2, sz.w, sz.h);
        if (Math.abs(state.mouse.wx - o.x) * 2 <= sz.w && Math.abs(state.mouse.wy - o.y) * 2 <= sz.h) hover = o;
      }
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      var lines = [
        'Depurador  F3 ocultar  F6 ' + (dbg.pause ? 'seguir' : 'pausa') + '  F7 un fotograma',
        dbg.fps + ' fps   logica ' + dbg.tStep.toFixed(1) + ' ms   dibujo ' + dbg.tDraw.toFixed(1) + ' ms',
        'escena ' + ((scenes[state.sceneId] || {}).name || state.sceneId) + '   objetos ' + L.length + '   particulas ' + particles.length,
        'camara ' + Math.round(cam.x) + ', ' + Math.round(cam.y) + '  zoom ' + (+z).toFixed(2) + '   raton ' + Math.round(state.mouse.wx) + ', ' + Math.round(state.mouse.wy)
      ];
      if (hover) {
        var hd = hover._od || objDef(hover);
        lines.push('> ' + (hd.name || hover.objId) + ' #' + hover.id + '  x ' + hover.x.toFixed(1) + '  y ' + hover.y.toFixed(1) +
                   '  vx ' + (+hover.vx || 0).toFixed(0) + '  vy ' + (+hover.vy || 0).toFixed(0) + (hover.grounded ? '  en el suelo' : ''));
        var vk = Object.keys(hover.vars || {}).slice(0, 6);
        if (vk.length) lines.push('  ' + vk.map(function (k) { return k + '=' + fmtVal(hover.vars[k]); }).join('  '));
      }
      var gk = Object.keys(state.globals).filter(function (k) { return k.charAt(0) !== '_'; }).slice(0, 12);
      for (i = 0; i < gk.length; i++) { var gv = state.globals[gk[i]]; lines.push(gk[i] + ' = ' + (typeof gv === 'object' ? JSON.stringify(gv) : String(gv)).slice(0, 60)); }
      ctx.font = '13px sans-serif';
      ctx.textAlign = 'left'; ctx.textBaseline = 'top';
      var w = 0;
      for (i = 0; i < lines.length; i++) w = Math.max(w, ctx.measureText(lines[i]).width);
      ctx.fillStyle = 'rgba(10,12,20,0.82)';
      ctx.fillRect(6, 6, w + 16, lines.length * 17 + 10);
      for (i = 0; i < lines.length; i++) { ctx.fillStyle = i === 0 ? '#ffd480' : i < 4 ? '#cfe3ff' : '#e8e8f0'; ctx.fillText(lines[i], 14, 11 + i * 17); }
      ctx.restore();
    }
    function loop(t) {
      if (!state.running) return;
      if (N.debug) {
        dbg.frames++;
        if (t - dbg.lastT >= 500) { dbg.fps = Math.round(dbg.frames * 1000 / Math.max(1, t - dbg.lastT)); dbg.frames = 0; dbg.lastT = t; }
        if (dbg.pause && !dbg.stepOne) {                            /* en pausa: solo se redibuja (con el panel) */
          last = t; draw(); if (dbg.on) drawDebug(); clearFrameInput(); return;
        }
        dbg.stepOne = false;
      }
      var dt = last ? Math.min(0.05, (t - last) / 1000) : 0.016;
      last = t;
      var tp0 = profT();
      /* paso fijo (Ajustes): la logica avanza siempre de 1/60 s (igual en cualquier monitor o equipo) y el dibujo
         se interpola entre los dos ultimos pasos, asi a 144 Hz se ve suave y a 30 fps el juego no cambia */
      if (project.settings && project.settings.fixedStep) {
        fixedAcc = Math.min(fixedAcc + dt, 0.25);
        var steps = 0;
        while (fixedAcc >= FIXED_DT && steps < 8) {
          step(FIXED_DT); fixedAcc -= FIXED_DT; steps++;
          clearFrameInput();
        }
        profAdd('logica y eventos', tp0);
        tp0 = profT();
        drawInterpolated(fixedAcc / FIXED_DT);
      } else {
        var dh0 = N.debug ? hrNow() : 0;
        step(dt);
        if (N.debug) dbg.tStep = dbg.tStep * 0.9 + (hrNow() - dh0) * 0.1;
        profAdd('logica y eventos', tp0);
        tp0 = profT();
        dh0 = N.debug ? hrNow() : 0;
        draw();
        if (N.debug) dbg.tDraw = dbg.tDraw * 0.9 + (hrNow() - dh0) * 0.1;
      }
      if (N.debug && dbg.on) drawDebug();
      profAdd('dibujo total', tp0);
      profFrame();
      /* los shaders GLSL de pantalla se aplican, uno tras otro, sobre el fotograma ya dibujado (u_amount = intensidad) */
      var sl = screenShaderList();
      for (var si = 0; si < sl.length; si++) {
        try { N.shaderAmount(sl[si].amount); N.gpuShader(canvas._gpu, sl[si].code, state.time, 0, 0, 1); } catch (e) { quiet(e); }
      }
      if (sl.length) N.shaderAmount(1);
      if (!(project.settings && project.settings.fixedStep)) clearFrameInput();
    }
    /* lo que solo dura un paso: teclas recien pulsadas o soltadas, rueda y clics */
    function clearFrameInput() {
      state.pressed = {}; state.released = {}; state.lastKey = ''; state.typed = '';
      state.wheelUp = false; state.wheelDown = false;
      state.mouse.pressed = false; state.mouse.released = false;
    }
    var FIXED_DT = 1 / 60, fixedAcc = 0;
    function drawInterpolated(a) {
      var L = state.instances, i, keep = [], cam = state.camera, cx = cam.x, cy = cam.y;
      for (i = 0; i < L.length; i++) {
        var o = L[i];
        if (o.prevX === undefined || (o.x === o.prevX && o.y === o.prevY)) continue;
        if (Math.abs(o.x - o.prevX) > 200 || Math.abs(o.y - o.prevY) > 200) continue;      /* teletransporte: sin mezclar */
        keep.push(o, o.x, o.y);
        o.x = o.prevX + (o.x - o.prevX) * a; o.y = o.prevY + (o.y - o.prevY) * a;
      }
      if (cam.prevX !== undefined && Math.abs(cx - cam.prevX) < 200 && Math.abs(cy - cam.prevY) < 200) {
        cam.x = cam.prevX + (cx - cam.prevX) * a; cam.y = cam.prevY + (cy - cam.prevY) * a;
      }
      try { draw(); }
      finally {
        for (i = 0; i < keep.length; i += 3) { keep[i].x = keep[i + 1]; keep[i].y = keep[i + 2]; }
        cam.x = cx; cam.y = cy;
      }
    }

    return {
      canvas: canvas,
      state: state,
      start: function () {
        var first = (project.scenes && project.scenes[0]) ? project.scenes[0].id : null;
        if (project.settings && project.settings.pixelPerfect && N.setPixelPerfect) { try { N.setPixelPerfect(true); } catch (e) { quiet(e); } }
        loadScene(first); last = 0; state.running = true;
        S2D.display = canvas; S2D.onInput = onInput; S2D.onFrame = loop;
      },
      restart: function () { loadScene(state.sceneId, true); },
      toggleFullscreen: toggleFullscreen,
      stop: function () {
        state.running = false; stopMusicTrack();
        if (S2D.onFrame === loop) { S2D.onFrame = null; S2D.onInput = null; S2D.display = null; }
      }
    };
  }

  S2D.createGame = createGame;
  S2D.easings = EASINGS;
})();
