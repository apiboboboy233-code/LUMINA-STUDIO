#pragma once
#include <stdint.h>

// Include after RGB is defined. studioPixel samples Gradient (mode 1) itself.
// Animated modes are > Gradient. Call update once per frame before render.
// Catalog indices match Mode; JSON exposes ParamInfo::defaultValue as "default".
namespace Fx {
enum Mode : uint8_t {
  Solid, Gradient, Meteor, Breathe, Rainbow, RainbowCycle, Chase, Comet,
  Scanner, ColorWipe, TheaterChase, Twinkle, Sparkle, Fire, RunningLights, Ocean,
  effectCount
};
constexpr bool isAnimated(unsigned mode) { return mode > Gradient && mode < effectCount; }
constexpr unsigned maxParams = 3;
struct Params { uint16_t values[maxParams]{}; };
struct ParamInfo { const char *id, *name; uint16_t min, max, defaultValue; };
struct EffectInfo {
  const char *id, *name, *category, *description;
  uint8_t paramCount;
  ParamInfo params[maxParams];
};
constexpr ParamInfo period(uint16_t value) { return {"period", "Period (ms)", 100, 10000, value}; }
constexpr ParamInfo step(uint16_t value) { return {"step", "Step (ms)", 20, 2000, value}; }
constexpr EffectInfo metadata[] = {
  {"solid", "Solid", "Basic", "Fill the strip with the primary color.", 0, {}},
  {"gradient", "Gradient", "Basic", "Render the configured studio gradient.", 0, {}},
  {"meteor", "Meteor", "Motion", "Sweep a bright head with a broken, decaying wake.", 3,
   {period(1700), {"tail", "Tail (pixels)", 1, 128, 18}, {"decay", "Decay (%)", 1, 100, 55}}},
  {"breathe", "Breathing", "Basic", "Smoothly fade the primary color in and out.", 2,
   {period(3000), {"floor", "Minimum level (%)", 0, 90, 5}}},
  {"rainbow", "Rainbow", "Color", "Cycle the entire strip through the color wheel.", 1, {period(4000)}},
  {"rainbow-cycle", "Rainbow Cycle", "Color", "Scroll a rainbow across the strip.", 2,
   {period(4000), {"cycles", "Spatial cycles", 1, 8, 1}}},
  {"chase", "Chase", "Motion", "Move repeating blocks of primary color on a secondary background.", 3,
   {step(100), {"spacing", "Spacing (pixels)", 2, 64, 8}, {"width", "Width (pixels)", 1, 32, 2}}},
  {"comet", "Comet", "Motion", "Sweep a bright head and fading tail around the strip.", 2,
   {period(2000), {"tail", "Tail (pixels)", 1, 128, 12}}},
  {"scanner", "Scanner / Sinelon", "Motion", "Sweep a soft beam back and forth.", 2,
   {period(2000), {"width", "Beam radius (pixels)", 1, 64, 4}}},
  {"color-wipe", "Color Wipe", "Motion", "Wipe alternating colors along the strip.", 1, {period(2400)}},
  {"theater-chase", "Theater Chase", "Motion", "March evenly spaced lights over a dark background.", 2,
   {step(120), {"spacing", "Spacing (pixels)", 2, 16, 3}}},
  {"twinkle", "Twinkle", "Texture", "Fade scattered primary stars over the secondary color.", 2,
   {period(1200), {"density", "Density (%)", 1, 100, 22}}},
  {"sparkle", "Sparkle", "Texture", "Flash scattered secondary highlights on the primary color.", 2,
   {step(90), {"density", "Density (%)", 1, 100, 8}}},
  {"fire", "Fire", "Texture", "Flicker from a hot base to darker tips.", 2,
   {step(70), {"heat", "Heat (%)", 1, 100, 70}}},
  {"running-lights", "Running Lights", "Color", "Roll smooth bands between the primary and secondary colors.", 2,
   {period(2200), {"cycles", "Spatial cycles", 1, 8, 2}}},
  {"ocean", "Ocean Wave", "Color", "Blend overlapping teal waves with pale crests.", 2,
   {period(4500), {"foam", "Foam (%)", 0, 100, 28}}}
};
static_assert(sizeof(metadata) / sizeof(metadata[0]) == effectCount, "Every effect needs metadata");

inline Params defaults(unsigned mode) {
  Params p{};
  if (mode < effectCount)
    for (unsigned j = 0; j < metadata[mode].paramCount; ++j)
      p.values[j] = metadata[mode].params[j].defaultValue;
  return p;
}
inline Params clampParams(unsigned mode, Params &p) {
  const unsigned count = mode < effectCount ? metadata[mode].paramCount : 0;
  for (unsigned j = 0; j < maxParams; ++j) {
    if (j >= count) p.values[j] = 0;
    else {
      const auto &range = metadata[mode].params[j];
      auto &v = p.values[j];
      v = v < range.min ? range.min : v > range.max ? range.max : v;
    }
  }
  return p;
}

namespace detail {
inline uint32_t hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  return x ^ (x >> 16);
}
inline uint8_t lerp(uint8_t a, uint8_t b, unsigned t) {
  return uint8_t((unsigned(a) * (255 - t) + unsigned(b) * t + 127) / 255);
}
inline RGB mix(RGB a, RGB b, unsigned t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}
inline unsigned smooth(unsigned x) { return (x * x * (765 - 2 * x) + 32512) / 65025; }
inline unsigned wave(unsigned phase) {
  phase &= 255;
  return smooth(phase < 128 ? phase * 2 : (255 - phase) * 2);
}
inline RGB wheel(unsigned phase) {
  const unsigned x = (phase & 255) * 6;
  const uint8_t rise = x & 255, fall = 255 - rise;
  switch (x >> 8) {
    case 0: return {255, rise, 0};
    case 1: return {fall, 255, 0};
    case 2: return {0, 255, rise};
    case 3: return {0, fall, 255};
    case 4: return {rise, 0, 255};
    default: return {255, 0, fall};
  }
}
}  // namespace detail

