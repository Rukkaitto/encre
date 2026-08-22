#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "reader/fontset.h"
#include "reader/glyphsource.h"
#include "reader/icons.h"
#include "reader/text.h"

namespace reader {
class Framebuffer;

// Fixed in pixels, not proportional: the X3 and X4 are within ~2% of the same
// PPI, so a margin should be the same physical size on both. What must adapt is
// the canvas width, which every primitive reads from the framebuffer.
inline constexpr int kMargin = 24;
// A menu row is 80 tall because the board says so in so many words
// (`height: 80px`), and the CONTINUE block 72 for the same reason.
// A menu row is the board's content box plus its own top border. The boards say
// `height: 80px` with `border-top: 1px`, and box-sizing is content-box there, so
// the rendered box is 81 -- the same pinned-number-ignoring-the-border mistake
// the two bars had. Every menu screen (Library, Settings, Wi-Fi settings...)
// stacks these, so a 1px error compounds per row.
inline constexpr int kRowContentH = 80;
inline constexpr int kRowRuleH = 1;
inline constexpr int kRowH = kRowContentH + kRowRuleH;

// --- The two bars that state a padding, not a height ------------------------
//
// The header band and the hint bar are the only chrome the boards size
// *implicitly*: neither declares a height. Each is padding around its tallest
// flex item plus a rule, and the browser adds them up. Both used to be pinned
// here as a single integer, and both integers were wrong -- the band by 6px,
// which put every screen's content 6px low, and the hint bar's text by 3px,
// because 20 + 27 + 16 + 1 happens to come to the pinned 64 while the padding
// is asymmetric and the primitive centred its content symmetrically.
//
// A pinned sum cannot be right for more than one type size, and the ramp has
// seven roles. So the padding is what is stated here -- which is what the boards
// state -- and the height is derived from it plus the content, the way Chrome
// derives it. That makes a bar's height depend on the type role it draws, which
// is the correct dependency: a screen whose band label is set larger gets a
// taller band, with no constant to remember to change.
//
// Both values are the boards' own, and they agree across every screen that has
// one: `padding: 18px 24px 14px 24px; border-bottom: 2px solid` on all 27
// screen header bands, `padding: 20px 24px 16px 24px; border-top: 1px solid` on
// 29 of the 30 hint bars (the exception is DirectionTerminal, an exploration
// board, not a V1 screen).
//
// The 8 boards that state something else for a top rule -- ReaderMenu,
// DeleteConfirm, GoToPage and the other overlays, all `padding: 21px 20px` --
// are not header bands: they are the caption of a modal panel, inset from the
// panel's own edge rather than the screen margin. When those screens are built
// they want their own primitive, not a widened kBandPadTop.
inline constexpr int kBandPadTop = 18;
inline constexpr int kBandPadBottom = 14;
inline constexpr int kBandRuleH = 2;
inline constexpr int kHintPadTop = 20;
inline constexpr int kHintPadBottom = 16;
inline constexpr int kHintRuleH = 1;
// There is no second line in a hint slot any more, and so no column gap: the
// long-press variant is a hollow ring beside the label (design 662557d). The
// board sets one `gap: 7px` on that flex row, which is kHintIconGap below, and
// it applies to the ring exactly as it applies to the leading mark. What used to
// be kHintHoldGap (`gap: 3px` on a flex *column*) describes a box the boards no
// longer draw, so it is gone rather than kept as a number nothing reads.

// Letter-spacing. The boards state it per run, in em, and the runs do not agree:
// 0.22em on the band's label, 0.18em on a menu row's, 0.20em on an action
// block's, 0.12em on a hint label, 0.16em and 0.10em on the two meta lines. One
// shared `kLabelTracking = 2` stood in for all six, which is right for exactly
// one of them and left every tracked label on every screen at the wrong width.
//
// What is stated here is the design's own number -- em in thousandths, nothing
// resolved, nothing rounded. Resolving it needs a pixel size, and the size now
// comes from the face itself (Font::ppem, declared by the asset), not from a
// private copy of the ramp's sizes kept alongside these constants. That copy was
// a second source of truth for every role, and it only covered the two roles
// today's screens tracked; a screen setting tracked text in Value or Title had
// nowhere to look. `trackingEm(font, kHintEm)` works for any role, including
// ones that do not exist yet.
//
// Nor is the product rounded to a whole pixel any more: 0.12em at 21px is
// 2.52px, and see reader/tracking.h for why that fraction has to survive.
inline constexpr int kBandLabelEm = 220;   // 0.22em, header band label
inline constexpr int kRowLabelEm = 180;    // 0.18em, menu row label
inline constexpr int kBlockLabelEm = 200;  // 0.20em, action block label
inline constexpr int kHintEm = 120;        // 0.12em, hint bar label
inline constexpr int kMetaEm = 160;        // 0.16em, the page-count meta line
inline constexpr int kTightMetaEm = 100;   // 0.10em, the chapter meta line
inline constexpr int kActionEm = 180;      // 0.18em, a prompt button's label
inline constexpr int kPromptTitleEm = 60;  // 0.06em, a full-screen prompt's title

// The design's em value, resolved against the face that will draw it.
inline Tracking trackingEm(const GlyphSource& font, int em1000) {
  return Tracking::em(font.ppem(), em1000);
}

// The design's `gap: 7px` between the header band's value and its battery
// glyph, and the same breathing room between a row's value and a trailing mark
// and between a hint's mark and its label -- the boards use one gap for all
// three, so this is one number three times rather than three numbers. The hint
// slot's gap is a flex `gap`, so it separates every child of that row: the
// leading mark from the label, and the label from the trailing hold ring.
inline constexpr int kBandGap = 7;
inline constexpr int kRowGap = 7;
inline constexpr int kHintIconGap = 7;

// A hint slot for a button with no action is not zero-wide: eight boards -- every
// one with a button that does nothing on that screen (SdMissing, HomeEmpty,
// BookDetails, WifiConnect, Transfer, SetupHotspot, ArticlesSetup,
// InstapaperConnect) -- author it as `<div style="width: 36px;"></div>`, and
// `justify-content: space-between` divides the leftover around it. Measuring an
// empty slot as 0 is not "drawing nothing", it is drawing the other slots in the
// wrong places: on SdMissing it moves the RETRY hint 36px left of the board's and
// widens each of the three gaps by 12px.
//
// It is a *width* only. The board's placeholder is 0 tall, and it makes no
// difference to the bar's height either way -- the height is the tallest of the
// four slots, and a bar has to have something in it to be worth drawing -- so an
// empty slot keeps the one line box every other slot has. That is what holds the
// invariant that the bar is exactly one line tall whatever its slots carry.
inline constexpr int kHintEmptySlotW = 36;

// One hint-bar slot: a leading mark, a label, and whether that button also has a
// long-press action.
//
// `hasHold` is a flag, not text. It used to be the second line of the slot
// (`"HOLD - ACTIONS"`), and the boards now say a long-press variant with a
// hollow ring after the label instead -- so there is nothing left to print, and
// a `std::string_view hold` would be a field whose contents no caller could see.
// Two real reasons for the change, both in design 662557d: a bar whose height
// varies with a hold moves every list stacked above it from screen to screen,
// and "- HOLD" does not fit a four-slot bar at 10pt on the 480-wide X4.
struct Hint {
  const Icon* icon;
  std::string_view label;
  bool hasHold;
};

// --- Derived heights -------------------------------------------------------
//
// The two bars' heights, computed the way the boards compute them: the padding
// above, the tallest thing inside, the padding below, and the rule. Both are
// exposed rather than left private to the draw calls because a caller sometimes
// has to know a bar's height *before* it draws it -- the hint bar draws itself
// at the bottom of the framebuffer, so a screen stacking menu rows above it
// needs the height to find their top edge. Asking the primitive is how that
// caller stays correct when the bar's content changes; a `kHintBarH` constant
// alongside it would just be the pinned number again under another name.

// Padding, the tallest of the band's own items, padding, rule. The items are a
// label, a value and the battery glyph, so the band tracks whichever of those
// roles is tallest -- which is the Value face on today's ramp (32 against the
// Label's 29 and the battery's 21), and would be the label on a screen that set
// its band label larger.
//
// `mark` is the glyph beside the value, and it may be null. Home's band carries
// the battery; the Library's (`LIBRARY / 12 BOOKS`) and Book details'
// (`ABOUT THIS BOOK / EPUB`) carry nothing at all, and their value's right edge
// lands on the margin instead. It is a parameter rather than a screen-side
// special case because the mark is part of the band's box model: it is one of
// the flex items the band's height is the tallest of.
int headerBandHeight(const FontSet& fonts, const Icon* mark = &icons::kBattery);

// The same, for the hint bar. A slot is exactly one line of Meta, at least as
// tall as the marks on that line; the bar takes the tallest slot. So the bar's
// height does *not* depend on whether any slot has a hold -- the ring rides on
// the same line as the label -- which is the point of the change: a bar that
// grew for a hold moved every list stacked above it. It still depends on the type
// role and on the marks, because that is what the board's box model depends on;
// the height is derived, never pinned.
int hintBarHeight(const FontSet& fonts, const Hint hints[4]);

// Each returns the height it consumed, so callers stack without recomputing.
// The band's value is drawn as one right-aligned group with the battery glyph.
// `plane` selects which bit-plane of a 2-bit grey level the text emits (see
// reader/text.h); it defaults to Plane::Bw so existing call sites are
// unaffected. It is passed on to drawIcon too, now that icons carry 2-bit
// anti-aliased coverage generated from the design's SVG and so differ per plane
// on every curve and diagonal. fillRect and ditherRect still need no plane:
// they are opaque by construction (coverage 0 or 3), identical in every plane.
int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value, const Icon* mark = &icons::kBattery,
                   Plane plane = Plane::Bw);
