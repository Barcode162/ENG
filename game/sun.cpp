#include "game/sun.h"

#include <algorithm>
#include <cmath>

namespace game {

namespace {
constexpr float kPi = 3.14159265f;
float smooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
// Where the sun is at an hour of a day near the equinox, 50° north.
Vec3 sunAt(float hours) {
  const float a = (hours - 6) / 12 * kPi;  // 0 at sunrise, pi at sunset
  return engine::normalize(Vec3{std::cos(a), std::sin(a) * 0.72f, std::sin(a) * 0.55f + 0.08f});
}
}  // namespace

SkyNow skyAt(float hours, float ground) {
  SkyNow s;
  Vec3 sun = sunAt(hours);
  const float elev = std::asin(std::clamp(sun.y, -1.0f, 1.0f)) * 57.29578f;  // degrees
  // The sun through the air: the lower, the more air, the redder.
  const float e = std::max(elev, -1.0f);
  const float airMass = 1 / (std::sin(std::max(e, 0.0f) * kPi / 180) + 0.50572f * std::pow(e + 6.07995f, -1.6364f));
  const Vec3 beta{0.055f, 0.11f, 0.26f};  // how much each colour scatters away, per air mass
  const Vec3 through{std::exp(-beta.x * airMass), std::exp(-beta.y * airMass), std::exp(-beta.z * airMass)};
  const float up = smooth(-1.0f, 1.5f, elev);
  s.sunLux = through * (115000 * up);
  // The sky's light: day, the twilight after sunset, night.
  const float day = 17000 * std::pow(std::max(sun.y, 0.0f), 0.85f);
  const float twilight = 420 * std::exp(std::min(elev, 0.0f) / 2.0f) * smooth(-14, -2, elev);
  const float night = 0.004f;
  const float skyLuxTotal = day + twilight + night;
  const Vec3 dayBlue{0.62f, 0.78f, 1.0f}, dusk{0.68f, 0.62f, 0.95f}, nightBlue{0.35f, 0.45f, 1.0f};
  const float dayness = smooth(-4, 12, elev), nightness = smooth(-4, -14, elev);
  Vec3 tint = dayBlue * dayness + dusk * (1 - dayness);
  tint = tint * (1 - nightness) + nightBlue * nightness;
  s.skyLux = tint * skyLuxTotal;
  // The sky's look: the zenith deeper blue, the horizon paler, warm near a low sun.
  const float L = skyLuxTotal / kPi;
  s.zenith = Vec3{0.3f, 0.5f, 1.15f} * (L * (0.55f + 0.45f * nightness));
  const Vec3 warm = through * 1.6f;
  const float low = smooth(25, 0, elev) * smooth(-8, 0, elev);
  s.horizon = (Vec3{0.95f, 1.0f, 1.05f} * (1 - low) + warm * low) * (L * 1.15f);
  s.glow = through * (115000 * up / kPi * 0.004f);
  s.sunDir = sun;
  // Night: the moon, opposite the sun, a cool faint light; and the stars.
  if (elev < -2) {
    const Vec3 moon = engine::normalize(Vec3{-sun.x, std::max(-sun.y, 0.25f), -sun.z * 0.5f});
    const float m = smooth(-2, -10, elev);
    s.sunDir = moon;
    s.sunLux = Vec3{0.18f, 0.22f, 0.3f} * m;
    s.moon = true;
    s.glow = Vec3{0.0012f, 0.0014f, 0.002f} * m;
  }
  s.stars = 0.004f * smooth(-6, -16, elev);
  s.bounce = (s.skyLux + s.sunLux * std::max(s.sunDir.y, 0.0f)) * ground;
  return s;
}

float exposureFor(float lux) {
  // A calm eye shows middle grey at about a fifth of white in daylight, but
  // the darker the world, the less it opens: dusk looks like dusk, a
  // moonlit night stays dark and blue, and lamps stand out from it.
  const float e = std::max(lux, 1e-4f);
  const float key = 2.4f * (0.12f + 0.88f * smooth(-1.5f, 3.2f, std::log10(e)));
  return key / e;
}

}  // namespace game
