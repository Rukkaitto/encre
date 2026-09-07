// THE PROOF THAT A PARTIAL REPAINT IS THE SAME FRAME AS A FULL ONE.
//
// App::renderTopOnly repaints the top overlay over whatever the previous paint
// left in the framebuffer, instead of re-rendering the parent and re-veiling
// every pixel. It is allowed only when the screen's own paintFootprint() has not
// moved -- the screen's promise that this paint covers the pixels the last one
// inked. test_app.cpp pins the CONDITIONS; this file pins the PROMISE, on the
// real overlays with the real theme and the real type ramp, byte for byte.
//
// The comparison is over data() rather than over pixels, because a stale pixel is
// a stale bit in a byte the panel driver is handed. And it runs under
// Rotation::Ccw as well as Rotation::None, because CCW is how the device paints.
//
// WHAT A FAILURE HERE MEANS: a screen is promising a footprint it does not keep,
// and the symptom on glass is ink from the previous frame surviving where the new
// panel does not reach -- which reads as a rendering bug rather than as a repaint
// one, and is the whole reason this enumerates every pair of states instead of
// sampling a few.
#include <string>
#include <vector>

#include "doctest.h"
#include "library_app.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_details.h"
#include "reader/screen_book_error.h"
#include "reader/theme_quiet.h"

using reader::App;
using reader::Button;
using reader::Framebuffer;
using reader::Plane;
using reader::PressKind;
using reader::Rotation;
using reader::ScreenId;

namespace {

struct Geometry {
  int w, h;
  const char* what;
};

const Geometry kGeometries[] = {{528, 792, "X3 528x792"}, {480, 800, "X4 480x800"}};

void moveFocusTo(App& app, int target) {
  // By pressing, not by assignment: a focus the user cannot reach is not a state
  // this needs to hold for. Bounded so a screen that clamps cannot spin here.
  for (int guard = 0; guard < 16 && app.top().focus() != target; ++guard)
    app.dispatch({app.top().focus() < target ? Button::Down : Button::Up, PressKind::Short});
  REQUIRE(app.top().focus() == target);
}

int firstDifferingByte(const Framebuffer& a, const Framebuffer& b) {
  REQUIRE(a.sizeBytes() == b.sizeBytes());
  for (int i = 0; i < a.sizeBytes(); ++i)
    if (a.data()[i] != b.data()[i]) return i;
  return -1;
}

int differingPixels(const Framebuffer& a, const Framebuffer& b) {
  int n = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.getPixel(x, y) != b.getPixel(x, y)) ++n;
  return n;
}

// What one focus move looks like through both paths.
struct Outcome {
  bool allowed = false;      // what App decided
  bool identical = false;    // whether a top-only repaint WOULD have been correct
  int differing = 0;         // ...and by how many pixels, when it would not
  uint32_t footprintA = 0, footprintB = 0;
};

// Paints the stack in full at focus `a`, moves the focus to `b`, then repaints
// the top screen alone over that same frame -- and compares the result against a
// full paint at `b`.
//
// The top screen is rendered DIRECTLY rather than through renderTopOnly, on
// purpose: this measures whether a partial repaint would be correct, separately
// from whether App permits it. `allowed` records the permission, so the two can
// be checked against each other.
Outcome focusMove(App& app, const reader::FontSet& fonts, reader::Theme& theme,
                  const Geometry& g, Rotation rot, Plane plane, int a, int b) {
  Outcome o;
  moveFocusTo(app, a);
  app.clearDirty();

  Framebuffer frame(g.w, g.h, rot);
  frame.clear(true);
  app.render(frame, fonts, theme, plane);
  o.footprintA = app.top().paintFootprint();
  app.clearDirty();

  moveFocusTo(app, b);
  o.footprintB = app.top().paintFootprint();
  // Asked BEFORE the reference render below, because a full paint into another
  // framebuffer moves App's record onto it.
  o.allowed = app.canRenderTopOnly(frame, plane);
  app.top().render(frame, fonts, theme, plane);

  Framebuffer reference(g.w, g.h, rot);
  reference.clear(true);
  app.render(reference, fonts, theme, plane);

  o.identical = firstDifferingByte(frame, reference) < 0;
  if (!o.identical) o.differing = differingPixels(frame, reference);
  return o;
}

// The item actions overlay over the board's Library, reached by holding Confirm
// on a row -- the way the user reaches it, and the way the goldens do.
struct ActionsStack {
  libapp::LibraryApp la;
  ActionsStack(const reader::Theme& theme, const reader::FontSet& fonts, int panelH)
      : la(theme, fonts, panelH, 5) {
    la.app.dispatch({Button::Confirm, PressKind::Long});
    REQUIRE(la.app.top().id() == ScreenId::ItemActions);
    REQUIRE(la.app.top().isOverlay());
  }
};

}  // namespace