// `value` may be empty and `trailing` may be null; a row may carry either, both
// or neither. A trailing mark is right-aligned on the margin and takes the row's
// ink, so it reverses out of a focused row along with the text.
int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused, const Icon* trailing = nullptr,
            Plane plane = Plane::Bw);
// Draws at the bottom of fb. Reports each slot's x in slotXOut[4] so tests and
// callers can assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4],
                Plane plane = Plane::Bw);

// --- A prompt button -------------------------------------------------------
//
// The boards' primary action on a full-screen prompt: a filled slab with one
// centred label. Five V1 boards draw it (SdMissing, DeleteConfirm, BookError,
// WifiError, ArticleEnd) and all five state the same box -- `height: 68px`, no
// border, label `--t-value` at weight 700 and `letter-spacing: 0.18em`, the
// block an `align-items: center; justify-content: center` flex row. The width is
// NOT shared: SdMissing pins its own 260, the overlays take their column's.
//
// This is not Home's CONTINUE block, which is 72 tall, left-aligns its label
// against `padding: 0 20px` and carries a trailing arrow. Same idea, different
// box, and drawing one with the other's numbers is the pinned-height mistake
// again.
//
// Both forms are here now that DeleteConfirm has landed -- the board pairs a
// filled CANCEL with an outlined DELETE -- and `filled` picks between them. It
// changes the TYPE as well as the box, which is the part that is easy to miss
// and is stated identically by all four boards that draw the pair (DeleteConfirm,
// BookError, WifiError, and SdMissing which draws the filled one alone):
//
//   filled    background: #000; label --t-value  at 700, letter-spacing 0.18em
//   outlined  border: 2px solid; label --t-label at 500, letter-spacing 0.18em
//
// So the outlined variant is not "the filled one with a border instead of a
// fill": its label is a different role at a different weight, 23px/500 against
// 25px/700. Drawing both in Value700 is the mistake Home's author line already
// made once, and it would be invisible in review for the same reason.
//
// Which variant a button gets is the FOCUS, not the button's identity: the
// boards fill exactly the slab the focus is on (DeleteConfirm's CANCEL,
// WifiError's first of three) and outline the rest.
inline constexpr int kActionH = 68;
inline constexpr int kActionBorder = 2;  // the outlined variant's `border: 2px`

