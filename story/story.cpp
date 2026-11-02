#include "story/story.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace story {

using ew::Expr;
using ew::Line;
using ew::Problem;
using ew::Problems;
using ew::Tok;
using ew::Token;

bool hasErrors(const Notes& n) {
  for (const Note& x : n)
    if (!x.p.warning) return true;
  return false;
}

const char* shapeName(Shape s) {
  switch (s) {
    case Shape::kBox: return "box";
    case Shape::kPost: return "post";
    case Shape::kCone: return "cone";
    case Shape::kBall: return "ball";
    case Shape::kRoof: return "roof";
    case Shape::kDisc: return "disc";
  }
  return "?";
}

const char* verbWord(Handler::Verb v) {
  switch (v) {
    case Handler::kTalk: return "Talk to";
    case Handler::kLook: return "Look at";
    case Handler::kTake: return "Take";
    case Handler::kUse: return "Use";
    case Handler::kLeave: return "Go to";
    default: return "";
  }
}

std::string keyOf(std::string_view s) {
  std::string o;
  bool space = false;
  for (char c : s) {
    if (c == ' ' || c == '\t') {
      space = !o.empty();
      continue;
    }
    if (space) o += ' ', space = false;
    o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  for (const char* a : {"the ", "a ", "an "}) {
    const size_t n = std::char_traits<char>::length(a);
    if (o.size() > n && o.compare(0, n, a) == 0) return o.substr(n);
  }
  return o;
}

namespace {

Vec3 hex(float r, float g, float b) { return {r, g, b}; }

}  // namespace

std::vector<std::string> skyNames() { return {"night", "dusk", "dawn", "day", "cave", "grey"}; }

bool skyNamed(std::string_view name, Sky& o) {
  o = Sky{};
  if (name == "night") {
    o.top = hex(0.0017333f, 0.0026f, 0.0075111f), o.horizon = hex(0.0052f, 0.0069333f, 0.015022f);
    o.moonDir = engine::normalize(Vec3{-0.45f, 0.55f, -0.6f}), o.moon = hex(0.063556f, 0.086667f, 0.15022f);
    o.ambient = hex(0.017333f, 0.023111f, 0.046222f), o.exposure = 9, o.stars = true;
  } else if (name == "dusk") {
    o.top = hex(0.004f, 0.005f, 0.016f), o.horizon = hex(0.05f, 0.022f, 0.016f);
    o.moonDir = engine::normalize(Vec3{0.8f, 0.12f, -0.4f}), o.moon = hex(0.5f, 0.25f, 0.14f);
    o.ambient = hex(0.06f, 0.05f, 0.07f), o.exposure = 4.5f, o.stars = true;
  } else if (name == "dawn") {
    o.top = hex(0.06f, 0.08f, 0.16f), o.horizon = hex(0.42f, 0.24f, 0.17f);
    o.moonDir = engine::normalize(Vec3{-0.6f, 0.25f, -0.75f}), o.moon = hex(2.6f, 1.7f, 1.15f);
    o.ambient = hex(0.55f, 0.52f, 0.62f), o.exposure = 1.5f;
  } else if (name == "day") {
    o.top = hex(0.25f, 0.42f, 0.85f), o.horizon = hex(0.62f, 0.72f, 0.86f);
    o.moonDir = engine::normalize(Vec3{0.4f, 0.8f, -0.3f}), o.moon = hex(5.f, 4.6f, 4.0f);
    o.ambient = hex(1.4f, 1.6f, 2.0f), o.exposure = 0.55f;
  } else if (name == "cave") {
    o.top = hex(0.0f, 0.0f, 0.0f), o.horizon = hex(0.00086667f, 0.00086667f, 0.0011556f);
    o.moonDir = Vec3{0, 1, 0}, o.moon = hex(0.0f, 0.0f, 0.0f);
    o.ambient = hex(0.0057778f, 0.0072222f, 0.011556f), o.exposure = 9;
  } else if (name == "grey") {
    o.top = hex(0.01f, 0.010571f, 0.012f), o.horizon = hex(0.019429f, 0.02f, 0.021714f);
    o.moonDir = engine::normalize(Vec3{0.2f, 0.9f, 0.1f}), o.moon = hex(0.057143f, 0.06f, 0.068571f);
    o.ambient = hex(0.085714f, 0.088571f, 0.097143f), o.exposure = 4.5;
  } else {
    return false;
  }
  return true;
}

void bounds(const Kind& k, float& x0, float& z0, float& x1, float& z1) {
  x0 = z0 = 1e9f, x1 = z1 = -1e9f;
  for (const Part& p : k.parts) {
    const float r = std::sqrt(p.size.x * p.size.x + p.size.y * p.size.y) * 0.5f;
    const float hx = p.turn != 0 ? r : p.size.x * 0.5f, hz = p.turn != 0 ? r : p.size.y * 0.5f;
    x0 = std::min(x0, p.at.x - hx), x1 = std::max(x1, p.at.x + hx);
    z0 = std::min(z0, p.at.z - hz), z1 = std::max(z1, p.at.z + hz);
  }
  if (x0 > x1) x0 = z0 = -0.1f, x1 = z1 = 0.1f;
}

float blockRadius(const Kind& k) {
  if (k.blocks >= 0) return k.blocks;
  float x0, z0, x1, z1;
  bounds(k, x0, z0, x1, z1);
  return std::max({x1, -x0, z1, -z0}) * 0.8f;
}

int Place::find(std::string_view ref) const {
  const std::string k = keyOf(ref);
  for (size_t i = 0; i < things.size(); ++i)
    if (!things[i].key.empty() && things[i].key == k) return static_cast<int>(i);
  return -1;
}

const Kind* Story::kind(std::string_view name) const {
  const std::string k = keyOf(name);
  for (const Kind& x : kinds) {
    if (x.name == k) return &x;
    for (const std::string& f : x.forms)
      if (f == k) return &x;
  }
  return nullptr;
}
const Person* Story::person(std::string_view name) const {
  const std::string k = keyOf(name);
  for (const Person& p : persons)
    if (keyOf(p.name) == k) return &p;
  return nullptr;
}
const Item* Story::item(std::string_view name) const {
  const std::string k = keyOf(name);
  for (const Item& x : items)
    if (x.name == k) return &x;
  return nullptr;
}
int Story::place(std::string_view id) const {
  const std::string k = keyOf(id);
  for (size_t i = 0; i < places.size(); ++i)
    if (places[i].id == k) return static_cast<int>(i);
  return -1;
}

// ---- Reading ----

struct Reader::Ctx {
  std::string file;
  Notes& notes;
  std::map<int, std::string> prose;  // line → its text after the colon
  Place* place = nullptr;
  bool inChoose = false;

  void bad(int line, int column, std::string what, std::string hint = {}, bool warning = false) {
    Problem p;
    p.line = line, p.column = column, p.what = std::move(what), p.hint = std::move(hint), p.warning = warning;
    notes.push_back({file, p});
  }
  void bad(const Token& t, std::string what, std::string hint = {}) { bad(t.line, t.column, std::move(what), std::move(hint)); }
  void bad(const Line& l, std::string what, std::string hint = {}) {
    bad(l.line, l.tokens.empty() ? 1 : l.tokens[0].column, std::move(what), std::move(hint));
  }
  void take(const Problems& ps) {
    for (const Problem& p : ps) notes.push_back({file, p});
  }
};

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool isWord(const Line& l, size_t i, std::string_view w) {
  return i < l.tokens.size() && l.tokens[i].kind == Tok::kWord && lower(l.tokens[i].text) == w;
}
bool isSym(const Line& l, size_t i, char c) {
  return i < l.tokens.size() && l.tokens[i].kind == Tok::kSymbol && l.tokens[i].text[0] == c;
}
std::string wordsOf(const Line& l, size_t from, size_t to) { return keyOf(ew::joinWords(l, from, std::min(to, l.tokens.size()))); }
std::string asWritten(const Line& l, size_t from, size_t to) { return ew::joinWords(l, from, std::min(to, l.tokens.size())); }
size_t findW(const Line& l, std::string_view w, size_t from = 0, size_t to = SIZE_MAX) {
  for (size_t i = from; i < l.tokens.size() && i < to; ++i)
    if (isWord(l, i, w)) return i;
  return l.tokens.size();
}
size_t comma(const Line& l, size_t from) {
  size_t i = from;
  while (i < l.tokens.size() && !isSym(l, i, ',')) ++i;
  return i;
}
bool article(const Line& l, size_t i) { return isWord(l, i, "a") || isWord(l, i, "an") || isWord(l, i, "the"); }

// A number, with its sign.
bool number(const Line& l, size_t& i, size_t to, float& v) {
  bool neg = false;
  size_t k = i;
  if (k < to && isSym(l, k, '-')) neg = true, ++k;
  if (k >= to || l.tokens[k].kind != Tok::kNumber) return false;
  v = static_cast<float>(neg ? -l.tokens[k].number : l.tokens[k].number);
  i = k + 1;
  return true;
}
// "m" (1) or "cm" (0.01) after sizes; none is metres.
float lengthUnit(const Line& l, size_t& i, size_t to) {
  if (i < to && isWord(l, i, "m")) return ++i, 1.0f;
  if (i < to && isWord(l, i, "cm")) return ++i, 0.01f;
  return 1.0f;
}

// Text before the first colon is words; after it is prose, unless the line
// is one of the few whose colon is followed by words (colours, the sky, the
// ground, the mist, a light). Prose is blanked for the engine's reader (it
// only knows letters, digits and a few signs) and kept here as written.
std::string maskProse(std::string_view text, std::map<int, std::string>& prose) {
  std::string out;
  out.reserve(text.size());
  size_t at = 0;
  int number = 0;
  while (at <= text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string_view::npos) end = text.size();
    std::string raw(text.substr(at, end - at));
    ++number;
    size_t colon = std::string::npos;
    for (size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '#') break;
      if (raw[i] == ':') {
        colon = i;
        break;
      }
    }
    if (colon != std::string::npos) {
      const std::string head = keyOf(raw.substr(0, colon));
      const std::string first = head.substr(0, head.find(' '));
      const bool words = first == "colour" || first == "color" || head == "sky" || head == "ground" || head == "mist" ||
                         head.rfind("light", 0) == 0 || first == "like" || first == "settings";
      std::string fullHead = lower(raw.substr(0, colon));
      while (!fullHead.empty() && fullHead[0] == ' ') fullHead.erase(0, 1);
      const bool aLight = fullHead.rfind("a light", 0) == 0;
      if (!words && !aLight) {
        std::string p = raw.substr(colon + 1);
        while (!p.empty() && (p.back() == ' ' || p.back() == '\r' || p.back() == '\t')) p.pop_back();
        size_t s = 0;
        while (s < p.size() && p[s] == ' ') ++s;
        prose[number] = p.substr(s);
        for (size_t i = colon + 1; i < raw.size(); ++i)
          if (raw[i] != '\r') raw[i] = ' ';
      }
    }
    out += raw;
    if (end < text.size()) out += '\n';
    at = end + 1;
  }
  return out;
}

