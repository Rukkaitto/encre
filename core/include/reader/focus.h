#pragma once
namespace reader {

// WHERE THE SELECTION IS IN A LIST, and the one place the rules about moving it
// live.
//
// Five screens each carried their own copy of this: add a delta, clamp to a
// range, report whether anything moved. HomeScreen, the Settings list, the two overlay
// panels and ScrollWindow, with the range spelled slightly differently in each.
// The duplication is not hypothetical harm -- it is why "clamp, do not wrap" had
// to be written into four separate comments to stay one rule, and why three of
// those screens shipped a focus() with no setFocus() and nothing caught it.
//
// A SCREEN DECLARES ITS RANGE AND GETS THE BEHAVIOUR. Nothing here knows what a
// row is, what is drawn, or which screen it belongs to; ScrollWindow adds the
// window on top for lists longer than the panel, and the screens that fit on one
// panel use this directly.
//
// -1 IS A POSITION, NOT AN ABSENCE, and that is the distinction the whole class
// turns on. Home's CONTINUE block sits below the first menu row and is somewhere
// the user really is -- the wake record has to store it and hand it back. An
// EMPTY list is also -1, because there is no row 0 to be on. Those are different
// states that share a number, which is exactly why `lowest()` exists rather than
// a caller comparing against -1 and guessing which it has.
class Focus {
 public:
  // Whether -1 is a position below the first item. Named rather than a bool at
  // the call site: `Focus(menu.size(), true)` says nothing about what is true.
  enum None { Noneless, WithNone };

  // WHETHER A POSITION MAY BE LANDED ON. Consulted by the gated overloads below:
  // move() steps over a refused position without consuming any of its distance,
  // and set() refuses a landing outright. The CURRENT position is never asked --
  // a focus can find itself somewhere it could not land (a screen before its
  // first setFocus), and moving OFF such a place must work.
  //
  // An interface rather than a callable for the reason ScreenFactory is one
  // (app.h): no <functional>, no allocation, and the one implementer is a
  // long-lived screen that can simply be pointed at.
  class Gate {
   public:
    virtual bool focusable(int index) const = 0;

   protected:
    ~Gate() = default;  // never owned, never deleted through this interface
  };

  Focus() = default;
  explicit Focus(int count, None none = Noneless);

  int index() const { return index_; }
  int count() const { return count_; }

  // The lowest position this focus can hold: -1 with a none slot, 0 without --
  // and ALWAYS -1 for an empty list, whichever it was built as.
  int lowest() const;

  // Selects outright, CLAMPING into range even when wrapping is on, and returns
  // whether anything moved.
  //
  // The clamp is not an oversight in the wrapping case, it is the point: this is
  // the restore path, where the wake record names a row and no press implies it.
  // A record naming row 400 of a list that now has three rows means "as far down
  // as you can go"; wrapping it round to row 1 would put the user somewhere with
  // no relation to where they were.
  //
  // With a `gate`, the clamp happens FIRST and the landing is then judged: a
  // landing the gate refuses restores the previous index and returns false,
  // which is what "refused rather than clamped" has to mean on a list whose
  // ends are unfocusable.
  bool set(int index, const Gate* gate = nullptr);

  // Moves by `delta`, wrapping if this focus wraps and clamping if it does not.
  // Returns whether anything moved, so a screen can answer Action::none() at the
  // end of a list instead of paying a refresh that repaints an identical screen.
  // On this panel that is at least 520 ms, and spending it to change nothing is
  // what makes the end of a list feel like a stuck button.
  //
  // `delta` may be far larger than the list: a held Up or Down delivers a
  // DISTANCE rather than a press (InputEvent::steps), so a wrapping list has to
  // take a delta of several laps and land where one lap would.
  //
  // `held` settles an interaction neither wrapping nor held-scroll owned alone.
  // A wrap is right for a PRESS: the screen always changes, so it can never read
  // as a dead button. It is wrong for a HOLD: a held button that wraps has no
  // end and cycles for as long as it is down, which is not scrolling, it is a
  // carousel. So a held move CLAMPS even on a wrapping focus. Here rather than
  // in a screen because it is one rule and there were two screens about to each
  // get their own copy of it -- the same shape as the five copies of the clamp
  // this class was extracted to remove.
  //
  // With a `gate`, a refused position is stepped over without consuming any of
  // the distance, wrapping through refused ends (clamping at them when `held`)
  // -- the walk Settings used to hand-roll, in the one place movement rules
  // live.
  bool move(int delta, bool held = false, const Gate* gate = nullptr);
  // The gate is the THIRD argument. Without this deletion, `move(d, &gate)`
  // would compile -- the pointer silently converting to `held == true` -- and
  // clamp a list someone meant to gate.
  bool move(int delta, const Gate*) = delete;

  // The list changed length -- a rescan after a delete, a menu built at boot. The
  // index is pulled back into range, because one left past the end indexes one
  // past the vector on the next render. Refilling a list that had emptied selects
  // the first row, EXCEPT where there is a none slot: -1 is where that focus
  // already was, so it stays there rather than being dragged onto row 0.
  void setCount(int n);

  // ON BY DEFAULT. Every list in the firmware wraps: Up from the first row goes to
  // the last, Down from the last comes back to the first.
  //
  // THIS REVERSED A DECISION THIS PROJECT HAD WRITTEN DOWN FOUR TIMES -- "clamp,
  // do not wrap: a list that jumps silently from the last item to the first is
  // indistinguishable from a stuck button". That argument was about ambiguity at
  // the end of a list, and it is worth knowing which half of it still stands. A
  // wrap is now the only thing a press at the end can do, so it is never confused
  // with a dead button: the screen always changes. What it costs instead is the
  // opposite reading -- a Down that appears to jump the user a long way -- and on
  // a four-row overlay or a three-row menu that is unambiguous, while on a
  // several-hundred-book Library it is the case to watch.
  //
  // AUTO-REPEAT WAS WHERE THIS WAS SHARPEST, and move()'s `held` flag is what
  // solved it: a HELD Down clamps at the end where a pressed one wraps, so the
  // Library no longer cycles for as long as the button is down. (This note said
  // "not solved here" until the flag existed -- an inherited-work note is a
  // claim with an expiry date.)
  //
  // The opt-out exists because a screen where running off the end is a mistake
  // rather than a convenience should say so.
  void setWrapping(bool on) { wrap_ = on; }
  bool wraps() const { return wrap_; }

 private:
  // Puts `index_` in range without touching anything else. One function, called
  // from every mutator, for the same reason ScrollWindow::clamp is one function.
  void clampIndex();

  // One position in `dir` from `from`, honouring the clamp and none-slot rules
  // and wrapping only when `wrapping` says so (a held move clamps even on a
  // wrapping focus) -- the arithmetic move(+/-1) performs, extracted so the
  // gated walk cannot become a second copy of it. Returns `from` itself at a
  // clamping end.
  int stepOnce(int from, int dir, bool wrapping) const;

  int count_ = 0;
  int index_ = -1;
  bool none_ = false;
  bool wrap_ = true;
};

}  // namespace reader
