#pragma once
// The sun and the sky at any time of day (J122.1), in real units: lux on
// the ground, nits in the sky. The sun rises in the east (+x), crosses the
// south (+z) and sets in the west; its light reddens through more air when
// it's low (Kasten's air mass and a simple scattering per colour); the sky
// gives about a sixth of the sun's light at noon, a few hundred lux at
// sunset, three at the end of dusk, and starlight at night; the moon takes
// over below the horizon. The eye's exposure follows the light it sees,
// like eyes do, over a second or two. Plain C++ (tested natively).
#include "looped/math.h"

namespace game {

using engine::Vec3;

struct SkyNow {
  Vec3 sunDir;   // towards the sun, or the moon at night
  Vec3 sunLux;   // its light on a surface facing it
  Vec3 skyLux;   // the sky's light on flat ground
  Vec3 bounce;   // light coming back up from the ground
  Vec3 zenith, horizon, glow;  // the sky's brightness (nits)
  float stars = 0;   // how bright the stars are (nits)
  bool moon = false;
};

// `hours` 0..24; `ground` how much light the ground reflects (0..1).
SkyNow skyAt(float hours, float ground = 0.15f);

// The exposure a calm eye would choose for `lux` around it.
float exposureFor(float lux);

}  // namespace game
