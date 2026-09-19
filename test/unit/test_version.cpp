#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cctype>
#include <string>

#include "doctest.h"
#include "reader/version.h"

// THE VERSION IS ONE LITERAL NOW, AND THIS FILE NO LONGER HOLDS A SECOND COPY
// OF IT (#152).
//
// It used to pin the string -- `CHECK(kVersion == "0.2.0")` -- and the comment
// above it explained that passing this test was not enough, because
// `design/Settings.dc.html` states the version too and `make compare` cannot
// see a stale one: the board carries its own copy, so when both are stale they
// agree exactly and the sheet measures a stale version against a stale version
// and reports Settings green. v0.2.0 shipped drawing `V 0.1.0` that way.
//
// A pin that has to be hand-bumped alongside the thing it pins is a second copy
// with a build failure attached, not a mechanism. The boards' version slot is
// GENERATED from `reader/version.h` now (`make version`), drift is refused by
// `make version-check` and by CI through `make compare
// COMPARE_ARGS=--require-version-current`, and there are exactly two hand edits
// in a release: the header, and the tag.
//
// WHAT IS LEFT HERE IS THE ONE THING THE FAST LOOP CAN STILL ANSWER ALONE, and
// it is load-bearing rather than tidy: the generator reads this literal with a
// regex, so a version it cannot parse takes the whole mechanism out at once --
// `versionc.py` refuses (exit 2) and every generated copy silently stops being
// regenerated. `make test` runs on a bare checkout with no Python, so this is
// the only check of that shape that always runs.
TEST_CASE("the version is a literal the generator can read") {
  const std::string v = reader::kVersion;
  REQUIRE_FALSE(v.empty());

  // `\d+\.\d+\.\d+`, which is versionc.py's SOURCE_RE spelled in C++. No `v`
  // prefix (the TAG has one, the literal does not -- `V 0.2.0` on the glass is
  // composed, see below), no whitespace, no suffix.
  int dots = 0;
  size_t run = 0;
  for (const char c : v) {
    if (c == '.') {
      CHECK(run > 0);  // no empty component, so no leading or doubled dot
      run = 0;
      ++dots;
    } else {
      CHECK(std::isdigit(static_cast<unsigned char>(c)));
      ++run;
    }
  }
  CHECK(dots == 2);
  CHECK(run > 0);  // ...and none trailing
}

// THE `V ` IS COMPOSED, NOT STORED, and the board's generated slot spells the
// composed form. So the two ends of the generator have to agree on the prefix:
// `versionc.py`'s BOARD_RE matches `V <n>` because that is what
// `SettingsScreen::syncVm` builds. Storing the prefix in the header instead
// would make every one of them unreadable to the regex at once.
TEST_CASE("the header states the number and not the way it is drawn") {
  const std::string v = reader::kVersion;
  CHECK(v.find('V') == std::string::npos);
  CHECK(v.find(' ') == std::string::npos);
}
