#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "reader/emphasis.h"
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
// A menu row is the board's content box plus its own top border, and the pitch
// is the sum: design/Main.dc.html says `height: 74px` with `border-top: 1px`
// and box-sizing is content-box there, so the rendered box is 75. Taking the
// declared height as the pitch is the pinned-number-ignoring-the-border mistake
// the two bars had.
//
// 74, WHERE IT WAS 80. The spine took the header band with it, and three rows
// plus the hint bar have to fit the column the band does not occupy.
//
// THIS IS HOME'S ROW AND NOBODY ELSE'S, which the previous note had wrong: it
// said "every menu screen (Library, Settings, Wi-Fi settings...) stacks these",
// and none of them do. Library draws drawBookRow, Settings and Contents
// drawDetailRow, the overlays drawPanelRow -- drawRow has exactly one production
// caller, renderHome's menu. Worth knowing before changing this number, because
// the old comment implied a blast radius that does not exist.
inline constexpr int kRowContentH = 74;
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
// The 7 boards that state something else for a top rule -- ReaderMenu,
// DeleteConfirm and the other overlays, all `padding: 21px 20px` --
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
// The menu row's own left padding inside the SPINE's column. design/Main.dc.html
// insets it 20px from the band's edge where the screen margin is 24 -- the
// column's padding, not the panel's, which is what keeps the label aligned with
// the stats above it. Passed to drawRow as `padL`; a full-width row keeps
// kMargin.
inline constexpr int kSpineRowPadL = 20;
inline constexpr int kBlockLabelEm = 200;  // 0.20em, action block label
inline constexpr int kHintEm = 120;        // 0.12em, hint bar label
// 0.2em, the loading line -- WIDER than a hint label on purpose, and it is the
// Sleep badge's tracking rather than the hint bar's. A hint label sits beside a
// mark and is read as one of four; this is a single centred statement with the
// whole bar to itself, and it is the same run Sleep already uses for exactly that
// (design/Sleep.dc.html's `ASLEEP - HOLD POWER TO WAKE`, and SleepWaking's
// `WAKING...` in the same badge). One spelling for one kind of line.
inline constexpr int kStatusEm = 200;
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

// The standard slot MARKS, in hardware order (Back, Confirm, Up, Down). Every
// screen but Home draws these four; Home's first slot is the one exception
// (kBook, because its first hint is READ), and it keeps its own array.
inline constexpr const Icon* kHintSlotMarks[4] = {&icons::kBack, &icons::kDot, &icons::kUp,
                                                  &icons::kDown};

// One view-model's hint arrays as the four Hint slots the bar draws. A slot's
// mark FOLLOWS its label: the boards author a dead button as an empty 36px slot
// (kHintEmptySlotW), and a mark over one would be an affordance for an action
// that is not there -- and it measures 32px where the board measures 36, which
// shifts every other slot along. This rule was applied at some call sites and
// not others (whose labels happened never to be empty), which is exactly the
// drift one helper exists to remove; the hold flag passes through untouched,
// because the ring rides with its binding whatever the label says.
void buildHints(const Icon* const marks[4], const std::array<std::string, 4>& labels,
                const std::array<bool, 4>& holds, Hint out[4]);

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
// `x0` is the row's left edge and `padL` its own inset from that edge. Home's
// menu sits in the column beside the spine, so its rows begin at the band and
// inset by the COLUMN's 20px; a full-width row begins at 0 and insets by the
// SCREEN's margin. Two numbers because two boards, and they are separate
// parameters rather than one derived from the other -- folding padL into x0
// made a default-x0 row draw its label at 20 instead of kMargin, which no test
// pinned and no caller would have noticed until a screen without a spine used
// this again.
int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused, const Icon* trailing = nullptr,
            Plane plane = Plane::Bw, int x0 = 0, int padL = kMargin);
