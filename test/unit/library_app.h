#pragma once
// The Library, reached the way the simulator reaches it.
//
// Four golden tests need the same thing: an App rooted at Home, a factory told
// how many rows this panel fits, and the presses that open the Library and land
// its focus where the board's focus is. Written once here because a second copy
// would be a second answer to "how do you get to this screen", and the goldens'
// whole value is that they pin the journey as well as the pixels -- the same
// reason test_app_golden.cpp reaches Home's focused row by pressing Down rather
// than by assigning an index.
//
// It is a struct with public members rather than a wrapper with accessors: a
// test wants the App itself, and hiding it behind forwarding methods would only
// mean adding one every time a test needs something new.
#include <memory>
#include <vector>

#include "doctest.h"
#include "home_vm.h"
#include "reader/app.h"
#include "reader/screen_home.h"
#include "reader/screen_library.h"
#include "reader/screens.h"
#include "reader/theme.h"

namespace libapp {

struct LibraryApp {
  reader::DemoScreenFactory factory;
  reader::App app;

  // `downs` is how many rows down from the top the focus should end up, which is
  // the one thing the four boards disagree about: Library.dc.html focuses its
  // second row and the three overlay boards focus its sixth.
  LibraryApp(const reader::Theme& theme, const reader::FontSet& fonts, int panelH, int downs = 1)
      : app(std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
            factory) {
    // The theme owns the box model, and this is the caller that knows the panel,
    // so the row count is asked for once and carried into every Library the
    // factory builds. Without it the window has no height and the list is empty.
    factory.setLibraryVisibleRows(theme.libraryVisibleRows(panelH, fonts));
    // Home's focus starts on the CONTINUE block, so one Down reaches its LIBRARY
    // row and a Confirm opens it.
    app.dispatch({reader::Button::Down, reader::PressKind::Short});
    app.dispatch({reader::Button::Confirm, reader::PressKind::Short});
    REQUIRE(app.top().id() == reader::ScreenId::Library);
    for (int i = 0; i < downs; ++i)
      app.dispatch({reader::Button::Down, reader::PressKind::Short});
  }

  reader::LibraryScreen& library() {
    reader::LibraryScreen* lib = factory.library();
    REQUIRE(lib != nullptr);
    return *lib;
  }
};

}  // namespace libapp
