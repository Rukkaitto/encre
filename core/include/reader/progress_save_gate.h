#pragma once

#include <cstdint>

#include "reader/layout.h"

namespace reader {

// WHETHER THE READING POSITION IS WORTH WRITING TO THE CARD RIGHT NOW.
//
// The reading position used to be saved on three edges only -- leaving the book,
// crossing a chapter, and sleeping -- because "a turn is ~570 ms of panel and a card
// write on top of each one would be felt". That bounded what a power cut costs the
// reader at one chapter, which on a real novel is a lot of reading to lose.
//
// The saving is now opportunistic instead: the shell offers the position to this gate
// in the same quiet window the page count, the refinement and the ring warm already
// use, and the gate says whether it is worth the bus. It answers two questions the
// shell cannot be trusted with, because `shell/` has no test harness:
//
//   1. HAS THE READER ACTUALLY MOVED. The quiet window fires on every loop iteration
//      once the buttons go quiet, so without this the device would re-save the same
//      page hundreds of times a second. `savePosition` would answer `Unchanged` and
//      write nothing -- but it reaches that answer by READING THE WHOLE SIDECAR BACK
//      first (reading_store.cpp's writeIfChanged), so "unchanged" is not free: it is
//      two file reads on the display's SPI bus, per iteration, forever. Comparing
//      three ints here costs nothing and never touches the card at all.
//
//   2. HAS THE CARD EARNED ANOTHER ATTEMPT. This is the hazard the whole feature
//      turns on. A card can be readable and refuse writes -- a physical write-protect
//      tab does exactly that -- and `writeAll` calls `noteCardGone()` when a write it
//      had already opened goes wrong, which `pollCardPresence` turns into an App
//      rooted at SdMissingScreen. So a failing save can throw the reader out of a book
//      they can still perfectly well read. That risk existed at three edges a session;
//      saving per page turn would multiply it by a hundred. The gate backs off after a
//      failure and GIVES UP after kGiveUpAfterFailures of them, so a write-protected
//      card costs at most three extra attempts per session and then this mechanism
//      goes silent -- leaving exactly the three original edges, which is the behaviour
//      that shipped.
//
// Deliberately NOT a timer for the quiet window itself. That is the shell's question,
// because it is about the panel and the button queue, and `core/` has no clock. The
// only time this gate keeps is the post-failure backoff, and `nowMs` is passed in.
struct SavePoint {
  int spine = 0;
  int block = 0;
  int line = 0;

  SavePoint() = default;
  SavePoint(int spineIndex, const Cursor& at) : spine(spineIndex), block(at.block), line(at.line) {}
  SavePoint(int spineIndex, int blockIndex, int lineIndex)
      : spine(spineIndex), block(blockIndex), line(lineIndex) {}

  bool operator==(const SavePoint& o) const {
    return spine == o.spine && block == o.block && line == o.line;
  }
  bool operator!=(const SavePoint& o) const { return !(*this == o); }
};

class ProgressSaveGate {
 public:
  // Three consecutive failures and this mechanism stops asking. Three rather than one
  // because a single failure can be a transient -- a card mid-housekeeping, a write
  // that lost a race -- and rather than ten because every attempt past the first is a
  // fresh chance to trip noteCardGone() on a card that is simply read-only.
  static constexpr int kGiveUpAfterFailures = 3;

  // A failure is worth retrying, but not soon. The three original save edges are still
  // live and still unconditional, so nothing is lost by waiting; and spacing the
  // retries is what keeps a read-only card from spending its whole give-up budget in
  // three consecutive loop iterations, which would make the backoff decorative.
  static constexpr uint32_t kRetryBackoffMs = 30000;

  // Is this position worth a write? False when it is already stored, when a failure is
  // still backing off, and forever once the gate has given up.
  bool wants(const SavePoint& where, uint32_t nowMs) const {
    if (givenUp()) return false;
    // Unsigned difference, so this is correct across the ~49-day millis() wrap for the
    // same reason every other quiet-window gate in the shell is.
    if (failures_ > 0 && static_cast<uint32_t>(nowMs - failedAtMs_) < kRetryBackoffMs) return false;
    return !have_ || where != stored_;
  }

  // The card now holds this position. Called for `Written` AND for `Unchanged`, which
  // are the same fact about the card and differ only in who put it there.
  void noteStored(const SavePoint& where) {
    stored_ = where;
    have_ = true;
    failures_ = 0;
  }

  void noteFailed(uint32_t nowMs) {
    if (failures_ < kGiveUpAfterFailures) ++failures_;
    failedAtMs_ = nowMs;
  }

  // A different book is open, so what is stored describes a file this gate is no
  // longer talking about.
  //
  // IT DELIBERATELY DOES NOT CLEAR THE FAILURE COUNT. Giving up is a fact about the
  // CARD, not about the book, and a card that has refused three writes will refuse the
  // next one too -- re-arming per book would turn a give-up into a pause and hand a
  // read-only card three fresh attempts for every book opened. A reboot clears it,
  // which is the right granularity for "the user has probably changed something".
  void forget() {
    stored_ = SavePoint{};
    have_ = false;
  }

  bool givenUp() const { return failures_ >= kGiveUpAfterFailures; }
  int failures() const { return failures_; }

 private:
  SavePoint stored_{};
  bool have_ = false;
  int failures_ = 0;
  uint32_t failedAtMs_ = 0;
};

}  // namespace reader