// THE LOADING LINE, AND IT REPLACES THE HINT BAR RATHER THAN JOINING IT.
//
// design/LibraryOpening.dc.html. The same box as the hint bar -- same rule, same
// padding, so the bar's height cannot differ between the two states and the list
// stacked above it cannot move when one replaces the other -- but one centred
// tracked run and no marks.
//
// WHY IT TAKES THE HINT BAR'S PLACE rather than sitting somewhere of its own: the
// hint bar is a promise about what the four buttons do, and while the device is
// blocked they do nothing. Replacing it is not a compromise for want of room, it is
// the more honest of the two states.
//
// IT IS DRAWN OVER A FINISHED FRAME, by the shell, and no screen knows it exists.
// That is what makes it one mechanism instead of a flag on every view model: the
// thing that knows an operation is taking too long is the shell, not the screen it
// is taking too long on. It CLEARS its own box first, because what is under it is
// the hint bar it is replacing.
//
// IT TAKES NO HINTS, and that is the point of the signature: the shell draws this
// over a finished frame and has no view model in hand, so requiring the screen's
// own hints would have put a lookup at every call site for a number that does not
// depend on them. A slot's height comes from its MARK and its type role, never
// from its label -- so the marks-only set is the same height as any real bar, and
// deriving it through hintBarHeight is what keeps the two boxes identical rather
// than merely equal today.
void drawStatusBar(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   Plane plane = Plane::Bw);

// THE TWO THINGS IT EVER SAYS, spelled once. The ellipsis is the single character
// U+2026 and not three full stops: the boards write `&hellip;`, and three periods
// at 0.2em tracking are three separated dots rather than an ellipsis.
//
// The shell owns WHEN each is shown; naming them here keeps the words with the
// component that draws them, and keeps the simulator's Sleep board and the
// device's wake from spelling the same line two ways.
inline constexpr const char* kStatusOpening = "OPENING\u2026";
inline constexpr const char* kStatusWaking = "WAKING\u2026";

// Draws at the bottom of fb. Reports each slot's x in slotXOut[4] so tests and
// callers can assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4],
                Plane plane = Plane::Bw);
// Without the slot report: what every production caller wants -- all seven were
// passing a dummy `int slots[4]` they never read. The out-parameter form stays
// for the tests that assert the distribution.
int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4],
                Plane plane = Plane::Bw);

// A rectangular outline of thickness `t`, as four fills, interior untouched --
// the treatment every bordered box on the boards shares: the sleep card and its
// badge, an unfocused action block, the outlined prompt button, the scroll
// rail's track and an overlay panel's border. It also drew the three placeholder
// covers' 1px and 2px borders and a focused book thumb's paper border on an
// inked row, and all four of those callers are gone -- so `white` has NO
// PRODUCTION CALLER LEFT, in the shape ditherRect's `Ink` is recorded in.
// Checked rather than assumed: all eight surviving call sites in core/src take
// the default, and test_components.cpp is the only thing that passes true. Kept
// as tested capability, since a paper border on an inked ground is a treatment
// the boards will ask for again.
// NOT for a box whose interior must be painted (Home's progress bar fills
// then hollows): an outline deliberately leaves the middle alone.
void outlineRect(Framebuffer& fb, int x, int y, int w, int h, int t, bool white = false);

// --- A proportional bar --------------------------------------------------------
//
// A 1px outline with a fill INSIDE it, proportional to `percent`. Three boards
// declare exactly this and declare it identically -- Main's reading progress,
// Sleep's, and Reader's footer -- each as
//
//   width: Wpx; height: Hpx; border: 1px solid #000; box-sizing: border-box
//
// wrapping a child at `width: P%; height: 100%`. `box-sizing: border-box` is the
// load-bearing word: the child's 100% is the CONTENT box, so the fill is `W - 2t`
// by `H - 2t` inset by `t`, and P% is a fraction of `W - 2t`, not of `W`.
//
// This exists because the three call sites had two different answers. Main insets
// the fill as the board says; Sleep painted it at full height over its own border
// and took its percentage of the outer width, so its 8px bar was 2px too tall and
// its fill up to 1px too wide -- on a bar 8 pixels tall, both are visible. Neither
// was wrong on purpose; the four lines are just short enough to retype and get
// subtly different, which is what a shared primitive is for.
//
// ROUNDED, not truncated, and once: Chrome resolves the child's width as a
// fraction and snaps at the paint, so 6% of 208 is 12.48 and paints 12 -- while
// 49% of 208 is 101.92 and paints 102, which truncation would put a pixel short.
// `percent` is clamped to 0..100 rather than trusted, because a reading position
// divided by a page count is arithmetic done elsewhere.
void drawProgressBar(Framebuffer& fb, int x, int y, int w, int h, int percent, int t = 1);

