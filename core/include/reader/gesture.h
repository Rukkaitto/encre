#pragma once
#include <cstdint>

#include "reader/input.h"

namespace reader {

// WHAT A PRESS MEANS, so no screen has to work it out.
//
// Before this, every screen interpreted PressKind itself, and the interpretation
// was the same interpretation nine times over:
//
//   * Six screens opened with `if (ev.kind != PressKind::Short) return none();`.
//     A guard each one had to remember, and forgetting it lets a HOLD do the
//     press's job -- on a list that is opening a book when the user asked for its
//     actions panel. Nothing failed if you left it out; it just did the wrong
//     thing.
//   * Library alone read `PressKind::Long` and `PressKind::Repeat`, so the two
//     kinds that carry real meaning were understood in exactly one place and
//     ignored in eight, which is not a design -- it is where the code happened to
//     stop.
//   * Nine screens overrode `longPressable()` with the identical
//     `hintHoldMask(vm_.holds)`, which is the SAME fact the hint bar already
//     states. Two spellings of one fact is how a ring ends up promising a hold
//     nothing bound.
//
// So a screen now answers a GESTURE and never sees a PressKind. `Screen::onEvent`
// does the translation once, from the declaration the screen's own hint bar
// already makes -- see Screen::declareHints.
enum class Gesture : uint8_t {
  None,
  Back,       // the Back button
  Activate,   // Confirm, PRESSED -- open, select, confirm
  Secondary,  // Confirm, HELD -- and only where the hint bar draws a ring
  Prev,       // Up
  Next,       // Down
};

struct GestureEvent {
  Gesture what = Gesture::None;

  // How far, for Prev and Next. 1 for a press. More for a held button, where the
  // step is derived from ELAPSED TIME rather than from a count of events -- see
  // InputEvent::steps for why the panel forces that.
  int steps = 1;

  // Whether this came from HOLDING rather than pressing.
  //
  // A screen needs this for MOVEMENT POLICY and for nothing else, which is the
  // whole reason it is here rather than left as a PressKind: a held Next must
  // CLAMP at the end of a list where a pressed Next WRAPS. Those two are right
  // for opposite reasons -- a wrap can never read as a dead button, and a held
  // button that wraps has no end and cycles for as long as it is down -- and
  // before this flag there was nowhere that knew the difference. Focus::move
  // takes it directly, so no screen decides.
  bool held = false;
};

// Translate one raw press into what it means on a screen that has declared
// `holds` (which buttons the hint bar draws a ring on) and `repeats` (which
// buttons scroll while held).
//
// PURE, and separated from Screen so it can be tested without one: the mapping is
// the whole of the behaviour and it is eight lines, so a test that needs a screen
// to reach it would be testing the wrong thing.
//
// The rules, and each is a decision:
//
//   * A HOLD on a button the hint bar shows no ring for is DROPPED, not demoted
//     to a press. The bar is a promise about what the buttons do; silently
//     treating an unadvertised hold as a press means a user who held too long
//     gets an action they did not ask for.
//   * A REPEAT on a button that does not repeat is likewise dropped. It cannot
//     arise today -- PressRecognizer only emits Repeat for its autoRepeat mask --
//     but the mapping should not depend on that being true elsewhere.
//   * Up and Down are Prev and Next whatever the kind: they are the only buttons
//     whose held form means MORE OF THE SAME rather than something else.
GestureEvent gestureFor(const InputEvent& ev, ButtonMask holds, ButtonMask repeats);

}  // namespace reader
