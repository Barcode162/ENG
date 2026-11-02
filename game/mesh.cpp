#include "game/mesh.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {

constexpr float kPi = 3.14159265f;

uint8_t byte(float x) { return static_cast<uint8_t>(std::clamp(std::sqrt(std::max(x, 0.0f)) * 255.0f + 0.5f, 0.0f, 255.0f)); }

struct Paint {
  Vec3 colour;
  uint8_t glow = 0;
};

// One flat triangle (both sides are drawn; the shader faces it towards the eye).
void tri(Mesh& m, Vec3 a, Vec3 b, Vec3 c, const Paint& p) {
  Vec3 n = engine::cross(b - a, c - a);
  const float len = engine::length(n);
  n = len > 1e-9f ? n / len : Vec3{0, 1, 0};
  const uint32_t base = static_cast<uint32_t>(m.v.size());
  for (Vec3 q : {a, b, c}) {
    Vertex v{};
    v.pos[0] = q.x, v.pos[1] = q.y, v.pos[2] = q.z;
    v.nrm[0] = n.x, v.nrm[1] = n.y, v.nrm[2] = n.z;
    v.col[0] = byte(p.colour.x), v.col[1] = byte(p.colour.y), v.col[2] = byte(p.colour.z), v.col[3] = p.glow;
    m.v.push_back(v);
  }
  m.i.insert(m.i.end(), {base, base + 1, base + 2});
}
// A sky triangle: each corner its own colour (a smooth gradient), lit by nothing.
void skyTri(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 ca, Vec3 cb, Vec3 cc) {
  const uint32_t base = static_cast<uint32_t>(m.v.size());
  const Vec3 q[3] = {a, b, c}, col[3] = {ca, cb, cc};
  for (int k = 0; k < 3; ++k) {
    Vertex v{};
    v.pos[0] = q[k].x, v.pos[1] = q[k].y, v.pos[2] = q[k].z;
    v.nrm[1] = 1;
    v.col[0] = byte(col[k].x), v.col[1] = byte(col[k].y), v.col[2] = byte(col[k].z), v.col[3] = 255;
    m.v.push_back(v);
  }
  m.i.insert(m.i.end(), {base, base + 1, base + 2});
}
void quad(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, const Paint& p) {
  tri(m, a, b, c, p);
  tri(m, a, c, d, p);
}

// A shape in its own space, then moved by `f` into the world.
template <class F>
void box(Mesh& m, Vec3 s, const Paint& p, F f) {
  const float x = s.x / 2, z = s.y / 2, h = s.z;
  const Vec3 c[8] = {{-x, 0, -z}, {x, 0, -z}, {x, 0, z}, {-x, 0, z}, {-x, h, -z}, {x, h, -z}, {x, h, z}, {-x, h, z}};
  Vec3 w[8];
  for (int i = 0; i < 8; ++i) w[i] = f(c[i]);
  quad(m, w[0], w[1], w[2], w[3], p);
  quad(m, w[4], w[7], w[6], w[5], p);
  quad(m, w[0], w[4], w[5], w[1], p);
  quad(m, w[1], w[5], w[6], w[2], p);
  quad(m, w[2], w[6], w[7], w[3], p);
  quad(m, w[3], w[7], w[4], w[0], p);
}

// A cylinder from radius r0 (bottom) to r1 (top), `sides` sides; capped.
template <class F>
void frustum(Mesh& m, float rx0, float rz0, float rx1, float rz1, float h, int sides, const Paint& p, F f, float y0 = 0) {
  for (int i = 0; i < sides; ++i) {
    const float a0 = 2 * kPi * static_cast<float>(i) / static_cast<float>(sides), a1 = 2 * kPi * static_cast<float>(i + 1) / static_cast<float>(sides);
    const Vec3 b0 = f({std::cos(a0) * rx0, y0, std::sin(a0) * rz0}), b1 = f({std::cos(a1) * rx0, y0, std::sin(a1) * rz0});
    const Vec3 t0 = f({std::cos(a0) * rx1, y0 + h, std::sin(a0) * rz1}), t1 = f({std::cos(a1) * rx1, y0 + h, std::sin(a1) * rz1});
    if (rx1 > 1e-4f) quad(m, b0, b1, t1, t0, p);
    else tri(m, b0, b1, t0, p);
    tri(m, f({0, y0, 0}), b1, b0, p);
    if (rx1 > 1e-4f) tri(m, f({0, y0 + h, 0}), t0, t1, p);
  }
}

