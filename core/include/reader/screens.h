#pragma once
#include "reader/toc.h"
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/screen_settings.h"
#include "reader/screen_book_details.h"
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

// design/HomeUnopened.dc.html. Books on the card, none of them open -- the third
// Home state and the one the device is actually in until a reading position
// exists. Same mechanism as the empty variant (nothingToContinue), different copy,
// and a real count on the LIBRARY row rather than `EMPTY`: that count is the whole
// fact separating the two screens, because it is what tells the user the books are
// there and it is worth going to look.
HomeViewModel demoHomeUnopenedVm();

std::vector<ScreenId> demoHomeTargets();

// design/Sleep.dc.html's own values -- see the definition.
SleepViewModel demoSleepVm();
// design/Contents.dc.html's own list, for the simulator and the goldens.
std::vector<TocEntry> demoContents();
int demoContentsSpine();
// design/SleepIdle.dc.html: asleep with nothing open, so the badge without the card.
SleepViewModel demoSleepIdleVm();

// design/Library.dc.html's own seven rows, with the authors and percentages the
// board draws. The device fills the same fields from the card -- filenames, blank
// authors, NEW -- so this is what keeps `make compare` and the goldens testing
// the RENDERING while the data they show is still Phase 3's.
std::vector<LibraryItem> demoLibraryItems();

class DemoScreenFactory : public ScreenFactory, public LibraryWatcher {
 public:
  DemoScreenFactory() = default;
  // Over a card: the Library lists `root` through `fs`. Without one it lists the
  // board's sample content, which is what the simulator and the goldens want.
  DemoScreenFactory(FileSystem& fs, std::string root);
  // THE WATCHER LETS GO ON THE WAY OUT. The Library holds a pointer back here so
  // it can null library_ when it dies, which makes the factory dying first the
  // mirror of the dangle that pointer used to have -- and it is not hypothetical:
  // the shell declares gApp before gFactory, so at process exit the factory would
  // go first and the App's Library would notify freed memory. A device never
  // exits, which is precisely the reasoning that let the first hole stand.
  ~DemoScreenFactory() override { dropWatch(); }

  std::unique_ptr<Screen> create(ScreenId id) override;

  // The Library this factory built last, or null before it has built one -- and
  // null again the moment that Library is destroyed.
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
  // WHAT BOUNDS ITS LIFETIME: the Library itself. Every Library this factory
  // builds is registered with it (LibraryWatcher), so the screen's own destructor
  // nulls this -- whether it was popped off a live App, taken down with an App the
  // shell replaced, or simply dropped. There is nothing for a caller to remember
  // and no ordering to get right.
  //
  // IT USED TO BE A RULE IN THIS COMMENT, and the rule had a hole. It said the
  // pointer was valid "exactly as long as the App that built it" and named the
  // three places the shell replaces its App, each of which called forgetLibrary().
  // A POP was none of those: Home > Library > Back destroys the Library and keeps
  // the App, so the pointer dangled from then on. Nothing could reach it -- the
  // overlays are only pushed by a live Library and the shell's open path only asks
  // for one with a Library on top -- so nothing failed, which is what let it stand.
  // The mechanism is the screen's destructor now, and this paragraph is history
  // rather than instructions.
  LibraryScreen* library() const { return library_; }

  // LibraryWatcher: the Library this factory named is being destroyed.
  //
  // GUARDED ON WHICH ONE, and not defensively: a second Library can be built
  // before the first is destroyed (two on a stack, or two held side by side), and
  // clearing unconditionally would null a pointer to a live screen. That failure
  // would be reachable, where the dangle this replaces was not.
  void libraryGone(const LibraryScreen* which) override {
    if (library_ == which) library_ = nullptr;
  }

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
  // The italic face for emphasis, or null for "draw it roman". Set alongside the
  // body, because it belongs to `readerMetrics_` too and the WRAP reads it.
  void setReaderItalic(const GlyphSource* italic) { readerItalic_ = italic; }
  void setReaderMetrics(const PageMetrics& m) { readerMetrics_ = m; }
  // The column the Reader is laid out in. Read by a caller that has to record WHICH
  // geometry a saved line was measured at -- see ReadingPosition. One source of
  // truth: the screen was built from this same value.
  const PageMetrics& readerMetrics() const { return readerMetrics_; }
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
  // WHICH STYLED SPECIMEN, and asked for rather than inferred -- the rule setReaderDemo
  // established after the factory substituted the demo for a real book and hid a
  // failure to load its contents.
  enum class ReaderStyleDemo { None, ChapterOpen, List };
  void setReaderStyleDemo(ReaderStyleDemo which) { readerStyleDemo_ = which; }

  // Build the IDLE sleep screen -- asleep with no book open, design/SleepIdle.dc.html
  // -- rather than the board's reading one. Asked for, like setReaderDemo, because a
  // screen nothing navigates to has no state to infer it from: the simulator names
  // which of the two it wants, and the shell builds its own view model either way.
  void setSleepIdle() { sleepIdle_ = true; }

  // design/SleepWaking.dc.html -- the same badge, waking rather than asleep. It is a
  // NOTE and nothing else: SleepViewModel::note already carries that line's words, so
  // the waking state needed no field, no flag on the theme and no second render path.
  // The two boards differ by one run for the same reason.
  void setSleepWaking() { sleepWaking_ = true; }

  // THE BOARD'S OWN CONTENTS AND MENU HEADER, ASKED FOR. Same rule as setReaderDemo,
  // and it is here because the alternative had just shipped its consequence: the
  // factory fell back to a demo table of contents whenever nothing had set one, so a
  // failure to read the real one showed as MIDDLEMARCH'S CHAPTERS over Le Fleau. A
  // silent substitution turned a diagnosable failure into a puzzle.
  //
  // "A factory that substitutes content is worse than one that refuses" was already
  // written down for exactly this, one screen earlier.
  void setContentsDemo() { contentsDemo_ = true; }

