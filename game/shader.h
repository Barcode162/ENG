#pragma once
// The game's one shader, written twice (WGSL for WebGPU, GLSL ES 3.0 for
// WebGL2), the same maths line for line:
//   light  = the sky's light (from everywhere, a little more from above)
//          + the moon or sun (one direction)
//          + up to 16 lights from the story's light words (candela / d², never
//            closer than 45 cm: a real flame has a size, so nothing burns white)
//   colour = surface · light / π · exposure + glow, the mist over it,
//            then the eye's curve (ACES) and sRGB.
// Vertex colours are stored as the square root of the linear colour;
// alpha says how it's lit: 0 by the lights, 1..250 glowing, 255 the sky.
namespace game {

struct Uniforms {
  float viewProj[16];
  float eye[4];      // xyz, exposure
  float offset[4];   // xyz added to every position (the sky follows you); w: glow strength
  float fog[4];      // the mist's colour (linear), density
  float ambient[4];  // the sky's light (lux)
  float moonDir[4];
  float moon[4];     // lux
  float count[4];    // x: how many lights
  float lp[16][4];   // position, range
  float lc[16][4];   // colour · candela
};
static_assert(sizeof(Uniforms) == 688, "std140");

inline const char* kWgsl = R"WGSL(
struct U {
  viewProj: mat4x4f,
  eye: vec4f,
  offset: vec4f,
  fog: vec4f,
  ambient: vec4f,
  moonDir: vec4f,
  moon: vec4f,
  count: vec4f,
  lp: array<vec4f, 16>,
  lc: array<vec4f, 16>,
};
@group(0) @binding(0) var<uniform> u: U;

struct VOut {
  @builtin(position) pos: vec4f,
  @location(0) world: vec3f,
  @location(1) normal: vec3f,
  @location(2) col: vec4f,
};

@vertex fn vs_main(@location(0) p: vec3f, @location(1) n: vec3f, @location(2) c: vec4f) -> VOut {
  var o: VOut;
  let w = p + u.offset.xyz;
  o.pos = u.viewProj * vec4f(w, 1.0);
  o.world = w;
  o.normal = n;
  o.col = c;
  return o;
}

fn aces(x: vec3f) -> vec3f { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3f(0.0), vec3f(1.0)); }
fn srgb(x: vec3f) -> vec3f { return select(1.055 * pow(x, vec3f(1.0 / 2.4)) - 0.055, 12.92 * x, x <= vec3f(0.0031308)); }

@fragment fn fs_main(v: VOut) -> @location(0) vec4f {
  let albedo = v.col.rgb * v.col.rgb;
  let kind = v.col.a * 255.0;
  if (kind > 252.0) {
    return vec4f(srgb(aces(albedo * u.eye.w)), 1.0);
  }
  var n = normalize(v.normal);
  let toEye = u.eye.xyz - v.world;
  if (dot(n, toEye) < 0.0) { n = -n; }
  var e = u.ambient.rgb * (0.65 + 0.35 * n.y) + u.moon.rgb * max(dot(n, u.moonDir.xyz), 0.0);
  let count = i32(u.count.x);
  for (var i = 0; i < 16; i = i + 1) {
    if (i >= count) { break; }
    let d = u.lp[i].xyz - v.world;
    let d2 = max(dot(d, d), 0.2);
    let l = d * inverseSqrt(d2);
    let r = sqrt(d2) / u.lp[i].w;
    let win = clamp(1.0 - r * r * r * r, 0.0, 1.0);
    e = e + u.lc[i].rgb * (max(dot(n, l) + 0.15, 0.0) / 1.15) / d2 * win * win;
  }
  var c = albedo * e / 3.14159265 * u.eye.w;
  if (kind > 0.5) { c = c + albedo * (kind / 250.0) * u.offset.w; }
  let f = exp(-u.fog.w * length(toEye));
  c = mix(u.fog.rgb * u.eye.w, c, f);
  return vec4f(srgb(aces(c)), 1.0);
}
)WGSL";

inline const char* kGlsl = R"GLSL(#version 300 es
precision highp float;
layout(std140) uniform U {
  mat4 viewProj;
  vec4 eye;
  vec4 offset;
  vec4 fog;
  vec4 ambient;
  vec4 moonDir;
  vec4 moon;
  vec4 count;
  vec4 lp[16];
  vec4 lc[16];
};
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
layout(location = 2) in vec4 c;
out vec3 vWorld; out vec3 vNormal; out vec4 vCol;
void main() {
  vec3 w = p + offset.xyz;
  gl_Position = viewProj * vec4(w, 1.0);
  vWorld = w; vNormal = n; vCol = c;
}
//--- fragment
#version 300 es
precision highp float;
layout(std140) uniform U {
  mat4 viewProj;
  vec4 eye;
  vec4 offset;
  vec4 fog;
  vec4 ambient;
  vec4 moonDir;
  vec4 moon;
  vec4 count;
  vec4 lp[16];
  vec4 lc[16];
};
in vec3 vWorld; in vec3 vNormal; in vec4 vCol;
out vec4 color;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 srgb(vec3 x) { return mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, 12.92 * x, vec3(lessThanEqual(x, vec3(0.0031308)))); }
void main() {
  vec3 albedo = vCol.rgb * vCol.rgb;
  float kind = vCol.a * 255.0;
  if (kind > 252.0) { color = vec4(srgb(aces(albedo * eye.w)), 1.0); return; }
  vec3 nn = normalize(vNormal);
  vec3 toEye = eye.xyz - vWorld;
  if (dot(nn, toEye) < 0.0) nn = -nn;
  vec3 e = ambient.rgb * (0.65 + 0.35 * nn.y) + moon.rgb * max(dot(nn, moonDir.xyz), 0.0);
  int cnt = int(count.x);
  for (int i = 0; i < 16; ++i) {
    if (i >= cnt) break;
    vec3 d = lp[i].xyz - vWorld;
    float d2 = max(dot(d, d), 0.2);
    vec3 l = d * inversesqrt(d2);
    float r = sqrt(d2) / lp[i].w;
    float win = clamp(1.0 - r * r * r * r, 0.0, 1.0);
    e += lc[i].rgb * (max(dot(nn, l) + 0.15, 0.0) / 1.15) / d2 * win * win;
  }
  vec3 col = albedo * e / 3.14159265 * eye.w;
  if (kind > 0.5) col += albedo * (kind / 250.0) * offset.w;
  float f = exp(-fog.w * length(toEye));
  col = mix(fog.rgb * eye.w, col, f);
  color = vec4(srgb(aces(col)), 1.0);
}
)GLSL";

}  // namespace game
