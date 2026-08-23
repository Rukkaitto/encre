#include "reader/screens.h"

#include "reader/screen_sleep.h"

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
  vm.note = std::string("ASLEEP") + kDot + "PRESS POWER TO WAKE";
  return vm;
}

// design/Reader.dc.html's own two paragraphs, verbatim, on the same reasoning as
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
    item.details.subtitle = "Fifteen stories \xC2\xB7 1914";
    item.details.progress = "31% \xC2\xB7 PAGE 78 OF 252";
    item.details.chapter = "ARABY";
    item.details.added = "AUG 14, 2026";
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
      if (library_ == nullptr) return nullptr;
      return std::make_unique<DeleteConfirmScreen>(*library_);
    case ScreenId::BookDetails:
      if (library_ == nullptr) return nullptr;
      return std::make_unique<BookDetailsScreen>(*library_);
    case ScreenId::Settings: {
      auto scr = std::make_unique<SettingsScreen>(settings_, settingsSink_);
      scr->setMetrics(settingsListH_, settingsRowH_, settingsHeaderH_);
      return scr;
    }
    case ScreenId::Sleep:
      // The board's own copy, which is what the simulator and the goldens render.
      // The shell builds its own from the book it was actually reading -- this is
      // the demo catalogue, and a screen nothing can navigate TO needs a source
      // for its values either way.
      return std::make_unique<SleepScreen>(demoSleepVm());
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
      } else if (readerDemo_) {
        scr = std::make_unique<ReaderScreen>(demoReaderXhtml(), title, "CH. 01", readerBody_);
      } else {
        // NO BOOK AND NO DEMO ASKED FOR: refused. This is the session-restore path --
        // the shell sets the book from a button press, so a wake has nothing set --
        // and it used to fall through to the demo, waking the device into
        // Middlemarch. A refused push leaves the Library standing, which is wrong in
        // a way the user can see through, rather than wrong in a way they cannot.
        return nullptr;
      }
      // The expensive call: one decode of the chapter to build the page index.
      scr->setMetrics(readerMetrics_);
      return scr;
    }
    case ScreenId::SdMissing:
      // Buildable through the factory, not only as a root, so the shell can
      // replace the stack with it if the card goes away later and the simulator
      // can render it. It takes no arguments: a missing card is a missing card.
      return std::make_unique<SdMissingScreen>();
    case ScreenId::Home:
      // The root is never rebuilt: popping to Home returns the original object,
      // with its focus intact.
      return nullptr;
  }
  return nullptr;
}

}  // namespace reader
