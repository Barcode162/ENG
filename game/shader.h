#pragma once
// The game's shaders, each written twice (WGSL for WebGPU, GLSL ES 3.0 for
// WebGL2), the same maths line for line.
//
// The scene (J120, J122.1):
//   light  = the sky's light (from above, and bounced up from the ground)
//          + the sun or moon (one direction), in its shadow or not
//          + up to 16 lights from the story's light words (candela / d², never
//            closer than 45 cm: a real flame has a size, so nothing burns white)
//   colour = surface · light / π · exposure + glow, the air over it,
//            then the eye's curve (ACES) and sRGB.
// Two kinds of sky: a story's fixed sky (its colours given), or the real
// one (J122.1: worked out from where the sun is: blue above, warm low down,
// a glow around the sun, its disc, stars at night), in real units (lux, nits).
// The sun's shadow: two maps from the sun's side (one sharp, 80 m around
// you; one wide, 700 m), each texel the nearest depth in two channels
// (22 bits); 9 taps blended for a soft edge.
// Vertex colours are stored as the square root of the linear colour;
// alpha says how it's lit: 0 by the lights, 1..250 glowing, 255 the sky.
//
// The shadow pass: the scene from the sun, writing only its depth.
namespace game {

struct Uniforms {
  float viewProj[16];
  float eye[4];      // xyz, exposure
  float offset[4];   // xyz added to every position (the sky follows you); w: glow strength
  float fog[4];      // the air's colour (linear, nits in the real sky), density
  float ambient[4];  // the sky's light (lux); w: 1 = the real sky
  float moonDir[4];  // towards the sun or moon
  float moon[4];     // its light (lux); w: 1 = its shadow is on
  float count[4];    // x: how many lights
  float lp[16][4];   // position, range
  float lc[16][4];   // colour · candela
  float shadow0[16]; // world → the sharp shadow map
  float shadow1[16]; // world → the wide shadow map
  float zenith[4];   // the real sky: straight up (nits)
  float horizon[4];  // at the horizon (nits)
  float sunGlow[4];  // around the sun (nits); w: the stars' strength
  float bounce[4];   // light coming up from the ground (lux)
  float texel[4];    // x, y: each shadow map's texel size (m); z: shadow map size (texels)
};
static_assert(sizeof(Uniforms) == 896, "std140");

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
  shadow0: mat4x4f,
  shadow1: mat4x4f,
  zenith: vec4f,
  horizon: vec4f,
  sunGlow: vec4f,
  bounce: vec4f,
  texel: vec4f,
};
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex0: texture_2d<f32>;
@group(0) @binding(2) var samp0: sampler;
@group(0) @binding(3) var tex1: texture_2d<f32>;
@group(0) @binding(4) var samp1: sampler;

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

fn depthAt(m: u32, p: vec2i) -> f32 {
  var t: vec4f;
  if (m == 0u) { t = textureLoad(tex0, p, 0); } else { t = textureLoad(tex1, p, 0); }
  return t.x + t.y / 2048.0;
}

