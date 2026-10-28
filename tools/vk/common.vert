#version 450
// cuadrados y triangulos en pixeles del lienzo (0,0 arriba a la izquierda)
layout(push_constant) uniform PC { vec2 inv; } pc;
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_col;
void main() { v_uv = a_uv; v_col = a_col; gl_Position = vec4(a_pos * pc.inv - 1.0, 0.0, 1.0); }
