#include "reader/screens.h"

#include "reader/screen_contents.h"
#include "reader/screen_reader_menu.h"
#include "reader/screen_sleep.h"

#include "reader/screen_battery_empty.h"
#include "reader/screen_book_details.h"
#include "reader/screen_delete_confirm.h"
#include "reader/screen_item_actions.h"
#include "reader/screen_library.h"
#include "reader/screen_sd_missing.h"

namespace reader {

HomeViewModel demoHomeVm() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  // THE BOARD'S OWN COUNTER, spine position of spine count -- not a chapter name,
  // which would need a table of contents. See design/Main.dc.html.
  vm.chapterLabel = "CH. 01 OF 24";
  vm.percent = 6;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  // Home binds no long press, so no slot shows a ring.
  vm.holds = {false, false, false, false};
  return vm;
}

// design/HomeEmpty.dc.html. The same screen with nothing to continue, so the
// reading column is replaced -- see HomeViewModel::nothingToContinue.
HomeViewModel demoHomeEmptyVm() {
  HomeViewModel vm;
  vm.batteryPercent = 87;
  vm.nothingToContinue = true;
  vm.emptyTitle = "NO BOOKS YET";
  vm.emptyBody = "Put the SD card in your computer and copy EPUB files into its /books folder.";
  // LIBRARY says EMPTY where Home says a count -- the value is what the state
  // changes, and the row is otherwise Home's row.
  vm.menu = {{"LIBRARY", "EMPTY"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = 0;
  // NO READ HINT: slot 0 is empty, because there is nothing to read. The bar keeps
  // its four slots and the empty one keeps its 36px -- measuring it as nothing
  // would move every other slot along.
  vm.hints = {"", "SELECT", "UP", "DOWN"};
  vm.holds = {false, false, false, false};
  return vm;
}

// design/HomeUnopened.dc.html. Home with books on the card and nothing open.
//
// It is demoHomeEmptyVm's SHAPE with different words, and deliberately so: the
// reading column and the CONTINUE slab both answer "where were you", and there is
// no answer in either state. What differs is the sentence -- HomeEmpty has to
// explain something the device cannot do for you, this only has to point at the row
// below it -- and the LIBRARY value, which is a count here rather than `EMPTY`.
//
// THE COUNT IS NOT SET HERE. `12` is the board's number and the shell overwrites
// row 0's value with the card's real one, exactly as it does for demoHomeVm. It is
// authored anyway so the simulator and the comparison sheet render the board.
HomeViewModel demoHomeUnopenedVm() {
  HomeViewModel vm;
  vm.batteryPercent = 87;
  vm.nothingToContinue = true;
  vm.emptyTitle = "NOTHING OPEN YET";
  vm.emptyBody = "Choose a book from your library to start reading.";
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  // The first menu row, not the CONTINUE block: there is no block, and it is also
  // the row this screen's sentence is telling the user to press.
  vm.focusedMenuIndex = 0;
  // NO READ HINT, for demoHomeEmptyVm's reason -- nothing is open, so there is
  // nothing to resume. The empty slot keeps its 36px.
  vm.hints = {"", "SELECT", "UP", "DOWN"};
  vm.holds = {false, false, false, false};
  return vm;
}

// design/Contents.dc.html's own list: two sections over eight chapters, with the
// reader on the first. Depths, not indentation -- a depth-1 entry is a section header
// and the rest are rows, which is how toc.h reports a real NCX (see its header: one of
// four measured books is three levels deep and two are flat).
//
// A screen the simulator and the goldens must render needs a source for its values,
// exactly as demoSleepVm and demoHomeVm do.
std::vector<TocEntry> demoContents() {
  return {
      {0, 1, "BOOK I \xC2\xB7 MISS BROOKE"}, {0, 2, "I \xC2\xB7 Miss Brooke"},
      {1, 2, "II \xC2\xB7 Sir James courts"}, {2, 2, "III \xC2\xB7 The engagement"},
      {3, 2, "IV \xC2\xB7 Celia\xE2\x80\x99s doubts"},
      {4, 2, "V \xC2\xB7 Mr. Casaubon writes"}, {5, 2, "VI \xC2\xB7 Mrs. Cadwallader"},
      {6, 1, "BOOK II \xC2\xB7 OLD AND YOUNG"}, {6, 2, "VII \xC2\xB7 Rome"},
      {7, 2, "VIII \xC2\xB7 Will Ladislaw"},
  };
}

// The board marks its FIRST chapter row `NOW`, so the demo reader is on spine 0.
int demoContentsSpine() { return 0; }

std::vector<ScreenId> demoHomeTargets() { return {ScreenId::Library, ScreenId::Settings}; }

// The board's `&middot;`, spaces included. A third copy of this two-byte string
// (screen_library.cpp and components.cpp have the others) and deliberately not
// shared: it is a punctuation choice each board makes, not a constant, and the day
// one board wants an en dash a shared one would have to be un-shared.
const char* const kDot = " \xC2\xB7 ";

// design/Sleep.dc.html's own values. A demo view model rather than a real one for
// the same reason demoHomeVm is: `core/` has no book to read, and a board is a
// statement about layout that a render has to be able to reproduce exactly.
SleepViewModel demoSleepVm() {
  SleepViewModel vm;
  vm.label = "NOW READING";
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.progressPercent = 6;
  vm.progress = std::string("6%") + kDot + "CH. 01";
  vm.note = std::string("ASLEEP") + kDot + "HOLD POWER TO WAKE";
  return vm;
}

// design/Reader.dc.html's own two paragraphs, verbatim, on the same reasoning as
// design/SleepIdle.dc.html. Asleep with no book open -- the badge and nothing else.
//
// The note is the ONLY field set, and that is the shape of the state rather than an
// omission: every other field on this view model describes a book.
SleepViewModel demoSleepIdleVm() {
  SleepViewModel vm;
  vm.nothingToContinue = true;
  vm.note = std::string("ASLEEP") + kDot + "HOLD POWER TO WAKE";
  return vm;
}

// design/BookEnd.dc.html's own book.
BookEndScreen::Facts demoBookEndFacts() {
  BookEndScreen::Facts f;
  f.bookTitle = "Middlemarch";
  f.author = "George Eliot";
  // design/Main.dc.html says `CH. 01 OF 24` for this same demo book. One demo book,
  // one count -- two boards drawing it must agree, and a merge once left
  // ReaderMenu.dc.html disagreeing with Reader.dc.html while `make compare` said ok.
  f.chapterCount = 24;
  // The board draws BACK TO LIBRARY, which is the common case: CLAUDE.md calls the
  // Library "the commonest way to open a book".
  f.libraryBeneath = true;
  return f;
}

// design/BookError.dc.html's own book. `dubliners.epub` is the file its paragraph
// names, and Dubliners is the row design/Library.dc.html draws focused -- the board
// stacks this dialog over that list, so the two agree by construction.
BookErrorScreen::Facts demoBookErrorFacts() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Damaged,
          ScreenId::Library};
}

