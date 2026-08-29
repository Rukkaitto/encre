// The high-water mark, with no book anywhere near it.
//
// spec: docs/superpowers/specs/2026-08-24-peek-and-return-design.md, "Verification"
//
// It is tested here rather than through the Reader because it is a RULE, and the spec
// is pointed about the failure mode: "a rule that is off by one is right at page 1 and
// wrong everywhere after it". The Reader-level property -- paging back N and returning
// lands exactly where you left, for every page of a real chapter -- lives in
// test_theme_reader_golden.cpp, which has a chapter to page through; the same shape is
// exercised here over synthetic pages, because the rule can be wrong on its own.
#include "doctest.h"
#include "reader/return_anchor.h"

namespace {

using reader::AnchorPos;
using reader::ReturnAnchor;

// A page, spelled the way the spec spells one.
AnchorPos at(int spine, int block, int line = 0) { return AnchorPos{spine, block, line}; }

}  // namespace

TEST_CASE("a fresh mark promises nothing, wherever the reader is standing") {
  // The absence of the promise IS the absence of the affordance -- the footer draws no
  // field, and `Up` must do nothing. This project has shipped a dead button twice, so
  // it is asserted rather than assumed.
  ReturnAnchor a;
  CHECK_FALSE(a.isSet());
  CHECK_FALSE(a.aheadOf(at(0, 0)));
  CHECK_FALSE(a.aheadOf(at(9, 999)));
}

TEST_CASE("THE USER'S SCENARIO: a forward jump to chapter 36 keeps the mark with the reader") {
  // THE HOLE THAT KILLED THE OLD RULE, and it was measured rather than argued. Under
  // the three-transition shape, `jumped` set the anchor to chapter 1 -- BEHIND the
  // reader -- and `pagedForward`'s "arriving at or past the anchor means you have read
  // back up to it" cleared it on the very first page turn:
  //
  //     after forward jump ch1->ch36: set=1 spine=0
  //     after ONE forward page turn:  set=0 spine=0
  //
  // So a forward jump bought a way back that survived exactly one press. Under the
  // high-water rule the mark travels WITH the reader, and the honest consequence --
  // there is no way back from a forward jump -- is stated rather than pretended at.
  ReturnAnchor a;
  a.note(at(1, 10));  // reading in chapter 1
  REQUIRE(a.get() == at(1, 10));

  a.note(at(36, 0));  // jump to chapter 36
  CHECK(a.get() == at(36, 0));
  CHECK_FALSE(a.aheadOf(at(36, 0)));  // nothing promised: this IS the furthest point

  // ...and it SURVIVES page turns, which is the whole failure above.
  for (int block = 0; block < 20; ++block) {
    a.note(at(36, block + 1));
    CHECK(a.get() == at(36, block + 1));
    CHECK_FALSE(a.aheadOf(at(36, block + 1)));
  }
  // Now turn back, and the way back is to chapter 36's furthest page -- not to the
  // chapter 1 the reader left an hour ago.
  CHECK(a.aheadOf(at(36, 5)));
  CHECK(a.get() == at(36, 20));
}

TEST_CASE("reading forward raises the mark and promises nothing") {
  ReturnAnchor a;
  for (int block = 0; block < 40; ++block) {
    a.note(at(3, block));
    CHECK(a.get() == at(3, block));
    CHECK_FALSE(a.aheadOf(at(3, block)));
  }
}

TEST_CASE("paging back is what makes the promise appear, and it moves the READER not the mark") {
  ReturnAnchor a;
  a.note(at(3, 300));
  a.note(at(3, 299));  // one turn back
  REQUIRE(a.isSet());
  CHECK(a.get() == at(3, 300));
  CHECK(a.aheadOf(at(3, 299)));
}

TEST_CASE("THE MARK IS NEVER LOWERED BY MOVING BACKWARD") {
  // Lowering it on a backward turn would make it name the last turn rather than the
  // furthest point, which is the one thing it is for -- and the bug would be invisible
  // on a single backward turn.
  ReturnAnchor a;
  a.note(at(3, 300));
  for (int block = 299; block > 280; --block) {
    a.note(at(3, block));
    CHECK(a.get() == at(3, 300));  // still the top, not `block`
    CHECK(a.aheadOf(at(3, block)));
  }
  // Nor by a backward JUMP, which under the old rule was a whole extra case.
  a.note(at(0, 0));
  CHECK(a.get() == at(3, 300));
  CHECK(a.aheadOf(at(0, 0)));
}

TEST_CASE("crossing a chapter forward raises the mark; crossing back does not lower it") {
  ReturnAnchor a;
  a.note(at(3, 40));
  a.note(at(4, 0));  // off the end of chapter 3 is chapter 4
  CHECK(a.get() == at(4, 0));
  CHECK_FALSE(a.aheadOf(at(4, 0)));
  // Back over the boundary. The mark stays in chapter 4 and is now ahead.
  a.note(at(3, 40));
  CHECK(a.get() == at(4, 0));
  CHECK(a.aheadOf(at(3, 40)));
}

