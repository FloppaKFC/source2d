#version 450
layout(push_constant) uniform PC { vec2 inv; vec2 res; vec4 rect; vec4 blur; } pc;
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 o;
void main() {
  vec2 p = v_uv * pc.res; vec4 sum = vec4(0.0); float r = pc.blur.z;
  for (int i = -64; i <= 64; i++) {
    float f = float(i);
    if (f < -r || f > r) continue;
    vec2 q = p + pc.blur.xy * f;
    if (q.x < pc.rect.x || q.y < pc.rect.y || q.x >= pc.rect.z || q.y >= pc.rect.w) continue;
    sum += texture(u_tex, q / pc.res);
  }
  o = sum / (2.0 * r + 1.0);
}