TEST_CASE("a focus move inside the actions overlay repaints identically, where it is allowed") {
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  const int kRows = 4;  // the board's Open / Book details / Mark as finished / Delete
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      int allowedCount = 0, refusedCount = 0, refusedButFine = 0;
      for (int a = 0; a < kRows; ++a) {
        for (int b = 0; b < kRows; ++b) {
          if (a == b) continue;  // a clamped press produces Action::none, so no repaint
          ActionsStack s(theme, ramp.fonts, g.h);
          const Outcome o = focusMove(s.la.app, ramp.fonts, theme, g, rot, Plane::Bw, a, b);
          const std::string where = std::string(g.what) + " rot=" +
                                    (rot == Rotation::Ccw ? "Ccw" : "None") + " focus " +
                                    std::to_string(a) + "->" + std::to_string(b);
          // THE PROMISE: an unchanged footprint means the repaint covers what the
          // previous one drew. If this ever fails, the screen's footprint is
          // wrong, not this test.
          if (o.footprintA == o.footprintB)
            CHECK_MESSAGE(o.identical, where << ": footprint unchanged (" << o.footprintA
                                             << ") but " << o.differing << " pixels differ");
          // App's decision follows the footprint and nothing else here: the frame,
          // plane, depth and screen are all unchanged, and the screen is an
          // overlay on the Mono path.
          CHECK_MESSAGE(o.allowed == (o.footprintA == o.footprintB), where << ": App allowed="
                                                                          << o.allowed);
          if (o.allowed) {
            ++allowedCount;
            CHECK_MESSAGE(o.identical, where << ": ALLOWED but " << o.differing
                                             << " pixels differ");
          } else {
            ++refusedCount;
            if (o.identical) ++refusedButFine;
          }
        }
      }
      // The mechanism is doing work rather than saying yes to everything or no to
      // everything: with four rows, the footprint changes exactly when the focus
      // moves on or off the LAST row, because that is the row whose rule the
      // theme drops either way. Three of the twelve ordered pairs move onto it
      // and three move off it.
      CHECK_MESSAGE(allowedCount == 6, g.what << ": " << allowedCount << " partial repaints");
      CHECK_MESSAGE(refusedCount == 6, g.what << ": " << refusedCount << " refusals");
      // Of the six refusals, the three that GROW the panel would have been
      // harmless and the three that shrink it would not. So the footprint is
      // conservative in one direction, which is the safe direction -- and at
      // least one refusal is a real one, which is what stops this whole file
      // passing on a screen that never moves.
      CHECK_MESSAGE(refusedButFine == 3, g.what << ": " << refusedButFine
                                                << " refusals were harmless");
    }
  }
}

TEST_CASE("the actions overlay's panel really does move, so the refusal is not theatre") {
  // The measurement the footprint exists for, pinned directly: moving the focus
  // OFF the last row takes the panel from one row without a rule to two, so it
  // loses a pixel of height, and being centred it also drops a pixel -- leaving
  // its old top border standing over the veiled Library. A hairline the width of
  // the panel, which the veil only takes 5 of every 9 pixels out of.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  ActionsStack s(theme, ramp.fonts, 792);
  const Geometry g{528, 792, "X3 528x792"};
  const Outcome o =
      focusMove(s.la.app, ramp.fonts, theme, g, Rotation::None, Plane::Bw, 3, 2);
  CHECK(o.footprintA != o.footprintB);
  CHECK_FALSE(o.allowed);
  CHECK_FALSE(o.identical);
  // Not "a few pixels": a row of the panel's full inner width.
  CHECK(o.differing > 200);
}

TEST_CASE("every focus move in the delete confirmation repaints identically") {
  // Nothing this screen draws changes shape with the focus -- both action slabs
  // are always drawn, in the same boxes, one filled and one outlined -- so its
  // footprint is constant and every focus move takes the fast path.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      for (int a = 0; a < 2; ++a) {
        for (int b = 0; b < 2; ++b) {
          if (a == b) continue;
          ActionsStack s(theme, ramp.fonts, g.h);
          // Down to `Delete...`, then Confirm, which pushes the confirmation.
          moveFocusTo(s.la.app, 3);
          s.la.app.dispatch({Button::Confirm, PressKind::Short});
          REQUIRE(s.la.app.top().id() == ScreenId::DeleteConfirm);
          REQUIRE(s.la.app.top().isOverlay());
          // Four screens deep -- Home, Library, actions, confirm -- and only the
          // top one gets repainted.
          REQUIRE(s.la.app.depth() == 4);
          const Outcome o = focusMove(s.la.app, ramp.fonts, theme, g, rot, Plane::Bw, a, b);
          const std::string where = std::string(g.what) + " focus " + std::to_string(a) + "->" +
                                    std::to_string(b);
          CHECK_MESSAGE(o.footprintA == o.footprintB, where);
          CHECK_MESSAGE(o.allowed, where << ": refused");
          CHECK_MESSAGE(o.identical, where << ": " << o.differing << " pixels differ");
        }
      }
    }
  }
}