TEST_CASE("following the mark leaves you AT it, with nothing promised") {
  // `Up` lands exactly on the mark, and arriving is a movement like any other -- so
  // note() raises it to a position it already holds and `aheadOf` goes false by
  // itself. There is nothing to spend and no `follow()` to spend it; the old shape
  // needed both, plus a paragraph saying which of its three rules did the clearing.
  ReturnAnchor a;
  a.note(at(3, 300));
  a.note(at(3, 250));
  REQUIRE(a.aheadOf(at(3, 250)));
  const AnchorPos target = a.get();
  CHECK(target == at(3, 300));

  a.note(target);  // the landing
  CHECK(a.get() == target);
  CHECK_FALSE(a.aheadOf(target));
  // ...and it comes back the moment the reader pages away, with no rule to make it.
  a.note(at(3, 299));
  CHECK(a.aheadOf(at(3, 299)));
  CHECK(a.get() == at(3, 300));
}

TEST_CASE("PAGING BACK N AND RETURNING LANDS EXACTLY WHERE YOU LEFT, for every page") {
  // The strong property at the rule level. Every page is used as a departure, because
  // a rule that is off by one is right at page 1 and wrong everywhere after it -- and
  // the off-by-one available here is `aheadOf`'s comparison: `<=` instead of `<` makes
  // the field promise the page under the reader's feet.
  const int kPages = 24;
  auto page = [](int p) { return at(2, p * 3, p % 2); };  // pages are not one block wide

  for (int from = 1; from < kPages; ++from) {
    ReturnAnchor a;
    for (int p = 0; p <= from; ++p) a.note(page(p));
    CHECK_FALSE(a.aheadOf(page(from)));  // at the furthest point: no promise

    // All the way back, checking the promise holds still and names the departure.
    for (int p = from - 1; p >= 0; --p) {
      a.note(page(p));
      CHECK(a.aheadOf(page(p)));
      CHECK(a.get() == page(from));
    }
    // One return, from however far away, lands exactly on the departure page.
    const AnchorPos target = a.get();
    CHECK(target == page(from));
    a.note(target);
    CHECK_FALSE(a.aheadOf(target));
  }
}

TEST_CASE("positions compare in READING order, across chapters") {
  // Spine order is reading order, which is what makes the high-water rule work at all
  // -- and a comparison that only looked at `block` would call chapter 1's block 300
  // later than chapter 7's block 0.
  CHECK(at(1, 300) < at(7, 0));
  CHECK(at(3, 10, 0) < at(3, 10, 1));
  CHECK(at(3, 9, 99) < at(3, 10, 0));
  CHECK_FALSE(at(3, 10, 1) < at(3, 10, 1));
  CHECK(at(3, 10, 1) <= at(3, 10, 1));
}

TEST_CASE("the mark is raised by LINE as well as by block, so a page is not a block") {
  // `note` compares whole positions. A rule that compared blocks alone would refuse to
  // raise within a long block, and a chapter set in long paragraphs is several pages to
  // one of them.
  ReturnAnchor a;
  a.note(at(3, 10, 0));
  a.note(at(3, 10, 5));
  CHECK(a.get() == at(3, 10, 5));
  CHECK(a.aheadOf(at(3, 10, 4)));
  CHECK_FALSE(a.aheadOf(at(3, 10, 5)));
}

TEST_CASE("A RESTORED MARK FROM THE SIDECAR BEHAVES LIKE ONE THAT WAS RAISED") {
  // The one path that sets a mark without a movement. It assigns rather than raising
  // because the record IS the mark -- and after that it is indistinguishable from one
  // this reading produced.
  ReturnAnchor a;
  a.set(at(7, 300, 1));
  REQUIRE(a.isSet());
  CHECK(a.get() == at(7, 300, 1));
  CHECK(a.aheadOf(at(7, 299)));       // promised, exactly as a raised one would be
  CHECK(a.aheadOf(at(1, 50)));        // including from another chapter
  CHECK_FALSE(a.aheadOf(at(7, 300, 1)));
  CHECK_FALSE(a.aheadOf(at(9, 0)));   // and not from beyond it

  // It is raised by movement past it...
  a.note(at(9, 0));
  CHECK(a.get() == at(9, 0));
  // ...and not lowered by movement behind it.
  a.note(at(1, 0));
  CHECK(a.get() == at(9, 0));
}

TEST_CASE("A RECORD BEHIND WHERE THE BOOK REOPENS IS RAISED BY THE LANDING") {
  // The ordering the factory relies on: restoreAnchor runs BEFORE setMetrics lands the
  // page, so a stale record cannot leave the reader promised a page behind them.
  ReturnAnchor a;
  a.set(at(2, 10));      // what the sidecar held
  a.note(at(5, 0));      // where the book actually reopened
  CHECK(a.get() == at(5, 0));
  CHECK_FALSE(a.aheadOf(at(5, 0)));
}

TEST_CASE("clear() forgets the mark, which is what a change of book means") {
  // The only thing that clears a mark. It is structural in the Reader -- the anchor is
  // a member of the screen and a screen holds one book -- so this exists for the
  // factory's clearReaderAnchor and for a test.
  ReturnAnchor a;
  a.note(at(4, 100));
  REQUIRE(a.isSet());
  a.clear();
  CHECK_FALSE(a.isSet());
  CHECK_FALSE(a.aheadOf(at(4, 99)));
  // ZEROED rather than left stale, so a caller that forgets to ask gets an obviously
  // wrong answer rather than a plausible one.
  CHECK(a.get() == at(0, 0));
}

TEST_CASE("note() reports whether it moved") {
  ReturnAnchor a;
  CHECK(a.note(at(1, 0)));         // the first is always a raise
  CHECK(a.note(at(1, 1)));
  CHECK_FALSE(a.note(at(1, 1)));   // standing still is not a raise
  CHECK_FALSE(a.note(at(1, 0)));   // nor is going back
  CHECK(a.note(at(2, 0)));
}