// design/BookErrorUnreadable.dc.html: the same file, the other refusal.
BookErrorScreen::Facts demoBookErrorUnreadableFacts() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::Unreadable,
          ScreenId::Library};
}

// design/BookErrorMemory.dc.html: the same file, the third refusal -- a book that is
// fine and did not fit.
BookErrorScreen::Facts demoBookErrorMemoryFacts() {
  return {"/books/dubliners.epub", "dubliners.epub", BookErrorReason::OutOfMemory,
          ScreenId::Library};
}

// demoSleepVm: a screen the simulator and the goldens must render needs a source
// for its content, and the board's copy is the one source that makes the
// comparison sheet meaningful.
//
// AS XHTML, not as blocks. It used to build Blocks directly, which skipped the
// document builder entirely -- so the demo exercised the renderer and nothing
// below it. A few hundred bytes of markup runs the same tokenizer and the same
// block builder a real chapter does, which is what makes the golden mean
// something about the reader rather than only about the theme.
std::string_view demoReaderXhtml() {
  return
      "<html><body>"
      "<p>Miss Brooke had that kind of beauty which seems to be thrown into relief by "
      "poor dress. Her hand and wrist were so finely formed that she could wear "
      "sleeves not less bare of style than those in which the Blessed Virgin "
      "appeared to Italian painters.</p>"
      "<p>Her sister Celia wore a necklace, and the two of them had that air of being "
      "dressed alike which is never quite an accident. It was the kind of morning "
      "that makes a plain room look deliberate.</p>"
      "</body></html>";
}

