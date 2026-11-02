// The Last Ember, on Looped: the story (story/, Looped Script's story words)
// drawn by the engine's drawing piece (looped/draw/, WebGPU or WebGL2) and
// lit by the engine's light words (looped/words/: every lantern, hearth and
// wisp is a phrase like "a lantern flame that flickers gently", worked out
// each frame). You walk with the left thumb and look with the right; the
// page (web/index.html) shows what's said, the choices and what you carry,
// and tells the story what you did.
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/mesh.h"
#include "game/shader.h"
#include "looped/draw/draw.h"
#include "looped/math.h"
#include "looped/world/loop.h"
#include "story/run.h"
#include "story/story.h"

using namespace engine;

namespace {

constexpr float kEye = 1.6f, kRadius = 0.3f, kWalk = 3.4f;

struct Buffers {
  draw::Handle vb = 0, ib = 0;
  size_t vCap = 0, iCap = 0;
  uint32_t count = 0;
};

struct PersonDraw {
  int thing = -1;
  Buffers buf;
  float yaw = 0, rest = 0;
  bool spirit = false;
};

struct Light {
  Vec3 at, colour;
  float cd = 0;
  bool mine = false;
};

struct Near {
  int thing = -1;
  story::Handler::Verb verb = story::Handler::kLook;
  std::string label;
};

struct Game {
  std::unique_ptr<draw::Device> dev;
  draw::Handle pipe = 0;
  std::map<std::string, std::string> files;
  std::string engineWords;
  story::Story story;
  std::unique_ptr<story::Runner> run;
  Buffers place, sky;
  std::vector<PersonDraw> people;
  uint32_t built = 0xffffffff;
  int skyFor = -2;
  Vec3 eye{0, kEye, 0};
  float yaw = 0, pitch = -4;
  float moveX = 0, moveY = 0;
  double last = 0;
  std::vector<Near> near;
  bool ready = false;
  std::string out;
};
Game g;

EM_JS(void, js_status, (const char* backend, const char* problem), {
  if (window['gameStatus']) window['gameStatus'](UTF8ToString(backend), UTF8ToString(problem));
});
EM_JS(void, js_lights, (const char* text), { if (/[?&]debug=1/.test(location.search)) console.log(UTF8ToString(text)); });
EM_JS(int, js_forceGl, (), { return /[?&]gl=1(&|$)/.test(location.search) ? 1 : 0; });

void upload(Buffers& b, const game::Mesh& m) {
  const size_t vb = m.v.size() * sizeof(game::Vertex);
  if (m.v.size() > b.vCap || !b.vb) {
    b.vCap = std::max<size_t>(m.v.size() + m.v.size() / 2, 1024);
    std::vector<game::Vertex> pad(b.vCap);
    std::copy(m.v.begin(), m.v.end(), pad.begin());
    b.vb = g.dev->vertexBuffer(pad.data(), pad.size() * sizeof(game::Vertex));
  } else if (vb) {
    g.dev->updateVertices(b.vb, 0, m.v.data(), vb);
  }
  if (m.i.size() > b.iCap || !b.ib) {
    b.iCap = std::max<size_t>(m.i.size() + m.i.size() / 2, 1024);
    std::vector<uint32_t> pad(b.iCap, 0);
    std::copy(m.i.begin(), m.i.end(), pad.begin());
    b.ib = g.dev->indexBuffer(pad.data(), pad.size());
  } else if (!m.i.empty()) {
    g.dev->updateIndices(b.ib, m.i.data(), m.i.size());
  }
  b.count = static_cast<uint32_t>(m.i.size());
}

const story::Place& here() { return g.story.places[static_cast<size_t>(g.run->place())]; }

void rebuild() {
  game::Mesh m;
  game::buildPlace(m, g.story, *g.run);
  upload(g.place, m);
  if (g.skyFor != g.run->place()) {
    g.skyFor = g.run->place();
    game::Mesh s;
    game::addSky(s, here().sky, 1234u + static_cast<uint32_t>(g.skyFor));
    upload(g.sky, s);
    // The people here, each drawn on its own so they can turn to you.
    g.people.clear();
    const story::Place& pl = here();
    for (size_t t = 0; t < pl.things.size(); ++t) {
      if (pl.things[t].person.empty()) continue;
      const story::Person* p = g.story.person(pl.things[t].person);
      if (!p) continue;
      PersonDraw d;
      d.thing = static_cast<int>(t);
      d.rest = d.yaw = pl.things[t].turn;
      d.spirit = p->spirit;
      game::Mesh pm;
      game::addPerson(pm, *p, game::Place3{});
      upload(d.buf, pm);
      g.people.push_back(d);
    }
  }
  g.built = g.run->changes();
}

// ---- Walking ----

// Pushes `p` out of everything solid here.
void collide(Vec3& p) {
  const story::Place& pl = here();
  for (size_t t = 0; t < pl.things.size(); ++t) {
    if (g.run->hidden(static_cast<int>(t))) continue;
    const story::Placed& th = pl.things[t];
    if (!th.person.empty()) {
      const Vec3 d{p.x - th.at.x, 0, p.z - th.at.z};
      const float l = length(d), r = 0.45f + kRadius;
      if (l < r && l > 1e-4f) p = p + d * ((r - l) / l);
      continue;
    }
    const story::Kind* k = g.story.kind(th.kind);
    if (!k) continue;
    if (k->wall) {
      float x0, z0, x1, z1;
      story::bounds(*k, x0, z0, x1, z1);
      const float a = -th.turn * 0.017453292f, c = std::cos(a), s = std::sin(a);
      const Vec3 d = p - th.at;
      float lx = (d.x * c + d.z * s) / th.scale, lz = (-d.x * s + d.z * c) / th.scale;
      const float m = kRadius / th.scale;
      if (lx > x0 - m && lx < x1 + m && lz > z0 - m && lz < z1 + m) {
        const float push[4] = {lx - (x0 - m), (x1 + m) - lx, lz - (z0 - m), (z1 + m) - lz};
        const int i = static_cast<int>(std::min_element(push, push + 4) - push);
        if (i == 0) lx = x0 - m;
        else if (i == 1) lx = x1 + m;
        else if (i == 2) lz = z0 - m;
        else lz = z1 + m;
        // Back to the world (the inverse turn).
        const float wx = (lx * c - lz * s) * th.scale, wz = (lx * s + lz * c) * th.scale;
        p.x = th.at.x + wx, p.z = th.at.z + wz;
      }
      continue;
    }
    const float r = story::blockRadius(*k) * th.scale;
    if (r <= 0) continue;
    const Vec3 d{p.x - th.at.x, 0, p.z - th.at.z};
    const float l = length(d), rr = r + kRadius;
    if (l < rr && l > 1e-4f) p = p + d * ((rr - l) / l);
  }
  const float hw = pl.groundW / 2 - 1, hd = pl.groundD / 2 - 1;
  p.x = std::clamp(p.x, -hw, hw), p.z = std::clamp(p.z, -hd, hd);
}

// What you can do from where you stand: the closest thing in front of you
// that the story has something for.
void findNear() {
  g.near.clear();
  if (g.run->waiting()) return;
  const auto acts = g.run->actions();
  const story::Place& pl = here();
  const float yr = g.yaw * 0.017453292f;
  const Vec3 fwd{std::sin(yr), 0, -std::cos(yr)};
  int best = -1;
  float bestD = 1e9f;
  for (const auto& a : acts) {
    const story::Placed& th = pl.things[static_cast<size_t>(a.thing)];
    float r = 0.45f;
    if (const story::Kind* k = th.person.empty() ? g.story.kind(th.kind) : nullptr) r = k->wall ? 1.5f : std::max(0.4f, story::blockRadius(*k) * th.scale);
    if (!th.way.empty()) r = 1.2f;
    const Vec3 d{th.at.x - g.eye.x, 0, th.at.z - g.eye.z};
    const float dist = std::max(0.0f, length(d) - r);
    if (dist > 2.4f) continue;
    const float facing = length(d) > 1e-3f ? dot(d / length(d), fwd) : 1;
    if (dist > 0.8f && facing < 0.3f) continue;
    const float score = dist - facing * 0.6f;
    if (score < bestD) bestD = score, best = a.thing;
  }
  if (best < 0) return;
  for (const auto& a : acts)
    if (a.thing == best) g.near.push_back({a.thing, a.verb, a.label});
}

// ---- Lights ----

void gather(std::vector<Light>& out, double now) {
  world::Clock clock;
  clock.seconds = now;
  std::vector<words::Words::Glow> glows;
  auto add = [&](const story::LightPart& lp, const game::Place3& w, uint32_t seed, bool mine) {
    glows.clear();
    g.story.words.lights(lp.thing, clock, glows, {}, seed);
    const Vec3 base = w.apply(lp.at);
    for (const auto& gl : glows)
      out.push_back({base + w.rotate(gl.at) * w.scale, gl.colour, gl.lumens / (4 * 3.14159265f), mine});
  };
  const story::Place& pl = here();
  for (size_t t = 0; t < pl.things.size(); ++t) {
    if (g.run->hidden(static_cast<int>(t))) continue;
    const story::Placed& th = pl.things[t];
    game::Place3 w;
    w.at = th.at, w.turn = th.turn, w.scale = th.scale;
    const uint32_t seed = static_cast<uint32_t>(t * 2654435761u + 17u);
    if (!th.person.empty()) {
      const story::Person* p = g.story.person(th.person);
      if (!p) continue;
      for (const auto& d : g.people)
        if (d.thing == static_cast<int>(t)) w.turn = d.yaw, w.at.y += d.spirit ? 0.18f : 0;
      for (const auto& lp : p->lights) add(lp, w, seed, false);
      continue;
    }
    if (!g.run->lit(static_cast<int>(t))) continue;
    if (const story::Kind* k = g.story.kind(th.kind))
      for (const auto& lp : k->lights) add(lp, w, seed, false);
  }
  if (g.story.carries && std::find(g.run->items().begin(), g.run->items().end(), g.story.carriedItem) != g.run->items().end()) {
    const float yr = g.yaw * 0.017453292f;
    game::Place3 w;
    w.at = g.eye + Vec3{std::sin(yr) * 0.35f + std::cos(yr) * 0.28f, -0.5f, -std::cos(yr) * 0.35f + std::sin(yr) * 0.28f};
    w.at.y -= g.story.carried.at.y;
    add(g.story.carried, w, 99, true);
  }
}

// ---- The camera ----

void perspective(float* m, float fovY, float aspect, float zn, float zf, bool zeroToOne) {
  const float f = 1 / std::tan(fovY / 2);
  std::memset(m, 0, 64);
  m[0] = f / aspect, m[5] = f, m[11] = -1;
  if (zeroToOne) m[10] = zf / (zn - zf), m[14] = zn * zf / (zn - zf);
  else m[10] = (zf + zn) / (zn - zf), m[14] = 2 * zf * zn / (zn - zf);
}
void mul(const float* a, const float* b, float* o) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
}

