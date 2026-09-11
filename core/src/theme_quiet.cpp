#include "reader/theme_quiet.h"

#include <string>

#include "reader/components.h"
#include "reader/dither.h"
#include "reader/framebuffer.h"
#include "reader/screen_book_end.h"  // BookEndScreen::kFinish / kLeave
#include "reader/screen_sleep.h"  // CoverSource
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
// HOME HAS NO COVER. `kCoverW`/`kCoverH` (112x168) and `kGutter` (16) were the
// board's numbers for a `.dither-dots` panel standing in for a cover image, and
// design/Main.dc.html has dropped it -- so they are gone rather than left with no
// reader, which is the shape this project has found twice from two directions
// (ListRow::trackingEm1000, readerBookTitle_). The reading column now takes the
// whole content width; `kCoverTopGap` stays, because it is the block's own top
// padding and not the cover's.
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

// design/BatteryEmpty.dc.html's paragraph says `max-width: 400px` and means it, and
// that was CHECKED IN BOTH ENGINES rather than assumed -- which is the whole point of
// the note on kProseMaxW, where SdMissing's 400 became 420 because its copy's three
// Chrome lines came out as four here. This copy wraps to FOUR lines at 400 in Chrome
// AND four here, at the same four breaks, so there is nothing to buy: the widths that
// would matter are the ones where the two engines disagree, and this is not one.
// Its own constant rather than kProseMaxW because the two boards state different
// numbers, and a board's max-width is a number to check rather than to share.
constexpr int kBatteryProseMaxW = 400;

// design/BookEnd.dc.html's own box model. Every one of these is a number the board
// STATES; the heights it COMPUTES -- the band's, the slabs', the hint bar's -- are
// derived from the primitives that draw them, never pinned. Pinning a computed
// height is the defect that put the header band 6px out, compounded a pixel a row
// down the menu, and gave the hint bar asymmetric padding.
constexpr int kBookEndBlockPadTop = 56;    // the content block's `padding-top`
constexpr int kBookEndGap = 12;            // `gap: 12px`, in the block and the slabs
constexpr int kBookEndSlabPadTop = 36;     // the slab block's `padding-top`
constexpr int kBookEndNotePadBottom = 14;  // the note's `padding-bottom`
constexpr int kBookEndTitleEm = 80;        // THE END's `letter-spacing: 0.08em`
constexpr int kBookEndMetaEm = 140;        // the meta line's `0.14em`
constexpr int kBookEndNoteLeadEm = 1500;   // the note's `line-height: 1.5`
constexpr int kBookEndNoteEm = 100;        // the note's `letter-spacing: 0.1em`

// NOTE: this theme used to carry its own copy of the half-leading baseline
// formula, and it was the *correct* copy while the shared primitives in
// components.cpp used a different, wrong one. That is the worst arrangement of
// the two: the theme's runs were centred and the shared boxes were not, so the
// screen was subtly inconsistent with itself and the bug was invisible to
// anyone reading either file alone. reader::baselineIn in core/src/text.cpp is
// now the only copy, and this theme is one of its callers like any other.

// The MEASURING slots: kHintSlotMarks with no labels, for the two functions that
// ask how tall the bar is before anything is drawn (libraryVisibleRows,
// settingsMetrics). Labels cannot change a bar's height -- a slot is one line of
// Meta whatever it says -- and these deliberately KEEP their marks where
// buildHints would drop a markless slot's, because only the marks and the type
// role set the height. Measuring is a different job from drawing, so it does not
// pretend to be the same call.
void measuringHints(Hint out[4]) {
  for (int i = 0; i < 4; ++i) out[i] = {kHintSlotMarks[i], "", false};
}

// design/HomeEmpty.dc.html's own numbers: a 44px top pad, the 20px flex `gap`
// between the mark, the title and the copy, and the copy's `max-width`.
// The board's `padding: 18px 24px 0` on the battery strip.
constexpr int kEmptyStripTop = 18;
constexpr int kEmptyTopPad = 44;
constexpr int kEmptyGap = 20;
constexpr int kEmptyProseMaxW = 400;

// Home's hint marks. Slot 0 is the BOOK, because Home's first hint is READ -- and
// on the empty variant that slot has no label at all, which by the hint bar's own
// rule means no mark: a mark over an empty slot is an affordance for an action
// that is not there, and it measures 32px where the board measures 36 and shifts
// every other slot along.
constexpr const Icon* kHomeMarks[4] = {&icons::kBook, &icons::kDot, &icons::kUp,
                                       &icons::kDown};

// "12 BOOKS", and "1 BOOK". The board only ever shows the plural, so the
// singular is a decision rather than a transcription -- and a screen reading
// "1 BOOKS" is a defect nobody would defend.
std::string bookCountLabel(int n) {
  return std::to_string(n) + (n == 1 ? " BOOK" : " BOOKS");
}

// NO drawCoverPlaceholder ANY MORE, AND NO CALLER LEFT TO WANT ONE. It drew a
// dithered stand-in for a cover image -- `ditherRect` at level 1 inside a 2px
// `outlineRect` -- and it served all three of this firmware's placeholder slots
// in turn: Home's 112x168, the Library row's 44x64 (which drew its own, inline)
// and Book details' 120x180, the last to go. A slot with nothing in it is not a
// promise the device keeps: nothing decodes a cover into one, and the one screen
// that does decode a cover paints it FULL-BLEED (see renderSleep) rather than
// into a box.
//
// WHAT REMOVING BOOK DETAILS' COST, since it is the one that was defended and
// deferred once: the block's height was the COVER's 180px, because the field
// column is shorter -- so the column's runs were free and losing one (the
// subtitle) moved nothing. The height is the COLUMN's now. That is
// renderBookDetails' business and the arithmetic is stated there.
//
// AND `ditherRect` HAS ONE PRODUCTION CALLER LEFT: renderSleep's full-panel
// field. That is the caller kClustered's own comment now has to name -- the
// tint-versus-edge argument for a clustered matrix is unchanged, but the
// placeholder cover it used to cite as its example is gone.
}  // namespace

