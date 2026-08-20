#pragma once
#include <cstdint>

namespace reader {

// How a screen must be painted. This is not a style choice: Phase 2A-2 measured
// 1-bit thresholded chrome as illegible on this panel, so a chrome screen has to
// go through the three-plane grayscale sequence every single time and cannot
// take a cheap 1-bit refresh. Body text at 14pt and up IS legible in 1 bit,
// which is what makes the Reader's fast page turns possible.
enum class Fidelity : uint8_t {
  Gray,  // three planes, ~1.5 s. Every product chrome screen.
  Mono,  // one thresholded plane, ~0.4 s. The Reader (Phase 3) and diagnostics.
};

enum class RefreshMode : uint8_t { Fast, Full };

// FULL every N refreshes, and always on a screen transition (spec 3.3).
//
// Why a cadence at all: a differential (FAST) e-ink update leaves a little of
// the previous frame behind each time, and the residue accumulates into visible
// ghosting. A periodic FULL clears it. N is a setting because the right value is
// a taste trade -- more FULLs is cleaner and slower.
class RefreshPolicy {
 public:
  // `cadence` is the number of refreshes between FULLs; the default matches the
  // spec's 15. A cadence of 1 or less means every refresh is FULL.
  explicit RefreshPolicy(int cadence = 15) : cadence_(cadence) {}

  void setCadence(int cadence) { cadence_ = cadence; }
  int cadence() const { return cadence_; }

  // Ask for the next refresh's mode and account for it. A transition is always
  // FULL and resets the count: the screen is changing completely, so there is
  // nothing for a differential update to be differential against.
  RefreshMode next(bool transition);

  // Refreshes since the last FULL. Exposed so the shell can log it.
  int sinceFull() const { return sinceFull_; }

 private:
  int cadence_;
  int sinceFull_ = 0;
};

}  // namespace reader
