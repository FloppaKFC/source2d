#version 450
layout(push_constant) uniform PC { vec2 inv; vec2 res; vec4 m0; vec4 m1; vec4 m2; vec4 k; } pc;
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 o;
void main() {
  vec4 c = texture(u_tex, v_uv);
  if (c.a <= 0.0) { o = c; return; }
  vec3 s = c.rgb / c.a; float a = c.a, k = pc.k.y;
  if (pc.k.x < 0.5) s = vec3(dot(pc.m0.xyz, s), dot(pc.m1.xyz, s), dot(pc.m2.xyz, s));
  else if (pc.k.x < 1.5) s = s + k * (1.0 - 2.0 * s);
  else if (pc.k.x < 2.5) s = s * k;
  else if (pc.k.x < 3.5) s = (s - 0.5) * k + 0.5;
  else a = clamp(a * k, 0.0, 1.0);
  s = clamp(s, 0.0, 1.0);
  o = vec4(s * a, a);
}
