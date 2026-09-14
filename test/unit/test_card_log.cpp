// THE CARD LOG'S ARMING QUESTION, WHICH IS WHY IT IS IN `core/` AT ALL.
//
// `logToCard` was parsed into Settings and never applied (#47, #69): gLogToCard was
// read at four sites in shell/src/main.cpp and assigned at none, so the buffer, the
// idle flush, the dropped-byte counting and the 256 KB cap had never run on any
// device. A one-line assignment at loadAndApplySettings() does not close it, because
// that function runs ~470 lines after the lines the feature exists to capture -- the
// wake diagnostics, the [prev] crumb record, the reset reason and the storage
// bring-up all print BEFORE the card has been mounted and therefore before anything
// can know whether the user asked for a log.
//
// So the tee buffers from the first line of boot and the setting decides, afterwards,
// whether what it holds is kept or thrown away. That is a three-state machine with
// exactly the shape `shell/` has no harness to check -- CLAUDE.md records five bugs
// that hid there -- so it lives here and the shell owns only the 4 KB array and the
// card write.
#include <cstring>
#include <string>

#include "doctest.h"
#include "reader/card_log.h"

using reader::CardLogBuffer;

namespace {

// A tiny capacity, because the overflow rule is the half a 4 KB array cannot reach in
// a test. The shell's real buffer is 4096.
struct Fixture {
  char storage[16];
  CardLogBuffer log{storage, sizeof(storage)};

  void put(const char* s) { log.append(s, std::strlen(s)); }
  std::string held() const { return std::string(log.data(), log.size()); }
};

}  // namespace

TEST_CASE("a fresh buffer is pending, so a line logged before the setting is known is kept") {
  Fixture f;
  CHECK(f.log.state() == CardLogBuffer::State::Pending);
  CHECK(f.log.buffering());
  CHECK_FALSE(f.log.enabled());

  f.put("[wake] refused\n");
  CHECK(f.log.size() == 15);
  CHECK(f.held() == "[wake] refused\n");
}

TEST_CASE("the setting turning out true keeps what was buffered before it was known") {
  Fixture f;
  f.put("[prev] boot\n");
  f.log.applySetting(true);

  CHECK(f.log.state() == CardLogBuffer::State::Enabled);
  CHECK(f.log.enabled());
  CHECK(f.held() == "[prev] boot\n");
}

TEST_CASE("the setting turning out false discards what was buffered and stops buffering") {
  Fixture f;
  f.put("[prev] boot\n");
  f.log.applySetting(false);

  CHECK(f.log.state() == CardLogBuffer::State::Disabled);
  CHECK_FALSE(f.log.enabled());
  CHECK_FALSE(f.log.buffering());
  CHECK(f.log.size() == 0);

  f.put("and nothing after it\n");
  CHECK(f.log.size() == 0);
}

TEST_CASE("a disabled log counts no drops, because nothing was asked for") {
  Fixture f;
  f.log.applySetting(false);
  f.put("a line far longer than the sixteen bytes this fixture holds\n");
  CHECK(f.log.dropped() == 0);
}

TEST_CASE("an overflow while pending is dropped and counted, never silently lost") {
  Fixture f;
  f.put("0123456789");  // 10 of 16
  f.put("abcdefgh");    // 8 more would be 18: refused whole
  CHECK(f.held() == "0123456789");
  CHECK(f.log.dropped() == 8);
}

TEST_CASE("a drop taken before the setting was known survives the setting turning out true") {
  Fixture f;
  f.put("0123456789");
  f.put("abcdefgh");
  f.log.applySetting(true);
  CHECK(f.log.dropped() == 8);
}

TEST_CASE("a drop taken before the setting was known is forgotten when it turns out false") {
  Fixture f;
  f.put("0123456789");
  f.put("abcdefgh");
  f.log.applySetting(false);
  // A log nobody asked for cannot have a hole in it, and `dropped` means the log HAS
  // a hole. Reporting one here would make the [alive] line claim a loss.
  CHECK(f.log.dropped() == 0);
}

TEST_CASE("a run that exactly fills the buffer is kept, not dropped") {
  Fixture f;
  f.put("0123456789abcdef");  // exactly 16
  CHECK(f.log.size() == 16);
  CHECK(f.log.dropped() == 0);
}

TEST_CASE("a run longer than the whole buffer is dropped rather than truncated") {
  Fixture f;
  f.put("0123456789abcdefg");  // 17 into 16
  CHECK(f.log.size() == 0);
  CHECK(f.log.dropped() == 17);
}

TEST_CASE("nothing may be written to the card while the setting is unknown") {
  Fixture f;
  f.put("0123456789abcdef");
  CHECK(f.log.size() == 16);
  // The card is not even mounted this early, so a flush could only fail -- and a
  // /encre.log written before the file that authorises it has been read would be the
  // feature acting on a setting it has not seen.
  CHECK_FALSE(f.log.wantsFlush(8));
  f.log.applySetting(true);
  CHECK(f.log.wantsFlush(8));
}