int drawActionButton(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                     std::string_view label, bool filled = true, Plane plane = Plane::Bw);

// --- A list row that is a book -----------------------------------------------
//
// design/Library.dc.html's row, which four boards draw (Library and the three
// that put a panel over it): a 44x64 thumbnail, then two stacked lines of text,
// then either a right-aligned value or a disclosure chevron.
//
//   padding: 11px 24px; gap: 16px; border-bottom: 1px solid
//   thumbnail  44x64      the dithered cover placeholder, or the folder mark
//   line 1     --t-body   500, or 700 when the row is focused; no tracking
//   line 2     --t-meta   400 at letter-spacing 0.10em
//   value      --t-value  700, right-aligned on the margin
//
// This is deliberately NOT drawRow widened. drawRow is the boards' *menu* row --
// `height: 80px`, one line of Label500 at 0.18em, a rule along its TOP -- and it
// differs from this one in its height, its rule's edge, the number of lines, the
// role and weight of every run on it, and whether it has a leading mark. A
// parameter for each of those would be one function with two layouts inside it
// and eight arguments to tell them apart, which is two primitives sharing a
// name: the next screen would pick whichever branch its neighbour used. What the
// two DO share is where their vertical placement comes from -- baselineIn,
// iconTopIn, centreIn -- and that is the part a fidelity bug lives in.
inline constexpr int kBookRowPadY = 11;    // the board's `padding: 11px 24px`
inline constexpr int kBookRowRuleH = 1;    // its `border-bottom: 1px solid`
inline constexpr int kBookThumbW = 44;     // `width: 44px; height: 64px`
inline constexpr int kBookThumbH = 64;
inline constexpr int kBookThumbGap = 16;   // the row's `gap: 16px`
inline constexpr int kBookLineGap = 3;     // the text column's `gap: 3px`
inline constexpr int kBookFocusBorder = 2;  // a focused cover's white border
inline constexpr int kBookCoverBorder = 1;  // an unfocused one's black border

