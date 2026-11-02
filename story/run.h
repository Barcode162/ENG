#pragma once
// Running a story (Looped Script's story words, story/story.h): where you
// are, what's been said and done, what you carry, and what happens next.
// The game asks it what to show (a line someone says, choices, the end),
// tells it what you did (talked to Mira, picked a choice, took the bread),
// and redraws the place when it changes. Every step is counted, so a story
// can never freeze the game. Plain C++, no drawing.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "story/story.h"

namespace story {

class Runner {
 public:
  explicit Runner(const Story& s, uint32_t seed = 1);

  // To the story's start (its first place's arrival).
  void start();

  // What's being shown now.
  struct Beat {
    enum K { kNone, kSay, kSpeak, kChoose, kEnd } k = kNone;
    std::string who, text;
    std::vector<std::string> choices;
    std::vector<int> picks;  // each shown choice's index in its `choose`
  };
  const Beat& beat() const { return beat_; }
  bool waiting() const { return beat_.k != Beat::kNone; }
  void next();          // a line was read
  void choose(int i);   // one of beat().choices

  // What you can do here now (the page's buttons, when you're close).
  struct Action {
    int thing = -1;
    Handler::Verb verb = Handler::kLook;
    std::string label;  // "Talk to Mira"
  };
  std::vector<Action> actions() const;
  void act(int thing, Handler::Verb verb);

  // Where you are, and how its things look now.
  int place() const { return place_; }
  bool lit(int thing) const;
  bool hidden(int thing) const;
  // Goes up whenever the place or a thing's look changes.
  uint32_t changes() const { return changes_; }
  bool arrived() const { return arrived_; }  // just came to this place (the game moves you to its door)
  void clearArrived() { arrived_ = false; }

  const std::vector<std::string>& items() const { return items_; }
  double number(std::string_view name) const;
  bool flag(std::string_view name) const { return flags_.count(std::string(name)) > 0; }
  bool been(std::string_view place) const;
  const std::string& ending() const { return ending_; }
  size_t steps() const { return totalSteps_; }

  // Saving between visits (when nothing is waiting): plain lines of text.
  std::string save() const;
  bool load(std::string_view text);

  std::string error;  // what went wrong, in words

 private:
  void run();
  void travel(int place);
  bool test(const Cond& c);
  double eval(const ew::Expr& e);
  int thingIn(int place, std::string_view ref) const;
  uint32_t rnd();

  const Story& s_;
  struct Frame {
    const std::vector<Stmt>* list;
    size_t i;
  };
  std::vector<Frame> stack_;
  const Stmt* choosing_ = nullptr;
  Beat beat_;
  int place_ = -1;
  std::vector<std::vector<uint8_t>> looks_;  // per place, per thing: 1 lit, 2 hidden
  std::map<std::string, double> numbers_;
  std::set<std::string> flags_;
  std::vector<std::string> items_;
  std::set<int> visited_;
  std::string ending_;
  uint32_t changes_ = 0, seed_ = 1;
  bool arrived_ = false;
  size_t totalSteps_ = 0;
};

}  // namespace story