template <class F>
void ball(Mesh& m, float rx, float rz, float h, const Paint& p, F f, int seg = 8, int rings = 6) {
  auto at = [&](int i, int j) {
    const float th = kPi * static_cast<float>(j) / static_cast<float>(rings);
    const float ph = 2 * kPi * static_cast<float>(i) / static_cast<float>(seg);
    return f({std::cos(ph) * std::sin(th) * rx, h / 2 - std::cos(th) * h / 2, std::sin(ph) * std::sin(th) * rz});
  };
  for (int j = 0; j < rings; ++j)
    for (int i = 0; i < seg; ++i) {
      const Vec3 a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
      if (j == 0) tri(m, a, c, d, p);
      else if (j == rings - 1) tri(m, a, b, d, p);
      else quad(m, a, b, c, d, p);
    }
}

template <class F>
void roof(Mesh& m, Vec3 s, const Paint& p, F f) {
  const float x = s.x / 2, z = s.y / 2, h = s.z;
  const Vec3 a = f({-x, 0, z}), b = f({x, 0, z}), c = f({x, 0, -z}), d = f({-x, 0, -z}), e = f({-x, h, 0}), g = f({x, h, 0});
  quad(m, a, b, g, e, p);
  quad(m, c, d, e, g, p);
  tri(m, a, e, d, p);
  tri(m, b, c, g, p);
  quad(m, a, d, c, b, p);
}

}  // namespace

