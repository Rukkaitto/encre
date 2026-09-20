// THE WAKE RECORD'S WIRE FORMAT, which is a string and lives in core/ so that it
// can be tested at all -- shell/ has no test harness, and this is the one piece
// of the resume path that is pure logic.
//
// `home:-1;library:7:/books;item-actions:1`, root first. Two things it inherits
// from the single-screen format it replaces: the screen is a NAME rather than an
// enum ordinal (2C-2 inserted three screens into the middle of ScreenId and
// silently renamed every stored record), and an unrecognised name is "no session"
// rather than a best-effort decode -- nothing here casts an integer into a
// ScreenId.
//
// AN ENTRY'S THIRD FIELD IS OPTIONAL AND IS Screen::place() -- what that entry's
// focus is an index into (#14). The cases for it are at the foot of this file.
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
  //
  // NAMES THE Count SENTINEL, NOT A MEMBER. This walk said `<= ScreenId::Peek`,
  // then `<= ScreenId::BookEnd`, then `<= ScreenId::BatteryEmpty` -- each append
  // left it one screen short and nothing said so, which is #42. A bound one past
  // the last member cannot be left behind by an append.
  std::vector<std::string> names;
  for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i) {
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
  //
  // AND IT NAMES THE Count SENTINEL NOW, not a member. Both walks in this file were
  // spelled by hand and both went quiet on every append -- Peek, then BookEnd, then
  // BatteryEmpty. That was #42, and the sentinel is its general fix: a bound one past
  // the last member cannot be satisfied unchanged by adding a screen.
  for (int i = 0; i < static_cast<int>(ScreenId::Count); ++i) {
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

// --- THE FOLDER AN ENTRY'S FOCUS IS AN INDEX INTO (#14) ----------------------
//
// The Library can be listing a SUBFOLDER of /books, and the record could not say
// which -- so sleeping in /books/Classics on row 3 woke on /books row 3. A wrong
// row that looks right is worse than no row, so the list has to be on the wire
// beside the index into it.
//
// EVERY CASE HERE IS ABOUT A STRING A REAL CARD CAN PRODUCE. A wire format that a
// legal filename can break is not a fix, and `;` and `%` are both legal in a FAT
// long name.

TEST_CASE("a place rides beside the focus and round-trips") {
  const std::vector<StackEntry> in{
      {ScreenId::Home, -1, ""}, {ScreenId::Library, 3, "/books/Classics"}};
  const std::string wire = encodeSessionStack(in);
  // The place is the entry's THIRD field, and `/` is deliberately not escaped:
  // `nvs_get encre_sess stack str` printing a readable path is the same property
  // that made the screen a name rather than an ordinal.
  CHECK(wire == "home:-1;library:3:/books/Classics");

  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  CHECK(out == in);
  CHECK(out[1].place == "/books/Classics");
}

TEST_CASE("a screen with no place writes no third field") {
  // Which is every screen but the Library, so the overwhelming majority of
  // entries are byte-for-byte the two-field form this format replaces -- and a
  // two-field entry still parses, which is what makes the version bump a decision
  // rather than a necessity. (It is still made: see session.cpp.)
  CHECK(encodeSessionStack({{ScreenId::Settings, 4, ""}}) == "settings:4");
  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack("home:-1;library:7", out));
  REQUIRE(out.size() == 2);
  CHECK(out[1].focus == 7);
  CHECK(out[1].place.empty());
}

TEST_CASE("a folder name containing the format's own separators survives it") {
  // `;` and `%` are legal in a FAT long name; `:` is not, but nothing here knows
  // which filesystem wrote the path, and a raw one would silently become a fourth
  // field. All three are escaped, so the parser cannot be fooled by a filename.
  const std::string nasty = "/books/A; B%C:D";
  const std::string wire = encodeSessionStack({{ScreenId::Library, 2, nasty}});
  CHECK(wire == "library:2:/books/A%3B B%25C%3AD");
  CHECK(wire.find(';') == std::string::npos);

  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  REQUIRE(out.size() == 1);  // one entry, not two -- the `;` did not split it
  CHECK(out[0].place == nasty);
}

TEST_CASE("an accented 90-character folder name is carried whole and unescaped") {
  // A real card carries these. UTF-8 passes through byte for byte, because
  // escaping it would triple the record for no gain and cost the one property the
  // format is readable for.
  std::string deep = "/books/Le Fl\xC3\xA9" "au";
  while (deep.size() < 90) deep += "\xC3\xA9";  // e-acute, two bytes each
  const std::string wire = encodeSessionStack({{ScreenId::Library, 11, deep}});
  CHECK(wire.find("Fl\xC3\xA9" "au") != std::string::npos);
  CHECK(wire.find('%') == std::string::npos);  // nothing here needed escaping

  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  CHECK(out[0].place == deep);
  CHECK(out[0].focus == 11);
}

TEST_CASE("a control byte in a name is escaped rather than put in a log line") {
  const std::string sneaky = "/books/a\nb";
  const std::string wire = encodeSessionStack({{ScreenId::Library, 0, sneaky}});
  CHECK(wire == "library:0:/books/a%0Ab");
  std::vector<StackEntry> out;
  REQUIRE(decodeSessionStack(wire.c_str(), out));
  CHECK(out[0].place == sneaky);
}

TEST_CASE("a malformed place refuses the WHOLE record") {
  // The unknown-name rule: a place this cannot decode was written by something
  // that is not this format, so the entries around it may not mean what they say
  // either. Home is the answer.
  std::vector<StackEntry> out;
  CHECK_FALSE(decodeSessionStack("library:3:/books/%", out));     // truncated escape
  CHECK_FALSE(decodeSessionStack("library:3:/books/%2", out));    // half an escape
  CHECK_FALSE(decodeSessionStack("library:3:/books/%ZZ", out));   // not hex
  CHECK_FALSE(decodeSessionStack("library:3:/books/%00x", out));  // a NUL cannot be a path
  CHECK_FALSE(decodeSessionStack("library:3:", out));             // a field with nothing in it
  // A FOURTH FIELD, which there is no such thing as: every `:` a place contains
  // is escaped, so a raw one is malformed rather than ambiguous.
  CHECK_FALSE(decodeSessionStack("library:3:/books:extra", out));
  CHECK(out.empty());
}

TEST_CASE("the focus is refused before the place is even looked at") {
  // The focus span ends at the SECOND colon, so a junk focus is still junk when
  // an entry has three fields -- it is not swallowed into the place.
  std::vector<StackEntry> out;
  CHECK_FALSE(decodeSessionStack("library:x:/books", out));
  CHECK_FALSE(decodeSessionStack("library::/books", out));
}

TEST_CASE("a place too long to hold is dropped WHOLE, and takes its row with it") {
  // Truncating it would address a DIFFERENT directory rather than none, which is
  // the reasoning Xml::kMaxAttrBytes reached from the other side. And the row
  // goes with it: a row index without the folder it indexes is the whole of #14,
  // so the two cannot be dropped separately. -1 is not a marker -- it is
  // "nothing selected", which clamps to the top of whatever list is built.
  const std::string tooDeep = "/books/" + std::string(200, 'a');
  CHECK(encodeSessionStack({{ScreenId::Library, 3, tooDeep}}) == "library:-1");

  // AND THE BOUND IS ON THE ESCAPED FORM, so it holds whatever the path contains:
  // 60 semicolons are 60 bytes of filename and 180 bytes of wire.
  const std::string spiky = "/books/" + std::string(60, ';');
  CHECK(encodeSessionStack({{ScreenId::Library, 3, spiky}}) == "library:-1");

  // A path at the cap is still carried, so the drop is a real ceiling rather than
  // a refusal of anything interesting: 121 characters of folder inside /books.
  const std::string atCap = "/books/" + std::string(121, 'a');
  CHECK(encodeSessionStack({{ScreenId::Library, 3, atCap}}) == "library:3:" + atCap);
}

TEST_CASE("the read buffer the format derives holds the longest record it can write") {
  // shell/src/session.cpp sizes its NVS read from sessionStackMaxBytes() rather
  // than from a number kept in step by hand, and the place is the term that made
  // that matter -- it is larger than the rest of an entry put together. A record
  // that did not fit would read back as 0 bytes and be refused as "no session",
  // which is a silent lost wake.
  std::vector<StackEntry> worst;
  for (size_t i = 0; i < App::kMaxDepth; ++i)
    worst.push_back({ScreenId::DeleteConfirm, 32767, "/" + std::string(127, 'a')});
  const std::string wire = encodeSessionStack(worst);
  CHECK(wire.size() + 1 <= sessionStackMaxBytes());
}

TEST_CASE("a screen with no name refuses the whole record, where it used to be `home`") {
  // THE ONE BEHAVIOUR THE EVERY-ID WALKS ABOVE CANNOT REACH. Both of them stop
  // at `i < Count`, which is right -- the sentinel is not a screen -- and it
  // left the out-of-range answer pinned by nothing at all. sessionWireName used
  // to be a 29-case switch ending in `return kNames[0]`, so an id with no case
  // serialised as `home`; that is #42, and it shipped twice, to Typography and
  // then BookEnd.
  //
  // It is an index now, so the case cannot go missing -- but SOMETHING still has
  // to say what an id past the table means, because a newer firmware's record
  // can carry one and the sentinel itself is reachable by a cast.
  CHECK(std::string(sessionWireName(ScreenId::Count)).empty());

  // AND THE EMPTY NAME IS NOT MERELY DIFFERENT, IT IS REFUSED. encodeSessionStack
  // writes the field anyway, so the record reads `:0` -- and decodeName rejects a
  // zero-length name, which refuses the record. That is the whole point of the
  // change: `home` was a wake landing on a screen nobody asked for, and a record
  // that will not decode is a cold start, which is the honest failure.
  const std::string alone = encodeSessionStack({{ScreenId::Count, 0}});
  CHECK(alone == ":0");
  std::vector<StackEntry> out;
  CHECK_FALSE(decodeSessionStack(alone.c_str(), out));

  // THE WHOLE RECORD, not the bad entry. A valid screen in front of it does not
  // rescue the rest -- the file's unknown-name rule is that a record containing
  // something this build cannot read was written by something that is not this
  // format, so the entries around it may not mean what they say either.
  const std::string mixed = encodeSessionStack({{ScreenId::Home, 0}, {ScreenId::Count, 0}});
  CHECK(mixed == "home:0;:0");
  std::vector<StackEntry> out2;
  CHECK_FALSE(decodeSessionStack(mixed.c_str(), out2));
  CHECK(out2.empty());
}