void frame() {
  if (!g.ready || !g.run) return;
  const double now = emscripten_get_now() / 1000.0;
  const float dt = static_cast<float>(std::clamp(now - g.last, 0.0, 0.1));
  g.last = now;
  if (g.run->arrived()) {
    const story::Place& pl = here();
    g.eye = {pl.start.x, kEye, pl.start.z};
    g.yaw = pl.facing, g.pitch = -4;
    g.run->clearArrived();
    g.skyFor = -2;
  }
  if (g.built != g.run->changes()) rebuild();
  // Walking (not while someone is talking to you).
  if (!g.run->waiting()) {
    const float yr = g.yaw * 0.017453292f;
    const Vec3 fwd{std::sin(yr), 0, -std::cos(yr)}, right{std::cos(yr), 0, std::sin(yr)};
    Vec3 p = g.eye + (fwd * g.moveY + right * g.moveX) * (kWalk * dt);
    collide(p);
    g.eye = {p.x, kEye, p.z};
  }
  // People turn to face you when you come close.
  const story::Place& pl = here();
  for (auto& d : g.people) {
    const story::Placed& th = pl.things[static_cast<size_t>(d.thing)];
    const Vec3 to{g.eye.x - th.at.x, 0, g.eye.z - th.at.z};
    const float want = length(to) < 6 ? std::atan2(to.x, to.z) * 57.29578f : d.rest;
    float diff = std::fmod(want - d.yaw + 540.0f, 360.0f) - 180.0f;
    d.yaw += diff * std::min(1.0f, dt * 3);
  }
  findNear();

  uint32_t w = 1, h = 1;
  g.dev->resize(&w, &h);
  game::Uniforms u{};
  const float yr = g.yaw * 0.017453292f, pr = g.pitch * 0.017453292f;
  const Vec3 f{std::sin(yr) * std::cos(pr), std::sin(pr), -std::cos(yr) * std::cos(pr)};
  const Vec3 right = normalize(cross(f, {0, 1, 0})), up = cross(right, f);
  const float view[16] = {right.x, up.x, -f.x, 0, right.y, up.y, -f.y, 0, right.z, up.z, -f.z, 0,
                          -dot(right, g.eye), -dot(up, g.eye), dot(f, g.eye), 1};
  float proj[16];
  perspective(proj, 68 * 0.017453292f, static_cast<float>(w) / static_cast<float>(h), 0.05f, 400, g.dev->backend() == draw::Backend::kWebGpu);
  mul(proj, view, u.viewProj);
  const story::Sky& sky = pl.sky;
  u.eye[0] = g.eye.x, u.eye[1] = g.eye.y, u.eye[2] = g.eye.z, u.eye[3] = sky.exposure;
  u.offset[3] = 1.4f;
  u.fog[0] = sky.horizon.x, u.fog[1] = sky.horizon.y, u.fog[2] = sky.horizon.z, u.fog[3] = pl.mist;
  u.ambient[0] = sky.ambient.x, u.ambient[1] = sky.ambient.y, u.ambient[2] = sky.ambient.z;
  u.moonDir[0] = sky.moonDir.x, u.moonDir[1] = sky.moonDir.y, u.moonDir[2] = sky.moonDir.z;
  u.moon[0] = sky.moon.x, u.moon[1] = sky.moon.y, u.moon[2] = sky.moon.z;
  std::vector<Light> lights;
  gather(lights, now);
  std::sort(lights.begin(), lights.end(), [](const Light& a, const Light& b) {
    if (a.mine != b.mine) return a.mine;
    auto score = [](const Light& l) { return l.cd / std::max(1.0f, lengthSq(l.at - g.eye)); };
    return score(a) > score(b);
  });
  const size_t n = std::min<size_t>(lights.size(), 16);
  static double logged = 0;
  if (now - logged > 2) {
    logged = now;
    std::string t = "lights " + std::to_string(lights.size()) + ":";
    for (size_t i = 0; i < n; ++i) {
      char b[160];
      std::snprintf(b, sizeof b, " [%.1f %.1f %.1f cd %.2f col %.2f %.2f %.2f]", lights[i].at.x, lights[i].at.y, lights[i].at.z, lights[i].cd, lights[i].colour.x, lights[i].colour.y, lights[i].colour.z);
      t += b;
    }
    js_lights(t.c_str());
  }
  u.count[0] = static_cast<float>(n);
  for (size_t i = 0; i < n; ++i) {
    const Light& l = lights[i];
    u.lp[i][0] = l.at.x, u.lp[i][1] = l.at.y, u.lp[i][2] = l.at.z, u.lp[i][3] = std::sqrt(l.cd / 0.0015f);
    u.lc[i][0] = l.colour.x * l.cd, u.lc[i][1] = l.colour.y * l.cd, u.lc[i][2] = l.colour.z * l.cd;
  }
  const float clear[4] = {0, 0, 0, 1};
  if (!g.dev->begin(clear)) return;
  // The sky, around the eye.
  game::Uniforms su = u;
  su.offset[0] = g.eye.x, su.offset[1] = 0, su.offset[2] = g.eye.z;
  g.dev->drawIndexed(g.pipe, g.sky.vb, g.sky.ib, g.sky.count, &su, sizeof su, nullptr, 0);
  g.dev->drawIndexed(g.pipe, g.place.vb, g.place.ib, g.place.count, &u, sizeof u, nullptr, 0);
  for (const auto& d : g.people) {
    if (g.run->hidden(d.thing)) continue;
    const story::Placed& th = pl.things[static_cast<size_t>(d.thing)];
    game::Uniforms pu = u;
    const float bob = d.spirit ? 0.18f + 0.07f * std::sin(static_cast<float>(now) * 1.7f + static_cast<float>(d.thing)) : 0;
    pu.offset[0] = th.at.x, pu.offset[1] = th.at.y + bob, pu.offset[2] = th.at.z;
    // The person's turn, as a turn of the whole view around their feet: folded into viewProj.
    const float a = d.yaw * 0.017453292f, c = std::cos(a), s = std::sin(a);
    const float model[16] = {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, th.at.x, th.at.y + bob, th.at.z, 1};
    mul(u.viewProj, model, pu.viewProj);
    pu.offset[0] = pu.offset[1] = pu.offset[2] = 0;
    // The shader lights in world space: hand it the eye in the person's own space.
    const Vec3 e = g.eye - Vec3{th.at.x, th.at.y + bob, th.at.z};
    pu.eye[0] = c * e.x - s * e.z, pu.eye[1] = e.y, pu.eye[2] = s * e.x + c * e.z;
    auto local = [&](Vec3 p) {
      const Vec3 q = p - Vec3{th.at.x, th.at.y + bob, th.at.z};
      return Vec3{c * q.x - s * q.z, q.y, s * q.x + c * q.z};
    };
    auto localDir = [&](Vec3 q) { return Vec3{c * q.x - s * q.z, q.y, s * q.x + c * q.z}; };
    for (size_t i = 0; i < n; ++i) {
      const Vec3 lp = local(lights[i].at);
      pu.lp[i][0] = lp.x, pu.lp[i][1] = lp.y, pu.lp[i][2] = lp.z;
    }
    const Vec3 md = localDir(sky.moonDir);
    pu.moonDir[0] = md.x, pu.moonDir[1] = md.y, pu.moonDir[2] = md.z;
    g.dev->drawIndexed(g.pipe, d.buf.vb, d.buf.ib, d.buf.count, &pu, sizeof pu, nullptr, 0);
  }
  g.dev->end();
}

