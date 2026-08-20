#include "reader/theme_quiet.h"

#include <string>

#include "reader/components.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
// The board's own numbers. The cover shrank from 156x234 to 128x192 when the pt
// type ramp landed: at legible sizes the stats column beside it needs the width,
// and a 234px-tall cover no longer bounded that column's height anyway.
constexpr int kCoverW = 112;
constexpr int kCoverH = 168;
constexpr int kGutter = 16;
constexpr int kBlockH = 72;
// The action block's own `padding: 0 20px`, which insets its label and its mark
// from the block's edges rather than from the screen margin.
constexpr int kBlockPadX = 20;
// Vertical rhythm, all straight off the board: the gap under the header band
// (`padding: 30px 24px 0`), the same 26px lead-in the progress bar and the
// CONTINUE block each get, and the bar's own height.
constexpr int kCoverTopGap = 30;
constexpr int kBlockGap = 26;
constexpr int kBarH = 8;
// The stats column's internal gaps: 2px of column padding, 5px between title
// and author, 22px between that group and the progress group, 4px between the
// numeral and each meta line.
constexpr int kColPadTop = 2;
constexpr int kTitleAuthorGap = 5;
constexpr int kGroupGap = 22;
constexpr int kMetaGap = 4;
// The two line boxes the board tightens below the font's natural line height:
// `line-height: 1.05` on the 42px title and `1` on the 67px numeral. Spelled out
// in pixels like every other number here, because a Font reports ascent and
// descent but not its own ppem, so 1.05em is not derivable from it. Both are
// shorter than the glyphs they hold, which is the point: it stops a 20pt title
// and a 32pt numeral from opening craters in the column.
constexpr int kTitleLineH = 44;    // round(1.05 * 42)
constexpr int kDisplayLineH = 67;  // 1.00 * 67

// ASCII-only uppercase, local to the theme. The design sets `text-transform:
// uppercase` on the title. A general Unicode case mapping is not something
// core/ should carry for one label, and the titles that need it (accented Latin,
// Greek, Cyrillic) arrive with real metadata in Phase 3 -- non-ASCII bytes are
// passed through untouched rather than mangled.
std::string upperAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return out;
}

// NOTE: this theme used to carry its own copy of the half-leading baseline
// formula, and it was the *correct* copy while the shared primitives in
// components.cpp used a different, wrong one. That is the worst arrangement of
// the two: the theme's runs were centred and the shared boxes were not, so the
// screen was subtly inconsistent with itself and the bug was invisible to
// anyone reading either file alone. reader::baselineIn in core/src/text.cpp is
// now the only copy, and this theme is one of its callers like any other.

// A dithered stand-in until Phase 3 decodes real cover images: a bordered panel,
// and nothing else. The board used to reverse the title out of a filled strip
// along the bottom; at the pt ramp's sizes that strip duplicated the title
// already set beside the cover and ran into the stats column, so the board
// dropped it and the cover is now a plain panel.
void drawCoverPlaceholder(Framebuffer& fb, int x, int y) {
  // Level 1, not 2: the board's `.dither-dots` is a 4px-pitch radial-gradient
  // dot, roughly a fifth coverage. Level 2 is a 50% checkerboard, which reads as
  // grey mesh rather than a sparse tint.
  ditherRect(fb, x, y, kCoverW, kCoverH, 1);
  fb.fillRect(x, y, kCoverW, 2, false);
  fb.fillRect(x, y + kCoverH - 2, kCoverW, 2, false);
  fb.fillRect(x, y, 2, kCoverH, false);
  fb.fillRect(x + kCoverW - 2, y, 2, kCoverH, false);
}
}  // namespace

