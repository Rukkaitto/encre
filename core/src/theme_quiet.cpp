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

// --- design/SdMissing.dc.html ----------------------------------------------
//
// The board's full-screen prompt is one `justify-content: center` column above
// the hint bar, with a single `gap: 22px` between all four of its items and its
// own 260px-wide button. Nothing here is a height: the column's height is the
// sum of what is in it, and where it starts falls out of that.
constexpr int kPromptGap = 22;
constexpr int kPromptActionW = 260;

// NOTE: this theme used to carry its own copy of the half-leading baseline
// formula, and it was the *correct* copy while the shared primitives in
// components.cpp used a different, wrong one. That is the worst arrangement of
// the two: the theme's runs were centred and the shared boxes were not, so the
// screen was subtly inconsistent with itself and the bug was invisible to
// anyone reading either file alone. reader::baselineIn in core/src/text.cpp is
// now the only copy, and this theme is one of its callers like any other.

// --- design/Library.dc.html -------------------------------------------------
//
// The four slots' MARKS, in hardware order (Back, Confirm, Up, Down). Declared
// once because two functions need them for two different reasons: renderLibrary
// draws them, and libraryVisibleRows measures the bar they make. The labels come
// from the view-model and cannot change the bar's height -- a slot is one line of
// Meta whatever it says -- so the sizing path builds slots with no labels from
// the same array rather than repeating the marks with a copy of the strings.
constexpr const Icon* kLibraryMarks[4] = {&icons::kBack, &icons::kDot, &icons::kUp,
                                          &icons::kDown};

void libraryHints(const LibraryViewModel& vm, Hint out[4]) {
  for (int i = 0; i < 4; ++i) out[i] = {kLibraryMarks[i], vm.hints[i], vm.holds[i]};
}

// "12 BOOKS", and "1 BOOK". The board only ever shows the plural, so the
// singular is a decision rather than a transcription -- and a screen reading
// "1 BOOKS" is a defect nobody would defend.
std::string bookCountLabel(int n) {
  return std::to_string(n) + (n == 1 ? " BOOK" : " BOOKS");
}

// A dithered stand-in until Phase 3 decodes real cover images: a bordered panel,
// and nothing else. The board used to reverse the title out of a filled strip
// along the bottom; at the pt ramp's sizes that strip duplicated the title
// already set beside the cover and ran into the stats column, so the board
// dropped it and the cover is now a plain panel.
//
// The size is the caller's: Home draws 112x168 and Book details 120x180, both
// with the same 2px border and the same tint, so this takes the box rather than
// each screen growing its own copy of the border-and-dither.
void drawCoverPlaceholder(Framebuffer& fb, int x, int y, int w, int h) {
  // Level 1, not 2: the board's `.dither-dots` is a 4px-pitch radial-gradient
  // dot, roughly a fifth coverage. Level 2 is a 50% checkerboard, which reads as
  // grey mesh rather than a sparse tint.
  ditherRect(fb, x, y, w, h, 1);
  fb.fillRect(x, y, w, 2, false);
  fb.fillRect(x, y + h - 2, w, 2, false);
  fb.fillRect(x, y, 2, h, false);
  fb.fillRect(x + w - 2, y, 2, h, false);
}
}  // namespace