// What one row says. A struct because there are four content fields and two
// pieces of state, and six positional arguments at a call site is how a title
// and a value end up transposed.
//
// `value` and `isFolder` are exclusive in the design rather than by construction:
// a folder discloses (chevron, no value) and a book states its progress (value,
// no chevron), which is the same rule renderHome applies to a menu row. Passing
// both draws both, and the caller is what keeps the design's rule.
struct BookRowContent {
  std::string_view title;
  std::string_view meta;   // the author, or "FOLDER - 6 BOOKS"; may be empty
  std::string_view value;  // "6%", "DONE", "NEW"; empty on a folder
  bool isFolder = false;
};

// The row's content box: the taller of the thumbnail and the two-line text
// column. Content-independent on purpose -- an empty author line must not make a
// row shorter than its neighbours -- so it depends on the faces and nothing else.
int bookRowContentH(const FontSet& fonts);

// The row's full height INCLUDING its bottom rule, which is the pitch a list of
// them stacks on. `bookRowHeight` is what a caller divides a panel by to learn
// how many rows fit; drawBookRow returns what it actually consumed, which is one
// pixel less for a row drawn without a rule.
//
// Those two numbers differ because the board's rule is a `border-bottom` on a
// content-box row, and the board omits it on the focused row and on the last
// row of the list -- so the pitch on the board genuinely varies by a pixel, and
// the rows below a focused one sit 1px higher than they would on an even grid.
// That is the board's own box model and this follows it; the alternative is an
// even grid that disagrees with the design by a pixel on five rows out of seven.
int bookRowHeight(const FontSet& fonts);

