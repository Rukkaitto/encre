#include "reader/screen_library.h"

#include "reader/reading_store.h"

#include "reader/filesystem.h"
#include "reader/text.h"  // upperLatin1
#include "reader/theme.h"

namespace reader {

namespace {
// The board's own copy for a folder's second line: `FOLDER &middot; 6 BOOKS`.
// U+00B7, as every board's meta line spells it.
const char* const kMiddot = " \xC2\xB7 ";

// Until per-book state exists there is exactly one thing a book's right-hand
// value can honestly say. The board shows percentages and DONE beside it, and
// the view-model carries whatever it is given, so Phase 3 fills this in without
// touching the theme.
const char* const kNewBook = "NEW";

// The last segment of a path, which is the name of the directory being listed.
// Not FileSystem's job: it addresses things, it does not decompose them.
std::string_view leafOf(std::string_view path) {
  const size_t slash = path.rfind('/');
  if (slash == std::string_view::npos) return path;
  return path.substr(slash + 1);
}
}  // namespace

LibraryScreen::LibraryScreen(FileSystem& fs, std::string root)
    : FocusScreen(0, 0), fs_(&fs), root_(std::move(root)), path_(root_) {
  // The hint labels are the board's: BACK / OPEN / UP / DOWN, with the hold ring
  // on Confirm because a long press opens the actions overlay. `holds` is the
  // ONE declaration of that -- the theme draws the ring from it and
  // longPressable() builds the recognizer's mask from it -- so this screen
  // cannot promise a hold it has not bound or bind one nothing advertises.
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
  vm_.holds = {false, true, false, false};
  declareHints(vm_.holds);
  // The one screen with a list long enough to need held scrolling -- 256 rows at
  // the cap, and a row per press is a minute of pressing. Declared beside the
  // holds because both are one statement about what the four buttons do, and
  // gestureFor reads them together.
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));
  rescan();
}

LibraryScreen::LibraryScreen(std::vector<LibraryItem> sample)
    // The path the device's Library is rooted at, so the sample says the same
    // thing about itself that a card would -- Book details draws it as its
    // `Location` row, and an empty path there would read as `/`.
    : FocusScreen(0, 0), root_(kBooksRoot), path_(kBooksRoot), items_(std::move(sample)) {
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
  vm_.holds = {false, true, false, false};
  declareHints(vm_.holds);
  // The one screen with a list long enough to need held scrolling -- 256 rows at
  // the cap, and a row per press is a minute of pressing. Declared beside the
  // holds because both are one statement about what the four buttons do, and
  // gestureFor reads them together.
  declareRepeat(static_cast<ButtonMask>(buttonBit(Button::Up) | buttonBit(Button::Down)));
  window().setCount(itemCount());
  syncVm();
}

LibraryScreen::~LibraryScreen() {
  if (watcher_ != nullptr) watcher_->libraryGone(this);
}

std::string LibraryScreen::join(std::string_view leaf) const {
  // FileSystem paths never end in '/', and "/" itself is the root, so the only
  // case that needs care is a root that IS "/": joining naively would produce
  // "//books", which the contract says addresses the same thing but which would
  // read badly in a log.
  if (path_ == "/") return "/" + std::string(leaf);
  return path_ + "/" + std::string(leaf);
}

