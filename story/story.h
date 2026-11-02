#pragma once
// Looped Script's story words. A story is a few .looped files: the story
// itself (its title, where you start, what it remembers, which files hold
// its places), its colours, its kinds of things (made of shapes, giving
// light with the engine's own light words), its people, the things you can
// carry, and its places, each with what happens there:
//
//   place the village
//     the sky: night
//     a hearth called the cold hearth at 0 -9, unlit
//     Mira at -2 -5, facing 20
//     when you talk to Mira
//       Mira: The Ember has gone out.
//       choose
//         choice: Where do I find it?
//           Mira: Up the hill.
//
// The lines are read by the engine's own reader (looped/words/syntax.h):
// indentation says what belongs to what, and every problem has its file,
// line, column, plain words and a "did you mean" hint. Text after a
// speaker's colon is prose, kept exactly as written. Plain C++.
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "looped/math.h"
#include "looped/words/syntax.h"
#include "looped/words/words.h"

namespace story {

using engine::Vec3;
namespace ew = engine::words;

// A problem in one file.
struct Note {
  std::string file;
  ew::Problem p;
  std::string text() const { return file + ", " + p.text(); }
};
using Notes = std::vector<Note>;
bool hasErrors(const Notes& n);

enum class Shape { kBox, kPost, kCone, kBall, kRoof, kDisc };
const char* shapeName(Shape s);

// One shape of a kind, in the kind's own space (x right, y up, z towards you).
struct Part {
  Shape shape = Shape::kBox;
  Vec3 size;  // box/roof: wide, deep, high; post/cone/ball/disc: across, across, high
  Vec3 at;    // where its foot's centre sits
  float turn = 0;
  Vec3 colour{0.5f, 0.5f, 0.5f};
  bool glows = false;  // glows only while its thing is lit
  bool onlyLit = false;  // gone while its thing is out (a flame)
  int line = 0;
};

// A light on a thing or a person: the engine's light words ("a flame that flickers").
struct LightPart {
  std::string phrase;
  Vec3 at;
  ew::Thing thing;
};

struct Kind {
  std::string name;
  std::vector<std::string> forms;  // plurals: "pine trees"
  std::vector<Part> parts;
  std::vector<LightPart> lights;
  float blocks = -1;  // how far it keeps you away (m); -1: worked out from its shapes
  bool wall = false;  // keeps you off its whole shape
  std::string file;
  int line = 0;
};

enum class Hat { kNone, kHood, kPointed, kHair };
struct Person {
  std::string name;  // as written: "Mira"
  Vec3 robe{0.4f, 0.3f, 0.25f}, skin{0.75f, 0.55f, 0.42f}, hat{0.2f, 0.15f, 0.1f};
  Hat hatShape = Hat::kHair;
  float height = 1.7f;
  bool spirit = false;  // glows softly (a spirit, a ghost)
  std::vector<LightPart> lights;
  std::string about;
  std::string file;
  int line = 0;
};

struct Item {
  std::string name;  // "silver bell" (no article)
  std::string about;
  std::string file;
  int line = 0;
};

// What to check: a tree of clauses.
struct Cond {
  enum K { kAlways, kFlag, kNot, kAnd, kOr, kCompare, kHave, kBeen } k = kAlways;
  std::string name;  // flag phrase, number name, item, place
  enum Cmp { kIs, kIsNot, kAtLeast, kAtMost, kMore, kLess } cmp = kIs;
  ew::Expr expr;
  std::vector<Cond> kids;
  int line = 0, column = 0;
};

struct Stmt {
  enum K {
    kSay, kSpeak, kChoose, kChoice, kIf, kRemember, kSet, kForget, kAdd, kSubtract,
    kGet, kLose, kGoTo, kLight, kPutOut, kShow, kHide, kEnd, kStop
  } k = kSay;
  std::string text;    // prose
  std::string who;     // speaker, flag, number, item, place, thing
  std::string where;   // a thing's place (show Pip in the village); "" = here
  Cond cond;
  ew::Expr expr;
  std::vector<Stmt> body;       // if / choice / choose's choices
  std::vector<Stmt> otherwise;  // if: the else (an `otherwise if` is an if inside it)
  int line = 0;
};

struct Handler {
  enum Verb { kArrive, kFirstArrive, kTalk, kLook, kTake, kUse, kLeave } verb = kArrive;
  std::string target;  // a thing's key, or a place for kLeave
  std::vector<Stmt> body;
  std::string file;
  int line = 0;
};
const char* verbWord(Handler::Verb v);  // "Talk to", "Look at"...

struct Placed {
  std::string kind;    // a kind's name ("" for a person)
  std::string person;  // a person's name
  std::string key;     // how scripts name it: "cold hearth", "mira", "well" ("" if it can't be named)
  std::string label;   // how the page shows it: "the cold hearth", "Mira"
  std::string way;     // a way's place
  Vec3 at;
  float turn = 0, scale = 1;
  bool lit = true, hidden = false;
  int line = 0;
};

struct Sky {
  Vec3 top, horizon;   // the sky's colours (linear)
  Vec3 moonDir;        // towards the moon or sun
  Vec3 moon;           // its light on the ground (lux, per channel)
  Vec3 ambient;        // the sky's light from everywhere (lux)
  float exposure = 1;
  bool stars = false;
};
bool skyNamed(std::string_view name, Sky& out);
std::vector<std::string> skyNames();

struct Place {
  std::string id;     // "the village"
  std::string title;  // "Emberhollow"
  std::string skyName = "night";
  Sky sky;
  float mist = 0.03f;
  Vec3 ground{0.2f, 0.25f, 0.15f};
  float groundW = 40, groundD = 40;
  Vec3 start;
  float facing = 0;
  // A land of hills and mountains made from a seed (world/land.h), instead of flat ground.
  bool hasLand = false;
  uint32_t landSeed = 7;
  std::vector<Placed> things;
  std::vector<Handler> handlers;
  std::string file;
  int line = 0;
  // The thing a script means by `ref` ("the cold hearth", "Mira", "the well"), or -1.
  int find(std::string_view ref) const;
};

struct Story {
  std::string title, by;
  std::string start;  // a place id
  std::vector<std::string> files;  // the other files it reads
  std::map<std::string, double> numbers;  // name → its start value
  std::map<std::string, Vec3> colours;
  std::vector<Kind> kinds;
  std::vector<Person> persons;
  std::vector<Item> items;
  std::vector<Place> places;
  std::set<std::string> flags;  // every flag some line remembers
  std::vector<std::string> endings;
  // The light you carry, once you have its item.
  bool carries = false;
  LightPart carried;
  std::string carriedItem;
  ew::Words words;  // the engine's words, for lights

