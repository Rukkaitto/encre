#pragma once
#include <cstdint>

namespace reader {

// --- THE FURTHEST YOU HAVE BEEN, AND THE WAY BACK TO IT ----------------------
//
// One position and ONE transition: the anchor is a HIGH-WATER MARK, the most advanced
// place the reading position has ever reached in this book. It only ever rises. `Up`
// returns to it, and the footer's third field names it -- but both are gated on the
// mark being AHEAD of where the reader is standing, because at the furthest point
// there is nothing to promise.
//
// spec: docs/superpowers/specs/2026-08-24-peek-and-return-design.md
// board: design/ReaderAnchored.dc.html
//
// It lives here rather than inside ScreenReader because it is a RULE, and a rule that
// is off by one is right at page 1 and wrong everywhere after it -- so it wants tests
// that never open a book.
//
// --- WHAT THIS REPLACED, AND THE MEASUREMENT THAT KILLED IT ------------------
//
// This was three transitions -- `pagedForward`, `pagedBackward`, `jumped` --
// implementing "where you were before you stopped reading linearly": the anchor was
// only ever set to a DEPARTURE point, and reading forward onto it spent it. The
// header argued at length that the two extra cases were load-bearing, that "a jump
// overwrites unconditionally, and this is why the rule needs two cases at all". That
// argument was wrong, and it was wrong in the direction that matters -- it defended
// the case it could not serve.
//
// A reader resuming a paper book is in chapter 1 and jumps to chapter 36. `jumped`
// set the anchor to chapter 1, BEHIND them; `pagedForward`'s "arriving at or past the
// anchor means you have read back up to it" then cleared it on the first page turn.
// Run against the real class:
//
//     after forward jump ch1->ch36: set=1 spine=0
//     after ONE forward page turn:  set=0 spine=0
//
// So the anchor never advanced to where the reader was, and a forward jump bought a
// way back that survived exactly one press. Three rules to produce that.
//
// --- WHAT THE ONE RULE BUYS --------------------------------------------------
//
// Every movement goes through `note()` -- forward page, backward page, chapter
// crossing, jump, and following the anchor itself -- so there is no movement the
// screen has to classify, and therefore no movement it can classify wrongly. That is
// the whole of the collapse: the three transitions were a taxonomy of presses, and a
// taxonomy has to be complete to be correct.
//
// FOLLOWING IT DOES NOT CLEAR IT, and does not need to. You arrive AT the mark, so it
// stops being ahead and the field withdraws itself -- and it reappears the moment you
// page away, with no "the first backward turn sets one" rule to make it. The old
// shape needed `follow()` to clear, and needed a paragraph explaining which of the
// three rules did the clearing.
//
// CLEARED ONLY WHEN THE BOOK CHANGES, which is structural rather than a call anybody
// has to remember: the anchor is a member of ReaderScreen and a screen holds one book.
//
// --- WHAT IT COSTS -----------------------------------------------------------
//
// COMMITTING A PEEK FORWARD NOW LEAVES NO WAY BACK. The reader jumps ahead, the mark
// rises to the arrival, and nothing is promised. The old rule nominally offered a way
// back there -- and only nominally: per the measurement above it offered it for one
// press. So almost nothing real is lost, and what is lost is stated here rather than
// discovered on the panel.
//
// THE TRIPLE IS A PAGE, not a chapter. `(spine, block, line)` is exactly what a
// page-start cursor is -- `starts_` holds one of these per page -- so the anchor names
// one page and `Up` returns to that page.
//
// IT IS NOT A PAGE NUMBER, and that is the load-bearing choice. `at_` is an index into
// the CURRENT chapter's `starts_`, so "page 7" means nothing once you are in a
// different chapter, and two positions in different chapters could not be compared --
// while the whole rule is a comparison. `(spine, block, line)` compares
// lexicographically and spine order IS reading order, so the ordering is the book's
// own rather than one invented here. It is also the triple `ReadingPosition` already
// stores, so nothing new is persisted.
struct AnchorPos {
  int spine = 0;
  int block = 0;
  int line = 0;

  bool operator==(const AnchorPos& o) const {
    return spine == o.spine && block == o.block && line == o.line;
  }
  bool operator!=(const AnchorPos& o) const { return !(*this == o); }
  // Reading order, which is spine order then position within the chapter.
  bool operator<(const AnchorPos& o) const {
    if (spine != o.spine) return spine < o.spine;
    if (block != o.block) return block < o.block;
    return line < o.line;
  }
  bool operator<=(const AnchorPos& o) const { return !(o < *this); }
};

class ReturnAnchor {
 public:
  // A mark is stored. Distinct from `aheadOf`, and only two things want it: the
  // sidecar, which persists a mark, and a test. NOTHING on the interaction path may
  // ask this -- see aheadOf.
  bool isSet() const { return set_; }
  // Meaningful only while `isSet()`. Zeroed rather than left stale when cleared, so a
  // caller that forgets to ask gets an obviously wrong answer rather than a plausible
  // one.
  AnchorPos get() const { return at_; }

  void clear() {
    set_ = false;
    at_ = AnchorPos{};
  }

  // THE RESTORE PATH, from the sidecar, and the one way a mark arrives without a
  // movement. It assigns rather than raising because the record IS the mark -- there
  // is nothing yet for it to be beyond. It runs before the reader lands (the factory
  // calls it ahead of setMetrics), so the landing's own `note` immediately raises it
  // if the record is behind where the book reopens.
  void set(AnchorPos p) {
    at_ = p;
    set_ = true;
  }

  // --- THE ONE TRANSITION ----------------------------------------------------
  //
  // Called after EVERY movement of the reading position, with where it ended up.
  // Raises the mark if that is further through the book than the mark has ever been,
  // and does nothing otherwise -- so paging back, following the mark, and jumping
  // backward all leave it exactly where it was.
  //
  // IT TAKES ONLY THE ARRIVAL. The old three took the departure as well, which is
  // what made them three: a departure is only interesting if you are classifying the
  // KIND of movement, and this rule does not.
  //
  // Returns whether it moved, for a caller that wants to log or test it. No caller
  // needs it today; it is free, and a transition that reports nothing cannot be
  // observed to have stopped firing.
  bool note(const AnchorPos& here) {
    if (set_ && here <= at_) return false;
    at_ = here;
    set_ = true;
    return true;
  }

  // IS THERE SOMEWHERE TO GO BACK TO. THE ONE PREDICATE, asked by the footer field
  // and by `Up`, so the promise and the affordance cannot disagree -- this project has
  // shipped a dead button twice, and both times because two conditions were spelled
  // separately and drifted.
  //
  // STRICTLY AHEAD. Standing ON the mark is the ordinary state of a reader who has
  // never turned back, and a field promising the page under your feet is worse than
  // no field.
  bool aheadOf(const AnchorPos& here) const { return set_ && here < at_; }

 private:
  AnchorPos at_{};
  bool set_ = false;
};

// THERE IS NO `follow()` ANY MORE, and its absence is the point. It existed to answer
// "is there a target, and what is it, and spend it" in one call, because spending was
// a rule of its own that a caller could forget. Under the high-water rule nothing is
// spent -- arriving at the mark is what withdraws the promise, and arriving is a
// movement like any other -- so the two survivors are a predicate and a getter, and
// the predicate is the same one the footer asks. See ReaderScreen's Gesture::AltPrev.

}  // namespace reader