TEST_CASE("every focus move in the corrupt-book dialog repaints identically") {
  // BookErrorScreen::paintFootprint() is a constant, and its header says so at
  // length; the spec says the constant is "pinned by test_partial_repaint.cpp" and
  // for a while it was not -- this file covered ItemActions and DeleteConfirm only.
  // ItemActions is the recorded precedent for a footprint that LOOKED constant and
  // was not, by one pixel: its panel's height is the sum of its rows and the focused
  // row loses its rule, so focusing the last row makes the panel a pixel taller and,
  // being centred, a pixel higher. Nothing about this screen's shape is more obvious
  // than that was, so it is asserted rather than argued.
  //
  // BOTH COPY SHAPES, because they wrap to different heights and the header rests on
  // that not mattering: a push is never a partial repaint, so two INSTANCES are never
  // compared against one frame record, and the token only has to hold across focus
  // moves within one screen's life. Walking both is what says the paragraph's height
  // is fixed once the screen exists rather than merely equal between the two.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  for (const Geometry& g : kGeometries) {
    for (const Rotation rot : {Rotation::None, Rotation::Ccw}) {
      for (const bool unreadable : {false, true}) {
        for (int a = 0; a < 2; ++a) {
          for (int b = 0; b < 2; ++b) {
            if (a == b) continue;
            libapp::LibraryApp la(theme, ramp.fonts, g.h, 5);
            // NOT reached by pressing: no gesture on a Library row raises this
            // dialog. The shell raises it when an open refuses, so priming the
            // factory and pushing is the honest model of what the device does --
            // the simulator's own route.
            la.factory.setBookErrorFacts(unreadable ? reader::demoBookErrorUnreadableFacts()
                                                    : reader::demoBookErrorFacts());
            REQUIRE(la.app.pushScreen(ScreenId::BookError));
            REQUIRE(la.app.top().id() == ScreenId::BookError);
            REQUIRE(la.app.top().isOverlay());
            // Home, Library, dialog. Shallower than the confirmation's four,
            // because this one is pushed over the list rather than over a panel.
            REQUIRE(la.app.depth() == 3);
            const Outcome o = focusMove(la.app, ramp.fonts, theme, g, rot, Plane::Bw, a, b);
            const std::string where = std::string(g.what) + " rot=" +
                                      (rot == Rotation::Ccw ? "Ccw" : "None") +
                                      (unreadable ? " unreadable" : " damaged") + " focus " +
                                      std::to_string(a) + "->" + std::to_string(b);
            CHECK_MESSAGE(o.footprintA == o.footprintB, where);
            CHECK_MESSAGE(o.allowed, where << ": refused");
            CHECK_MESSAGE(o.identical, where << ": " << o.differing << " pixels differ");
          }
        }
      }
    }
  }
}

TEST_CASE("a partial repaint is refused on the screen the overlay pushes, which is not an overlay") {
  // Book details fills the frame, so there is nothing beneath it to preserve --
  // and a partial repaint would be actively wrong, because a full paint clears
  // the frame first and this one must not. Its own board's unused `.dim-veil`
  // rule is exactly the confusion this refusal has to survive.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  ActionsStack s(theme, ramp.fonts, 792);
  moveFocusTo(s.la.app, 1);  // `Book details`
  s.la.app.dispatch({Button::Confirm, PressKind::Short});
  REQUIRE(s.la.app.top().id() == ScreenId::BookDetails);
  REQUIRE_FALSE(s.la.app.top().isOverlay());
  Framebuffer frame(528, 792);
  frame.clear(true);
  s.la.app.render(frame, ramp.fonts, theme, Plane::Bw);
  s.la.app.clearDirty();
  // Nothing on this screen produces a Redraw, so ask directly for the state a
  // redraw would leave: dirty, no transition, and the same frame.
  CHECK(s.la.app.top().paintFootprint() == 0);
  CHECK_FALSE(s.la.app.canRenderTopOnly(frame, Plane::Bw));
  CHECK_FALSE(s.la.app.renderTopOnly(frame, ramp.fonts, theme, Plane::Bw));
}

TEST_CASE("renderTopOnly leaves the frame alone when it refuses") {
  // A refusal that had already drawn half the screen would have ruined the frame
  // it was checking, and the caller's fallback to render() would then be painting
  // over damage rather than over the previous frame.
  ramp::Ramp ramp;
  reader::QuietTheme theme;
  ActionsStack s(theme, ramp.fonts, 792);
  Framebuffer frame(528, 792);
  frame.clear(true);
  s.la.app.render(frame, ramp.fonts, theme, Plane::Bw);
  s.la.app.clearDirty();
  std::vector<uint8_t> before(frame.data(), frame.data() + frame.sizeBytes());
  // Not dirty, so refused whatever else holds.
  CHECK_FALSE(s.la.app.renderTopOnly(frame, ramp.fonts, theme, Plane::Bw));
  for (int i = 0; i < frame.sizeBytes(); ++i) REQUIRE(frame.data()[i] == before[i]);
  // ...and refused for a plane the frame was not painted in, which is the case
  // the grayscale sequence would hit on its second pass.
  CHECK_FALSE(s.la.app.renderTopOnly(frame, ramp.fonts, theme, Plane::Lsb));
  for (int i = 0; i < frame.sizeBytes(); ++i) REQUIRE(frame.data()[i] == before[i]);
}