const std::vector<std::string> kStatements = {"say", "choose", "choice", "if", "otherwise", "remember", "forget", "set", "add", "subtract",
                                              "you get", "you lose", "go to", "light", "put out", "show", "hide", "the end", "stop"};

}  // namespace

bool Reader::words(std::string_view text, const std::string& file, Notes& n) {
  Problems p;
  const bool ok = s_.words.load(text, file, p);
  for (const Problem& x : p) n.push_back({file, x});
  return ok;
}

bool Reader::read(std::string_view text, const std::string& file, Notes& n) {
  Ctx c{file, n, {}};
  const std::string masked = maskProse(text, c.prose);
  Problems p;
  const std::vector<Line> lines = ew::readLines(masked, p);
  c.take(p);
  for (const Line& l : lines) top(l, c);
  return !hasErrors(n);
}

void Reader::top(const Line& l, Ctx& c) {
  const std::string w = l.tokens.empty() ? "" : lower(l.tokens[0].text);
  if (w == "story") return storyLine(l, c);
  if (w == "colour" || w == "color") return colourLine(l, c);
  if (w == "item") return itemLine(l, c);
  if (w == "kind") return kindBlock(l, c);
  if (w == "person") return personBlock(l, c);
  if (w == "place") return placeBlock(l, c);
  c.bad(l.tokens[0], "`" + l.tokens[0].text + "` can't start a line at the left edge",
        ew::didYouMean(w, {"story", "colour", "item", "kind", "person", "place"}).empty()
            ? "a line at the left edge starts with story, colour, item, kind, person or place"
            : ew::didYouMean(w, {"story", "colour", "item", "kind", "person", "place"}));
}

void Reader::storyLine(const Line& l, Ctx& c) {
  if (c.prose.count(l.line)) {
    c.bad(l, "the story's title has no colon", "story The Last Ember");
    return;
  }
  s_.title = asWritten(l, 1, l.tokens.size());
  if (s_.title.empty()) c.bad(l, "the story has no title", "story The Last Ember");
  for (const Line& k : l.children) {
    const std::string w = k.tokens.empty() ? "" : lower(k.tokens[0].text);
    if (w == "by" && c.prose.count(k.line)) {
      s_.by = c.prose[k.line];
    } else if (isWord(k, 0, "you") && isWord(k, 1, "start") && isWord(k, 2, "in")) {
      s_.start = wordsOf(k, 3, k.tokens.size());
      pending_.push_back({c.file, k.line, k.tokens[3 < k.tokens.size() ? 3 : 0].column, "place", s_.start, ""});
    } else if (w == "keep" && isWord(k, 1, "count") && isWord(k, 2, "of")) {
      // keep count of <name> [from <n>]
      const size_t from = findW(k, "from", 3);
      if (from != 4) {
        c.bad(k, "a count has a one-word name", "keep count of embers");
        continue;
      }
      float v = 0;
      size_t i = from + 1;
      if (from < k.tokens.size() && (!number(k, i, k.tokens.size(), v) || i != k.tokens.size())) {
        c.bad(k, "a count starts from a plain number", "keep count of courage from 0");
        continue;
      }
      s_.numbers[lower(k.tokens[3].text)] = v;
    } else if ((w == "words" && isWord(k, 1, "from")) || w == "read") {
      for (size_t i = w == "read" ? 1 : 2; i < k.tokens.size(); ++i) {
        if (isSym(k, i, ',') || isWord(k, i, "and")) continue;
        if (w == "words") s_.files.push_back("words:" + k.tokens[i].text);
        else s_.files.push_back(k.tokens[i].text);
      }
    } else if (article(k, 0) && isWord(k, 1, "light")) {
      // a light when you have <an item>: <light words>
      const size_t colon = ew::findSymbol(k, ":");
      const size_t when = findW(k, "when", 0, colon);
      if (when == k.tokens.size() || !isWord(k, when + 1, "you") || !isWord(k, when + 2, "have")) {
        c.bad(k, "the light you carry says when you have it", "a light when you have the old lantern: a lantern flame");
        continue;
      }
      s_.carriedItem = wordsOf(k, when + 3, colon);
      pending_.push_back({c.file, k.line, k.tokens[when + 3 < colon ? when + 3 : when].column, "item", s_.carriedItem, ""});
      s_.carries = lightLine(k, c, s_.carried, when);
    } else if (!k.tokens.empty()) {
      c.bad(k, "`" + k.tokens[0].text + "` isn't something a story says about itself",
            "under `story`: by: …, you start in <a place>, keep count of <a name>, words from <files>, read <files>, a light when you have <an item>: …");
    }
  }
}

void Reader::colourLine(const Line& l, Ctx& c) {
  const size_t colon = ew::findSymbol(l, ":");
  if (colon == l.tokens.size() || colon < 2) {
    c.bad(l, "a colour is a name, a colon and three numbers from 0 to 1 (red, green, blue)", "colour moss: 0.2 0.35 0.15");
    return;
  }
  float r, g, b;
  size_t i = colon + 1;
  if (!number(l, i, l.tokens.size(), r) || !number(l, i, l.tokens.size(), g) || !number(l, i, l.tokens.size(), b) ||
      i != l.tokens.size()) {
    c.bad(l, "a colour is three numbers from 0 to 1 (red, green, blue)", "colour moss: 0.2 0.35 0.15");
    return;
  }
  // Written as the eye sees it (sRGB); kept as light adds up (linear).
  auto lin = [](float x) { return std::pow(std::clamp(x, 0.0f, 1.0f), 2.2f); };
  s_.colours[wordsOf(l, 1, colon)] = Vec3{lin(r), lin(g), lin(b)};
}