// One centred run in a box: measured, centred, drawn. The measure and the draw
// take the SAME tracking, or the centring is off by the tracking's total -- the
// hazard this exists to remove, since a caller centring by hand has to remember
// the tracking twice. `boxX`/`boxW` are the box the run centres in; the baseline
// is the caller's, because what varies between callers is the line box it came
// from, not the centring.
void drawCentredText(Framebuffer& fb, const Font& font, int boxX, int boxW, int baseline,
                     std::string_view text, Ink ink, Tracking tracking = {},
                     Plane plane = Plane::Bw);

// THE BOARDS' RULE FOR A LIST ROW'S BOTTOM RULE, positional rather than by
// identity: every row carries a `border-bottom` EXCEPT the focused one, whose
// fill runs to the next row's top edge, the last one drawn, which leaves the
// list's bottom edge open rather than hanging a hairline over the slack above
// the hint bar, and the LAST ROW OF A SECTION, because whatever the next
// section's header draws is the line between them and a row rule under it is a
// second one. Stated once because it was restated at three call sites, and one
// restatement once also advanced `y` -- the compounding kind of defect, every
// row below it a pixel low.
//
// `nextIsHeader` IS THE THIRD TERM AND IT ARRIVED ONE COPY LATE, which is this
// file's own second-copy rule again: `renderSettings` carried it hand-written as
// `rowRuleFor(...) && !nextIsHeader`, this comment carried the rest of it in
// PROSE ("screens with extra reasons to drop a rule (Settings' section
// boundaries) AND this together"), and `renderContents` -- the second sectioned
// list -- had neither. A rule half in a constexpr and half in a sentence is a
// primitive not yet finished, and the sentence is the half that does not get
// copied to the next caller. It drew a row's 1px rule straight into a header's
// 2px `border-top`: a 3px line where `Contents.dc.html` draws none, #81.
//
// It DEFAULTS to false, which is exactly right for every headerless list
// (Library, both overlay panels, the reader menu, Book details) rather than
// merely convenient: those view models cannot produce a header, so the answer is
// false by construction and none of them moves a pixel.
//
// It is deliberately NOT "does the next row exist and is it a header" over a row
// vector: the two sectioned screens hold DIFFERENT row types (`ListRow` and
// `SettingsRow`), so the shared half is the RULE and the per-screen half is the
// one-line lookahead that answers it.
constexpr bool rowRuleFor(int i, int rows, bool focused, bool nextIsHeader = false) {
  return !focused && i != rows - 1 && !nextIsHeader;
}