void QuietTheme::renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                            Plane plane) {
  fb.clear(true);
  int y = drawHeaderBand(fb, fonts, "NOW READING", std::to_string(vm.batteryPercent) + "%", plane);

  // Two columns: cover on the left, the reading state stacked on the right.
  y += kCoverTopGap;
  drawCoverPlaceholder(fb, kMargin, y);

  const int rightX = kMargin + kCoverW + kGutter;
  const Font& title = fonts[Role::Title];
  const Font& body = fonts[Role::Body];
  const Font& meta = fonts[Role::Meta];
  const Font& display = fonts[Role::Display];

  // The stats column flows downward from its own top, with the board's gaps
  // between runs. It used to hang the numeral off the cover's *bottom* edge,
  // which worked only while the cover was the taller of the two columns: at the
  // pt ramp the column is ~239px against a 192px cover, so that anchor drove the
  // numeral up into the author line. Nothing here positions a run off kCoverH.
  int ry = y + kColPadTop;
  // The board sets the title in caps (text-transform: uppercase). Casing is a
  // presentation decision, so the theme applies it rather than the view-model
  // carrying a pre-shouted string.
  drawText(fb, title, rightX, baselineIn(title, ry, kTitleLineH), upperAscii(vm.title), Ink::Black,
           0, plane);
  ry += kTitleLineH + kTitleAuthorGap;

  drawText(fb, body, rightX, baselineIn(body, ry, body.lineHeight()), vm.author, Ink::Black, 0,
           plane);
  ry += body.lineHeight() + kGroupGap;

  // The percentage is the one display-scale run on the screen: 32pt against the
  // title's 20, so it outranks the book's name instead of tying with it.
  drawText(fb, display, rightX, baselineIn(display, ry, kDisplayLineH),
           std::to_string(vm.percent) + "%", Ink::Black, 0, plane);
  ry += kDisplayLineH + kMetaGap;

  // Two meta lines, two trackings: the board sets the page count at 0.16em and
  // the chapter label at 0.10em. They are not the same run.
  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()),
           "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount),
           Ink::Black, kMetaTracking, plane);
  ry += meta.lineHeight() + kMetaGap;

  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()), vm.chapterLabel, Ink::Black,
           kTightMetaTracking, plane);
  ry += meta.lineHeight();

  // The block is as tall as its taller column. The stats column now normally
  // wins, but keying off whichever is taller keeps a short view model (no
  // chapter label, a one-digit percentage) from letting the progress bar ride up
  // over the cover's bottom edge.
  // The board gives the stats column `padding: 2px 0` -- both edges, not just the
  // top -- and that column is the taller of the two, so it sets the section's
  // height. Omitting the bottom padding lands everything below it 2px high.
  ry += kColPadTop;
  const int coverBottom = y + kCoverH;
  y = (ry > coverBottom ? ry : coverBottom) + kBlockGap;

  // Progress bar spans the usable width.
  const int barW = fb.width() - 2 * kMargin;
  fb.fillRect(kMargin, y, barW, kBarH, false);
  fb.fillRect(kMargin + 1, y + 1, barW - 2, kBarH - 2, true);
  const int clamped = vm.percent < 0 ? 0 : (vm.percent > 100 ? 100 : vm.percent);
  fb.fillRect(kMargin + 1, y + 1, (barW - 2) * clamped / 100, kBarH - 2, false);
  y += kBarH + kBlockGap;

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
  // The block is an `align-items: center` flex row on the board, so its label
  // and its mark are placed by the same two shared helpers every other box uses
  // -- there is nothing about a 72px action block that makes it a special case.
  const int cbase = baselineIn(label, y, kBlockH);
  drawText(fb, label, kMargin + kBlockPadX, cbase, "CONTINUE", cink, kBlockLabelTracking, plane);
  // kForward, not kChevron: the board draws a 32x25 long arrow with a shaft here
  // (`M1 7h15M11 1l6 6-6 6`), and the 25x25 chevron this used to draw is the
  // *menu row's* disclosure -- a different mark for a different job. An action
  // block proceeds; a row discloses.
  const Icon& mark = icons::kForward;
  drawIcon(fb, mark, kMargin + barW - kBlockPadX - mark.w, iconTopFor(label, cbase, mark.h), cink,
           plane);

  const Hint hints[4] = {{&icons::kBook, vm.hints[0], ""},
                         {&icons::kDot, vm.hints[1], ""},
                         {&icons::kUp, vm.hints[2], ""},
                         {&icons::kDown, vm.hints[3], ""}};

  // Menu rows sit above the hint bar, so the bar's height decides where they
  // start. That height is the bar's to compute -- from its own padding and its
  // own content -- and asking it is what keeps this stacking correct when a bar
  // gains a hold line or a screen sets its hints in a larger role. A constant
  // here would be a second, private copy of the bar's box model.
  const int menuTop =
      fb.height() - hintBarHeight(fonts, hints) - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    // A row states a quantity or discloses a screen, never both: the design gives
    // LIBRARY its count and SETTINGS a chevron. Keying the mark on an absent
    // value keeps that rule in the theme, where the design lives, rather than
    // adding a per-entry icon field the view model has no opinion about.
    const bool discloses = vm.menu[i].value.empty();
    drawRow(fb, fonts, menuTop + static_cast<int>(i) * kRowH, vm.menu[i].label, vm.menu[i].value,
            static_cast<int>(i) == vm.focusedMenuIndex, discloses ? &icons::kChevron : nullptr,
            plane);
  }

  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

}  // namespace reader