// design/Peek.dc.html's own peeked text -- Middlemarch's opening, which is the board's
// story: the reader is at CH. 07, 34%, has met a name they cannot place, and has peeked
// back to CH. 01, 4%, to read the sentence that introduced her.
//
// THE BOARD'S TWO SENTENCES AND NOT demoReaderXhtml'S, although the first sentence is
// shared: the peek's panel is eight line boxes, and the reading board's second
// paragraph would page straight off the bottom of it. The board states exactly what
// fits, which is what makes `make compare` a measurement of the panel rather than of
// where a longer specimen happened to break.
std::string demoPeekXhtml() {
  return
      "<html><body>"
      "<p>Miss Brooke had that kind of beauty which seems to be thrown into relief by "
      "poor dress. Her hand and wrist were so finely formed that she could wear "
      "sleeves not less bare of style.</p>"
      "</body></html>";
}

// THE STYLED BOARDS' OWN CONTENT, so `make compare` measures the STYLING rather than
// the difference between two sets of sample prose. Each is the board's text verbatim,
// as XHTML -- which means it goes through the same tokenizer and block builder a card
// would feed, so the block KINDS are decided by the parser and not asserted here.
std::string_view demoChapterOpenXhtml() {
  return
      "<html><body>"
      "<h1>Chapter I</h1>"
      "<blockquote>Since I can do no good because a woman.</blockquote>"
      "<p>Miss Brooke had that kind of beauty which seems to be thrown into relief by "
      "<em>poor dress</em>. Her hand and wrist were so finely formed that she could "
      "wear sleeves not less bare of style than those in which the Blessed Virgin "
      "appeared to Italian painters.</p>"
      "<p>Her sister Celia wore a necklace, and the two of them had that air of being "
      "dressed alike which is never quite an accident.</p>"
      "</body></html>";
}

std::string_view demoListXhtml() {
  return
      "<html><body>"
      "<p>A page is not a container that text is poured into. It is a grid of line "
      "boxes.</p>"
      "<ul>"
      "<li>A block may begin on any row, but not between two.</li>"
      "<li>Space is counted in rows.</li>"
      "<li>A row holds one line.</li>"
      "</ul>"
      "<p>The third of those is the one that surprises people, and it is the one the "
      "reader cannot bend.</p>"
      "</body></html>";
}

