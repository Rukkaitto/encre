#pragma once
#include <cstdint>

namespace reader {

// WHERE A RENDER'S TIME GOES, broken down by PRIMITIVE rather than by screen.
//
// The device can already say what a paint cost (`[paint] done render=`) and what
// the panel cost, and that was enough to find the expensive screens: the reader
// menu renders in 266 ms against Contents' 62 ms, and 172 ms of it even as an
// overlay-only partial repaint. It is not enough to say WHY, and this project's
// standing rule is that where the answer decides a change you measure the thing
// rather than argue about it -- three of its worst calls were arguments that
// sounded right (the root-directory probe answered from cache, the stb heap
// attribution, the ~37x ratio applied to a path that is not render-bound).
//
// PER PRIMITIVE, NOT PER SCREEN, because the primitives are shared: whatever is
// expensive here is expensive on every screen that draws one, and a per-screen
// breakdown would have to be read once per screen to notice that. Five slots,
// each a leaf that touches pixels, so nothing nests and nothing is double-counted:
// outlineRect is four fillRects and is not itself a slot; drawPanelRow is a fill
// plus a run and is not either. The time NOT in any slot is layout arithmetic,
// measuring and wrapping.
//
// core/ HAS NO CLOCK and must not acquire one -- it compiles for the desktop and
// the ESP32 alike -- so the owner installs one. With none installed a Span is a
// load and a branch, which is what the simulator, the tests and any build that has
// not asked for this pay.
enum class Phase : uint8_t {
  Fill,    // Framebuffer::fillRect -- panels, bands, focus bars, rules
  Veil,    // veilRect -- the whole-frame stipple under an overlay
  Dither,  // ditherRect -- tints, the cover placeholder
  Glyph,   // the coverage blit, one span per RUN of text
  Icon,    // drawIcon
  Count_
};
inline constexpr int kPhaseCount = static_cast<int>(Phase::Count_);

class Profile {
 public:
  // Microseconds since some fixed origin. Null disables everything.
  using Clock = uint32_t (*)();

  static void install(Clock c);
  static bool active() { return clock_ != nullptr; }
  static void reset();
  static uint32_t micros(Phase p) { return micros_[static_cast<int>(p)]; }
  static uint32_t calls(Phase p) { return calls_[static_cast<int>(p)]; }
  static const char* name(Phase p);

 private:
  friend class PhaseSpan;
  static Clock clock_;
  static uint32_t micros_[kPhaseCount];
  static uint32_t calls_[kPhaseCount];
};

// RAII, and deliberately not nestable within the same phase -- see the header
// note. Cheap enough to sit in a leaf that runs a few hundred times per frame:
// the clock is read twice per CALL, never per pixel.
//
// NOT `Span`: reader::Span is already a range of emphasised bytes (emphasis.h),
// and text.cpp draws styled runs with a `const std::vector<Span>&` in scope.
class PhaseSpan {
 public:
  explicit PhaseSpan(Phase p) : p_(p), t0_(Profile::clock_ ? Profile::clock_() : 0) {}
  ~PhaseSpan() {
    if (Profile::clock_ == nullptr) return;
    const int i = static_cast<int>(p_);
    Profile::micros_[i] += Profile::clock_() - t0_;
    ++Profile::calls_[i];
  }
  PhaseSpan(const PhaseSpan&) = delete;
  PhaseSpan& operator=(const PhaseSpan&) = delete;

 private:
  Phase p_;
  uint32_t t0_;
};

}  // namespace reader
