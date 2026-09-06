// THE WAKE RECORD'S WIRE FORMAT, which is a string and lives in core/ so that it
// can be tested at all -- shell/ has no test harness, and this is the one piece
// of the resume path that is pure logic.
//
// `home:-1;library:7;item-actions:1`, root first. Two things it inherits from the
// single-screen format it replaces: the screen is a NAME rather than an enum
// ordinal (2C-2 inserted three screens into the middle of ScreenId and silently
// renamed every stored record), and an unrecognised name is "no session" rather
// than a best-effort decode -- nothing here casts an integer into a ScreenId.
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/session_record.h"

using namespace reader;

TEST_CASE("a stack round-trips through the wire format") {
  const std::vector<StackEntry> in{
      {ScreenId::Home, -1}, {ScreenId::Library, 7}, {ScreenId::ItemActions, 1}};
  const std::string wire = encodeSessionStack(in);
  CHECK(wire == "home:-1;library:7;item-actions:1");

  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  CHECK(out == in);
}

TEST_CASE("a one-screen stack needs no separator") {
  CHECK(encodeSessionStack({{ScreenId::Home, 0}}) == "home:0");
  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack("home:0", out));
  REQUIRE(out.size() == 1);
  CHECK(out[0].screen == ScreenId::Home);
  CHECK(out[0].focus == 0);
}

TEST_CASE("-1 survives, because it is a position and not an error") {
  // Home's CONTINUE block and an empty /books both report -1. The record this
  // replaces stored the focus in an unsigned NVS key and flattened it to 0, which
  // woke the user on the first menu row instead of on CONTINUE.
  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack("home:-1", out));
  CHECK(out[0].focus == -1);
}

TEST_CASE("an unknown screen name rejects the WHOLE record, not just its entry") {
  // A name this build does not know comes from a firmware that does, so the
  // entries around it may not mean what they say either. Home is the answer.
  //
  // The example used to be "reader", which stopped being unknown the moment the
  // Reader screen landed -- and the test then asserted that a valid record was
  // refused, passing for the wrong reason right up until it failed. A name that
  // cannot become a screen is the only safe stand-in.
  std::vector<StackEntry> out{{ScreenId::Library, 3}};
  CHECK_FALSE(decodeSessionStack("home:0;not-a-screen:12", out));
  CHECK(out.empty());
}

TEST_CASE("malformed input is refused rather than half-read") {
  std::vector<StackEntry> out;
  CHECK_FALSE(decodeSessionStack("", out));
  CHECK_FALSE(decodeSessionStack(nullptr, out));
  CHECK_FALSE(decodeSessionStack("home", out));         // no focus
  CHECK_FALSE(decodeSessionStack("home:", out));        // no digits
  CHECK_FALSE(decodeSessionStack("home:x", out));       // not a number
  CHECK_FALSE(decodeSessionStack("home:0;", out));      // trailing separator
  CHECK_FALSE(decodeSessionStack(":0", out));           // no name
  CHECK_FALSE(decodeSessionStack("home:0;;library:1", out));
  CHECK_FALSE(decodeSessionStack("home:1-2", out));
}

TEST_CASE("a record deeper than the app's stack is refused") {
  // App::kMaxDepth is 8 and a push past it fails, so a longer record could only
  // ever be half-restored. Refusing it whole keeps "the record was usable" a
  // single yes-or-no.
  std::string wire = "home:0";
  for (int i = 0; i < 8; ++i) wire += ";library:1";
  std::vector<StackEntry> out;
  CHECK_FALSE(decodeSessionStack(wire.c_str(), out));
}

TEST_CASE("the focus is clamped on the way out, so the string has a bound") {
  // The clamp used to be in the shell, where it existed because the NVS key was a
  // uint16. The key is a string now and the reason changed with it: a focus is a
  // row index, and an unbounded one would make the record's length unbounded too.
  CHECK(encodeSessionStack({{ScreenId::Home, 999999}}) == "home:32767");
  CHECK(encodeSessionStack({{ScreenId::Home, -999999}}) == "home:-1");
}