std::vector<LibraryItem> demoLibraryItems() {
  // The board's rows, in the board's order, with the board's own authors and
  // right-hand values. The folder's `childBooks` is 6 because the board says
  // `FOLDER - 6 BOOKS`, and it is also what makes the band read `12 BOOKS` over
  // six books beside it -- the count is derived here exactly as it is on a card.
  //
  // The names carry extensions and the titles do not, because that is the
  // relationship BookList::titleFor establishes and the sample must not be a
  // second answer to it. Dubliners' size is what Book details formats as
  // `0.4 MB`.
  auto book = [](const char* name, const char* title, const char* author, const char* progress,
                 uint32_t size) {
    LibraryItem item;
    item.entry = BookEntry{name, title, false, size};
    item.author = author;
    item.progress = progress;
    return item;
  };
  LibraryItem folder;
  folder.entry = BookEntry{"Classics", "Classics", true, 0};
  folder.childBooks = 6;

  // Book details' board describes ONE of these rows, Dubliners, so its extra
  // fields are set on that one and left blank on the rest -- which is also what a
  // real card looks like today, since nothing can fill them in. Its 416 KB is
  // what the details screen formats as the board's `0.4 MB`.
  auto dubliners = [](LibraryItem item) {
    item.details.author = "James Joyce";
    // THE PERCENTAGE ALONE, as the board now draws it: a page number for a place in the
    // BOOK needs every chapter paginated, ~49 s of decode on this device.
    item.details.progress = "31%";
    item.details.chapter = "ARABY";
    return item;
  };

  return {folder,
          book("Middlemarch.epub", "Middlemarch", "GEORGE ELIOT", "6%", 1268 * 1024),
          book("Jane Eyre.epub", "Jane Eyre", "CHARLOTTE BRONT\xC3\x8B", "DONE", 902 * 1024),
          book("Walden.epub", "Walden", "HENRY DAVID THOREAU", "48%", 511 * 1024),
          book("Meditations.epub", "Meditations", "MARCUS AURELIUS", "NEW", 288 * 1024),
          dubliners(book("Dubliners.epub", "Dubliners", "JAMES JOYCE", "31%", 416 * 1024)),
          book("The Odyssey.epub", "The Odyssey", "HOMER \xC2\xB7 TR. BUTLER", "NEW",
               1704 * 1024)};
}

// design/LibraryScrolled.dc.html: 24 books, windowed at rows 8-14. The board's
// seven visible rows are real entries in a list long enough to scroll, because the
// whole point of the state is the rail -- and a rail's proportions come from the
// list's LENGTH, so a seven-item list could not produce them.
//
// The titles either side are filler and are named as such: what matters is that
// exactly seven sort between `Hard Times` and `North and South`, so the board's
// window is the window BookList's own ordering produces rather than one this
// function asserts.
std::vector<LibraryItem> demoLibraryScrolledItems() {
  auto book = [](const char* title, const char* author, const char* progress) {
    LibraryItem item;
    item.entry = BookEntry{std::string(title) + ".epub", title, false, 512 * 1024};
    item.author = author;
    item.progress = progress;
    return item;
  };
  auto filler = [&book](const char* title) { return book(title, "", "NEW"); };

  return {
      // Seven before the window.
      filler("Anna Karenina"), filler("Bleak House"), filler("Cranford"), filler("Dracula"),
      filler("Emma"), filler("Frankenstein"), filler("Hard Times"),
      // The board's seven, with the board's authors and values.
      book("Jane Eyre", "CHARLOTTE BRONT\xC3\x8B", "DONE"),
      book("Kidnapped", "R. L. STEVENSON", "NEW"),
      book("Little Dorrit", "CHARLES DICKENS", "12%"),
      book("Mansfield Park", "JANE AUSTEN", "NEW"),
      book("Meditations", "MARCUS AURELIUS", "NEW"),
      book("Middlemarch", "GEORGE ELIOT", "6%"),
      book("Moby-Dick", "HERMAN MELVILLE", "48%"),
      // Ten after it, so the rail's thumb has somewhere below to point at.
      filler("North and South"), filler("Oliver Twist"), filler("Persuasion"),
      filler("Rob Roy"), filler("Silas Marner"), filler("The Odyssey"), filler("Ulysses"),
      filler("Villette"), filler("Walden"), filler("Wuthering Heights")};
}

DemoScreenFactory::DemoScreenFactory(FileSystem& fs, std::string root)
    : fs_(&fs), root_(std::move(root)) {}

