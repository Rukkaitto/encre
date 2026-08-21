#pragma once
#include <cstdint>

namespace reader {

// How a screen must be painted.
//
// The names are the *technique*, not a quality ranking. All three are reachable;
// chrome ships on the first.
//
// The reference firmware settles the chrome question. CrossInk (a CrossPoint
// derivative) builds its **UI fonts 1-bit** and only its *reader* fonts 2-bit,
// and its "Text Anti-Aliasing" setting is read exclusively by the EPUB/TXT
// reader activities -- never by a menu, home, library or settings screen. Its
// chrome is therefore hard-thresholded 1-bit with no anti-aliasing at all, and
// that is the chrome the user compared ours against on the same glass.
enum class Fidelity : uint8_t {
  // One render pass, one panel waveform, coverage hard-thresholded to ink or
  // paper (Plane::Bw). **The default, and what every chrome screen ships on**,
  // because it is what the reference firmware ships on this panel.
  Mono,
  // Also one pass and one waveform, but partial coverage is stippled through a
  // dispersed Bayer 4x4 rather than thresholded away (Plane::BwDithered), so a
  // glyph keeps a soft edge on a two-level frame. Implemented, tested and
  // measured on X3 hardware; it is simply not what chrome ships. Kept as a real
  // choice for large display type, where there is enough stroke for a stipple to
  // read as a soft edge instead of as grain.
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

// FULL every N refreshes, and optionally on a screen transition (spec 3.3).
//
// Why a cadence at all: a differential (FAST) e-ink update leaves a little of
// the previous frame behind each time, and the residue accumulates into visible
// ghosting. A periodic FULL clears it. N is a setting because the right value is
// a taste trade -- more FULLs is cleaner and slower.
//
// Why the transition FULL is optional: it is the one the *user* sees, because it
// flashes the panel black exactly when they pressed a button to go somewhere.
// The reference firmware deliberately does not do it on this hardware --
// CrossInk's ScreenTransitionRefresh::modeFor returns FULL only when
// `screenChanged && !gpio.deviceIsX3()`, so on the X3 a screen change is a FAST
// refresh like any other, and its list/menu screens call displayBuffer() with no
// argument at all (the default is FAST_REFRESH). Chrome therefore constructs its
// policy with this false; see the call in shell/src/main.cpp.
class RefreshPolicy {
 public:
  // `cadence` is the number of refreshes between FULLs; the default matches the
  // spec's 15. A cadence of 1 or less means every refresh is FULL.
  // `fullOnTransition` makes a screen change force a FULL. It defaults true
  // because that is the conservative reading of spec 3.3; the shell turns it off.
  // A cadence of this or below means NEVER schedule a periodic FULL. Distinct
  // from 1, which means every refresh is FULL -- the two ends of the range must
  // not collide, so 0 and negatives are the "never" end rather than folding into
  // "always" as they used to.
  static constexpr int kNever = 0;

  explicit RefreshPolicy(int cadence = 15, bool fullOnTransition = true)
      : cadence_(cadence), fullOnTransition_(fullOnTransition) {}

  void setCadence(int cadence) { cadence_ = cadence; }
  int cadence() const { return cadence_; }
  bool fullOnTransition() const { return fullOnTransition_; }

  // Ask for the next refresh's mode and account for it.
  //
  // With `fullOnTransition` set, a transition is FULL and resets the count: the
  // screen is changing completely, so there is nothing for a differential update
  // to be differential against. With it clear, a transition is an ordinary
  // refresh in every respect -- it returns FAST *and* it counts toward the
  // cadence, because a FAST screen change leaves exactly the same residue behind
  // as any other FAST refresh. Treating it as a clean slate would be claiming a
  // FULL happened when none did, and the ghosting the cadence exists to clear
  // would accumulate unbounded across a run of navigation.
  RefreshMode next(bool transition);

  // Refreshes since the last FULL. Exposed so the shell can log it.
  int sinceFull() const { return sinceFull_; }

 private:
  int cadence_;
  bool fullOnTransition_;
  int sinceFull_ = 0;
};

}  // namespace reader
