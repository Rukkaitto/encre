#pragma once

#include <cstddef>
#include <cstdint>

namespace reader {

// WHETHER A LOG LINE IS WORTH KEEPING, AND WHETHER WHAT IS KEPT MAY GO TO THE CARD.
//
// The card log exists for the one class of fault the cable cannot see: serial write
// and flush short-circuit unplugged and BLOCK when a host is attached, and attaching
// after a sleep can reset the chip -- which turns the wake being investigated into a
// cold boot. So `logToCard` in /.reader/settings.json tees everything logf() writes
// into a RAM buffer that is appended to /encre.log in an idle window.
//
// IT HAD NEVER RUN (#47, #69). `gLogToCard` was read at four sites in
// shell/src/main.cpp and assigned at none, so a card saying `true` and a firmware
// ignoring it were indistinguishable. The one-line fix does not close it, and this
// class is why:
//
//   THE SETTING ARRIVES ~470 LINES AFTER THE LINES THE FEATURE EXISTS TO CAPTURE.
//   loadAndApplySettings() cannot run before the card is mounted, and the wake
//   diagnostics ([wake] refused / [wake] held), the [prev] crumb record, the reset
//   reason and the storage bring-up all print before that. A tee armed at the load
//   drops exactly the boot the diagnostic was wanted for.
//
// So there are THREE states rather than a bool, and the middle one is the whole
// point: the tee buffers from the first line of boot, and the setting decides
// afterwards whether what it holds is kept or thrown away.
//
//   Pending  -- the file has not been read. Lines are BUFFERED and nothing may be
//               written; the card is not mounted this early anyway, so the buffer is
//               the only thing that can carry those lines forward at all.
//   Enabled  -- the file said yes. What Pending accumulated is kept, and flushing is
//               allowed.
//   Disabled -- the file said no, or there was no file. The buffer is discarded and
//               the tee stops, so a device whose log is off pays one branch per line.
//
// A DROP IS COUNTED, NEVER SILENT -- a hole in the log must not read as the device
// having gone quiet -- except while Disabled, where there is no log to have a hole
// in it and reporting one would make the [alive] line claim a loss nobody asked for.
//
// IN `core/` BECAUSE `shell/` HAS NO TEST HARNESS. All of this is bytes in and bytes
// out with no hardware in it; the shell owns the 4 KB array (passed in, so nothing
// here allocates) and the card write, which is the only part a desktop test could
// not reach.
class CardLogBuffer {
 public:
  enum class State : uint8_t { Pending, Enabled, Disabled };

  // constexpr, so the shell's file-scope instance is constant-initialised: logf()
  // can only run from setup() or loop(), but a tee whose constructor had not run yet
  // is not a question worth leaving open in a diagnostic.
  constexpr CardLogBuffer(char* storage, size_t capacity) : buf_(storage), cap_(capacity) {}

  State state() const { return state_; }
  // The setting said yes. This is what gates writing the card and reporting the
  // instrument's own weight.
  bool enabled() const { return state_ == State::Enabled; }
  // Lines are being kept. True while Pending as well, which is the difference
  // between this and enabled().
  bool buffering() const { return state_ != State::Disabled; }

  // Idempotent for the same answer, because loadAndApplySettings() runs a SECOND
  // time on the RETRY path -- a card that was absent at boot and has appeared. A
  // re-arm that discarded would throw away everything logged since the first one.
  void applySetting(bool on) {
    if (on) {
      state_ = State::Enabled;
      return;
    }
    if (state_ == State::Disabled) return;
    state_ = State::Disabled;
    len_ = 0;
    dropped_ = 0;
  }

  // Never blocks, never allocates, never touches the card.
  void append(const char* s, size_t n) {
    if (state_ == State::Disabled || n == 0 || s == nullptr) return;
    if (len_ + n > cap_) {
      // REFUSED WHOLE rather than truncated: half a log line reads as a corrupt log,
      // where a missing one plus a count reads as what it is.
      dropped_ += static_cast<uint32_t>(n);
      return;
    }
    for (size_t i = 0; i < n; ++i) buf_[len_ + i] = s[i];
    len_ += n;
  }

  const char* data() const { return buf_; }
  size_t size() const { return len_; }
  size_t capacity() const { return cap_; }
  uint32_t dropped() const { return dropped_; }

  // Past the threshold AND authorised. The threshold is the caller's, because it is
  // a headroom decision about the caller's buffer: flushing short of full leaves
  // room for the burst that arrives before the next idle window, so the lines a
  // burst loses are the ones that would have overflowed anyway.
  bool wantsFlush(size_t threshold) const { return enabled() && len_ >= threshold; }

  // THE RESERVE IS GONE AND THE NEXT LINE WILL BE REFUSED -- flush wherever you are.
  //
  // wantsFlush() alone is a ONE-SHOT reserve and #83 is what that costs. The caller
  // may only write in an idle window, so between crossing the threshold and the next
  // quiet loop iteration the free space can only shrink and nothing tops it up. A
  // reader turning pages keeps a paint owed or a press queued continuously, so that
  // stretch is bounded by the USER rather than by anything the firmware picks: no
  // threshold derived from a burst size can be a bound, it can only make the hole
  // rarer. Measured on glass (X3, 2026-09-07): 1024 B of reserve was overrun by
  // 407 B, and the log lost whole lines in the one window a fault is most
  // interesting -- the "reports on less than it claims" shape.
  //
  // So the reserve is restored EVERY loop iteration instead of once per idle window,
  // and this is the question that does it. What it buys is a bound the caller's
  // threshold cannot express: at the start of every iteration there are at least
  // `reserveBytes` free, so a drop now needs more than that to arrive inside ONE
  // iteration rather than merely more than the headroom across an open-ended stretch.
  //
  // `reserveBytes` IS FREE SPACE, WHERE wantsFlush() TAKES A FILL LEVEL. They are
  // deliberately different questions -- one is "enough has accumulated to be worth a
  // write", the other is "there is no longer room to keep logging" -- and the caller
  // must keep the second point above the first or every flush becomes this one and
  // the idle gate stops meaning anything.
  //
  // ARMING IS ASKED HERE TOO, exactly as wantsFlush() asks it: nothing may reach the
  // card before the setting has been read, and a Pending buffer that filled during
  // boot must keep what it holds rather than write it to a card that is not mounted.
  // And an EMPTY buffer never must: a reserve larger than the whole buffer would
  // otherwise ask for a write of nothing on every iteration for ever. Note the
  // Disabled half of the arming rides `len_ > 0` rather than `enabled()` -- a
  // Disabled buffer is always empty, because applySetting(false) zeroes it and
  // append() returns early -- so the term enabled() actually holds is PENDING.
  bool mustFlush(size_t reserveBytes) const {
    return enabled() && len_ > 0 && (cap_ - len_) < reserveBytes;
  }

  // The buffer is emptied either way. A card that refuses the write must not make
  // the buffer grow until it starts losing lines silently, and a log that stops the
  // device working is worse than no log -- so a failure shows up as a gap plus the
  // dropped count on the next line that does land.
  void wrote(bool ok) {
    if (!ok) dropped_ += static_cast<uint32_t>(len_);
    len_ = 0;
  }

 private:
  char* buf_ = nullptr;
  size_t cap_ = 0;
  size_t len_ = 0;
  uint32_t dropped_ = 0;
  State state_ = State::Pending;
};

}  // namespace reader
