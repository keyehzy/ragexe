#version 450
// Direct3D 7's two texture stages, alpha test and vertex fog, from the states the
// original sets (docs/render/gpu.md). Operations are D3DTOP values, arguments D3DTA
// values with COMPLEMENT (0x10) and ALPHAREPLICATE (0x20).

layout(location = 0) in vec4 in_diffuse;
layout(location = 1) in vec4 in_specular;
layout(location = 2) in vec2 in_uv0;
layout(location = 3) in vec2 in_uv1;

layout(set = 2, binding = 0) uniform sampler2D texture0;
layout(set = 2, binding = 1) uniform sampler2D texture1;

layout(set = 3, binding = 0) uniform Stages {
  ivec4 stage0_color;  // op, arg1, arg2
  ivec4 stage0_alpha;
  ivec4 stage1_color;
  ivec4 stage1_alpha;
  ivec4 alpha_test;    // enabled, D3DCMP function, reference (0 to 255), fog enabled
  vec4 fog_color;
} stages;

layout(location = 0) out vec4 out_color;

vec4 Argument(int arg, vec4 diffuse, vec4 current, vec4 texel) {
  vec4 v;
  int which = arg & 0xf;
  if (which == 0) {
    v = diffuse;
  } else if (which == 2) {
    v = texel;
  } else {
    v = current;
  }
  if ((arg & 0x10) != 0) v = vec4(1.0) - v;
  if ((arg & 0x20) != 0) v = vec4(v.a);
  return v;
}

vec4 Operate(int op, vec4 a, vec4 b, vec4 diffuse, vec4 current, vec4 texel) {
  if (op == 2) return a;                                    // SELECTARG1
  if (op == 3) return b;                                    // SELECTARG2
  if (op == 4) return a * b;                                // MODULATE
  if (op == 5) return clamp(a * b * 2.0, 0.0, 1.0);         // MODULATE2X
  if (op == 6) return clamp(a * b * 4.0, 0.0, 1.0);         // MODULATE4X
  if (op == 7) return clamp(a + b, 0.0, 1.0);               // ADD
  if (op == 8) return clamp(a + b - 0.5, 0.0, 1.0);         // ADDSIGNED
  if (op == 10) return clamp(a - b, 0.0, 1.0);              // SUBTRACT
  if (op == 12) return mix(b, a, diffuse.a);                // BLENDDIFFUSEALPHA
  if (op == 13) return mix(b, a, texel.a);                  // BLENDTEXTUREALPHA
  if (op == 16) return mix(b, a, current.a);                // BLENDCURRENTALPHA
  if (op == 18) return clamp(a + a.a * b, 0.0, 1.0);        // MODULATEALPHA_ADDCOLOR
  return current;                                           // DISABLE and the rest
}

vec4 RunStage(ivec4 color, ivec4 alpha, vec4 diffuse, vec4 current, vec4 texel) {
  vec4 c = Operate(color.x, Argument(color.y, diffuse, current, texel),
                   Argument(color.z, diffuse, current, texel), diffuse, current, texel);
  vec4 a = Operate(alpha.x, Argument(alpha.y, diffuse, current, texel),
                   Argument(alpha.z, diffuse, current, texel), diffuse, current, texel);
  return vec4(c.rgb, a.a);
}

bool Passes(int func, float value, float reference) {
  if (func == 1) return false;                 // NEVER
  if (func == 2) return value < reference;     // LESS
  if (func == 3) return value == reference;    // EQUAL
  if (func == 4) return value <= reference;    // LESSEQUAL
  if (func == 5) return value > reference;     // GREATER
  if (func == 6) return value != reference;    // NOTEQUAL
  if (func == 7) return value >= reference;    // GREATEREQUAL
  return true;                                 // ALWAYS
}

void main() {
  vec4 color = in_diffuse;
  if (stages.stage0_color.x != 1) {
    color = RunStage(stages.stage0_color, stages.stage0_alpha, in_diffuse, in_diffuse,
                     texture(texture0, in_uv0));
    if (stages.stage1_color.x != 1) {
      color = RunStage(stages.stage1_color, stages.stage1_alpha, in_diffuse, color,
                       texture(texture1, in_uv1));
    }
  }
  if (stages.alpha_test.x != 0) {
    float value = floor(color.a * 255.0 + 0.5);
    if (!Passes(stages.alpha_test.y, value, float(stages.alpha_test.z))) discard;
  }
  if (stages.alpha_test.w != 0) {
    color.rgb = mix(stages.fog_color.rgb, color.rgb, in_specular.a);
  }
  out_color = color;
}