void QuietTheme::renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                            Plane plane) {
  fb.clear(true);
  int y = drawHeaderBand(fb, fonts, "NOW READING", std::to_string(vm.batteryPercent) + "%",
                             &icons::kBattery, plane);

  // Two columns: cover on the left, the reading state stacked on the right.
  y += kCoverTopGap;
  drawCoverPlaceholder(fb, kMargin, y, kCoverW, kCoverH);

  const int rightX = kMargin + kCoverW + kGutter;
  const Font& title = fonts[Role::Title700];
  // Body400, not Body500: the board's author line is `font-size: var(--t-body)`
  // with no font-weight, so it is CSS default 400. The ramp used to carry one
  // 29px face at 500 -- the weight the *other* --t-body runs ask for -- and this
  // line was drawn in it, measuring 19% over the board's ink. The role names the
  // weight now, so asking for the wrong one is a visible mistake in this line
  // rather than an invisible property of the asset.
  const Font& body = fonts[Role::Body400];
  const Font& meta = fonts[Role::Meta400];
  const Font& display = fonts[Role::Display700];

  // The stats column flows downward from its own top, with the board's gaps
  // between runs. It used to hang the numeral off the cover's *bottom* edge,
  // which worked only while the cover was the taller of the two columns: at the
  // pt ramp the column is ~239px against a 192px cover, so that anchor drove the
  // numeral up into the author line. Nothing here positions a run off kCoverH.
  int ry = y + kColPadTop;
  // The board sets the title in caps (text-transform: uppercase). Casing is a
  // presentation decision, so the theme applies it rather than the view-model
  // carrying a pre-shouted string.
  //
  // And it truncates, per the board's own `text-overflow: ellipsis`: the stats
  // column is the row's last flex item, so what the title has is everything from
  // the column's left edge to the screen margin. Derived from the box model and
  // not pinned, which is what keeps it right on both panels -- 304px on the X4
  // and 352 on the X3.
  drawTextElided(fb, title, rightX, baselineIn(title, ry, kTitleLineH), upperAscii(vm.title),
                 fb.width() - kMargin - rightX, Ink::Black, {}, plane);
  ry += kTitleLineH + kTitleAuthorGap;

  drawText(fb, body, rightX, baselineIn(body, ry, body.lineHeight()), vm.author, Ink::Black, {},
           plane);
  ry += body.lineHeight() + kGroupGap;

  // The percentage is the one display-scale run on the screen: 32pt against the
  // title's 20, so it outranks the book's name instead of tying with it.
  drawText(fb, display, rightX, baselineIn(display, ry, kDisplayLineH),
           std::to_string(vm.percent) + "%", Ink::Black, {}, plane);
  ry += kDisplayLineH + kMetaGap;

  // Two meta lines, two trackings: the board sets the page count at 0.16em and
  // the chapter label at 0.10em. They are not the same run.
  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()),
           "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount),
           Ink::Black, trackingEm(meta, kMetaEm), plane);
  ry += meta.lineHeight() + kMetaGap;

  drawText(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()), vm.chapterLabel, Ink::Black,
           trackingEm(meta, kTightMetaEm), plane);
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
  const Font& label = fonts[Role::Label500];
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
  drawText(fb, label, kMargin + kBlockPadX, cbase, "CONTINUE", cink,
           trackingEm(label, kBlockLabelEm), plane);
  // kForward, not kChevron: the board draws a 32x25 long arrow with a shaft here
  // (`M1 7h15M11 1l6 6-6 6`), and the 25x25 chevron this used to draw is the
  // *menu row's* disclosure -- a different mark for a different job. An action
  // block proceeds; a row discloses.
  const Icon& mark = icons::kForward;
  drawIcon(fb, mark, kMargin + barW - kBlockPadX - mark.w, iconTopIn(y, kBlockH, mark.h), cink,
           plane);

  // The ring comes from the view model, not from this function: a slot shows a
  // hold mark if and only if the screen bound a long-press to that button.
  const Hint hints[4] = {{&icons::kBook, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};

  // Menu rows sit above the hint bar, so the bar's height decides where they
  // start. That height is the bar's to compute -- from its own padding and its
  // own content -- and asking it is what keeps this stacking correct when a
  // screen sets its hints in a larger role or pairs them with a taller mark. A
  // constant here would be a second, private copy of the bar's box model.
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

void QuietTheme::renderSdMissing(Framebuffer& fb, const FontSet& fonts,
                                 const SdMissingViewModel& vm, Plane plane) {
  fb.clear(true);

  // A slot's mark follows its label, because on the board only the button that
  // does something carries one: three of the four slots are the empty 36px
  // placeholder. A mark over a slot with no label would be an affordance for an
  // action that is not there, and it would measure 32px where the board measures
  // 36 and shift every other slot along.
  const Icon* const marks[4] = {&icons::kBack, &icons::kDot, &icons::kUp, &icons::kDown};
  Hint hints[4];
  for (int i = 0; i < 4; ++i)
    hints[i] = {vm.hints[i].empty() ? nullptr : marks[i], vm.hints[i], vm.holds[i]};

  // `flex-grow: 1` on the column: it takes everything the hint bar leaves. Asking
  // the bar rather than assuming a height is what makes this correct on both
  // panels and on a bar whose content changes.
  const int areaH = fb.height() - hintBarHeight(fonts, hints);
  const int usableW = fb.width() - 2 * kMargin;

  const Icon& mark = icons::kSdCard;
  const Font& title = fonts[Role::Title700];
  // Body400, not Body500: the board's paragraph is `font-size: var(--t-body)`
  // with no font-weight, so it is CSS default 400 -- the same distinction that
  // had Home's author line rendering 19% over the board's ink.
  const Font& body = fonts[Role::Body400];
  const Tracking titleTracking = trackingEm(title, kPromptTitleEm);

  // The paragraph is wrapped before anything is placed, because its height is
  // what it wraps to and the whole column is centred on that total. `max-width`
  // is a maximum: on a narrower panel the screen margins win.
  const int colW = usableW < kProseMaxW ? usableW : kProseMaxW;
  const int colX = centreIn(kMargin, usableW, colW);
  const Prose prose = wrapProse(body, vm.message, colW, kProseLeadEm);

  // The column's own height, and then its top: three gaps, the mark, one line of
  // title, the paragraph, the button. In 1/64 px because the paragraph's height
  // is a fraction -- 1.55 x 29px is 44.95 -- and rounding it before halving the
  // free space would put the whole column half a pixel off centre for no reason.
  const int stackF26 =
      pxToF26(mark.h + title.lineHeight() + kActionH + 3 * kPromptGap) + prose.heightF26();
  // Arithmetic shift rather than / 2, for the reason baselineInF26 uses one: the
  // fraction is kept and the ONE rounding happens where the value is painted, and
  // a shift halves a column taller than its area the same way it halves one that
  // fits instead of truncating toward the origin. Identical on every positive
  // value, so no screen moves today; it stops being identical exactly when a
  // prompt outgrows the space above the hint bar.
  int yF26 = (pxToF26(areaH) - stackF26) >> 1;

  drawIcon(fb, mark, centreIn(kMargin, usableW, mark.w), f26ToPx(yF26), Ink::Black, plane);
  yF26 += pxToF26(mark.h + kPromptGap);

  drawText(fb, title, centreIn(kMargin, usableW, title.measure(vm.title, titleTracking)),
           baselineInF26(title, yF26, pxToF26(title.lineHeight())), vm.title, Ink::Black,
           titleTracking, plane);
  yF26 += pxToF26(title.lineHeight() + kPromptGap);

  yF26 += drawProse(fb, body, prose, colX, colW, yF26, Ink::Black, plane);
  yF26 += pxToF26(kPromptGap);

  // Filled: SdMissing's board draws the one variant, and RETRY is the only thing
  // on the screen a focus could be on.
  drawActionButton(fb, fonts, centreIn(kMargin, usableW, kPromptActionW), f26ToPx(yF26),
                   kPromptActionW, vm.action, /*filled=*/true, plane);

  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

int QuietTheme::libraryVisibleRows(int panelH, const FontSet& fonts) const {
  // Labelless slots: only the marks and the type role can change a bar's height,
  // and both are the same here as in renderLibrary. An empty LABEL narrows a
  // slot, which moves the others along the bar -- but this function is only
  // asking how tall the bar is.
  Hint hints[4];
  for (int i = 0; i < 4; ++i) hints[i] = {kLibraryMarks[i], "", false};
  // No mark on this band, so headerBandHeight is asked for the band the Library
  // actually draws rather than for Home's.
  const int area = panelH - headerBandHeight(fonts, nullptr) - hintBarHeight(fonts, hints);
  const int row = bookRowHeight(fonts);
  if (area <= 0 || row <= 0) return 0;
  // Divided by the RULED height, which is the pitch of every row but the last:
  // dividing by the 1px-shorter unruled one would claim room for a row that only
  // the bottom of the list has.
  return area / row;
}

void QuietTheme::renderLibrary(Framebuffer& fb, const FontSet& fonts, const LibraryViewModel& vm,
                               Plane plane) {
  fb.clear(true);
  // No battery on this band: the board draws `LIBRARY` and a book count, and
  // nothing else. Home's is the screen with the charge reading.
  const int listTop = drawHeaderBand(fb, fonts, vm.title, bookCountLabel(vm.bookCount), nullptr, plane);
  int y = listTop;

  const int rows = static_cast<int>(vm.rows.size());
  // ONE condition for the rail and the gutter, read from one place: a gutter
  // without a rail is a white strip, and a rail without a gutter is a thumb the
  // focused row swallows. drawScrollRail refuses the same case independently, so
  // the two cannot disagree even if a caller gets this wrong.
  const bool overflowing = vm.totalRows > rows;
  for (int i = 0; i < rows; ++i) {
    const LibraryRow& row = vm.rows[static_cast<size_t>(i)];
    const bool focused = (i == vm.focusedRow);
    // The board gives every row a `border-bottom` EXCEPT the focused one, whose
    // fill runs to the next row's top edge, and the last one drawn, where it
    // leaves the list's bottom edge open rather than hanging a hairline over the
    // slack above the hint bar. So `y` advances by what the row actually
    // consumed, which is 1px less without a rule -- the board's own pitch, which
    // genuinely varies.
    const bool rule = !focused && i != rows - 1;
    // The gutter exists only when the rail does, so a list that fits runs its
    // rows to the panel edge -- focused fill included. See kListGutterW.
    y += drawBookRow(fb, fonts, y, {row.title, row.meta, row.value, row.isFolder}, focused,
                     rule, plane, overflowing ? kListGutterW : 0);
  }

  Hint hints[4];
  libraryHints(vm, hints);

  // The rail spans the LIST, not the panel: a track running the full height would
  // claim the header band and the hint bar scroll, which they do not. `listTop` is
  // the band's bottom edge drawHeaderBand already returned, and the bottom is
  // DERIVED from the bar rather than pinned -- hintBarHeight needs the hints, so
  // they are built before the rail rather than after it.
  drawScrollRail(fb, listTop, fb.height() - hintBarHeight(fonts, hints), vm.firstRow, rows,
                 vm.totalRows, plane);

  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

namespace {
// --- The overlay panels' own widths ------------------------------------------
//
// Each board's own number, and they are NOT the same: LibraryActions is 340 on
// the 480 canvas and DeleteConfirm is 380. Fixed in pixels rather than a
// fraction of the canvas, for the reason kMargin is: the two panels are within
// ~2% of the same PPI, so a panel should be the same physical size on both, and
// what adapts is where it sits -- panelLeft centres it on whatever the canvas
// is.
constexpr int kActionsPanelW = 340;
constexpr int kConfirmPanelW = 380;

// design/DeleteConfirm.dc.html's own numbers, below its caption: the paragraph's
// `padding: 18px 20px` and `line-height: 1.45`, then the button column's
// `gap: 12px` and its `padding: 0 20px 20px 20px`.
constexpr int kConfirmProsePadY = 18;
constexpr int kConfirmProseLeadEm = 1450;
constexpr int kConfirmButtonGap = 12;
constexpr int kConfirmButtonPadBottom = 20;

// --- design/BookDetails.dc.html ---------------------------------------------
//
// Its cover is bigger than Home's -- 120x180 against 112x168 -- because it is the
// subject of the screen rather than a thumbnail beside a stats column. The rest
// is the board's own box model: the block's `padding: 24px 24px 20px 24px` and
// `gap: 20px`, the text column's `padding-top: 4px` and `gap: 6px`, the
// `line-height: 1.1` on the title, and the `border-top: 2px` above the fields.
constexpr int kDetailsCoverW = 120;
constexpr int kDetailsCoverH = 180;
constexpr int kDetailsPadTop = 24;
constexpr int kDetailsPadBottom = 20;
constexpr int kDetailsGutter = 20;
constexpr int kDetailsColPadTop = 4;
constexpr int kDetailsColGap = 6;
constexpr int kDetailsTitleLineH = 46;  // round(1.1 * 42)
constexpr int kDetailsRuleH = 2;

// The strip at the bottom that an overlay repaints. The overlay boards draw
// their hint bar OVER the veil with `background: #ffffff`, so the parent's bar --
// which App::render has already painted, with the parent's own labels on it -- is
// covered rather than veiled. Anything less would leave two bars' worth of ink
// on one line.
void drawOverlayHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], Plane plane) {
  const int barH = hintBarHeight(fonts, hints);
  fb.fillRect(0, fb.height() - barH, fb.width(), barH, true);
  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}
}  // namespace

void QuietTheme::renderItemActions(Framebuffer& fb, const FontSet& fonts,
                                   const ItemActionsViewModel& vm, Plane plane) {
  // NO fb.clear(): the Library underneath has already been painted by
  // App::render, and this screen's whole job is to be in front of it.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kActionsPanelW);
  // This caption IS the book's name, and the board truncates it on one line
  // (`white-space: nowrap; text-overflow: ellipsis`) rather than wrapping it. The
  // reason is the panel: it is a list of actions on one item, and a name allowed
  // to wrap makes the panel a different height for every book -- which on this
  // screen also means a different partial-repaint footprint for every book.
  //
  // So the name is elided to the caption's column FIRST, less what the status
  // value on its right takes and the board's `gap: 7px`, and then wrapped -- to
  // the one line it now fits on. It still goes through the wrap rather than
  // around it because the panel's height is measured off a Prose, and two paths
  // into that measurement is how the caption and the panel come to disagree.
  const Font& capValueFont = fonts[Role::Meta400];
  const int statusW =
      vm.status.empty() ? 0
                        : capValueFont.measure(vm.status, trackingEm(capValueFont, kHintEm)) +
                              kBandGap;
  const std::string caption = elideToWidth(fonts[Role::Label500], upperAscii(vm.title),
                                           panelCaptionColumnW(contentW) - statusW,
                                           trackingEm(fonts[Role::Label500], kBandLabelEm));
  const Prose label = wrapPanelCaption(fonts, caption, contentW);

  const int rows = static_cast<int>(vm.actions.size());
  int rowsH = 0;
  for (int i = 0; i < rows; ++i)
    rowsH += panelRowHeight(i != vm.focusedAction && i != rows - 1);
  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) + rowsH;

  const int x = panelLeft(fb.width(), kActionsPanelW);
  // `top: 50%; transform: translateY(-50%)` -- centred on the SCREEN, not on the
  // area above the hint bar (spec 4.1c). The bar is drawn over the veil after
  // this, and the panel does not reach it.
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kActionsPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, vm.status, plane);
  for (int i = 0; i < rows; ++i) {
    const ItemActionEntry& row = vm.actions[static_cast<size_t>(i)];
    const bool focused = (i == vm.focusedAction);
    // The board's rule again: every row has a `border-bottom` except the focused
    // one, whose fill runs to the next row's edge, and the last one, where the
    // panel's own border closes the list.
    cy += drawPanelRow(fb, fonts, cx, cy, contentW, row.label, focused, row.discloses,
                       !focused && i != rows - 1, plane);
  }

  const Hint hints[4] = {{&icons::kBack, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};
  drawOverlayHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderDeleteConfirm(Framebuffer& fb, const FontSet& fonts,
                                     const DeleteConfirmViewModel& vm, Plane plane) {
  // No fb.clear(), as with the actions overlay: the parent is already painted.
  //
  // Worth knowing why this looks like the board even though the board draws the
  // LIBRARY behind it while the real stack has the actions panel in between: the
  // confirm panel is 380 wide against the actions panel's 340 and taller than it
  // on both geometries, and both are centred -- so it covers the actions panel
  // completely. The two renders are the same pixels, and the board is not
  // simplifying anything.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kConfirmPanelW);
  const int colW = contentW - 2 * kPanelPadX;
  const Font& body = fonts[Role::Body400];
  // This caption is the one run on any board that is a SENTENCE with a name
  // inside it rather than a name, and it is the one that keeps wrapping: an
  // ellipsis here would eat the closing quote and the question mark, and a
  // confirmation that no longer reads as a question is one people dismiss without
  // reading. Wrapping also shows the whole name, which an ellipsis cannot, and
  // naming the file is what this prompt is for.
  //
  // What it gains from the board is `overflow-wrap: anywhere`: a filename is
  // frequently one unbreakable word, and a word wider than the column would
  // otherwise hang out past the panel's own border.
  std::string captionTail;
  Prose label = wrapPanelCaption(fonts, vm.title, contentW, WordBreak::Anywhere);
  // The paragraph, wrapped before anything is placed: its height is what it wraps
  // to, and everything below it -- both buttons and the panel's own bottom edge --
  // hangs off that. Wrapping it twice would be two chances to disagree.
  const Prose prose = wrapProse(body, vm.message, colW, kConfirmProseLeadEm);

  const int proseH = f26ToPx(prose.heightF26());
  // The board's `max-height: 100%; overflow: hidden` on the panel, in the only
  // form a firmware can honour it: the caption is the one part of this panel whose
  // height is unbounded, so it is the part that yields. Everything else in the
  // panel is fixed once the paragraph is wrapped, so subtract it and divide the
  // rest by the caption's line box -- derived from the canvas, never pinned, and
  // 13 lines on the X4 against 12 on the X3, which no ordinary name comes near.
  // A caption clipped by the panel's border would be worse than an ellipsis, and
  // a panel taller than the glass -- centred, so cut off at BOTH ends -- worse
  // still.
  const int panelFixedH = 2 * kPanelBorder + 2 * kPanelCaptionPadY + kPanelCaptionRuleH +
                          (2 * kConfirmProsePadY + proseH) +
                          (2 * kActionH + kConfirmButtonGap + kConfirmButtonPadBottom);
  const int captionRoom = fb.height() - panelFixedH;
  int maxCaptionLines = 1;
  while (maxCaptionLines < label.lineCount() &&
         f26ToPx((maxCaptionLines + 1) * label.leadF26) <= captionRoom)
    ++maxCaptionLines;
  clampProse(fonts[Role::Label500], label, maxCaptionLines, panelCaptionColumnW(contentW),
             captionTail);

  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) +
                     (kConfirmProsePadY + proseH + kConfirmProsePadY) +
                     (2 * kActionH + kConfirmButtonGap + kConfirmButtonPadBottom);

  const int x = panelLeft(fb.width(), kConfirmPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kConfirmPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, "", plane);

  // Left-aligned: the board's paragraph declares no `text-align`, so it is a
  // plain block -- unlike a full-screen prompt's, which is centred.
  cy += kConfirmProsePadY;
  cy += f26ToPx(drawProse(fb, body, prose, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black, plane,
                          ProseAlign::Left));
  cy += kConfirmProsePadY;

  // The focused slab is the filled one and the other is outlined, which is the
  // boards' rule wherever they pair the two. The focus starts on CANCEL, so a
  // press that arrives before the user has read anything cancels.
  drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.cancelLabel, vm.focusedAction == 0,
                   plane);
  cy += kActionH + kConfirmButtonGap;
  drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.confirmLabel, vm.focusedAction == 1,
                   plane);

  const Hint hints[4] = {{&icons::kBack, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};
  drawOverlayHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderBookDetails(Framebuffer& fb, const FontSet& fonts,
                                   const BookDetailsViewModel& vm, Plane plane) {
  // A whole screen, so it clears -- Book details is NOT an overlay, whatever the
  // unused `.dim-veil` rule in its stylesheet suggests.
  fb.clear(true);
  // No mark on this band either: `ABOUT THIS BOOK` and the file's format.
  // The label is the board's literal, as renderHome's `NOW READING` is: it names
  // the screen rather than its content, so it does not vary and the view-model
  // has no opinion about it. What varies beside it is the file's format.
  const int bandH = drawHeaderBand(fb, fonts, "ABOUT THIS BOOK", vm.format, nullptr, plane);
  int y = bandH;

  // The cover and the title column, `align-items: flex-start` -- so the column
  // starts at the block's top rather than being centred against a cover more than
  // twice its height.
  y += kDetailsPadTop;
  drawCoverPlaceholder(fb, kMargin, y, kDetailsCoverW, kDetailsCoverH);

  const Font& title = fonts[Role::Title700];
  // Body400 and Label400: both runs are `font-size: var(--t-...)` with no
  // font-weight, so both are CSS default 400. This is the distinction that had
  // Home's author line rendering 19% over the board's ink.
  const Font& author = fonts[Role::Body400];
  const Font& subtitle = fonts[Role::Label400];

  // The bar and the rows are measured before the title is laid out, because they
  // are what decides how many lines the title may have. Building the hints here
  // rather than at the end is that: a slot's mark follows its LABEL, so the three
  // dead buttons get neither, and the bar's height follows its content.
  const Icon* const marks[4] = {&icons::kBack, &icons::kDot, &icons::kUp, &icons::kDown};
  Hint hints[4];
  for (int i = 0; i < 4; ++i)
    hints[i] = {vm.hints[i].empty() ? nullptr : marks[i], vm.hints[i], vm.holds[i]};

  const int rows = static_cast<int>(vm.fields.size());
  int rowsH = 0;
  for (int i = 0; i < rows; ++i) rowsH += detailRowHeight(i != rows - 1);

  const int colX = kMargin + kDetailsCoverW + kDetailsGutter;
  const int colW = fb.width() - colX - kMargin;
  int cy = y + kDetailsColPadTop;

  // THE ONE SCREEN WHERE A LONG NAME WRAPS instead of truncating. This is the page
  // about the book, so its name is the content: an ellipsis here hides the thing
  // the reader opened the screen to read, where on a list row it hides only which
  // of seven rows this is. The board says so -- `overflow-wrap: anywhere` on this
  // run and on no other title -- and the break has to be allowed inside a word
  // because a filename is usually one word.
  //
  // The wrap is bounded, and this is the "what gives" answer: spec 4.1b makes Book
  // details a fixed single-screen summary, so the field rows and the hint bar do
  // not move and the NAME yields. The bound is derived -- the room the block has
  // is the canvas less the band, the block's own padding, the rule, the rows and
  // the bar; the column's other two lines and its padding are fixed; what is left,
  // divided by the title's line box, is how many lines the title may have. That is
  // 3 on both panels today (235px of block room on the X4, 227 on the X3, against
  // 82px of fixed column and a 46px line box), and it is 3 rather than 2 because
  // the cover is 180px tall and absorbs the first two lines for free -- a 1- or
  // 2-line title moves nothing on this screen at all.
  const int blockRoom = fb.height() - (bandH + kDetailsPadTop) -
                        (kDetailsPadBottom + kDetailsRuleH + rowsH + hintBarHeight(fonts, hints));
  const int columnFixedH = kDetailsColPadTop + kDetailsColGap + author.lineHeight() +
                           kDetailsColGap + subtitle.lineHeight();
  int maxTitleLines = (blockRoom - columnFixedH) / kDetailsTitleLineH;
  if (maxTitleLines < 1) maxTitleLines = 1;

  // The title's line box is the board's `line-height: 1.1`, tighter than the
  // face's own -- which is what stops a 20pt name opening a crater above the
  // author. Passed as a lead rather than an em multiple for that reason: 1.1 is
  // the board's number and 46 is what it resolves to, and wrapProseLead is the
  // form that takes a line box the board tightened by hand.
  std::string titleTail;
  Prose titleProse =
      wrapProseLead(title, vm.title, colW, pxToF26(kDetailsTitleLineH), {}, WordBreak::Anywhere);
  clampProse(title, titleProse, maxTitleLines, colW, titleTail);
  // Left-aligned, and one line is bit-identical to the drawText this replaced:
  // drawProse's first baseline is baselineInF26(font, pxToF26(cy),
  // pxToF26(kDetailsTitleLineH)), which is baselineIn's own definition.
  cy += f26ToPx(drawProse(fb, title, titleProse, colX, colW, pxToF26(cy), Ink::Black, plane,
                          ProseAlign::Left));
  cy += kDetailsColGap;
  drawText(fb, author, colX, baselineIn(author, cy, author.lineHeight()), vm.author, Ink::Black,
           {}, plane);
  cy += author.lineHeight() + kDetailsColGap;
  drawText(fb, subtitle, colX, baselineIn(subtitle, cy, subtitle.lineHeight()), vm.subtitle,
           Ink::Black, {}, plane);
  cy += subtitle.lineHeight();

  // The block is as tall as its taller column plus the block's own bottom
  // padding. Keyed on whichever is taller rather than on the cover, so a book
  // with a long title -- or Phase 3's wrapped one -- pushes the fields down
  // instead of running into them.
  const int coverBottom = y + kDetailsCoverH;
  y = (cy > coverBottom ? cy : coverBottom) + kDetailsPadBottom;

  fb.fillRect(0, y, fb.width(), kDetailsRuleH, false);
  y += kDetailsRuleH;

  for (int i = 0; i < rows; ++i) {
    // Nothing is focused on this screen -- there is nothing to select, which is
    // why three of its four hint slots are the boards' dead-button placeholder.
    // The last row has no rule, as the board's last row has no border-bottom.
    y += drawDetailRow(fb, fonts, y, vm.fields[static_cast<size_t>(i)].label,
                       vm.fields[static_cast<size_t>(i)].value, false, i != rows - 1, plane);
  }

  // `margin-top: auto` above the bar on the board: the slack is whatever is left,
  // and the bar sits on the bottom edge. Its hints were built above, because the
  // title's line budget is measured against the room this bar leaves.
  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

void QuietTheme::renderStub(Framebuffer& fb, const FontSet& fonts, const StubViewModel& vm,
                            Plane plane) {
  fb.clear(true);
  // Built only from primitives already matched to boards -- header band, rows,
  // hint bar. Nothing here invents a measurement, so this surface cannot
  // introduce a fidelity defect the real screens would inherit.
  int y = drawHeaderBand(fb, fonts, vm.title, std::to_string(vm.batteryPercent) + "%",
                             &icons::kBattery, plane);

  const Font& meta = fonts[Role::Meta400];
  y += kMargin;
  drawText(fb, meta, kMargin, baselineIn(meta, y, meta.lineHeight()), vm.note, Ink::Black,
           trackingEm(meta, kBandLabelEm), plane);
  y += meta.lineHeight() + kMargin;

  const Hint hints[4] = {{&icons::kBack, vm.hints[0], vm.holds[0]},
                         {&icons::kDot, vm.hints[1], vm.holds[1]},
                         {&icons::kUp, vm.hints[2], vm.holds[2]},
                         {&icons::kDown, vm.hints[3], vm.holds[3]}};
  const int barTop = fb.height() - hintBarHeight(fonts, hints);

  for (size_t i = 0; i < vm.lines.size(); ++i) {
    const int rowY = y + static_cast<int>(i) * kRowH;
    // Clip against the hint bar rather than drawing under it. The real screens
    // scroll; this one just stops, which is honest for a placeholder.
    if (rowY + kRowH > barTop) break;
    drawRow(fb, fonts, rowY, vm.lines[i], "", static_cast<int>(i) == vm.focusedLine, nullptr,
            plane);
  }

  int slots[4] = {};
  drawHintBar(fb, fonts, hints, slots, plane);
}

}  // namespace reader