// `rule` draws the bottom border. A focused row does not have one (its fill runs
// to the row's bottom edge and a black line on black would be invisible anyway)
// and neither does the last row drawn, where the board leaves the list's bottom
// edge open rather than hanging a hairline over the slack above the hint bar.
// The column the scroll rail owns, which no row may enter.
//
// TAKEN ONLY WHEN A RAIL IS DRAWN. A list that fits gives up nothing: its rows
// run to the panel edge and the focused row's fill touches the border.
//
// Reserving it unconditionally was tried first, to spare a library crossing the
// visible-row count one reflow of its right-aligned values. That reflow is real
// but it happens ONCE, at the same moment the rail appears, which explains the
// shift. The reserved-but-empty gutter is there always, and a white strip beside
// a full-bleed black row reads as a rendering fault rather than as space. A
// defect you see every time beats a reflow you see once.
//
// It has to be a real column and not the outer margin: the focused row is
// FULL-BLEED INVERTED, so a black thumb crossing it would be black on black and
// vanish, and each row's 1px rule would run straight through the track. That was
// tried on the board and rendered exactly that way.
constexpr int kListGutterW = 14;

// The rail's own box, all from design/LibraryScrolled.dc.html: 6px wide with a
// 1px outline, its right edge 4px off the panel, and inset 6px from the list's
// top and bottom so the track clears the header band's rule and the hint bar's.
// 6 + 4 = 10 of the gutter's 14, leaving 4px of clear space to the rows.
constexpr int kRailW = 6;
constexpr int kRailRightGap = 4;
constexpr int kRailEndInset = 6;
constexpr int kRailBorder = 1;
// A thumb this short is dirt on the track rather than a position, so the
// proportion is floored here -- at the 256-row cap it would otherwise round to
// two or three pixels.
constexpr int kRailThumbMinH = 8;

// `rightInset` narrows the row's box -- kListGutterW for a list that has a rail's
// column, 0 for one that does not. It shifts the row's right-aligned value; the
// rule, the fill and the cover strip all follow the same narrowed box.
int drawBookRow(Framebuffer& fb, const FontSet& fonts, int y, const BookRowContent& row,
                bool focused, bool rule, Plane plane = Plane::Bw, int rightInset = 0);

// The scroll rail: an outlined track with a solid proportional thumb, drawn in
// the gutter beside a list that overflows.
//
// `first`, `visible` and `total` are rows. Draws nothing when the list fits
// (`visible >= total`), because a full-height thumb communicates nothing, and
// nothing when `total <= 0`.
//
// Axis-aligned and coverage 0-or-3 throughout, which is the BEST case on this
// glass rather than the worst: a vertical edge needs no anti-aliasing, so track
// and thumb are identical in every plane and every pass. (The thin-stroke warning
// this project records is about DIAGONALS -- kChevron -- not vertical bars.) An
// outlined track with a solid fill is the treatment kBattery already proves reads
// well hard-thresholded; a dithered track would get one and a half dots across a
// 4px grid, which is the moth-eaten failure the small round marks documented.
void drawScrollRail(Framebuffer& fb, int listTop, int listBottom, int first, int visible,
                    int total, Plane plane = Plane::Bw);

