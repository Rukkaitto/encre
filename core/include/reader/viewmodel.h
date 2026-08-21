#pragma once
#include <array>
#include <string>
#include <vector>

namespace reader {

struct MenuEntry {
  std::string label;
  std::string value;
};

// Semantic content + interaction state only. No geometry, no style.
struct HomeViewModel {
  std::string title;
  std::string author;
  std::string chapterLabel;
  int percent = 0;
  int currentPage = 0;
  int pageCount = 0;
  int batteryPercent = 0;
  // Cover art is not decoded yet (Phase 3 owns EPUB images), so the theme draws
  // a dithered placeholder carrying the title. This flag says whether a real
  // cover exists, so the placeholder can be replaced without a view-model change.
  bool hasCover = false;
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
  // Which of those four buttons also has a long-press action. The theme draws a
  // hollow ring on the slot (design 662557d) and the screen builds its
  // long-press mask from the same array, so the affordance and the behaviour
  // cannot drift apart -- a ring always means a hold is bound, and a bound hold
  // always shows a ring.
  std::array<bool, 4> holds{};
};

// The no-card prompt (spec 6): design/SdMissing.dc.html. Content only -- the
// board's own copy, which the screen supplies and the theme lays out.
//
// There is no `retrying` or `failed` flag here, and that is deliberate: a retry
// takes a mount attempt and a repaint, and the screen cannot know the outcome
// because it is not the thing that mounts (see Action::Kind::Retry). Either the
// card is there, in which case the shell replaces this screen, or it is not, in
// which case the honest UI is the same prompt again. A "checking..." state that
// no code could ever clear would be a lie drawn on glass.
struct SdMissingViewModel {
  std::string title;    // "NO SD CARD"
  std::string message;  // the paragraph under it, wrapped by the theme
  std::string action;   // the button's label
  std::array<std::string, 4> hints{};  // Back, Confirm, Up, Down
  std::array<bool, 4> holds{};
};

// One Library row as the theme draws it (design/Library.dc.html). Nothing here
// addresses a file: the leaf name the card knows the thing by stays on the
// screen's side of the wall, because the theme has no business with it and a
// view-model that carried it would be the seam through which layout learned
// about storage.
//
// `meta` is the second line, and it is composed by the screen rather than by the
// theme because it is CONTENT: an author, or a folder's "FOLDER - 6 BOOKS"
// summary. It is empty on the device today -- an author needs the EPUB's OPF,
// which is Phase 3 -- and the row's height does not depend on it, so a blank
// line leaves the list on the same grid.
struct LibraryRow {
  std::string title;
  std::string meta;
  std::string value;  // "6%", "DONE", "NEW"; empty on a folder, which discloses
  bool isFolder = false;
};

// The Library (spec 4.1), from design/Library.dc.html.
//
// `rows` is EXACTLY what is on glass, never the whole directory: the scroll
// window is the screen's business, and handing the theme a hundred books plus a
// first-visible index would put the one rule that matters -- that the focus is
// inside the window -- in two places. `focusedRow` therefore indexes `rows`, and
// a screen with a focus scrolled out of view is not expressible.
struct LibraryViewModel {
  std::string title;  // the band's label: "LIBRARY", or a subfolder's own name
  // The band's value, as a number: the theme formats it, because "12 BOOKS"
  // against "1 BOOK" is a presentation decision and a pre-formatted string in
  // here would be a screen making one.
  int bookCount = 0;
  std::vector<LibraryRow> rows;
  int focusedRow = -1;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// The item actions overlay (design/LibraryActions.dc.html): a panel over the
// veiled Library, captioned with the book it acts on.
struct ItemActionEntry {
  std::string label;
  // Whether the row leads somewhere, which is the board's rule for the trailing
  // chevron: Open and Book details have one, Mark as finished and Delete... do
  // not. A flag rather than the theme keying on the row's index, which would
  // silently mark the wrong row the first time the list is reordered.
  bool discloses = false;
};

struct ItemActionsViewModel {
  std::string title;   // the book's name; the theme shouts it, as a caps label
  std::string status;  // the caption's right-hand value: "31%", or "NEW" today
  std::vector<ItemActionEntry> actions;
  int focusedAction = 0;
  std::array<std::string, 4> hints{};
  std::array<bool, 4> holds{};
};

// A provisional titled-list surface: Phase 2B's Library and Settings
// placeholders and its Input Monitor. It exists so the interaction runtime can
// be navigated and verified before the real screens are built, and Phase 2C
// deletes it. Deliberately plain, and it carries `note` so nobody reads it as a
// design.
struct StubViewModel {
  std::string title;
  std::string note;                    // e.g. "PLACEHOLDER - PHASE 2C"
  std::vector<std::string> lines;
  int focusedLine = -1;                // -1 = nothing focused
  int batteryPercent = 0;
  std::array<std::string, 4> hints{};  // Back, Confirm, Up, Down
  std::array<bool, 4> holds{};
};

}  // namespace reader