  // THE AUTHOR FOR BOOK DETAILS, read by the shell from the one book that screen shows.
  //
  // It cannot come from the Library's scan: the author lives in the OPF, so learning it
  // per row means opening every book on the card -- ~100 ms each, ~20 s for a 203-book
  // library, on a screen that has to paint. Book details shows ONE book, so it is one
  // archive open on the press that opens it, and there is heap for it because no Reader
  // is on the stack.
  //
  // Empty leaves the row blank, which is what it has always drawn.
  void setDetailsAuthor(std::string author) { detailsAuthor_ = std::move(author); }

  // EVERYTHING BOOK DETAILS DRAWS, for the caller that has no Library row to point at.
  // The reader menu's `About this book` is that caller: it opens from a Reader, which may
  // have been reached through Home's CONTINUE with no Library on the stack. Clearing it
  // puts the screen back on the Library's row, which is what the simulator uses.
  void setDetailsFacts(BookDetailsScreen::Facts f) {
    detailsFacts_ = std::move(f);
    detailsFactsSet_ = true;
  }
  void clearDetailsFacts() { detailsFactsSet_ = false; }

  // THE BOOK'S TABLE OF CONTENTS, for the Contents screen. Set by the shell when the
  // menu's Contents row is chosen -- reading it is card work (`toc.h` re-opens the
  // archive) and `core/` does no storage, so the factory is handed the answer rather
  // than the question. Empty means the book has none, which Contents renders as an
  // empty list rather than refusing: a book with no NCX still reads.
  // How many Contents rows fit, from Theme::contentsVisibleRows. Held here for the
  // reason libraryVisibleRows is: the factory constructs the screen and a panel height
  // is not something `core/` can ask for. 0 means "not told", and the list renders
  // empty rather than guessing.
  void setContentsVisibleRows(int n) { contentsRows_ = n; }

  void setContents(std::vector<TocEntry> toc, int spine) {
    contentsToc_ = std::move(toc);
    contentsSpine_ = spine;
    // PRIMED IS ITS OWN FLAG, not "the list is non-empty". A real book with no NCX
    // primes an EMPTY list and must still build -- it reads fine and simply cannot name
    // its chapters. Inferring from emptiness collapses that into "nothing was primed",
    // which is a shell bug and is refused.
    contentsPrimed_ = true;
  }

  // What the reader menu's header says. Two strings rather than a reach down the stack
  // into the Reader: an overlay that read its parent would be a second place that
  // knows how a Reader is shaped.
  void setReaderMenuHeader(std::string bookTitle, std::string progress) {
    menuTitle_ = std::move(bookTitle);
    menuProgress_ = std::move(progress);
  }

  // THE BOOK'S WHOLE GEOMETRY, from one openBook: its path, its metadata and twelve
  // bytes an entry. The reader reaches another chapter by picking a row out of it,
  // where it used to re-parse the archive per chapter.
  // `startAt` is a RESTORED POSITION -- the cursor of the page the reader was on --
  // and Cursor{} means page one, which is both "no saved position" and "the top of
  // the chapter". The screen spends it on the first chapter it lands on.
  void setReaderBook(OpenedBook book, int startChapter, Cursor startAt = Cursor{}) {
    readerBook_ = std::move(book);
    readerStartChapter_ = startChapter;
    readerStartAt_ = startAt;
  }
  // The restored way back, or nothing. Separate from setReaderBook because it comes
  // from a DIFFERENT grade of the same record -- restoreFrom keeps the anchor only at
  // an Exact fit -- and folding it into the book call would invite a caller to pass
  // one the fit had already refused.
  void setReaderAnchor(const AnchorPos& a) {
    readerAnchor_ = a;
    readerHasAnchor_ = true;
  }
  void clearReaderAnchor() { readerHasAnchor_ = false; }

  // The open book's geometry, for a caller that needs to say something about the
  // book as a whole -- progressPercent sums its chapters' sizes. A reference rather
  // than a copy: this is 12 bytes a spine entry and the shell would otherwise keep a
  // third copy of it beside this one and openBook's.
  const OpenedBook& readerBook() const { return readerBook_; }

 private:
  // Break the link in the direction the SCREEN holds it: called when a newer
  // Library replaces the one library_ names, and from the destructor, so AT MOST
  // ONE live Library ever points back here -- which is what makes the destructor's
  // guarantee total rather than covering only the tracked one.
  //
  // Dereferencing library_ is safe for exactly the reason the pointer exists: it
  // is nulled when the screen dies. The two halves are each other's guarantee.
  void dropWatch() {
    if (library_ != nullptr) library_->stopWatching(*this);
  }

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
  const GlyphSource* readerItalic_ = nullptr;
  ReaderStyleDemo readerStyleDemo_ = ReaderStyleDemo::None;
  AnchorPos readerAnchor_{};
  bool readerHasAnchor_ = false;
  PageMetrics readerMetrics_{};
  OpenedBook readerBook_{};
  bool readerDemo_ = false;
  bool sleepIdle_ = false;
  bool sleepWaking_ = false;
  bool contentsDemo_ = false;
  std::string detailsAuthor_;
  BookDetailsScreen::Facts detailsFacts_{};
  bool detailsFactsSet_ = false;
  std::vector<TocEntry> contentsToc_;
  int contentsSpine_ = 0;
  bool contentsPrimed_ = false;
  int contentsRows_ = 0;
  std::string menuTitle_, menuProgress_;
  int readerStartChapter_ = 0;
  Cursor readerStartAt_{};
  std::string readerBookTitle_;
  std::string readerChapter_;
};

}  // namespace reader
