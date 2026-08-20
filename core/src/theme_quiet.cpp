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

// Baseline of one text run, given the top of its CSS line box and that box's
// height. This is CSS half-leading: the font's ascent+descent is centred in the
// line box, so a box shorter than the glyphs (the board's `line-height: 1.05` on
// the title, `1` on the numeral) pulls the baseline up rather than letting the
// run sit flush to the top. Getting this right is what keeps the 67px numeral
// clear of the author line above it instead of butting into it.
int baselineIn(const Font& f, int boxTop, int boxH) {
  const int natural = f.ascent() - f.descent();
  return boxTop + (boxH - natural) / 2 + f.ascent();
}

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

  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()),
           "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount),
           Ink::Black, kLabelTracking, plane);
  ry += meta.lineHeight() + kMetaGap;

  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()), vm.chapterLabel, Ink::Black,
           kLabelTracking, plane);
  ry += meta.lineHeight();

  // The block is as tall as its taller column. The stats column now normally
  // wins, but keying off whichever is taller keeps a short view model (no
  // chapter label, a one-digit percentage) from letting the progress bar ride up
  // over the cover's bottom edge.
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
  drawText(fb, label, kMargin + 20, y + kBlockH / 2 + label.ascent() / 2, "CONTINUE", cink,
           kLabelTracking, plane);
  drawIcon(fb, icons::kChevron, kMargin + barW - 20 - icons::kChevron.w,
           y + kBlockH / 2 - icons::kChevron.h / 2, cink, plane);

  // Menu rows sit above the hint bar.
  const int menuTop = fb.height() - kHintBarH - static_cast<int>(vm.menu.size()) * kRowH;
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

  const Hint hints[4] = {{&icons::kBook, vm.hints[0], ""},
                         {&icons::kDot, vm.hints[1], ""},
                         {&icons::kUp, vm.hints[2], ""},
                         {&icons::kDown, vm.hints[3], ""}};
  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

}  // namespace reader
