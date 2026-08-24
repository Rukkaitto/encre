#pragma once

namespace reader {

// A LONG OPERATION SAYING IT IS STILL GOING, so the owner can decide whether the
// user needs telling.
//
// The problem it solves: opening a book is one blocking call from the shell's side
// -- locate, read the contents, paginate to the saved page -- and on the device that
// measured 2269 ms for a position deep in a long chapter. The shell is the only
// thing that knows a deadline has passed and the only thing that can paint; the walk
// is the only thing that knows it is still walking. Neither can see the other.
//
// SO THE OWNER INSTALLS A HOOK, exactly as reader::Profile takes its clock and for
// the identical reason: `core/` compiles for the desktop and the ESP32 alike, so it
// has no clock, no panel and no business acquiring either. With nothing installed
// `tick()` is a load and a branch, which is what the simulator and the tests pay.
//
// WHAT IT IS NOT: a progress FRACTION. A walk does not know how far it has to go --
// that is the whole reason the page count is deferred in the first place -- and a
// bar that cannot be truthful is worse than a line that says only "still working".
// The one thing every caller can honestly report is that it has not finished.
//
// Called from the reader's decode walks, which are the only operations on this
// device that run long enough to need it. It must stay cheap enough to sit in a
// per-block loop: the handler, not this, is where any real work belongs.
class Progress {
 public:
  using Fn = void (*)(void*);

  // Null disables. Installing also clears whatever the previous handler had, so a
  // caller cannot inherit a deadline from an operation that has already finished.
  static void install(Fn f, void* ctx);
  static void tick() {
    if (fn_ != nullptr) fn_(ctx_);
  }

 private:
  static Fn fn_;
  static void* ctx_;
};

}  // namespace reader
