#pragma once
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/screen_settings.h"
#include "reader/screen_library.h"
#include "reader/book.h"
#include "reader/screen_reader.h"
#include "reader/viewmodel.h"

namespace reader {

// The demo content Phase 2B navigates. Real content arrives in Phase 2C from the
// SD card and the settings store; until then this is the single definition both
// the simulator and the shell build from, so a screenshot from the desktop is
// evidence about the device rather than about a second, similar-looking
// catalogue.
HomeViewModel demoHomeVm();

// Home's menu rows, in order, and the screen each one opens.
// design/LibraryScrolled.dc.html -- 24 books, so the rail has proportions.
std::vector<LibraryItem> demoLibraryScrolledItems();

// design/HomeEmpty.dc.html -- Home with nothing to continue.
HomeViewModel demoHomeEmptyVm();

std::vector<ScreenId> demoHomeTargets();

// design/Sleep.dc.html's own values -- see the definition.
SleepViewModel demoSleepVm();

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
  // most recent one. It is not a general-purpose handle.
  //
  // WHAT BOUNDS ITS LIFETIME, now that the shell has a long-lived factory: the
  // App owns the screen, so the pointer is valid exactly as long as the App that
  // built it. The shell REPLACES its App in three places (a successful retry, a
  // card lost at runtime, and the boot path itself), and each of them destroys
  // the Library this points at -- so each of them calls forgetLibrary(). That is
  // the whole of the relationship, and it is stated here because the pointer's
  // safety is not local to this class.
  LibraryScreen* library() const { return library_; }

  // The Library this factory last built is gone. Called by whoever destroyed the
  // App that owned it, BEFORE anything can ask for an overlay again.
  //
  // Without it a dangling pointer survives an App swap, and the overlay factory
  // cases below would hand an overlay a reference to freed memory. Today nothing
  // could reach them in that state -- the swap roots the new App at Home or at
  // the SD-missing screen, and an overlay is only pushed by a live Library -- but
  // "nothing can currently reach it" is a property of two other files, and the
  // session restore already pushes a screen with no press behind it.
  void forgetLibrary() { library_ = nullptr; }

  // How many rows a Library this factory builds should show, from
  // Theme::libraryVisibleRows. Held here because the factory is what constructs
  // the screen and the panel size is not something core/ can ask for; 0 means
  // "not told", and the Library then shows nothing rather than guessing.
  void setLibraryVisibleRows(int n) { libraryVisibleRows_ = n; }

  // Which demo books a card-less Library shows. Empty means demoLibraryItems() --
  // the board's own seven. Set it to render a state the default list cannot
  // produce, which today is the SCROLLED library: a rail's proportions come from
  // the list's length, so a seven-item list cannot show one.
  //
  // Ignored when the factory has a real filesystem; a card's contents are the
  // card's.
  void setLibraryItems(std::vector<LibraryItem> items) { libraryItems_ = std::move(items); }

  // What a Settings screen this factory builds starts from, and where its changes
  // go. Held here for the same reason the Library's row count is: the factory is
  // what constructs the screen, and neither the current settings nor a place to
  // write them is something `core/` can go and find.
  //
  // A null sink is the simulator and the golden tests -- see SettingsSink. The
  // metrics are Theme::settingsMetrics's three numbers; zero means "not told", and
  // the screen then shows nothing rather than guessing, exactly as the Library
  // does.
  void setSettings(const Settings& s) { settings_ = s; }
  void setSettingsSink(SettingsSink* sink) { settingsSink_ = sink; }
  void setSettingsMetrics(int listH, int rowH, int headerH) {
    settingsListH_ = listH;
    settingsRowH_ = rowH;
    settingsHeaderH_ = headerH;
  }

  // What a Reader this factory builds shows, and what it draws body text with.
  // Held here for the reason the Library's row count and Settings' sink are: the
  // factory constructs the screen, and neither the chapter nor a rasterised face
  // is something `core/` can go and find.
  //
  // With no body face set the factory REFUSES to build a Reader, rather than
  // building one that renders nothing: a screen with no text is
  // indistinguishable from a book that failed to open.
  void setReaderBody(const GlyphSource* body) { readerBody_ = body; }
  void setReaderMetrics(const PageMetrics& m) { readerMetrics_ = m; }
  // WHERE the chapter is, not the chapter itself -- a path and three numbers, which
  // is what openBook hands back and all a ChapterReader needs. An empty bookPath
  // means the demo content: design/Reader.dc.html's own two paragraphs, streamed
  // from memory, which is what the simulator and the goldens render, on the same
  // reasoning as demoSleepVm().
  // THE DEMO CHAPTER, EXPLICITLY. design/Reader.dc.html's own two paragraphs,
  // streamed from memory -- what the simulator and the goldens render, on the same
  // reasoning as demoSleepVm().
  //
  // It has to be ASKED FOR, and that is the whole point of it being its own setter.
  // It used to be the fallback for "no book set", which meant a session restore --
  // where nothing has called setReaderBook, because the shell only calls it from a
  // button press -- silently built a Reader full of Middlemarch. The device woke
  // from sleep showing fiction from a book the user was not reading. A factory that
  // substitutes content is worse than one that refuses.
  void setReaderDemo() { readerDemo_ = true; }

  // THE BOOK'S WHOLE GEOMETRY, from one openBook: its path, its metadata and twelve
  // bytes an entry. The reader reaches another chapter by picking a row out of it,
  // where it used to re-parse the archive per chapter.
  void setReaderBook(OpenedBook book, int startChapter) {
    readerBook_ = std::move(book);
    readerStartChapter_ = startChapter;
  }

 private:
  FileSystem* fs_ = nullptr;
  std::string root_;
  LibraryScreen* library_ = nullptr;
  int libraryVisibleRows_ = 0;
  std::vector<LibraryItem> libraryItems_;
  Settings settings_{};
  SettingsSink* settingsSink_ = nullptr;
  int settingsListH_ = 0;
  int settingsRowH_ = 0;
  int settingsHeaderH_ = 0;
  const GlyphSource* readerBody_ = nullptr;
  PageMetrics readerMetrics_{};
  OpenedBook readerBook_{};
  bool readerDemo_ = false;
  int readerStartChapter_ = 0;
  std::string readerBookTitle_;
  std::string readerChapter_;
};

}  // namespace reader
