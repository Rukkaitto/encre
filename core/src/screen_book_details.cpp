#include "reader/screen_book_details.h"

#include <string>

#include "reader/screen_library.h"
#include "reader/text.h"  // upperLatin1
#include "reader/theme.h"

namespace reader {

namespace {

// The extension, shouted: the board's band value is `EPUB`. Empty for a name
// with no extension, which BookList would not have listed anyway -- so this is a
// guard rather than a case.
std::string formatOf(std::string_view name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= name.size()) return {};
  return upperLatin1(name.substr(dot + 1));
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

BookDetailsScreen::BookDetailsScreen(Facts facts) {
  vm_.title = std::move(facts.title);
  vm_.author = std::move(facts.author);
  vm_.format = formatOf(facts.fileName);
  // The board's FIVE rows, in the board's order. A value that is not known is an EMPTY
  // string rather than an invented one -- a blank row is honest and a "0%" would be a
  // claim about a book nobody has opened.
  //
  // `Bookmarks` is the exception that is genuinely zero: there is no way to make a
  // bookmark yet, so nought is a fact rather than a placeholder.
  vm_.fields = {{"Progress", std::move(facts.progress)},
                {"Current chapter", std::move(facts.chapter)},
                {"Bookmarks", "0"},
                {"File size", megabytes(facts.bytes)},
                // Shouted and with the board's trailing slash: `/BOOKS/`. The directory
                // the book lives in, so a book inside a folder says so.
                {"Location", upperLatin1(facts.directory) + "/"}};
  // BACK, and three dead slots. The board draws exactly that -- one label and three of
  // the boards' `width: 36px` placeholders -- because there is nothing on this screen to
  // move a focus through or to select, and a bar promising a button the screen does not
  // use is worse than one promising nothing.
  vm_.hints = {"BACK", "", "", ""};
  vm_.holds = {false, false, false, false};
  declareHints(vm_.holds);
}

Action BookDetailsScreen::onGesture(const GestureEvent& g) {
  // Back, and only Back. Confirm, Up and Down have no hint and do nothing, which
  // is the same rule SdMissing states: a button that acts without the bar saying
  // so is worse than one that does nothing.
  if (g.what == Gesture::Back) return Action::pop();
  return Action::none();
}

void BookDetailsScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                               Plane plane) const {
  theme.renderBookDetails(fb, fonts, vm_, plane);
}

}  // namespace reader
