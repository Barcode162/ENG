// Every place of the story turns into triangles: no broken numbers, sizes a
// phone can draw, and lighting a thing changes how it looks.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "game/mesh.h"
#include "story/run.h"

namespace {
std::string slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "stories/the-last-ember";
  std::map<std::string, std::string> files;
  const std::string first = slurp(dir + "/story.looped");
  files["story.looped"] = first;
  for (const std::string& f : story::storyFiles(first)) files[f] = slurp(dir + "/" + f);
  story::Story s;
  story::Notes notes;
  if (!story::loadStory(s, slurp("looped/words/first.looped"), [&](const std::string& n) -> const std::string* {
        auto it = files.find(n);
        return it == files.end() ? nullptr : &it->second;
      }, notes)) {
    std::printf("FAIL: the story doesn't read\n");
    return 1;
  }
  int failures = 0;
  story::Runner r(s);
  r.start();
  size_t most = 0;
  for (size_t p = 0; p < s.places.size(); ++p) {
    // Visit by loading a save that puts you there.
    story::Runner at(s);
    at.load("place " + std::to_string(p) + "\n");
    game::Mesh m;
    game::buildPlace(m, s, at);
    bool finite = true;
    for (const auto& v : m.v)
      for (float x : v.pos) finite = finite && std::isfinite(x);
    for (uint32_t i : m.i) finite = finite && i < m.v.size();
    std::printf("%-24s %7zu vertices, %7zu triangles, %3zu things\n", s.places[p].title.c_str(), m.v.size(), m.i.size() / 3, s.places[p].things.size());
    if (!finite) ++failures, std::printf("FAIL: %s has broken numbers\n", s.places[p].title.c_str());
    most = std::max(most, m.i.size() / 3);
  }
  if (most > 300000) ++failures, std::printf("FAIL: a place has %zu triangles (a phone wants under 300,000)\n", most);
  // People and the sky.
  for (const auto& p : s.persons) {
    game::Mesh m;
    game::addPerson(m, p, game::Place3{});
    if (m.v.empty()) ++failures, std::printf("FAIL: %s has no shape\n", p.name.c_str());
  }
  game::Mesh sky;
  story::Sky night;
  story::skyNamed("night", night);
  game::addSky(sky, night, 1);
  if (sky.v.empty()) ++failures, std::printf("FAIL: no sky\n");
  // Lighting the cold hearth adds its flames.
  {
    const int village = s.place("village");
    const int hearth = s.places[static_cast<size_t>(village)].find("cold hearth");
    story::Runner a(s), b(s);
    a.load("place " + std::to_string(village) + "\n");
    b.load("place " + std::to_string(village) + "\nt " + std::to_string(village) + " " + std::to_string(hearth) + " 1\n");
    game::Mesh ma, mb;
    game::buildPlace(ma, s, a);
    game::buildPlace(mb, s, b);
    if (mb.v.size() <= ma.v.size()) ++failures, std::printf("FAIL: lighting the hearth doesn't add its flames\n");
  }
  std::printf(failures ? "%d FAILED\n" : "all mesh tests passed\n", failures);
  return failures ? 1 : 0;
}
