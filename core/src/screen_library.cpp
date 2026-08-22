#include "reader/screen_library.h"

#include "reader/filesystem.h"
#include "reader/text.h"  // upperAscii
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
    : fs_(&fs), root_(std::move(root)), path_(root_) {
  // The hint labels are the board's: BACK / OPEN / UP / DOWN, with the hold ring
  // on Confirm because a long press opens the actions overlay. `holds` is the
  // ONE declaration of that -- the theme draws the ring from it and
  // longPressable() builds the recognizer's mask from it -- so this screen
  // cannot promise a hold it has not bound or bind one nothing advertises.
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
  vm_.holds = {false, true, false, false};
  rescan();
}

LibraryScreen::LibraryScreen(std::vector<LibraryItem> sample)
    // The path the device's Library is rooted at, so the sample says the same
    // thing about itself that a card would -- Book details draws it as its
    // `Location` row, and an empty path there would read as `/`.
    : root_(kBooksRoot), path_(kBooksRoot), items_(std::move(sample)) {
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
  vm_.holds = {false, true, false, false};
  window_.setCount(itemCount());
  syncVm();
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
  const int wasFocus = window_.focus();
  items_.clear();
  bool ok = false;
  if (fs_ != nullptr) {
    std::vector<BookEntry> entries;
    ok = BookList::scan(*fs_, path_, entries);
    items_.reserve(entries.size());
    for (BookEntry& e : entries) {
      LibraryItem item;
      const bool dir = e.isDir;
      // A folder's own book count, for the board's `FOLDER - 6 BOOKS` line. One
      // extra listing per folder, at rescan time only. It is deliberately one
      // level deep: the board's `12 BOOKS` in the band is the 6 books beside the
      // folder plus the 6 inside it, so that is what the design counts, and a
      // full recursive walk of a card would be an unbounded cost on a screen
      // that has to paint.
      item.childBooks = dir ? BookList::countBooks(*fs_, join(e.name)) : -1;
      item.progress = dir ? "" : kNewBook;
      item.entry = std::move(e);
      items_.push_back(std::move(item));
    }
  }
  // setCount pulls the focus back into range and the window follows it, which is
  // what makes deleting the last book land the focus on the new last one instead
  // of one past the end.
  window_.setCount(itemCount());
  if (wasFocus < 0) window_.setFocus(0);
  syncVm();
  return ok;
}

void LibraryScreen::setVisibleRows(int n) {
  window_.setVisibleRows(n);
  syncVm();
}

bool LibraryScreen::setFocus(int index) {
  const bool moved = window_.setFocus(index);
  if (moved) syncVm();
  return moved;
}

const LibraryItem* LibraryScreen::focusedItem() const {
  const int i = window_.focus();
  if (i < 0 || i >= itemCount()) return nullptr;
  return &items_[static_cast<size_t>(i)];
}

void LibraryScreen::syncVm() {
  // The band's label: LIBRARY at the top, and the folder's own name inside one.
  // Shouted here rather than by the theme because the theme shouts what the
  // board declares `text-transform: uppercase` on, and this band's label is a
  // caps label whose text arrives from a directory name.
  vm_.title = (path_ == root_) ? "LIBRARY" : upperAscii(BookList::titleFor(leafOf(path_), true));

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
  vm_.firstRow = window_.firstVisible();
  vm_.totalRows = window_.count();

  vm_.rows.clear();
  const int first = window_.firstVisible();
  const int count = window_.visibleCount();
  vm_.rows.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    const LibraryItem& item = items_[static_cast<size_t>(first + i)];
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
  // or the window has no height. `visibleCount` is 0 in that case, so this
  // cannot name a row that was not drawn.
  const int focus = window_.focus();
  vm_.focusedRow = (focus >= first && focus < first + count) ? focus - first : -1;
}

Action LibraryScreen::moveFocus(int delta) {
  // ScrollWindow reports whether anything moved, so the end of a list costs no
  // refresh: on this panel a repaint that changes nothing is ~520 ms of the user
  // wondering whether the button works.
  if (!window_.moveFocus(delta)) return Action::none();
  syncVm();
  return Action::redraw();
}

bool LibraryScreen::descend() {
  const LibraryItem* item = focusedItem();
  if (item == nullptr || !item->entry.isDir || fs_ == nullptr) return false;
  path_ = join(item->entry.name);
  // A fresh directory starts at its first row rather than inheriting the parent's
  // scroll position, which would open a folder half way down.
  window_.setCount(0);
  rescan();
  return true;
}

bool LibraryScreen::ascend() {
  if (path_ == root_) return false;
  const size_t slash = path_.rfind('/');
  // Never above the root the Library was given: `root_` is a prefix of `path_`
  // by construction, so this cannot walk off the top of the card.
  path_ = (slash == std::string::npos || slash < root_.size()) ? root_ : path_.substr(0, slash);
  window_.setCount(0);
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

Action LibraryScreen::onEvent(const InputEvent& ev) {
  if (ev.kind == PressKind::Long) {
    // The only hold this screen binds, and the one its bar advertises. A hold on
    // any other button is not reachable -- the mask comes from the same array --
    // so anything else arriving here means the two have drifted, and ignoring it
    // keeps that visible.
    if (ev.button != Button::Confirm) return Action::none();
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

  // A held Up or Down carries how far to go: see InputEvent::steps. The panel is
  // why it is a distance rather than a count of events -- a paint blocks the loop
  // that ticks the recognizer, so one event has to stand for all the time that
  // passed while the panel was busy.
  const int step = ev.kind == PressKind::Repeat ? ev.steps : 1;
  switch (ev.button) {
    case Button::Down:
      return moveFocus(+step);
    case Button::Up:
      return moveFocus(-step);
    case Button::Confirm: {
      const LibraryItem* item = focusedItem();
      if (item == nullptr) return Action::none();
      if (item->entry.isDir) return descend() ? Action::redraw() : Action::none();
      // Opening a book is the Reader, which is Phase 3. Nothing happens yet, on
      // purpose: pushing a placeholder screen would be a screen to delete, and
      // core/ has no logger to say so through -- the shell is what logs, and it
      // sees only the Action. So this returns none and the comment is the record.
      return Action::none();
    }
    case Button::Back:
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
