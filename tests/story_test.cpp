// The story's tests (native, no browser): the story reads clean; broken
// copies give the right file, line and hint; thousands of random
// playthroughs never go wrong and reach every place and every ending;
// saving and loading keeps everything; every light gives light.
//   build/story_test [stories/the-last-ember]
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "looped/world/loop.h"
#include "story/run.h"
#include "story/story.h"

using namespace story;

namespace {

int failures = 0;
#define CHECK(cond, ...)                         \
  do {                                           \
    if (!(cond)) {                               \
      ++failures;                                \
      std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__);                  \
      std::printf("\n");                         \
    }                                            \
  } while (0)

std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::map<std::string, std::string> readFolder(const std::string& dir, const std::vector<std::string>& names) {
  std::map<std::string, std::string> m;
  for (const std::string& n : names) m[n] = slurp(dir + "/" + n);
  return m;
}

bool load(Story& s, const std::map<std::string, std::string>& files, Notes& notes) {
  static const std::string engine = slurp("looped/words/first.looped");
  return loadStory(s, engine, [&](const std::string& n) -> const std::string* {
    auto it = files.find(n);
    return it == files.end() ? nullptr : &it->second;
  }, notes);
}

int lineOf(const std::string& text, const std::string& find) {
  const size_t at = text.find(find);
  if (at == std::string::npos) return -1;
  int line = 1;
  for (size_t i = 0; i < at; ++i) line += text[i] == '\n';
  return line;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "stories/the-last-ember";
  const std::string first = slurp(dir + "/story.looped");
  std::vector<std::string> names = storyFiles(first);
  names.push_back("story.looped");
  const auto files = readFolder(dir, names);

  // 1. The story reads clean.
  Story s;
  Notes notes;
  const bool ok = load(s, files, notes);
  for (const Note& n : notes) std::printf("  %s %s\n", n.p.warning ? "warning:" : "problem:", n.text().c_str());
  CHECK(ok && notes.empty(), "the story should read with no problems (%zu)", notes.size());
  if (!ok) return 1;
  std::printf("read: %zu places, %zu people, %zu kinds, %zu items, %zu endings\n", s.places.size(), s.persons.size(), s.kinds.size(),
              s.items.size(), s.endings.size());

  // 2. Broken copies: the right file, line and words.
  struct Broken {
    std::string file, find, replace, says;
  };
  const std::vector<Broken> broken = {
      {"village.looped", "      Mira: There you are.", "      Mria: There you are.", "did you mean `mira`?"},
      {"woods.looped", "          go to the ford", "          go to the frod", "`frod` isn't a place in this story; did you mean `ford`?"},
      {"woods.looped", "    if pip is home", "    if pip is hme", "no line ever remembers `pip is hme`"},
      {"village.looped", "          add 1 to kindness", "          add 1 to kindnes", "did you mean `kindness`?"},
      {"village.looped", "          you get the honey bread", "          you get the honey bred", "did you mean `honey bread`?"},
      {"things.looped", "  a box 4.2 by 3.6 by 2.6 m, plaster", "  a box 4.2 by 3.6 by 2.6 m, plastr", "did you mean `plaster`?"},
      {"things.looped", "  a light 2.95 m up: a lamp flame", "  a light 2.95 m up: a lamp flam", "flam"},
      {"village.looped", "  the sky: night", "  the sky: nigt", "did you mean `night`?"},
      {"village.looped", "  when you talk to Tobin", "  when you tlak to Tobin", "isn't something a story waits for"},
      {"woods.looped", "      light the toadstool ring", "      light the toadstool rings", "did you mean `toadstool ring`?"},
      {"things.looped", "  a cone 2.8 by 2.6 m, 1 m up, pine", "  a cnoe 2.8 by 2.6 m, 1 m up, pine", "did you mean `cone`?"},
      {"village.looped", "  a well at 6 2.4", "  a wel at 6 2.4", "did you mean `well`?"},
      {"peak.looped", "      remember met ysolde\n", "      remember met ysolde\n      sing loudly\n", "`sing loudly` isn't something a story does"},
      {"hill.looped", "              go to morning", "              go to morning\n              say: Too late.", "this line never happens"},
  };
  for (const Broken& b : broken) {
    auto copy = files;
    std::string& text = copy[b.file];
    const int line = lineOf(text, b.find);
    CHECK(line > 0, "the test's text isn't in %s: %s", b.file.c_str(), b.find.c_str());
    if (line <= 0) continue;
    text.replace(text.find(b.find), b.find.size(), b.replace);
    Story bs;
    Notes bn;
    load(bs, copy, bn);
    bool found = false;
    for (const Note& n : bn)
      if (n.file == b.file && n.p.line == line + (b.find.back() == '\n' && b.replace.size() > b.find.size() ? 1 : 0) &&
          n.text().find(b.says) != std::string::npos)
        found = true;
    if (b.says == "this line never happens")
      for (const Note& n : bn) found = found || (n.file == b.file && n.text().find(b.says) != std::string::npos && n.p.warning);
    CHECK(found, "%s line %d (%s) should say \"%s\"", b.file.c_str(), line, b.replace.c_str(), b.says.c_str());
    if (!found)
      for (const Note& n : bn) std::printf("    got: %s\n", n.text().c_str());
  }

  // 3. Random playthroughs: never stuck, never wrong; every place and ending reached.
  std::map<std::string, int> endings;
  std::set<int> places;
  int stuck = 0;
  size_t longest = 0;
  for (uint32_t seed = 1; seed <= 4000; ++seed) {
    Runner r(s, seed);
    uint32_t x = seed * 747796405u + 2891336453u;
    auto pick = [&x](size_t n) {
      x ^= x << 13, x ^= x >> 17, x ^= x << 5;
      return static_cast<int>(x % static_cast<uint32_t>(n));
    };
    r.start();
    std::map<std::string, int> tried;
    size_t moves = 0;
    for (; moves < 600 && r.ending().empty(); ++moves) {
      places.insert(r.place());
      CHECK(r.error.empty(), "seed %u: %s", seed, r.error.c_str());
      if (!r.error.empty()) break;
      const Runner::Beat& b = r.beat();
      if (b.k == Runner::Beat::kSay || b.k == Runner::Beat::kSpeak) {
        CHECK(!b.text.empty(), "seed %u: an empty line", seed);
        r.next();
      } else if (b.k == Runner::Beat::kChoose) {
        CHECK(!b.choices.empty(), "seed %u: a choice with no choices", seed);
        r.choose(pick(b.choices.size()));
      } else {
        auto acts = r.actions();
        if (acts.empty()) {
          ++stuck;
          break;
        }
        // Mostly what hasn't been tried much here, so a run gets somewhere.
        int best = pick(acts.size());
        if (pick(4) != 0) {
          int fewest = 1 << 30;
          for (size_t i = 0; i < acts.size(); ++i) {
            const int t = tried[std::to_string(r.place()) + acts[i].label];
            if (t < fewest || (t == fewest && pick(2))) fewest = t, best = static_cast<int>(i);
          }
        }
        ++tried[std::to_string(r.place()) + acts[static_cast<size_t>(best)].label];
        r.act(acts[static_cast<size_t>(best)].thing, acts[static_cast<size_t>(best)].verb);
      }
    }
    if (!r.ending().empty()) ++endings[r.ending()], longest = std::max(longest, moves);
  }
  CHECK(stuck == 0, "%d playthroughs got stuck (nothing to do and no ending)", stuck);
  for (const std::string& e : s.endings) {
    CHECK(endings[e] > 0, "no playthrough reached the ending \"%s\"", e.c_str());
    std::printf("ending \"%s\": %d of 4000\n", e.c_str(), endings[e]);
  }
  CHECK(places.size() == s.places.size(), "playthroughs reached %zu of %zu places", places.size(), s.places.size());

  // 4. A walk-through a player could take, to the best ending.
  {
    Runner r(s, 7);
    r.start();
    auto doAct = [&](const std::string& label) {
      while (r.waiting() && r.beat().k != Runner::Beat::kChoose) r.next();
      for (const auto& a : r.actions())
        if (a.label == label) return r.act(a.thing, a.verb), true;
      CHECK(false, "walk-through: no \"%s\" in %s", label.c_str(), s.places[static_cast<size_t>(r.place())].title.c_str());
      return false;
    };
    auto say = [&](const std::string& start) {
      while (r.waiting() && r.beat().k != Runner::Beat::kChoose) r.next();
      const auto& c = r.beat().choices;
      for (size_t i = 0; i < c.size(); ++i)
        if (c[i].rfind(start, 0) == 0) return r.choose(static_cast<int>(i)), true;
      CHECK(false, "walk-through: no choice \"%s\"", start.c_str());
      return false;
    };
    doAct("Talk to Mira"), say("What can we do"), say("I'll bring");
    doAct("Talk to Tobin"), say("It's all right to be scared. Here");
    doAct("Go to the woods");
    doAct("Talk to Pip"), say("Let me try");
    doAct("Use the toadstool ring");
    doAct("Talk to the great oak"), say("Pip says");
    doAct("Go to the ford");
    doAct("Use the ferry lantern");
    doAct("Talk to Old Bram");
    doAct("Talk to The Lady of the Ford"), say("Honey bread");
    doAct("Talk to Old Bram"), say("Yes");
    doAct("Go to the peak");
    doAct("Talk to Ysolde"), say("Why do you live");
    doAct("Talk to the Watcher"), say("Fire. Kept alone");
    // Saving and loading half way keeps everything.
    while (r.waiting()) r.next();
    const std::string saved = r.save();
    Runner r2(s, 7);
    CHECK(r2.load(saved), "loading a save");
    CHECK(r2.save() == saved, "a loaded save saves the same");
    CHECK(r2.number("embers") == 3, "three embers by the peak (%g)", r2.number("embers"));
    doAct("Go to the hill");
    doAct("Talk to Sorrel"), say("Take them"), say("I promise");
    while (r.waiting() && r.beat().k != Runner::Beat::kEnd) r.next();
    CHECK(r.ending() == "The Long Dawn", "the walk-through should end with The Long Dawn, not \"%s\"", r.ending().c_str());
    std::printf("walk-through: %s, kindness %g, courage %g, %zu steps\n", r.ending().c_str(), r.number("kindness"), r.number("courage"), r.steps());
  }

  // 5. Every light gives light.
  engine::world::Clock clock;
  clock.seconds = 3.2;
  auto lit = [&](const LightPart& lp, const std::string& who) {
    std::vector<ew::Words::Glow> g;
    s.words.lights(lp.thing, clock, g);
    float lm = 0;
    for (const auto& x : g) lm += x.lumens;
    CHECK(lm > 0, "%s: `%s` gives no light", who.c_str(), lp.phrase.c_str());
  };
  for (const Kind& k : s.kinds)
    for (const LightPart& lp : k.lights) lit(lp, k.name);
  for (const Person& p : s.persons)
    for (const LightPart& lp : p.lights) lit(lp, p.name);
  if (s.carries) lit(s.carried, "your lantern");

  std::printf(failures ? "%d FAILED\n" : "all story tests passed\n", failures);
  return failures ? 1 : 0;
}