class Engine {
 public:
  void reset() { *this = Engine{}; }

  // Call once per frame. Unsigned deltas preserve timing across millis() wrap.
  void update(unsigned mode, const Params &params, uint32_t nowMs) {
    if (mode >= effectCount) mode = Solid;
    if (started_ && mode == mode_) elapsed_ += uint32_t(nowMs - last_);
    else elapsed_ = 0;
    last_ = nowMs;
    mode_ = mode;
    started_ = true;
    params_ = params;
    clampParams(mode_, params_);
    const unsigned duration = params_.values[0];
    tick_ = duration ? elapsed_ / duration : 0;
    offset_ = duration ? elapsed_ % duration : 0;
  }

  RGB render(unsigned i, unsigned count, RGB primary, RGB secondary) const {
    using namespace detail;
    if (i >= count) return {};
    const unsigned duration = params_.values[0];
    const unsigned a = params_.values[1], b = params_.values[2];
    const unsigned phase = duration ? offset_ * 256 / duration : 0;
    switch (mode_) {
      case Meteor: {
        const unsigned head = unsigned(uint64_t(offset_) * count / duration);
        const unsigned distance = i <= head ? head - i : count - (i - head);
        if (!distance) return primary;
        if (distance >= a) return secondary;
        const unsigned tail = (a - distance) * 255 / a;
        const unsigned persistence = 255 - b * distance * 255 / (100 * a);
        const unsigned grain = 96 + hash(i ^ uint32_t(tick_)) % 160;
        return mix(secondary, primary, tail * persistence * grain / 65025);
      }
      case Breathe: {
        const unsigned floor = a * 255 / 100;
        return mix({}, primary, floor + wave(phase) * (255 - floor) / 255);
      }
      case Rainbow:
        return wheel(phase);
      case RainbowCycle:
        return wheel(phase + position(i, count, 256 * a));
      case Chase: {
        const unsigned width = b < a ? b : a - 1;
        return (i % a + a - unsigned(tick_ % a)) % a < width ? primary : secondary;
      }
      case Comet: {
        const unsigned head = unsigned(uint64_t(offset_) * count / duration);
        const unsigned distance = i <= head ? head - i : count - (i - head);
        return mix(secondary, primary, distance < a ? (a - distance) * 255 / a : 0);
      }
      case Scanner: {
        const unsigned half = offset_ * 2;
        const unsigned sweep = half <= duration ? half : 2 * duration - half;
        const unsigned head = unsigned(uint64_t(sweep) * (count - 1) / duration);
        const unsigned distance = i > head ? i - head : head - i;
        return mix(secondary, primary, distance < a ? (a - distance) * 255 / a : 0);
      }
      case ColorWipe: {
        const bool passed = uint64_t(i) * duration <= uint64_t(offset_) * count;
        return passed != bool(tick_ & 1) ? primary : secondary;
      }
      case TheaterChase:
        return i % a == tick_ % a ? ((tick_ / a) & 1 ? secondary : primary) : RGB{};
      case Twinkle: {
        const uint32_t seed = hash(i);
        const unsigned shifted = offset_ + seed % duration;
        const uint32_t event = uint32_t(tick_) + shifted / duration;
        const unsigned glow = wave(shifted % duration * 256 / duration);
        return mix(secondary, primary, hash(seed ^ event) % 100 < a ? glow : 0);
      }
      case Sparkle:
        return offset_ * 4 < duration * 3 && hash(hash(i) ^ uint32_t(tick_)) % 100 < a
          ? secondary : primary;
      case Fire: {
        const uint32_t seed = hash(i), event = uint32_t(tick_);
        const unsigned flicker = lerp(hash(seed ^ event) & 255, hash(seed ^ (event + 1)) & 255, phase);
        const unsigned base = count == 1 ? 255 : 255 - position(i, count - 1, 255);
        const unsigned heat = (base * 2 + flicker) * a / 210;
        return heat < 128 ? mix({}, RGB{230, 25, 0}, heat * 2)
                          : mix(RGB{230, 25, 0}, RGB{255, 230, 90}, (heat - 128) * 2);
      }
      case RunningLights:
        return mix(primary, secondary, wave(position(i, count, 256 * a) + phase));
      case Ocean: {
        const unsigned swell = wave(position(i, count, 512) + phase);
        const unsigned ripple = wave(position(i, count, 768) + 256 - phase);
        const unsigned foam = swell > 180 ? (swell - 180) * a / 75 : 0;
        return mix(mix(RGB{3, 18, 62}, RGB{0, 180, 145}, (swell * 2 + ripple) / 3),
                   RGB{200, 250, 255}, foam * 255 / 100);
      }
      default: return primary;
    }
  }

 private:
  static unsigned position(unsigned i, unsigned count, unsigned scale) {
    return unsigned(uint64_t(i) * scale / count);
  }
  Params params_{};
  uint64_t elapsed_ = 0, tick_ = 0;
  uint32_t last_ = 0;
  unsigned offset_ = 0, mode_ = Solid;
  bool started_ = false;
};
}  // namespace Fx