// --- Wrapped prose ---------------------------------------------------------
//
// The first paragraph in the firmware: SdMissing's explanation is body copy that
// wraps inside a `max-width: 400px` column, centred, at `line-height: 1.55`.
// Everything above this line is a single line of text in a box whose size the
// board states; a paragraph's height is instead a *result* -- of the face, the
// copy and the column -- and the screen cannot place anything below it without
// wrapping it first.
//
// So the wrap is a value, computed once and then both measured and drawn. Two
// calls that each re-wrapped would be two chances to disagree about the line
// count, and a disagreement would not look like a bug in the wrap: it would look
// like a paragraph that had drifted off centre.
//
// Line breaking is greedy on ASCII spaces, which is what the boards' copy needs
// and no more: no hyphenation, no break inside a word (a word wider than the
// column gets its own line and overhangs, visibly, rather than being silently
// cut), and no bidi or CJK line breaking. Phase 3's EPUB text is a different
// problem with a different budget.
//
// Note that this wraps against the FIRMWARE's own metrics, which are not
// Chrome's: the .rfnt faces are autohinted, so their advances are whole pixels
// and a long run measures ~3% wider here than the same string in the browser.
// The line count following the face is the honest behaviour -- the alternative is
// a paragraph laid out for a font the device does not have -- and it is why
// SdMissing's board now says `max-width: 420px` where it used to say 400. Its
// copy's second line is 401px in this face against 389.97 in Chrome's, so the
// board's own three lines came out as four here, with "retry." alone on the last.
// 420 is the same three lines in both engines, and Chrome renders the board
// pixel-for-pixel identically either way -- the wider box moves no break, no line
// and not the paragraph's height, because nothing in the copy can rise into the
// extra 20px. The number was wrong, not the design.
inline constexpr int kProseMaxW = 420;     // SdMissing's `max-width: 420px`
inline constexpr int kProseLeadEm = 1550;  // its `line-height: 1.55`

struct Prose {
  // Views into the text passed to wrapProse -- which must outlive the Prose.
  std::vector<std::string_view> lines;
  // One line box, in 1/64 px: the board's line-height resolved against the face.
  // Fractional on purpose (1.55 x 29px is 44.95), so the lines below the first
  // do not accumulate a rounding error each.
  int leadF26 = 0;
  // Carried with the lines rather than passed again at draw time: the wrap and
  // the centring have to measure identically, and a second Tracking argument at
  // the draw call is a way for them not to.
  Tracking tracking{};

  int lineCount() const { return static_cast<int>(lines.size()); }
  // The paragraph's own height, the way the board computes it: line boxes, times
  // the line-height. In 1/64 px, because that is what it is.
  int heightF26() const { return lineCount() * leadF26; }
};

// Whether a word wider than the column may be broken inside.
//
// `Normal` is the boards' default and what every paragraph wants: the copy is
// prose, its words are words, and a break inside one would be a hyphenation
// decision nothing here is qualified to make. `Anywhere` is the boards'
// `overflow-wrap: anywhere`, declared on exactly two runs -- BookDetails' title
// and DeleteConfirm's caption -- and it exists because those two runs carry a
// FILENAME. A filename is frequently one unbreakable word ("Middlemarch_George
// _Eliot"), and CSS offers no break opportunity at an underscore, so the choice
// on those two runs is between breaking inside the word and hanging it out past
// the panel. The boards chose; this is the parameter that says which.
enum class WordBreak { Normal, Anywhere };

Prose wrapProse(const GlyphSource& font, std::string_view text, int maxW, int leadEm1000,
                Tracking tracking = {}, WordBreak breaking = WordBreak::Normal);

// The same wrap for a run whose line box the board leaves at `line-height:
// normal` -- the face's own line height, which is not expressible as one of the
// design's em multiples without inventing a ratio the board never states. A
// wrapped LABEL (an overlay's caption) is that case; a paragraph states its
// multiple and uses the form above, which is implemented as a call to this one.
//
// The lead is in 1/64 px because that is the unit a line box lives in once it
// stops being a whole number, and taking it here rather than converting inside
// keeps the ONE rounding at the paint.
Prose wrapProseLead(const GlyphSource& font, std::string_view text, int maxW, int leadF26,
                    Tracking tracking = {}, WordBreak breaking = WordBreak::Normal);