void start(std::unique_ptr<draw::Device> dev) {
  if (!dev) {
    js_status("none", "This browser can draw with neither WebGPU nor WebGL2.");
    return;
  }
  g.dev = std::move(dev);
  draw::PipelineDesc pd;
  pd.label = "story";
  pd.wgsl = game::kWgsl;
  pd.glsl = game::kGlsl;
  pd.textures = 0;
  pd.layout.stride = sizeof(game::Vertex);
  pd.layout.items = {{draw::Attr::kFloat3, 0}, {draw::Attr::kFloat3, 12}, {draw::Attr::kUnorm8x4, 24}};
  g.pipe = g.dev->pipeline(pd);
  js_status(draw::backendName(g.dev->backend()), g.dev->problem.c_str());
}

std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\', o += c;
    else if (c == '\n') o += "\\n";
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

}  // namespace

extern "C" {

// The files the story's first file names, one a line.
EMSCRIPTEN_KEEPALIVE const char* g_storyFiles(const char* first) {
  g.out.clear();
  for (const std::string& f : story::storyFiles(first)) g.out += f + "\n";
  return g.out.c_str();
}

EMSCRIPTEN_KEEPALIVE void g_addFile(const char* name, const char* text) {
  if (std::strcmp(name, "first.looped") == 0) g.engineWords = text;
  else g.files[name] = text;
}

// Reads the story and starts it (or carries on from `save`). Returns its
// problems, one a line ("" when it reads clean).
EMSCRIPTEN_KEEPALIVE const char* g_begin(const char* save) {
  g.out.clear();
  g.story = story::Story{};
  story::Notes notes;
  const bool ok = story::loadStory(g.story, g.engineWords, [](const std::string& n) -> const std::string* {
    auto it = g.files.find(n);
    return it == g.files.end() ? nullptr : &it->second;
  }, notes);
  for (const auto& n : notes)
    if (!n.p.warning) g.out += n.text() + "\n";
  if (!ok) return g.out.c_str();
  g.run = std::make_unique<story::Runner>(g.story, static_cast<uint32_t>(emscripten_get_now()) | 1u);
  std::string s = save ? save : "";
  float px = 0, pz = 0, pyaw = 0;
  bool hasPos = false;
  const size_t pos = s.find("pos ");
  if (pos != std::string::npos) hasPos = std::sscanf(s.c_str() + pos, "pos %f %f %f", &px, &pz, &pyaw) == 3;
  if (!s.empty() && g.run->load(s)) {
    g.run->clearArrived();
    const story::Place& pl = here();
    g.eye = hasPos ? Vec3{px, kEye, pz} : Vec3{pl.start.x, kEye, pl.start.z};
    g.yaw = hasPos ? pyaw : pl.facing;
  } else {
    g.run->start();
  }
  g.built = 0xffffffff, g.skyFor = -2;
  g.last = emscripten_get_now() / 1000.0;
  g.ready = g.dev != nullptr;
  return g.out.c_str();
}

EMSCRIPTEN_KEEPALIVE void g_move(float x, float y) { g.moveX = std::clamp(x, -1.0f, 1.0f), g.moveY = std::clamp(y, -1.0f, 1.0f); }
EMSCRIPTEN_KEEPALIVE void g_look(float dx, float dy) {
  g.yaw = std::fmod(g.yaw + dx + 360.0f, 360.0f);
  g.pitch = std::clamp(g.pitch - dy, -60.0f, 60.0f);
}
EMSCRIPTEN_KEEPALIVE void g_next() {
  if (g.run) g.run->next();
}
EMSCRIPTEN_KEEPALIVE void g_choose(int i) {
  if (g.run) g.run->choose(i);
}
EMSCRIPTEN_KEEPALIVE void g_act(int i) {
  if (!g.run || i < 0 || static_cast<size_t>(i) >= g.near.size()) return;
  const Near n = g.near[static_cast<size_t>(i)];
  g.run->act(n.thing, n.verb);
}

// Everything the page shows, as JSON.
EMSCRIPTEN_KEEPALIVE const char* g_state() {
  g.out.clear();
  if (!g.run) return "{}";
  const story::Runner& r = *g.run;
  const auto& b = r.beat();
  static const char* kinds[] = {"none", "say", "speak", "choose", "end"};
  std::string& o = g.out;
  o = "{\"beat\":{\"k\":\"" + std::string(kinds[b.k]) + "\",\"who\":\"" + esc(b.who) + "\",\"text\":\"" + esc(b.text) + "\",\"choices\":[";
  for (size_t i = 0; i < b.choices.size(); ++i) o += (i ? ",\"" : "\"") + esc(b.choices[i]) + "\"";
  o += "]},\"near\":[";
  for (size_t i = 0; i < g.near.size(); ++i) o += (i ? ",\"" : "\"") + esc(g.near[i].label) + "\"";
  o += "],\"place\":\"" + (r.place() >= 0 ? esc(here().title) : "") + "\",\"changes\":" + std::to_string(r.changes()) + ",\"items\":[";
  for (size_t i = 0; i < r.items().size(); ++i) {
    const story::Item* it = g.story.item(r.items()[i]);
    o += std::string(i ? "," : "") + "{\"name\":\"" + esc(r.items()[i]) + "\",\"about\":\"" + esc(it ? it->about : "") + "\"}";
  }
  o += "],\"ending\":\"" + esc(r.ending()) + "\",\"error\":\"" + esc(r.error) + "\",\"title\":\"" + esc(g.story.title) + "\"}";
  return o.c_str();
}

// The game so far, to carry on later (only between conversations).
EMSCRIPTEN_KEEPALIVE const char* g_save() {
  g.out.clear();
  if (!g.run || g.run->waiting() || !g.run->ending().empty()) return "";
  char pos[96];
  std::snprintf(pos, sizeof pos, "pos %.2f %.2f %.1f\n", g.eye.x, g.eye.z, g.yaw);
  g.out = g.run->save() + pos;
  return g.out.c_str();
}

}  // extern "C"

int main() {
  draw::create("#canvas", js_forceGl() != 0, start);
  emscripten_set_main_loop(frame, 0, false);
  return 0;
}
