#version 450
// degradado radial: v_uv trae la posicion en el lienzo; hasta 4 paradas
layout(push_constant) uniform PC { vec2 inv; vec2 pad; vec4 g; vec4 n; vec4 pos; vec4 col[4]; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 o;
void main() {
  float d = distance(v_uv, pc.g.xy), span = pc.g.w - pc.g.z;
  float t = span > 0.000001 ? (d - pc.g.z) / span : (d >= pc.g.w ? 1.0 : 0.0);
  int n = int(pc.n.x + 0.5);
  vec4 c = pc.col[0];
  for (int i = 1; i < 4; i++) {
    if (i >= n) break;
    if (t >= pc.pos[i]) c = pc.col[i];
    else if (t > pc.pos[i - 1]) { float sp = pc.pos[i] - pc.pos[i - 1]; c = mix(pc.col[i - 1], pc.col[i], sp > 0.000001 ? (t - pc.pos[i - 1]) / sp : 0.0); break; }
    else break;
  }
  float a = c.a * pc.n.y;
  o = vec4(c.rgb * a, a);
}