// --- Bounding a wrapped run ---------------------------------------------------
//
// A wrap turns a width limit into a HEIGHT, and a height is the one thing two of
// this design's screens cannot let float: Book details is a fixed single-screen
// summary (spec 4.1b) whose field rows sit below the title, and an overlay's
// panel is centred on the screen and has to stay on it. Left unbounded, a long
// enough name pushes the rows off the bottom or grows the panel past the glass --
// which is the same defect as the overflow this change is fixing, turned ninety
// degrees.
//
// So a wrapped run that carries a filename is clamped: the first `maxLines` line
// boxes are kept and everything the wrap put below them is elided into the last
// one, which is what CSS `-webkit-line-clamp` does and what the two boards'
// `overflow: hidden` says in the vertical direction. The ellipsis therefore
// appears on a WRAPPING run too -- but only once the run has already been given
// every line the screen can spare, which is the difference between this and
// truncating a title outright.
//
// `maxLines` is derived by the caller from its own box model and never pinned:
// Book details divides the room its cover, its rows and its hint bar leave by the
// title's line box, and gets 3 on both panels today.
//
// `tail` is where the elided last line LIVES. A Prose holds views into the text
// it wrapped, and the clamped last line is a new string that is not in that text,
// so the caller owns it and it must outlive the Prose -- the same rule, and the
// same reason, as the text itself.
void clampProse(const GlyphSource& font, Prose& prose, int maxLines, int maxW, std::string& tail);

// How a wrapped run sits in its column. The boards want both: a full-screen
// prompt's paragraph is `text-align: center` and an overlay caption's wrapped
// label is a plain left-aligned block.
enum class ProseAlign { Centre, Left };

// Draws every line in the column [boxX, boxX + boxW), the first line box
// starting at `topF26`. Returns the height consumed, in 1/64 px, so a caller
// stacking below it does not have to recompute what it just drew.
//
// `align` comes after `plane` rather than beside `boxW` where it reads better,
// because moving `ink` or `plane` along would silently rebind every existing
// call site that passes them positionally.
int drawProse(Framebuffer& fb, const GlyphSource& font, const Prose& prose, int boxX, int boxW,
              int topF26, Ink ink = Ink::Black, Plane plane = Plane::Bw,
              ProseAlign align = ProseAlign::Centre);

// --- A detail row ------------------------------------------------------------
//
// design/BookDetails.dc.html's field rows and design/Contents.dc.html's chapter
// rows -- fourteen rows across two boards, stating one box:
//
//   height: 64px; padding: 0 24px; border-bottom: 1px solid
//   label  --t-value at 500        value  --t-value at 700, right-aligned
//
// Full-bleed and inset on the screen margin, like drawRow and unlike
// drawPanelRow. It is neither of them: drawRow is 80 tall with one Label500 run
// at 0.18em and its rule along the TOP, and drawPanelRow is 72 tall, inset on a
// panel's own 20px padding, and discloses with a chevron where this states a
// value. Same reasoning as bookRowHeight's: three boxes that differ in height,
// inset, rule edge and type are three primitives, not one with three modes.
//
// `focused` inverts it, which BookDetails never does and Contents does on the
// chapter you are in (`background: #000000; color: #ffffff` on one of its eight).
inline constexpr int kDetailRowContentH = 64;
inline constexpr int kDetailRowRuleH = 1;

int detailRowHeight(bool rule);
int drawDetailRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
                  std::string_view value, bool focused, bool rule, Plane plane = Plane::Bw);

