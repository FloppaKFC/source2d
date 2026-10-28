#version 450
// imagen con alfa premultiplicado por el color del vertice (tambien los rellenos lisos, con una textura blanca)
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_col;
layout(location = 0) out vec4 o;
void main() { o = texture(u_tex, v_uv) * v_col; }