  const Kind* kind(std::string_view name) const;  // by name or form
  const Person* person(std::string_view name) const;
  const Item* item(std::string_view name) const;
  int place(std::string_view id) const;
};

// A kind's footprint on the ground, in its own space (before turning and scaling).
void bounds(const Kind& k, float& x0, float& z0, float& x1, float& z1);
// How far a thing keeps you away when it isn't a wall (m).
float blockRadius(const Kind& k);

// Lower case, single spaces, without a leading a / an / the.
std::string keyOf(std::string_view s);

// Reading a story: the dictionary files first (the engine's light words),
// then the story's first file (it names the others), then each of them,
// then check() for what spans files.
class Reader {
 public:
  explicit Reader(Story& s) : s_(s) {}
  bool words(std::string_view text, const std::string& file, Notes& n);
  bool read(std::string_view text, const std::string& file, Notes& n);
  bool check(Notes& n);

 private:
  struct Ctx;
  void top(const ew::Line& l, Ctx& c);
  void storyLine(const ew::Line& l, Ctx& c);
  void colourLine(const ew::Line& l, Ctx& c);
  void itemLine(const ew::Line& l, Ctx& c);
  void kindBlock(const ew::Line& l, Ctx& c);
  void personBlock(const ew::Line& l, Ctx& c);
  void placeBlock(const ew::Line& l, Ctx& c);
  bool partLine(const ew::Line& l, Ctx& c, Part& out);
  bool lightLine(const ew::Line& l, Ctx& c, LightPart& out, size_t placeEnd = 0);
  void thingLine(const ew::Line& l, Ctx& c, Place& pl);
  bool thingOptions(const ew::Line& l, size_t i, Ctx& c, Placed& t);
  void handler(const ew::Line& l, Ctx& c, Place& pl);
  std::vector<Stmt> block(const std::vector<ew::Line>& lines, Ctx& c);
  bool stmt(const ew::Line& l, Ctx& c, Stmt& out);
  bool cond(const ew::Line& l, size_t from, size_t to, Ctx& c, Cond& out);
  bool colour(const ew::Line& l, size_t& i, size_t to, Ctx& c, Vec3& out);

  Story& s_;
  struct Pending {  // names checked once every file is read
    std::string file;
    int line, column;
    std::string what, name, place;
  };
  std::vector<Pending> pending_;
  std::vector<std::pair<std::string, int>> handlerFiles_;
};

// Reads a whole story: the engine's words (`engineWords`), then the
// story's first file, the files it names (through `file`, which returns the
// text of a file in the story's folder, or nullptr if there's none), and
// checks it. The game and the tests use the same reading.
bool loadStory(Story& s, std::string_view engineWords, const std::function<const std::string*(const std::string&)>& file,
               Notes& notes);
// The files a story's first file names (its own reading, thrown away).
std::vector<std::string> storyFiles(std::string_view first);

}  // namespace story
