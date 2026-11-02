#include "world/land.h"

#include <algorithm>
#include <cmath>

namespace terrain {

namespace {

uint32_t hash(int x, int z, uint32_t s) {
  uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float smooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
uint8_t byte(float x) { return static_cast<uint8_t>(std::clamp(std::sqrt(std::max(x, 0.0f)) * 255.0f + 0.5f, 0.0f, 255.0f)); }

}  // namespace

Land::Land(const LandShape& s) : s_(s) {}

// Gradient noise: a random slope at every whole point, blended smoothly.
float Land::noise(float x, float z, uint32_t salt) const {
  const float fx = std::floor(x), fz = std::floor(z);
  const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
  const float dx = x - fx, dz = z - fz;
  auto grad = [&](int cx, int cz, float px, float pz) {
    const float a = static_cast<float>(hash(cx, cz, s_.seed + salt) & 0xffff) * (6.2831853f / 65536.0f);
    return std::cos(a) * px + std::sin(a) * pz;
  };
  const float n00 = grad(ix, iz, dx, dz), n10 = grad(ix + 1, iz, dx - 1, dz);
  const float n01 = grad(ix, iz + 1, dx, dz - 1), n11 = grad(ix + 1, iz + 1, dx - 1, dz - 1);
  const float u = fade(dx), v = fade(dz);
  return 1.41f * ((n00 + (n10 - n00) * u) + ((n01 + (n11 - n01) * u) - (n00 + (n10 - n00) * u)) * v);
}

float Land::fbm(float x, float z, int octaves, uint32_t salt) const {
  float sum = 0, amp = 0.5f, total = 0;
  for (int o = 0; o < octaves; ++o) {
    sum += amp * noise(x, z, salt + static_cast<uint32_t>(o) * 101u);
    total += amp;
    // Each octave turned a little, so the grid never lines up.
    const float nx = x * 1.6f - z * 1.2f, nz = x * 1.2f + z * 1.6f;
    x = nx + 17.3f, z = nz - 9.1f;
    amp *= 0.5f;
  }
  return sum / total;
}

// Sharp crests and soft valleys: what mountain ranges look like.
float Land::ridged(float x, float z, int octaves, uint32_t salt) const {
  float sum = 0, amp = 0.5f, total = 0, weight = 1;
  for (int o = 0; o < octaves; ++o) {
    float r = 1 - std::fabs(noise(x, z, salt + static_cast<uint32_t>(o) * 131u));
    r *= r;
    sum += amp * r * weight;
    weight = std::clamp(r * 1.6f, 0.0f, 1.0f);
    total += amp;
    const float nx = x * 1.7f - z * 1.1f, nz = x * 1.1f + z * 1.7f;
    x = nx + 5.7f, z = nz + 3.3f;
    amp *= 0.42f;
  }
  return sum / total;
}

float Land::height(float x, float z) const {
  const float half = s_.size / 2;
  // The coast: a circle pushed in and out by slow noise.
  const float r = std::sqrt(x * x + z * z) / half + 0.22f * fbm(x / 1400, z / 1400, 3, 11);
  const float island = smooth(1.0f, 0.62f, r);
  // Rolling hills everywhere, mountains where the mask is high.
  const float hills = (fbm(x / 700, z / 700, 5, 23) * 0.5f + 0.5f) * s_.hills;
  const float mask = smooth(0.05f, 0.45f, fbm(x / 2200, z / 2200, 3, 37) + 0.15f * (1 - r));
  const float peaks = std::pow(ridged(x / 1500, z / 1500, 6, 53), 1.35f) * s_.mountains * mask;
  // Small bumps underfoot.
  const float bumps = fbm(x / 18, z / 18, 3, 71) * 0.9f;
  const float land = 4 + hills + peaks + bumps;
  return s_.sea + island * land - (1 - island) * 28;
}

Vec3 Land::normal(float x, float z, float e) const {
  const float hx = height(x + e, z) - height(x - e, z);
  const float hz = height(x, z + e) - height(x, z - e);
  return engine::normalize(Vec3{-hx, 2 * e, -hz});
}

Vec3 Land::colour(float x, float z, float h, Vec3 n) const {
  const float slope = 1 - n.y;  // 0 flat, 1 a cliff
  const float jit = fbm(x / 9, z / 9, 2, 97) * 0.5f + 0.5f;
  const float patch = fbm(x / 120, z / 120, 3, 89) * 0.5f + 0.5f;
  // Sea-washed sand, then grass (lush or dry in patches), then rock and snow.
  const Vec3 sand{0.36f, 0.31f, 0.21f}, grass{0.07f, 0.12f, 0.035f}, dry{0.16f, 0.15f, 0.06f}, forest{0.04f, 0.07f, 0.03f};
  const Vec3 rock{0.13f, 0.12f, 0.11f}, rock2{0.2f, 0.18f, 0.16f}, snow{0.72f, 0.75f, 0.8f}, seabed{0.1f, 0.1f, 0.08f};
  Vec3 g = grass * (1 - patch) + dry * patch;
  g = g * (1 - smooth(0.4f, 0.8f, jit) * 0.4f) + forest * (smooth(0.4f, 0.8f, jit) * 0.4f);
  const float beach = smooth(s_.sea + 4.0f, s_.sea + 1.0f, h + jit * 1.5f);
  Vec3 c = g * (1 - beach) + sand * beach;
  if (h < s_.sea - 1) c = seabed;
  const float rocky = smooth(0.28f, 0.5f, slope + (jit - 0.5f) * 0.15f) + smooth(320, 520, h) * 0.6f;
  const Vec3 r = rock * (1 - jit) + rock2 * jit;
  c = c * (1 - std::min(rocky, 1.0f)) + r * std::min(rocky, 1.0f);
  const float snowy = smooth(560, 700, h + patch * 80) * (1 - smooth(0.45f, 0.65f, slope));
  c = c * (1 - snowy) + snow * snowy;
  return c * (0.85f + 0.3f * jit);
}

// ---- Drawing ----

float LandDraw::squareSize(int level) const { return land_.shape().size / static_cast<float>(1 << level); }

void LandDraw::visit(int level, int x, int z, Vec3 eye, std::vector<Key>& out) const {
  const float size = squareSize(level), half = land_.shape().size / 2;
  const float x0 = -half + static_cast<float>(x) * size, z0 = -half + static_cast<float>(z) * size;
  // How far you are from the square (flat distance, plus how high you are above it).
  const float dx = std::max({x0 - eye.x, 0.0f, eye.x - (x0 + size)}), dz = std::max({z0 - eye.z, 0.0f, eye.z - (z0 + size)});
  const float dy = std::max(0.0f, eye.y - land_.shape().mountains);
  const float dist = std::sqrt(dx * dx + dz * dz + dy * dy);
  if (size > kFinest && dist < size * 0.8f) {
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) visit(level + 1, x * 2 + i, z * 2 + j, eye, out);
    return;
  }
  out.push_back({level, x, z});
}

bool LandDraw::pick(Vec3 eye) {
  const Vec3 d = eye - lastEye_;
  if (engine::dot(d, d) < 6 * 6 && !picked_.empty()) return false;
  lastEye_ = eye;
  std::vector<Key> keys;
  visit(0, 0, 0, eye, keys);
  if (keys.size() == picked_.size() && std::equal(keys.begin(), keys.end(), picked_.begin(), [](const Key& a, const Key& b) {
        return !(a < b) && !(b < a);
      }))
    return false;
  picked_ = std::move(keys);
  return true;
}

const game::Mesh& LandDraw::square(const Key& k) {
  used_[k] = ++tick_;
  auto it = cache_.find(k);
  if (it != cache_.end()) return it->second;
  // Forget the squares not seen for longest when the cache grows.
  if (cache_.size() > 600) {
    std::vector<std::pair<uint32_t, Key>> ages;
    for (const auto& [key, t] : used_) ages.push_back({t, key});
    std::sort(ages.begin(), ages.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < 200 && i < ages.size(); ++i) cache_.erase(ages[i].second), used_.erase(ages[i].second);
  }
  game::Mesh& m = cache_[k];
  const int n = kGrid;
  const float size = squareSize(k.level), step = size / static_cast<float>(n), half = land_.shape().size / 2;
  const float x0 = -half + static_cast<float>(k.x) * size, z0 = -half + static_cast<float>(k.z) * size;
  auto vertex = [&](float x, float z, float drop) {
    const float h = land_.height(x, z);
    const Vec3 nrm = land_.normal(x, z, std::max(step * 0.5f, 0.5f));
    const Vec3 c = land_.colour(x, z, h, nrm);
    game::Vertex v{};
    v.pos[0] = x, v.pos[1] = h - drop, v.pos[2] = z;
    v.nrm[0] = nrm.x, v.nrm[1] = nrm.y, v.nrm[2] = nrm.z;
    v.col[0] = byte(c.x), v.col[1] = byte(c.y), v.col[2] = byte(c.z), v.col[3] = 0;
    m.v.push_back(v);
  };
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i) vertex(x0 + static_cast<float>(i) * step, z0 + static_cast<float>(j) * step, 0);
  const uint32_t w = static_cast<uint32_t>(n + 1);
  for (uint32_t j = 0; j < static_cast<uint32_t>(n); ++j)
    for (uint32_t i = 0; i < static_cast<uint32_t>(n); ++i) {
      const uint32_t a = j * w + i, b = a + 1, c = a + w, d = c + 1;
      m.i.insert(m.i.end(), {a, c, b, b, c, d});
    }
  // The skirt: each edge again, hanging down, joined to the edge above it.
  const float drop = step * 3 + 2;
  auto skirt = [&](uint32_t from, int stride) {
    const uint32_t base = static_cast<uint32_t>(m.v.size());
    for (int k = 0; k <= n; ++k) {
      game::Vertex v = m.v[from + static_cast<uint32_t>(k * stride)];
      v.pos[1] -= drop;
      m.v.push_back(v);
    }
    for (uint32_t k = 0; k < static_cast<uint32_t>(n); ++k) {
      const uint32_t t0 = from + static_cast<uint32_t>(static_cast<int>(k) * stride), t1 = from + static_cast<uint32_t>(static_cast<int>(k + 1) * stride);
      m.i.insert(m.i.end(), {t0, base + k, t1, t1, base + k, base + k + 1});
    }
  };
  skirt(0, 1);
  skirt(static_cast<uint32_t>(n) * w, 1);
  skirt(0, static_cast<int>(w));
  skirt(static_cast<uint32_t>(n), static_cast<int>(w));
  return m;
}