// --- HOW TWO RUNS SHARE ONE ROW, and the answer for the left-hand one -------
//
// Every `justify-content: space-between` pair in this file asks it -- a header
// band's label and value, a detail row's field name and its state -- and until
// #82 the answer was spelled once per site as "the value keeps its measured width
// and the label gets the remainder". That is right for a row whose VALUE is a
// literal and whose LABEL is a fact about the card, which is the shape both sites
// happened to have when they were written, and it is exactly wrong the other way
// round: Contents' band is `CONTENTS` beside the BOOK TITLE, so with `Amusing
// Ourselves to Death` on a real card the screen's own name came out as `C ...` at
// 480x800 and `C O N T ...` at 528x792. A band that cannot say which screen you
// are on is worse than a book title cut short.
//
// SO THE RUN THAT YIELDS IS DECIDED BY MEASUREMENT, NOT BY A FLAG: each run keeps
// its natural width for as long as the other's natural width leaves room for it,
// and neither may be squeezed below half the row they share. Written out, that is
// "the run with slack to give up is the one that has more of it" -- which is the
// design rule the boards state per screen, reached without asking the caller which
// of its two runs is data. `Library.dc.html` marks its LABEL as the yielding run
// (a subfolder's own name, against a derived count) and `Contents.dc.html` marks
// its VALUE (a book title, against the screen's name); this reproduces both, and
// no caller can forget to answer a question it is never asked.
//
// A `bool` PARAMETER INSTEAD WOULD BE A CALLER LIST, which this file has a rule
// about: seven band call sites, five of which have no yielding question at all
// because both their runs are literals that fit -- so five of the seven answers
// would be unverifiable, and the sixth would be this defect reintroduced the day a
// screen got it wrong.
//
// THE HALF-ROW FLOOR IS THE DERIVED FORM OF THE READER HEADER'S
// `kReadChapterFloor`, which is the same fix one band up (theme_quiet.cpp: the
// chapter's name yields and the book title is capped so the chapter can never be
// squeezed to nothing). That floor is a pinned 96 justified by knowing what the
// shortest fallback label is; this primitive knows neither run's content, so its
// floor is the only thing it can derive -- an equal division of the row it is
// dividing. It is a BOUND rather than a rendered behaviour: it engages only where
// BOTH runs are wider than half the row, which no board declares and no screen
// produces today (the widest band label in the firmware is `ABOUT THIS BOOK` at
// 268px, and the value beside it is `EPUB`).
//
// `avail` IS THE ROW THE TWO RUNS SHARE -- the content row less anything reserved
// beside them (the band's mark) and less the board's `gap: 7px` between them. The
// caller subtracts those, because what is reserved differs per primitive.
//
// Reduces to the old expression whenever the label is the run that yields, so
// every band and every detail row that shipped is pixel-identical: a label
// narrower than its budget is drawn whole either way (elideToWidth returns a run
// that fits untouched), and a label wider than it gets `avail - valueNatural`
// exactly as before.
constexpr int labelShare(int labelNatural, int valueNatural, int avail) {
  const int floored = avail - valueNatural > avail / 2 ? avail - valueNatural : avail / 2;
  return labelNatural < floored ? labelNatural : floored;
}

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

// --- The boards' bottom badge ------------------------------------------------
//
// A 1px-outlined box measured from the BOTTOM of the panel, holding one tracked
// caps label. design/Sleep.dc.html's `ASLEEP · HOLD POWER TO WAKE` and
// design/BatteryEmpty.dc.html's `CHARGE · HOLD POWER TO WAKE` are the same box to
// the pixel --
// 34px from the bottom, 1px border, 8/18 padding, --t-meta at 0.2em -- which is
// what makes this an extraction rather than a generalisation. (The two boards
// spell the 34 differently, Sleep as `position: absolute; bottom: 34px` and
// BatteryEmpty as a centring row's `padding-bottom`, and they render identically.)
//
// IT SIZES ITSELF TO THE LABEL and is centred on the panel, so the two screens'
// different words need no second set of numbers. Returns the badge's top y, which
// a caller that stacks anything above it needs and neither of today's two do.
//
// IT FILLS ITS INTERIOR unconditionally. Sleep's board says `background: #ffffff`
// because the badge sits on the dither field; BatteryEmpty's sits on paper and does
// not need it. Filling is correct on both and is what ships today.
//
// The label's tracking is the BADGE's (0.2em), not the hint bar's -- a hint label
// sits beside a mark and this one stands alone. One spelling for one kind of line.
int drawBadge(Framebuffer& fb, const GlyphSource& font, std::string_view label, Plane plane);

