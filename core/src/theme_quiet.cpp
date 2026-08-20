#include "reader/theme_quiet.h"

#include <string>

#include "reader/components.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
constexpr int kCoverW = 156;
constexpr int kCoverH = 234;
constexpr int kGutter = 22;
constexpr int kBlockH = 52;

// ASCII-only uppercase, local to the theme. The design sets `text-transform:
// uppercase` on both title runs; the strip's is small enough that mixed case
// reads as a different element. A general Unicode case mapping is not something
// core/ should carry for one label, and the titles that need it (accented Latin,
// Greek, Cyrillic) arrive with real metadata in Phase 3 -- non-ASCII bytes are
// passed through untouched rather than mangled.
std::string upperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

// A dithered stand-in until Phase 3 decodes real cover images: a bordered
// panel with the title reversed out of a filled strip along its bottom.
void drawCoverPlaceholder(Framebuffer& fb, const FontSet& fonts, int x, int y,
                          std::string_view title) {
  // Level 1, not 2: the board's `.dither-dots` is a 4px-pitch radial-gradient
  // dot, roughly a fifth coverage. Level 2 is a 50% checkerboard, which reads as
  // grey mesh rather than a sparse tint and swamped the strip's border.
  ditherRect(fb, x, y, kCoverW, kCoverH, 1);
  fb.fillRect(x, y, kCoverW, 2, false);
  fb.fillRect(x, y + kCoverH - 2, kCoverW, 2, false);
  fb.fillRect(x, y, 2, kCoverH, false);
  fb.fillRect(x + kCoverW - 2, y, 2, kCoverH, false);
  // The strip's title is the board's 16px/700 uppercase, so Value (14/700), not
  // Body (17/500): the weight is what makes it read at this size, and mixed case
  // in a bold face at cover scale competes with the real title beside it.
  const Font& bf = fonts[Role::Value];
  const std::string stripTitle = upperAscii(title);
  const int stripH = bf.lineHeight() + 10;
  const int stripY = y + kCoverH - stripH - 2;
  fb.fillRect(x + 2, stripY, kCoverW - 4, stripH, true);
  fb.fillRect(x + 2, stripY, kCoverW - 4, 2, false);
  drawText(fb, bf, x + 10, stripY + stripH - 8, stripTitle);
}
}  // namespace

void QuietTheme::renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm) {
  fb.clear(true);
  int y = drawHeaderBand(fb, fonts, "NOW READING", std::to_string(vm.batteryPercent) + "%");

  // Two columns: cover on the left, the reading state stacked on the right.
  y += 28;
  drawCoverPlaceholder(fb, fonts, kMargin, y, vm.title);

  const int rightX = kMargin + kCoverW + kGutter;
  const Font& title = fonts[Role::Title];
  const Font& body = fonts[Role::Body];
  const Font& meta = fonts[Role::Meta];
  int ry = y + title.ascent();
  drawText(fb, title, rightX, ry, vm.title);
  ry += body.lineHeight() + 6;
  drawText(fb, body, rightX, ry, vm.author);

  // The percentage is the one display-scale number on the screen: 44px against
  // the title's 24, so it outranks the book's name instead of tying with it.
  ry = y + kCoverH - meta.lineHeight() * 2 - 8;
  drawText(fb, fonts[Role::Display], rightX, ry, std::to_string(vm.percent) + "%");
  ry += meta.lineHeight() + 4;
  drawText(fb, meta, rightX, ry,
           "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount),
           Ink::Black, kLabelTracking);
  ry += meta.lineHeight() + 2;
  drawText(fb, meta, rightX, ry, vm.chapterLabel, Ink::Black, kLabelTracking);

  y += kCoverH + 22;

  // Progress bar spans the usable width.
  const int barW = fb.width() - 2 * kMargin;
  fb.fillRect(kMargin, y, barW, 8, false);
  fb.fillRect(kMargin + 1, y + 1, barW - 2, 6, true);
  const int clamped = vm.percent < 0 ? 0 : (vm.percent > 100 ? 100 : vm.percent);
  fb.fillRect(kMargin + 1, y + 1, (barW - 2) * clamped / 100, 6, false);
  y += 8 + 22;

  // Continue block: focused when no menu row is.
  const bool continueFocused = (vm.focusedMenuIndex < 0);
  const Font& label = fonts[Role::Label];
  if (continueFocused) {
    fb.fillRect(kMargin, y, barW, kBlockH, false);
  } else {
    fb.fillRect(kMargin, y, barW, 2, false);
    fb.fillRect(kMargin, y + kBlockH - 2, barW, 2, false);
    fb.fillRect(kMargin, y, 2, kBlockH, false);
    fb.fillRect(kMargin + barW - 2, y, 2, kBlockH, false);
  }
  const Ink cink = continueFocused ? Ink::White : Ink::Black;
  drawText(fb, label, kMargin + 20, y + kBlockH / 2 + label.ascent() / 2, "CONTINUE", cink,
           kLabelTracking);
  drawIcon(fb, icons::kChevron, kMargin + barW - 20 - icons::kChevron.w,
           y + kBlockH / 2 - icons::kChevron.h / 2, cink);

  // Menu rows sit above the hint bar.
  const int menuTop = fb.height() - kHintBarH - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    // A row states a quantity or discloses a screen, never both: the design gives
    // LIBRARY its count and SETTINGS a chevron. Keying the mark on an absent
    // value keeps that rule in the theme, where the design lives, rather than
    // adding a per-entry icon field the view model has no opinion about.
    const bool discloses = vm.menu[i].value.empty();
    drawRow(fb, fonts, menuTop + static_cast<int>(i) * kRowH, vm.menu[i].label, vm.menu[i].value,
            static_cast<int>(i) == vm.focusedMenuIndex, discloses ? &icons::kChevron : nullptr);
  }

  const Hint hints[4] = {{&icons::kBook, vm.hints[0], ""},
                         {&icons::kDot, vm.hints[1], ""},
                         {&icons::kUp, vm.hints[2], ""},
                         {&icons::kDown, vm.hints[3], ""}};
  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots);
}

}  // namespace reader
