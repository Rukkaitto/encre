// THE LINK IS THE POINT OF THIS FILE, AND IT ASSERTS ALMOST NOTHING ELSE.
//
// PR2 of #179's deliverable is that shell/src/main.cpp compiles and links on the
// desktop -- 9,331 lines that no test could reach, and where five recorded defects
// have hidden, including a deleted `gApp->dispatch(ev)` that made every button on
// every screen dead while 803 tests passed.
//
// Driving setup() and loop() is the NEXT step. Calling setup() here would bring up
// a panel, mount a card and build an App with no scenario behind it, and whatever
// it did would be a behaviour nobody chose. What this proves is that the symbols
// resolve -- which is exactly the thing that was impossible yesterday.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "harness_state.h"

// main.cpp's only two non-static entry points. Declared rather than included:
// there is no header for them, which is the shape Arduino imposes.
void setup();
void loop();

TEST_CASE("shell/src/main.cpp links on the desktop") {
  harness::resetAll();
  // Taking their addresses is what forces the linker to resolve them. Calling them
  // is phase 2 of the epic.
  void (*s)() = &setup;
  void (*l)() = &loop;
  CHECK(s != nullptr);
  CHECK(l != nullptr);
}