// --- A list row that is a book -----------------------------------------------
//
// design/Library.dc.html's row, which four boards draw (Library and the three
// that put a panel over it): a 44x64 thumbnail, then two stacked lines of text,
// then either a right-aligned value or a disclosure chevron.
//
//   padding: 11px 24px; gap: 16px; border-bottom: 1px solid
//   thumbnail  44x64      the 44x44 book or folder mark, centred in the slot
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
// The 44x64 SLOT, which both row kinds centre their mark in. It survives the
// placeholder cover's removal because it is what `bookRowContentH` takes the max
// with, so it sets the row's height and the whole list's geometry -- not because
// anything is drawn at 64 tall. Both marks now fill its WIDTH exactly (kFolder is
// 44x39, kBookRow 44x44), so the only slack they are centred in is vertical.
inline constexpr int kBookThumbW = 44;     // `width: 44px; height: 64px`
inline constexpr int kBookThumbH = 64;
inline constexpr int kBookThumbGap = 16;   // the row's `gap: 16px`
inline constexpr int kBookLineGap = 3;     // the text column's `gap: 3px`
// kBookFocusBorder (2) and kBookCoverBorder (1) were the placeholder cover's two
// border widths -- the board's `border: 2px solid #ffffff` on a focused row against
// `border: 1px solid #000000` elsewhere. The placeholder is gone and a mark has no
// border, so they are gone with it rather than left as constants nothing reads.

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

  // How far the FIRST line is pushed in, in 1/64 px -- CSS `text-indent`, which
  // the reader's continuing paragraphs declare (design/Reader.dc.html). Carried
  // here rather than passed separately to the draw because the wrap MEASURED with
  // it: a caller that drew the first line flush would have a line laid out for a
  // narrower column than it was drawn in, which overflows by exactly the indent.
  int firstIndentF26 = 0;

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
//
// `firstIndentF26` shortens the FIRST line only, which is what `text-indent` does:
// a book's continuing paragraph starts an em and a half in and its remaining lines
// run full width. Zero for every chrome caller, so nothing on any existing board
// moves.
Prose wrapProseLead(const GlyphSource& font, std::string_view text, int maxW, int leadF26,
                    Tracking tracking = {}, WordBreak breaking = WordBreak::Normal,
                    int firstIndentF26 = 0);

// The styled wrap. Identical to the single-face one when `face.anyEmphasis()` is
// false, which is what keeps a chapter with no `<em>` in it costing exactly what it
// cost before.
Prose wrapProseStyled(const StyledFace& face, std::string_view text, int maxW, int leadF26,
                      Tracking tracking = {}, WordBreak breaking = WordBreak::Normal,
                      int firstIndentF26 = 0);

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
enum class ProseAlign {
  Centre,
  Left,
  // STRETCHED to the column, every line but the last -- design/Reader.dc.html's
  // `text-align: justify`, which design/Typography.dc.html's preview box also
  // declares.
  //
  // It applies kMinJustifyFillPercent exactly as the reader's own page does, so a
  // line too empty to justify is left ragged here too. Justifying a line the page
  // would leave alone is a subtler wrong than not justifying: the preview would
  // look tidier than the book it is previewing.
  //
  // THE READER'S PAGE DOES NOT COME THROUGH HERE. layout.cpp lays the page out
  // line by line and stretches each one itself; this is the option a CHROME
  // paragraph needs, and today the Typography preview is its only caller. Both go
  // through the same stretchFor and the same drawTextJustified, which is what
  // keeps the preview a preview.
  Justify,
};

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

int detailRowHeight(bool rule, int contentH = kDetailRowContentH);

// --- A list's section header -------------------------------------------------
//
// `--t-meta` tracked caps at 0.2em/500 in an 18/6 padding box, optionally under a 2px
// rule. THE BOX is byte-identical on two boards -- Settings.dc.html's `DEVICE` and
// Contents.dc.html's `BOOK I - MISS BROOKE` both declare
// `padding: 18px 24px 6px 24px` -- which is what makes it a primitive rather than a
// copy, this project's own rule about the second copy.
//
// THE RULE IS NOT SHARED, and this comment claimed it was: it said the two boards
// declare "the same `border-top: 2px`", and Contents.dc.html declares NONE, on either
// of its headers. A comment about a neighbouring file is not evidence about it -- the
// same shape as the `book.cpp` comment that described `Epub::open` letting a broken
// chapter through -- and this one licensed `renderContents` to draw a rule the board
// does not, straight into the row rule above it: a 3px line where the board draws none
// (#81). WHETHER a screen's headers rule at all is that screen's design and stays at
// the call site: Settings passes `i != 0`, Contents passes `false`.
//
// WHERE IT IS DRAWN, THE RULE IS POSITIONAL: the FIRST header in a window has none,
// because the header band's own 2px border is already the separation and a second
// doubles it into a 4px slab. `drawSectionHeader` returns the height it ACTUALLY drew
// for that reason -- a first header is shorter by its missing rule, and a caller that
// advanced by the nominal height would put every row below it 2px low. Settings shipped
// exactly that bug once, in its section-final rule. That return is also what makes
// Contents' `false` cost nothing below it.
inline constexpr int kSectionRuleH = 2;
inline constexpr int kSectionPadTop = 18;
inline constexpr int kSectionPadBottom = 6;
inline constexpr int kSectionEm = 200;

