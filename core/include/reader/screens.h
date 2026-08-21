#pragma once
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/screen_library.h"
#include "reader/viewmodel.h"

namespace reader {

// The demo content Phase 2B navigates. Real content arrives in Phase 2C from the
// SD card and the settings store; until then this is the single definition both
// the simulator and the shell build from, so a screenshot from the desktop is
// evidence about the device rather than about a second, similar-looking
// catalogue.
HomeViewModel demoHomeVm();

// Home's menu rows, in order, and the screen each one opens.
std::vector<ScreenId> demoHomeTargets();

// design/Library.dc.html's own seven rows, with the authors and percentages the
// board draws. The device fills the same fields from the card -- filenames, blank
// authors, NEW -- so this is what keeps `make compare` and the goldens testing
// the RENDERING while the data they show is still Phase 3's.
std::vector<LibraryItem> demoLibraryItems();

class DemoScreenFactory : public ScreenFactory {
 public:
  DemoScreenFactory() = default;
  // Over a card: the Library lists `root` through `fs`. Without one it lists the
  // board's sample content, which is what the simulator and the goldens want.
  DemoScreenFactory(FileSystem& fs, std::string root);

  std::unique_ptr<Screen> create(ScreenId id) override;

  // The Library this factory built last, or null before it has built one.
  //
  // An overlay acts on the focused row of the screen UNDER it, and it reads that
  // through this pointer rather than being handed a copy of the selection: the
  // actions overlay and the delete confirmation behind it have to agree about
  // which book they mean even across a rescan, and two copies of an index
  // cannot. The App owns the screen; this only observes it.
  //
  // The pointer is overwritten on every Library this factory builds, so it names
  // the one on the stack: an overlay is only ever created BY a live Library
  // (through Action::push from its own onEvent), which is by construction the
  // most recent one. It is not a general-purpose handle, and Phase 2C-2's Task 6
  // is what replaces this with the shell's real catalogue.
  LibraryScreen* library() const { return library_; }

  // How many rows a Library this factory builds should show, from
  // Theme::libraryVisibleRows. Held here because the factory is what constructs
  // the screen and the panel size is not something core/ can ask for; 0 means
  // "not told", and the Library then shows nothing rather than guessing.
  void setLibraryVisibleRows(int n) { libraryVisibleRows_ = n; }

 private:
  FileSystem* fs_ = nullptr;
  std::string root_;
  LibraryScreen* library_ = nullptr;
  int libraryVisibleRows_ = 0;
};

}  // namespace reader
