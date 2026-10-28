#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 v_uv;
uniform sampler2D u_tex;
uniform float u_time;

const vec2 RES = vec2(1280.0, 720.0);
const float INTENSITY = 0.58;
const float OPACITY = 1.0;
const float SPEED = 3.2;
const float WIND = 0.22;
const vec3 TINT = vec3(0.3, 0.58, 1.0);

mat2 rot(float a) { float s = sin(a); float c = cos(a); return mat2(c, -s, s, c); }
float hash21(vec2 p) { p = fract(p * vec2(233.34, 851.73)); p += dot(p, p + 23.45); return fract(p.x * p.y); }
float rainLayer(vec2 sp, float t, float cols, float rows, float lenH, float widPx, float seed, float pxH, out float head) {
  float drift = t * 0.12 * (0.5 + fract(seed * 0.371));
  vec2 p = vec2(sp.x * cols + drift, sp.y * rows);
  float col = floor(p.x);
  float h = hash21(vec2(col, seed));
  float h2 = hash21(vec2(col, seed + 7.31));
  float h3 = hash21(vec2(col, seed + 13.77));
  float h4 = hash21(vec2(col, seed + 29.13));
  float sway = sin(t * (0.22 + 0.58 * h2) + h3 * 6.2831) * 0.16;
  float x = fract(p.x + sway) - 0.5;
  float speed = 0.72 + 0.95 * h;
  float phaseJit = sin(t * (0.11 + 0.34 * h3) + h2 * 6.2831) * (0.055 + 0.11 * h);
  float travel = fract(t * speed + h * 41.7 + phaseJit);
  float d = fract(travel - p.y);
  float lenPulse = 0.62 + 0.75 * (0.5 + 0.5 * sin(t * (0.17 + 0.29 * h4) + h * 6.2831));
  float wid = max(widPx * pxH * cols * (0.75 + 0.6 * h4), 0.0001);
  float len = clamp(lenH * rows * lenPulse, 0.0001, 0.98);
  float core = 1.0 - smoothstep(0.0, wid, abs(x));
  float tail = 1.0 - smoothstep(0.0, min(len * (0.55 + h * 0.9), 0.98), d);
  float glow = exp(-d * 6.0 / len) * 0.55;
  head = core * (1.0 - smoothstep(0.0, wid * 3.0, d)) * step(0.25, h);
  return core * (tail + glow) * (0.3 + 0.85 * h);
}
void main() {
  vec2 uv = v_uv;
  float aspect = RES.x / RES.y;
  float pxH = 1.0 / RES.y;
  float t0 = u_time;
  float gust = sin(t0 * 0.23) * 0.5 + sin(t0 * 0.61) * 0.5 + sin(t0 * 1.13) * 0.25;
  float wind = WIND + gust * 0.1;
  vec2 sp = rot(wind) * vec2(uv.x * aspect, uv.y);
  float t = t0 * SPEED;
  float h1; float h2; float h3; float h4;
  float r1 = rainLayer(sp, t * 2.25, 52.0, 11.0, 0.08, 1.15, 3.1, pxH, h1);
  float r2 = rainLayer(sp, t * 1.7, 35.0, 7.5, 0.115, 1.8, 17.7, pxH, h2);
  float r3 = rainLayer(sp, t * 1.28, 22.0, 4.6, 0.17, 2.7, 51.3, pxH, h3);
  float r4 = rainLayer(sp, t * 0.8, 10.0, 2.4, 0.25, 7.5, 87.9, pxH, h4) * 0.22;
  float heads = (h1 * 0.5 + h2 * 0.8 + h3 * 1.0) * 0.7;
  float rain = clamp((r1 * 0.3 + r2 * 0.55 + r3 * 0.82 + r4) * INTENSITY, 0.0, 1.5);
  vec2 warp = vec2(r2 - r1, r3 * 0.5) * 0.0055 * INTENSITY;
  vec4 baseG = texture2D(u_tex, uv + warp);
  vec4 baseB = texture2D(u_tex, uv + warp + vec2(0.0018 * rain, 0.0));
  vec4 baseR = texture2D(u_tex, uv + warp - vec2(0.0018 * rain, 0.0));
  vec3 col = vec3(baseR.r, baseG.g, baseB.b);
  float mist = smoothstep(0.35, 1.0, uv.y) * 0.09;
  vec3 rainCol = mix(TINT * 1.2, vec3(0.8, 0.92, 1.18), 0.5);
  col = mix(col, col * vec3(0.48, 0.7, 1.3), clamp(rain * 0.42, 0.0, 1.0));
  col += rainCol * (rain * 0.26);
  col += vec3(0.75, 0.88, 1.2) * heads * 0.22;
  col = mix(col, TINT * 0.5, mist);
  vec2 vq = uv - 0.5;
  col *= 1.0 - dot(vq, vq) * 0.55;
  float g = hash21(uv * RES + fract(t0) * 137.0);
  col += (g - 0.5) * 0.035;
  float alpha = clamp(max(baseG.a, rain * 0.5 * OPACITY), 0.0, 1.0);
  gl_FragColor = vec4(col, alpha);
}