TEST_CASE("every screen in the catalogue has a wire name, and they are all distinct") {
  // A screen with no name cannot be stored, which is a defined outcome -- but it
  // must be a deliberate one. This is the check that makes forgetting a row show
  // up here rather than as a screen that quietly never restores.
  std::vector<std::string> names;
  for (int i = 0; i <= static_cast<int>(ScreenId::BookEnd); ++i) {
    const ScreenId id = static_cast<ScreenId>(i);
    const char* n = sessionWireName(id);
    REQUIRE(n != nullptr);
    CHECK(std::string(n).find(':') == std::string::npos);
    CHECK(std::string(n).find(';') == std::string::npos);
    names.push_back(n);
  }
  for (size_t a = 0; a < names.size(); ++a)
    for (size_t b = a + 1; b < names.size(); ++b) CHECK(names[a] != names[b]);
}

TEST_CASE("Peek round-trips through the record's screen NAME") {
  // A NAME AND NOT AN ORDINAL, which is why appending a ScreenId is safe at all:
  // 2C-2 inserted three screens into the middle of the enum and a stored ordinal
  // silently became a different screen.
  //
  // A PEEK IS NEVER RESTORED IN PRACTICE -- the factory refuses an unprimed one, so
  // App::restore stops short and leaves the Reader standing, which is the existing
  // "a restore that stops early keeps what already stands" behaviour. It still has to
  // round-trip, because a record naming a screen this build cannot MAP is a different
  // failure from one naming a screen it cannot BUILD, and only the second is intended.
  const std::vector<StackEntry> in{
      {ScreenId::Home, -1}, {ScreenId::Reader, 0}, {ScreenId::Peek, 0}};
  const std::string wire = encodeSessionStack(in);
  CHECK(wire.find("peek") != std::string::npos);

  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  REQUIRE(out.size() == in.size());
  CHECK(out[2].screen == ScreenId::Peek);
}

TEST_CASE("every ScreenId round-trips to ITSELF") {
  // WALKS TO THE ENUM'S LAST MEMBER, NOT TO A NAMED ONE. The walk above this one
  // said `<= ScreenId::Peek`, so appending BookEnd left it silently covering
  // twelve of thirteen screens -- while sessionWireName's `return kNames[0]`
  // fallthrough stored the thirteenth as `home`. A reader who idle-slept on the
  // end-of-book screen would have woken on Home, which is exactly the failure
  // sessionWireName's own comment says the name table exists to prevent.
  //
  // DISTINCTNESS IS NOT ASSERTED HERE, and deliberately: the case above already
  // asserts it across the same walk, and a round-trip catches a collision anyway --
  // a shared name decodes to the LOWER id, so at most one of the two can come back
  // as itself. That is what made a second copy of the collision loop a second copy
  // rather than a second check.
  for (int i = 0; i <= static_cast<int>(ScreenId::BookEnd); ++i) {
    const ScreenId id = static_cast<ScreenId>(i);
    const char* n = sessionWireName(id);
    REQUIRE(n != nullptr);

    const std::string wire = encodeSessionStack({{id, 0}});
    std::vector<StackEntry> out;
    REQUIRE(decodeSessionStack(wire.c_str(), out));
    REQUIRE(out.size() == 1);
    INFO("id " << i << " wire name '" << std::string(n) << "'");
    CHECK(out[0].screen == id);
  }
}

// EVERY SCREEN, NOT EVERY SCREEN SOMEBODY REMEMBERED. session_record.cpp's table and
// switch have twice been left short by an append -- Typography, then BookEnd -- and
// each time the new screen fell through to `return kNames[0]` and serialised as
// `home`, so a reader idle-sleeping on it woke on Home with no failing test and no log
// line. A static_assert on the table's LENGTH cannot see that, because the bound it
// compares against is a hand-named member that the append does not move.
//
// This walks the enum by ORDINAL up to the Count sentinel, so it cannot be left short.
TEST_CASE("every ScreenId has its own wire name") {
  std::set<std::string> seen;
  for (int i = 0; i < static_cast<int>(reader::ScreenId::Count); ++i) {
    const reader::ScreenId id = static_cast<reader::ScreenId>(i);
    const char* n = reader::sessionWireName(id);
    REQUIRE(n != nullptr);
    CAPTURE(i);
    CAPTURE(n);
    // Distinct: a screen that fell through to kNames[0] collides with Home, and a
    // collision is exactly what the fall-through produces.
    CHECK(seen.insert(n).second);
  }
  CHECK(seen.size() == static_cast<size_t>(reader::ScreenId::Count));
}
