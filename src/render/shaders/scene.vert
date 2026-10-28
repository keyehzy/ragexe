#version 450
// Transformed vertices (Direct3D's XYZRHW): screen x and y in pixels, z in [0, 1], and
// rhw = 1/w. Multiplying the position through by w keeps the interpolation perspective
// correct, as Direct3D's is for these vertices.

layout(location = 0) in vec4 in_position;  // x, y, z, rhw
layout(location = 1) in vec4 in_diffuse;   // bytes B, G, R, A of the ARGB dword
layout(location = 2) in vec4 in_specular;
layout(location = 3) in vec2 in_uv0;
layout(location = 4) in vec2 in_uv1;

layout(set = 1, binding = 0) uniform Viewport {
  vec4 size;  // width, height
} viewport;

layout(location = 0) out vec4 out_diffuse;
layout(location = 1) out vec4 out_specular;
layout(location = 2) out vec2 out_uv0;
layout(location = 3) out vec2 out_uv1;

void main() {
  float w = 1.0 / in_position.w;
  vec2 ndc = vec2(in_position.x / viewport.size.x * 2.0 - 1.0,
                  1.0 - in_position.y / viewport.size.y * 2.0);
  gl_Position = vec4(ndc * w, in_position.z * w, w);
  out_diffuse = in_diffuse.bgra;
  out_specular = in_specular.bgra;
  out_uv0 = in_uv0;
  out_uv1 = in_uv1;
}