TEST_CASE("a flush is wanted only past the threshold") {
  Fixture f;
  f.log.applySetting(true);
  CHECK_FALSE(f.log.wantsFlush(8));
  f.put("01234567");
  CHECK(f.log.wantsFlush(8));
}

TEST_CASE("a disabled log never wants a flush") {
  Fixture f;
  f.put("0123456789abcdef");
  f.log.applySetting(false);
  CHECK_FALSE(f.log.wantsFlush(1));
}

TEST_CASE("a write that landed empties the buffer and loses nothing") {
  Fixture f;
  f.log.applySetting(true);
  f.put("01234567");
  f.log.wrote(true);
  CHECK(f.log.size() == 0);
  CHECK(f.log.dropped() == 0);
}

TEST_CASE("a write that failed empties the buffer and counts what it cost") {
  Fixture f;
  f.log.applySetting(true);
  f.put("01234567");
  f.log.wrote(false);
  // Dropped either way: a card that refuses the write must not make the buffer grow
  // until it starts losing lines silently, and a log that stops the device working is
  // worse than no log.
  CHECK(f.log.size() == 0);
  CHECK(f.log.dropped() == 8);
}

TEST_CASE("applying true twice does not discard the buffer") {
  Fixture f;
  f.log.applySetting(true);
  f.put("kept\n");
  // loadAndApplySettings() runs a SECOND time on the RETRY path, when a card that was
  // absent at boot has appeared. Re-arming must not throw away the log that has
  // accumulated since the first one.
  f.log.applySetting(true);
  CHECK(f.held() == "kept\n");
  CHECK(f.log.state() == CardLogBuffer::State::Enabled);
}

TEST_CASE("a card that says no turns an enabled log back off") {
  Fixture f;
  f.log.applySetting(true);
  f.put("kept\n");
  f.log.applySetting(false);
  CHECK(f.log.state() == CardLogBuffer::State::Disabled);
  CHECK(f.log.size() == 0);
}

TEST_CASE("an empty append is not a drop") {
  Fixture f;
  f.log.append("", 0);
  f.log.append(nullptr, 0);
  CHECK(f.log.size() == 0);
  CHECK(f.log.dropped() == 0);
}

// --- THE RESERVE, AND WHY IT HAD TO STOP BEING ONE-SHOT (#83) -----------------
//
// wantsFlush() is a TRIGGER and the room above it was being read as a RESERVE. The
// caller may only write in an idle window, so from the trigger onwards the free space
// only shrinks and nothing tops it up -- and a reader turning pages keeps a paint owed
// or a press queued continuously, so the stretch that room has to survive is bounded
// by the user and not by anything the firmware chooses. On glass (X3, 2026-09-07) 1024
// bytes of it were overrun by 407, and whole lines went.
//
// mustFlush() is the second question, and what it buys is that the reserve is restored
// every loop ITERATION instead of every idle window.

TEST_CASE("a buffer still inside its reserve does not force a flush") {
  Fixture f;
  f.log.applySetting(true);
  f.put("01234567");  // 8 of 16, so 8 free
  CHECK_FALSE(f.log.mustFlush(4));
  CHECK_FALSE(f.log.mustFlush(8));  // free == reserve: intact, not broken
}

TEST_CASE("a buffer whose reserve is broken forces a flush") {
  Fixture f;
  f.log.applySetting(true);
  f.put("012345678");  // 9 of 16, so 7 free
  CHECK(f.log.mustFlush(8));
}

TEST_CASE("the forced point is `fewer than the reserve free`, not `at or fewer`") {
  // The boundary is load-bearing: at free == reserve the buffer can still take a
  // maximal line, and forcing there would move the forced point a byte below where
  // the caller's static_assert believes it is.
  Fixture f;
  f.log.applySetting(true);
  f.put("01234567");  // free == 8
  CHECK_FALSE(f.log.mustFlush(8));
  f.put("x");  // free == 7
  CHECK(f.log.mustFlush(8));
}

TEST_CASE("a pending buffer never forces a flush, however full it is") {
  // Boot fills this before the card is mounted. Nothing may reach /encre.log before
  // the file authorising it has been read -- which is the whole of #47/#69 -- so the
  // last-resort write is arming-gated exactly as the ordinary one is.
  Fixture f;
  f.put("0123456789abcde");  // 15 of 16
  CHECK(f.log.state() == CardLogBuffer::State::Pending);
  CHECK_FALSE(f.log.mustFlush(8));
}

TEST_CASE("a disabled buffer never forces a flush") {
  // KEPT, AND IT DOES NOT PROVE WHAT IT LOOKS LIKE IT PROVES. A Disabled buffer is
  // always EMPTY -- applySetting(false) zeroes it and append() returns early -- so
  // this passes on the `len_ > 0` term and cannot reach `enabled()` at all. Deleting
  // the arming gate leaves it green; the Pending case above is the one that bites.
  // Stated rather than left implied, because a case that holds for a reason other
  // than the one its name gives is how a guard quietly stops being tested.
  Fixture f;
  f.put("0123456789abcde");
  f.log.applySetting(false);
  CHECK(f.log.size() == 0);
  CHECK_FALSE(f.log.mustFlush(8));
}