void Reader::itemLine(const Line& l, Ctx& c) {
  Item it;
  it.name = wordsOf(l, 1, ew::findSymbol(l, ":"));
  it.file = c.file, it.line = l.line;
  if (it.name.empty()) return c.bad(l, "the item has no name", "item silver bell: A small bell that rings like rain.");
  if (c.prose.count(l.line)) it.about = c.prose[l.line];
  if (s_.item(it.name)) return c.bad(l, "there's already an item called `" + it.name + "`");
  s_.items.push_back(it);
}

bool Reader::colour(const Line& l, size_t& i, size_t to, Ctx& c, Vec3& out) {
  if (isWord(l, i, "colour") || isWord(l, i, "color")) {
    size_t k = i + 1;
    float r, g, b;
    if (number(l, k, to, r) && number(l, k, to, g) && number(l, k, to, b)) {
      auto lin = [](float x) { return std::pow(std::clamp(x, 0.0f, 1.0f), 2.2f); };
      out = {lin(r), lin(g), lin(b)};
      i = k;
      return true;
    }
  }
  const size_t end = std::min(to, comma(l, i));
  const std::string name = wordsOf(l, i, end);
  auto it = s_.colours.find(name);
  if (it == s_.colours.end()) {
    std::vector<std::string> names;
    for (const auto& [k, v] : s_.colours) names.push_back(k);
    const std::string hint = ew::didYouMean(name, names);
    c.bad(l.tokens[std::min(i, l.tokens.size() - 1)], "`" + name + "` isn't a colour (yet)",
          hint.empty() ? "colours are read before they're used: add `colour " + name + ": 0.5 0.5 0.5` above, or in colours.looped" : hint);
    return false;
  }
  out = it->second;
  i = end;
  return true;
}

bool Reader::partLine(const Line& l, Ctx& c, Part& out) {
  out.line = l.line;
  size_t i = article(l, 0) ? 1 : 0;
  static const std::vector<std::string> shapes = {"box", "post", "cone", "ball", "roof", "disc"};
  const std::string sw = i < l.tokens.size() ? lower(l.tokens[i].text) : "";
  const auto at = std::find(shapes.begin(), shapes.end(), sw);
  if (at == shapes.end()) {
    const std::string hint = ew::didYouMean(sw, shapes);
    c.bad(l.tokens[std::min(i, l.tokens.size() - 1)], "`" + sw + "` isn't a shape",
          hint.empty() ? "a kind is made of shapes: a box, a post, a cone, a ball, a roof or a disc" : hint);
    return false;
  }
  out.shape = static_cast<Shape>(at - shapes.begin());
  ++i;
  // Sizes: a by b [by c] [m|cm]
  std::vector<float> sz;
  const size_t end = comma(l, i);
  float v;
  while (number(l, i, end, v)) {
    sz.push_back(v);
    if (isWord(l, i, "by")) ++i;
  }
  const float unit = lengthUnit(l, i, end);
  for (float& x : sz) x *= unit;
  const bool three = out.shape == Shape::kBox || out.shape == Shape::kRoof;
  const char* example = three ? "a box 4 by 3 by 2.5 m (wide by deep by high)" : out.shape == Shape::kBall ? "a ball 0.5 m (across)" : "a post 0.3 by 2 m (across by high)";
  if ((three && sz.size() != 3) || (!three && (sz.empty() || sz.size() > 2)) || i != end) {
    c.bad(l, std::string("a ") + shapeName(out.shape) + "'s size is " + (three ? "three numbers" : "one or two numbers"), example);
    return false;
  }
  if (three) out.size = {sz[0], sz[1], sz[2]};
  else if (sz.size() == 1) out.size = {sz[0], sz[0], sz[0]};
  else out.size = {sz[0], sz[0], sz[1]};
  // Options after commas.
  while (i < l.tokens.size()) {
    if (!isSym(l, i, ',')) {
      c.bad(l.tokens[i], "expected a comma before `" + l.tokens[i].text + "`");
      return false;
    }
    ++i;
    const size_t e = comma(l, i);
    float n;
    size_t k = i;
    if (number(l, k, e, n) && lengthUnit(l, k, e) && isWord(l, k, "up") && k + 1 == e) {
      const float u = isWord(l, k - 1, "cm") ? 0.01f : 1.0f;
      out.at.y = n * u;
      i = e;
    } else if (isWord(l, i, "at")) {
      k = i + 1;
      float x, y, z;
      if (!number(l, k, e, x) || !number(l, k, e, y) || !number(l, k, e, z) || k != e) {
        c.bad(l.tokens[i], "`at` takes three numbers: right, up, towards you (m)", "at 0 1.5 0.3");
        return false;
      }
      out.at = {x, y, z};
      i = e;
    } else if (isWord(l, i, "turned")) {
      k = i + 1;
      if (!number(l, k, e, n) || k != e) {
        c.bad(l.tokens[i], "`turned` takes degrees", "turned 45");
        return false;
      }
      out.turn = n;
      i = e;
    } else if (isWord(l, i, "only") && isWord(l, i + 1, "when") && isWord(l, i + 2, "lit") && i + 3 == e) {
      out.onlyLit = true;
      i = e;
    } else if (isWord(l, i, "glowing")) {
      ++i;
      out.glows = true;
      if (!colour(l, i, e, c, out.colour)) return false;
    } else {
      if (!colour(l, i, e, c, out.colour)) return false;
    }
  }
  return true;
}

bool Reader::lightLine(const Line& l, Ctx& c, LightPart& out, size_t placeEnd) {
  // a light [N m up | at x y z]: <the engine's light words>
  const size_t colon = ew::findSymbol(l, ":");
  const size_t stop = placeEnd ? placeEnd : colon;
  if (colon == l.tokens.size() || colon + 1 >= l.tokens.size()) {
    c.bad(l, "a light says what it is after a colon, in light words", "a light 2.5 m up: a flame that flickers");
    return false;
  }
  size_t i = 2;
  float n;
  if (number(l, i, stop, n)) {
    const float u = lengthUnit(l, i, stop);
    if (!isWord(l, i, "up")) {
      c.bad(l, "a light's height is `N m up`", "a light 2.5 m up: a flame that flickers");
      return false;
    }
    ++i;
    out.at = {0, n * u, 0};
  } else if (isWord(l, i, "at")) {
    ++i;
    float x, y, z;
    if (!number(l, i, stop, x) || !number(l, i, stop, y) || !number(l, i, stop, z)) {
      c.bad(l, "`at` takes three numbers: right, up, towards you (m)", "a light at 0 1.2 0.3: a candle flame");
      return false;
    }
    out.at = {x, y, z};
  }
  if (i != stop) {
    c.bad(l.tokens[i], "a light's place is `N m up` or `at x y z`", "a light 2.5 m up: a flame that flickers");
    return false;
  }
  out.phrase = asWritten(l, colon + 1, l.tokens.size());
  Problems p;
  const bool ok = s_.words.read(l, colon + 1, l.tokens.size(), out.thing, p);
  c.take(p);
  if (ok && !out.thing.hasLight) {
    bool any = false;
    for (const ew::Thing& pt : out.thing.parts) any = any || pt.hasLight;
    if (!any) c.bad(l.tokens[colon + 1], "`" + out.phrase + "` gives no light", "a light 2 m up: a flame that flickers");
  }
  return ok;
}