void QuietTheme::renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                            Plane plane) {
  fb.clear(true);

  Hint homeHints[4];
  buildHints(kHomeMarks, vm.hints, vm.holds, homeHints);

  // ONE CHOICE OF MARK AND ONE SPELLING OF THE NUMBER, for both draw sites below
  // -- the header band and the nothingToContinue strip. A choice made in two
  // places is a choice that will eventually be made differently in the two
  // places, which is this file's own rule about the second copy.
  const Icon& batteryMark = vm.batteryCharging ? icons::kBatteryCharging : icons::kBattery;
  // An empty string, not "0%": see HomeViewModel::batteryPercent. drawText and
  // measure both answer nothing for it, so the mark keeps its place on the margin
  // and no gap is left where the number would have been.
  const std::string charge =
      vm.batteryPercent < 0 ? std::string() : std::to_string(vm.batteryPercent) + "%";

  if (vm.nothingToContinue) {
    // NOT A HEADER BAND, and drawHeaderBand is the wrong primitive for it. This
    // board's top strip is a bare right-aligned battery -- `padding: 18px 24px 0`,
    // no label and NO `border-bottom` -- where the band has a label and a 2px
    // rule. Reaching for the band with an empty label drew that rule, which is a
    // line the board does not have.
    //
    // Inline rather than a `drawHeaderBandNoRule`: the board makes this a
    // different element, not a variant of one, and there is exactly one of it.
    // `NOW READING` is also absent for a reason -- it would be a claim about a
    // book that does not exist.
    const Font& pct = fonts[Role::Value700];
    const int chargeW = pct.measure(charge);
    const int stripRight = fb.width() - kMargin;
    const int battX = stripRight - batteryMark.w;
    const int textX = battX - kBandGap - chargeW;
    int ey = kEmptyStripTop;
    drawIcon(fb, batteryMark, battX, iconTopIn(ey, pct.lineHeight(), batteryMark.h),
             Ink::Black, plane);
    drawText(fb, pct, textX, baselineIn(pct, ey, pct.lineHeight()), charge, Ink::Black, {},
             plane);
    ey += pct.lineHeight();
    const Font& big = fonts[Role::Title700];
    const Font& copy = fonts[Role::Body400];
    const int colW = fb.width() - 2 * kMargin;

    // 1/64 px through the column, for the reason renderSdMissing gives: the
    // paragraph's height is a fraction (1.55 x 29px is 44.95) and rounding it
    // before the next element would move everything below it.
    const int proseW = colW < kEmptyProseMaxW ? colW : kEmptyProseMaxW;
    const Prose lines = wrapProse(copy, vm.emptyBody, proseW, kProseLeadEm);

    int eyF26 = pxToF26(ey + kEmptyTopPad);
    drawIcon(fb, icons::kBookLarge, centreIn(kMargin, colW, icons::kBookLarge.w),
             f26ToPx(eyF26), Ink::Black, plane);
    eyF26 += pxToF26(icons::kBookLarge.h + kEmptyGap);

    drawCentredText(fb, big, kMargin, colW,
                    baselineInF26(big, eyF26, pxToF26(big.lineHeight())), vm.emptyTitle,
                    Ink::Black, {}, plane);
    eyF26 += pxToF26(big.lineHeight() + kEmptyGap);

    drawProse(fb, copy, lines, centreIn(kMargin, colW, proseW), proseW, eyF26, Ink::Black, plane);

    // The menu and the bar sit exactly where Home's do: this is a VARIANT of one
    // screen, and a menu that moved between the two would read as a different
    // screen rather than a different state. Same call, same rule for the trailing
    // mark -- a row states a quantity or discloses a screen, never both.
    const int menuTop = fb.height() - hintBarHeight(fonts, homeHints) -
                        static_cast<int>(vm.menu.size()) * kRowH;
    for (size_t i = 0; i < vm.menu.size(); ++i) {
      const bool discloses = vm.menu[i].value.empty();
      drawRow(fb, fonts, menuTop + static_cast<int>(i) * kRowH, vm.menu[i].label,
              vm.menu[i].value, static_cast<int>(i) == vm.focusedMenuIndex,
              discloses ? &icons::kChevron : nullptr, plane);
    }
    drawHintBar(fb, fonts, homeHints, plane);
    return;
  }

  int y = drawHeaderBand(fb, fonts, "NOW READING", charge, &batteryMark, plane);

  // ONE COLUMN. This was two -- a 112x168 placeholder cover on the left and the
  // reading state stacked to its right -- and design/Main.dc.html has dropped the
  // cover: it claimed a picture this screen does not have, in the most prominent
  // slot on the screen whose whole job is to name the book being read, and it took
  // 128px of a 480px panel away from the name to do it.
  y += kCoverTopGap;

  // The column starts on the margin now, so `titleW` below picks up the cover's
  // 112 and the gutter's 16. Derived from the margin rather than pinned, exactly
  // as it was derived from the cover before.
  const int rightX = kMargin;
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
  // pt ramp the column was ~239px against a 192px cover, so that anchor drove the
  // numeral up into the author line. With the cover gone there is nothing left to
  // anchor to even by mistake, which is worth knowing before adding a run here.
  int ry = y + kColPadTop;
  // The board sets the title in caps (text-transform: uppercase). Casing is a
  // presentation decision, so the theme applies it rather than the view-model
  // carrying a pre-shouted string.
  //
  // IT WRAPS, AND IT USED TO ELIDE. The board now says `overflow-wrap: anywhere`
  // where it said `text-overflow: ellipsis` -- and the reason is BookDetails'
  // reason: an ellipsis on a list ROW hides only which of seven rows this is, and
  // here it hides the thing the screen exists to say. The device showed a truncated
  // name on the one screen whose whole job is to name the book being read.
  //
  // `WordBreak::Anywhere` because the break has to be allowed inside a word: a
  // title that fell back to a filename is usually one word, and there is no break
  // opportunity at an underscore or a hyphen.
  //
  // The column is everything from its left edge to the screen margin -- derived,
  // not pinned, which is what keeps it right on both panels (304px on the X4, 352
  // on the X3).
  const int titleW = fb.width() - kMargin - rightX;

  // THE WRAP IS BOUNDED, and the bound is DERIVED the way Book details derives its
  // own: everything below this block is fixed -- the progress bar, CONTINUE, the
  // menu and the hint bar -- so the NAME is what yields. Room for the block is the
  // canvas less the band and this block's top padding, less the bar and the slab
  // with their gaps, less the bottom-anchored menu and bar. The column's other runs
  // are fixed; what is left, over the title's line box, is how many lines it may
  // have.
  //
  // A constant here would be a second copy of three other boxes' models, and it
  // would be wrong the first time any of them changed.
  const int belowBlock = kBlockGap + kBarH + kBlockGap + kBlockH;
  const int bottomAnchored =
      hintBarHeight(fonts, homeHints) + static_cast<int>(vm.menu.size()) * kRowH;
  const int blockRoom = fb.height() - y - belowBlock - bottomAnchored;
  // Both of the board's 2px column paddings, the two gaps, and the three runs that
  // are not the title.
  const int columnFixedH = 2 * kColPadTop + kTitleAuthorGap + body.lineHeight() + kGroupGap +
                           kDisplayLineH + kMetaGap + meta.lineHeight();
  int maxTitleLines = (blockRoom - columnFixedH) / kTitleLineH;
  if (maxTitleLines < 1) maxTitleLines = 1;

  // The board's `line-height: 1.05`, passed as a LEAD rather than an em multiple:
  // 1.05 is the board's number and 44 is what it resolves to, and wrapProseLead is
  // the form that takes a line box the board tightened by hand.
  //
  // Casing is applied before the wrap, not after: the caps run is wider than the
  // mixed-case one, so wrapping the original would break in the wrong places.
  // THE SHOUTED STRING IS NAMED, and it has to be: `Prose::lines` are string_VIEWS
  // into the text handed to the wrap, "which must outlive the Prose" (components.h
  // says so). Passing `upperLatin1(vm.title)` inline made that text a temporary that
  // died at the end of the expression, and drawProse then read freed memory -- which
  // rendered as a column of notdef boxes for a title long enough to wrap, and
  // rendered CORRECTLY for a short one, because the freed bytes were still there.
  // Silently right in the case every golden covers.
  const std::string shouted = upperLatin1(vm.title);
  std::string titleTail;
  Prose titleProse =
      wrapProseLead(title, shouted, titleW, pxToF26(kTitleLineH), {}, WordBreak::Anywhere);
  clampProse(title, titleProse, maxTitleLines, titleW, titleTail);
  // One line is bit-identical to the drawText this replaced: drawProse's first
  // baseline is baselineInF26(font, pxToF26(ry), pxToF26(kTitleLineH)), which is
  // baselineIn's own definition -- so an ordinary short title moves nothing.
  ry += f26ToPx(drawProse(fb, title, titleProse, rightX, titleW, pxToF26(ry), Ink::Black, plane,
                          ProseAlign::Left));
  ry += kTitleAuthorGap;

  drawText(fb, body, rightX, baselineIn(body, ry, body.lineHeight()), vm.author, Ink::Black, {},
           plane);
  ry += body.lineHeight() + kGroupGap;

  // The percentage is the one display-scale run on the screen: 32pt against the
  // title's 20, so it outranks the book's name instead of tying with it.
  drawText(fb, display, rightX, baselineIn(display, ry, kDisplayLineH),
           std::to_string(vm.percent) + "%", Ink::Black, {}, plane);
  ry += kDisplayLineH + kMetaGap;

  // ONE META LINE, AND IT NAMES THE CHAPTER. It held a spine position of a spine
  // count (`CH. 14 OF 36`) and that was a false claim -- a spine counts the front and
  // back matter and the part dividers with the chapters, so the pair invites an
  // arithmetic it does not support. See design/Main.dc.html for the corpus figures.
  //
  // AT THE BOARD'S 0.10em, WHICH IS THE TRACKING THIS BOARD ALREADY GAVE A CHAPTER
  // NAME before the counter displaced it -- `kMetaEm`'s 0.16em was the counter's, and
  // a name is not a counter.
  //
  // ELIDED, NOT WRAPPED, AND THE BUDGET IS THE TITLE'S OWN COLUMN. The words come off
  // the card, so this run is as long as a publisher made it; the Reader's header band
  // elides the identical string against the identical hazard. It may not wrap: the
  // title above it already grows into a budget derived from everything below this
  // block, and one budget cannot serve two growable runs without saying which yields
  // -- so the run that names the BOOK keeps every line, and the run that names where
  // you are in it takes one. That is also what keeps `columnFixedH` above honest,
  // since it reserves exactly one `meta.lineHeight()` for this line.
  //
  // AN EMPTY LABEL DRAWS NOTHING AND STILL COSTS ITS LINE, deliberately: a pointer
  // from before last.json carried a chapter cannot say which one this is, and the
  // line goes blank rather than falling back to the position it was reported for.
  // `ry` advances either way, so the runs below do not step up under a blank -- which
  // is the property the `whichever is taller` note below already rests on.
  drawTextElided(fb, meta, rightX, baselineIn(meta, ry, meta.lineHeight()), vm.chapterLabel,
                 titleW, Ink::Black, trackingEm(meta, kTightMetaEm), plane);
  ry += meta.lineHeight();

  // The block is as tall as its ONE column, where it used to be as tall as
  // whichever of two was taller. With the cover gone there is no second column to
  // compare against and no 168px floor under the block: a short view model (no
  // chapter label, a one-digit percentage) simply makes the block shorter, and
  // everything below it moves up with it. That floor was never reached in
  // practice anyway -- the comment above records the column at ~239px against a
  // 192px cover -- so this is the removal of an inert `max`, not a behaviour
  // change dressed as one.
  //
  // The board gives the column `padding: 2px 0` -- both edges, not just the top --
  // so the bottom 2px is charged here. Omitting it lands everything below 2px
  // high, which is a defect this screen has shipped once.
  ry += kColPadTop;
  y = ry + kBlockGap;

  // Progress bar spans the usable width.
  const int barW = fb.width() - 2 * kMargin;
  drawProgressBar(fb, kMargin, y, barW, kBarH, vm.percent);
  y += kBarH + kBlockGap;

  // Continue block: focused when no menu row is.
  const bool continueFocused = (vm.focusedMenuIndex < 0);
  const Font& label = fonts[Role::Label500];
  if (continueFocused) {
    fb.fillRect(kMargin, y, barW, kBlockH, false);
  } else {
    outlineRect(fb, kMargin, y, barW, kBlockH, 2);
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

  // Menu rows sit above the hint bar, so the bar's height decides where they
  // start. That height is the bar's to compute -- from its own padding and its
  // own content -- and asking it is what keeps this stacking correct when a
  // screen sets its hints in a larger role or pairs them with a taller mark. A
  // constant here would be a second, private copy of the bar's box model.
  const int menuTop =
      fb.height() - hintBarHeight(fonts, homeHints) - static_cast<int>(vm.menu.size()) * kRowH;
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

  drawHintBar(fb, fonts, homeHints, plane);
}

void QuietTheme::renderBookEnd(Framebuffer& fb, const FontSet& fonts,
                               const BookEndViewModel& vm, Plane plane) {
  fb.clear(true);

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // NO MARK ON THIS BAND, and that is not an omission: drawHeaderBand DEFAULTS to the
  // battery, which this board does not draw. Passing null is also what makes
  // headerBandHeight answer about the band this screen actually draws -- asking it
  // about Home's would put every run below here in the wrong place.
  //
  // AND NO VALUE EITHER. The slot held the book's shouted name, which the byline
  // below states again -- and a long title squeezed the LABEL until `BOOK FINISHED`
  // itself elided, so the screen stopped saying what it is. drawHeaderBand with an
  // empty value is already right: it reserves a gap for the absent run and then
  // draws the (null) mark past it, so the phantom gap CANCELS and nothing lands off
  // the margin. The board reserves the slot with an nbsp for the other half of this
  // -- Chrome sizes a flex row by its children, bandContentH() does not.
  const int bandH = drawHeaderBand(fb, fonts, "BOOK FINISHED", "",
                                   /*mark=*/nullptr, plane);

  const int usableW = fb.width() - 2 * kMargin;
  const Font& titleF = fonts[Role::Title700];
  const Font& bylineF = fonts[Role::Value500];
  const Font& metaF = fonts[Role::Meta400];

  // --- The note's box, computed FIRST because it is a floor -----------------------
  //
  // `margin-top: auto` on the board, so the note hangs off the BOTTOM of the frame
  // and not off the slabs. That is what keeps it still when the meta line is absent,
  // and it is why the hint bar's height is ASKED FOR rather than assumed.
  //
  // Wrapped once, then both measured and drawn from that one Prose: two calls that
  // each re-wrapped would be two chances to disagree, and the disagreement reads as a
  // paragraph drifted off position.
  // TRACKED, and the wrap is where the tracking has to arrive -- Prose carries it
  // through to the draw precisely so the two cannot disagree. Passing it only to
  // drawProse would wrap at one measure and paint at another, which is a line that
  // breaks in the wrong place rather than a line that looks slightly off.
  //
  // It is wrapped up here rather than beside its draw because `noteTop` is the FLOOR
  // the content block may not reach, and the byline's line budget is derived from it.
  const Prose note = wrapProse(metaF, vm.note, usableW, kBookEndNoteLeadEm,
                               trackingEm(metaF, kBookEndNoteEm));
  const int barH = hintBarHeight(fonts, hints);
  const int noteTop = fb.height() - barH - kBookEndNotePadBottom - f26ToPx(note.heightF26());

  // --- The content block, top-anchored under the band --------------------------
  //
  // Accumulated in 1/64 px and rounded ONCE where each run is painted. Three
  // truncating divisions once put an icon 1.5px low on another screen, and
  // pre-rounding 2.52px of tracking to 3 drifted a label ~3px.
  int yF26 = pxToF26(bandH + kBookEndBlockPadTop);

  const Icon& tick = icons::kCheck;
  drawIcon(fb, tick, centreIn(kMargin, usableW, tick.w), f26ToPx(yF26), Ink::Black, plane);
  yF26 += pxToF26(tick.h + kBookEndGap);

  drawCentredText(fb, titleF, kMargin, usableW,
                  baselineInF26(titleF, yF26, pxToF26(titleF.lineHeight())), vm.title,
                  Ink::Black, trackingEm(titleF, kBookEndTitleEm), plane);
  yF26 += pxToF26(titleF.lineHeight() + kBookEndGap);

  // THE BYLINE WRAPS, and it is the only run on this screen that does. It was one
  // centred line, so a long title ran off BOTH margins on a real card -- and this is
  // the run that now carries the book's name alone, the band having given the slot
  // up. `WordBreak::Anywhere` is Home's and Book details' choice for the same reason:
  // a title that fell back to a filename is usually one unbreakable word.
  //
  // `vm.byline` is passed DIRECTLY, and that is load-bearing: `Prose::lines` are
  // views into the text handed to the wrap, so a temporary here would render a
  // wrapped title as a column of notdef boxes while a short one came out fine --
  // which is exactly the bug Home's title shipped with, invisible to every golden
  // because a notdef box inks rows like a letter does. A view-model member outlives
  // the render; `upperLatin1(...)` inline would not.
  //
  // The lead is the face's own line box, because the board states no `line-height`
  // here -- which is also what makes a ONE-LINE byline bit-identical to the
  // drawCentredText this replaced: drawProse's first baseline is
  // baselineInF26(font, topF26, leadF26), which is that call's own argument.
  //
  // The budget is DERIVED, never pinned: what is left between this run's top and the
  // note's, once the meta line and the two slabs have taken theirs. Unbounded, a long
  // enough name pushes the slabs off the bottom -- the same defect as the overflow
  // this fixes, turned ninety degrees, which is the case clampProse exists for.
  const int metaH = vm.meta.empty() ? 0 : kBookEndGap + metaF.lineHeight();
  const int slabsH = kBookEndSlabPadTop + 2 * kActionH + kBookEndGap;
  int maxBylineLines = (noteTop - f26ToPx(yF26) - metaH - slabsH) / bylineF.lineHeight();
  if (maxBylineLines < 1) maxBylineLines = 1;

  std::string bylineTail;
  Prose bylineProse = wrapProseLead(bylineF, vm.byline, usableW, pxToF26(bylineF.lineHeight()),
                                    Tracking{}, WordBreak::Anywhere);
  clampProse(bylineF, bylineProse, maxBylineLines, usableW, bylineTail);
  yF26 += drawProse(fb, bylineF, bylineProse, kMargin, usableW, yF26, Ink::Black, plane,
                    ProseAlign::Centre);

  // THE META LINE IS ABSENT, NOT BLANK, when the book's chapter count is unknown --
  // and ITS GAP GOES WITH IT. Adding the gap unconditionally would sit the slabs a
  // line lower on a book that simply did not say, which is a layout that moves for a
  // reason the reader cannot see.
  if (!vm.meta.empty()) {
    yF26 += pxToF26(kBookEndGap);
    drawCentredText(fb, metaF, kMargin, usableW,
                    baselineInF26(metaF, yF26, pxToF26(metaF.lineHeight())), vm.meta,
                    Ink::Black, trackingEm(metaF, kBookEndMetaEm), plane);
    yF26 += pxToF26(metaF.lineHeight());
  }

  // --- The two slabs ------------------------------------------------------------
  //
  // FILLED IS THE FOCUS, not the button's identity -- components.h states the rule:
  // "the boards fill exactly the slab the focus is on". The variant changes the TYPE
  // as well as the box (Value700 against Label500), which is the half that is easy to
  // miss and would be invisible in review. The HEIGHT comes back from the primitive
  // rather than from a constant here, so an outlined slab and a filled one cannot
  // disagree about the rect they occupy.
  yF26 += pxToF26(kBookEndSlabPadTop);
  int slabY = f26ToPx(yF26);
  slabY += drawActionButton(fb, fonts, kMargin, slabY, usableW, vm.finishLabel,
                            vm.focusedAction == BookEndScreen::kFinish, plane);
  slabY += kBookEndGap;
  drawActionButton(fb, fonts, kMargin, slabY, usableW, vm.leaveLabel,
                   vm.focusedAction == BookEndScreen::kLeave, plane);

  // --- The note, in the box measured at the top of this function -----------------
  //
  // LEFT, not the Centre default: the board states no `text-align` on this block,
  // unlike the centred content block above it.
  drawProse(fb, metaF, note, kMargin, usableW, pxToF26(noteTop), Ink::Black, plane,
            ProseAlign::Left);

  drawHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderSdMissing(Framebuffer& fb, const FontSet& fonts,
                                 const SdMissingViewModel& vm, Plane plane) {
  fb.clear(true);

  // Three of the four slots are the boards' empty 36px placeholder, and
  // buildHints is what keeps them markless -- see its header comment.
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

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

  drawCentredText(fb, title, kMargin, usableW,
                  baselineInF26(title, yF26, pxToF26(title.lineHeight())), vm.title, Ink::Black,
                  titleTracking, plane);
  yF26 += pxToF26(title.lineHeight() + kPromptGap);

  yF26 += drawProse(fb, body, prose, colX, colW, yF26, Ink::Black, plane);
  yF26 += pxToF26(kPromptGap);

  // Filled: SdMissing's board draws the one variant, and RETRY is the only thing
  // on the screen a focus could be on.
  drawActionButton(fb, fonts, centreIn(kMargin, usableW, kPromptActionW), f26ToPx(yF26),
                   kPromptActionW, vm.action, /*filled=*/true, plane);

  drawHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderBatteryEmpty(Framebuffer& fb, const FontSet& fonts,
                                    const BatteryEmptyViewModel& vm, Plane plane) {
  fb.clear(true);

  const Icon& mark = icons::kBatteryLarge;
  const Font& title = fonts[Role::Title700];
  // Body400, not Body500: the board's paragraph is `font-size: var(--t-body)` with
  // no font-weight, so it is CSS default 400 -- the distinction that had Home's
  // author line rendering 19% over the board's ink.
  const Font& body = fonts[Role::Body400];
  const Font& note = fonts[Role::Meta400];

  // THE BADGE FIRST, because the column above it is centred on the room the badge
  // leaves and the badge is the only thing that knows how much that is. It returns
  // its own top y, which is exactly the bottom of the board's `flex-grow: 1` block:
  // the outer flex column holds the centring block and then the badge's row, and the
  // row's height is the badge plus its `padding-bottom: 34px`. Asking drawBadge is
  // what makes this correct on both panels and on a label of any width, where
  // subtracting a transcribed height would be a second spelling of its box model.
  //
  // NOT the hint bar's height, which is what SdMissing subtracts: this screen draws
  // no bar, because the shell paints it and then calls deep sleep and there is
  // nobody left to press anything.
  const int areaH = drawBadge(fb, note, vm.note, plane);
  const int usableW = fb.width() - 2 * kMargin;

  // kPromptGap and kPromptTitleEm, NOT a second pair of numbers: this board and
  // SdMissing's state the identical `gap: 22px` and `letter-spacing: 0.06em` on the
  // identical centred column, and two spellings of one number is the shape this
  // project has a rule about. What the two boards do NOT share is the paragraph's
  // `max-width`, which is per-copy rather than per-screen -- see kBatteryProseMaxW.
  const int colW = usableW < kBatteryProseMaxW ? usableW : kBatteryProseMaxW;
  const int colX = centreIn(kMargin, usableW, colW);
  const Prose prose = wrapProse(body, vm.message, colW, kProseLeadEm);

  // Two gaps, not SdMissing's three: the mark, one line of title, the paragraph, and
  // no action slab -- this screen has nothing to offer, because the next statement in
  // the shell is deep sleep. In 1/64 px because the paragraph's height is a fraction
  // (1.55 x 29px is 44.95) and rounding it before halving the free space would put
  // the whole column half a pixel off centre.
  const int stackF26 =
      pxToF26(mark.h + title.lineHeight() + 2 * kPromptGap) + prose.heightF26();
  // Arithmetic shift rather than / 2, for renderSdMissing's reason: identical on
  // every positive value, and it halves a column TALLER than its area the same way
  // instead of truncating toward the origin.
  int yF26 = (pxToF26(areaH) - stackF26) >> 1;

  drawIcon(fb, mark, centreIn(kMargin, usableW, mark.w), f26ToPx(yF26), Ink::Black, plane);
  yF26 += pxToF26(mark.h + kPromptGap);

  drawCentredText(fb, title, kMargin, usableW,
                  baselineInF26(title, yF26, pxToF26(title.lineHeight())), vm.title, Ink::Black,
                  trackingEm(title, kPromptTitleEm), plane);
  yF26 += pxToF26(title.lineHeight() + kPromptGap);

  drawProse(fb, body, prose, colX, colW, yF26, Ink::Black, plane);
}

int QuietTheme::libraryVisibleRows(int panelH, const FontSet& fonts) const {
  // Labelless slots: only the marks and the type role can change a bar's height,
  // and both are the same here as in renderLibrary. An empty LABEL narrows a
  // slot, which moves the others along the bar -- but this function is only
  // asking how tall the bar is.
  Hint hints[4];
  measuringHints(hints);
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
    const bool rule = rowRuleFor(i, rows, focused);
    // The gutter exists only when the rail does, so a list that fits runs its
    // rows to the panel edge -- focused fill included. See kListGutterW.
    y += drawBookRow(fb, fonts, y, {row.title, row.meta, row.value, row.isFolder}, focused,
                     rule, plane, overflowing ? kListGutterW : 0);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // The rail spans the LIST, not the panel: a track running the full height would
  // claim the header band and the hint bar scroll, which they do not. `listTop` is
  // the band's bottom edge drawHeaderBand already returned, and the bottom is
  // DERIVED from the bar rather than pinned -- hintBarHeight needs the hints, so
  // they are built before the rail rather than after it.
  drawScrollRail(fb, listTop, fb.height() - hintBarHeight(fonts, hints), vm.firstRow, rows,
                 vm.totalRows, plane);

  drawHintBar(fb, fonts, hints, plane);
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

// design/BookError.dc.html's body is `display: flex; flex-direction: column;
// gap: 12px` holding the mark and then the paragraph -- so this is the gap BELOW
// the icon, and it is the board's own number rather than the button gap reused.
constexpr int kBookErrorIconGap = 12;

// --- design/BookDetails.dc.html ---------------------------------------------
//
// The board's own box model: the block's `padding: 24px 24px 20px 24px`, the text
// column's `padding-top: 4px` and `gap: 6px`, the `line-height: 1.1` on the
// title, and the `border-top: 2px` above the fields.
//
// NO kDetailsCoverW/H AND NO kDetailsGutter. They were the 120x180 placeholder
// cover and the block's `gap: 20px` that separated it from the column; the cover
// is gone from the board and the gap went with it, a flex gap having nothing to
// separate once one child is left. Removed rather than left unused -- an unread
// constant is the `ListRow::trackingEm1000` shape, a value with no consumer that
// reads as capability. A real decoded cover here would be a new box on the board
// first, so it would bring its own numbers.
constexpr int kDetailsPadTop = 24;
constexpr int kDetailsPadBottom = 20;
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
  drawHintBar(fb, fonts, hints, plane);
}

// HOW TALL A CENTRED OVERLAY PANEL MAY BE, which is the canvas less the hint bar
// TWICE -- and the two is the whole point of the function existing.
//
// The boards say `max-height: 100%`, and 100% of the canvas is not the bound a
// firmware can honour, because the board draws its hint bar `position: absolute;
// bottom: 0` OVER the panel while `drawOverlayHintBar` paints an opaque strip that
// slices whatever is under it -- and the slab it slices is the one the reader is
// about to press. Reserving the bar ONCE does not fix it either: `centreIn` splits
// the slack evenly, so a panel of `height - barH` still hangs barH/2 into the bar.
// Reserving it at both ends is what a centred box costs.
//
// DERIVED THROUGH hintBarHeight, never pinned -- the bar's own height follows its
// hints and the type ramp, and this project has paid three times for a number a
// board computes being written down instead. It needs the hints for that, which is
// why both callers build theirs before they size their panel rather than at the end.
int centredPanelRoom(const Framebuffer& fb, const FontSet& fonts, const Hint hints[4]) {
  return fb.height() - 2 * hintBarHeight(fonts, hints);
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
  const std::string caption = elideToWidth(fonts[Role::Label500], upperLatin1(vm.title),
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
                       rowRuleFor(i, rows, focused), plane);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
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
  // The bar is measured BEFORE the panel is sized, because its height is an input to
  // the budget below -- renderBookDetails' order, for renderBookDetails' reason.
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
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
  // ten lines on the X4 against nine on the X3, which no ordinary name comes near.
  // A caption clipped by the panel's border would be worse than an ellipsis, and
  // a panel taller than the glass -- centred, so cut off at BOTH ends -- worse
  // still.
  //
  // THE ROOM IS THE CANVAS LESS THE HINT BAR TWICE, not the whole canvas: this line
  // read `fb.height() - panelFixedH`, so a 255-character name -- FAT's own maximum,
  // a name a real card can hold -- grew the panel until CANCEL was sliced in half by
  // the bar and the panel's bottom border went off the glass. See centredPanelRoom.
  const int panelFixedH = 2 * kPanelBorder + 2 * kPanelCaptionPadY + kPanelCaptionRuleH +
                          (2 * kConfirmProsePadY + proseH) +
                          (2 * kActionH + kConfirmButtonGap + kConfirmButtonPadBottom);
  const int captionRoom = centredPanelRoom(fb, fonts, hints) - panelFixedH;
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

  drawOverlayHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderBookError(Framebuffer& fb, const FontSet& fonts,
                                 const BookErrorViewModel& vm, Plane plane) {
  // No fb.clear(): the parent -- the Library, or HOME on the CONTINUE path -- is
  // already painted. Getting this wrong is a panel floating on white, and nothing on
  // the desktop can catch it, because the simulator and every golden go through
  // App::render.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kConfirmPanelW);
  const int colW = contentW - 2 * kPanelPadX;
  const Font& body = fonts[Role::Body400];
  const Icon& mark = icons::kWarning;

  // This caption is the board's fixed `CAN'T OPEN FILE` and cannot overflow, unlike
  // the confirmation's, which is a sentence with a filename in it. It goes through
  // the same wrap anyway so the two panels cannot disagree about a caption's height,
  // and its height is INDEPENDENT of the paragraph here -- which is what lets the
  // budget below be spent on the paragraph instead.
  const Prose label = wrapPanelCaption(fonts, vm.title, contentW, WordBreak::Anywhere);
  const int captionH = panelCaptionHeight(fonts, label);

  // The bar is measured BEFORE the panel is sized, because its height is an input to
  // the budget below -- renderBookDetails' order, for renderBookDetails' reason.
  Hint bookErrorHints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, bookErrorHints);

  // Wrapped once, before anything is placed: its height is what it wraps to, and the
  // panel's own bottom edge hangs off that. Two wraps would be two chances to
  // disagree, and the disagreement reads as a paragraph drifted off centre.
  //
  // `WordBreak::Anywhere` IS THE BOARD'S `overflow-wrap: anywhere` on the paragraph,
  // and renderDeleteConfirm's CAPTION rule arriving one slot down: this board puts
  // the filename in the PARAGRAPH, and a filename is frequently one unbreakable word
  // -- so under the default `Normal` a real card's name is one line WIDER than the
  // column, drawn straight through the panel's right border and off the glass, with
  // the rest of the name lost. Rendered and looked at, not reasoned about; both
  // boards declare it, because a behaviour invented in code silently invalidates
  // `make compare`.
  // DECLARED BEFORE THE WRAP IT WILL BE VIEWED BY, which is renderDeleteConfirm's
  // order: clampProse's elided last line is a new string that is not in the wrapped
  // text, so it must outlive the Prose that views it.
  std::string proseTail;
  Prose prose = wrapProse(body, vm.message, colW, kConfirmProseLeadEm, {}, WordBreak::Anywhere);

  // The board's `max-height: 100%; overflow: hidden`, in the one form a firmware can
  // honour it -- renderDeleteConfirm's rule with the two runs SWAPPED. There the
  // caption carries the name and yields; here the caption is a fixed literal and the
  // PARAGRAPH is the unbounded part, because that is where this board puts the name.
  // Clamping the caption instead would bound the run that cannot grow and leave the
  // one that can, and the panel is CENTRED, so one taller than the canvas is cut off
  // at BOTH ends.
  //
  // THE ROOM IS THE CANVAS LESS THE HINT BAR TWICE, not the whole canvas: this line
  // read `fb.height() - panelFixedH`, so a 255-character name -- FAT's own maximum,
  // a name a real card can hold -- grew the panel until `DELETE FILE...` was sliced
  // in half by the bar and the panel's bottom border went off the glass. See
  // centredPanelRoom for why reserving the bar ONCE would not have been enough.
  // THE ACTIONS BLOCK, ONCE. It is a term of the paragraph's budget AND of the
  // panel's height, and those two spellings shipped as two copies of the same
  // expression -- which is the shape that lets a budget and a height disagree, and a
  // panel whose parts do not add up to its box is a panel drawn off centre.
  //
  // THE GAP GOES WITH THE SLAB IT SEPARATED. The board's actions block is a flex
  // COLUMN with `gap: 12px`, and a gap is BETWEEN items: with one slab there is
  // nothing for it to separate, so the memory shape's block is one kActionH plus the
  // block's own 20px bottom padding. Keeping the gap would leave 12px of dead air
  // under the only slab and put the whole centred panel 6px high.
  const int actionsH = (vm.offersDelete ? 2 * kActionH + kConfirmButtonGap : kActionH) +
                       kConfirmButtonPadBottom;
  const int panelFixedH = 2 * kPanelBorder + captionH +
                          (2 * kConfirmProsePadY + mark.h + kBookErrorIconGap) + actionsH;
  const int proseRoom = centredPanelRoom(fb, fonts, bookErrorHints) - panelFixedH;
  int maxProseLines = 1;
  while (maxProseLines < prose.lineCount() &&
         f26ToPx((maxProseLines + 1) * prose.leadF26) <= proseRoom)
    ++maxProseLines;
  clampProse(body, prose, maxProseLines, colW, proseTail);
  const int proseH = f26ToPx(prose.heightF26());

  const int panelH = 2 * kPanelBorder + captionH +
                     (kConfirmProsePadY + mark.h + kBookErrorIconGap + proseH +
                      kConfirmProsePadY) +
                     actionsH;

  const int x = panelLeft(fb.width(), kConfirmPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kConfirmPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, "", plane);

  cy += kConfirmProsePadY;
  // LEFT-ALIGNED at the column's own left edge, not centred: the board's body is a
  // flex COLUMN with default `align-items: stretch`, so the mark sits at the start of
  // the line box rather than in the middle of the panel.
  drawIcon(fb, mark, cx + kPanelPadX, cy, Ink::Black, plane);
  cy += mark.h + kBookErrorIconGap;

  // Left-aligned: the board's paragraph declares no `text-align`, so it is a plain
  // block -- unlike a full-screen prompt's, which is centred.
  cy += f26ToPx(drawProse(fb, body, prose, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black,
                          plane, ProseAlign::Left));
  cy += kConfirmProsePadY;

  // The focused slab is filled and the other outlined, which is the boards' rule
  // wherever they pair the two. Focus starts on OK.
  drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.okLabel, vm.focusedAction == 0,
                   plane);
  // ONE SLAB ON THE OutOfMemory SHAPE (design/BookErrorMemory.dc.html). Absent, not
  // inert: the file is fine, so offering to delete it over a transient shortage is
  // the wrong nudge -- HomeEmpty's cut slab is the precedent. The screen gives that
  // shape ONE row, so the OK slab above is always the focused, filled one here.
  if (vm.offersDelete) {
    cy += kActionH + kConfirmButtonGap;
    drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.deleteLabel,
                     vm.focusedAction == 1, plane);
  }

  drawOverlayHintBar(fb, fonts, bookErrorHints, plane);
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

  // The title column, and it is the block's only child now -- the placeholder
  // cover that used to open the row is gone, and `align-items: flex-start` no
  // longer has a taller sibling to start the column against.
  y += kDetailsPadTop;

  const Font& title = fonts[Role::Title700];
  // Body400 and Label400: both runs are `font-size: var(--t-...)` with no
  // font-weight, so both are CSS default 400. This is the distinction that had
  // Home's author line rendering 19% over the board's ink.
  const Font& author = fonts[Role::Body400];

  // The bar and the rows are measured before the title is laid out, because they
  // are what decides how many lines the title may have. Building the hints here
  // rather than at the end is that: buildHints leaves the three dead buttons
  // markless, and the bar's height follows its content.
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  const int rows = static_cast<int>(vm.fields.size());
  int rowsH = 0;
  for (int i = 0; i < rows; ++i) rowsH += detailRowHeight(i != rows - 1);

  // The full width now. The cover took `kDetailsGutter` with it, because a flex
  // `gap` is BETWEEN items and one child has nothing for it to separate --
  // renderBookError's cut slab took its own gap the same way.
  const int colX = kMargin;
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
  // the bar; the column's other line and its padding are fixed; what is left,
  // divided by the title's line box, is how many lines the title may have. That is
  // 5 on both panels today (300px of block room on the X4, 292 on the X3, against
  // 47px of fixed column and a 46px line box).
  //
  // TWO QUANTITIES, AND THEY ARE NOT ONE NUMBER -- the same split the sleep card's
  // chapter reserve is. `blockRoom` is a BUDGET and it comes from the CANVAS: the
  // band above it and the rule, rows and bar below it, none of which depends on
  // what the block ends up being. The block's actual HEIGHT is `cy`, what the
  // column really took, and it is <= blockRoom by construction. So the title's
  // budget is NOT self-referential even though the block's height is now the
  // column's: the budget is measured against the canvas, and the height is a
  // result. Do not collapse them -- a budget derived from the height would be a
  // circle, and a height taking the budget would leave the field rules standing
  // wherever the tallest possible title would have put them.
  //
  // THIS COMMENT SAID "3 on both panels today (235px ... 227)" AND BOTH FIGURES
  // WERE STALE: 235 is the SIX-row board's room, and the `Added` row went. Read
  // them off the arithmetic below rather than from here, which is why the terms
  // are named.
  const int blockRoom = fb.height() - (bandH + kDetailsPadTop) -
                        (kDetailsPadBottom + kDetailsRuleH + rowsH + hintBarHeight(fonts, hints));
  // ONE kDetailsColGap, NOT TWO, AND THAT CORRECTION IS LOAD-BEARING NOW. The
  // board's column is a flex column with `gap: 6px`, and a gap is BETWEEN items --
  // so a title and an author cost ONE gap, not one each. This expression carried
  // two while the cover set the block's height, where over-reserving 6px only made
  // the title's budget conservative and nothing on the glass could see it. With
  // the height now the column's, the second gap would draw every rule below the
  // block 6px lower than the board's.
  const int columnFixedH = kDetailsColPadTop + kDetailsColGap + author.lineHeight();
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
  // NO TRAILING GAP. This read `cy += author.lineHeight() + kDetailsColGap`, and
  // the author is the column's LAST run -- a flex gap sits between items, so there
  // is nothing below it for a gap to separate. See columnFixedH above for why the
  // spare 6px was invisible until now.
  cy += author.lineHeight();
  // NO SUBTITLE RUN. It drew a field no book carries -- and removing it also gives the
  // TITLE its line back, because the budget below is the block's room less the column's
  // FIXED runs, and the subtitle was one of them.

  // THE BLOCK IS AS TALL AS ITS COLUMN, plus the block's own bottom padding, and
  // that is the whole consequence of the placeholder cover going. It used to be
  // `max(cy, coverBottom)` -- and the cover's 180px won for every title the screen
  // can draw, so the block's height was a CONSTANT and the column's runs were free.
  // Each run in it costs its line box now, and the rule below moves with them: the
  // board puts it at 203 for a one-line title (66 band + 24 padTop + 4 colPadTop +
  // 46 title + 6 gap + 37 author + 20 padBottom) where the cover put it at 290.
  //
  // A LONG TITLE STILL PUSHES THE FIELDS DOWN rather than running into them, which
  // is what the max used to be credited with and is really maxTitleLines' doing:
  // the wrap is clamped to the room the block has, so the fields cannot be reached.
  y = cy + kDetailsPadBottom;

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
  drawHintBar(fb, fonts, hints, plane);
}

// --- Sleep -------------------------------------------------------------------
//
// design/Sleep.dc.html: a dithered field with a bordered white card centred on it,
// and a smaller badge near the bottom. Every measurement below is the board's.
namespace {

constexpr int kSleepCardMaxW = 400;
constexpr int kSleepCardPadX = 42;
constexpr int kSleepCardPadY = 38;
constexpr int kSleepCardBorder = 2;
constexpr int kSleepGap = 14;      // the card's flex `gap`
constexpr int kSleepRuleW = 44;    // the little rule under NOW READING
constexpr int kSleepRuleH = 2;
constexpr int kSleepBarW = 170;
constexpr int kSleepBarH = 8;
constexpr int kSleepBarTopGap = 8;  // the bar's own `margin-top`, on top of the gap
constexpr int kSleepLabelEm = 260;   // NOW READING, 0.26em
constexpr int kSleepAuthorEm = 220;  // 0.22em
constexpr int kSleepProgressEm = 140;

// The chapter NAME's tracking, and it is deliberately not kSleepProgressEm's
// 0.14em -- design/Main.dc.html reached this conclusion for the identical string
// one screen over: 0.14em is a COUNTER's tracking, and a name is not a counter.
// It is also the narrower of the two on the one run whose width is the whole
// problem: 34.54% of the corpus's 8,617 chapter labels overflow this column at
// 0.10em against 37.60% at 0.14em.
constexpr int kSleepChapterEm = 100;

// HOW MANY LINES THE CHAPTER MAY TAKE -- design/Sleep.dc.html's
// `-webkit-line-clamp: 2`, and the board carries the measurement and the decision.
// In short: of this column's 8,617 corpus labels, 2,976 (34.54%) overflow one line,
// and of THOSE 2,976, 2,274 (76.41%) fit two lines whole while 702 (23.59%) need
// three or more. So two lines elide 702 of 8,617 labels (8.15%) where one line
// elides 2,976 -- a 4.2x reduction in cut names, which is what earns the second
// line, and the same shape of knee kSleepAuthorMaxLines was earned by (85% there).
//
// THIS RUN ELIDED ON ONE LINE AND THE DISTRIBUTION PERMITS THAT. It wraps because
// this screen holds the glass for HOURS, so a cut name is one the reader lives with
// rather than one they press past -- the argument that made the title wrap and then
// the author, reaching the last run on the card. A third line was permitted and is
// not taken: a smaller marginal gain out of another of the title's lines.
constexpr int kSleepChapterMaxLines = 2;

// The board's `line-height: 1.1` on `--t-title`, resolved -- and it is the number
// the FACE would not have given: Title700's own lineHeight() is 53px at ppem 42,
// which is what this screen drew its single line in before it could wrap, 7px
// looser than the board's box. A wrap forces the question (a lead is an argument
// to wrapProseLead, so there is no way to not answer it) and the board had already
// answered it.
//
// A SECOND COPY OF kDetailsTitleLineH, DELIBERATELY, WITH THE REASON WRITTEN DOWN.
// BookDetails.dc.html states the identical `--t-title` at `line-height: 1.1`, so
// this is the same fact and by this project's own rule the second copy is the
// extraction point. It is not extracted here because the two constants live in two
// screens' anonymous namespaces in a file several people edit at once, and the
// extraction is a rename reaching a screen this change does not own. The pair is
// worth one line of a future tidy-up, not a cross-screen edit now.
constexpr int kSleepTitleLineH = 46;  // round(1.1 * 42)

// HOW MANY LINES THE AUTHOR MAY TAKE BEFORE IT ELIDES -- design/Sleep.dc.html's
// `-webkit-line-clamp: 2`, and the board carries the measurement this number is
// made of. In short: every `dc:creator` in the 225-book corpus, shouted, at this
// face and this tracking, against this 312px column -- 68 of 221 (30.8%) overflow
// one line, and 58 of those 68 (85%) fit WHOLE in two. Only 10 need a third and
// every one is a corporate author rather than a person.
//
// IT IS A LINE COUNT WHERE THE TITLE'S BOUND IS THE CARD'S ROOM, and the two being
// different KINDS is the point: both runs grow now, and one budget cannot serve two
// growable runs without saying which yields. This one does, FIRST and by a fixed
// amount, so the title -- the one fact this screen exists to state -- keeps every
// line left over. A proportional split would let a three-line corporate name eat
// the hero.
constexpr int kSleepAuthorMaxLines = 2;

}  // namespace

void QuietTheme::renderSleep(Framebuffer& fb, const FontSet& fonts, const SleepViewModel& vm,
                             Plane plane, CoverSource* cover) {
  // THE COVER REPLACES THE CLEAR -- design/SleepCoverDetails.dc.html is
  // design/Sleep.dc.html with its background swapped and nothing else, so this is
  // the ONE branch the two cover boards need. Everything below is the shipped
  // screen unchanged.
  //
  // It is asked per PASS because a plane is what it can deliver; see
  // CoverSource::loadPlane, which also says why the passes cannot realistically
  // disagree.
  const bool covered =
      cover != nullptr && vm.shows != SleepShows::Details && cover->loadPlane(plane, fb);

  if (!covered) {
    // The FIELD: the board's `.dither-field` is the same 4px-pitch dot as
    // `.dither-dots`, so this is kClustered at level 1 over the whole panel -- the
    // one dither this screen wants, and the reason it is clustered rather than
    // dispersed is in dither.cpp: a tint reads as a blob, not as grain.
    //
    // AND THE CLEAR IS WHAT MAKES A FAILED LOAD SAFE. loadPlane does not promise
    // it left the frame alone -- a streaming implementation finds out the card is
    // gone half way down the picture -- so whatever it wrote is overwritten here
    // rather than shown.
    fb.clear(true);
    ditherRect(fb, 0, 0, fb.width(), fb.height(), 1, Ink::Black);
  }

  // ONE PREDICATE, ASKED ONCE, AND IT DRIVES BOTH THE CARD AND THE BADGE.
  // design/SleepCover.dc.html is SleepCoverDetails with the card and the badge
  // taken away, so they go together or not at all.
  //
  // THE BADGE HALF OVERRIDES A RULE CLAUDE.md STATES OUTRIGHT -- the badge "is the
  // load-bearing half and it stays", because e-ink holds its last image and a
  // screen left on the glass gives no clue the device is asleep rather than
  // frozen. It may go HERE because a full-bleed book cover is not a screen the
  // device can otherwise be in, so it is unambiguous by itself. That reason is
  // FALSE the moment no cover is on the glass, which is exactly why `covered` is
  // in this expression and not just `vm.shows`: every fallback -- no source, a
  // source that refused, a mode that never asked -- puts the badge back.
  //
  // Two conditions spelled separately would drift, and this project has shipped a
  // dead button twice from that shape.
  const bool coverOnly = covered && vm.shows == SleepShows::Cover;

  const Font& label = fonts[Role::Meta500];
  const Font& title = fonts[Role::Title700];
  const Font& author = fonts[Role::Label400];
  const Font& progress = fonts[Role::Label500];
  const Font& note = fonts[Role::Meta400];

  // THE THREE STATES, IN ORDER OF HOW LITTLE THEY DRAW, and the badge is hoisted
  // ABOVE the card because the card's line budget is measured against it -- see
  // below. Nothing about the pixels moved with it: the bound makes the two boxes
  // disjoint by construction, so which is drawn first cannot matter, and
  // test_theme_sleep_golden.cpp asserts that disjointness rather than trusting it.
  //
  // design/SleepCover.dc.html drops the card and the badge TOGETHER -- see the
  // predicate above -- so this is the state that draws nothing but the picture.
  //
  // `!vm.waking` IS THE ONE EXCEPTION TO `coverOnly`, AND IT IS NOT A SECOND
  // CONDITION FOR THE CARD. The predicate above may drop the badge because a
  // full-bleed cover is not a screen this device can otherwise be in -- the
  // picture says "asleep" unaided. A WAKING screen is making a different claim,
  // and the cover is byte-identical in both states, so without these words a
  // COVER-mode wake would paint something indistinguishable from the sleep it is
  // waking from. The card stays suppressed by `coverOnly` alone: waking shows the
  // cover and the words, never the cover and the reading card. See
  // SleepViewModel::waking, which carries the whole of this reasoning.
  if (coverOnly && !vm.waking) return;

  // The badge, measured from the BOTTOM as the board positions it, and drawn in
  // BOTH remaining states -- which is what makes design/SleepIdle.dc.html one
  // screen with its content removed rather than a second screen.
  //
  // ITS TOP IS THE CARD'S BOUND, asked of the function that placed it rather than
  // recomputed from the board's 34px and the note face's line box. Deriving a
  // shared edge twice is how the header band ended up 6px out, and it is why
  // drawStatusBar asks hintBarHeight instead of measuring its own bar.
  const int badgeTop = drawBadge(fb, note, vm.note, plane);

  // THE BADGE ALONE, for either of two reasons, and BOTH of them are here.
  //
  // `nothingToContinue` is design/SleepIdle.dc.html: the card IS the reading state,
  // so with nothing to read there is nothing to put in it, and the badge is the half
  // that carries this screen's whole purpose -- telling the user the device is asleep
  // rather than frozen.
  //
  // `coverOnly` is the WAKING cover screen, and it must be tested again HERE rather
  // than only in the early return above. `vm.waking` suppresses the suppression for
  // the BADGE and must not reach the CARD: a waking cover screen is the cover and the
  // words, never the cover and the reading card. Dropping this term draws the card
  // over the picture, which is what test_theme_sleep_cover_golden.cpp's "a WAKING
  // cover screen keeps the badge and still drops the card" fails on -- and did, when
  // this function was restructured to hoist the badge.
  if (coverOnly || vm.nothingToContinue) return;

  // The card's width is the board's max, or the panel less a margin on the
  // narrower X4 -- `max-width` is a ceiling, not a pin.
  const int roomy = fb.width() - 2 * kMargin;
  const int cardW = roomy < kSleepCardMaxW ? roomy : kSleepCardMaxW;
  const int contentW = cardW - 2 * (kSleepCardBorder + kSleepCardPadX);

  // THE TITLE WRAPS, AND IT USED TO ELIDE. This is Home's arc and Home's reason
  // (renderHome says it at length): an ellipsis on a LIST ROW hides only which of
  // seven rows this is, and here it hides the one fact the screen exists to state.
  // This screen holds the glass for HOURS, so a name cut short is not a truncation
  // the reader presses past -- it is the truncation they live with.
  //
  // `WordBreak::Anywhere` for Home's reason too: a title that fell back to a
  // filename is usually one word, and there is no break opportunity at an
  // underscore or a hyphen.
  //
  // THE WRAP IS BOUNDED, AND THE BADGE IS WHAT BOUNDS IT. Everything else on this
  // screen is fixed, so the NAME is what yields -- but the thing a growing card
  // collides with is not the edge of the glass, it is the badge: an overrunning
  // title would run UNDER an opaque white box and be hidden by it, which is an
  // ellipsis by another name.
  //
  // AND THE RESERVE IS TAKEN TWICE, which is the whole subtlety. The card is
  // CENTRED, so centreIn splits the slack evenly: reserving the badge once still
  // leaves a tall card hanging half a badge into it. renderDeleteConfirm and
  // renderBookError both shipped exactly that defect -- a panel budgeted against
  // the whole canvas, sliced by the hint bar -- and this is the same arithmetic
  // one screen on. `fb.height() - badgeTop` IS the badge's footprint plus its
  // 34px offset, so this needs no number of its own.
  const int badgeReserve = fb.height() - badgeTop;
  const int cardRoom = badgeTop - badgeReserve;
  // Every child but the title AND THE AUTHOR, plus both of the board's paddings and
  // its border: five gaps, the label, the little rule, the bar with its own
  // margin-top, and the progress line. A constant here would be a second copy of
  // the box model a dozen lines below.
  //
  // THE AUTHOR'S LINE LEFT THIS SUM WHEN IT STOPPED BEING ONE LINE. It was a fixed
  // term here for exactly as long as the run could not wrap; now it is a result,
  // measured just below and added to both the budget and the height, which is the
  // move the title made one change earlier for the same reason.
  const int cardFixedH = 2 * (kSleepCardBorder + kSleepCardPadY) + label.lineHeight() +
                         kSleepGap + kSleepRuleH + kSleepGap + kSleepGap +
                         kSleepGap + kSleepBarTopGap + kSleepBarH + kSleepGap +
                         progress.lineHeight();

  // THE CHAPTER'S RESERVE -- WHAT THE TITLE'S BUDGET IS MEASURED AGAINST, AND NOT
  // WHAT THE CARD IS TALL BY. Those are two different quantities and nothing
  // requires them to be the same number; `chapterH` below is the other one.
  //
  // READ BOTH BEFORE UNIFYING THEM, because they will look like one expression
  // spelled twice and they are not. This is the RESERVE: always
  // kSleepChapterMaxLines, whatever the name does. `chapterH` is the ACTUAL: the
  // wrap's own height, one line for the 65.46% of corpus labels that fit one.
  // Swapping either for the other is a one-line change and each has its own test
  // (`the card's HEIGHT follows the chapter's actual wrap` and `the TITLE's budget
  // does NOT follow it`, in test_theme_sleep_golden.cpp) -- deliberately two, so a
  // reader who unifies them cannot get away with it by satisfying one.
  //
  // WHY THE BUDGET MAY NOT READ THE ACTUAL: A CHAPTER CHANGES WHILE THE BOOK IS
  // BEING READ AND AN AUTHOR DOES NOT. The title takes what the card's room leaves,
  // so a budget counting this run's second line only when the name USED it would
  // make the TITLE's line budget depend on where the reader is standing: cross a
  // chapter boundary and the book's name could reflow, or newly acquire an
  // ellipsis, because a page was turned. That is a visible defect with a baffling
  // cause, and it is the reason this run shipped fixed at one line. Reserved
  // unconditionally, the title's budget is a CONSTANT and the card's LAYOUT is a
  // function of the BOOK, exactly as it was at one line.
  //
  // WHAT THAT COSTS, stated rather than discovered: the title is CONSERVATIVE BY
  // UP TO ONE LINE, since a one-line chapter buys it nothing. Measured by
  // `sleep_chapter_probe` over 225 books -- only a title needing exactly
  // `budget + 1` lines can notice, which is 3 of 225 (1.33%), and in those three
  // books the seventh line is given up in 45 of their 119 chapters and kept in the
  // other 74. Every other corpus title either fits its six lines or would elide at
  // seven too.
  //
  // Zero when there is no chapter, in both quantities: an old pointer with no
  // chapter gets a shorter card rather than a blank band at its foot.
  const int chapterReserveH =
      vm.chapter.empty() ? 0 : kSleepGap + kSleepChapterMaxLines * progress.lineHeight();

  // THE AUTHOR IS WRAPPED FIRST, AND THE ORDER IS THE WHOLE OF HOW THE BUDGET IS
  // SPLIT. THREE runs on this card can grow, so two of them have to be measured
  // against a fixed rule and the last against what is left: the author takes
  // kSleepAuthorMaxLines (whose derivation is with the constant), the chapter takes
  // kSleepChapterMaxLines, and the title takes the remainder. That ordering IS the
  // design decision -- neither the author nor the chapter is the fact this screen
  // exists to state -- and it is expressed as a sequence rather than as a comment,
  // so it cannot drift from what is drawn.
  //
  // The chapter is the one whose fixed rule is a RESERVE rather than a cap: its
  // wrap is measured too, just below, but only the card's HEIGHT is allowed to see
  // it. See chapterReserveH above for why.
  //
  // The lead is the FACE's own line height, not a number of this screen's, because
  // the board sets no `line-height` on this run and so leaves it at `normal` --
  // which is what wrapProseLead is for. That is also what keeps a ONE-LINE author
  // byte-identical to the drawCentredText call this replaces: same box, same
  // baseline, same tracking, so no golden with a short author may move.
  //
  // `WordBreak::Anywhere` for the title's reason: a name can arrive as one
  // unbroken token with no break opportunity inside it.
  //
  // BOTH STRINGS ARE NAMED LOCALS AND HAVE TO BE -- `Prose::lines` are string
  // VIEWS into the text handed to the wrap, and clampProse's elided last line is a
  // NEW string that is not in that text. Home passed `upperLatin1(...)` inline
  // once and drew a column of NOTDEF BOXES from freed memory, correctly for a
  // short name and wrongly for a long one -- silently right in exactly the case
  // every golden covered.
  const std::string shoutedAuthor = upperLatin1(vm.author);
  std::string authorTail;
  Prose authorProse =
      wrapProseLead(author, shoutedAuthor, contentW, pxToF26(author.lineHeight()),
                    trackingEm(author, kSleepAuthorEm), WordBreak::Anywhere);
  clampProse(author, authorProse, kSleepAuthorMaxLines, contentW, authorTail);
  const int authorH = f26ToPx(authorProse.heightF26());

  // THE CHAPTER'S WRAP, HOISTED OUT OF THE DRAW SO THE CARD'S HEIGHT CAN SEE IT.
  // It was built at draw time, below the height, and that is exactly what made the
  // reserve the only number the height could have.
  //
  // THIS IS THE ACTUAL, AND `chapterReserveH` ABOVE IS THE RESERVE. The card is
  // tall by what the name TAKES -- one line for the 65.46% of corpus labels that
  // fit one -- because this is the card's LAST run and a reserved-but-unused second
  // line is 29px of dead space at the foot of a card that holds the glass for
  // HOURS, not spacing. The card is CENTRED, so a shorter card re-centres and no
  // type moves relative to any other type. design/Sleep.dc.html dropped its
  // `min-height: 58px` for this and carries the reasoning.
  //
  // DO NOT GIVE THIS EXPRESSION TO maxTitleLines BELOW, and do not give
  // chapterReserveH to cardH. Each swap has its own test; the pair of them is the
  // whole specification of the split.
  //
  // `WordBreak::Anywhere` and the FACE's own line height as the lead, both for the
  // reasons at the draw site -- and `chapterTail` is named here rather than there
  // because clampProse's elided last line is a NEW string, so it must outlive the
  // Prose it now sits above. `vm.chapter` itself needs no local: this run is NOT
  // shouted, so there is no temporary to dangle.
  const Tracking chapterTrack = trackingEm(progress, kSleepChapterEm);
  std::string chapterTail;
  Prose chapterProse;
  if (!vm.chapter.empty()) {
    chapterProse = wrapProseLead(progress, vm.chapter, contentW,
                                 pxToF26(progress.lineHeight()), chapterTrack,
                                 WordBreak::Anywhere);
    clampProse(progress, chapterProse, kSleepChapterMaxLines, contentW, chapterTail);
  }
  const int chapterH = vm.chapter.empty() ? 0 : kSleepGap + f26ToPx(chapterProse.heightF26());

  // THE BUDGET TAKES THE RESERVE. Not chapterH -- see chapterReserveH.
  int maxTitleLines = (cardRoom - cardFixedH - authorH - chapterReserveH) / kSleepTitleLineH;
  if (maxTitleLines < 1) maxTitleLines = 1;

  // THE SHOUTED STRING AND THE ELIDED TAIL ARE BOTH NAMED, and they have to be:
  // `Prose::lines` are string_VIEWS into the text handed to the wrap, "which must
  // outlive the Prose" (components.h says so), and clampProse's last line is a NEW
  // string that is not in that text. Home passed `upperLatin1(vm.title)` inline
  // once: the temporary died at the end of the expression and drawProse read freed
  // memory, which rendered as a column of NOTDEF BOXES for a title long enough to
  // wrap and rendered correctly for a short one, because the freed bytes were still
  // there. Silently right in exactly the case every golden covered.
  //
  // Casing is applied before the wrap, not after: the caps run is wider than the
  // mixed-case one, so wrapping the original would break in the wrong places.
  const std::string shouted = upperLatin1(vm.title);
  std::string titleTail;
  Prose titleProse = wrapProseLead(title, shouted, contentW, pxToF26(kSleepTitleLineH), {},
                                   WordBreak::Anywhere);
  clampProse(title, titleProse, maxTitleLines, contentW, titleTail);

  // HEIGHT IS A RESULT, not a number the board states: it is the sum of six
  // children and five gaps, and pinning it would be the mistake the header band
  // and the menu rows both taught. The title's term is the wrap's OWN height rather
  // than one line, which is what lets the card grow with the name -- and it is asked
  // of the Prose rather than multiplied out here, so the height the card reserves
  // and the height drawProse consumes are ONE expression.
  //
  // THE CHAPTER'S TERM IS `chapterH`, THE ACTUAL, WHERE THE BUDGET ABOVE TOOK
  // `chapterReserveH`. That difference is deliberate and it is the whole of this
  // screen's chapter arithmetic: the card is tall by what the name takes, and the
  // title is budgeted against what the name might take. Read chapterReserveH before
  // making these one expression -- and note the consequence that makes the bound
  // safe: cardH can only be SHORTER than the budget was computed against, never
  // taller, so `cardH <= cardRoom` holds a fortiori.
  //
  // `cardFixedH` is the one term that really is shared with the budget above, and
  // it is the same expression for the same reason.
  const int cardH = cardFixedH + authorH + chapterH + f26ToPx(titleProse.heightF26());

  const int cardX = centreIn(0, fb.width(), cardW);
  const int cardY = centreIn(0, fb.height(), cardH);

  // Paper under the card, then its border: the card is opaque white ON the field,
  // so the dither has to be cleared rather than drawn around.
  fb.fillRect(cardX, cardY, cardW, cardH, true);
  outlineRect(fb, cardX, cardY, cardW, cardH, kSleepCardBorder);

  int y = cardY + kSleepCardBorder + kSleepCardPadY;
  const int cx = cardX + kSleepCardBorder + kSleepCardPadX;

  drawCentredText(fb, label, cx, contentW, baselineIn(label, y, label.lineHeight()), vm.label,
                  Ink::Black, trackingEm(label, kSleepLabelEm), plane);
  y += label.lineHeight() + kSleepGap;

  fb.fillRect(cx + centreIn(0, contentW, kSleepRuleW), y, kSleepRuleW, kSleepRuleH, false);
  y += kSleepRuleH + kSleepGap;

  // `ProseAlign::Centre` is the board's `text-align: center`, and drawProse centres
  // each line on its OWN measured width -- which is what drawCentredText did for
  // the one line this used to draw, through the identical centreIn call. So the
  // only thing that moved for a short title is the line BOX: 46px, the board's
  // `line-height: 1.1`, where this drew the face's own 53.
  y += f26ToPx(drawProse(fb, title, titleProse, cx, contentW, pxToF26(y), Ink::Black, plane,
                         ProseAlign::Centre));
  y += kSleepGap;

  // `ProseAlign::Centre` is the board's `text-align: center`, and drawProse centres
  // each line on its OWN measured width -- the identical centreIn this run reached
  // through drawCentredText while it was one line. The tracking rides on the Prose
  // rather than being passed again here, which is what stops the wrap and the
  // centring measuring the run differently.
  y += f26ToPx(drawProse(fb, author, authorProse, cx, contentW, pxToF26(y), Ink::Black, plane,
                         ProseAlign::Centre));
  y += kSleepGap + kSleepBarTopGap;

  // The bar: a 1px outline with a proportional fill, the treatment kBattery uses
  // and the same reason -- an outline plus a solid fill is what reads on this glass
  // hard-thresholded.
  const int barX = cx + centreIn(0, contentW, kSleepBarW);
  drawProgressBar(fb, barX, y, kSleepBarW, kSleepBarH, vm.progressPercent);
  y += kSleepBarH + kSleepGap;

  // THE PERCENTAGE, COMPOSED HERE FROM THE NUMBER THE BAR ABOVE IT READS. It was
  // a string on the view model -- `6% - CH. 01`, built by the shell -- and that
  // made the figure under the bar and the length of the bar two spellings of one
  // fact, free to disagree. renderHome composes its own the same way from the same
  // field. Not arithmetic the theme should not be doing: it is one integer and a
  // per-cent sign, where the SPINE POSITION this run used to carry was.
  drawCentredText(fb, progress, cx, contentW, baselineIn(progress, y, progress.lineHeight()),
                  std::to_string(vm.progressPercent) + "%", Ink::Black,
                  trackingEm(progress, kSleepProgressEm), plane);

  // THE CHAPTER, ON ITS OWN TWO LINES -- design/Sleep.dc.html carries the
  // measurement and the decision, and both matter here.
  //
  // IT MAY NOT GO THROUGH drawCentredText, which is what the run above it does and
  // what this run did while it was a spine position. That function places a run at
  // `centreIn(0, contentW, w)`, and centreIn returns a NEGATIVE half for a run
  // WIDER than its box: the name would begin left of the card's padding, paint over
  // both 2px borders onto the dither field, and be clipped by the panel edge with
  // no ellipsis to say so. That is not a hypothetical -- it is precisely the defect
  // the AUTHOR line above was fixed for, and every golden passed through it because
  // every golden's author was short. A chapter name off a real card is 34.54% likely
  // to be wider than this column, so it would have been the common case.
  //
  // THE WRAP IS WHAT KEEPS IT IN THE COLUMN NOW, and clampProse is the last resort
  // rather than the mechanism: `WordBreak::Anywhere` means every line the wrap
  // emits is at most contentW wide, so centreIn's half cannot go negative for any
  // of them -- and that holds for a name with no space in it, which is what
  // `Anywhere` is for. clampProse then elides the SECOND line for the 8.15% of
  // labels that need a third. `test_theme_sleep_golden.cpp` watches the card's
  // PADDING for an escape, and on this screen that is the only place ink is
  // evidence at all -- the dither field inks every row of the panel and the card's
  // own side borders ink every row of the card, so neither a full-row scan nor an
  // in-card extent can separate this run's ink from furniture that belongs there.
  //
  // THE WRAP ITSELF IS NOT HERE -- it is above cardH, because the card's HEIGHT
  // takes this run's ACTUAL height and a Prose built at draw time could not be
  // reached by that sum. This is the draw alone: `chapterProse` and `chapterTail`
  // are the hoisted locals, and the tail has to be one of them for its own reason
  // (clampProse's elided last line is a NEW string, so it must outlive the Prose).
  //
  // IT IS DRAWN AT THE TOP OF THE ROOM `chapterH` GAVE IT, which for a one-line
  // name is exactly one line: there is no slack under it to sit above, since
  // `chapterH` is the wrap's own height and the card ends at the bottom padding
  // below. A short name and a two-line name still share a first baseline relative
  // to the run above them; what a chapter crossing moves is the whole CARD, by
  // 29px, re-centred -- no type moves relative to any other type, and the title's
  // budget is a constant across it. See chapterReserveH.
  //
  // The lead is the FACE's own line height, which is the 29px the board states on
  // this run -- one number, reached from both sides.
  if (!vm.chapter.empty()) {
    y += progress.lineHeight() + kSleepGap;
    drawProse(fb, progress, chapterProse, cx, contentW, pxToF26(y), Ink::Black, plane,
              ProseAlign::Centre);
  }
}

// --- Settings ----------------------------------------------------------------
//
// The board's boxes: a 54px row with a `border-bottom`, and a section header that
// is `--t-meta` tracked caps in an 18/6 padding box under a 2px rule. Both are
// DERIVED from the type rather than pinned -- the 54 is the board's number at the
// board's face, and a role change would move it.
namespace {

// The board's `letter-spacing: 0.2em` on a section header, in thousandths as
// trackingEm takes it.
constexpr int kSettingsHeaderEm = 200;

constexpr int kSettingsRowH = 54;
constexpr int kSettingsRuleH = 1;
constexpr int kSettingsHeaderRuleH = 2;
constexpr int kSettingsHeaderPadTop = 18;
constexpr int kSettingsHeaderPadBottom = 6;
// Between a truncating label and its value -- the same 7px the Library band
// uses, and for the same reason: without it the ellipsis touches the value the
// moment the label fills the line.
constexpr int kSettingsLabelGap = 7;

int settingsHeaderHeight(const FontSet& fonts) {
  return kSettingsHeaderRuleH + kSettingsHeaderPadTop + fonts[Role::Meta500].lineHeight() +
         kSettingsHeaderPadBottom;
}

// One row's pitch INCLUDING its rule, which is what a list's arithmetic wants.
int settingsRowPitch() { return kSettingsRowH + kSettingsRuleH; }

}  // namespace

void QuietTheme::settingsMetrics(int panelH, const FontSet& fonts, int& listH, int& rowH,
                                 int& headerH) const {
  Hint hints[4];
  measuringHints(hints);
  const int area = panelH - headerBandHeight(fonts, nullptr) - hintBarHeight(fonts, hints);
  listH = area > 0 ? area : 0;
  // The RULED pitch, as libraryVisibleRows uses: the 1px-shorter unruled height
  // belongs only to the bottom of a list, and claiming it for every row would
  // promise room for one more row than there is.
  rowH = settingsRowPitch();
  headerH = settingsHeaderHeight(fonts);
}

namespace {

// design/Reader.dc.html's box model. The frame is `padding: 20px 18px 0 18px`, so
// the side margin is 18 rather than the 24 every chrome screen uses -- a reading
// column wants the width, and this is the one screen whose content is the book's
// rather than the app's.
constexpr int kReadPadTop = 20;
constexpr int kReadPadX = 18;
constexpr int kReadHeaderPadBottom = 22;  // the header row's `padding-bottom`
constexpr int kReadFooterPadTop = 12;     // the footer's `padding: 12px 0 16px 0`
constexpr int kReadFooterPadBottom = 16;
constexpr int kReadBarW = 210;
constexpr int kReadBarH = 5;
constexpr int kReadTitleEm = 180;  // MIDDLEMARCH, 0.18em
constexpr int kReadMetaEm = 120;
// The narrowest the chapter run may be squeezed to before the BOOK TITLE starts giving
// way instead. Enough for `CH. 01` plus an ellipsis, so the fallback form always fits
// whole and a long name always shows something.
constexpr int kReadChapterFloor = 96;   // the chapter, the percent and the counter, 0.12em
// design/LowBattery.dc.html's band: full-bleed, 78px tall, `padding: 0 18px` -- the
// page's own horizontal padding, so the band's two runs align with the header's book
// title and the footer's percentage. A 12px gap between the mark and its label.
constexpr int kBannerH = 78;
constexpr int kBannerPadX = 18;
constexpr int kBannerGap = 12;
constexpr int kBannerLabelEm = 100;  // letter-spacing: 0.1em, both runs
// U+2014, the real character. Every chrome face's subset carries it (tools/fontc.py
// adds it alongside the quotes and the ellipsis), so this is not a hyphen standing in.
constexpr std::string_view kEmDash = "\xE2\x80\x94";

}  // namespace

void QuietTheme::readerMetrics(int panelW, int panelH, const FontSet& fonts,
                               const GlyphSource& body, const Settings& settings,
                               PageMetrics& out) const {
  // Both bands are one line of --t-meta plus their padding. The header's two runs
  // are `align-items: baseline` and the same size, so the row is one line high;
  // the footer's tallest child is its text, not the 5px bar.
  const Font& meta = fonts[Role::Meta400];
  const int headerH = meta.lineHeight() + kReadHeaderPadBottom;
  const int footerH = kReadFooterPadTop + meta.lineHeight() + kReadFooterPadBottom;

  // THE MARGIN IS THE SETTING NOW, and kReadPadX is its default -- see
  // Settings::margins, whose middle step is this constant. Three separate claims
  // sit on that, and each needs its own reason rather than one "so" spanning all
  // of them.
  //
  // VERTICALLY, NOTHING MOVES. The band and the footer are full-bleed runs whose
  // HEIGHT is type -- one line of --t-meta plus padding, computed above -- so a
  // margin is not an input to either, and columnTop and columnH are therefore
  // independent of it. Asserted in test_theme_reader_metrics.cpp, because getting
  // it wrong costs a line of every page at one margin setting and nothing at
  // another, which is the hardest kind of layout bug to attribute.
  //
  // HORIZONTALLY, THE COLUMN MOVES AND THE CHROME DOES NOT, and that is a
  // DECISION the board does not state. renderReader draws the band's title and
  // the footer's percentage at kReadPadX unconditionally: they are chrome and keep
  // chrome's padding, where the column is the reading MEASURE and is the thing the
  // reader is being given control of. So at `margins = 30` the text sits 12px
  // inside the band's runs and at `margins = 10` it sits 8px outside them. Tying
  // the chrome to the setting instead would make the header band move on every
  // press of one row, which is the reflow the scroll-rail gutter decision already
  // refused once.
  //
  // AND `margins` IS TRUSTED AS A GEOMETRY HERE, which it may be because
  // Settings::validate SNAPS it onto kMarginSteps rather than clamping it to a
  // range -- so the only values that reach this line are 10, 18 and 30, and
  // columnW cannot go non-positive. This is the point where an integer becomes a
  // geometry, and a hand-built `margins = 400` on a 480px panel would hand the
  // wrap a columnW of -320. No caller bypasses loadSettings, which is what makes
  // that a precondition rather than a bug.
  out.columnLeft = settings.margins;
  out.columnTop = kReadPadTop + headerH;
  out.columnW = panelW - 2 * settings.margins;
  out.columnH = panelH - kReadPadTop - headerH - footerH;
  out.leadEm1000 = settings.lineSpacing;
  out.indentEm1000 = kBodyIndentEm;
  out.justify = settings.justify;
  // The body face's own tracking is the face's: the board sets no letter-spacing
  // on the reading column, and a book's text is the one run on this device that
  // must not be tracked -- the chrome's wide spacing is a chrome mannerism.
  out.tracking = {};
  (void)body;
}

void QuietTheme::renderReader(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                              const GlyphSource* italic,
                              const ReaderViewModel& vm, const Page& page, Plane plane) {
  const Font& meta = fonts[Role::Meta400];
  const Font& metaTitle = fonts[Role::Meta500];  // the header's book title, 21px/500
  const Font& metaPct = fonts[Role::Meta700];    // the footer's percentage, 21px/700

  // --- The header: the book, and the chapter, on one baseline ---
  //
  // `align-items: baseline` and both runs at --t-meta, so ONE baseline serves
  // both -- taken from the taller of the two faces so neither is clipped. Placing
  // each in its own box would centre two different line heights separately and
  // part the baselines by a pixel, which on two runs that the board explicitly
  // puts on a shared baseline is the whole point.
  const int bandH = metaTitle.lineHeight() > meta.lineHeight() ? metaTitle.lineHeight()
                                                              : meta.lineHeight();
  const int headBase = baselineIn(metaTitle, kReadPadTop, bandH);
  const Tracking titleTrack = trackingEm(metaTitle, kReadTitleEm);
  const int right = fb.width() - kReadPadX;

  // THE PRIORITY INVERTED WHEN THE CHAPTER BECAME A NAME. This reserved the chapter's
  // full width first, because `CH. 01` was `white-space: nowrap` on the board and the
  // book title was the run with slack to give up. The board now gives the chapter
  // `min-width: 0; text-overflow: ellipsis` and leaves the title alone -- so the NAME is
  // the run that yields, which is right: a chapter name runs long ("PREMIÈRE PARTIE : À
  // LIRE AVANT L'ACHAT") where a book title is a book title.
  //
  // THE TITLE IS STILL CAPPED, so the chapter can never be squeezed to nothing: it takes
  // its natural width up to everything but a floor for the chapter. Both runs elide, and
  // that matters -- either can be arbitrarily long on a real card.
  const Tracking metaTrack = trackingEm(meta, kReadMetaEm);
  const int row = fb.width() - 2 * kReadPadX;
  const int titleNatural = metaTitle.measure(upperLatin1(vm.bookTitle), titleTrack);
  const int titleCap = row - kReadPadX - kReadChapterFloor;
  const int titleW = titleNatural < titleCap ? titleNatural : (titleCap > 0 ? titleCap : 0);
  drawTextElided(fb, metaTitle, kReadPadX, headBase, upperLatin1(vm.bookTitle), titleW,
                 Ink::Black, titleTrack, plane);
  const int chapterRoom = row - titleW - kReadPadX;
  const std::string chapter = elideToWidth(meta, vm.chapter, chapterRoom, metaTrack);
  drawText(fb, meta, right - meta.measure(chapter, metaTrack), headBase, chapter, Ink::Black,
           metaTrack, plane);

  // --- The page ---
  //
  // Already positioned by reader/layout.h, in these coordinates. All this does is
  // draw each line with the stretch layout computed, which is what keeps
  // justification a property of the measurement rather than of the paint.
  // ONE CALL PER LINE, and it takes the same path as before when the line carries no
  // emphasis -- which is almost every line of almost every book.
  const StyledFace face{&body, italic, nullptr};
  for (const LaidLine& ln : page.lines) {
    // THE LIST MARKER, at the position the LAYOUT chose. Drawn in the roman whatever
    // the line is set in: a dash has no italic form worth the name, and an italic
    // list item with a slanted dash beside it reads as a rendering fault.
    if (ln.markerX >= 0)
      drawText(fb, body, ln.markerX, ln.baselineY, kListMarker, Ink::Black, {}, plane);
    drawTextStyled(fb, face, ln.x, ln.baselineY, ln.text, ln.emphasis, ln.extraPerGapF26,
                   Ink::Black, ln.tracking, plane);
  }

  // --- The footer: percent, bar, counter ---
  const int footerTop = fb.height() - kReadFooterPadBottom - meta.lineHeight();
  const int base = baselineIn(metaPct, footerTop, meta.lineHeight());

  // AN EM DASH FOR AN UNKNOWN PERCENTAGE TOO, and it is the SAME em dash the counter
  // draws for the same missing total -- this number is that counter as a fraction, so
  // two glyphs for one unknown would be two spellings of one fact. `0%` was the old
  // answer and 0 is a value the arithmetic reaches honestly (page 1 of a long chapter),
  // which is what made the unknown indistinguishable from the top of the chapter. See
  // design/Reader.dc.html's footer and ReaderViewModel::kProgressUnknown.
  const std::string pct =
      vm.progressPercent < 0 ? std::string(kEmDash) + "%"
                             : std::to_string(vm.progressPercent) + "%";
  const Tracking pctTrack = trackingEm(metaPct, kReadMetaEm);
  drawText(fb, metaPct, kReadPadX, base, pct, Ink::Black, pctTrack, plane);

  // AN EM DASH FOR AN UNKNOWN TOTAL, which the board states. Not a blank (the slash
  // would read as broken), not a zero (that would be a lie), and the same width
  // every time so the counter does not reflow when the number arrives.
  const std::string counter =
      std::to_string(vm.page) + " / " +
      (vm.pageTotal > 0 ? std::to_string(vm.pageTotal) : std::string(kEmDash));
  const int counterW = meta.measure(counter, metaTrack);
  drawText(fb, meta, right - counterW, base, counter, Ink::Black, metaTrack, plane);

  // THE CENTRE SLOT HOLDS ONE OF TWO THINGS, and design/ReaderAnchored.dc.html is
  // unambiguous about which: with a way back, the arrow and its label take the
  // progress bar's place rather than sitting beside it. The bar is what gives way
  // because the PERCENT IS STILL THERE on the left -- progress is still stated, as a
  // number instead of a length -- while there is nowhere else on this screen for the
  // return to live. The Reader draws no hint bar, which is the whole reason the
  // footer has to teach its own button.
  //
  // Either way it is `justify-content: space-between`'s middle child, so it is
  // centred across the WHOLE row rather than in the space left over -- which is what
  // the board's three equal-weight children resolve to, and is why both are placed
  // off fb.width() and not off the two measured runs.
  if (!vm.anchorLabel.empty()) {
    // `gap: 7px` between the mark and its text, and the mark is the same `kUp` the
    // hint bars draw -- the board's SVG is that chevron, so a second drawing of it
    // would be a second thing to keep in step.
    constexpr int kAnchorGap = 7;
    const Font& anchorFont = fonts[Role::Meta500];
    const Tracking anchorTrack = trackingEm(anchorFont, kReadMetaEm);
    const int textW = anchorFont.measure(vm.anchorLabel, anchorTrack);
    const int groupW = icons::kUp.w + kAnchorGap + textW;
    // NOT CENTRED, and that was the defect: a centred field names no button. Every
    // other screen puts a hint's label at its BUTTON'S PLACE IN THE ROW -- a bar
    // reads BACK, SELECT, UP, DOWN across the width in the physical order of the
    // buttons it names -- so the THIRD OF FOUR slots is how this device says "the UP
    // button". The centre falls between the second and the third and names neither.
    //
    // 5/8 of the content box is that slot's centre, which is what
    // design/ReaderAnchored.dc.html's `left: 62.5%` states. Clamped at the left
    // rather than trusted: the group is ~90px against ~320px of middle, so the clamp
    // cannot fire today, and it is here so that a wider label crowds the counter
    // rather than reaching back over the percent.
    const int content = fb.width() - 2 * kReadPadX;
    const int slotCentre = kReadPadX + content * 5 / 8;
    const int wantX = slotCentre - groupW / 2;
    const int groupX = wantX > kReadPadX ? wantX : kReadPadX;
    drawIcon(fb, icons::kUp, groupX, iconTopIn(footerTop, meta.lineHeight(), icons::kUp.h),
             Ink::Black, plane);
    drawText(fb, anchorFont, groupX + icons::kUp.w + kAnchorGap, base, vm.anchorLabel,
             Ink::Black, anchorTrack, plane);
  } else if (vm.progressPercent >= 0) {
    // AND NOTHING AT ALL WHILE THE PERCENTAGE IS UNKNOWN, which is the second half of
    // #93. A bar is a LENGTH stating the same fraction, and it has no dash to fall back
    // on: `drawProgressBar(..., 0)` is pixel-identical to the settled 0% the arithmetic
    // legitimately reaches, so drawing an empty track for an unknown reintroduces
    // exactly the ambiguity the run on the left was just fixed for. An absent claim
    // beats a false one -- the call this project already makes for an unread gauge
    // (-1, never 0%) and for the sleep card's absent chapter.
    //
    // Nothing reflows: this is the only slot that changes, and its two neighbours are
    // placed off the padding and off fb.width() rather than off each other. That is
    // the property the anchor branch above already relies on.
    const int barX = centreIn(kReadPadX, fb.width() - 2 * kReadPadX, kReadBarW);
    const int barY = iconTopIn(footerTop, meta.lineHeight(), kReadBarH);
    drawProgressBar(fb, barX, barY, kReadBarW, kReadBarH, vm.progressPercent);
  }

  // --- The low-battery banner ----------------------------------------------------
  //
  // LAST, SO IT IS ON TOP. It is drawn OVER the page and never displaces it: the
  // band inside the column would take a default page from 12 lines to 10 and
  // re-paginate the whole chapter, at the moment the device has least energy to
  // spend and with the reader's page moving under them. So `columnH` is untouched
  // and the last line and a half of the page go under the band -- which is what
  // design/LowBattery.dc.html draws, with the band absolutely positioned against
  // the column's bottom for exactly this reason.
  //
  // AND THAT IS WHERE THE y COMES FROM. The band's bottom IS the column box's
  // bottom, and readerMetrics puts that at `panelH - footerH` where `footerH =
  // kReadFooterPadTop + lineHeight + kReadFooterPadBottom` -- which is exactly
  // `footerTop - kReadFooterPadTop`. Derived from the same two terms the footer is
  // placed by rather than restated as a number, so a footer that moves takes the
  // band with it.
  //
  // FULL-BLEED, so it is placed from 0 and fb.width() rather than from kReadPadX.
  // Every inverted band on this device is.
  //
  // Meta700 AND NOT Label500, and the X4's fit is why: at 23px the label and the
  // hint collide on a 480px panel with the longest string the firmware can produce
  // (`BATTERY LOW - 10%`, since the X4's ADC reports 10% notches and 10 is the only
  // value its banner ever shows). The ramp has no 23px/700 role either. Measured in
  // Chrome on the board: 27px of gap at 480 wide, ~16px once the firmware's ~3%
  // wider advances are allowed for.
  if (vm.batteryLowPercent >= 0) {
    const Font& label = fonts[Role::Meta700];
    const int top = footerTop - kReadFooterPadTop - kBannerH;
    // `false` IS INK. Framebuffer::fillRect takes `white`, so the inverted band is
    // the FALSE case -- exactly as drawMenuRow's focused fill and the hint bar's rule
    // spell it. `true` here paints white on paper and the whole band vanishes, with
    // the white runs on top of it invisible too: a banner that renders as nothing.
    fb.fillRect(0, top, fb.width(), kBannerH, false);

    const Icon& warn = icons::kWarning;
    // WHITE INK on a filled band. The board authors the triangle white for the same
    // reason, and drawIcon takes the ink rather than the icon carrying it.
    drawIcon(fb, warn, kBannerPadX, iconTopIn(top, kBannerH, warn.h), Ink::White, plane);

    const Tracking track = trackingEm(label, kBannerLabelEm);
    // THE MIDDLE DOT IS ITS OWN LITERAL, and it must stay that way: a C++ hex escape
    // is UNBOUNDED, so `"\xC2\xB75%"` parses `\xB75` as one escape -- clang rejects
    // it and the ESP32's GCC accepts it and emits a byte that is not U+00B7. Adjacent
    // literals end the escape. The board writes `&middot;` with a space either side.
    const std::string text = std::string("BATTERY LOW ") + "\xC2\xB7" + " " +
                             std::to_string(vm.batteryLowPercent) + "%";
    drawText(fb, label, kBannerPadX + warn.w + kBannerGap,
             baselineIn(label, top, kBannerH), text, Ink::White, track, plane);

    const Tracking anyTrack = trackingEm(meta, kBannerLabelEm);
    const int anyW = meta.measure("ANY BUTTON", anyTrack);
    drawText(fb, meta, fb.width() - kBannerPadX - anyW, baselineIn(meta, top, kBannerH),
             "ANY BUTTON", Ink::White, anyTrack, plane);
  }
}

// --- The reader's menu -------------------------------------------------------
//
// design/ReaderMenu.dc.html. The SAME 340px panel the actions overlay draws -- eight
// boards share that box (components.h lists them) -- with a header that names the book
// and a column of 72px rows. So this is assembly and not new geometry, and it adds
// nothing at all to the shared primitives now: the one thing it used to add was a row
// that states a value, and with `Bookmarks` cut (#3) and `Names` after it (#73) no row
// on this sheet states a quantity. `drawPanelRow`'s value slot is pinned by
// test_components.cpp instead of by a caller here.
//
// THE ROW COUNT IS READ OFF THE VIEW MODEL, never written down. This comment has stated
// it as six and as four and been wrong both times; `vm.rows.size()` is the only place
// it lives, and the screen's own tests assert the property rather than the number.
constexpr int kReaderMenuPanelW = 340;

void QuietTheme::renderReaderMenu(Framebuffer& fb, const FontSet& fonts,
                                  const ReaderMenuViewModel& vm, Plane plane) {
  // NO fb.clear(): App::render has already painted the Reader, and this screen's whole
  // job is to be in front of it.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kReaderMenuPanelW);
  // The book's name, elided to the caption's column less what the progress value on
  // its right takes -- the actions panel's reasoning verbatim, and for the same reason:
  // a name allowed to wrap makes the panel a different height for every book, which on
  // an overlay also means a different partial-repaint footprint for every book.
  const Font& capValue = fonts[Role::Meta400];
  const int valueW =
      vm.progress.empty() ? 0
                          : capValue.measure(vm.progress, trackingEm(capValue, kHintEm)) + kBandGap;
  const std::string caption = elideToWidth(fonts[Role::Label500], upperLatin1(vm.bookTitle),
                                           panelCaptionColumnW(contentW) - valueW,
                                           trackingEm(fonts[Role::Label500], kBandLabelEm));
  const Prose label = wrapPanelCaption(fonts, caption, contentW);

  const int rows = static_cast<int>(vm.rows.size());
  int rowsH = 0;
  for (int i = 0; i < rows; ++i) rowsH += panelRowHeight(rowRuleFor(i, rows, i == vm.focusedRow));
  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) + rowsH;

  const int x = panelLeft(fb.width(), kReaderMenuPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kReaderMenuPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, vm.progress, plane);
  for (int i = 0; i < rows; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    const bool focused = (i == vm.focusedRow);
    // AN INERT ROW IS DRAWN EXACTLY AS AN UNFOCUSED LIVE ONE. `row.focusable` is
    // deliberately not read: the flag is about input, and a theme that dimmed on it
    // would be inventing a design decision nobody made. renderSettings says the same.
    cy += drawPanelRow(fb, fonts, cx, cy, contentW, row.label, focused, row.discloses,
                       rowRuleFor(i, rows, focused), plane, row.value, row.trackingEm1000);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawOverlayHintBar(fb, fonts, hints, plane);
}

// --- The table of contents ---------------------------------------------------
//
// design/Contents.dc.html. Structurally Settings: a header band, then a list that
// interleaves section headers with 64px rows, a rail when it overflows, and a hint
// bar. The row is `drawDetailRow`, whose own comment was written anticipating this
// screen -- "`focused` inverts it, which BookDetails never does and Contents does on
// the chapter you are in".
void QuietTheme::renderContents(Framebuffer& fb, const FontSet& fonts,
                                const ContentsViewModel& vm, Plane plane) {
  fb.clear(true);
  const int listTop = drawHeaderBand(fb, fonts, vm.title, upperLatin1(vm.bookTitle), nullptr,
                                     plane);
  int y = listTop;

  const int rows = static_cast<int>(vm.rows.size());
  // ASKED, not assumed: a book with a short contents does not overflow, and the gutter
  // exists only where the rail does -- reserving it on a list that fits leaves a white
  // strip beside the full-bleed focused row, which reads as a rendering fault.
  const int inset = vm.scrollable ? kListGutterW : 0;
  const int listW = fb.width() - inset;

  for (int i = 0; i < rows; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    if (row.isHeader) {
      // NO RULE ON ANY HEADER HERE, AND THAT IS WHERE THIS SCREEN DIFFERS FROM ITS
      // SETTINGS SIBLING -- which passes `i != 0` through this same primitive and gets
      // the boards' positional first-versus-later line. `Contents.dc.html` gives
      // NEITHER header a `border-top`, and `Settings.dc.html` gives every non-first one
      // a `border-top: 2px`. The two boards genuinely differ and the difference is a
      // design decision written down on the board: a Settings section is a change of
      // SUBJECT and a divider says so, where a part of a book is a soft hierarchy over
      // one continuous reading sequence, carried by the label's own 18/6 padding and
      // tracked caps.
      //
      // AND THIS LIST SCROLLS. Settings' rule is positional because the header band's
      // own 2px border is the separation at the top of the window; a header that ruled
      // at all would make that a question this screen has to answer correctly at every
      // scroll offset, for a line it wants nowhere. `false` has no offsets.
      //
      // WHAT THIS COST BEFORE IT WAS TRUE: `i != 0` here plus a row rule above it drew
      // a 3px full-width line, and that band was 1,440 of 13,274 differing pixels at X4
      // and 1,584 of 13,514 at X3 -- ~11% of the screen's whole mismatch (#81). The
      // header being 2px shorter moves nothing below it, because `drawSectionHeader`
      // returns the height it ACTUALLY drew; Settings shipped the other version of that
      // bug once.
      y += drawSectionHeader(fb, fonts, y, listW, upperLatin1(row.label), /*rule=*/false, plane);
      continue;
    }
    const bool focused = (i == vm.focusedRow);
    // THE LAST DRAWN ROW HAS NO RULE, which is renderLibrary's and renderSettings' rule
    // verbatim -- the list ends at the hint bar and a trailing hairline reads as a row
    // that was cut off. AND NEITHER DOES THE LAST ROW OF A SECTION: with the header
    // above drawing no line, a row rule there would be the only line between two
    // sections and the board draws none -- `VI` and `VIII` both drop their
    // `border-bottom`. The lookahead is here and the rule is `rowRuleFor`'s, because
    // `ListRow` is this view model's type and the rule is every sectioned list's.
    const bool nextIsHeader =
        (i + 1 < rows) && vm.rows[static_cast<size_t>(i + 1)].isHeader;
    y += drawDetailRow(fb, fonts, y, row.label, row.value, focused,
                       rowRuleFor(i, rows, focused, nextIsHeader), plane);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  // The rail spans the LIST, band to bar -- and it takes the list's top from the same
  // value the rows started at rather than re-deriving it, because a second
  // drawHeaderBand call here would paint the band twice as well as answer the question.
  if (vm.scrollable)
    drawScrollRail(fb, listTop, fb.height() - hintBarHeight(fonts, hints), vm.scrollFirst,
                   vm.scrollCount, vm.scrollTotal, plane);
  drawHintBar(fb, fonts, hints, plane);
}

int QuietTheme::contentsVisibleRows(int panelH, const FontSet& fonts) {
  Hint measuring[4];
  measuringHints(measuring);
  const int list = panelH - headerBandHeight(fonts) - hintBarHeight(fonts, measuring);
  const int pitch = detailRowHeight(true);
  return pitch > 0 && list > 0 ? list / pitch : 0;
}

void QuietTheme::renderSettings(Framebuffer& fb, const FontSet& fonts,
                                const SettingsViewModel& vm, Plane plane) {
  fb.clear(true);
  // The band carries the version, not a battery: the board's right slot is
  // `V 0.1.0`. Same band as Library's otherwise.
  const int listTop = drawHeaderBand(fb, fonts, vm.title, vm.version, nullptr, plane);
  int y = listTop;

  const int rows = static_cast<int>(vm.rows.size());
  // NOT overflowing in practice -- the list is seven items and about eleven fit --
  // but asked rather than assumed, so the day the reading settings still to come
  // push it over, the screen starts scrolling with nothing here changing.
  const bool overflowing = vm.totalRows > rows;
  const int inset = overflowing ? kListGutterW : 0;

  const Font& label = fonts[Role::Value500];
  const Font& labelFocused = fonts[Role::Value700];
  const Font& value = fonts[Role::Value700];
  const Font& header = fonts[Role::Meta500];

  for (int i = 0; i < rows; ++i) {
    const SettingsRow& row = vm.rows[static_cast<size_t>(i)];
    if (row.isHeader) {
      // A section's 2px rule, EXCEPT on the first item in the window -- the board
      // gives DEVICE a `border-top` and gives READING none,
      // because the first section sits directly under the header band's own 2px
      // border and a second rule doubles it into a 4px slab. Drawing it
      // unconditionally is exactly what this did, and it read as a stray separator
      // against the top bar.
      //
      // POSITIONAL, not by identity: the rule separates a section from the content
      // above it, and at the top of the window the band is that separation
      // whichever section happens to be scrolled there. Same shape as
      // drawBookRow's last-row rule, which is also about where a row is rather
      // than which row it is.
      // drawSectionHeader owns the box and the positional rule now -- Contents.dc.html
      // declares the same one byte for byte, and it returns the height it actually
      // drew so a first header being shorter cannot put the rows below it 2px low.
      y += drawSectionHeader(fb, fonts, y, fb.width() - inset, row.label, /*rule=*/i != 0, plane);
      continue;
    }

    const bool focused = (i == vm.focusedRow);
    // An INERT row is drawn exactly as an unfocused focusable one. `row.focusable`
    // is deliberately not read here -- see SettingsViewModel: the flag is about
    // input, and a theme that dimmed on it would be inventing a design decision.
    if (focused) fb.fillRect(0, y, fb.width() - inset, kSettingsRowH, false);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const Font& lf = focused ? labelFocused : label;

    const int rightEdge = fb.width() - inset - kMargin;
    const int valueW = row.value.empty() ? 0 : value.measure(row.value);
    // A ROW STATES A QUANTITY OR DISCLOSES A SCREEN, NEVER BOTH -- Home's menu rows
    // and drawPanelRow both state the rule, and the READING row is where this screen
    // first needed the disclosing half of it. They occupy the same right slot, so
    // the label's budget reserves whichever one this row has rather than their sum.
    const int trailingW = row.discloses ? icons::kChevron.w : valueW;
    // The label truncates and the value keeps its width, the same rule the
    // Library band states: the value is the state and the label is what it names.
    const int labelMaxW =
        rightEdge - kMargin - (trailingW > 0 ? trailingW + kSettingsLabelGap : 0);
    drawTextElided(fb, lf, kMargin, baselineIn(lf, y, kSettingsRowH), row.label, labelMaxW, ink,
                   {}, plane);
    // EXCLUSIVE, not two independent ifs: vm.rows already guarantees a disclosing
    // row's value is empty, but writing the branch this way means a future table
    // that broke that guarantee draws one mark or the other rather than a chevron
    // stamped over a value. `ink` follows the focus for both -- the chevron is WHITE
    // on the inverted row, which is exactly the row it is most likely to be on, and
    // a black one there would be invisible.
    if (row.discloses) {
      const Icon& chev = icons::kChevron;
      drawIcon(fb, chev, rightEdge - chev.w, iconTopIn(y, kSettingsRowH, chev.h), ink, plane);
    } else if (valueW > 0) {
      drawText(fb, value, rightEdge - valueW, baselineIn(value, y, kSettingsRowH), row.value, ink,
               {}, plane);
    }

    y += kSettingsRowH;
    // TWO reasons a row draws no rule, and both are the board's.
    //
    // The focused row's fill runs to the next row's top edge -- the same asymmetry
    // drawBookRow implements.
    //
    // And the LAST ROW OF A SECTION has none, because the next section's 2px
    // `border-top` is the line between them: `Typography` on the board carries no
    // `border-bottom` for exactly that reason. Drawing one anyway made a 3px slab
    // where the board draws 2, and -- because it also advanced `y` -- pushed every
    // row below the DEVICE header down by a pixel. That is the compounding kind:
    // one wrong rule, and the whole bottom half of the screen is off by one.
    // ...and the LAST DRAWN row has none either, which is renderLibrary's rule
    // verbatim (`i != rows - 1`): it leaves the list's bottom edge open rather
    // than hanging a hairline over the slack above the hint bar. Missing it left a
    // rule under `Sleep screen` that the board does not draw.
    //
    // The section-final term lives in `rowRuleFor` now rather than beside it here:
    // `renderContents` is the second sectioned list and had none of it (#81), and a
    // rule spelled once in a constexpr and once in a `&&` at a call site is the shape
    // that drifts. What stays here is the one-line lookahead that ANSWERS it, because
    // the row type is this view model's.
    const bool nextIsHeader =
        (i + 1 < rows) && vm.rows[static_cast<size_t>(i + 1)].isHeader;
    if (rowRuleFor(i, rows, focused, nextIsHeader)) {
      fb.fillRect(0, y, fb.width() - inset, kSettingsRuleH, false);
      y += kSettingsRuleH;
    }
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawScrollRail(fb, listTop, fb.height() - hintBarHeight(fonts, hints), vm.firstRow, rows,
                 vm.totalRows, plane);
  drawHintBar(fb, fonts, hints, plane);
}

// --- design/Typography.dc.html ------------------------------------------------
namespace {

// Every one of these is the board's own number. Nothing here is derived by this
// theme EXCEPT the preview box's height, which is the panel less all of them --
// see typographyPreviewBoxH.
constexpr int kTypoPreviewTop = 16;    // the box's `margin-top`
constexpr int kTypoPreviewBorder = 2;  // `border: 2px`
constexpr int kTypoPreviewPadY = 12;   // `padding: 12px 24px`
// THE HORIZONTAL PADDING'S BASE, NOT THE PADDING. The drawn padding is this plus
// however far the margin setting sits above the tightest step, so the board's own
// 24px is what `margins = 18` renders (16 + 18 - 10) and the three offered steps
// read 16, 24 and 36. See renderTypography, which does the arithmetic and states
// why the delta is exact.
// NO HORIZONTAL PADDING CONSTANT: the preview's is the MARGIN SETTING itself, read
// from the view model. design/Typography.dc.html states 18px, which IS `margins = 18`.
// A `16 + (margins - kMarginSteps[0])` base shipped for one commit and put 24px of
// padding against the book's 18px of margin -- reported off the device as the
// preview's margins being bigger than the book's, and they were, by 6px a side.
constexpr int kTypoLabelPadTop = 8;  // `LIVE PREVIEW`'s `padding: 8px 24px 10px`
constexpr int kTypoLabelPadBottom = 10;
constexpr int kTypoLabelEm = 120;  // `letter-spacing: 0.12em`
constexpr int kTypoRowsBorder = 2;  // the rows block's `border-top: 2px`
// 50, NOT kSettingsRowH's 54. Two boards, two numbers -- and pinning one to the
// other is exactly the class of defect this project records three times (the
// header band 6px out, menu rows compounding a pixel each, the hint bar's
// asymmetric padding). Derive from the board you are drawing.
constexpr int kTypoRowH = 50;
constexpr int kTypoRuleH = 1;           // `border-bottom: 1px` between rows
constexpr int kTypoFootPadBottom = 10;  // the footnote's `padding: 0 24px 10px`
constexpr int kTypoFootEm = 100;        // `letter-spacing: 0.1em`
// `line-height: 1.5`, em x 1000 as wrapProse takes it -- NOT the 1/64 px unit
// wrapProseLead takes. Handing 1500 to the latter would be a 23.4px line box on a
// 21px face, which is tighter than the face's own extent.
constexpr int kTypoFootLeadEm = 1500;
// THE FOOTNOTE'S COPY. In the theme rather than the view model because it is the
// BOARD'S text about how the screen behaves, not a fact about the current state --
// the same call renderSdMissing makes for its paragraph.
//
// It does two jobs. It answers the only question a reader actually has (the place
// IS kept: relayout lands at the top of the current block), and it states that the
// setting is DEVICE-WIDE -- which is why the band's right slot names no book.
//
// PURE ASCII, and that is worth one line: the copy it replaced carried an em dash,
// and this repo has been bitten twice by an unbounded C++ hex escape swallowing
// the character after it (`"\xB7C"`, `"\xA0b"`). There is no escape here to get
// wrong.
constexpr const char* kTypoFootnote = "APPLIES TO EVERY BOOK. YOUR PLACE IS KEPT.";

// HOW MANY OF A WRAP'S LINES HAVE THEIR INK INSIDE A BOX `boxH` PX TALL.
//
// Not `clampProse`, which takes a line budget and ellipsises the remainder -- both
// wrong here: the box is a window onto a fixed specimen rather than a budget, and
// an ellipsis on a type specimen reads as content withheld.
//
// AND `floor(boxH / lead)` IS WRONG NOW, WHICH IT WAS NOT WHEN THIS FUNCTION WAS
// WRITTEN. This comment used to say so plainly rather than overclaim -- the two
// rules agreed at every size and lead the screen could reach, and a mutation to
// floor() failed nothing in the suite, so the reason to ask about the ink was
// structural:
//
//   floor keeps line i when its LINE BOX fits. A line's ink exceeds its line box
//   by (extent - lead) / 2 whenever the lead is tighter than the face's extent,
//   which is a lead one step in settings.h away -- and the line the box's own
//   2px border then cuts through is the sliced line design/Reader.dc.html's column
//   once had. Asking about the ink cannot produce it, at any lead, for any face.
//
// THAT STEP WAS TAKEN. kLineSpacingSteps gained 1.0 and 1.2, both tighter than the
// body face's 48px extent at ppem 32, and the argument above turned into an
// observation: over the reachable space -- 5 sizes x 7 leads x 3 margins x 2 panels
// -- the two rules now differ in 13 of 210 cases, and at **(ppem 42, lead 1000) on
// the X4 floor draws a FIFTH line whose ink leaves the box**, which is the slice
// itself. The other twelve are loose leads where floor is merely one line stingier.
//
// So the disagreement window -- ~(extent - lead) / 2 px out of each line box -- is
// no longer too narrow for a test to land in, and test_theme_typography.cpp's
// "no drawn line's ink leaves the preview box" walks the whole space rather than a
// sample for exactly that reason: NO HAND-PICKED SAMPLE HAD (42, 1000). The list it
// replaced held 25, 32 and 46 at that lead and every one of them agreed with floor.
//
// TWO UNITS THAT ARE EASY TO GET WRONG, and both were wrong in the first draft of
// this function. `baselineInF26` takes 1/64 px and returns a WHOLE-PIXEL baseline
// (it is what drawProse hands drawText), so the result is converted rather than
// compared. And `descent()` is NEGATIVE and in whole pixels -- `lineHeight()` is
// `ascent - descent + lineGap` -- so the ink's bottom edge is `baseline - descent`.
// Adding it instead moves the edge UP by the descender, which silently keeps one
// line too many.
int previewLinesThatFit(const GlyphSource& face, const Prose& p, int boxH) {
  if (p.leadF26 <= 0) return 0;
  const int boxF26 = pxToF26(boxH);
  int fit = 0;
  for (int i = 0; i < p.lineCount(); ++i) {
    // baselineInF26 is the same helper drawProse places by, so this cannot
    // disagree with where the glyphs actually land.
    const int baselinePx = baselineInF26(face, i * p.leadF26, p.leadF26);
    const int inkBottomF26 = pxToF26(baselinePx - face.descent());
    if (inkBottomF26 > boxF26) break;
    ++fit;
  }
  return fit;
}

// THE PREVIEW BOX'S HEIGHT: the panel less every fixed run above and below it.
//
// DERIVED, NOT PINNED, which is CLAUDE.md's first invariant -- and this feature
// has already paid for ignoring it once: the board pinned `height: 292px`,
// computed from a footnote assumed to be two lines that rendered in three, and
// `flex-shrink`'s default of 1 absorbed the ~42px error silently.
//
// MEASURED TARGETS, so a mismatch here is visible immediately rather than at the
// comparison sheet -- and they are NOT the plan's 250/241. Read out of Chrome's
// own layout, per element, at both of compare-design.py's frame overrides, the
// board's box is **282px on the X4 and 274px on the X3** (254 and 246 of text
// area), and its footnote is TWO lines. 250/241 is exactly one 31.5px footnote
// line short of that, so it was measured while the copy still wrapped to three.
//
// This derives 280 and 272 -- 2px under, both accounted for: 1px is the focused
// row's dropped rule (see below) and 1px is two half-pixel Chrome line boxes,
// `LIVE PREVIEW` at 44.5 and the hint bar at 63.5. test_theme_typography.cpp
// carries the same arithmetic beside the numbers it asserts.
//
// It is FIXED with respect to the SETTINGS, which is the point: the box does not
// grow with the type, so the five rows below it never move.
//
// AND FIXED WITH RESPECT TO THE FOCUS, which is the other thing it must be. The
// rows block is measured with every between-row rule, and the row that is FOCUSED
// draws none (its fill runs to the next row's top edge, rowRuleFor's rule) -- so
// the block actually drawn is a pixel shorter than the block measured here. That
// pixel becomes slack above the bottom-anchored footnote, where a box height that
// tracked the focus would move the whole rows block on every press.
int typographyPreviewBoxH(const Framebuffer& fb, const FontSet& fonts, int bandH, int rowCount,
                          const Hint hints[4]) {
  const Font& meta = fonts[Role::Meta400];
  const int labelH = kTypoLabelPadTop + meta.lineHeight() + kTypoLabelPadBottom;
  // Rules BETWEEN rows only: the last row's bottom edge is the block's end, which
  // is renderSettings' and renderLibrary's rule verbatim.
  const int rowsH =
      kTypoRowsBorder + rowCount * kTypoRowH + (rowCount > 0 ? (rowCount - 1) * kTypoRuleH : 0);
  // The footnote is TWO lines at the board's measure on both panels -- and it is
  // ASKED rather than hardcoded, because the firmware's whole-pixel advances
  // measure ~3% wider than Chrome's and a board's measure is a number to check in
  // both engines (SdMissing's had to go 400 -> 420 for exactly this). The previous
  // copy was three lines, and assuming two is what cost the 42px above.
  const Prose foot = wrapProse(meta, kTypoFootnote, fb.width() - 2 * kMargin, kTypoFootLeadEm,
                               trackingEm(meta, kTypoFootEm));
  const int footH = f26ToPx(foot.heightF26()) + kTypoFootPadBottom;
  const int fixed =
      bandH + kTypoPreviewTop + labelH + rowsH + footH + hintBarHeight(fonts, hints);
  const int box = fb.height() - fixed;
  return box > 0 ? box : 0;
}

}  // namespace

void QuietTheme::renderTypography(Framebuffer& fb, const FontSet& fonts, const GlyphSource* body,
                                  const TypographyViewModel& vm, Plane plane) {
  fb.clear(true);

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // THE BAND'S RIGHT SLOT IS EMPTY, and that is the design: these settings are
  // device-wide, so there is no fact about "the book you are looking at" to put
  // there, and naming one book would contradict the footnote below.
  //
  // The band's HEIGHT does not change for it -- bandContentH() takes
  // max(Label500, Value700) unconditionally -- which is deliberate and is what the
  // board reserves a line box to match (its right slot is an `&nbsp;`): a band
  // that shrank when a screen left the slot empty would move every row beneath it.
  const int afterBand = drawHeaderBand(fb, fonts, vm.title, "", nullptr, plane);

  const int rowCount = static_cast<int>(vm.rows.size());
  const int boxH = typographyPreviewBoxH(fb, fonts, afterBand, rowCount, hints);

  // --- The preview box --------------------------------------------------------
  int y = afterBand + kTypoPreviewTop;
  outlineRect(fb, kMargin, y, fb.width() - 2 * kMargin, boxH, kTypoPreviewBorder);
  // THE PADDING IS THE MARGIN SETTING, AND THE DELTA IS EXACT RATHER THAN SCALED.
  // The box and the panel are the same device pixels, so there is nothing to scale:
  // one px of margin narrows the reading column by 2px (`panelW - 2 * margins`) and
  // narrows this measure by 2px as well, 1px of padding each side. The base is the
  // tightest step, so `margins = 10` draws the board's old 16px, the default 18
  // draws its current 24, and 30 draws 36.
  //
  // ONLY THE MEASURE MOVES. The border is placed from kMargin and boxH, neither of
  // which reads this, so no row below the box shifts -- which is the property the
  // derived-and-fixed box height exists for, and it would be lost if the setting
  // reached the outline instead.
  //
  // `margins` is TRUSTED AS A GEOMETRY here for the reason readerMetrics states:
  // validate() SNAPS it onto kMarginSteps rather than range-clamping, so the only
  // values that arrive are the table's. textW is checked against 0 below anyway,
  // because the view model is public and a test may build one by hand.
  // THE PADDING IS THE MARGIN, and the mapping is an IDENTITY rather than a scale
  // because the box and the panel are the same device pixels. So the whitespace
  // inside the box's border matches the whitespace beside the reader's column,
  // setting for setting, which is what "live preview" has to mean for this row.
  const int padX = vm.margins;
  const int textW = fb.width() - 2 * kMargin - 2 * kTypoPreviewBorder - 2 * padX;
  const int textH = boxH - 2 * kTypoPreviewBorder - 2 * kTypoPreviewPadY;
  // NO FACE, NO SPECIMEN -- the box is still drawn, because the box is the board's
  // and an absent preview is not an absent screen.
  if (body != nullptr && !vm.specimen.empty() && textW > 0 && textH > 0) {
    // The LEAD is the setting's, resolved against the face exactly as the board's
    // `line-height: 1.7` on `font-size: 32px` is -- which is what makes the box a
    // preview of Line spacing as well as of Size.
    //
    // AND THE ALIGNMENT IS THE SETTING'S TOO. With the padding above, that is all
    // four editable rows answered in the box.
    //
    // ProseAlign::Justify applies kMinJustifyFillPercent exactly as the reader's
    // page does, through the same stretchFor: justifying a line the page would
    // leave ragged would make the preview tidier than the book it previews, which
    // is a subtler wrong than not justifying at all.
    Prose p = wrapProse(*body, vm.specimen, textW, vm.leadEm1000);
    p.lines.resize(static_cast<size_t>(previewLinesThatFit(*body, p, textH)));
    // THE CLAMP ABOVE MOVES WHICH LINE IS LAST, and the direction it errs in is the
    // safe one. drawProse reads "last" as the last line it is GIVEN, so a specimen
    // the box cut short has its bottom drawn line set ragged where Chrome would
    // justify it and clip the rest. That is one line looser than the board, never
    // tighter -- and it does not arise in the state the board draws, where all four
    // lines fit and the fourth is the sentence's own last. The error that must never
    // happen is the opposite one, a genuinely full last line stretched to the
    // margin, and deciding by index is what forecloses it.
    drawProse(fb, *body, p, kMargin + kTypoPreviewBorder + padX,
              textW, pxToF26(y + kTypoPreviewBorder + kTypoPreviewPadY), Ink::Black, plane,
              vm.justify ? ProseAlign::Justify : ProseAlign::Left);
  }
  y += boxH;

  // --- `LIVE PREVIEW` ---------------------------------------------------------
  const Font& meta = fonts[Role::Meta400];
  const Tracking labelTrack = trackingEm(meta, kTypoLabelEm);
  y += kTypoLabelPadTop;
  drawText(fb, meta, kMargin, baselineIn(meta, y, meta.lineHeight()), "LIVE PREVIEW", Ink::Black,
           labelTrack, plane);
  y += meta.lineHeight() + kTypoLabelPadBottom;

  // --- The rows ---------------------------------------------------------------
  fb.fillRect(0, y, fb.width(), kTypoRowsBorder, false);
  y += kTypoRowsBorder;

  const Font& label = fonts[Role::Value500];
  const Font& labelFocused = fonts[Role::Value700];
  const Font& value = fonts[Role::Value700];
  for (int i = 0; i < rowCount; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    const bool focused = (i == vm.focusedRow);
    // AN UNFOCUSABLE ROW IS DRAWN EXACTLY AS AN UNFOCUSED FOCUSABLE ONE.
    // `row.focusable` is deliberately not read here -- the flag is about input, and
    // a theme that dimmed on it would be inventing a design decision. Settings'
    // render makes the same point in the same words.
    if (focused) fb.fillRect(0, y, fb.width(), kTypoRowH, false);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const Font& lf = focused ? labelFocused : label;

    const int rightEdge = fb.width() - kMargin;
    const int valueW = row.value.empty() ? 0 : value.measure(row.value);
    // The label truncates and the value keeps its width -- the Library band's
    // rule: the value is the state and the label is what it names.
    const int labelMaxW = rightEdge - kMargin - (valueW > 0 ? valueW + kSettingsLabelGap : 0);
    drawTextElided(fb, lf, kMargin, baselineIn(lf, y, kTypoRowH), row.label, labelMaxW, ink, {},
                   plane);
    if (valueW > 0)
      drawText(fb, value, rightEdge - valueW, baselineIn(value, y, kTypoRowH), row.value, ink, {},
               plane);

    y += kTypoRowH;
    // A rule BETWEEN rows only, and none under the focused row whose fill runs to
    // the next row's top edge. rowRuleFor is the shared spelling of both.
    if (rowRuleFor(i, rowCount, focused)) {
      fb.fillRect(0, y, fb.width(), kTypoRuleH, false);
      y += kTypoRuleH;
    }
  }

  // --- The footnote, bottom-anchored above the hint bar (`margin-top: auto`) ---
  const Tracking footTrack = trackingEm(meta, kTypoFootEm);
  const Prose foot = wrapProse(meta, kTypoFootnote, fb.width() - 2 * kMargin, kTypoFootLeadEm,
                               footTrack);
  const int footTop =
      fb.height() - hintBarHeight(fonts, hints) - kTypoFootPadBottom - f26ToPx(foot.heightF26());
  drawProse(fb, meta, foot, kMargin, fb.width() - 2 * kMargin, pxToF26(footTop), Ink::Black,
            plane, ProseAlign::Left);

  drawHintBar(fb, fonts, hints, plane);
}

// --- The peek ----------------------------------------------------------------
//
// design/Peek.dc.html. Book text over the veiled page, for looking somewhere else
// without going there. An overlay, so App::render has already painted the Reader
// underneath and this draws in front of it.
namespace {

// THE PANEL WIDTH IS THE CONSTANT, NOT THE VEIL MARGIN. The board is authored at
// 480 wide with `left: 34px; width: 412px`, symmetric about the frame -- and the
// intent it states is 34px of veil either side. Only one of the two can be pinned:
// pinning 34 would make the panel 460 on the X3, which changes its MEASURE, and the
// peek's text would then wrap differently on the two panels for no reason the design
// states. Pinning the width is the same call kActionsPanelW and kConfirmPanelW make,
// and for the same reason -- the two geometries are within ~2% of the same PPI, so a
// panel should be the same physical size on both and what adapts is where it sits.
constexpr int kPeekPanelW = 412;

// The band's `padding: 18px 20px` and its `border-bottom: 2px`, then the body's
// `padding: 16px 20px 20px 20px`. The side padding is one number because the board
// states one.
constexpr int kPeekPadX = 20;
constexpr int kPeekBandPadY = 18;
constexpr int kPeekBandRuleH = 2;
constexpr int kPeekBodyPadTop = 16;
constexpr int kPeekBodyPadBottom = 20;
// `PEEK`, at the band label's own 0.22em -- the same tracking the Library's band and
// the panels' captions carry. The value beside it is untracked, as the board sets it.
constexpr int kPeekLabelEm = 220;
// The band's `gap: 7px`, between the label and the value. It is what keeps the
// value's first character off the label's last when the value fills its room.
constexpr int kPeekBandGap = 7;

// THE PANEL'S BOX, DERIVED ONCE AND USED TWICE.
//
// peekMetrics places the column, renderPeek draws the border and the band around it,
// and peekVisibleLines answers how many lines fit inside it. Two spellings of one
// geometry is this project's first invariant, and this is the one function that
// forecloses it.
// AND IT NO LONGER DEPENDS ON THE READER'S TYPE AT ALL, which is the inversion stated
// as a signature: the old form took a `leadEm1000` because the height was derived from
// the line count, so a render that assumed the default lead drew the border in the
// wrong place. With the box fixed there is nothing here for a lead to move -- the
// COUNT is what moves, and that is peekLineCount's, asked separately by whoever needs
// it. PeekViewModel carried the lead solely to feed this and no longer does.
struct PeekBox {
  int x = 0, y = 0, w = 0, h = 0;  // the panel, in frame coordinates
  int bandH = 0;                   // its band, including the rule
  int columnLeft = 0, columnTop = 0, columnW = 0, columnH = 0;
};

// THE BAND'S HEIGHT, which is what makes the column a derivation rather than a
// literal: the panel is 546 and the band is a RESULT of the type ramp, so 436 of
// column is `546 - 110` only for as long as Value700 is 25px.
//
// It is `align-items: center` with two runs of different sizes, so its line box is
// the TALLER of the two faces -- taking the label's would clip the value, which is
// the bigger of them on today's ramp (25px against 23px).
int peekBandH(const FontSet& fonts) {
  const Font& label = fonts[Role::Label500];
  const Font& value = fonts[Role::Value700];
  const int bandLineH =
      value.lineHeight() > label.lineHeight() ? value.lineHeight() : label.lineHeight();
  return bandLineH + 2 * kPeekBandPadY + kPeekBandRuleH;
}

// The fixed panel less the border, the band and the body's own padding.
int peekColumnH(const FontSet& fonts) {
  return kPeekPanelH - (2 * kPanelBorder + peekBandH(fonts) + kPeekBodyPadTop +
                        kPeekBodyPadBottom);
}

// HOW MANY WHOLE LINES THAT COLUMN HOLDS. rowsThatFit is PageBuilder's own rule, so
// this cannot claim a line the pagination will not lay -- which is the failure mode
// the derivation had to be shared to foreclose.
//
// THE CLAMP TO 1 IS DEAD CODE ON THE SHIPPED RAMPS, and saying so is the point of
// having it: the widest line box either ramp can ask for is 46 * 2.000 = 92px against
// a 436px column, 4.7x of headroom. It exists because zero is not a count a panel can
// be built on -- a peek that reported none would draw an empty box with a band over
// it, indistinguishable from a book that failed to open -- and because a negative one
// is unreachable only for as long as kPeekPanelH stays above the band plus the
// padding. If it ever FIRES, the panel is reporting a line PageBuilder will refuse to
// lay, and the fix is the box and not this line.
//
// The static_assert below is half a proof and the ramp walk in
// test_theme_peek_metrics.cpp is the other half: a face's lineHeight() is a runtime
// fact, so this cannot see the band, and the test asserts a count of at least one at
// all 35 combinations with the real ramp loaded.
static_assert(kBodyPpemSteps[sizeof(kBodyPpemSteps) / sizeof(int) - 1] *
                      kLineSpacingSteps[sizeof(kLineSpacingSteps) / sizeof(int) - 1] / 1000 <
                  kPeekPanelH - (2 * kPanelBorder + kPeekBodyPadTop + kPeekBodyPadBottom),
              "the widest line box on the settings ramps must fit the peek's panel");

int peekLineCount(const FontSet& fonts, const GlyphSource& body, int leadEm1000) {
  const int n = rowsThatFit(peekColumnH(fonts), body.ppem(), leadEm1000);
  return n > 0 ? n : 1;
}

PeekBox peekBox(int panelW, int panelH, const FontSet& fonts) {
  PeekBox b;
  b.bandH = peekBandH(fonts);

  // THE BOX IS THE CONSTANT AND THE COUNT IS THE RESULT -- see kPeekPanelH, which
  // carries the two measurements that inverted this. The leftover between the last
  // whole line box and the foot of the column is SLACK, exactly as Typography's
  // preview box carries slack at large sizes.
  b.h = kPeekPanelH;
  b.columnH = peekColumnH(fonts);

  b.w = kPeekPanelW;
  b.x = panelLeft(panelW, kPeekPanelW);
  // `top: 50%; transform: translateY(-50%)` -- centred on the SCREEN, as the actions
  // panel is. The hint bar is drawn over the veil afterwards and the panel does not
  // reach it. A FIXED HEIGHT IS WHAT MAKES THAT TRUE BY CONSTRUCTION: 546 against 800
  // and 792 leaves 127px / 123px either side, where the derived height reached 846 at
  // the top of the ramp and centreIn handed back a negative origin.
  b.y = centreIn(0, panelH, b.h);

  b.columnLeft = b.x + kPanelBorder + kPeekPadX;
  b.columnW = panelContentW(kPeekPanelW) - 2 * kPeekPadX;
  b.columnTop = b.y + kPanelBorder + b.bandH + kPeekBodyPadTop;
  return b;
}

}  // namespace

void QuietTheme::peekMetrics(int panelW, int panelH, const FontSet& fonts,
                             const GlyphSource& body, const Settings& settings,
                             PageMetrics& out) const {
  const PeekBox b = peekBox(panelW, panelH, fonts);
  out.columnLeft = b.columnLeft;
  out.columnTop = b.columnTop;
  out.columnW = b.columnW;
  out.columnH = b.columnH;
  out.leadEm1000 = settings.lineSpacing;
  out.indentEm1000 = kBodyIndentEm;
  out.justify = settings.justify;
  // The body face's own tracking, as the reading column has: a book's text is the one
  // run on this device that must not be tracked.
  out.tracking = {};
  // `settings.margins` IS DELIBERATELY UNREAD, and the omission is stated rather than
  // silent. A margin is the reading PAGE's box model -- readerMetrics spends it on
  // columnLeft and columnW -- and this panel's box is its own, inset from the veil by
  // a width the design fixes. There is nothing here for it to apply to. `bodyPpem` is
  // absent for readerMetrics' reason: it has already arrived as `body`. Four
  // typography fields, two reads.
}

int QuietTheme::peekVisibleLines(const FontSet& fonts, const GlyphSource& body,
                                 const Settings& settings) const {
  return peekLineCount(fonts, body, settings.lineSpacing);
}

void QuietTheme::renderPeek(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                            const GlyphSource* italic, const PeekViewModel& vm,
                            const Page& page, Plane plane) {
  // NO fb.clear(): App::render has already painted the Reader, and this screen's whole
  // job is to be in front of it.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  // THE SAME BOX peekMetrics PLACED THE COLUMN IN, and it takes no typography to
  // compute -- kPeekPanelH is fixed, so the two callers cannot disagree about a lead
  // because neither of them has one. Before the box was pinned this took vm.leadEm1000,
  // and the view model carried that field for no other reader.
  const PeekBox b = peekBox(fb.width(), fb.height(), fonts);
  drawPanel(fb, b.x, b.y, b.w, b.h);

  // --- The band: `PEEK` left, the chapter and percent right --------------------
  //
  // ITS OWN BAND, NOT drawPanelCaption, and the difference is measured rather than
  // stylistic: that caption sets its value in Meta400 (21px) on 21px of padding where
  // this board says --t-value at weight 700 on 18px. Reusing it would draw the band
  // ~6px too tall, which is the header-band defect this project has already paid for.
  const Font& label = fonts[Role::Label500];
  const Font& value = fonts[Role::Value700];
  const int contentW = panelContentW(kPeekPanelW);
  // THE BAND'S RUNS SIT IN THE COLUMN THE BOX ALREADY PLACED. The band's `padding:
  // 18px 20px` and the body's `20px` are one number on the board, so the label, the
  // value and the text below them share one left edge and one measure -- read off the
  // box rather than re-derived, which is the whole reason peekBox exists.
  const int textLeft = b.columnLeft;
  const int colW = b.columnW;
  const int textRight = textLeft + colW;
  const int bandLineH = b.bandH - 2 * kPeekBandPadY - kPeekBandRuleH;
  const int bandTextTop = b.y + kPanelBorder + kPeekBandPadY;

  const Tracking labelTrack = trackingEm(label, kPeekLabelEm);
  const std::string title = upperLatin1(vm.title);
  drawText(fb, label, textLeft, baselineIn(label, bandTextTop, bandLineH), title, Ink::Black,
           labelTrack, plane);

  // ELIDED TO WHAT THE LABEL LEAVES. `PEEK` is fixed and the value is not: a real
  // chapter name runs long -- `PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT` is one from a
  // book on the user's own card -- and a run allowed to overflow reaches back over the
  // label rather than off the panel, because it is right-aligned.
  const int room = colW - label.measure(title, labelTrack) - kPeekBandGap;
  const std::string where = elideToWidth(value, vm.where, room, {});
  drawText(fb, value, textRight - value.measure(where, {}),
           baselineIn(value, bandTextTop, bandLineH), where, Ink::Black, {}, plane);

  fb.fillRect(b.x + kPanelBorder, bandTextTop + bandLineH + kPeekBandPadY, contentW,
              kPeekBandRuleH, false);

  // --- The peeked text ---------------------------------------------------------
  //
  // Already positioned by reader/layout.h, in these coordinates, exactly as the
  // Reader's page is -- so this is renderReader's loop and not a second one. The
  // marker is drawn in the roman whatever the line is set in, for its reason there.
  const StyledFace face{&body, italic, nullptr};
  for (const LaidLine& ln : page.lines) {
    if (ln.markerX >= 0)
      drawText(fb, body, ln.markerX, ln.baselineY, kListMarker, Ink::Black, {}, plane);
    drawTextStyled(fb, face, ln.x, ln.baselineY, ln.text, ln.emphasis, ln.extraPerGapF26,
                   Ink::Black, ln.tracking, plane);
  }

  // The peek's OWN bar: the Reader beneath has none at all, so this is the only place
  // the peek can say what it responds to.
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawOverlayHintBar(fb, fonts, hints, plane);
}

// ---------------------------------------------------------------------------
// THE V1.1 CONNECT FLOW
//
// Every number below is the board's, and the ones that are not derived say so.
// ---------------------------------------------------------------------------
namespace {

constexpr int kWifiProsePadY = 16;      // the on-demand sentence's box
constexpr int kWifiProseLeadEm = 1550;  // line-height: 1.55
constexpr int kWifiRowH = 80;           // a saved network, and a scan result
constexpr int kWifiMarkGap = 12;        // between a row's padlock and its meter
constexpr int kWifiEmptyGap = 16;       // the empty block's flex `gap`
constexpr int kWifiEmptyPadX = 40;
constexpr int kWifiTitleLeadEm = 1100;  // line-height: 1.1 on the empty title
constexpr int kRescanPadY = 21;
constexpr int kRescanGap = 12;
constexpr int kPickerNotePadBottom = 14;

// The keyboard, all from design/WifiPassword.dc.html.
constexpr int kKeyFieldMarginTop = 24;
// THE FIELD IS 80 PAINTED, NOT 76. The board says `height: 76px; border: 2px`
// on a div that does NOT set box-sizing, so 76 is the CONTENT box and the
// painted extent is 76 + two borders. Measured off the render: the design's
// field spans 80 rows and this read 76, which put every keyboard row 4px high.
// The first invariant, from the other direction -- a number the board states
// is not always the number it draws.
constexpr int kKeyFieldH = 80;
constexpr int kKeyFieldPadX = 16;
constexpr int kKeyFieldGap = 10;
constexpr int kKeyFieldEm = 80;  // 0.08em
constexpr int kCaretW = 10;
constexpr int kCaretH = 34;
constexpr int kKeyCounterPadTop = 10;
constexpr int kKeyGridPadTop = 24;
constexpr int kKeyGap = 3;
constexpr int kKeyW = 40;
constexpr int kKeyH = 52;
// The function row's four cells. WIDER AND UNEQUAL, which is the board's own
// declaration: 83 + 83 + 126 + 126 with three 3px gaps is 427, exactly what ten
// 40px cells and nine gaps make -- so the two row shapes share an edge.
constexpr int kKeyFnW[4] = {83, 83, 126, 126};
constexpr int kKeyRowW = 10 * kKeyW + 9 * kKeyGap;  // 427

// The centred block both empty states draw: a title over a paragraph, in the
// space the list would have had. Returns nothing -- it is placed by its caller,
// which knows what is below it.
void drawEmptyBlock(Framebuffer& fb, const FontSet& fonts, int top, int areaH,
                    std::string_view title, std::string_view prose,
                    std::string_view second, Plane plane) {
  const Font& titleFont = fonts[Role::Title700];
  const Font& bodyFont = fonts[Role::Label400];
  const int colW = fb.width() - 2 * kWifiEmptyPadX;
  const int colX = kWifiEmptyPadX;

  // Wrapped before anything is placed, because the block's height is what it
  // wraps to and the whole thing is centred on that total. Two wraps would be
  // two chances to disagree, which reads as a paragraph drifted off centre.
  const Prose titleProse = wrapProse(titleFont, title, colW, kWifiTitleLeadEm);
  const Prose p1 = wrapProse(bodyFont, prose, colW, kWifiProseLeadEm);
  const bool hasSecond = !second.empty();
  const Prose p2 =
      hasSecond ? wrapProse(bodyFont, second, colW, kWifiProseLeadEm) : Prose{};

  int blockF26 = titleProse.heightF26() + pxToF26(kWifiEmptyGap) + p1.heightF26();
  if (hasSecond) blockF26 += pxToF26(kWifiEmptyGap) + p2.heightF26();

  int yF26 = pxToF26(top) + (pxToF26(areaH) - blockF26) / 2;
  yF26 += drawProse(fb, titleFont, titleProse, colX, colW, yF26, Ink::Black, plane,
                    ProseAlign::Centre);
  yF26 += pxToF26(kWifiEmptyGap);
  yF26 += drawProse(fb, bodyFont, p1, colX, colW, yF26, Ink::Black, plane, ProseAlign::Centre);
  if (hasSecond) {
    yF26 += pxToF26(kWifiEmptyGap);
    drawProse(fb, bodyFont, p2, colX, colW, yF26, Ink::Black, plane, ProseAlign::Centre);
  }
}

// One 80px list row: a label on the left margin and whatever the caller draws
// on the right. Shared by the hub and the picker, which state the identical box
// -- `height: 80px; padding: 0 24px` -- and would otherwise be two copies of
// the fill-and-baseline dance.
int drawWifiRow(Framebuffer& fb, const FontSet& fonts, int y, int w, std::string_view label,
                bool focused, bool rule, int rightReserved, Plane plane) {
  if (focused) fb.fillRect(0, y, w, kWifiRowH, false);
  const Ink ink = focused ? Ink::White : Ink::Black;
  const Font& f = focused ? fonts[Role::Value700] : fonts[Role::Value500];
  const int maxW = w - 2 * kMargin - rightReserved;
  drawTextElided(fb, f, kMargin, baselineIn(f, y, kWifiRowH), label, maxW, ink, {}, plane);
  // The boards' positional rule: every row carries a bottom border except the
  // focused one, whose fill runs to the next row's edge, and the last drawn.
  if (rule) fb.fillRect(0, y + kWifiRowH, w, 1, false);
  return kWifiRowH + (rule ? 1 : 0);
}

}  // namespace

void QuietTheme::renderWifiSettings(Framebuffer& fb, const FontSet& fonts,
                                    const WifiSettingsViewModel& vm, Plane plane) {
  fb.clear(true);
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  // No mark in the band: the board's right slot is `ON DEMAND`, which is a
  // state and not a charge cell.
  int y = drawHeaderBand(fb, fonts, vm.title, vm.state, nullptr, plane);

  // The on-demand sentence, in its own bordered box under the band.
  const Font& proseFont = fonts[Role::Label400];
  const int colW = fb.width() - 2 * kMargin;
  const Prose prose = wrapProse(proseFont, vm.prose, colW, kWifiProseLeadEm);
  y += kWifiProsePadY;
  y += f26ToPx(drawProse(fb, proseFont, prose, kMargin, colW, pxToF26(y), Ink::Black, plane,
                         ProseAlign::Left));
  y += kWifiProsePadY;
  fb.fillRect(0, y, fb.width(), 1, false);
  y += 1;

  const int rows = static_cast<int>(vm.rows.size());
  // Where the SETUP section starts. The empty variant floats its copy in the
  // space above it, so the one row the two variants share stays at the foot --
  // see design/WifiSettingsEmpty.dc.html, which records that this DOES move the
  // row between the two states and why that is right.
  int fixedBelow = 0;
  if (vm.nothingSaved) {
    for (int i = 0; i < rows; ++i) {
      const ListRow& r = vm.rows[static_cast<size_t>(i)];
      fixedBelow += r.isHeader ? sectionHeaderHeight(fonts) : kWifiRowH;
    }
    const int barH = hintBarHeight(fonts, hints);
    drawEmptyBlock(fb, fonts, y, fb.height() - barH - fixedBelow - y, vm.emptyTitle,
                   vm.emptyProse, {}, plane);
    y = fb.height() - barH - fixedBelow;
  }

  const Font& value = fonts[Role::Value700];
  for (int i = 0; i < rows; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    if (row.isHeader) {
      // `rule=false` on BOTH headers: this board gives neither a `border-top`.
      // SAVED NETWORKS sits directly under the prose block's own 1px border,
      // and SETUP is separated by the list above it -- the same positional
      // judgement renderSettings makes, reaching the opposite answer because
      // the board says so.
      y += drawSectionHeader(fb, fonts, y, fb.width(), row.label, /*rule=*/false, plane);
      continue;
    }
    const bool focused = (i == vm.focusedRow);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const int trailingW = row.discloses ? icons::kChevron.w : value.measure(row.value);
    y += drawWifiRow(fb, fonts, y, fb.width(), row.label, focused,
                     rowRuleFor(i, rows, focused, i + 1 < rows &&
                                                      vm.rows[static_cast<size_t>(i + 1)].isHeader),
                     trailingW + kMargin, plane);
    const int rowTop = y - kWifiRowH - (rowRuleFor(i, rows, focused,
                                                   i + 1 < rows &&
                                                       vm.rows[static_cast<size_t>(i + 1)].isHeader)
                                            ? 1
                                            : 0);
    const int right = fb.width() - kMargin;
    if (row.discloses) {
      const Icon& chev = icons::kChevron;
      drawIcon(fb, chev, right - chev.w, iconTopIn(rowTop, kWifiRowH, chev.h), ink, plane);
    } else if (!row.value.empty()) {
      drawText(fb, value, right - value.measure(row.value), baselineIn(value, rowTop, kWifiRowH),
               row.value, ink, {}, plane);
    }
  }

  drawHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderWifiPicker(Framebuffer& fb, const FontSet& fonts,
                                  const WifiPickerViewModel& vm, Plane plane) {
  fb.clear(true);
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  // THE SCANNING STATE REPLACES THE HINT BAR, not the list: drawStatusBar is
  // the mechanism LibraryOpening and SleepWaking already board, and it derives
  // its box through hintBarHeight so the two are identical.
  const int barH = hintBarHeight(fonts, hints);

  int y = drawHeaderBand(fb, fonts, vm.title, vm.found, nullptr, plane);

  const Font& note = fonts[Role::Meta400];
  const Tracking noteTracking = trackingEm(note, 100);
  const Prose noteProse = wrapProse(note, vm.note, fb.width() - 2 * kMargin, 1500, noteTracking);
  const int noteH = f26ToPx(noteProse.heightF26()) + kPickerNotePadBottom;

  // THE FOOTER NOTE IS ONLY ON THE POPULATED BOARD. It explains what the
  // ROWS do -- open ones join directly, locked ones ask -- so on a screen with
  // no rows it is a caption for nothing, and design/WifiPickerEmpty.dc.html
  // draws none.
  const bool showNote = !vm.nothingFound;
  const int noteRoom = showNote ? noteH : 0;

  if (vm.nothingFound) {
    // The Rescan row still draws, anchored at the foot: it is the only action,
    // and an empty state with a live action is a different shape from one
    // without (HomeEmpty has none, and says why).
    const Font& lf = fonts[Role::Label500];
    const int rowH = 2 * kRescanPadY + lf.lineHeight();
    const int rescanH = 2 + rowH;
    drawEmptyBlock(fb, fonts, y, fb.height() - barH - rescanH - y, vm.emptyTitle, vm.emptyProse,
                   vm.emptyCaveat, plane);
    y = fb.height() - barH - rescanH;
    // DRAWN, not merely reserved. Reserving its height and never painting it
    // is what this did first: the space was right and the row was not there,
    // which on a screen whose only action it is reads as a dead end.
    fb.fillRect(0, y, fb.width(), 2, false);
    y += 2;
    const WifiScanRow* rescan = nullptr;
    for (const WifiScanRow& r : vm.rows) {
      if (r.isRescan) rescan = &r;
    }
    const bool focused = vm.focusedRow >= 0;
    if (focused) fb.fillRect(0, y, fb.width(), rowH, false);
    const Ink ink = focused ? Ink::White : Ink::Black;
    const Icon& mark = icons::kRescan;
    drawIcon(fb, mark, kMargin, iconTopIn(y, rowH, mark.h), ink, plane);
    drawText(fb, lf, kMargin + mark.w + kRescanGap, baselineIn(lf, y, rowH),
             rescan != nullptr ? rescan->ssid : std::string_view("Rescan"), ink, {}, plane);
  } else {
    const int listTop = y;
    const bool overflowing = vm.totalRows > static_cast<int>(vm.rows.size());
    const int inset = overflowing ? kListGutterW : 0;
    const int rows = static_cast<int>(vm.rows.size());
    for (int i = 0; i < rows; ++i) {
      const WifiScanRow& row = vm.rows[static_cast<size_t>(i)];
      const bool focused = (i == vm.focusedRow);
      if (row.isRescan) {
        // The board's own box: `padding: 21px 24px; border-top: 2px`, a mark
        // and a Label500 run rather than a Value row.
        fb.fillRect(0, y, fb.width() - inset, 2, false);
        y += 2;
        const Font& lf = fonts[Role::Label500];
        const int h = 2 * kRescanPadY + lf.lineHeight();
        if (focused) fb.fillRect(0, y, fb.width() - inset, h, false);
        const Ink ink = focused ? Ink::White : Ink::Black;
        const Icon& mark = icons::kRescan;
        drawIcon(fb, mark, kMargin, iconTopIn(y, h, mark.h), ink, plane);
        drawText(fb, lf, kMargin + mark.w + kRescanGap, baselineIn(lf, y, h), row.ssid, ink, {},
                 plane);
        y += h;
        continue;
      }
      const Ink ink = focused ? Ink::White : Ink::Black;
      // The right group: an optional padlock, then the meter, both on the
      // margin. Reserved before the label is drawn so a long SSID elides
      // against them rather than through them.
      const int lockW = row.locked ? icons::kLock.w + kWifiMarkGap : 0;
      const int groupW = lockW + kSignalW;
      const int rule = rowRuleFor(i, rows, focused);
      drawWifiRow(fb, fonts, y, fb.width() - inset, row.ssid, focused, rule, groupW + kMargin,
                  plane);
      const int right = fb.width() - inset - kMargin;
      drawSignalBars(fb, right - kSignalW, iconTopIn(y, kWifiRowH, kSignalH), row.bars, ink);
      if (row.locked) {
        const Icon& lock = icons::kLock;
        drawIcon(fb, lock, right - groupW, iconTopIn(y, kWifiRowH, lock.h), ink, plane);
      }
      y += kWifiRowH + (rule ? 1 : 0);
    }
    if (overflowing) {
      drawScrollRail(fb, listTop, fb.height() - barH - noteRoom, vm.firstRow,
                     static_cast<int>(vm.rows.size()), vm.totalRows, plane);
    }
  }

  // The footer note is CHROME rather than slack -- see the scrolled board:
  // drawing it only when the list FITS would delete it exactly when there are
  // most networks to disambiguate. That is a different question from whether
  // there is a list at all, which is what showNote asks.
  if (showNote) {
    drawProse(fb, note, noteProse, kMargin, fb.width() - 2 * kMargin,
              pxToF26(fb.height() - barH - noteH), Ink::Black, plane, ProseAlign::Left);
  }

  if (vm.scanning) {
    drawStatusBar(fb, fonts, vm.statusLabel, plane);
  } else {
    drawHintBar(fb, fonts, hints, plane);
  }
}

void QuietTheme::renderWifiPassword(Framebuffer& fb, const FontSet& fonts,
                                    const WifiPasswordViewModel& vm, Plane plane) {
  fb.clear(true);
  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  int y = drawHeaderBand(fb, fonts, vm.title, vm.ssid, nullptr, plane);

  // --- the field ---------------------------------------------------------
  y += kKeyFieldMarginTop;
  const int fieldW = fb.width() - 2 * kMargin;
  outlineRect(fb, kMargin, y, fieldW, kKeyFieldH, 2);
  const Font& fieldFont = fonts[Role::Body400];
  const Tracking fieldTracking = trackingEm(fieldFont, kKeyFieldEm);
  const int textX = kMargin + 2 + kKeyFieldPadX;
  // THE TEXT SCROLLS SO THE CARET AND THE TAIL STAY VISIBLE, head-first off
  // the left: a 63-character passphrase is far wider than this box, and a
  // field that elided its TAIL would hide the character just typed.
  const int textRoom = fieldW - 2 * (2 + kKeyFieldPadX) - kKeyFieldGap - kCaretW;
  std::string shown = vm.entered;
  while (!shown.empty() && fieldFont.measure(shown, fieldTracking) > textRoom) {
    shown.erase(shown.begin());
  }
  const int textW = fieldFont.measure(shown, fieldTracking);
  drawText(fb, fieldFont, textX, baselineIn(fieldFont, y, kKeyFieldH), shown, Ink::Black,
           fieldTracking, plane);
  fb.fillRect(textX + textW + kKeyFieldGap, y + centreIn(0, kKeyFieldH, kCaretH), kCaretW,
              kCaretH, false);
  y += kKeyFieldH;

  // --- the counter row ---------------------------------------------------
  y += kKeyCounterPadTop;
  const Font& meta = fonts[Role::Meta400];
  const Tracking metaTracking = trackingEm(meta, 100);
  const int metaBase = baselineIn(meta, y, meta.lineHeight());
  drawText(fb, meta, kMargin, metaBase, vm.counter, Ink::Black, metaTracking, plane);
  const int visW = meta.measure(vm.visibility, metaTracking);
  drawText(fb, meta, fb.width() - kMargin - visW, metaBase, vm.visibility, Ink::Black,
           metaTracking, plane);
  y += meta.lineHeight();

  // --- the grid ----------------------------------------------------------
  //
  // THE ONLY THING ON THIS SCREEN components.h HAS NO PRIMITIVE FOR, and it is
  // not extracted into one: a 10x4 character grid over a ragged function row
  // has exactly one caller, and this project undid an extraction made in
  // advance of a second (the Typography formatters).
  y += kKeyGridPadTop;
  const Font& cellFont = fonts[Role::Body400];
  const Font& cellFocused = fonts[Role::Body700];
  const Font& fnFont = fonts[Role::Meta500];
  const Tracking fnTracking = trackingEm(fnFont, 100);
  const int gridX = centreIn(0, fb.width(), kKeyRowW);

  int cell = 0;
  for (size_t r = 0; r < vm.rowWidths.size(); ++r) {
    const int cols = vm.rowWidths[r];
    const bool functionRow = (r + 1 == vm.rowWidths.size());
    int x = gridX;
    for (int c = 0; c < cols; ++c, ++cell) {
      const int w = functionRow ? kKeyFnW[c < 4 ? c : 3] : kKeyW;
      const bool focused = (cell == vm.focusedCell);
      if (focused) {
        fb.fillRect(x, y, w, kKeyH, false);
      } else {
        outlineRect(fb, x, y, w, kKeyH, 1);
      }
      if (cell < static_cast<int>(vm.cells.size())) {
        const std::string& label = vm.cells[static_cast<size_t>(cell)];
        const Ink ink = focused ? Ink::White : Ink::Black;
        const Font& f = functionRow ? fnFont : (focused ? cellFocused : cellFont);
        const Tracking t = functionRow ? fnTracking : Tracking{};
        drawCentredText(fb, f, x, w, baselineIn(f, y, kKeyH), label, ink, t, plane);
      }
      x += w + kKeyGap;
    }
    y += kKeyH + kKeyGap;
  }

  // --- the note ----------------------------------------------------------
  const int barH = hintBarHeight(fonts, hints);
  const Prose noteProse =
      wrapProse(meta, vm.note, fb.width() - 2 * kMargin, 1500, metaTracking);
  drawProse(fb, meta, noteProse, kMargin, fb.width() - 2 * kMargin,
            pxToF26(fb.height() - barH - kPickerNotePadBottom -
                    f26ToPx(noteProse.heightF26())),
            Ink::Black, plane, ProseAlign::Left);

  drawHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderWifiConnect(Framebuffer& fb, const FontSet& fonts,
                                   const WifiConnectViewModel& vm, Plane plane) {
  // No fb.clear(): WifiSettings underneath is already painted, and this
  // screen's whole job is to be in front of it.
  veilRect(fb, 0, 0, fb.width(), fb.height());

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  const int contentW = panelContentW(kActionsPanelW);
  const int colW = contentW - 2 * kPanelPadX;
  const Font& body = fonts[Role::Value500];
  const Font& note = fonts[Role::Meta400];
  const Tracking noteTracking = trackingEm(note, 100);
  const Icon& mark = icons::kWifi;

  const Prose label = wrapPanelCaption(fonts, vm.caption, contentW);
  // `WordBreak::Anywhere` FOR renderWifiError's REASON, and this screen
  // embeds the SAME string: an SSID is 32 arbitrary octets and need contain no
  // space, so the quoted name is one unbreakable token. Measured on the worst
  // case, a 32-byte name with nothing to break on runs to 727px against this
  // 296px column -- and the line is CENTRED, so it starts 215px left of the
  // column and ran off BOTH sides of the panel.
  //
  // The sibling one function down had this and said why; this one did not.
  const Prose message = wrapProse(body, vm.message, colW, 1300, {}, WordBreak::Anywhere);
  const Prose noteProse = wrapProse(note, vm.note, colW, 1500, noteTracking);

  const int bodyH = kConfirmProsePadY + mark.h + kWifiMarkGap + f26ToPx(message.heightF26()) +
                    kWifiMarkGap + f26ToPx(noteProse.heightF26()) + kConfirmButtonPadBottom;
  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) + bodyH;

  const int x = panelLeft(fb.width(), kActionsPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kActionsPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, vm.right, plane);
  cy += kConfirmProsePadY;
  // CENTRED, unlike BookError's left-aligned block: this board's body is
  // `align-items: center`.
  drawIcon(fb, mark, cx + centreIn(0, contentW, mark.w), cy, Ink::Black, plane);
  cy += mark.h + kWifiMarkGap;
  cy += f26ToPx(drawProse(fb, body, message, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black,
                          plane, ProseAlign::Centre));
  cy += kWifiMarkGap;
  drawProse(fb, note, noteProse, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black, plane,
            ProseAlign::Centre);

  drawOverlayHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderWifiError(Framebuffer& fb, const FontSet& fonts,
                                 const WifiErrorViewModel& vm, Plane plane) {
  veilRect(fb, 0, 0, fb.width(), fb.height());

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);

  const int contentW = panelContentW(kConfirmPanelW);
  const int colW = contentW - 2 * kPanelPadX;
  const Font& body = fonts[Role::Body400];
  const Icon& mark = icons::kWarning;

  const Prose label = wrapPanelCaption(fonts, vm.caption, contentW);
  // WordBreak::Anywhere for BookError's reason: an SSID is 32 arbitrary octets
  // and is frequently one unbreakable token, so under the default a real
  // network's name is drawn straight through the panel's right border.
  std::string tail;
  Prose prose = wrapProse(body, vm.message, colW, kConfirmProseLeadEm, {}, WordBreak::Anywhere);

  const int slabs = static_cast<int>(vm.actions.size());
  // The gap is BETWEEN items, so n slabs carry n-1 of them -- the arithmetic
  // BookErrorMemory's single slab made explicit, and the reason that shape's
  // panel is 80px shorter rather than 68.
  const int actionsH = slabs * kActionH + (slabs - 1) * kConfirmButtonGap + kConfirmButtonPadBottom;
  const int panelFixedH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) +
                          (2 * kConfirmProsePadY + mark.h + kBookErrorIconGap) + actionsH;
  const int proseRoom = centredPanelRoom(fb, fonts, hints) - panelFixedH;
  int maxProseLines = 1;
  while (maxProseLines < prose.lineCount() &&
         f26ToPx((maxProseLines + 1) * prose.leadF26) <= proseRoom)
    ++maxProseLines;
  clampProse(body, prose, maxProseLines, colW, tail);

  const int panelH = panelFixedH - (2 * kConfirmProsePadY + mark.h + kBookErrorIconGap) +
                     (kConfirmProsePadY + mark.h + kBookErrorIconGap +
                      f26ToPx(prose.heightF26()) + kConfirmProsePadY);

  const int x = panelLeft(fb.width(), kConfirmPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kConfirmPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, "", plane);
  cy += kConfirmProsePadY;
  drawIcon(fb, mark, cx + kPanelPadX, cy, Ink::Black, plane);
  cy += mark.h + kBookErrorIconGap;
  cy += f26ToPx(drawProse(fb, body, prose, cx + kPanelPadX, colW, pxToF26(cy), Ink::Black, plane,
                          ProseAlign::Left));
  cy += kConfirmProsePadY;

  for (int i = 0; i < slabs; ++i) {
    if (i > 0) cy += kActionH + kConfirmButtonGap;
    drawActionButton(fb, fonts, cx + kPanelPadX, cy, colW, vm.actions[static_cast<size_t>(i)],
                     i == vm.focusedAction, plane);
  }

  drawOverlayHintBar(fb, fonts, hints, plane);
}

void QuietTheme::renderWifiNetworkActions(Framebuffer& fb, const FontSet& fonts,
                                          const WifiNetworkActionsViewModel& vm, Plane plane) {
  veilRect(fb, 0, 0, fb.width(), fb.height());

  const int contentW = panelContentW(kActionsPanelW);
  // THE CAPTION IS THE SSID and the board truncates it on one line rather than
  // wrapping: an SSID is up to 32 arbitrary octets, and a caption allowed to
  // wrap makes the panel a different height for every network.
  const Font& capValueFont = fonts[Role::Meta400];
  const int valueW =
      vm.captionValue.empty()
          ? 0
          : capValueFont.measure(vm.captionValue, trackingEm(capValueFont, kHintEm)) + kBandGap;
  const std::string caption =
      elideToWidth(fonts[Role::Label500], upperLatin1(vm.caption),
                   panelCaptionColumnW(contentW) - valueW,
                   trackingEm(fonts[Role::Label500], kBandLabelEm));
  const Prose label = wrapPanelCaption(fonts, caption, contentW);

  const int rows = static_cast<int>(vm.rows.size());
  int rowsH = 0;
  for (int i = 0; i < rows; ++i) rowsH += panelRowHeight(i != vm.focusedRow && i != rows - 1);
  const int panelH = 2 * kPanelBorder + panelCaptionHeight(fonts, label) + rowsH;

  const int x = panelLeft(fb.width(), kActionsPanelW);
  const int y = centreIn(0, fb.height(), panelH);
  drawPanel(fb, x, y, kActionsPanelW, panelH);

  const int cx = x + kPanelBorder;
  int cy = y + kPanelBorder;
  cy += drawPanelCaption(fb, fonts, cx, cy, contentW, label, vm.captionValue, plane);
  for (int i = 0; i < rows; ++i) {
    const ListRow& row = vm.rows[static_cast<size_t>(i)];
    cy += drawPanelRow(fb, fonts, cx, cy, contentW, row.label, i == vm.focusedRow, row.discloses,
                       rowRuleFor(i, rows, i == vm.focusedRow), plane);
  }

  Hint hints[4];
  buildHints(kHintSlotMarks, vm.hints, vm.holds, hints);
  drawOverlayHintBar(fb, fonts, hints, plane);
}

}  // namespace reader
