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
