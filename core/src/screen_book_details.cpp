#include "reader/screen_book_details.h"

#include <string>

#include "reader/screen_library.h"
#include "reader/text.h"  // upperAscii
#include "reader/theme.h"

namespace reader {

namespace {

// The extension, shouted: the board's band value is `EPUB`. Empty for a name
// with no extension, which BookList would not have listed anyway -- so this is a
// guard rather than a case.
std::string formatOf(std::string_view name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) return {};
  return upperAscii(name.substr(dot + 1));
}

// `0.4 MB`, which is what the board writes for a 416 KB file -- so its unit is
// MB with one decimal even below one, not a unit that switches to KB. One
// decimal place, computed in tenths so there is no floating point on a chip that
// emulates it.
std::string megabytes(uint32_t bytes) {
  const uint32_t tenths = static_cast<uint32_t>((static_cast<uint64_t>(bytes) * 10 + (1u << 19)) >>
                                                20);
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

}  // namespace

BookDetailsScreen::BookDetailsScreen(const LibraryScreen& library) {
  const LibraryItem* item = library.focusedItem();
  if (item != nullptr) {
    vm_.title = item->entry.title;
    vm_.author = item->details.author;
    vm_.subtitle = item->details.subtitle;
    vm_.format = formatOf(item->entry.name);
    // The board's six rows, in the board's order. Four of the values need
    // metadata that does not exist yet and are therefore EMPTY strings rather
    // than invented ones -- a row whose value is blank is honest, and a "0%" or a
    // fabricated date would be a claim.
    //
    // `Bookmarks` is the exception that is genuinely zero: there is no way to
    // make a bookmark yet, so nought is a fact rather than a placeholder.
    vm_.fields = {{"Progress", item->details.progress},
                  {"Current story", item->details.chapter},
                  {"Bookmarks", "0"},
                  {"File size", megabytes(item->entry.size)},
                  {"Added", item->details.added},
                  // Shouted and with the board's trailing slash: `/BOOKS/`. The
                  // path is the directory the book was listed from, which is the
                  // Library's, so a book inside a folder says so.
                  {"Location", upperAscii(library.path()) + "/"}};
  }
  // BACK, and three dead slots. The board draws exactly that -- one label and
  // three of the boards' `width: 36px` placeholders -- because there is nothing
  // on this screen to move a focus through or to select, and a bar promising a
  // button the screen does not use is worse than one promising nothing.
  vm_.hints = {"BACK", "", "", ""};
  vm_.holds = {false, false, false, false};
}

Action BookDetailsScreen::onEvent(const InputEvent& ev) {
  if (ev.kind != PressKind::Short) return Action::none();
  // Back, and only Back. Confirm, Up and Down have no hint and do nothing, which
  // is the same rule SdMissing states: a button that acts without the bar saying
  // so is worse than one that does nothing.
  if (ev.button == Button::Back) return Action::pop();
  return Action::none();
}

void BookDetailsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                               Plane plane) const {
  theme.renderBookDetails(fb, fonts, vm_, plane);
}

}  // namespace reader
