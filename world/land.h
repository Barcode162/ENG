#pragma once
// The land (J121): an island of our own, made from rules, not a map.
// Its height anywhere is worked out from a seed: a coastline, rolling hills,
// ridged mountains where the mountain-mask says so, small bumps underfoot.
// Nothing is stored; any point can be asked for at any time, so a world
// 6 km across costs no memory until you look at it.
//
// Drawing it: a quadtree. The whole island is one square; a square close to
// you splits into four, again and again, down to squares 32 m across with a
// corner every metre. Far squares stay big and coarse. Each square is a
// 32 × 32 grid with a skirt hanging from its edges, so a fine square next to
// a coarse one shows no gap. The same idea as the clusters (detail where
// it's seen), for ground that goes on for kilometres. Plain C++.
#include <cstdint>
#include <map>
#include <vector>

#include "game/mesh.h"
#include "looped/math.h"

namespace terrain {

using engine::Vec3;

struct LandShape {
  uint32_t seed = 7;
  float size = 6000;       // across (m)
  float mountains = 900;   // how high the highest ridges reach (m)
  float hills = 70;
  float sea = 0;           // the sea's height
};

class Land {
 public:
  explicit Land(const LandShape& s);
  const LandShape& shape() const { return s_; }
  float height(float x, float z) const;
  Vec3 normal(float x, float z, float step = 1.0f) const;
  // The ground's colour there (linear), from its height, slope and a little noise.
  Vec3 colour(float x, float z, float h, Vec3 n) const;

 private:
  float noise(float x, float z, uint32_t salt) const;  // -1..1, smooth
  float fbm(float x, float z, int octaves, uint32_t salt) const;
  float ridged(float x, float z, int octaves, uint32_t salt) const;
  LandShape s_;
};

// The squares to draw from where you stand, and their triangles.
class LandDraw {
 public:
  explicit LandDraw(const Land& land) : land_(land) {}
  // Picks the squares for an eye at `eye`; true if the pick changed.
  bool pick(Vec3 eye);
  // Every picked square's triangles, and the sea, in one mesh.
  void mesh(game::Mesh& out);
  size_t squares() const { return picked_.size(); }

  static constexpr int kGrid = 32;
  static constexpr float kFinest = 32;  // the smallest square (m)

 private:
  struct Key {
    int level, x, z;
    bool operator<(const Key& o) const { return level != o.level ? level < o.level : x != o.x ? x < o.x : z < o.z; }
  };
  void visit(int level, int x, int z, Vec3 eye, std::vector<Key>& out) const;
  const game::Mesh& square(const Key& k);
  float squareSize(int level) const;

  const Land& land_;
  std::vector<Key> picked_;
  std::map<Key, game::Mesh> cache_;
  std::map<Key, uint32_t> used_;
  uint32_t tick_ = 0;
  Vec3 lastEye_{1e9f, 0, 1e9f};
};

// The shape a place's land has (from its `the land:` line).
LandShape shapeOf(const story::Place& p);
// Stands everything in a place with land on the ground: things, people and
// where you come in get the land's height under them.
void settle(story::Story& s);

}  // namespace terrain
