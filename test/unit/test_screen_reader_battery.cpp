// The low-battery banner's latch. It lives on ReaderScreen rather than in the
// shell because `shell/` has no test harness and five bugs have hidden there --
// and because `ANY BUTTON` is a BINDING: a bar cannot promise what nothing has
// bound, and the thing that binds it is onGesture.
#include <string>

#include "doctest.h"
#include "reader_fixture.h"
#include "reader/app.h"
#include "reader/gesture.h"
#include "reader/screen_reader.h"

using reader::Action;
using reader::Gesture;
using reader::GestureEvent;

namespace {
// A Reader over the long in-memory chapter, at the X4's own column. The fixture is
// the one every other paging test uses, so a failure here is about the banner --
// constructed in place rather than returned, because Reading holds a ScalableFont
// that borrows its own byte vector.
#define READING(name) readerfix::Reading name(readerfix::longChapter(20))

GestureEvent g(Gesture what) {
  GestureEvent e;
  e.what = what;
  return e;
}
}  // namespace

TEST_CASE("the banner is absent until it is armed, and -1 is how that is spelled") {
  READING(f);
  // ONE FIELD, ONE SENTINEL. A separate bool would let the condition be spelled
  // twice, and two spellings of one fact is what this project has a rule about.
  CHECK(f.scr->vm().batteryLowPercent == -1);
  f.scr->setBatteryLow(5);
  CHECK(f.scr->vm().batteryLowPercent == 5);
  f.scr->setBatteryLow(-1);
  CHECK(f.scr->vm().batteryLowPercent == -1);
}

TEST_CASE("EVERY gesture dismisses the banner and does nothing else") {
  // The board's right slot says ANY BUTTON, so every one of them has to clear it --
  // including the ones this screen would otherwise act on. Back POPS the Reader, so
  // it is the case that matters most: without this, the first press after a warning
  // would leave the book.
  for (Gesture what : {Gesture::Next, Gesture::Prev, Gesture::AltNext, Gesture::AltPrev,
                       Gesture::Activate, Gesture::Back}) {
    READING(f);
    const int pageBefore = f.scr->vm().page;
    f.scr->setBatteryLow(5);
    const Action a = f.scr->onGesture(g(what));
    CHECK(a.kind == Action::Kind::Redraw);
    CHECK(f.scr->vm().batteryLowPercent == -1);
    // AND NOTHING ELSE. The press is spent dismissing, which is exactly what the
    // bar promised.
    CHECK(f.scr->vm().page == pageBefore);
  }
}

TEST_CASE("with no banner up, a page turn is a page turn") {
  // The mutation guard for the case above: if the dismissal branch were
  // unconditional, this screen would never turn a page again.
  READING(f);
  const int before = f.scr->vm().page;
  const Action a = f.scr->onGesture(g(Gesture::Next));
  CHECK(a.kind == Action::Kind::Redraw);
  CHECK(f.scr->vm().page == before + 1);
}

TEST_CASE("re-arming after a dismissal shows the banner again") {
  // A wake is a chip reset, so a low battery shows the banner again on every wake;
  // within one session the shell re-arms on a fresh entry into Low. Neither is this
  // class's business -- what it must do is accept being armed twice.
  READING(f);
  f.scr->setBatteryLow(5);
  f.scr->onGesture(g(Gesture::Next));
  CHECK(f.scr->vm().batteryLowPercent == -1);
  f.scr->setBatteryLow(4);
  CHECK(f.scr->vm().batteryLowPercent == 4);
}

TEST_CASE("the banner does not survive a page the screen lays for another reason") {
  // syncVm() rebuilds the view model on every movement of the reading position, so
  // the field has to be one syncVm does not clobber. Asserted because the obvious
  // implementation -- a plain vm_ field written only by the setter -- is right, and
  // the obvious BUG is a syncVm that resets it.
  READING(f);
  f.scr->setBatteryLow(7);
  f.scr->relayout(f.m);
  CHECK(f.scr->vm().batteryLowPercent == 7);
}
