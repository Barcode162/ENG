#include "story/run.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace story {

namespace {
constexpr size_t kBudget = 20000;  // steps for one thing you do; a story needs a few dozen
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

Runner::Runner(const Story& s, uint32_t seed) : s_(s), seed_(seed ? seed : 1) {
  looks_.resize(s.places.size());
  for (size_t p = 0; p < s.places.size(); ++p)
    for (const Placed& t : s.places[p].things) looks_[p].push_back(static_cast<uint8_t>((t.lit ? 1 : 0) | (t.hidden ? 2 : 0)));
  numbers_ = s.numbers;
}

uint32_t Runner::rnd() {
  seed_ ^= seed_ << 13, seed_ ^= seed_ >> 17, seed_ ^= seed_ << 5;
  return seed_;
}

void Runner::start() { travel(s_.place(s_.start)); }

bool Runner::lit(int t) const {
  if (place_ < 0 || t < 0 || static_cast<size_t>(t) >= looks_[static_cast<size_t>(place_)].size()) return false;
  return looks_[static_cast<size_t>(place_)][static_cast<size_t>(t)] & 1;
}
bool Runner::hidden(int t) const {
  if (place_ < 0 || t < 0 || static_cast<size_t>(t) >= looks_[static_cast<size_t>(place_)].size()) return true;
  return looks_[static_cast<size_t>(place_)][static_cast<size_t>(t)] & 2;
}
double Runner::number(std::string_view name) const {
  auto it = numbers_.find(std::string(name));
  return it == numbers_.end() ? 0 : it->second;
}
bool Runner::been(std::string_view place) const { return visited_.count(s_.place(place)) > 0; }

int Runner::thingIn(int place, std::string_view ref) const {
  if (place < 0) return -1;
  return s_.places[static_cast<size_t>(place)].find(ref);
}

void Runner::travel(int place) {
  if (place < 0) {
    error = "the story went to a place that isn't there";
    return;
  }
  stack_.clear();
  place_ = place;
  arrived_ = true;
  ++changes_;
  const Place& pl = s_.places[static_cast<size_t>(place)];
  const bool first = visited_.insert(place).second;
  // `when you arrive` runs after `when you first arrive`: pushed first, popped last.
  for (const Handler& h : pl.handlers)
    if (h.verb == Handler::kArrive) stack_.push_back({&h.body, 0});
  if (first)
    for (const Handler& h : pl.handlers)
      if (h.verb == Handler::kFirstArrive) stack_.push_back({&h.body, 0});
  run();
}

void Runner::next() {
  if (beat_.k != Beat::kSay && beat_.k != Beat::kSpeak) return;
  beat_ = Beat{};
  run();
}

void Runner::choose(int i) {
  if (beat_.k != Beat::kChoose || !choosing_ || i < 0 || static_cast<size_t>(i) >= beat_.picks.size()) return;
  const Stmt& c = choosing_->body[static_cast<size_t>(beat_.picks[static_cast<size_t>(i)])];
  beat_ = Beat{};
  choosing_ = nullptr;
  stack_.push_back({&c.body, 0});
  run();
}

std::vector<Runner::Action> Runner::actions() const {
  std::vector<Action> out;
  if (place_ < 0 || waiting() || !ending_.empty()) return out;
  const Place& pl = s_.places[static_cast<size_t>(place_)];
  for (size_t t = 0; t < pl.things.size(); ++t) {
    const Placed& th = pl.things[t];
    if (hidden(static_cast<int>(t)) || th.key.empty()) continue;
    bool leave = false;
    for (const Handler& h : pl.handlers) {
      if (h.target != th.key || h.verb == Handler::kArrive || h.verb == Handler::kFirstArrive) continue;
      if (h.verb == Handler::kLeave) leave = true;
      out.push_back({static_cast<int>(t), h.verb, std::string(verbWord(h.verb)) + " " + (h.verb == Handler::kLeave ? th.label : th.label)});
    }
    if (!th.way.empty() && !leave) out.push_back({static_cast<int>(t), Handler::kLeave, "Go to " + th.label});
  }
  return out;
}

void Runner::act(int thing, Handler::Verb verb) {
  if (place_ < 0 || waiting() || !ending_.empty() || thing < 0) return;
  const Place& pl = s_.places[static_cast<size_t>(place_)];
  if (static_cast<size_t>(thing) >= pl.things.size() || hidden(thing)) return;
  const Placed& th = pl.things[static_cast<size_t>(thing)];
  for (const Handler& h : pl.handlers)
    if (h.verb == verb && h.target == th.key) {
      stack_.push_back({&h.body, 0});
      run();
      return;
    }
  if (verb == Handler::kLeave && !th.way.empty()) travel(s_.place(th.way));
}

void Runner::run() {
  size_t steps = 0;
  while (!stack_.empty()) {
    if (++steps > kBudget) {
      error = "the story ran " + std::to_string(kBudget) + " steps without stopping";
      stack_.clear();
      return;
    }
    ++totalSteps_;
    Frame& f = stack_.back();
    if (f.i >= f.list->size()) {
      stack_.pop_back();
      continue;
    }
    const Stmt& s = (*f.list)[f.i++];
    switch (s.k) {
      case Stmt::kSay:
      case Stmt::kSpeak:
        beat_ = Beat{};
        beat_.k = s.k == Stmt::kSay ? Beat::kSay : Beat::kSpeak;
        beat_.who = s.who, beat_.text = s.text;
        return;
      case Stmt::kChoose: {
        Beat b;
        b.k = Beat::kChoose;
        for (size_t i = 0; i < s.body.size(); ++i)
          if (test(s.body[i].cond)) b.choices.push_back(s.body[i].text), b.picks.push_back(static_cast<int>(i));
        if (b.choices.empty()) break;
        beat_ = b;
        choosing_ = &s;
        return;
      }
      case Stmt::kChoice: break;
      case Stmt::kIf:
        if (test(s.cond)) stack_.push_back({&s.body, 0});
        else if (!s.otherwise.empty()) stack_.push_back({&s.otherwise, 0});
        break;
      case Stmt::kRemember: flags_.insert(s.who); break;
      case Stmt::kForget: flags_.erase(s.who); break;
      case Stmt::kSet: numbers_[s.who] = eval(s.expr); break;
      case Stmt::kAdd: numbers_[s.who] += eval(s.expr); break;
      case Stmt::kSubtract: numbers_[s.who] -= eval(s.expr); break;
      case Stmt::kGet:
        if (std::find(items_.begin(), items_.end(), s.who) == items_.end()) items_.push_back(s.who), ++changes_;
        break;
      case Stmt::kLose: {
        auto it = std::find(items_.begin(), items_.end(), s.who);
        if (it != items_.end()) items_.erase(it), ++changes_;
        break;
      }
      case Stmt::kGoTo: travel(s_.place(s.who)); return;
      case Stmt::kLight:
      case Stmt::kPutOut:
      case Stmt::kShow:
      case Stmt::kHide: {
        const int p = s.where.empty() ? place_ : s_.place(s.where);
        const int t = thingIn(p, s.who);
        if (t < 0) break;
        uint8_t& look = looks_[static_cast<size_t>(p)][static_cast<size_t>(t)];
        if (s.k == Stmt::kLight) look |= 1;
        if (s.k == Stmt::kPutOut) look &= static_cast<uint8_t>(~1);
        if (s.k == Stmt::kShow) look &= static_cast<uint8_t>(~2);
        if (s.k == Stmt::kHide) look |= 2;
        ++changes_;
        break;
      }
      case Stmt::kEnd:
        ending_ = s.text;
        beat_ = Beat{};
        beat_.k = Beat::kEnd, beat_.text = s.text;
        stack_.clear();
        return;
      case Stmt::kStop: stack_.clear(); break;
    }
  }
  beat_ = Beat{};
}

bool Runner::test(const Cond& c) {
  switch (c.k) {
    case Cond::kAlways: return true;
    case Cond::kFlag: return flags_.count(c.name) > 0;
    case Cond::kNot: return !test(c.kids[0]);
    case Cond::kAnd:
      for (const Cond& k : c.kids)
        if (!test(k)) return false;
      return true;
    case Cond::kOr:
      for (const Cond& k : c.kids)
        if (test(k)) return true;
      return false;
    case Cond::kHave: return std::find(items_.begin(), items_.end(), c.name) != items_.end();
    case Cond::kBeen: return been(c.name);
    case Cond::kCompare: {
      const double a = number(c.name), b = eval(c.expr);
      switch (c.cmp) {
        case Cond::kIs: return std::fabs(a - b) < 1e-9;
        case Cond::kIsNot: return std::fabs(a - b) >= 1e-9;
        case Cond::kAtLeast: return a >= b - 1e-9;
        case Cond::kAtMost: return a <= b + 1e-9;
        case Cond::kMore: return a > b + 1e-9;
        case Cond::kLess: return a < b - 1e-9;
      }
    }
  }
  return false;
}

double Runner::eval(const ew::Expr& e) {
  switch (e.kind) {
    case ew::Expr::kNumber: return e.value;
    case ew::Expr::kName: return number(lower(e.name));
    case ew::Expr::kNegate: return -eval(e.args[0]);
    case ew::Expr::kBinary: {
      const double a = eval(e.args[0]), b = eval(e.args[1]);
      switch (e.op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b == 0 ? 0 : a / b;
      }
      return 0;
    }
    case ew::Expr::kCall:
      if (e.name == "random" && e.args.size() == 2) {
        const double lo = std::floor(eval(e.args[0])), hi = std::floor(eval(e.args[1]));
        if (hi <= lo) return lo;
        return lo + static_cast<double>(rnd() % static_cast<uint32_t>(hi - lo + 1));
      }
      return 0;
  }
  return 0;
}

std::string Runner::save() const {
  std::ostringstream o;
  o << "place " << place_ << "\n";
  for (const auto& [k, v] : numbers_) o << "n " << v << " " << k << "\n";
  for (const std::string& f : flags_) o << "f " << f << "\n";
  for (const std::string& i : items_) o << "i " << i << "\n";
  for (int v : visited_) o << "v " << v << "\n";
  for (size_t p = 0; p < looks_.size(); ++p)
    for (size_t t = 0; t < looks_[p].size(); ++t) {
      const Placed& th = s_.places[p].things[t];
      const uint8_t start = static_cast<uint8_t>((th.lit ? 1 : 0) | (th.hidden ? 2 : 0));
      if (looks_[p][t] != start) o << "t " << p << " " << t << " " << static_cast<int>(looks_[p][t]) << "\n";
    }
  return o.str();
}

bool Runner::load(std::string_view text) {
  std::istringstream in{std::string(text)};
  std::string line;
  int place = -1;
  stack_.clear(), choosing_ = nullptr, beat_ = Beat{}, flags_.clear(), items_.clear(), visited_.clear(), ending_.clear(), error.clear();
  numbers_ = s_.numbers;
  for (size_t p = 0; p < s_.places.size(); ++p)
    for (size_t t = 0; t < s_.places[p].things.size(); ++t) {
      const Placed& th = s_.places[p].things[t];
      looks_[p][t] = static_cast<uint8_t>((th.lit ? 1 : 0) | (th.hidden ? 2 : 0));
    }
  while (std::getline(in, line)) {
    if (line.size() < 2) continue;
    const std::string rest = line.substr(line.find(' ') + 1);
    if (line.rfind("place ", 0) == 0) place = std::atoi(rest.c_str());
    else if (line[0] == 'n') {
      const size_t sp = rest.find(' ');
      if (sp != std::string::npos) numbers_[rest.substr(sp + 1)] = std::atof(rest.substr(0, sp).c_str());
    } else if (line[0] == 'f') flags_.insert(rest);
    else if (line[0] == 'i' && s_.item(rest)) items_.push_back(rest);
    else if (line[0] == 'v') visited_.insert(std::atoi(rest.c_str()));
    else if (line[0] == 't') {
      int p = 0, t = 0, v = 0;
      if (std::sscanf(rest.c_str(), "%d %d %d", &p, &t, &v) == 3 && p >= 0 && static_cast<size_t>(p) < looks_.size() && t >= 0 &&
          static_cast<size_t>(t) < looks_[static_cast<size_t>(p)].size())
        looks_[static_cast<size_t>(p)][static_cast<size_t>(t)] = static_cast<uint8_t>(v & 3);
    }
  }
  if (place < 0 || static_cast<size_t>(place) >= s_.places.size()) return false;
  place_ = place;
  arrived_ = true;
  ++changes_;
  return true;
}

}  // namespace story
