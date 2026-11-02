#pragma once
// A place's shapes as triangles (the story's kinds and people, the ground,
// the sky): one vertex list, drawn in one go by the game. Faces are flat,
// for the story's carved, low-poly look. Plain C++ (tested natively).
#include <cstdint>
#include <vector>

#include "looped/math.h"
#include "story/run.h"
#include "story/story.h"

namespace game {

using engine::Vec3;

struct Vertex {
  float pos[3];
  float nrm[3];
  uint8_t col[4];  // colour (sRGB-ish bytes of a linear colour's square root); a: 0 lit, 1..250 glowing, 255 sky
};
static_assert(sizeof(Vertex) == 28, "28-byte vertices");

struct Mesh {
  std::vector<Vertex> v;
  std::vector<uint32_t> i;
};

// Where a part sits: a thing's position, turn (degrees) and scale, then the part's own.
struct Place3 {
  Vec3 at;
  float turn = 0, scale = 1;
  Vec3 apply(Vec3 p) const;   // a point in the thing's space → the world
  Vec3 rotate(Vec3 n) const;  // a direction
};

void addPart(Mesh& m, const story::Part& p, const Place3& where, bool lit);
void addKind(Mesh& m, const story::Kind& k, const Place3& where, bool lit);
void addPerson(Mesh& m, const story::Person& p, const Place3& where);
void addGround(Mesh& m, const story::Place& pl);
void addSky(Mesh& m, const story::Sky& sky, uint32_t seed);

// The whole place as it looks now (hidden things left out, unlit ones dark;
// people are drawn on their own).
void buildPlace(Mesh& m, const story::Story& s, const story::Runner& r);

}  // namespace game