// How much sun reaches this point: 1 lit, 0 in shadow, soft between.
fn sunSeen(world: vec3f, n: vec3f) -> f32 {
  let size = u.texel.z;
  for (var m = 0u; m < 2u; m = m + 1u) {
    let texel = select(u.texel.y, u.texel.x, m == 0u);
    let at = world + n * texel * 1.5;
    var s: vec4f;
    if (m == 0u) { s = u.shadow0 * vec4f(at, 1.0); } else { s = u.shadow1 * vec4f(at, 1.0); }
    let uv = vec2f(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (uv.x < 0.02 || uv.y < 0.02 || uv.x > 0.98 || uv.y > 0.98 || s.z > 1.0) { continue; }
    let f = uv * size - 0.5;
    let base = vec2i(floor(f));
    let fr = fract(f);
    let bias = texel * 0.0008;
    var lit = 0.0;
    var wsum = 0.0;
    for (var j = -1; j <= 2; j = j + 1) {
      for (var i = -1; i <= 2; i = i + 1) {
        let wx = select(select(1.0, fr.x, i == 2), 1.0 - fr.x, i == -1);
        let wy = select(select(1.0, fr.y, j == 2), 1.0 - fr.y, j == -1);
        let p = clamp(base + vec2i(i, j), vec2i(0), vec2i(i32(size) - 1));
        let d = depthAt(m, p);
        lit = lit + wx * wy * select(0.0, 1.0, s.z - bias <= d);
        wsum = wsum + wx * wy;
      }
    }
    return lit / wsum;
  }
  return 1.0;
}

// The real sky, looking along `dir`.
fn skyLight(dir: vec3f) -> vec3f {
  let up = max(dir.y, 0.0);
  var c = mix(u.horizon.rgb, u.zenith.rgb, pow(up, 0.45));
  let toSun = max(dot(dir, u.moonDir.xyz), 0.0);
  c = c + u.sunGlow.rgb * (pow(toSun, 6.0) * 0.35 + pow(toSun, 60.0) * 1.5);
  if (toSun > 0.99996 && u.moonDir.y > -0.05) { c = c + u.moon.rgb * 3.0; }
  if (dir.y < 0.0) { c = mix(u.horizon.rgb, u.bounce.rgb * 0.25, min(-dir.y * 4.0, 1.0)); }
  return c;
}

@fragment fn fs_main(v: VOut) -> @location(0) vec4f {
  let albedo = v.col.rgb * v.col.rgb;
  let kind = v.col.a * 255.0;
  let real = u.ambient.w > 0.5;
  let toEye = u.eye.xyz - v.world;
  let dist = length(toEye);
  if (kind > 252.0) {
    if (real) {
      // The dome is black but for its stars, shown as the sky darkens.
      let dir = -toEye / dist;
      let star = albedo.r * u.sunGlow.w;
      return vec4f(srgb(aces((skyLight(dir) + vec3f(star)) * u.eye.w)), 1.0);
    }
    return vec4f(srgb(aces(albedo * u.eye.w)), 1.0);
  }
  var n = normalize(v.normal);
  if (dot(n, toEye) < 0.0) { n = -n; }
  var e: vec3f;
  if (real) {
    e = u.ambient.rgb * (0.5 + 0.5 * n.y) + u.bounce.rgb * (0.5 - 0.5 * n.y);
  } else {
    e = u.ambient.rgb * (0.65 + 0.35 * n.y);
  }
  let sunCos = dot(n, u.moonDir.xyz);
  if (sunCos > 0.0) {
    var seen = 1.0;
    if (u.moon.w > 0.5) { seen = sunSeen(v.world, n); }
    e = e + u.moon.rgb * sunCos * seen;
  }
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
  let f = exp(-u.fog.w * dist);
  var air = u.fog.rgb;
  if (real) { air = skyLight(normalize(vec3f(-toEye.x, 0.02, -toEye.z))); }
  c = mix(air * u.eye.w, c, f);
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
  mat4 shadow0;
  mat4 shadow1;
  vec4 zenith;
  vec4 horizon;
  vec4 sunGlow;
  vec4 bounce;
  vec4 texel;
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
precision highp int;
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
  mat4 shadow0;
  mat4 shadow1;
  vec4 zenith;
  vec4 horizon;
  vec4 sunGlow;
  vec4 bounce;
  vec4 texel;
};
uniform sampler2D tex0;
uniform sampler2D tex1;
in vec3 vWorld; in vec3 vNormal; in vec4 vCol;
out vec4 color;
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }
vec3 srgb(vec3 x) { return mix(1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055, 12.92 * x, vec3(lessThanEqual(x, vec3(0.0031308)))); }
float depthAt(int m, ivec2 p) {
  vec4 t = m == 0 ? texelFetch(tex0, p, 0) : texelFetch(tex1, p, 0);
  return t.x + t.y / 2048.0;
}
float sunSeen(vec3 world, vec3 nn) {
  float size = texel.z;
  for (int m = 0; m < 2; ++m) {
    float tx = m == 0 ? texel.x : texel.y;
    vec3 at = world + nn * tx * 1.5;
    vec4 s = (m == 0 ? shadow0 : shadow1) * vec4(at, 1.0);
    vec2 uv = s.xy * 0.5 + 0.5;
    float z = s.z * 0.5 + 0.5;
    if (uv.x < 0.02 || uv.y < 0.02 || uv.x > 0.98 || uv.y > 0.98 || z > 1.0) continue;
    vec2 f = uv * size - 0.5;
    ivec2 base = ivec2(floor(f));
    vec2 fr = fract(f);
    float bias = tx * 0.0008;
    float lit = 0.0, wsum = 0.0;
    for (int j = -1; j <= 2; ++j)
      for (int i = -1; i <= 2; ++i) {
        float wx = i == -1 ? 1.0 - fr.x : (i == 2 ? fr.x : 1.0);
        float wy = j == -1 ? 1.0 - fr.y : (j == 2 ? fr.y : 1.0);
        ivec2 q = clamp(base + ivec2(i, j), ivec2(0), ivec2(int(size) - 1));
        lit += wx * wy * (z - bias <= depthAt(m, q) ? 1.0 : 0.0);
        wsum += wx * wy;
      }
    return lit / wsum;
  }
  return 1.0;
}
vec3 skyLight(vec3 dir) {
  float up = max(dir.y, 0.0);
  vec3 c = mix(horizon.rgb, zenith.rgb, pow(up, 0.45));
  float toSun = max(dot(dir, moonDir.xyz), 0.0);
  c += sunGlow.rgb * (pow(toSun, 6.0) * 0.35 + pow(toSun, 60.0) * 1.5);
  if (toSun > 0.99996 && moonDir.y > -0.05) c += moon.rgb * 3.0;
  if (dir.y < 0.0) c = mix(horizon.rgb, bounce.rgb * 0.25, min(-dir.y * 4.0, 1.0));
  return c;
}
void main() {
  vec3 albedo = vCol.rgb * vCol.rgb;
  float kind = vCol.a * 255.0;
  bool real = ambient.w > 0.5;
  vec3 toEye = eye.xyz - vWorld;
  float dist = length(toEye);
  if (kind > 252.0) {
    if (real) {
      vec3 dir = -toEye / dist;
      float star = albedo.r * sunGlow.w;
      color = vec4(srgb(aces((skyLight(dir) + vec3(star)) * eye.w)), 1.0);
      return;
    }
    color = vec4(srgb(aces(albedo * eye.w)), 1.0);
    return;
  }
  vec3 nn = normalize(vNormal);
  if (dot(nn, toEye) < 0.0) nn = -nn;
  vec3 e = real ? ambient.rgb * (0.5 + 0.5 * nn.y) + bounce.rgb * (0.5 - 0.5 * nn.y) : ambient.rgb * (0.65 + 0.35 * nn.y);
  float sunCos = dot(nn, moonDir.xyz);
  if (sunCos > 0.0) e += moon.rgb * sunCos * (moon.w > 0.5 ? sunSeen(vWorld, nn) : 1.0);
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
  float f = exp(-fog.w * dist);
  vec3 air = real ? skyLight(normalize(vec3(-toEye.x, 0.02, -toEye.z))) : fog.rgb;
  col = mix(air * eye.w, col, f);
  color = vec4(srgb(aces(col)), 1.0);
}
)GLSL";

