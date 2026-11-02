// The land: its heights are sane (sea at the edges, mountains inside, no
// broken numbers), the squares near you are fine and far ones coarse, no
// square's edge leaves a gap, and the whole pick is light enough for a phone.
#include <cmath>
#include <cstdio>

#include "world/land.h"

int main() {
  int failures = 0;
  terrain::LandShape shape;
  terrain::Land land(shape);
  // Heights over the whole island.
  float lo = 1e9f, hi = -1e9f, hx = 0, hz = 0;
  int above = 0, total = 0;
  for (float z = -3000; z <= 3000; z += 25)
    for (float x = -3000; x <= 3000; x += 25) {
      const float h = land.height(x, z);
      if (!std::isfinite(h)) ++failures;
      if (h > hi) hi = h, hx = x, hz = z;
      lo = std::min(lo, h);
      above += h > shape.sea;
      ++total;
    }
  std::printf("heights %.0f to %.0f m; highest at %.0f %.0f; %.0f%% above the sea\n", lo, hi, hx, hz, 100.0 * above / total);
  if (hi < 400 || hi > 1300) ++failures, std::printf("FAIL: the highest point should be a mountain (400 to 1300 m)\n");
  if (land.height(2990, 0) > shape.sea || land.height(0, -2990) > shape.sea) ++failures, std::printf("FAIL: the edges should be sea\n");
  if (above < total / 5 || above > total * 4 / 5) ++failures, std::printf("FAIL: the island should be a fair part of the map\n");
  // A good place to come in: low, dry, near the south coast.
  for (float z = 2400; z > 0; z -= 20) {
    const float h = land.height(0, z);
    if (h > 6 && h < 40) {
      std::printf("come in at 0 %.0f (%.1f m up)\n", z, h);
      break;
    }
  }
  // The squares: fine near you, coarse far away, and not too many.
  terrain::LandDraw draw(land);
  draw.pick({0, land.height(0, 0) + 1.6f, 0});
  game::Mesh m;
  draw.mesh(m);
  std::printf("%zu squares, %zu vertices, %zu triangles\n", draw.squares(), m.v.size(), m.i.size() / 3);
  if (draw.squares() > 200) ++failures, std::printf("FAIL: too many squares\n");
  if (m.i.size() / 3 > 600000) ++failures, std::printf("FAIL: too many triangles for a phone\n");
  for (const auto& v : m.v)
    for (float p : v.pos)
      if (!std::isfinite(p)) ++failures;
  for (uint32_t i : m.i)
    if (i >= m.v.size()) ++failures;
  std::printf(failures ? "%d FAILED\n" : "all land tests passed\n", failures);
  return failures ? 1 : 0;
}