// A SECTION HEADER'S HEIGHT, AND IT DEPENDS ON THE RULE -- which is why
// `rule` is required rather than defaulted. A ruleless header is
// kSectionRuleH shorter, drawSectionHeader returns exactly that, and a caller
// RESERVING the nominal height for a header it then draws without one puts
// everything below it that many pixels out.
//
// That is not hypothetical: renderWifiSettings' empty variant summed this to
// decide where its SETUP section starts, drew both headers with `rule=false`,
// and so floated the block 2px high -- two 2px full-width slivers at the
// focused row's top and bottom, and the largest single band in that screen's
// mismatch. Settings shipped the mirror of it once, advancing `y` by the
// nominal height and putting every row below a header a pixel low.
//
// No default, because the one caller that had to answer this got it wrong by
// not being asked.
int sectionHeaderHeight(const FontSet& fonts, bool rule);
int drawSectionHeader(Framebuffer& fb, const FontSet& fonts, int y, int w,
                      std::string_view label, bool rule, Plane plane = Plane::Bw);
//
// `contentH` IS A PARAMETER BECAUSE A THIRD BOARD DECLARES A THIRD HEIGHT.
// BookDetails and Contents both say 64 (kDetailRowContentH, the default);
// WallabagAccount.dc.html says 72. That is the board's own number and it is
// followed rather than argued with -- a row height is exactly the kind of thing
// CLAUDE.md's first invariant says to derive from the board and never to pin.
// The alternative was a fourth hand-written copy of "label left, value right,
// optionally inverted, optionally ruled", which is what this function already
// exists to have deleted.
int drawDetailRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
                  std::string_view value, bool focused, bool rule, Plane plane = Plane::Bw,
                  int contentH = kDetailRowContentH);

// --- An overlay's panel ------------------------------------------------------
//
// The floating panel the overlay boards put over a veiled parent
// (LibraryActions, DeleteConfirm, ReaderMenu, ArticleActions, WifiConnect,
// WifiError, BookError -- seven boards, one box; GoToPage was an eighth until
// its board was cut):
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
// --- The picker's signal meter ---------------------------------------------
//
// design/WifiPicker.dc.html draws it as `<svg width="30" height="23" viewBox="0
// 0 17 13">` holding THREE rects -- bottom-aligned bars of increasing height,
// filled up to the level and outlined above it.
//
// DRAWN RATHER THAN GENERATED, which is a deliberate exception to "assets are
// generated from the design, not transcribed" and is worth the sentence:
//
//   - IT IS A FAMILY OF THREE STATES, not a mark. As icons it would be three
//     pre-rendered bitmaps of the same drawing differing only in which
//     rectangles are filled, which is what kBook/kBookLarge exists to avoid one
//     level up.
//   - iconc.py COULD NOT TELL THEM APART. The three states are byte-identical
//     but for a `fill` attribute, so each match would have to key on that -- and
//     the generator refuses an ambiguous match, correctly, which is how this was
//     found rather than shipped.
//   - THEY ARE AXIS-ALIGNED RECTANGLES, so they are coverage 0 or 3 and
//     identical in every plane and every pass -- the same class of furniture as
//     drawProgressBar and drawScrollRail, both of which this firmware already
//     draws in code from geometry a board states.
//
// The thin-stroke warning this project records is about DIAGONALS (kChevron)
// and does not reach a vertical bar. `level` is 1..3 and is clamped.
//
// NO `plane` PARAMETER, which is the same statement outlineRect and
// Framebuffer::fillRect make: this is furniture, so every pixel of it is
// coverage 0 or 3 and identical in Bw, BwDithered and all three grayscale
// planes. A plane argument would imply it could differ.
inline constexpr int kSignalW = 30;
inline constexpr int kSignalH = 23;
void drawSignalBars(Framebuffer& fb, int x, int y, int level, Ink ink = Ink::Black);