bool LibraryScreen::rescan() {
  const int wasFocus = window().focus();
  items_.clear();
  bool ok = false;
  if (fs_ != nullptr) {
    std::vector<BookEntry> entries;
    ok = BookList::scan(*fs_, path_, entries);
    // Every book that has been opened, read once for the whole listing rather than
    // once per row. An empty index is the normal state of a card nothing has been
    // read on, so a failure here costs percentages and nothing else.
    std::vector<ProgressEntry> started;
    loadProgressIndex(*fs_, started);
    items_.reserve(entries.size());
    for (BookEntry& e : entries) {
      LibraryItem item;
      const bool dir = e.isDir;
      // A folder's own book count, for the board's `FOLDER - 6 BOOKS` line. It
      // is deliberately one level deep: the board's `12 BOOKS` in the band is
      // the 6 books beside the folder plus the 6 inside it, so that is what the
      // design counts, and a full recursive walk of a card would be an unbounded
      // cost on a screen that has to paint.
      //
      // IT USED TO BE ONE EXTRA LISTING PER FOLDER ON EVERY PUSH, and the
      // Library is destroyed by the pop that leaves it -- so Home > Library >
      // Back > Library paid for all of them twice, at ~2.90 ms an ENTRY, on top
      // of Home's own count having paid for the same folders at boot. countBooks
      // memoises on the filesystem now (reader/dir_counts.h), so a folder is
      // walked once per card STATE rather than once per caller, and a rescan
      // that follows a count reaches the card for nothing but its own listing.
      // Nothing here changed: the number, and where it is not available, are
      // exactly as before.
      item.childBooks = dir ? BookList::countBooks(*fs_, join(e.name)) : -1;
      // A PERCENTAGE IF THE BOOK HAS BEEN STARTED, `NEW` if it has not.
      //
      // design/Library.dc.html gives rows both forms, and this header used to say
      // "on the device today the author is blank and every book reads NEW" because a
      // percentage needed `/.reader/state/` and there was nothing in it. There is
      // now.
      //
      // The index is read ONCE per rescan, above -- one listing plus one read per
      // book STARTED. Asking each row for its own sidecar would be one open per book
      // on the card, most of them misses.
      // ONE LOOKUP FOR BOTH ROWS. The Library's own row wants a percentage and Book
      // details wants a percentage AND the chapter name, and they come out of the same
      // entry -- two scans for one answer would be two scans.
      const ProgressEntry* seen = dir ? nullptr : progressFor(started, join(e.name));
      item.progress = dir ? "" : (seen != nullptr ? std::to_string(seen->percent) + "%"
                                                  : kNewBook);
      if (seen != nullptr) {
        // BOOK DETAILS' OWN RUNS, which are not the row's. Its Progress row is the
        // percentage without the page count the board used to ask for, and its "Current
        // story" is the chapter name the sidecar carries -- so both cost the listing the
        // percentages already cost rather than an archive open per book.
        item.details.progress = std::to_string(seen->percent) + "%";
        item.details.chapter = seen->chapter;
      }
      item.entry = std::move(e);
      items_.push_back(std::move(item));
    }
  }
  // setCount pulls the focus back into range and the window follows it, which is
  // what makes deleting the last book land the focus on the new last one instead
  // of one past the end.
  window().setCount(itemCount());
  if (wasFocus < 0) window().setFocus(0);
  syncVm();
  return ok;
}

void LibraryScreen::setVisibleRows(int n) {
  window().setVisibleRows(n);
  syncVm();
}

const LibraryItem* LibraryScreen::focusedItem() const {
  const int i = window().focus();
  if (i < 0 || i >= itemCount()) return nullptr;
  return &items_[static_cast<size_t>(i)];
}

void LibraryScreen::syncVm() {
  // The band's label: LIBRARY at the top, and the folder's own name inside one.
  // Shouted here rather than by the theme because the theme shouts what the
  // board declares `text-transform: uppercase` on, and this band's label is a
  // caps label whose text arrives from a directory name.
  vm_.title = (path_ == root_) ? "LIBRARY" : upperLatin1(BookList::titleFor(leafOf(path_), true));

  // The band's count is the books here plus the books one level down, which is
  // what the board's `12 BOOKS` over 6 books and a 6-book folder measures.
  int books = 0;
  for (const LibraryItem& item : items_) {
    if (!item.entry.isDir) ++books;
    else if (item.childBooks > 0) books += item.childBooks;
  }
  vm_.bookCount = books;

  // The rail's two numbers. The theme cannot derive them: vm_.rows holds only
  // what is on screen, so "how far down a longer list is this" has to be said.
  const ScrollWindow::Slice s = window().slice();
  vm_.firstRow = s.first;
  vm_.totalRows = window().count();

  vm_.rows.clear();
  vm_.rows.reserve(static_cast<size_t>(s.count));
  for (int i = 0; i < s.count; ++i) {
    const LibraryItem& item = items_[static_cast<size_t>(s.first + i)];
    LibraryRow row;
    row.title = std::string(item.entry.title());
    row.isFolder = item.entry.isDir;
    if (item.entry.isDir) {
      row.meta = "FOLDER";
      if (item.childBooks >= 0)
        row.meta += kMiddot + std::to_string(item.childBooks) +
                    (item.childBooks == 1 ? " BOOK" : " BOOKS");
      // No value: a folder discloses, and the board gives it a chevron instead.
    } else {
      row.meta = item.author;
      row.value = item.progress;
    }
    vm_.rows.push_back(std::move(row));
  }
  // The focus as an index into the SLICE, or -1 when there is nothing selected
  // or the window has no height -- Slice's own rule, so this cannot name a row
  // that was not drawn.
  vm_.focusedRow = s.focused;
}