// --- An overlay's panel ------------------------------------------------------
//
// The floating panel the overlay boards put over a veiled parent
// (LibraryActions, DeleteConfirm, ReaderMenu, GoToPage, ArticleActions,
// WifiConnect, WifiError, BookError -- eight boards, one box):
//
//   border: 2px solid; box-sizing: border-box; background: #ffffff
//   top: 50%; transform: translateY(-50%)          vertically centred
//   caption  padding: 21px 20px; border-bottom: 2px solid
//   row      height: 72px; padding: 0 20px
//
// The WIDTH is each board's own and is not shared -- LibraryActions is 340 on
// the 480 canvas and DeleteConfirm is 380, and an earlier draft of the 2C-2 plan
// generalised from the first and would have drawn the second 40px too narrow. Nor
// is the left edge: both boards centre their panel (70+340+70 and 50+380+50 both
// make 480), so it is derived from the canvas, and hardcoding 70 or 50 puts the
// panel 24px off centre on the 528-wide X3 -- which is the dev device.
inline constexpr int kPanelBorder = 2;
inline constexpr int kPanelPadX = 20;
inline constexpr int kPanelCaptionPadY = 21;
inline constexpr int kPanelCaptionRuleH = 2;
inline constexpr int kPanelRowContentH = 72;
inline constexpr int kPanelRowRuleH = 1;

// The panel's left edge on a canvas `canvasW` wide, and the content box inside
// its border. Trivial, and here rather than at the call sites because "centred"
// and "inside the border" are the two things every overlay has to get right on
// both geometries.
inline int panelLeft(int canvasW, int panelW) { return centreIn(0, canvasW, panelW); }
inline int panelContentW(int panelW) { return panelW - 2 * kPanelBorder; }

// Clears the panel to paper and draws its 2px border. Opaque by design: the veil
// has already knocked the parent back, and a panel the parent showed through
// would be unreadable. Nothing inside it needs to clear again.
void drawPanel(Framebuffer& fb, int x, int y, int w, int h);

// The panel's caption: a tracked caps label, and optionally a right-aligned
// value beside it (LibraryActions' `31%`; DeleteConfirm has none).
//
// The label WRAPS, which is why it arrives already wrapped. DeleteConfirm's
// caption is a sentence with a book's title shouted inside it -- `DELETE
// "DUBLINERS"?` -- and it is two lines on a 380px panel with the board's own
// sample title, more with a longer one. So the caption's height is a RESULT, the
// way a paragraph's is, and it is computed once and then both measured and drawn
// rather than wrapped twice in two places that could disagree.
//
// The line box is the FACE's own line height, not a multiple the board states:
// the caption is `line-height: normal` where a paragraph is `1.45`. Same wrap,
// same discipline, different source for the one number.
// `breaking` follows the board: LibraryActions' caption is a NAME and its board
// truncates it on one line, so the theme elides it before it gets here and the
// wrap has nothing to break; DeleteConfirm's is a SENTENCE with a name in it,
// which its board keeps wrapping and gives `overflow-wrap: anywhere` so the name
// cannot hang past the panel's border.
Prose wrapPanelCaption(const FontSet& fonts, std::string_view label, int contentW,
                       WordBreak breaking = WordBreak::Normal);
// The column a caption's label wraps and truncates inside -- the panel's content
// box less its own padding. Exposed because the theme has to elide a name against
// it BEFORE the wrap, and a second copy of `contentW - 2 * kPanelPadX` at the
// call site is a second answer to what the column is.
inline int panelCaptionColumnW(int contentW) { return contentW - 2 * kPanelPadX; }
int panelCaptionHeight(const FontSet& fonts, const Prose& label);
int drawPanelCaption(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                     const Prose& label, std::string_view value, Plane plane = Plane::Bw);

// One action row inside a panel. `x`/`w` are the panel's CONTENT box, not the
// panel: the row's label is inset by the board's 20px from the content edge, and
// passing the panel itself would inset it from the outside of the border and
// draw the label 2px left of the board's.
//
// The label's role follows the focus -- `--t-value` at 700 on the focused row,
// at 500 on the others -- which is the boards' own declaration and the same
// distinction Body500-versus-Body700 makes on a Library row. `discloses` is the
// trailing chevron: LibraryActions gives one to Open and Book details, which
// lead somewhere, and none to Mark as finished or Delete..., which act in place.
int panelRowHeight(bool rule);
int drawPanelRow(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                 std::string_view label, bool focused, bool discloses, bool rule,
                 Plane plane = Plane::Bw);

}  // namespace reader