int panelRowHeight(bool rule);
// `value` is the row's right slot where the board gives one, and empty where it draws
// a chevron -- a row states a quantity or discloses a screen, never both. Defaulted
// empty so the actions panel, which only ever discloses, is unchanged; with the reader
// menu's `Bookmarks` row cut (#55) that default is what EVERY caller takes, and the
// value path keeps its own test at the primitive rather than becoming a claim nobody
// runs, the way `labelTrackingEm1000` did when `Close book` went.
int drawPanelRow(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                 std::string_view label, bool focused, bool discloses, bool rule,
                 Plane plane = Plane::Bw, std::string_view value = {},
                 int labelTrackingEm1000 = 0);

// --- Home's spine ------------------------------------------------------------
//
// design/Main.dc.html sets the book's name along the panel's LONG axis, in a
// black band down the left edge. That is what takes the title out of the
// competition for height: 692px of run per line on the X4 against the 608px two
// lines of the old 304px title column gave.
//
// NO GLYPH IS EVER ROTATED, which is the whole reason this is affordable. The
// text is drawn HORIZONTALLY into a scratch framebuffer whose logical axes are
// the panel's transposed -- its width is the panel's HEIGHT, its height is the
// band's thickness -- and the scratch is then transferred a row at a time. So
// wrapping, measuring, eliding and the blit are all the existing primitives,
// working in their ordinary orientation, and `text.cpp` is untouched.
//
// THE THICKNESS MUST BE A MULTIPLE OF 8 AND SO MUST THE PANEL'S HEIGHT. Under
// Rotation::Ccw the framebuffer maps physX = logY, so the band is `kSpineW`
// CONSECUTIVE PHYSICAL ROWS and the scratch's rows are the same length as the
// panel's -- which is what makes the transfer a byte operation rather than a
// per-pixel transpose. Both panels satisfy both: 112 % 8 == 0, 800 % 8 == 0 and
// 792 % 8 == 0.
constexpr int kSpineW = 112;
// The board's `padding: 22px 0` on the band, and its `line-height: 1.12` on a
// 42px face. Spelled in pixels like every other number here, because a Font
// reports ascent and descent but not its own ppem.
constexpr int kSpinePadEnds = 22;
constexpr int kSpineLineH = 47;  // round(1.12 * 42)
// Two lines, for the reason design/Main.dc.html gives: a thick book sets its
// spine in two, and `A Portrait of the Artist as a Young Man` needs both at both
// geometries. Past two the band would have to thicken, which is the one number
// here that may not move.
constexpr int kSpineMaxLines = 2;

// Fills `scratch` with the band and its text, in the scratch's own orientation.
//
// `scratch` must be `Framebuffer(panelHeight, kSpineW, Rotation::None)`: its
// logical x is the panel's y (the band's LENGTH) and its logical y is the
// panel's x (its THICKNESS). Exposed rather than private because it is the half
// of `drawSpine` that has nothing to do with rotation, which lets a test build
// the same pixels by an obviously-correct route and compare.
// `bandLen` is how far down the panel the band runs -- the boards stop it at the
// hint bar, which spans the FULL width because it describes the device's buttons
// rather than a column. The scratch stays the panel's whole height either way so
// the transfer can move whole rows; what is past `bandLen` is left PAPER.
void composeSpineScratch(Framebuffer& scratch, const FontSet& fonts, int bandLen,
                         std::string_view text, Plane plane);

// Draws the spine into `fb` occupying logical x in [0, w), full height.
//
// The run reads BOTTOM-TO-TOP, which is how a book on a shelf reads and what the
// board draws. That mirror is the one thing stopping the transfer being a plain
// memcpy: it reverses the destination along the row, so each row is copied
// through a bit-reversal instead. Still O(bytes) and not O(pixels) -- 112 rows
// of 99 bytes through a 256-entry table, against 77,504 setPixel calls.
void drawSpine(Framebuffer& fb, const FontSet& fonts, int w, int bandLen,
               std::string_view text, Plane plane = Plane::Bw);

}  // namespace reader