void Reader::kindBlock(const Line& l, Ctx& c) {
  Kind k;
  k.file = c.file, k.line = l.line;
  size_t open = ew::findSymbol(l, "(");
  k.name = wordsOf(l, 1, open);
  if (k.name.empty()) return c.bad(l, "the kind has no name", "kind pine tree (pine trees)");
  if (open < l.tokens.size()) {
    const size_t close = ew::findSymbol(l, ")", open);
    size_t s = open + 1;
    for (size_t i = open + 1; i <= close && i < l.tokens.size() + 1; ++i) {
      if (i == close || isSym(l, i, ',')) {
        if (i > s) k.forms.push_back(wordsOf(l, s, i));
        s = i + 1;
      }
    }
  } else {
    k.forms.push_back(k.name + (k.name.back() == 's' || k.name.back() == 'h' ? "es" : "s"));
  }
  if (s_.kind(k.name)) return c.bad(l, "there's already a kind called `" + k.name + "`");
  for (const Line& x : l.children) {
    if (x.tokens.empty()) continue;
    if (isWord(x, 0, "blocks") && isWord(x, 1, "like")) {
      k.wall = true;
    } else if (isWord(x, 0, "blocks")) {
      size_t i = 1;
      float n;
      if (!number(x, i, x.tokens.size(), n)) {
        c.bad(x, "`blocks` takes how far it keeps you away", "blocks 0.6 m");
        continue;
      }
      k.blocks = n * lengthUnit(x, i, x.tokens.size());
    } else if (isWord(x, 0, "you") && isWord(x, 1, "can") && isWord(x, 2, "walk")) {
      k.blocks = 0;
    } else if (article(x, 0) && isWord(x, 1, "light")) {
      LightPart lp;
      if (lightLine(x, c, lp)) k.lights.push_back(std::move(lp));
    } else {
      Part p;
      if (partLine(x, c, p)) k.parts.push_back(p);
    }
  }
  if (k.parts.empty() && k.lights.empty()) c.bad(l, "the kind `" + k.name + "` is made of nothing", "under it: a box 1 by 1 by 1 m, oak");
  s_.kinds.push_back(std::move(k));
}

void Reader::personBlock(const Line& l, Ctx& c) {
  Person p;
  p.file = c.file, p.line = l.line;
  p.name = asWritten(l, 1, l.tokens.size());
  if (p.name.empty()) return c.bad(l, "the person has no name", "person Mira");
  if (s_.person(p.name)) return c.bad(l, "there's already a person called `" + p.name + "`");
  for (const Line& x : l.children) {
    if (x.tokens.empty()) continue;
    const std::string w = lower(x.tokens[0].text);
    size_t i = 1;
    if (w == "about" && c.prose.count(x.line)) {
      p.about = c.prose[x.line];
    } else if (w == "robe") {
      colour(x, i, x.tokens.size(), c, p.robe);
    } else if (w == "skin") {
      colour(x, i, x.tokens.size(), c, p.skin);
    } else if (w == "hood") {
      p.hatShape = Hat::kHood;
      colour(x, i, x.tokens.size(), c, p.hat);
    } else if (w == "hair") {
      p.hatShape = Hat::kHair;
      colour(x, i, x.tokens.size(), c, p.hat);
    } else if (w == "pointed" && isWord(x, 1, "hat")) {
      p.hatShape = Hat::kPointed;
      i = 2;
      colour(x, i, x.tokens.size(), c, p.hat);
    } else if (w == "height") {
      float n;
      if (!number(x, i, x.tokens.size(), n)) {
        c.bad(x, "`height` takes how tall", "height 1.6 m");
        continue;
      }
      p.height = n * lengthUnit(x, i, x.tokens.size());
    } else if (article(x, 0) && isWord(x, 1, "spirit")) {
      p.spirit = true;
    } else if (article(x, 0) && isWord(x, 1, "light")) {
      LightPart lp;
      if (lightLine(x, c, lp)) p.lights.push_back(std::move(lp));
    } else {
      static const std::vector<std::string> opts = {"about", "robe", "skin", "hood", "hair", "pointed hat", "height", "a spirit", "a light"};
      const std::string hint = ew::didYouMean(w, opts);
      c.bad(x.tokens[0], "`" + x.tokens[0].text + "` isn't something a person has", hint.empty() ? "robe, skin, hood, hair, pointed hat, height, a spirit, a light, about" : hint);
    }
  }
  s_.persons.push_back(std::move(p));
}

void Reader::placeBlock(const Line& l, Ctx& c) {
  Place pl;
  pl.file = c.file, pl.line = l.line;
  pl.id = wordsOf(l, 1, l.tokens.size());
  pl.title = asWritten(l, 1, l.tokens.size());
  skyNamed("night", pl.sky);
  if (pl.id.empty()) return c.bad(l, "the place has no name", "place the village");
  if (s_.place(pl.id) >= 0) return c.bad(l, "there's already a place called `" + pl.id + "`");
  pl.start = {0, 0, pl.groundD / 2 - 3};
  c.place = &pl;
  for (const Line& x : l.children) {
    if (x.tokens.empty()) continue;
    const std::string w = lower(x.tokens[0].text);
    if (w == "title" && c.prose.count(x.line)) {
      pl.title = c.prose[x.line];
    } else if (w == "the" && isWord(x, 1, "sky") && isSym(x, 2, ':')) {
      pl.skyName = wordsOf(x, 3, x.tokens.size());
      if (!skyNamed(pl.skyName, pl.sky)) {
        const std::string hint = ew::didYouMean(pl.skyName, skyNames());
        c.bad(x.tokens[std::min<size_t>(3, x.tokens.size() - 1)], "`" + pl.skyName + "` isn't a sky", hint.empty() ? "night, dusk, dawn, day, cave or grey" : hint);
      }
    } else if (w == "the" && isWord(x, 1, "mist") && isSym(x, 2, ':')) {
      const std::string m = wordsOf(x, 3, x.tokens.size());
      size_t i = 3;
      float n;
      if (m == "none") pl.mist = 0;
      else if (m == "light") pl.mist = 0.02f;
      else if (m == "thick") pl.mist = 0.07f;
      else if (m == "very thick") pl.mist = 0.14f;
      else if (number(x, i, x.tokens.size(), n)) pl.mist = n;
      else c.bad(x, "the mist is none, light, thick, very thick, or a number", "the mist: light");
    } else if (w == "the" && isWord(x, 1, "ground") && isSym(x, 2, ':')) {
      size_t i = 3;
      if (!colour(x, i, x.tokens.size(), c, pl.ground)) continue;
      if (isSym(x, i, ',')) {
        ++i;
        float a, b;
        if (number(x, i, x.tokens.size(), a) && isWord(x, i, "by") && (++i, number(x, i, x.tokens.size(), b))) {
          lengthUnit(x, i, x.tokens.size());
          pl.groundW = a, pl.groundD = b;
          pl.start = {0, 0, b / 2 - 3};
        } else {
          c.bad(x, "the ground's size is wide by deep", "the ground: moss, 40 by 40 m");
        }
      }
    } else if (w == "you" && isWord(x, 1, "come") && isWord(x, 2, "in")) {
      size_t i = findW(x, "at");
      float a, b;
      if (i == x.tokens.size() || (++i, !number(x, i, x.tokens.size(), a)) || !number(x, i, x.tokens.size(), b)) {
        c.bad(x, "where you come in: two numbers, right and towards you", "you come in at 0 12, facing 0");
        continue;
      }
      pl.start = {a, 0, b};
      if (isSym(x, i, ',') && isWord(x, i + 1, "facing")) {
        i += 2;
        float f;
        if (number(x, i, x.tokens.size(), f)) pl.facing = f;
      }
    } else if (w == "when") {
      handler(x, c, pl);
    } else {
      thingLine(x, c, pl);
    }
  }
  // Scattered things keep clear of everything placed on purpose, of the
  // paths and of where you come in.
  std::vector<Placed> kept;
  for (const Placed& t : pl.things) {
    bool clear = true;
    if (t.line == -1) {
      if (engine::length(t.at - pl.start) < 3.5f) clear = false;
      for (const Placed& o : pl.things) {
        if (!clear || o.line == -1) continue;
        const Kind* k = o.person.empty() ? s_.kind(o.kind) : nullptr;
        float x0 = -0.6f, z0 = -0.6f, x1 = 0.6f, z1 = 0.6f;
        if (k) bounds(*k, x0, z0, x1, z1);
        const float m = o.line == 0 ? 0.9f : 1.6f;  // paths and rows; things on purpose
        const float a = -o.turn * 0.017453292f, ca = std::cos(a), sa = std::sin(a);
        const Vec3 d = t.at - o.at;
        const float lx = (d.x * ca - d.z * sa) / o.scale, lz = (d.x * sa + d.z * ca) / o.scale;
        if (lx > x0 - m && lx < x1 + m && lz > z0 - m && lz < z1 + m) clear = false;
      }
    }
    if (clear) kept.push_back(t);
  }
  pl.things = std::move(kept);
  // Unnamed things a script can still name: the only one of its kind here.
  std::map<std::string, int> count;
  for (const Placed& t : pl.things)
    if (!t.kind.empty() && t.key.empty() && t.way.empty()) ++count[t.kind];
  for (Placed& t : pl.things)
    if (!t.kind.empty() && t.key.empty() && t.way.empty() && t.line > 0 && count[t.kind] == 1 && pl.find(t.kind) < 0) {
      t.key = t.kind;
      t.label = "the " + t.kind;
    }
  c.place = nullptr;
  s_.places.push_back(std::move(pl));
}

