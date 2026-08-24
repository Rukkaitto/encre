// The anchor's state machine, with no book anywhere near it.
//
// spec: docs/superpowers/specs/2026-08-24-peek-and-return-design.md, "Verification"
//
// It is tested here rather than through the Reader because it is a RULE, and the
// spec is pointed about the failure mode: "a rule that is off by one is right at
// page 1 and wrong everywhere after it". The Reader-level property -- paging back N
// and returning lands exactly where you left, for every page of a chapter -- is the
// separate test the spec asks for, and it needs a real chapter.
#include "doctest.h"
#include "reader/return_anchor.h"

namespace {

using reader::AnchorPos;
using reader::ReturnAnchor;

// A page, spelled the way the spec spells one.
AnchorPos at(int spine, int block, int line = 0) { return AnchorPos{spine, block, line}; }

}  // namespace

TEST_CASE("a fresh anchor promises nothing") {
  // The absence of the promise IS the absence of the affordance -- the footer draws
  // no field, and `Up` must do nothing. This project has shipped a dead button
  // twice, so it is asserted rather than assumed.
  ReturnAnchor a;
  CHECK_FALSE(a.isSet());
  AnchorPos out = at(9, 9, 9);
  CHECK_FALSE(a.follow(&out));
  // `follow` must not have written through on failure.
  CHECK(out == at(9, 9, 9));
}

TEST_CASE("reading forward NEVER raises an anchor") {
  // The part the spec calls easy to get wrong. An anchor that appeared while reading
  // forward would name the last thing the reader did; the whole point is that it
  // names the furthest point of an excursion, which only a turn BACK can create.
  ReturnAnchor a;
  for (int block = 0; block < 40; ++block) a.pagedForward(at(3, block), at(3, block + 1));
  CHECK_FALSE(a.isSet());
  // Nor across a chapter boundary.
  a.pagedForward(at(3, 40), at(4, 0));
  CHECK_FALSE(a.isSet());
}

TEST_CASE("paging backward sets the anchor to the position being LEFT") {
  ReturnAnchor a;
  a.pagedBackward(at(3, 300), at(3, 299));
  REQUIRE(a.isSet());
  CHECK(a.get() == at(3, 300));
}

TEST_CASE("an anchor already standing HOLDS STILL while the reader drifts below it") {
  // THE HIGH-WATER PROPERTY. Lowering it on every backward turn would make it name
  // the last turn rather than the top of the excursion, which is the one thing it is
  // for -- and the bug would be invisible on a single backward turn.
  ReturnAnchor a;
  a.pagedBackward(at(3, 300), at(3, 299));
  for (int block = 299; block > 280; --block) a.pagedBackward(at(3, block), at(3, block - 1));
  REQUIRE(a.isSet());
  CHECK(a.get() == at(3, 300));  // still the top, not 281
}

TEST_CASE("reading back up to the anchor spends it") {
  ReturnAnchor a;
  a.pagedBackward(at(3, 300), at(3, 299));
  for (int block = 299; block < 300; ++block) a.pagedForward(at(3, block), at(3, block + 1));
  CHECK_FALSE(a.isSet());
}

TEST_CASE("reading PAST the anchor spends it too, not only landing exactly on it") {
  // A page is a range of blocks, so a forward turn can step over the anchor without
  // ever equalling it -- `at_ <= to` and not `at_ == to`. An equality test passes
  // every test whose pages happen to be one block wide.
  ReturnAnchor a;
  a.pagedBackward(at(3, 300, 5), at(3, 290));
  a.pagedForward(at(3, 290), at(3, 340, 0));  // straight over the top
  CHECK_FALSE(a.isSet());
}

TEST_CASE("a jump OVERWRITES an anchor that is behind the reader") {
  // The case the two-rule shape exists for. Commit a peek from chapter 2 into
  // chapter 8: a pure high-water rule finds the anchor behind and clears it as
  // satisfied, throwing away the one breadcrumb the reader wanted.
  ReturnAnchor a;
  a.pagedBackward(at(2, 100), at(2, 99));
  REQUIRE(a.get() == at(2, 100));
  a.jumped(at(2, 99), at(8, 0));
  REQUIRE(a.isSet());
  CHECK(a.get() == at(2, 99));  // the departure point, NOT the destination
}

TEST_CASE("a jump sets an anchor even when none was standing, in both directions") {
  ReturnAnchor forward;
  forward.jumped(at(1, 50), at(7, 0));
  REQUIRE(forward.isSet());
  CHECK(forward.get() == at(1, 50));

  ReturnAnchor backward;
  backward.jumped(at(7, 0), at(1, 50));
  REQUIRE(backward.isSet());
  CHECK(backward.get() == at(7, 0));
}

TEST_CASE("the anchor names the DEPARTURE, which is what makes a backward jump useful") {
  // Jump backward from chapter 7 to chapter 1 -- the peek-commit story the board
  // tells. The anchor must hold chapter 7, or the reader has no way back to what
  // they were actually reading.
  ReturnAnchor a;
  a.jumped(at(7, 300), at(1, 53));
  CHECK(a.get() == at(7, 300));
  // And reading forward through chapter 1 must NOT satisfy it: the anchor is far
  // ahead in reading order, so nothing here reaches it.
  for (int block = 53; block < 90; ++block) a.pagedForward(at(1, block), at(1, block + 1));
  REQUIRE(a.isSet());
  CHECK(a.get() == at(7, 300));
}

TEST_CASE("positions compare in READING order, across chapters") {
  // Spine order is reading order, which is what makes the high-water rule work at
  // all -- and a comparison that only looked at `block` would call chapter 1's
  // block 300 later than chapter 7's block 0.
  CHECK(at(1, 300) < at(7, 0));
  CHECK(at(3, 10, 0) < at(3, 10, 1));
  CHECK(at(3, 9, 99) < at(3, 10, 0));
  CHECK_FALSE(at(3, 10, 1) < at(3, 10, 1));
  CHECK(at(3, 10, 1) <= at(3, 10, 1));
}

TEST_CASE("following the anchor hands over the target and spends it") {
  ReturnAnchor a;
  a.pagedBackward(at(3, 300), at(3, 250));
  AnchorPos target{};
  REQUIRE(a.follow(&target));
  CHECK(target == at(3, 300));
  // SPENT, by the forward rule rather than a new one: `Up` lands exactly on the
  // anchor, and arriving at it is already the condition that clears it. So a second
  // press promises nothing and does nothing.
  CHECK_FALSE(a.isSet());
  AnchorPos again = at(0, 0);
  CHECK_FALSE(a.follow(&again));
}

TEST_CASE("a full excursion: read on, turn back, wander, return") {
  // The shape a reader actually produces, in one case, because the individual rules
  // being right does not prove the sequence is.
  ReturnAnchor a;
  for (int b = 0; b < 300; ++b) a.pagedForward(at(4, b), at(4, b + 1));
  CHECK_FALSE(a.isSet());                       // forward alone raises nothing
  a.pagedBackward(at(4, 300), at(4, 299));      // turn back
  CHECK(a.get() == at(4, 300));
  a.pagedBackward(at(4, 299), at(4, 298));      // wander
  a.pagedForward(at(4, 298), at(4, 299));       // drift about below it
  CHECK(a.get() == at(4, 300));                 // held still throughout
  AnchorPos target{};
  REQUIRE(a.follow(&target));
  CHECK(target == at(4, 300));
  CHECK_FALSE(a.isSet());
  // ...and reading on from there raises nothing again.
  for (int b = 300; b < 310; ++b) a.pagedForward(at(4, b), at(4, b + 1));
  CHECK_FALSE(a.isSet());
}