bool LibraryScreen::descend() {
  const LibraryItem* item = focusedItem();
  if (item == nullptr || !item->entry.isDir || fs_ == nullptr) return false;
  path_ = join(item->entry.name);
  // A fresh directory starts at its first row rather than inheriting the parent's
  // scroll position, which would open a folder half way down.
  window().setCount(0);
  rescan();
  return true;
}

bool LibraryScreen::ascend() {
  if (path_ == root_) return false;
  const size_t slash = path_.rfind('/');
  // Never above the root the Library was given: `root_` is a prefix of `path_`
  // by construction, so this cannot walk off the top of the card.
  path_ = (slash == std::string::npos || slash < root_.size()) ? root_ : path_.substr(0, slash);
  window().setCount(0);
  rescan();
  return true;
}

bool LibraryScreen::deleteFocused() {
  const LibraryItem* item = focusedItem();
  if (item == nullptr || fs_ == nullptr) return false;
  // Files only. FileSystem::remove is files-only by contract, and recursively
  // deleting a directory is not a decision V1 makes on one long press.
  if (item->entry.isDir) return false;
  const std::string target = join(item->entry.name);
  const bool gone = fs_->remove(target);
  // Rescan either way: `remove` reports the END STATE, so a false means the file
  // is still there and the list should say so.
  rescan();
  return gone;
}

Action LibraryScreen::onGesture(const GestureEvent& g) {
  if (g.what == Gesture::Secondary) {
    // The actions overlay. Which BUTTON produced this is no longer this screen's
    // business -- gestureFor already refused a hold the hint bar does not
    // advertise, so a Secondary arriving here is one the bar drew a ring for.
    const LibraryItem* item = focusedItem();
    if (item == nullptr) return Action::none();
    // Books only. The overlay is design/LibraryActions.dc.html, whose four rows
    // are Open / Book details / Mark as finished / Delete... -- three of which
    // mean nothing for a directory, and there is no board for a folder's
    // actions. Doing nothing is the honest answer until there is one; inventing
    // a folder overlay here would be a design decision made in a screen.
    if (item->entry.isDir) return Action::none();
    return Action::push(ScreenId::ItemActions);
  }

  // The distance comes off the gesture, and so does whether it was HELD -- which
  // is the whole reason this screen no longer decides. A held Next must clamp at
  // the end of the list where a pressed Next wraps, and `held` is what lets
  // ScrollWindow apply that without any screen choosing.
  switch (g.what) {
    case Gesture::Next:
      return moveFocus(+g.steps, g.held);
    case Gesture::Prev:
      return moveFocus(-g.steps, g.held);
    case Gesture::Activate: {
      const LibraryItem* item = focusedItem();
      if (item == nullptr) return Action::none();
      if (item->entry.isDir) return descend() ? Action::redraw() : Action::none();
      // ASKS, rather than pushes. A Reader needs a Document, and a Document needs
      // this file inflated, unzipped and parsed -- none of which a screen can do,
      // because storage is not core/'s. So the request is latched and the shell
      // answers it; see Action::open() and App::openRequested().
      return Action::open();
    }
    case Gesture::Back:
      // Out of a folder, or off the Library entirely. Ascending is a content
      // change within one screen, so it is a Redraw; leaving is a Pop, and the
      // App's transition flag makes that the screen change it is.
      return ascend() ? Action::redraw() : Action::pop();
    default:
      return Action::none();
  }
}

void LibraryScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                           Plane plane) const {
  theme.renderLibrary(fb, fonts, vm_, plane);
}

}  // namespace reader
