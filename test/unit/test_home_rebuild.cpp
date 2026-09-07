// HOME'S LIBRARY COUNT AFTER A BOOK IS DELETED (#43).
//
// Home is the App's root, so it is built once and handed back by every pop that returns
// to it. The shell rebuilds it when what it says has changed -- and for as long as that
// question was a bare `bool`, it was a caller list: a delete set nothing, so Home kept
// the count it was born with until a reading position was saved, a card was re-inserted,
// or the device rebooted. Reported off an X3 with a 208-book card.
//
// HomeRebuildGate makes the book-left half DERIVED instead, from the removal counter the
// filesystem already keeps above every one of its own refusals, so no caller can be the
// one that forgot. The two traps it introduces are both invisible on a desktop and both
// expensive on the panel, which is why the answer lives in `core/` rather than in
// `shell/src/main.cpp` where five bugs have hidden:
//
//   * a rebuild must RE-STAMP the counter, or one delete asks for a rebuild on every
//     loop iteration for the rest of the session -- each one replacing the App under
//     the user;
//   * a rebuild must not swallow a removal that happened after the decision was made,
//     or the delete that raced a paint is the one that goes unreported.
#include "doctest.h"
#include "reader/home_rebuild.h"

using reader::HomeRebuildGate;

TEST_CASE("a freshly built Home is not stale") {
  HomeRebuildGate g;
  g.noteBuilt(0);
  CHECK_FALSE(g.stale(0));
  // ...and stays that way however many times it is asked. The question is put on every
  // loop iteration.
  CHECK_FALSE(g.stale(0));
  CHECK_FALSE(g.latched());
}

TEST_CASE("a gate that has never seen a build agrees with a fresh card") {
  // Nothing has been removed and Home has not been built, which is the state between
  // boot and the first buildHomeApp(). It must not report stale: the consumer is gated
  // on Home being on top, and a Home that does not exist cannot be rebuilt.
  HomeRebuildGate g;
  CHECK_FALSE(g.stale(0));
}

TEST_CASE("a removal makes Home stale with no caller saying so") {
  // THE DEFECT, at its smallest. The delete path sets nothing here; the counter moving
  // is the whole signal.
  HomeRebuildGate g;
  g.noteBuilt(4);
  REQUIRE_FALSE(g.stale(4));
  CHECK(g.stale(5));
}

TEST_CASE("rebuilding after a removal stops asking") {
  // The trap. `noteBuilt` clears the latch AND re-stamps the counter, so the rebuild a
  // removal asked for is the last one it asks for. Clearing only the latch would leave
  // this true for ever, and a Home rebuilt per loop iteration replaces the App under
  // the user hundreds of times a second.
  HomeRebuildGate g;
  g.noteBuilt(4);
  REQUIRE(g.stale(5));
  g.noteBuilt(5);
  CHECK_FALSE(g.stale(5));
  CHECK_FALSE(g.stale(5));
}

TEST_CASE("a second removal is not swallowed by the first rebuild") {
  HomeRebuildGate g;
  g.noteBuilt(0);
  REQUIRE(g.stale(1));
  g.noteBuilt(1);
  REQUIRE_FALSE(g.stale(1));
  // Two books deleted in one session, and the second must cost its own rebuild.
  CHECK(g.stale(2));
}

TEST_CASE("a reading position that moved is latched, because no counter carries it") {
  // The sidecar is REWRITTEN rather than removed, so the removal counter does not move
  // when a reader closes a book -- and Home's CONTINUE block is exactly what changed.
  HomeRebuildGate g;
  g.noteBuilt(7);
  REQUIRE_FALSE(g.stale(7));
  g.markStale();
  CHECK(g.stale(7));
  CHECK(g.latched());
  g.noteBuilt(7);
  CHECK_FALSE(g.stale(7));
  CHECK_FALSE(g.latched());
}

TEST_CASE("the latch survives being asked, and outlives the screen it is waiting for") {
  // gHomeStale is set while the READER is on top and consumed only when Home is, which
  // is several presses later -- so being asked must not answer the question away.
  HomeRebuildGate g;
  g.noteBuilt(2);
  g.markStale();
  for (int i = 0; i < 100; ++i) CHECK(g.stale(2));
  g.noteBuilt(2);
  CHECK_FALSE(g.stale(2));
}

TEST_CASE("a removal while the latch is up costs one rebuild, not two") {
  // Marking as finished does both at once -- it rewrites the sidecar AND drops the
  // card's `last.json` through the same `remove` the count is keyed on. One rebuild
  // answers both.
  HomeRebuildGate g;
  g.noteBuilt(1);
  g.markStale();
  REQUIRE(g.stale(2));
  g.noteBuilt(2);
  CHECK_FALSE(g.stale(2));
}

TEST_CASE("the counter is compared, never ordered, so a wrap is not a rebuild storm") {
  // Only inequality is ever asked, which is what makes this correct across a wrap of
  // the unsigned counter. A `>` would answer false for ever after one.
  HomeRebuildGate g;
  g.noteBuilt(0xFFFFFFFFu);
  CHECK_FALSE(g.stale(0xFFFFFFFFu));
  CHECK(g.stale(0u));
  g.noteBuilt(0u);
  CHECK_FALSE(g.stale(0u));
}