// The shadow pass: the depth seen from the sun, in two channels.
inline const char* kShadowWgsl = R"WGSL(
struct U { viewProj: mat4x4f, eye: vec4f, offset: vec4f, };
@group(0) @binding(0) var<uniform> u: U;
struct VOut { @builtin(position) pos: vec4f, };
@vertex fn vs_main(@location(0) p: vec3f, @location(1) n: vec3f, @location(2) c: vec4f) -> VOut {
  var o: VOut;
  o.pos = u.viewProj * vec4f(p + u.offset.xyz, 1.0);
  return o;
}
@fragment fn fs_main(v: VOut) -> @location(0) vec4f {
  let d = clamp(v.pos.z, 0.0, 1.0);
  let hi = floor(d * 2048.0) / 2048.0;
  return vec4f(hi, (d - hi) * 2048.0, 0.0, 1.0);
}
)WGSL";

inline const char* kShadowGlsl = R"GLSL(#version 300 es
precision highp float;
layout(std140) uniform U { mat4 viewProj; vec4 eye; vec4 offset; };
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
layout(location = 2) in vec4 c;
void main() { gl_Position = viewProj * vec4(p + offset.xyz, 1.0); }
//--- fragment
#version 300 es
precision highp float;
out vec4 color;
void main() {
  float d = clamp(gl_FragCoord.z, 0.0, 1.0);
  float hi = floor(d * 2048.0) / 2048.0;
  color = vec4(hi, (d - hi) * 2048.0, 0.0, 1.0);
}
)GLSL";

}  // namespace game
