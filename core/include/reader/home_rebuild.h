#pragma once

#include <cstdint>

namespace reader {

// DOES HOME'S VIEW MODEL STILL DESCRIBE THE CARD.
//
// Home is the App's ROOT, so returning to it hands back the same instance with the view
// model it was CONSTRUCTED with -- and two of the four things that view model states are
// facts about the card that can change while the firmware runs: the LIBRARY row's count,
// and whether there is anything to continue at all. So the shell rebuilds Home when
// either has moved, and this object is the question "has it".
//
// IT WAS A BARE `bool` AND THAT BOOL WAS A CALLER LIST. Every route that could change
// what Home says had to remember to set it, the routes are not enumerable from Home's
// side, and the one that did not remember produced exactly the defect this file's own
// rule predicts: a deleted book left Home reporting the pre-delete count until a reading
// position was saved, a card was re-inserted, or the device rebooted (#43). The count's
// own cache was already keyed on the removal counter and was simply never consulted on
// that path -- the invalidation was right and nothing asked it.
//
// SO ONE HALF IS DERIVED AND ONLY THE OTHER IS LATCHED, and the split is the design:
//
//   * A BOOK LEAVING is derived, from the count of removals the filesystem has seen.
//     Every delete goes through one `FileSystem::remove`, which counts the ASK above
//     all of its own refusals, so a removal cannot happen without this seeing it and
//     no caller can be the one that forgot. That covers a book deleted from the actions
//     panel, the same book deleted from the corrupt-book dialog, the card's own
//     `last.json` being dropped when a book is marked finished, and whatever the next
//     door to a removal turns out to be.
//
//   * A READING POSITION MOVING is latched, because it is not derivable from anything
//     the shell can ask a filesystem: the sidecar is REWRITTEN rather than removed, and
//     `savePosition` reaching `Unchanged` is a fact about the card that leaves no
//     counter behind. `markStale()` is that one signal, and it has one meaning.
//
// A BOOK CANNOT ARRIVE while V1 runs -- transfer is card-only, so putting a book on the
// card means the card is in a computer and this firmware is not -- which is the same
// premise the count's cache key rests on. V2's Wi-Fi transfer is what breaks it, and it
// breaks both at once: a transfer that writes a book has to `markStale()`.
//
// THE CARD GOING AWAY OR COMING BACK IS NOT THIS OBJECT'S QUESTION. Both edges replace
// the whole App -- rooted at the SD-missing screen on the way out and at a freshly built
// Home on the way back -- so there is no surviving Home to be stale, and the count's
// cache keys on usability separately.
//
// It lives in `core/` for `ProgressSaveGate`'s reason: `shell/` has no test harness, and
// this is a latch plus a comparison whose one trap is invisible on a desktop and
// expensive on the panel. `noteBuilt()` re-stamps the counter as well as clearing the
// latch, so a rebuild that happened BECAUSE of a removal stops asking for another one --
// without that, one delete would rebuild Home on every loop iteration for the rest of the
// session, replacing the App under the user each time.
class HomeRebuildGate {
 public:
  // Home has something different to say for a reason no counter can carry.
  void markStale() { latched_ = true; }

  // Should Home be rebuilt? `removals` is the filesystem's cumulative count of removal
  // ASKS. It is passed in rather than held, because `core/` has no filesystem here and
  // the counter belongs to the one that mounted the card.
  bool stale(uint32_t removals) const { return latched_ || removals != builtAtRemovals_; }

  // Home has just been built, from the card in this state. Called by the ONE function
  // that builds Home, which is what keeps this off the caller list the bool was.
  //
  // UNSIGNED, so it is correct across a wrap of the counter for the same reason every
  // quiet-window gate in the shell is: only inequality is ever asked.
  void noteBuilt(uint32_t removals) {
    latched_ = false;
    builtAtRemovals_ = removals;
  }

  // For the log line. "Home was rebuilt" has two causes and they are worth telling
  // apart on a device: a position that moved, or a book that left.
  bool latched() const { return latched_; }
  uint32_t builtAtRemovals() const { return builtAtRemovals_; }

 private:
  bool latched_ = false;
  // Zero, which is a fresh filesystem's own count -- so a Home that has never been
  // built is not reported stale by a card nothing has been removed from. The shell
  // builds Home at boot and that build is what stamps this for real.
  uint32_t builtAtRemovals_ = 0;
};

}  // namespace reader