std::unique_ptr<Screen> DemoScreenFactory::create(ScreenId id) {
  switch (id) {
    case ScreenId::Library: {
      // Over the card when there is one, over the board's own content when there
      // is not. Both go through the real LibraryScreen, so a desktop render is
      // evidence about the device rather than about a second, similar screen.
      auto lib = fs_ != nullptr ? std::make_unique<LibraryScreen>(*fs_, root_)
                                : std::make_unique<LibraryScreen>(
                                      libraryItems_.empty() ? demoLibraryItems()
                                                            : libraryItems_);
      lib->setVisibleRows(libraryVisibleRows_);
      // REGISTERED, so the pointer below cannot outlive what it names. The screen
      // notifies on the way out however it dies -- popped off a live App, taken
      // down with a replaced one, or dropped by a unique_ptr in a test.
      //
      // The Library being replaced is released FIRST: the link is two-way, and
      // leaving an older screen pointing back at this factory would leave a
      // watcher pointer to outlive the watcher.
      dropWatch();
      lib->watchedBy(*this);
      library_ = lib.get();
      return lib;
    }
    case ScreenId::ItemActions:
      // Over the Library this factory built last, which is the one on the stack:
      // an overlay is only ever pushed BY a live Library. A null there is a
      // refused push, which leaves the stack alone rather than putting a hole in
      // it.
      if (library_ == nullptr) return nullptr;
      return std::make_unique<ItemActionsScreen>(*library_);
    case ScreenId::DeleteConfirm:
      // THE FACTS ARE CHECKED FIRST, and the `library_ == nullptr` guard that used to
      // sit above this line is GONE WITH THE REFERENCE. Leaving it would repeat the
      // exact defect recorded on BookDetails below: a change that replaces the
      // `return` and not the GUARD leaves the case refused for the very reason it
      // was meant to stop refusing. WHEN A CASE'S EARLY RETURN ENCODES AN ASSUMPTION
      // A CHANGE REMOVES, THE GUARD IS PART OF THE CHANGE.
      if (deleteFactsSet_) return std::make_unique<DeleteConfirmScreen>(deleteFacts_);
      // The Library is the fallback, and it is what the simulator and the goldens
      // use: it can answer both facts from its focused row.
      if (library_ == nullptr) return nullptr;
      {
        const LibraryItem* item = library_->focusedItem();
        // A folder has no file to remove, so there is nothing to confirm. The
        // actions panel is only ever opened over a book, which is why this has
        // never had to refuse; it is stated rather than assumed because the facts
        // path can be primed by anyone.
        if (item == nullptr || item->entry.isDir) return nullptr;
        return std::make_unique<DeleteConfirmScreen>(DeleteConfirmScreen::Facts{
            library_->focusedPath(), std::string(item->entry.title()), ScreenId::Library});
      }
    case ScreenId::BookDetails:
      // THE FACTS ARE CHECKED FIRST, and a `library_ == nullptr` guard used to sit ABOVE
      // this line -- left over from when the screen was built from a Library reference.
      // The change that removed that requirement replaced the `return` and not the
      // GUARD, so `About this book` still did nothing from a Reader opened through
      // Home's CONTINUE: refused before the facts were ever consulted, which is exactly
      // the case it was meant to fix. The duplicated guard below was the visible tell.
      //
      // WHEN A CASE'S EARLY RETURN ENCODES AN ASSUMPTION A CHANGE REMOVES, THE GUARD IS
      // PART OF THE CHANGE.
      if (detailsFactsSet_) return std::make_unique<BookDetailsScreen>(detailsFacts_);
      // The Library is the fallback, and it is what the simulator and the goldens use.
      if (library_ == nullptr) return nullptr;
      if (const LibraryItem* it = library_->focusedItem()) {
        BookDetailsScreen::Facts f;
        f.title = std::string(it->entry.title());
        // The shell's author wins where it read one; the Library's field is the demo
        // content's and is empty on a card.
        f.author = detailsAuthor_.empty() ? it->details.author : detailsAuthor_;
        f.fileName = it->entry.name;
        f.directory = library_->path();
        f.progress = it->details.progress;
        f.chapter = it->details.chapter;
        f.bytes = it->entry.size;
        return std::make_unique<BookDetailsScreen>(std::move(f));
      }
      return std::make_unique<BookDetailsScreen>(BookDetailsScreen::Facts{});
    case ScreenId::BookEnd:
      // REFUSED WHEN UNPRIMED. A refused push leaves the Reader standing, which is
      // wrong in a way the reader can see through -- where a substituted demo would
      // put another book's title over the one they just finished. setBookEndDemo() is
      // how the simulator and the goldens ask for the board's content, on the same
      // rule as setReaderDemo, setContentsDemo and setPeekDemo.
      if (!bookEndPrimed_) return nullptr;
      return std::make_unique<BookEndScreen>(bookEndFacts_);
    case ScreenId::BookError:
      // REFUSED WHEN NOTHING PRIMED IT, never substituted. A dialog naming a book the
      // reader did not try to open is how this device once woke into Middlemarch.
      // A refused push leaves the parent standing, which is wrong in a way the reader
      // can see through, and the shell logs why.
      if (!bookErrorFactsSet_) return nullptr;
      return std::make_unique<BookErrorScreen>(bookErrorFacts_);
    case ScreenId::Settings: {
      auto scr = std::make_unique<SettingsScreen>(settings_, settingsSink_);
      scr->setMetrics(settingsListH_, settingsRowH_, settingsHeaderH_);
      return scr;
    }
    case ScreenId::Typography:
      // The SAME settings copy and the SAME sink the Settings screen gets: the
      // typography fields live in `Settings`, so there is nothing extra to plumb.
      //
      // AND NOTHING TO SUBSTITUTE, which is what removes the hazard the Reader and
      // Contents cases both have. The band's right slot is empty because these
      // settings are device-wide, so there is no book title to fall back to -- the
      // earlier design passed menuTitle_ here and that is gone with it.
      //
      // `readerBody_` is the same face the Reader draws with, which is what makes
      // the preview a live preview rather than a second approximation of one. IT
      // MAY BE NULL and this case does NOT refuse for it: the theme draws an empty
      // preview box, which is a degradation and not a failure, and refusing would
      // make the panel unreachable from Settings on a device with no book open.
      return std::make_unique<TypographyScreen>(settings_, settingsSink_, readerBody_);
    case ScreenId::ReaderMenu:
      // THE DEMO HAS TO BE ASKED FOR. This fell back to the board's own name whenever
      // nothing set one, and the device then showed `MIDDLEMARCH` in the header over a
      // real book -- the substitution hid the fact that the shell had primed nothing.
      if (!menuTitle_.empty()) return std::make_unique<ReaderMenuScreen>(menuTitle_, menuProgress_);
      if (contentsDemo_) return std::make_unique<ReaderMenuScreen>("Middlemarch", "6%");
      return nullptr;
    case ScreenId::Contents: {
      // AND HERE, WHICH IS WHERE IT ACTUALLY BIT. The fallback was
      // `contentsToc_.empty() ? demoContents() : contentsToc_`, so a real book whose
      // table of contents failed to LOAD showed Middlemarch's chapters -- and the
      // failure had a cause worth seeing (a second 32 KB inflate window against a
      // 45,840-byte floor) that the substitution completely hid.
      //
      // A refused push leaves the menu standing, which is wrong in a way the reader can
      // see through, and the shell's own log says why. Same call the Reader makes.
      if (contentsDemo_)
        return std::make_unique<ContentsScreen>(demoContents(), "Middlemarch",
                                                demoContentsSpine(), contentsRows_);
      // PRIMED, not non-empty: a real book with no NCX primes an empty list and still
      // builds, because it reads fine and simply cannot name its chapters. Only
      // "nothing was primed at all" is refused.
      if (!contentsPrimed_) return nullptr;
      // THE TITLE COMES FROM THE OPENED BOOK, not from `readerBookTitle_` -- which
      // NOTHING ASSIGNS. It is a member the factory reads and no setter writes, so the
      // band would have drawn an empty book name on the device. `readerBook_.title` is
      // the OPF's own, set by setReaderBook along with the spine.
      return std::make_unique<ContentsScreen>(contentsToc_, readerBook_.title, contentsSpine_,
                                              contentsRows_);
    }
    case ScreenId::Sleep:
      // The board's own copy, which is what the simulator and the goldens render.
      // The shell builds its own from the book it was actually reading -- this is
      // the demo catalogue, and a screen nothing can navigate TO needs a source
      // for its values either way.
    {
      SleepViewModel vm = sleepIdle_ ? demoSleepIdleVm() : demoSleepVm();
      // BOTH FIELDS, because they are two halves of one fact and the shell sets
      // both too: the note is what the screen SAYS, `waking` is which screen this
      // IS -- and only the second reaches the badge rule that COVER mode would
      // otherwise silence (SleepViewModel::waking). The demo catalogue holds no
      // CoverSource, so `covered` is false here and this moves no golden; it is
      // set anyway so the two builders of a waking view model cannot drift.
      if (sleepWaking_) {
        vm.note = kStatusWaking;
        vm.waking = true;
      }
      return std::make_unique<SleepScreen>(std::move(vm));
    }
    case ScreenId::Reader: {
      // REFUSED without a body face, rather than built empty. A Reader that
      // rendered nothing looks exactly like a book that failed to open, and the
      // caller can act on a refused push.
      if (readerBody_ == nullptr) return nullptr;
      const std::string title = readerBookTitle_.empty() ? "Middlemarch" : readerBookTitle_;
      std::unique_ptr<ReaderScreen> scr;
      if (!readerBook_.path.empty() && fs_ != nullptr) {
        scr = std::make_unique<ReaderScreen>(*fs_, readerBook_, readerStartChapter_,
                                             readerBody_);
        // BEFORE setMetrics, which is the landing -- see ReaderScreen::restoreAt.
        // Cursor{} is a no-op, so an ordinary open costs nothing for this.
        scr->restoreAt(readerStartAt_);
        // The chapter names, so the header says `LIVRE I` and not `CH. 08`. Empty for a
        // book with no contents, which falls the label back to the position.
        scr->setChapterNames(contentsToc_);
      } else if (readerStyleDemo_ != ReaderStyleDemo::None) {
        // THE STYLED SPECIMENS. Their headers are the boards' own, because the header
        // is chrome the reader is handed rather than something it derives.
        const bool open = readerStyleDemo_ == ReaderStyleDemo::ChapterOpen;
        scr = std::make_unique<ReaderScreen>(
            open ? demoChapterOpenXhtml() : demoListXhtml(),
            open ? "Middlemarch" : "The Craft of Type", open ? "CHAPTER I" : "THE PAGE",
            readerBody_);
      } else if (readerDemo_) {
        // A NAME, as the board now draws: the header holds the chapter's name where the
        // contents supply one, and `CH. 01` is only the fallback for a book that has
        // none. The in-memory constructor takes the label directly, so the demo states
        // it rather than looking it up.
        scr = std::make_unique<ReaderScreen>(demoReaderXhtml(), title, "LIVRE I", readerBody_);
      } else {
        // NO BOOK AND NO DEMO ASKED FOR: refused. This is the session-restore path --
        // the shell sets the book from a button press, so a wake has nothing set --
        // and it used to fall through to the demo, waking the device into
        // Middlemarch. A refused push leaves the Library standing, which is wrong in
        // a way the user can see through, rather than wrong in a way they cannot.
        return nullptr;
      }
      // BEFORE setMetrics, and that ordering is the whole point: setMetrics lays the
      // chapter out, and the wrap measures emphasis with this face. Setting it after
      // would leave the first page measured roman and drawn in two faces -- the
      // measure/draw disagreement StyledFace exists to prevent, and invisible on any
      // page that happens to have no emphasis.
      scr->setItalic(readerItalic_);
      // BEFORE setMetrics for the reason stated just above, and it applies more
      // sharply here: the classes decide which runs are emphasised at all, so a set
      // arriving after the landing would leave the first page with no emphasis and
      // every page after it with some.
      scr->setItalicClasses(readerItalicClasses_);
      // BEFORE setMetrics, like the italic and for a related reason: setMetrics lands
      // the page, and syncVm computes the footer's way-back label off the anchor. An
      // anchor arriving after would be restored but invisible until the next turn.
      if (readerHasAnchor_) scr->restoreAnchor(readerAnchor_);
      // The expensive call: one decode of the chapter to build the page index.
      scr->setMetrics(readerMetrics_);
      return scr;
    }
    case ScreenId::SdMissing:
      // Buildable through the factory, not only as a root, so the shell can
      // replace the stack with it if the card goes away later and the simulator
      // can render it. It takes no arguments: a missing card is a missing card.
      return std::make_unique<SdMissingScreen>();
    case ScreenId::BatteryEmpty:
      // BUILDABLE AND NEVER PUSHED. The shell paints this one directly, on
      // SleepScreen's argument -- a pushed BatteryEmpty would be restored INTO on the
      // next wake -- so nothing here ever asks the factory for it. It is a case
      // anyway, for SdMissing's reason: the simulator and the goldens then reach it
      // the way they reach every other screen, and it needs no priming, because a
      // flat pack is a flat pack and the board's copy is the only copy.
      return std::make_unique<BatteryEmptyScreen>();
    case ScreenId::Home:
      // The root is never rebuilt: popping to Home returns the original object,
      // with its focus intact.
      return nullptr;
    case ScreenId::Peek: {
      // REFUSED without a body face, as the Reader is: a panel that rendered nothing is
      // indistinguishable from a chapter that failed to open, and the caller can act on
      // a refused push.
      if (readerBody_ == nullptr) return nullptr;
      std::unique_ptr<PeekScreen> scr;
      if (peekPrimed_ && !readerBook_.path.empty() && fs_ != nullptr) {
        scr = std::make_unique<PeekScreen>(*fs_, readerBook_, peekSpine_, readerBody_);
        // The chapter names, so the band says the chapter's NAME where the contents
        // supply one. Empty for a book with no contents, which falls back to `CH. NN`.
        scr->setChapterNames(contentsToc_);
      } else if (peekDemo_) {
        scr = std::make_unique<PeekScreen>(demoPeekXhtml(), "CH. 01", 4, readerBody_);
      } else {
        // NOTHING PRIMED AND NO DEMO ASKED FOR: refused. This is the session-restore
        // path, and it is why a peek is not restorable across a wake -- App::restore
        // stops short and leaves the Reader standing, which is right: a peek is a
        // transient excursion, and rebuilding one would need a peeked cursor nothing
        // persists.
        return nullptr;
      }
      // BEFORE setMetrics, for renderReader's reason: setMetrics lays the page out and
      // the wrap measures emphasis with this face, so a face arriving after would leave
      // the first page measured roman and drawn in two.
      scr->setItalic(readerItalic_);
      scr->setMetrics(peekMetrics_);
      return scr;
    }
    // NOT A SCREEN -- see ScreenId::Count's own comment. Refused explicitly so this
    // switch stays exhaustive and -Wswitch keeps working as the reminder that a NEW
    // screen needs a case here. Falling through to the `return nullptr` below would
    // behave identically and cost exactly that reminder.
    case ScreenId::Count:
      return nullptr;
  }
  return nullptr;
}

}  // namespace reader