Vec3 Place3::apply(Vec3 p) const {
  const float a = turn * 0.017453292f, c = std::cos(a), s = std::sin(a);
  p = p * scale;
  return at + Vec3{p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
}
Vec3 Place3::rotate(Vec3 n) const {
  const float a = turn * 0.017453292f, c = std::cos(a), s = std::sin(a);
  return {n.x * c + n.z * s, n.y, -n.x * s + n.z * c};
}

void addPart(Mesh& m, const story::Part& p, const Place3& where, bool lit) {
  if (p.onlyLit && !lit) return;
  Paint paint{p.colour, static_cast<uint8_t>(p.glows && lit ? 200 : 0)};
  if (p.glows && !lit) paint.colour = paint.colour * 0.18f;  // a dark window, a cold lamp
  Place3 local;
  local.at = p.at, local.turn = p.turn;
  auto f = [&](Vec3 q) { return where.apply(local.apply(q)); };
  using story::Shape;
  switch (p.shape) {
    case Shape::kBox: box(m, p.size, paint, f); break;
    case Shape::kRoof: roof(m, p.size, paint, f); break;
    case Shape::kPost: frustum(m, p.size.x / 2, p.size.y / 2, p.size.x / 2, p.size.y / 2, p.size.z, p.size.x > 1.5f ? 16 : 10, paint, f); break;
    case Shape::kCone: frustum(m, p.size.x / 2, p.size.y / 2, 0, 0, p.size.z, 9, paint, f); break;
    case Shape::kDisc: frustum(m, p.size.x / 2, p.size.y / 2, p.size.x / 2, p.size.y / 2, p.size.z, 18, paint, f); break;
    case Shape::kBall: ball(m, p.size.x / 2, p.size.y / 2, p.size.z, paint, f, p.size.x > 2 ? 10 : 8, p.size.x > 2 ? 7 : 5); break;
  }
}

void addKind(Mesh& m, const story::Kind& k, const Place3& where, bool lit) {
  for (const story::Part& p : k.parts) addPart(m, p, where, lit);
}

void addPerson(Mesh& m, const story::Person& p, const Place3& where) {
  const float h = p.height;
  const uint8_t glow = p.spirit ? 90 : 0;
  auto f = [&](Vec3 q) { return where.apply(q); };
  const Paint robe{p.robe, glow}, skin{p.skin, glow}, hat{p.hat, glow}, eye{{0.02f, 0.02f, 0.03f}, 0};
  // Robe to the shoulders, a little narrower at the top; arms as a short wide cone.
  frustum(m, 0.24f * h, 0.2f * h, 0.13f * h, 0.11f * h, 0.66f * h, 9, robe, f);
  frustum(m, 0.2f * h, 0.15f * h, 0.07f * h, 0.07f * h, 0.16f * h, 9, robe, f, 0.5f * h);
  // Head.
  const float hr = 0.11f * h, hy = 0.7f * h;
  ball(m, hr, hr, 2 * hr, skin, [&](Vec3 q) { return f(q + Vec3{0, hy, 0}); }, 9, 6);
  // Eyes, looking ahead (+z).
  for (float s : {-1.0f, 1.0f})
    ball(m, 0.018f * h, 0.018f * h, 0.036f * h, eye, [&](Vec3 q) { return f(q + Vec3{s * 0.038f * h, hy + hr * 1.05f, hr * 0.9f}); }, 5, 3);
  switch (p.hatShape) {
    case story::Hat::kHair: ball(m, hr * 1.08f, hr * 1.08f, hr * 1.3f, hat, [&](Vec3 q) { return f(q + Vec3{0, hy + hr * 0.95f, -hr * 0.12f}); }, 9, 4); break;
    case story::Hat::kHood:
      ball(m, hr * 1.22f, hr * 1.22f, hr * 2.3f, hat, [&](Vec3 q) { return f(q + Vec3{0, hy - hr * 0.1f, -hr * 0.3f}); }, 9, 5);
      frustum(m, 0.15f * h, 0.12f * h, 0.11f * h, 0.09f * h, 0.12f * h, 9, hat, f, 0.6f * h);
      break;
    case story::Hat::kPointed:
      frustum(m, hr * 1.5f, hr * 1.5f, hr * 1.5f, hr * 1.5f, 0.012f * h, 12, hat, f, hy + hr * 1.6f);
      frustum(m, hr * 0.95f, hr * 0.95f, 0, 0, 0.32f * h, 9, hat, f, hy + hr * 1.6f);
      break;
    case story::Hat::kNone: break;
  }
}

void addGround(Mesh& m, const story::Place& pl) {
  const float w = pl.groundW, d = pl.groundD, step = 2;
  const int nx = std::max(1, static_cast<int>(w / step)), nz = std::max(1, static_cast<int>(d / step));
  uint32_t seed = 977;
  for (int j = 0; j < nz; ++j)
    for (int i = 0; i < nx; ++i) {
      seed = seed * 1664525u + 1013904223u;
      const float jit = 0.85f + 0.3f * static_cast<float>(seed >> 8) / 16777216.0f;
      const float x0 = -w / 2 + w * static_cast<float>(i) / static_cast<float>(nx), x1 = -w / 2 + w * static_cast<float>(i + 1) / static_cast<float>(nx);
      const float z0 = -d / 2 + d * static_cast<float>(j) / static_cast<float>(nz), z1 = -d / 2 + d * static_cast<float>(j + 1) / static_cast<float>(nz);
      quad(m, {x0, 0, z0}, {x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}, Paint{pl.ground * jit, 0});
    }
  // A dark rim beyond the edge, so the world doesn't end in a cliff of sky.
  const float r = 120;
  const Paint rim{pl.ground * 0.5f, 0};
  quad(m, {-r, -0.02f, -r}, {-r, -0.02f, -d / 2}, {r, -0.02f, -d / 2}, {r, -0.02f, -r}, rim);
  quad(m, {-r, -0.02f, d / 2}, {-r, -0.02f, r}, {r, -0.02f, r}, {r, -0.02f, d / 2}, rim);
  quad(m, {-r, -0.02f, -d / 2}, {-r, -0.02f, d / 2}, {-w / 2, -0.02f, d / 2}, {-w / 2, -0.02f, -d / 2}, rim);
  quad(m, {w / 2, -0.02f, -d / 2}, {w / 2, -0.02f, d / 2}, {r, -0.02f, d / 2}, {r, -0.02f, -d / 2}, rim);
}

void addSky(Mesh& m, const story::Sky& sky, uint32_t seed) {
  // A dome around the eye (the game keeps it centred on you): its colour from the horizon to the top.
  const int seg = 24, rings = 10;
  const float R = 150;
  auto at = [&](int i, int j) {
    const float th = (kPi / 2) * static_cast<float>(j) / static_cast<float>(rings);  // 0 top .. pi/2 horizon
    const float ph = 2 * kPi * static_cast<float>(i) / static_cast<float>(seg);
    return Vec3{std::cos(ph) * std::sin(th) * R, std::cos(th) * R - 10, std::sin(ph) * std::sin(th) * R};
  };
  auto colour = [&](int j) {
    const float t = static_cast<float>(j) / static_cast<float>(rings);
    return sky.top * (1 - t * t) + sky.horizon * (t * t);
  };
  for (int j = 0; j < rings; ++j) {
    const Vec3 c0 = colour(j), c1 = colour(j + 1);
    for (int i = 0; i < seg; ++i) {
      skyTri(m, at(i, j), at(i + 1, j), at(i + 1, j + 1), c0, c0, c1);
      skyTri(m, at(i, j), at(i + 1, j + 1), at(i, j + 1), c0, c1, c1);
    }
  }
  // A skirt below the horizon in the horizon's colour.
  for (int i = 0; i < seg; ++i) {
    const Vec3 a = at(i, rings), b = at(i + 1, rings);
    quad(m, a, b, b + Vec3{0, -60, 0}, a + Vec3{0, -60, 0}, Paint{sky.horizon, 255});
  }
  if (sky.stars) {
    for (int k = 0; k < 260; ++k) {
      seed = seed * 1664525u + 1013904223u;
      const float u = static_cast<float>(seed >> 8) / 16777216.0f;
      seed = seed * 1664525u + 1013904223u;
      const float v = static_cast<float>(seed >> 8) / 16777216.0f;
      seed = seed * 1664525u + 1013904223u;
      const float b = 0.3f + 0.7f * static_cast<float>(seed >> 8) / 16777216.0f;
      const float ph = 2 * kPi * u, th = std::acos(1 - v * 0.85f);
      const Vec3 dir{std::cos(ph) * std::sin(th), std::cos(th), std::sin(ph) * std::sin(th)};
      const Vec3 c = dir * (R * 0.97f) + Vec3{0, -10, 0};
      const Vec3 e1 = engine::normalize(engine::cross(dir, {0, 1, 0.001f})), e2 = engine::cross(dir, e1);
      const float s = 0.16f + 0.2f * b;
      tri(m, c + e1 * s, c + e2 * s, c - e1 * s, Paint{Vec3{1, 0.95f, 0.85f} * (0.06f * b), 255});
      tri(m, c - e1 * s, c - e2 * s, c + e1 * s, Paint{Vec3{1, 0.95f, 0.85f} * (0.06f * b), 255});
    }
  }
}

void buildPlace(Mesh& m, const story::Story& s, const story::Runner& r) {
  m.v.clear(), m.i.clear();
  if (r.place() < 0) return;
  const story::Place& pl = s.places[static_cast<size_t>(r.place())];
  addGround(m, pl);
  for (size_t t = 0; t < pl.things.size(); ++t) {
    if (r.hidden(static_cast<int>(t))) continue;
    const story::Placed& th = pl.things[t];
    Place3 w;
    w.at = th.at, w.turn = th.turn, w.scale = th.scale;
    if (!th.person.empty()) {
      continue;  // people are drawn on their own (they turn to face you)
    } else if (const story::Kind* k = s.kind(th.kind)) {
      addKind(m, *k, w, r.lit(static_cast<int>(t)));
    }
  }
}

}  // namespace game