TEST_CASE("an empty buffer never forces a flush, even under a reserve it cannot hold") {
  // A reserve larger than the whole buffer would otherwise ask for a write of nothing
  // on every iteration for ever -- a [log] wrote 0B line per loop.
  Fixture f;
  f.log.applySetting(true);
  CHECK_FALSE(f.log.mustFlush(999));
  f.put("x");
  CHECK(f.log.mustFlush(999));
}

TEST_CASE("a forced write restores the reserve, so the next iteration starts with room") {
  Fixture f;
  f.log.applySetting(true);
  f.put("0123456789abcde");
  CHECK(f.log.mustFlush(8));
  f.log.wrote(true);
  CHECK_FALSE(f.log.mustFlush(8));
  CHECK(f.log.dropped() == 0);
}

namespace {

// The shell's real numbers, so this reproduces the device rather than a scale model:
// 4096 B of buffer, the ordinary trigger at 2048, the reserve at 1024. A plain reader
// page turn emits 417 B -- [i] 137 + [paint] 152 + [render] 91 + [page] 37, measured
// off the real format strings in shell/src/main.cpp at values from this project's own
// recorded runs.
constexpr size_t kBuf = 4096;
constexpr size_t kTrigger = 2048;
constexpr size_t kReserve = 1024;
constexpr size_t kPageTurnBytes = 417;

struct Loop {
  char storage[kBuf];
  CardLogBuffer log{storage, sizeof(storage)};
  std::string turn = std::string(kPageTurnBytes, 'x');
  int writes = 0;

  Loop() { log.applySetting(true); }

  // One iteration of loop(): the interaction's lines are teed, then the tail decides.
  // `quiet` is false throughout -- the reader is pressing, so a paint is owed or a
  // sample is queued on every iteration, which is the state #83 was measured in.
  void iterate(bool forcedGateBuilt) {
    log.append(turn.data(), turn.size());
    if (forcedGateBuilt && log.mustFlush(kReserve)) {
      log.wrote(true);
      ++writes;
    }
  }
};

}  // namespace

TEST_CASE("a reader who never lets the loop go quiet loses nothing") {
  // THE REGRESSION. Sixty page turns with `quiet` false for all of them: on the
  // shipped gate the buffer runs from the trigger to the ceiling and refuses whole
  // lines, and the log grows a hole in the one window a fault is most interesting.
  Loop withGate;
  for (int i = 0; i < 60; ++i) withGate.iterate(true);
  CHECK(withGate.log.dropped() == 0);

  // And it is bounded rather than merely rarer: a flush empties the buffer, so the
  // forced write cannot recur until the reserve has been eaten again. 3072 B of room
  // is 7.37 page turns, but a turn is indivisible, so it is the EIGHTH that crosses
  // and sixty turns buy 7 writes -- one per 8 turns, not one per turn. The bytes-wise
  // ratio rounds the wrong way here and the discrete count is the honest one.
  CHECK(withGate.writes == 7);

  Loop without;
  for (int i = 0; i < 60; ++i) without.iterate(false);
  CHECK(without.log.dropped() > 0);
  CHECK(without.writes == 0);
}

TEST_CASE("every iteration begins with room for the worst one this device emits") {
  // The reserve is derived from what ONE loop iteration can emit: 417 B for a plain
  // page turn, 559 for a chapter crossing, 725 for a crossing whose quiet-window jobs
  // also report. The bound the forced gate buys is that a drop needs more than
  // kReserve inside a single iteration -- so 725 must fit with room over.
  Loop l;
  const std::string worstIteration(725, 'y');
  for (int i = 0; i < 40; ++i) {
    CHECK(kBuf - l.log.size() >= kReserve);
    l.log.append(worstIteration.data(), worstIteration.size());
    if (l.log.mustFlush(kReserve)) l.log.wrote(true);
  }
  CHECK(l.log.dropped() == 0);
}

TEST_CASE("the trigger and the reserve are separate questions about the same buffer") {
  // wantsFlush() takes a FILL level and mustFlush() takes FREE space, which is the one
  // place these two could be read as one number. Between the trigger and the forced
  // point there is a window where the ordinary flush is wanted and the forced one is
  // not -- that window is what the idle gate still governs, and collapsing the two
  // constants closes it.
  Loop l;
  const std::string chunk(kTrigger, 'z');
  l.log.append(chunk.data(), chunk.size());
  CHECK(l.log.wantsFlush(kTrigger));
  CHECK_FALSE(l.log.mustFlush(kReserve));

  const std::string more(kBuf - kTrigger - kReserve, 'z');
  l.log.append(more.data(), more.size());
  CHECK(l.log.wantsFlush(kTrigger));
  CHECK_FALSE(l.log.mustFlush(kReserve));  // free == kReserve exactly: still intact
  l.log.append("!", 1);
  CHECK(l.log.mustFlush(kReserve));
}
