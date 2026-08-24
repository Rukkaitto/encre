#pragma once
#include <cstdint>

namespace reader {

// --- Where you were before you stopped reading linearly ----------------------
//
// One optional position, and the whole of it is a state machine over three kinds of
// movement. It lives here rather than inside ScreenReader because it is a RULE, and
// a rule that is off by one is right at page 1 and wrong everywhere after it -- so
// it wants tests that never open a book.
//
// spec: docs/superpowers/specs/2026-08-24-peek-and-return-design.md
// board: design/ReaderAnchored.dc.html
//
// THE TRIPLE IS A PAGE, not a chapter. `(spine, block, line)` is exactly what a
// page-start cursor is -- `starts_` holds one of these per page -- so the anchor
// names one page and `Up` returns to that page. Page back three pages inside one
// chapter and the anchor is the page you left.
//
// IT IS NOT A PAGE NUMBER, and that is the load-bearing choice. `at_` is an index
// into the CURRENT chapter's `starts_`, so "page 7" means nothing once you are in a
// different chapter, and two positions in different chapters could not be compared
// -- while the high-water rule is entirely a comparison. `(spine, block, line)`
// compares lexicographically and spine order IS reading order, so the ordering is
// the book's own rather than one invented here. It is also the triple
// `ReadingPosition` already stores, so nothing new is persisted.
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
  bool isSet() const { return set_; }
  // Meaningful only while `isSet()`. Zeroed rather than left stale when cleared, so
  // a caller that forgets to ask gets an obviously wrong answer rather than a
  // plausible one.
  AnchorPos get() const { return at_; }

  void clear() {
    set_ = false;
    at_ = AnchorPos{};
  }

  void set(AnchorPos p) {
    at_ = p;
    set_ = true;
  }

  // --- The three movements -------------------------------------------------
  //
  // Each takes the position being LEFT and the position being ARRIVED AT, because
  // two of the three rules need both.

  // READING FORWARD NEVER RAISES AN ANCHOR. That is the part that is easy to get
  // wrong, and getting it wrong makes the anchor name the last thing the reader did
  // instead of the furthest point of their excursion.
  //
  // It only ever SATISFIES one: arriving at or past the anchor means the reader has
  // read back up to where they were, so the excursion is over and the promise is
  // spent.
  void pagedForward(const AnchorPos& /*from*/, const AnchorPos& to) {
    if (set_ && at_ <= to) clear();
  }

  // Paging backward is how a reader loses their place far more often than by
  // jumping, and it is what makes the anchor appear during ordinary reading.
  //
  // IF UNSET. An anchor already standing HOLDS STILL while the reader moves around
  // below it -- lowering it on every backward turn would make it name the last turn
  // rather than the top of the excursion, which is the one thing it is for.
  void pagedBackward(const AnchorPos& from, const AnchorPos& /*to*/) {
    if (!set_) set(from);
  }

  // A JUMP OVERWRITES UNCONDITIONALLY, and this is why the rule needs two cases at
  // all. Commit a peek from chapter 2 into chapter 8 and a pure high-water rule
  // would find the anchor BEHIND the reader and clear it as satisfied -- throwing
  // away the one breadcrumb they wanted. Distinguishing a departure from a drift is
  // not a special case; it is the whole distinction.
  void jumped(const AnchorPos& from, const AnchorPos& /*to*/) { set(from); }

  // Following the anchor. Returns where to go and clears, or false when there is
  // nowhere to go -- which is the state in which `Up` must do nothing.
  //
  // IT CLEARS BY THE FORWARD RULE RATHER THAN BY A NEW ONE. Neither the spec nor
  // the board says what happens after a return, and nothing needs to: `Up` lands
  // exactly ON the anchor, and "arriving at or past it" is already the condition
  // that spends it. The alternative -- re-anchoring to where the reader came from,
  // so `Up` toggles -- would have to treat a jump TO the anchor as a departure FROM
  // it, which is the one reading the rules do not support.
  bool follow(AnchorPos* out) {
    if (!set_) return false;
    if (out != nullptr) *out = at_;
    clear();
    return true;
  }

 private:
  AnchorPos at_{};
  bool set_ = false;
};

}  // namespace reader