void Reader::thingLine(const Line& l, Ctx& c, Place& pl) {
  const size_t n = l.tokens.size();
  // <N> <kinds> in a line from x z to x z | around x z, a to b m away | between x1 z1 and x2 z2
  if (l.tokens[0].kind == Tok::kNumber) {
    const int count = std::max(1, static_cast<int>(l.tokens[0].number));
    size_t around = findW(l, "around"), between = findW(l, "between"), inLine = n;
    for (size_t q = 1; q + 3 < n; ++q)
      if (isWord(l, q, "in") && isWord(l, q + 1, "a") && isWord(l, q + 2, "line") && isWord(l, q + 3, "from")) {
        inLine = q;
        break;
      }
    const size_t headEnd = std::min({around, between, inLine});
    const std::string kn = wordsOf(l, 1, headEnd);
    const Kind* k = s_.kind(kn);
    if (!k) {
      std::vector<std::string> names;
      for (const Kind& x : s_.kinds) names.insert(names.end(), x.forms.begin(), x.forms.end());
      c.bad(l.tokens[std::min<size_t>(1, n - 1)], "`" + kn + "` isn't a kind of thing (yet)", ew::didYouMean(kn, names));
      return;
    }
    uint32_t seed = static_cast<uint32_t>(l.line) * 2654435761u + 12345u;
    auto rnd = [&seed] {
      seed = seed * 1664525u + 1013904223u;
      return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    std::vector<Placed> made;
    size_t i = headEnd + 1;
    float x0, z0, a, b;
    if (headEnd == inLine) {
      i = inLine + 4;
      float x1, z1;
      if (!number(l, i, n, x0) || !number(l, i, n, z0) || !isWord(l, i, "to") || (++i, !number(l, i, n, x1)) || !number(l, i, n, z1)) {
        c.bad(l, "things in a line: from x z to x z", "6 lamp posts in a line from -10 0 to 10 0");
        return;
      }
      for (int j = 0; j < count; ++j) {
        const float f = count == 1 ? 0.5f : static_cast<float>(j) / static_cast<float>(count - 1);
        Placed t;
        t.kind = k->name, t.at = {x0 + (x1 - x0) * f, 0, z0 + (z1 - z0) * f}, t.line = 0;
        made.push_back(t);
      }
    } else if (headEnd == around) {
      if (!number(l, i, n, x0) || !number(l, i, n, z0) || !isSym(l, i, ',') || (++i, !number(l, i, n, a)) || !isWord(l, i, "to") ||
          (++i, !number(l, i, n, b))) {
        c.bad(l, "a ring of things: around x z, from how far to how far", "12 pine trees around 0 0, 14 to 20 m away");
        return;
      }
      lengthUnit(l, i, n);
      if (isWord(l, i, "away")) ++i;
      for (int j = 0; j < count; ++j) {
        const float ang = (static_cast<float>(j) + rnd() * 0.8f) / static_cast<float>(count) * 6.2831853f;
        const float r = a + (b - a) * rnd();
        Placed t;
        t.kind = k->name, t.at = {x0 + std::sin(ang) * r, 0, z0 + std::cos(ang) * r}, t.turn = rnd() * 360, t.scale = 0.8f + rnd() * 0.45f;
        t.line = -1;
        made.push_back(t);
      }
    } else {
      float x1, z1;
      if (!number(l, i, n, x0) || !number(l, i, n, z0) || !isWord(l, i, "and") || (++i, !number(l, i, n, x1)) || !number(l, i, n, z1)) {
        c.bad(l, "things scattered between two corners: x1 z1 and x2 z2", "6 rocks between -5 -5 and 5 5");
        return;
      }
      for (int j = 0; j < count; ++j) {
        Placed t;
        t.kind = k->name, t.at = {x0 + (x1 - x0) * rnd(), 0, z0 + (z1 - z0) * rnd()}, t.turn = rnd() * 360, t.scale = 0.8f + rnd() * 0.45f;
        t.line = -1;
        made.push_back(t);
      }
    }
    Placed opts;
    if (!thingOptions(l, i, c, opts)) return;
    for (Placed& t : made) {
      t.lit = opts.lit, t.hidden = opts.hidden, t.scale *= opts.scale;
      if (opts.turn != 0) t.turn = opts.turn;
      pl.things.push_back(t);
    }
    return;
  }
  const size_t at = findW(l, "at");
  if (at == n) {
    c.bad(l, "where is it? a thing in a place says `at` and where", "a well at 3 -4");
    return;
  }
  Placed t;
  t.line = l.line;
  // Where: x z, or x y z.
  size_t i = at + 1;
  std::vector<float> xyz;
  float v;
  while (number(l, i, n, v)) xyz.push_back(v);
  if (xyz.size() == 2) t.at = {xyz[0], 0, xyz[1]};
  else if (xyz.size() == 3) t.at = {xyz[0], xyz[1], xyz[2]};
  else {
    c.bad(l.tokens[at], "`at` takes two numbers (right, towards you) or three (right, up, towards you)", "a well at 3 -4");
    return;
  }
  // What.
  if (isWord(l, 0, "a") && isWord(l, 1, "way") && isWord(l, 2, "to")) {
    t.way = wordsOf(l, 3, at);
    t.kind = "way";
    t.key = "way to " + t.way;
    t.label = asWritten(l, 3, at);
    if (!s_.kind("way")) c.bad(l, "a way needs a kind to look like", "kind way, made of shapes, in your kinds file");
    pending_.push_back({c.file, l.line, l.tokens[3 < at ? 3 : 0].column, "place", t.way, ""});
  } else {
    const size_t called = findW(l, "called", 0, at);
    const size_t kindEnd = std::min(called, at);
    const std::string head = wordsOf(l, 0, kindEnd);
    if (const Person* p = s_.person(head)) {
      t.person = p->name;
      t.key = keyOf(p->name);
      t.label = p->name;
    } else if (const Kind* k = s_.kind(head)) {
      t.kind = k->name;
      if (called < at) {
        t.key = wordsOf(l, called + 1, at);
        t.label = asWritten(l, called + 1, at);
        if (!article(l, called + 1)) t.label = "the " + t.label;
      }
    } else {
      std::vector<std::string> names;
      for (const Kind& x : s_.kinds) names.push_back(x.name);
      for (const Person& x : s_.persons) names.push_back(keyOf(x.name));
      const std::string hint = ew::didYouMean(head, names);
      c.bad(l.tokens[0], "`" + head + "` isn't a kind of thing or a person (yet)",
            hint.empty() ? "kinds and people are read before the places: add `kind " + head + "` to your kinds file" : hint);
      return;
    }
    if (!t.key.empty() && pl.find(t.key) >= 0) {
      c.bad(l.tokens[0], "there's already something called `" + t.key + "` here", "give it its own name: a well called the old well at 3 4");
      return;
    }
  }
  if (!thingOptions(l, i, c, t)) return;
  pl.things.push_back(t);
}

bool Reader::thingOptions(const Line& l, size_t i, Ctx& c, Placed& t) {
  const size_t n = l.tokens.size();
  while (i < n) {
    if (!isSym(l, i, ',')) {
      c.bad(l.tokens[i], "expected a comma before `" + l.tokens[i].text + "`");
      return false;
    }
    ++i;
    const size_t e = comma(l, i);
    size_t k = i + 1;
    float f;
    if ((isWord(l, i, "turned") || isWord(l, i, "facing")) && number(l, k, e, f) && k == e) {
      t.turn = f;
    } else if (k = i, number(l, k, e, f) && isWord(l, k, "times") && isWord(l, k + 1, "as") && isWord(l, k + 2, "big") && k + 3 == e) {
      t.scale = f;
    } else if (isWord(l, i, "unlit") && i + 1 == e) {
      t.lit = false;
    } else if (isWord(l, i, "hidden") && i + 1 == e) {
      t.hidden = true;
    } else {
      static const std::vector<std::string> opts = {"turned", "facing", "times as big", "unlit", "hidden"};
      const std::string hint = ew::didYouMean(lower(l.tokens[i].text), opts);
      c.bad(l.tokens[i], "`" + wordsOf(l, i, e) + "` isn't something a thing can be here",
            hint.empty() ? "turned 45, facing 90, 1.5 times as big, unlit, hidden" : hint);
      return false;
    }
    i = e;
  }
  return true;
}

void Reader::handler(const Line& l, Ctx& c, Place& pl) {
  Handler h;
  h.file = c.file, h.line = l.line;
  const size_t n = l.tokens.size();
  size_t from = n;
  if (!isWord(l, 1, "you")) {
    c.bad(l, "`when` is followed by what you do", "when you talk to Mira");
    return;
  }
  if (isWord(l, 2, "arrive") && n == 3) h.verb = Handler::kArrive;
  else if (isWord(l, 2, "first") && isWord(l, 3, "arrive") && n == 4) h.verb = Handler::kFirstArrive;
  else if (isWord(l, 2, "talk") && isWord(l, 3, "to")) h.verb = Handler::kTalk, from = 4;
  else if (isWord(l, 2, "look") && isWord(l, 3, "at")) h.verb = Handler::kLook, from = 4;
  else if (isWord(l, 2, "take")) h.verb = Handler::kTake, from = 3;
  else if (isWord(l, 2, "use")) h.verb = Handler::kUse, from = 3;
  else if (isWord(l, 2, "leave") && isWord(l, 3, "for")) h.verb = Handler::kLeave, from = 4;
  else {
    static const std::vector<std::string> opts = {"when you arrive", "when you first arrive", "when you talk to", "when you look at",
                                                  "when you take", "when you use", "when you leave for"};
    const std::string said = "when you " + wordsOf(l, 2, std::min<size_t>(n, 4));
    const std::string hint = ew::didYouMean(said, opts);
    c.bad(l.tokens[std::min<size_t>(2, n - 1)], "`" + said + "` isn't something a story waits for",
          hint.empty() ? "when you arrive, first arrive, talk to …, look at …, take …, use …, leave for …" : hint);
    return;
  }
  if (from < n) {
    h.target = wordsOf(l, from, n);
    if (h.verb == Handler::kLeave) {
      pending_.push_back({c.file, l.line, l.tokens[from].column, "place", h.target, ""});
      h.target = "way to " + h.target;
    }
    pending_.push_back({c.file, l.line, l.tokens[from].column, "thing", h.target, pl.id});
  } else if (h.verb != Handler::kArrive && h.verb != Handler::kFirstArrive) {
    c.bad(l, "who or what?", "when you look at the old well");
    return;
  }
  h.body = block(l.children, c);
  if (h.body.empty()) c.bad(l, "nothing happens: the lines under `when` say what does", "  say: The well is dry.");
  pl.handlers.push_back(std::move(h));
}

std::vector<Stmt> Reader::block(const std::vector<Line>& lines, Ctx& c) {
  std::vector<Stmt> out;
  Stmt* lastIf = nullptr;
  bool ended = false;
  for (const Line& l : lines) {
    if (l.tokens.empty()) continue;
    if (isWord(l, 0, "otherwise")) {
      if (!lastIf) {
        c.bad(l, "`otherwise` without an `if` just above it", "if you have the lantern … otherwise …");
        continue;
      }
      Stmt* tail = lastIf;
      while (!tail->otherwise.empty() && tail->otherwise.size() == 1 && tail->otherwise[0].k == Stmt::kIf && tail->otherwise[0].line != 0 &&
             tail->otherwise[0].text == "else-if")
        tail = &tail->otherwise[0];
      if (!tail->otherwise.empty()) {
        c.bad(l, "this `if` already has its `otherwise`");
        continue;
      }
      if (isWord(l, 1, "if")) {
        Stmt s;
        s.k = Stmt::kIf, s.line = l.line, s.text = "else-if";
        if (!cond(l, 2, l.tokens.size(), c, s.cond)) continue;
        s.body = block(l.children, c);
        tail->otherwise.push_back(std::move(s));
      } else {
        if (l.tokens.size() > 1) c.bad(l.tokens[1], "`otherwise` stands alone (or `otherwise if …`)");
        tail->otherwise = block(l.children, c);
        if (tail->otherwise.empty()) c.bad(l, "nothing under `otherwise`");
      }
      continue;
    }
    if (ended) {
      c.bad(l.line, l.tokens[0].column, "this line never happens: the one above leaves", "move it above the `go to`, `the end` or `stop`", true);
      ended = false;
    }
    Stmt s;
    if (!stmt(l, c, s)) {
      lastIf = nullptr;
      continue;
    }
    ended = s.k == Stmt::kGoTo || s.k == Stmt::kEnd || s.k == Stmt::kStop;
    out.push_back(std::move(s));
    lastIf = out.back().k == Stmt::kIf ? &out.back() : nullptr;
  }
  return out;
}

bool Reader::stmt(const Line& l, Ctx& c, Stmt& s) {
  s.line = l.line;
  const size_t n = l.tokens.size();
  const std::string w0 = lower(l.tokens[0].text);
  auto children = [&](bool want) {
    if (!want && !l.children.empty()) c.bad(l.children[0], "this line is indented under one that holds no lines", "move it to the left");
  };
  if (c.prose.count(l.line)) {
    const size_t colon = ew::findSymbol(l, ":");
    const std::string head = wordsOf(l, 0, colon);
    const std::string& text = c.prose[l.line];
    if (head == "say") s.k = Stmt::kSay;
    else if (head == "end" && isWord(l, 0, "the")) s.k = Stmt::kEnd, s_.endings.push_back(text);
    else if (w0 == "choice") {
      if (!c.inChoose) {
        c.bad(l, "a `choice` belongs under `choose`");
        return false;
      }
      s.k = Stmt::kChoice;
      if (isWord(l, 1, "if") && !cond(l, 2, colon, c, s.cond)) return false;
      if (!isWord(l, 1, "if") && colon != 1) {
        c.bad(l.tokens[1], "a choice is `choice: <what you say or do>` (or `choice if …:`)");
        return false;
      }
      const bool was = c.inChoose;
      c.inChoose = false;
      s.body = block(l.children, c);
      c.inChoose = was;
    } else if (head == "you") {
      s.k = Stmt::kSpeak, s.who = "you";
    } else if (const Person* p = s_.person(head)) {
      s.k = Stmt::kSpeak, s.who = p->name;
    } else {
      std::vector<std::string> names = {"say", "the end", "choice", "you"};
      for (const Person& p2 : s_.persons) names.push_back(keyOf(p2.name));
      const std::string hint = ew::didYouMean(head, names);
      c.bad(l.tokens[0], "`" + asWritten(l, 0, colon) + "` isn't a person in this story, so they can't speak",
            hint.empty() ? "people are written as `person <name>`; narration is `say: …`" : hint);
      return false;
    }
    if (text.empty()) {
      c.bad(l.tokens[colon], "nothing after the colon", "Mira: The Ember has gone out.");
      return false;
    }
    s.text = text;
    if (s.k != Stmt::kChoice) children(false);
    return true;
  }
  if (w0 == "choose") {
    s.k = Stmt::kChoose;
    const bool was = c.inChoose;
    c.inChoose = true;
    for (const Line& x : l.children) {
      if (!isWord(x, 0, "choice")) {
        c.bad(x, "under `choose`, every line is a `choice: …`", "choice: Where is the Ember?");
        continue;
      }
      Stmt ch;
      if (stmt(x, c, ch)) s.body.push_back(std::move(ch));
    }
    c.inChoose = was;
    if (s.body.empty()) c.bad(l, "`choose` needs choices under it", "  choice: Where is the Ember?");
    return !s.body.empty();
  }
  if (w0 == "if") {
    s.k = Stmt::kIf;
    if (!cond(l, 1, n, c, s.cond)) return false;
    s.body = block(l.children, c);
    if (s.body.empty()) c.bad(l, "nothing under `if`");
    return true;
  }
  children(false);
  if (w0 == "set") {
    const size_t to = findW(l, "to");
    if (to == n || to < 2) return c.bad(l, "set what to what?", "set courage to 2"), false;
    s.k = Stmt::kSet;
    s.who = wordsOf(l, 1, to);
    if (!s_.numbers.count(s.who)) {
      std::vector<std::string> names;
      for (const auto& [k, v] : s_.numbers) names.push_back(k);
      const std::string hint = ew::didYouMean(s.who, names);
      c.bad(l.tokens[1], "`" + s.who + "` isn't something this story keeps count of",
            hint.empty() ? "add it under `story`: keep count of " + s.who : hint);
      return false;
    }
    Problems p;
    if (!ew::parseExpr(l.tokens, to + 1, n, s.expr, p)) return c.take(p), false;
    return true;
  }
  if (w0 == "remember") {
    s.k = Stmt::kRemember, s.who = wordsOf(l, 1, n);
    if (s.who.empty()) return c.bad(l, "remember what?", "remember you met Mira"), false;
    if (s_.numbers.count(s.who)) return c.bad(l, "`" + s.who + "` is a count: give it a number", "set " + s.who + " to 3"), false;
    s_.flags.insert(s.who);
    return true;
  }
  if (w0 == "forget") {
    s.k = Stmt::kForget, s.who = wordsOf(l, 1, n);
    pending_.push_back({c.file, l.line, l.tokens[std::min<size_t>(1, n - 1)].column, "flag", s.who, ""});
    return !s.who.empty();
  }
  if (w0 == "add" || w0 == "subtract") {
    s.k = w0 == "add" ? Stmt::kAdd : Stmt::kSubtract;
    const size_t to = findW(l, w0 == "add" ? "to" : "from");
    if (to == n || to < 2) {
      c.bad(l, w0 == "add" ? "add how much to what?" : "subtract how much from what?", w0 == "add" ? "add 1 to courage" : "subtract 1 from courage");
      return false;
    }
    Problems p;
    if (!ew::parseExpr(l.tokens, 1, to, s.expr, p)) return c.take(p), false;
    s.who = wordsOf(l, to + 1, n);
    if (!s_.numbers.count(s.who)) {
      std::vector<std::string> names;
      for (const auto& [k, v] : s_.numbers) names.push_back(k);
      const std::string hint = ew::didYouMean(s.who, names);
      c.bad(l.tokens[std::min(to + 1, n - 1)], "`" + s.who + "` isn't something this story keeps count of",
            hint.empty() ? "add it under `story`: keep count of " + s.who : hint);
      return false;
    }
    return true;
  }
  if (w0 == "you" && (isWord(l, 1, "get") || isWord(l, 1, "lose"))) {
    s.k = isWord(l, 1, "get") ? Stmt::kGet : Stmt::kLose;
    s.who = wordsOf(l, 2, n);
    if (!s_.item(s.who)) {
      std::vector<std::string> names;
      for (const Item& it : s_.items) names.push_back(it.name);
      const std::string hint = ew::didYouMean(s.who, names);
      c.bad(l.tokens[std::min<size_t>(2, n - 1)], "`" + s.who + "` isn't an item in this story", hint.empty() ? "add it: item " + s.who + ": what it is." : hint);
      return false;
    }
    return true;
  }
  if (w0 == "go" && isWord(l, 1, "to")) {
    s.k = Stmt::kGoTo, s.who = wordsOf(l, 2, n);
    pending_.push_back({c.file, l.line, l.tokens[std::min<size_t>(2, n - 1)].column, "place", s.who, ""});
    return true;
  }
  if (w0 == "light" || (w0 == "put" && isWord(l, 1, "out")) || w0 == "show" || w0 == "hide") {
    s.k = w0 == "light" ? Stmt::kLight : w0 == "put" ? Stmt::kPutOut : w0 == "show" ? Stmt::kShow : Stmt::kHide;
    const size_t from = w0 == "put" ? 2 : 1;
    size_t end = n;
    // "… in <a place>": the last `in` whose words name a place.
    for (size_t i = n; i-- > from + 1;)
      if (isWord(l, i, "in")) {
        end = i;
        s.where = wordsOf(l, i + 1, n);
        pending_.push_back({c.file, l.line, l.tokens[i + 1 < n ? i + 1 : i].column, "place", s.where, ""});
        break;
      }
    s.who = wordsOf(l, from, end);
    if (s.who.empty()) return c.bad(l, "which thing?", "light the cold hearth"), false;
    pending_.push_back({c.file, l.line, l.tokens[std::min(from, n - 1)].column, "thing", s.who, s.where.empty() && c.place ? c.place->id : s.where});
    return true;
  }
  if (w0 == "stop" && n == 1) {
    s.k = Stmt::kStop;
    return true;
  }
  if (w0 == "choice") return c.bad(l, "a choice says what you say or do after a colon", "choice: Where is the Ember?"), false;
  const std::string said = wordsOf(l, 0, std::min<size_t>(n, 2));
  std::string hint = ew::didYouMean(said, kStatements);
  if (hint.empty()) hint = ew::didYouMean(w0, kStatements);
  c.bad(l.tokens[0], "`" + said + "` isn't something a story does",
        hint.empty() ? "a line in a story: say: …, <a person>: …, choose, if, remember, add, you get, go to, light, show, hide, the end: …" : hint);
  return false;
}

bool Reader::cond(const Line& l, size_t from, size_t to, Ctx& c, Cond& out) {
  to = std::min(to, l.tokens.size());
  if (from >= to) {
    c.bad(l, "if what?", "if you have the lantern");
    return false;
  }
  out.line = l.line, out.column = l.tokens[from].column;
  // `or` binds loosest, then `and`.
  for (const char* join : {"or", "and"}) {
    std::vector<size_t> cuts;
    for (size_t i = from; i < to; ++i)
      if (isWord(l, i, join)) cuts.push_back(i);
    if (cuts.empty()) continue;
    out.k = join[0] == 'o' ? Cond::kOr : Cond::kAnd;
    size_t s = from;
    cuts.push_back(to);
    for (size_t e : cuts) {
      Cond k;
      if (!cond(l, s, e, c, k)) return false;
      out.kids.push_back(std::move(k));
      s = e + 1;
    }
    return true;
  }
  if (isWord(l, from, "not")) {
    out.k = Cond::kNot;
    Cond k;
    if (!cond(l, from + 1, to, c, k)) return false;
    out.kids.push_back(std::move(k));
    return true;
  }
  if (isWord(l, from, "you") && isWord(l, from + 1, "have") && isWord(l, from + 2, "been") && isWord(l, from + 3, "to")) {
    out.k = Cond::kBeen, out.name = wordsOf(l, from + 4, to);
    pending_.push_back({c.file, l.line, out.column, "place", out.name, ""});
    return true;
  }
  if (isWord(l, from, "you") && isWord(l, from + 1, "have")) {
    out.k = Cond::kHave, out.name = wordsOf(l, from + 2, to);
    if (!s_.item(out.name)) {
      std::vector<std::string> names;
      for (const Item& it : s_.items) names.push_back(it.name);
      c.bad(l.tokens[std::min(from + 2, to - 1)], "`" + out.name + "` isn't an item in this story", ew::didYouMean(out.name, names));
      return false;
    }
    return true;
  }
  // <number> is [not | at least | at most | more than | less than] <expr>
  if (from + 1 < to && isWord(l, from + 1, "is") && s_.numbers.count(lower(l.tokens[from].text))) {
    out.k = Cond::kCompare, out.name = lower(l.tokens[from].text);
    size_t i = from + 2;
    if (isWord(l, i, "not")) out.cmp = Cond::kIsNot, ++i;
    else if (isWord(l, i, "at") && isWord(l, i + 1, "least")) out.cmp = Cond::kAtLeast, i += 2;
    else if (isWord(l, i, "at") && isWord(l, i + 1, "most")) out.cmp = Cond::kAtMost, i += 2;
    else if (isWord(l, i, "more") && isWord(l, i + 1, "than")) out.cmp = Cond::kMore, i += 2;
    else if (isWord(l, i, "less") && isWord(l, i + 1, "than")) out.cmp = Cond::kLess, i += 2;
    Problems p;
    if (!ew::parseExpr(l.tokens, i, to, out.expr, p)) {
      c.take(p);
      return false;
    }
    return true;
  }
  out.k = Cond::kFlag, out.name = wordsOf(l, from, to);
  pending_.push_back({c.file, l.line, out.column, "flag", out.name, ""});
  return true;
}

namespace {

// The names an expression uses must be numbers the story remembers; its calls, `random`.
void checkExpr(const Expr& e, const Story& s, const std::string& file, Notes& n) {
  if (e.kind == Expr::kName && !s.numbers.count(lower(e.name))) {
    std::vector<std::string> names;
    for (const auto& [k, v] : s.numbers) names.push_back(k);
    Problem p;
    p.line = e.line, p.column = e.column, p.what = "`" + e.name + "` isn't something this story keeps count of", p.hint = ew::didYouMean(e.name, names);
    n.push_back({file, p});
  }
  if (e.kind == Expr::kCall && e.name != "random") {
    Problem p;
    p.line = e.line, p.column = e.column, p.what = "`" + e.name + "` isn't something a story can work out", p.hint = "random(1, 6)";
    n.push_back({file, p});
  }
  for (const Expr& a : e.args) checkExpr(a, s, file, n);
}
void checkStmts(const std::vector<Stmt>& ss, const Story& s, const std::string& file, Notes& n);
void checkCond(const Cond& c, const Story& s, const std::string& file, Notes& n) {
  if (c.k == Cond::kCompare) checkExpr(c.expr, s, file, n);
  for (const Cond& k : c.kids) checkCond(k, s, file, n);
}
void checkStmts(const std::vector<Stmt>& ss, const Story& s, const std::string& file, Notes& n) {
  for (const Stmt& x : ss) {
    if (x.k == Stmt::kSet || x.k == Stmt::kAdd || x.k == Stmt::kSubtract) checkExpr(x.expr, s, file, n);
    if (x.k == Stmt::kIf || x.k == Stmt::kChoice) checkCond(x.cond, s, file, n);
    checkStmts(x.body, s, file, n);
    checkStmts(x.otherwise, s, file, n);
  }
}
void ways(const std::vector<Stmt>& ss, std::vector<std::string>& to) {
  for (const Stmt& x : ss) {
    if (x.k == Stmt::kGoTo) to.push_back(x.who);
    ways(x.body, to);
    ways(x.otherwise, to);
  }
}

}  // namespace

bool Reader::check(Notes& n) {
  auto bad = [&](const Pending& p, std::string what, std::string hint, bool warning = false) {
    Problem pr;
    pr.line = p.line, pr.column = p.column, pr.what = std::move(what), pr.hint = std::move(hint), pr.warning = warning;
    n.push_back({p.file, pr});
  };
  std::vector<std::string> placeIds;
  for (const Place& p : s_.places) placeIds.push_back(p.id);
  std::vector<std::string> flags(s_.flags.begin(), s_.flags.end());
  for (const Pending& p : pending_) {
    if (p.what == "place" && s_.place(p.name) < 0) {
      const std::string hint = ew::didYouMean(p.name, placeIds);
      bad(p, "`" + p.name + "` isn't a place in this story", hint.empty() ? "add it: place " + p.name : hint);
    } else if (p.what == "item" && !s_.item(p.name)) {
      std::vector<std::string> names;
      for (const Item& it : s_.items) names.push_back(it.name);
      bad(p, "`" + p.name + "` isn't an item in this story", ew::didYouMean(p.name, names));
    } else if (p.what == "flag" && !s_.flags.count(p.name)) {
      const std::string hint = ew::didYouMean(p.name, flags);
      bad(p, "no line ever remembers `" + p.name + "`, so this is never true", hint.empty() ? "somewhere: remember " + p.name : hint);
    } else if (p.what == "thing") {
      const int pi = s_.place(p.place);
      if (pi < 0) continue;  // the place's own problem says it
      const Place& pl = s_.places[static_cast<size_t>(pi)];
      if (pl.find(p.name) < 0) {
        std::vector<std::string> keys;
        for (const Placed& t : pl.things)
          if (!t.key.empty()) keys.push_back(t.key);
        const std::string hint = ew::didYouMean(p.name, keys);
        bad(p, "there's nothing called `" + p.name + "` in " + pl.title + " that a story can name",
            hint.empty() ? "name it where it's placed: a well called the old well at 3 4 (or make it the only one of its kind there)" : hint);
      }
    }
  }
  if (s_.title.empty()) n.push_back({"story", Problem{1, 1, "the story has no `story` line", "story The Last Ember", false}});
  if (s_.places.empty()) n.push_back({"story", Problem{1, 1, "the story has no places", "place the village", false}});
  if (s_.endings.empty()) n.push_back({"story", Problem{1, 1, "the story never ends", "the end: The Long Dawn", false}});
  for (const Place& pl : s_.places)
    for (const Handler& h : pl.handlers) checkStmts(h.body, s_, h.file, n);
  // Every place reachable from the start.
  if (s_.place(s_.start) >= 0) {
    std::set<std::string> seen{s_.start};
    std::vector<std::string> todo{s_.start};
    while (!todo.empty()) {
      const Place& pl = s_.places[static_cast<size_t>(s_.place(todo.back()))];
      todo.pop_back();
      std::vector<std::string> to;
      for (const Placed& t : pl.things)
        if (!t.way.empty()) to.push_back(t.way);
      for (const Handler& h : pl.handlers) ways(h.body, to);
      for (const std::string& x : to)
        if (s_.place(x) >= 0 && seen.insert(x).second) todo.push_back(x);
    }
    for (const Place& pl : s_.places)
      if (!seen.count(pl.id)) n.push_back({pl.file, Problem{pl.line, 1, "nothing leads to " + pl.title, "a way to " + pl.id + " at 0 -18, in a place you can reach", true}});
  } else if (s_.start.empty()) {
    n.push_back({"story", Problem{1, 1, "the story doesn't say where you start", "you start in the village", false}});
  }
  return !hasErrors(n);
}

bool loadStory(Story& s, std::string_view engineWords, const std::function<const std::string*(const std::string&)>& file,
               Notes& notes) {
  Reader r(s);
  r.words(engineWords, "first.looped", notes);
  const std::string* first = file("story.looped");
  if (!first) {
    notes.push_back({"story.looped", ew::Problem{1, 1, "the story's first file, story.looped, is missing", "", false}});
    return false;
  }
  // The story line first (to learn the files), then the words, then the rest.
  Story head;
  Reader peek(head);
  Notes ignore;
  peek.read(*first, "story.looped", ignore);
  for (const std::string& f : head.files) {
    if (f.rfind("words:", 0) != 0) continue;
    const std::string name = f.substr(6);
    if (const std::string* t = file(name)) r.words(*t, name, notes);
    else notes.push_back({"story.looped", ew::Problem{1, 1, "`" + name + "` isn't in the story's folder", "", false}});
  }
  // The first file (title, numbers, items, the light you carry), then the rest in order.
  r.read(*first, "story.looped", notes);
  for (const std::string& f : s.files) {
    if (f.rfind("words:", 0) == 0) continue;
    if (const std::string* t = file(f)) r.read(*t, f, notes);
    else notes.push_back({"story.looped", ew::Problem{1, 1, "`" + f + "` isn't in the story's folder", "", false}});
  }
  return r.check(notes) && !hasErrors(notes);
}

std::vector<std::string> storyFiles(std::string_view first) {
  Story head;
  Reader peek(head);
  Notes ignore;
  peek.read(first, "story.looped", ignore);
  std::vector<std::string> out;
  for (const std::string& f : head.files) out.push_back(f.rfind("words:", 0) == 0 ? f.substr(6) : f);
  return out;
}

}  // namespace story