void LandDraw::mesh(game::Mesh& out) {
  out.v.clear(), out.i.clear();
  for (const Key& k : picked_) {
    const game::Mesh& s = square(k);
    const uint32_t base = static_cast<uint32_t>(out.v.size());
    out.v.insert(out.v.end(), s.v.begin(), s.v.end());
    for (uint32_t i : s.i) out.i.push_back(base + i);
  }
  // The sea, out to the horizon.
  const float r = land_.shape().size * 3, y = land_.shape().sea;
  const Vec3 water{0.012f, 0.03f, 0.05f};
  const uint32_t base = static_cast<uint32_t>(out.v.size());
  for (Vec3 p : {Vec3{-r, y, -r}, Vec3{r, y, -r}, Vec3{r, y, r}, Vec3{-r, y, r}}) {
    game::Vertex v{};
    v.pos[0] = p.x, v.pos[1] = p.y, v.pos[2] = p.z;
    v.nrm[1] = 1;
    v.col[0] = byte(water.x), v.col[1] = byte(water.y), v.col[2] = byte(water.z);
    out.v.push_back(v);
  }
  out.i.insert(out.i.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
}

LandShape shapeOf(const story::Place& p) {
  LandShape s;
  s.seed = p.landSeed, s.size = p.groundW;
  s.mountains = std::min(900.0f, p.groundW * 0.15f);
  return s;
}

void settle(story::Story& s) {
  for (story::Place& p : s.places) {
    if (!p.hasLand) continue;
    const Land land(shapeOf(p));
    for (story::Placed& t : p.things) t.at.y += land.height(t.at.x, t.at.z);
    p.start.y = land.height(p.start.x, p.start.z);
  }
}

}  // namespace terrain
