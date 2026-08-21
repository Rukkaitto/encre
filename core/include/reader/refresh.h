#pragma once
#include <cstdint>

namespace reader {

// How a screen must be painted.
//
// The names are the *technique*, not a quality ranking, because the obvious
// ranking is wrong: `Dithered` is a 1-bit frame and still anti-aliased. What
// Phase 2A-2 measured as illegible was 1-bit *thresholding* -- coverage below
// half thrown away, so every stem lost its soft edge. Stippling that same edge
// coverage through a dispersed Bayer 4x4 keeps the anti-aliasing on one plane
// (see Plane::BwDithered in text.h), which is why chrome no longer needs the
// expensive path. Verified on X3 hardware.
enum class Fidelity : uint8_t {
  // One render pass, one panel waveform. Edge coverage is stippled rather than
  // thresholded, so curves still read as curves. This is the default and what
  // every chrome screen wants.
  Dithered,
  // Three render passes plus a fourth for the cleanup rebase, and three panel
  // waveforms (366 + 366 + 156 ms): 1363 ms for a focus move, measured on X3.
  // True 4-level grey, so it is the only path that can render a continuous-tone
  // image. Nothing ships on it today; retained for Phase 3 to decide about book
  // images, and it is hardware-validated -- see paintGray() in shell/src/main.cpp
  // for the sequence and why each step is where it is.
  Grayscale,
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
