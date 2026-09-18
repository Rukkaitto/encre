#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "reader/version.h"

// THE VERSION IS THREE HARDCODED COPIES AND THIS IS THE ONLY ONE THAT FAILS
// LOUDLY. `reader/version.h` is what Settings' header band draws, and
// `design/Settings.dc.html` states the same string so `make compare` has
// something to compare against -- which means the two agree while BOTH are
// stale, and the sheet stays green. v0.2.0 shipped saying `V 0.1.0` that way.
//
// So this pin is the reminder: bumping the header fails here, and the fix is to
// bump the board too. docs/releasing.md's gate names all three.
TEST_CASE("core reports its version") {
  CHECK(std::string(reader::kVersion) == "0.2.0");
}
