#include "reader/components.h"

// reader/dither.h is deliberately NOT included any more: the book row's
// placeholder cover was this file's only ditherRect caller. Dropping it is safe
// under both toolchains rather than only under this one -- dither.h's sole include
// is reader/text.h, which this file includes directly two lines down -- which is
// the check worth making here, since a transitively-satisfied include is a bug
// only the OTHER compiler can see (test_scalablefont.cpp's missing <cstring>
// compiled on macOS for months and failed on the first Linux build).
#include "reader/framebuffer.h"
#include "reader/text.h"

namespace reader {

namespace {
int maxOf(int a, int b) { return a > b ? a : b; }

// The tallest item in the header band's flex row: the label's line box, the
// value's, and -- because the battery sits inside the value's item rather than
// beside it -- the glyph's own height.
int bandContentH(const FontSet& fonts, const Icon* mark) {
  const int text =
      maxOf(fonts[Role::Label500].lineHeight(), fonts[Role::Value700].lineHeight());
  // A band with no mark is two flex items, not three -- the Library's and Book
  // details' bands carry a value and nothing beside it. Folding the absent
  // glyph in as 0 rather than branching keeps one expression for both.
  return maxOf(text, mark ? mark->h : 0);
}

// One hint slot's height. The slot is a single flex row of the leading mark, the
// label and -- when the button has a long-press action -- the hollow hold ring,
// so it is as tall as the tallest of them. Nothing here adds a second line box:
// design 662557d made the hold a mark on this row rather than a line under it,
// so a hold cannot change a slot's height and therefore cannot change the bar's.
// The height is still *derived* from the content rather than pinned, which is
// what keeps it correct for a screen that sets its hints in a larger role or
// pairs them with a taller mark.
int hintSlotH(const GlyphSource& mf, const Hint& hint) {
  int h = mf.lineHeight();
  if (hint.icon) h = maxOf(h, hint.icon->h);
  // The ring is 25px against Meta's 27px line box today, so this max is a no-op
  // on today's ramp -- and it is here for the same reason the leading mark's is:
  // the rule must not depend on that happening to be true.
  if (hint.hasHold) h = maxOf(h, icons::kHold.h);
  return h;
}

// A slot with nothing in it is the board's 36px placeholder; everything else is
// the flex row's own content. Kept beside hintSlotH because the two answer the
// same question on the two axes and a slot that is empty on one is empty on the
// other.
bool hintSlotEmpty(const Hint& hint) {
  return hint.icon == nullptr && hint.label.empty() && !hint.hasHold;
}

int hintSlotW(const GlyphSource& mf, const Hint& hint, Tracking tracking) {
  if (hintSlotEmpty(hint)) return kHintEmptySlotW;
  const int iconW = hint.icon ? hint.icon->w + kHintIconGap : 0;
  const int textW = mf.measure(hint.label, tracking);
  // The hold ring is part of the slot's flex row, so it is part of the slot's
  // measured width -- gap included. Leaving it out would not make the ring
  // vanish, it would make every space-between gap this bar computes too wide
  // by 32px and let the ring lap the next slot's mark.
  const int holdW = hint.hasHold ? kHintIconGap + icons::kHold.w : 0;
  return iconW + textW + holdW;
}

int hintContentH(const FontSet& fonts, const Hint hints[4]) {
  const Font& mf = fonts[Role::Meta400];
  int h = 0;
  for (int i = 0; i < 4; ++i) h = maxOf(h, hintSlotH(mf, hints[i]));
  return h;
}
}  // namespace

int headerBandHeight(const FontSet& fonts, const Icon* mark) {
  return kBandPadTop + bandContentH(fonts, mark) + kBandPadBottom + kBandRuleH;
}

int hintBarHeight(const FontSet& fonts, const Hint hints[4]) {
  return kHintRuleH + kHintPadTop + hintContentH(fonts, hints) + kHintPadBottom;
}

int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value, const Icon* mark, Plane plane) {
  const Font& lf = fonts[Role::Label500];
  const Font& vf = fonts[Role::Value700];
  // The band's content box is the strip the board's padding leaves between the
  // top edge and the rule -- not "everything above the rule", which is what the
  // pinned height made it and which is why the rule sat 6px low. The label and
  // the value are two different roles (23px against 25px), so they get two
  // baselines: the board centres each flex item on its own line box, and one
  // shared baseline taken from the label would sit the value 1px low. Both come
  // out of the same box, so they still agree optically.
  const int bandH = headerBandHeight(fonts, mark);
  const int contentH = bandContentH(fonts, mark);
  const int labelBase = baselineIn(lf, kBandPadTop, contentH);
  const int valueBase = baselineIn(vf, kBandPadTop, contentH);
  // EITHER RUN CAN BE THE DATA ONE, AND EITHER CAN TRUNCATE. The Library's label
  // is a subfolder's own name and Contents' value is the book title, so a rule
  // that gave one of them its width first was wrong on the other -- `labelShare`
  // in the header carries the whole of it, and why the choice is made by
  // measuring rather than by asking the caller.
  //
  // The mark's own reservation is the board's `gap: 7px` plus the glyph, and it
  // comes out of the row before either run is measured: with no mark the group IS
  // the value, so the value's own right edge lands on the margin -- which is what
  // the Library's `12 BOOKS` and Book details' `EPUB` measure on their boards.
  // Reserving a gap and a glyph width anyway would pull both 45px left of the
  // design.
  const int markW = mark ? kBandGap + mark->w : 0;
  const int vNatural = vf.measure(value);
  // The two runs' shared row, less the board's `gap: 7px` between them. Reserved
  // whenever there IS a group, because `justify-content: space-between` with no gap
  // lets the ellipsis touch the value the moment the label fills the line -- the
  // Library's board declares that gap for exactly this reason, and Contents.dc.html
  // now declares it too because it is what keeps an ellipsis off `CONTENTS`.
  const bool hasGroup = (vNatural + markW) > 0;
  const int avail = fb.width() - 2 * kMargin - markW - (hasGroup ? kBandGap : 0);
  const int labelW = labelShare(lf.measure(label, trackingEm(lf, kBandLabelEm)), vNatural,
                                avail);
  // THE VALUE IS ELIDED ONLY WHEN IT HAS TO BE, so the common case allocates
  // nothing and takes the same path it always took. drawTextElided cannot serve
  // here: the group is right-aligned on the margin, so the CUT run's own width is
  // what positions it, and that has to be known before it is drawn. The flag is
  // what selects the string rather than `cut.empty()`: elideToWidth answers EMPTY
  // for a budget that cannot hold even the ellipsis (its documented contract), and
  // reading that as "nothing was cut" would draw the whole run at full length --
  // the overhang the elide exists to prevent.
  const bool cutting = vNatural > avail - labelW;
  const std::string cut = cutting ? elideToWidth(vf, value, avail - labelW) : std::string();
  const std::string_view drawn = cutting ? std::string_view(cut) : value;
  // RIGHT-ALIGNED ON THE MARGIN even when cut, which is the reader header's own
  // rule for the same run one band up (`right - measure(chapter)`), and is what
  // keeps a band's right slot flush with the margin whatever it holds. The board
  // instead keeps the box at the budget and left-aligns the truncated text inside
  // it, so Chrome leaves the ellipsis a few pixels short of the margin -- a
  // disagreement that exists only in the truncating state, which no board's
  // committed specimen shows, and the alternative is a value that visibly drifts
  // off the margin exactly when it is longest.
  const int vw = vf.measure(drawn);
  const int groupW = vw + markW;
  const int groupX = fb.width() - kMargin - groupW;
  drawTextElided(fb, lf, kMargin, labelBase, label, labelW, Ink::Black,
                 trackingEm(lf, kBandLabelEm), plane);
  drawText(fb, vf, groupX, valueBase, drawn, Ink::Black, {}, plane);
  // Centred in the band's content box, which is what the design's
  // `align-items: center` does to that flex row and its nested value+battery
  // row alike: every child of a flex line, whatever its height, centres on the
  // line's one cross-axis centre. The battery is 21px where the marks elsewhere
  // are 25, so a constant offset cannot serve both -- and deriving the centre
  // from the value's baseline instead of from the box, which is what this used
  // to do, spent three integer divisions to reach the same number and landed
  // the glyph 1.5px below the percentage it belongs to.
  if (mark)
    drawIcon(fb, *mark, groupX + vw + kBandGap, iconTopIn(kBandPadTop, contentH, mark->h),
             Ink::Black, plane);
  fb.fillRect(0, bandH - kBandRuleH, fb.width(), kBandRuleH, false);
  return bandH;
}

int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused, const Icon* trailing, Plane plane) {
  // Role::Label500, not a Body role: the boards set a menu row's label to
  // `--t-label` (11pt/23px, weight 500) with `letter-spacing: 0.18em`. Body is
  // 14pt/29px and is what a *list item's title* uses -- a different thing on a
  // different screen. Drawing rows in Body made LIBRARY and SETTINGS six pixels
  // too tall and, with tracking 0, visibly too tight.
  const Font& lf = fonts[Role::Label500];
  const Font& vf = fonts[Role::Value700];
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(0, y, fb.width(), kRowH, false);
  else
    fb.fillRect(0, y, fb.width(), 1, false);  // hairline above
  // Content is centred in the content box, below the row's own rule.
  const int labelBase = baselineIn(lf, y + kRowRuleH, kRowContentH);
  const int valueBase = baselineIn(vf, y + kRowRuleH, kRowContentH);
  drawText(fb, lf, kMargin, labelBase, label, ink, trackingEm(lf, kRowLabelEm), plane);
  // A row carries a value, a trailing mark, or neither -- the design has one of
  // each (LIBRARY's count, SETTINGS' chevron). Both are right-aligned on the
  // margin; the icon takes the row's ink, so it inverts with a focused row.
  int rightEdge = fb.width() - kMargin;
  if (trailing) {
    drawIcon(fb, *trailing, rightEdge - trailing->w,
             iconTopIn(y + kRowRuleH, kRowContentH, trailing->h), ink, plane);
    rightEdge -= trailing->w + kRowGap;
  }
  if (!value.empty())
    drawText(fb, vf, rightEdge - vf.measure(value), valueBase, value, ink, {}, plane);
  return kRowH;
}

void buildHints(const Icon* const marks[4], const std::array<std::string, 4>& labels,
                const std::array<bool, 4>& holds, Hint out[4]) {
  // The mark follows the label -- see the header for why an empty slot must not
  // carry one. The hold passes through whatever the label says: the ring rides
  // with its binding, and gestureFor drops a hold on a slot nothing advertises.
  for (int i = 0; i < 4; ++i) {
    const size_t s = static_cast<size_t>(i);
    out[i] = {labels[s].empty() ? nullptr : marks[i], labels[s], holds[s]};
  }
}

void outlineRect(Framebuffer& fb, int x, int y, int w, int h, int t, bool white) {
  if (w <= 0 || h <= 0 || t <= 0) return;
  fb.fillRect(x, y, w, t, white);
  fb.fillRect(x, y + h - t, w, t, white);
  fb.fillRect(x, y, t, h, white);
  fb.fillRect(x + w - t, y, t, h, white);
}

void drawProgressBar(Framebuffer& fb, int x, int y, int w, int h, int percent, int t) {
  outlineRect(fb, x, y, w, h, t);
  const int innerW = w - 2 * t;
  const int innerH = h - 2 * t;
  if (innerW <= 0 || innerH <= 0) return;
  const int pct = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  const int fill = (innerW * pct + 50) / 100;
  if (fill > 0) fb.fillRect(x + t, y + t, fill, innerH, false);
}

void drawCentredText(Framebuffer& fb, const Font& font, int boxX, int boxW, int baseline,
                     std::string_view text, Ink ink, Tracking tracking, Plane plane) {
  const int w = font.measure(text, tracking);
  drawText(fb, font, boxX + centreIn(0, boxW, w), baseline, text, ink, tracking, plane);
}

int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], Plane plane) {
  int slots[4] = {};
  return drawHintBar(fb, fonts, hints, slots, plane);
}

void drawStatusBar(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   Plane plane) {
  const Font& mf = fonts[Role::Meta400];
  // The marks with no labels: a slot is one line of Meta whatever it says, so this
  // measures the same bar the screen would have drawn. Same reasoning as the
  // theme's own measuring set, and the marks are KEPT because they are half of what
  // sets the height.
  Hint hints[4];
  for (int i = 0; i < 4; ++i) hints[i] = {kHintSlotMarks[i], "", false};
  // THE HINT BAR'S OWN HEIGHT, asked of the same function rather than recomputed --
  // the board gives the two states the identical box, and this is what makes that
  // true in the firmware rather than merely intended. Deriving it twice is how the
  // header band ended up 6px out.
  const int barH = hintBarHeight(fonts, hints);
  const int top = fb.height() - barH;
  // CLEARED FIRST. This is drawn over a frame that already holds the hint bar it
  // replaces, so without the clear the two would be superimposed -- which reads as
  // corruption rather than as a state.
  fb.fillRect(0, top, fb.width(), barH, true);
  fb.fillRect(0, top, fb.width(), kHintRuleH, false);

  // Centred in the CONTENT box -- the strip the board's padding leaves between the
  // rule and the bottom edge -- exactly as every hint slot is. The padding is
  // asymmetric (20 above, 16 below), so centring in the bar instead would put this
  // line 2px high, which is the defect drawHintBar records against itself.
  const int contentTop = top + kHintRuleH + kHintPadTop;
  const int contentH = hintContentH(fonts, hints);
  const Tracking tracking = trackingEm(mf, kStatusEm);
  const int baseline = contentTop + (contentH - mf.lineHeight()) / 2 + mf.ascent();
  drawCentredText(fb, mf, 0, fb.width(), baseline, label, Ink::Black, tracking, plane);
}

int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4],
                Plane plane) {
  const Font& mf = fonts[Role::Meta400];
  const int barH = hintBarHeight(fonts, hints);
  const int top = fb.height() - barH;
  fb.fillRect(0, top, fb.width(), kHintRuleH, false);

  // Measure every slot, then distribute the leftover space evenly. This is what
  // makes the bar correct on both 480 and 528 wide canvases: nothing is pinned.
  const Tracking hintTracking = trackingEm(mf, kHintEm);
  int widths[4] = {};
  int total = 0;
  for (int i = 0; i < 4; ++i) {
    widths[i] = hintSlotW(mf, hints[i], hintTracking);
    total += widths[i];
  }
  const int usable = fb.width() - 2 * kMargin;
  // `justify-content: space-between`: the leftover is split into three equal
  // gaps, and the browser's are fractional. Each slot's offset is therefore
  // `leftover * i / 3` computed from i -- one rounding per slot -- rather than
  // three copies of a truncated integer gap, which would lose up to a pixel per
  // gap and land the last slot up to 3px left of the board's.
  const int leftover = (usable > total && total > 0) ? usable - total : 0;

  // Every slot is centred in the bar's *content box* -- the strip the board's
  // padding leaves between the rule and the bottom edge -- which is what the
  // design's `align-items: center` does. The content box, not the bar, is the box
  // that matters, and the difference is measurable: the padding is 20 above and
  // 16 below, so the content's centre line sits 2px below the bar's. Centring in
  // the bar put every hint label on every screen 3px high.
  //
  // The baseline is still per slot rather than shared. Every slot is one line now,
  // so on today's screens they agree -- but a slot is only as tall as its own
  // tallest mark, and one slot carrying a taller mark than its neighbours makes
  // the boxes differ again. A baseline keyed on slot 0 would then pull the rest
  // off the centre line with it.
  const int contentTop = top + kHintRuleH + kHintPadTop;
  const int contentH = hintContentH(fonts, hints);
  int prefix = 0;  // sum of the slot widths before this one
  for (int i = 0; i < 4; ++i) {
    const int x = kMargin + prefix + (leftover * i + 1) / 3;
    // The slot's own box, centred in the content box: one flex row of the mark,
    // the label and the hold ring, as tall as the tallest of them. On a bar where
    // every slot is the same height this is the content box itself; on one where
    // a taller mark makes a slot taller it is what keeps the short slots on the
    // centre line the tall one straddles.
    const int slotH = hintSlotH(mf, hints[i]);
    // centreIn, not `contentTop + (contentH - slotH) / 2`: the two are the same
    // number while every slot is the same height as the content box, which is
    // every screen today, and they part company by a pixel the moment one slot
    // carries a taller mark than its neighbours -- an open-coded halving
    // truncates where the primitive rounds halves up, and it would put the odd
    // slot on a different centre line from the rest of the bar.
    const int slotTop = centreIn(contentTop, contentH, slotH);
    const int baseline = baselineIn(mf, slotTop, slotH);
    slotXOut[i] = x;
    int textX = x;
    if (hints[i].icon) {
      // Centred in the same box the label is centred in -- the mark and the label
      // are two children of one `align-items: center` flex row on the board. The
      // bar's marks are a uniform box today, but the placement must not depend on
      // that: Reader's bar pairs a 21px battery with them.
      drawIcon(fb, *hints[i].icon, x, iconTopIn(slotTop, slotH, hints[i].icon->h), Ink::Black,
               plane);
      textX += hints[i].icon->w + kHintIconGap;
    }
    drawText(fb, mf, textX, baseline, hints[i].label, Ink::Black, hintTracking, plane);
    if (hints[i].hasHold) {
      // The hold ring is the row's last child: after the label, one flex gap
      // along, aligned by iconTopIn like every other mark on the screen. Drawn
      // after the label because the board orders it after the label -- it reads
      // as a modifier of that word, not as a second button.
      const Icon& ring = icons::kHold;
      const int ringX = textX + mf.measure(hints[i].label, hintTracking) + kHintIconGap;
      drawIcon(fb, ring, ringX, iconTopIn(slotTop, slotH, ring.h), Ink::Black, plane);
    }
    prefix += widths[i];
  }
  return barH;
}

int drawActionButton(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                     std::string_view label, bool filled, Plane plane) {
  // The variant picks the role, because the boards do: a filled slab's label is
  // `--t-value` at 700 and an outlined one's is `--t-label` at 500. Same
  // tracking, same 68px box, two different faces.
  const Font& lf = fonts[filled ? Role::Value700 : Role::Label500];
  if (filled) {
    fb.fillRect(x, y, w, kActionH, false);
  } else {
    // `border: 2px solid; box-sizing: border-box` -- the border is inside the
    // box, so an outlined slab and a filled one occupy exactly the same rect and
    // a focus move between them cannot shift either.
    outlineRect(fb, x, y, w, kActionH, kActionBorder);
  }
  // Reversed out of the slab, which is what Ink::White is for: no scratch
  // buffer, no second pass. The label is centred on both axes because the
  // board's block is `align-items: center; justify-content: center` -- the same
  // two helpers, one per axis, that every other centred box on every screen
  // resolves against.
  const Tracking tracking = trackingEm(lf, kActionEm);
  const int labelW = lf.measure(label, tracking);
  drawText(fb, lf, centreIn(x, w, labelW), baselineIn(lf, y, kActionH), label,
           filled ? Ink::White : Ink::Black, tracking, plane);
  return kActionH;
}

namespace {
// design/Sleep.dc.html and design/BatteryEmpty.dc.html, identically.
constexpr int kBadgeBottom = 34;
constexpr int kBadgePadX = 18;
constexpr int kBadgePadY = 8;
constexpr int kBadgeBorder = 1;
constexpr int kBadgeEm = 200;  // letter-spacing: 0.2em
}  // namespace

int drawBadge(Framebuffer& fb, const GlyphSource& font, std::string_view label, Plane plane) {
  const Tracking track = trackingEm(font, kBadgeEm);
  const int labelW = font.measure(label, track);
  const int w = labelW + 2 * (kBadgeBorder + kBadgePadX);
  const int h = font.lineHeight() + 2 * (kBadgeBorder + kBadgePadY);
  const int x = centreIn(0, fb.width(), w);
  const int y = fb.height() - kBadgeBottom - h;
  fb.fillRect(x, y, w, h, true);
  outlineRect(fb, x, y, w, h, kBadgeBorder);
  drawText(fb, font, x + kBadgeBorder + kBadgePadX,
           baselineIn(font, y + kBadgeBorder + kBadgePadY, font.lineHeight()), label, Ink::Black,
           track, plane);
  return y;
}

Prose wrapProse(const GlyphSource& font, std::string_view text, int maxW, int leadEm1000,
                Tracking tracking, WordBreak breaking) {
  // The board states the leading as a multiple of the font size, so it resolves
  // against the face exactly as letter-spacing does -- and lands on the same 1/64
  // px unit, for the same reason: 1.55 x 29 is 44.95, and three line boxes of a
  // pre-rounded 45 put the last line a pixel low.
  return wrapProseLead(font, text, maxW, Tracking::em(font.ppem(), leadEm1000).f26(), tracking,
                       breaking);
}

namespace {
// The end of the longest prefix of text[from, to) that measures within maxW, on
// a codepoint boundary and never shorter than one codepoint. "Never shorter than
// one" is what stops a column too narrow for a single glyph from making this an
// infinite loop -- the line then overhangs by construction, which is the honest
// outcome and the same one wrapProse's first-word rule already has.
size_t fitPrefixEnd(const StyledFace& face, std::string_view text, size_t from, size_t to,
                    int maxW, Tracking tracking) {
  size_t i = from;
  size_t fits = from;
  while (i < to) {
    const size_t before = i;
    utf8Next(text, i);
    if (i > to) i = to;  // a sequence straddling the word's end: do not read past
    if (face.measure(text, from, i, tracking) > maxW) {
      // The first codepoint alone is already too wide, so it is the answer.
      return fits == from ? i : fits;
    }
    fits = i;
    if (i == before) break;  // defensive: utf8Next always advances
  }
  return fits;
}
}  // namespace

Prose wrapProseLead(const GlyphSource& font, std::string_view text, int maxW, int leadF26,
                    Tracking tracking, WordBreak breaking, int firstIndentF26) {
  // ONE IMPLEMENTATION, NOT TWO. Every chrome caller arrives here and gets a face
  // with no italic and no spans, which `StyledFace::measure` short-circuits to a
  // single `roman->measure` -- the same call it made before. Two wrap loops would
  // drift, and the drift would be silent: the reader's would get the fixes and the
  // twenty chrome callers' would not.
  const StyledFace face{&font, nullptr, nullptr};
  return wrapProseStyled(face, text, maxW, leadF26, tracking, breaking, firstIndentF26);
}

int StyledFace::measure(std::string_view text, size_t from, size_t to, Tracking tracking) const {
  if (to <= from) return 0;
  // THE COMMON CASE IS ONE CALL. No italic face or no spans means no boundary can
  // fall inside the range, so this is exactly what the single-face wrap did.
  if (!anyEmphasis()) return roman->measure(text.substr(from, to - from), tracking);
  int w = 0;
  size_t pos = from;  // not `at`: that is this struct's own accessor
  while (pos < to) {
    const size_t next = nextStyleBoundary(*emphasis, pos, to);
    const size_t stop = (next > pos && next <= to) ? next : to;
    w += at(pos).measure(text.substr(pos, stop - pos), tracking);
    pos = stop;
  }
  return w;
}

Prose wrapProseStyled(const StyledFace& face, std::string_view text, int maxW, int leadF26,
                      Tracking tracking, WordBreak breaking, int firstIndentF26) {
  Prose out;
  out.tracking = tracking;
  out.leadF26 = leadF26;
  out.firstIndentF26 = firstIndentF26;

  // The width available to the line CURRENTLY being built. `out.lines.size()` is
  // that line's index, so the indent applies while nothing has been emitted yet
  // -- which is CSS's rule, and it has to be asked per candidate rather than
  // computed once, because a line is emitted in the middle of the loop below.
  const int indentPx = f26ToPx(firstIndentF26);
  const auto limit = [&]() { return out.lines.empty() ? maxW - indentPx : maxW; };

  size_t lineStart = 0;   // first byte of the line being built
  size_t lineEnd = 0;     // one past its last non-space byte
  size_t i = 0;
  // Starts a fresh line with the word [wordStart, wordEnd). Under
  // WordBreak::Anywhere a word wider than the column is split across as many
  // full lines as it takes, and what is left -- which fits by construction --
  // becomes the line being built. Under Normal it goes on the line whole and
  // overhangs, which is the behaviour every paragraph on every board relies on.
  const auto startLine = [&](size_t wordStart, size_t wordEnd) {
    if (breaking == WordBreak::Anywhere) {
      while (face.measure(text, wordStart, wordEnd, tracking) > limit()) {
        const size_t cut = fitPrefixEnd(face, text, wordStart, wordEnd, limit(), tracking);
        if (cut <= wordStart || cut >= wordEnd) break;
        out.lines.push_back(text.substr(wordStart, cut - wordStart));
        wordStart = cut;
      }
    }
    lineStart = wordStart;
    lineEnd = wordEnd;
  };
  while (i < text.size()) {
    // One word, plus the run of spaces before it.
    while (i < text.size() && text[i] == ' ') ++i;
    const size_t wordStart = i;
    // A SEGMENT ENDS AT A SPACE **OR JUST AFTER A HYPHEN**, which is a real line
    // breaking rule (UAX #14 allows a break after a hyphen) and what Chrome does --
    // so honouring it moves the firmware toward the boards rather than away.
    //
    // It is here because a real book needed it. `Le Fléau` writes chanted phrases as
    // hyphen chains, and the worst of them --
    // "Jeff-Marty-Helen-Harriett-Bill-George-Robert-Stanley-Richard-Danny-Frank" --
    // was ONE unbreakable word, so it ran 683px past a 492px column and off the
    // panel. Eight lines in 96,823 did that; six were hyphen chains.
    //
    // The hyphen stays at the END of the line, which is what makes the break read as
    // typography rather than as damage. Not applied to a leading hyphen ("-5" is not
    // two segments), nor when a space follows anyway.
    while (i < text.size() && text[i] != ' ') {
      ++i;
      if (text[i - 1] == '-' && i > wordStart + 1 && i < text.size() && text[i] != ' ') break;
    }
    if (wordStart == i) break;  // trailing spaces only
    const bool lineEmpty = (lineEnd == lineStart);
    if (lineEmpty) {
      // The first word of a line goes on it whatever it measures: a word wider
      // than the column has nowhere better to be, and breaking inside it is a
      // decision only the board's `overflow-wrap` may make.
      startLine(wordStart, i);
      continue;
    }
    // The candidate is measured from the line's start, spaces included, because
    // that is the run that will be drawn -- measuring the word alone and adding
    // a space's advance would lose every kern across the join.
    if (face.measure(text, lineStart, i, tracking) <= limit()) {
      lineEnd = i;
      continue;
    }
    out.lines.push_back(text.substr(lineStart, lineEnd - lineStart));
    startLine(wordStart, i);
  }
  if (lineEnd > lineStart) out.lines.push_back(text.substr(lineStart, lineEnd - lineStart));
  return out;
}

void clampProse(const GlyphSource& font, Prose& prose, int maxLines, int maxW, std::string& tail) {
  if (maxLines < 1) {
    prose.lines.clear();
    return;
  }
  if (prose.lineCount() <= maxLines) return;
  // Every line is a view into ONE buffer, so what the clamp has to elide is the
  // single span from the last kept line's first byte to the wrap's very last --
  // interior spaces included, because those are the spaces that would have been
  // drawn had the run been one long line. That is what makes the ellipsis land
  // where CSS lands it rather than at the end of the last surviving word.
  const std::string_view keep = prose.lines[static_cast<size_t>(maxLines) - 1];
  const std::string_view last = prose.lines.back();
  const std::string_view rest(keep.data(),
                              static_cast<size_t>(last.data() + last.size() - keep.data()));
  tail = elideToWidth(font, rest, maxW, prose.tracking);
  prose.lines.resize(static_cast<size_t>(maxLines));
  prose.lines.back() = tail;
}

int drawProse(Framebuffer& fb, const GlyphSource& font, const Prose& prose, int boxX, int boxW,
              int topF26, Ink ink, Plane plane, ProseAlign align) {
  const int last = prose.lineCount() - 1;
  for (int i = 0; i < prose.lineCount(); ++i) {
    const std::string_view line = prose.lines[static_cast<size_t>(i)];
    // THE FIRST LINE'S INDENT, which the WRAP MEASURED WITH (Prose::firstIndentF26
    // carries it for exactly that reason). Zero for every caller on every board
    // today -- the reader's indented paragraphs go through layout.cpp, not here --
    // and honoured rather than ignored because a Prose that carries an indent is a
    // Prose that was wrapped against a narrower first measure. A justify computed
    // against the full box would overflow the column by exactly the indent.
    const int indent = i == 0 ? f26ToPx(prose.firstIndentF26) : 0;
    // A centred line is centred on its OWN measured width -- which is what
    // `text-align: center` does. Not on the widest line's, and not on the
    // column's centre with a half-width offset: that spends a second division.
    // A left-aligned one needs no measurement at all.
    const int x = align == ProseAlign::Centre
                      ? centreIn(boxX, boxW, font.measure(line, prose.tracking))
                      : boxX + indent;
    const int baseline = baselineInF26(font, topF26 + i * prose.leadF26, prose.leadF26);
    // JUSTIFIED, and THE LAST LINE IS DECIDED BY INDEX rather than by stretchFor
    // answering 0. Those are two different reasons for a ragged line: the last line
    // of a paragraph is short by however much the paragraph ended short, and
    // conflating the two would leave a genuinely FULL last line stretched to the
    // margin -- which layout.cpp names as the single most recognisable way
    // justified text can be wrong.
    //
    // `prose.tracking`, never a second tracking argument, and the measure is the
    // one the line was wrapped against: same rule, same reason.
    const int extraPerGapF26 = (align == ProseAlign::Justify && i != last)
                                   ? stretchFor(font, line, boxW - indent, prose.tracking)
                                   : 0;
    if (extraPerGapF26 > 0)
      drawTextJustified(fb, font, x, baseline, line, extraPerGapF26, ink, prose.tracking, plane);
    else
      drawText(fb, font, x, baseline, line, ink, prose.tracking, plane);
  }
  return prose.heightF26();
}


// --- design/Library.dc.html's row -------------------------------------------

int bookRowContentH(const FontSet& fonts) {
  // The board's row is a flex line of a 44x64 thumbnail and a two-line text
  // column, so its content box is the taller of the two -- 67 against 64 on
  // today's ramp, which means the TEXT is what sets the row's height and a
  // change to the body or meta face changes it. Content-independent on purpose:
  // measuring the strings would make a row with no author shorter than its
  // neighbours and put every row below it on a different grid.
  const int column = fonts[Role::Body500].lineHeight() + kBookLineGap +
                     fonts[Role::Meta400].lineHeight();
  return maxOf(kBookThumbH, column);
}

int bookRowHeight(const FontSet& fonts) {
  return kBookRowPadY + bookRowContentH(fonts) + kBookRowPadY + kBookRowRuleH;
}

void drawScrollRail(Framebuffer& fb, int listTop, int listBottom, int first, int visible,
                    int total, Plane plane) {
  (void)plane;  // every pixel here is coverage 0 or 3, so the plane cannot change it
  if (total <= 0 || visible >= total) return;  // a list that fits has nothing to say

  // The board's numbers: 6px wide, right edge 4px off the panel, inset 6px from
  // the list's own top and bottom so the track does not touch the header band's
  // rule or the hint bar's.
  const int x = fb.width() - kRailRightGap - kRailW;
  const int top = listTop + kRailEndInset;
  const int bottom = listBottom - kRailEndInset;
  const int h = bottom - top;
  if (h <= 2 * kRailBorder) return;  // no room to draw a track, let alone a thumb

  // The outlined track: four 1px edges, the treatment kBattery uses.
  outlineRect(fb, x, top, kRailW, h, kRailBorder);

  // The thumb, inside the border. ROUNDED ONCE: both proportions are taken
  // against the inner height in one division each, rather than accumulating a
  // per-row pitch that would drift down a 256-row list.
  const int innerX = x + kRailBorder;
  const int innerY = top + kRailBorder;
  const int innerW = kRailW - 2 * kRailBorder;
  const int innerH = h - 2 * kRailBorder;

  int thumbH = (innerH * visible + total / 2) / total;
  // A floor, because at the 256-row cap a proportional thumb can round to a
  // couple of pixels and a thumb too small to see is worse than none: it reads as
  // dirt on the track rather than as a position.
  if (thumbH < kRailThumbMinH) thumbH = kRailThumbMinH;
  if (thumbH > innerH) thumbH = innerH;

  int thumbY = innerY + (innerH * first + total / 2) / total;
  // Clamped so the floor above cannot push the thumb past the track's end -- at
  // the bottom of a long list the proportional top plus a floored height would
  // otherwise overhang the border it sits inside.
  if (thumbY + thumbH > innerY + innerH) thumbY = innerY + innerH - thumbH;
  if (thumbY < innerY) thumbY = innerY;

  fb.fillRect(innerX, thumbY, innerW, thumbH, false);
}

int drawBookRow(Framebuffer& fb, const FontSet& fonts, int y, const BookRowContent& row,
                bool focused, bool rule, Plane plane, int rightInset) {
  // The row's box, narrowed by the rail's gutter. Everything that spans the row
  // reads this rather than fb.width(): the focused fill, the rule and the right
  // edge the value and chevron sit on. A fill that kept the full width would run
  // under the rail and swallow the thumb, which is exactly what the board's first
  // draft did.
  const int rowW = fb.width() - rightInset;
  const int contentH = bookRowContentH(fonts);
  const int boxH = kBookRowPadY + contentH + kBookRowPadY;
  const int consumed = boxH + (rule ? kBookRowRuleH : 0);
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused) {
    // The whole box, which on the board is the row without its border: the
    // focused row is the one row the design gives no `border-bottom`, so its
    // fill runs to the next row's top edge.
    fb.fillRect(0, y, rowW, boxH, false);
  } else if (rule) {
    fb.fillRect(0, y + boxH, rowW, kBookRowRuleH, false);
  }

  const int contentTop = y + kBookRowPadY;

  // The thumbnail slot: a folder gets the folder mark and a book gets the book
  // mark, both centred in the slot on both axes and reversed out on a focused row.
  //
  // ONE EXPRESSION CHOOSING THE MARK, NOT TWO BRANCHES DRAWING IT. A book row used
  // to draw a 44x64 `ditherRect` plus an `outlineRect` -- the placeholder cover --
  // where a folder row drew a bare centred mark, so the two kinds of row placed
  // their contents by two different pieces of arithmetic that were free to drift.
  // design/Library.dc.html has dropped the placeholder (it claimed a picture the
  // row does not have, and six identical grey blocks down the left edge carried no
  // information), which makes a book row the folder row with a different mark --
  // and that is now what the code says.
  //
  // THE SLOT STAYS 44x64 THOUGH NEITHER MARK IS THAT TALL -- kFolder is 44x39 and
  // kBookRow 44x44. `bookRowContentH` is `max(kBookThumbH, the text column)`, so the
  // slot sets the row's height and therefore the whole list's geometry; sizing it to
  // whichever mark is taller would move every row on every Library screen for a
  // reason that has nothing to do with rows.
  //
  // kBookRow, NOT kBook: the two marks in this column are matched in STROKE now, so
  // a book does not read lighter than the folder one row above it. That is a second
  // 44px asset rather than a resize of the 25px one, because kBook at 25px is still
  // the `READ` hint's mark on three Home boards -- kBookLarge's split at 112px, one
  // size down. See design/Library.dc.html for the stroke arithmetic and for which
  // instance on that board the generator reads.
  //
  // The mark is centred in the row's CONTENT box directly rather than in the 44x64
  // box centred inside it. Concentric boxes compose exactly in real arithmetic --
  // the board's 91 is 78.5 + 12.5 and also 77 + 14 -- so nesting the two would only
  // add a second rounding for the same answer.
  const Icon& mark = row.isFolder ? icons::kFolder : icons::kBookRow;
  drawIcon(fb, mark, centreIn(kMargin, kBookThumbW, mark.w),
           iconTopIn(contentTop, contentH, mark.h), ink, plane);

  // The text column, centred in the content box as its own flex item -- which is
  // a no-op while the column is the taller of the two, and is what keeps the
  // text on the centre line if a future face makes the thumbnail win.
  const Font& tf = fonts[focused ? Role::Body700 : Role::Body500];
  const Font& mf = fonts[Role::Meta400];
  const Font& vf = fonts[Role::Value700];
  const int columnH = tf.lineHeight() + kBookLineGap + mf.lineHeight();
  const int columnTop = centreIn(contentTop, contentH, columnH);
  const int textX = kMargin + kBookThumbW + kBookThumbGap;
  const int rightEdge = rowW - kMargin;

  // BOTH of the column's lines truncate, and to the SAME budget, because they are
  // two children of one `min-width: 0` column on the board and neither is copy
  // the design chose: the title is a filename today and the meta line is an
  // author (Phase 3) or a folder's own summary. Truncating only the title would
  // leave the identical defect one line lower for whichever of the two grew
  // first.
  //
  // What the column has is everything between the thumbnail and the row's third
  // flex child, less the board's `gap: 16px` before it. The chevron and the value
  // are exclusive in the design -- a folder discloses, a book states its progress
  // -- and they are placed on the same right edge below, so the budget reserves
  // the wider of the two rather than their sum: reserving both would narrow every
  // row by a value's width for a trailing mark that is not there.
  int trailingW = row.isFolder ? icons::kChevron.w : 0;
  if (!row.value.empty()) trailingW = maxOf(trailingW, vf.measure(row.value));
  const int textMaxW =
      rightEdge - (trailingW > 0 ? trailingW + kBookThumbGap : 0) - textX;

  drawTextElided(fb, tf, textX, baselineIn(tf, columnTop, tf.lineHeight()), row.title, textMaxW,
                 ink, {}, plane);
  // 0.10em, the tighter of the boards' two meta trackings, and the same run
  // Home's chapter label is. No tracking on the title above it: the board sets
  // none there.
  drawTextElided(fb, mf, textX,
                 baselineIn(mf, columnTop + tf.lineHeight() + kBookLineGap, mf.lineHeight()),
                 row.meta, textMaxW, ink, trackingEm(mf, kTightMetaEm), plane);

  // Then either the chevron or the value, right-aligned on the margin and
  // centred in the content box.
  if (row.isFolder) {
    const Icon& chev = icons::kChevron;
    drawIcon(fb, chev, rightEdge - chev.w, iconTopIn(contentTop, contentH, chev.h), ink, plane);
  }
  if (!row.value.empty())
    drawText(fb, vf, rightEdge - vf.measure(row.value), baselineIn(vf, contentTop, contentH),
             row.value, ink, {}, plane);
  return consumed;
}

// --- design/BookDetails.dc.html's and Contents.dc.html's field row -----------

int detailRowHeight(bool rule, int contentH) { return contentH + (rule ? kDetailRowRuleH : 0); }

int drawDetailRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
                  std::string_view value, bool focused, bool rule, Plane plane,
                  int contentH) {
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(0, y, fb.width(), contentH, false);
  else if (rule)
    fb.fillRect(0, y + contentH, fb.width(), kDetailRowRuleH, false);
  // Value500 and Value700: the same size at two weights, which is the board's
  // own distinction between what a field is called and what it says. Neither run
  // is tracked -- the board sets no letter-spacing on either.
  const Font& lf = fonts[Role::Value500];
  const Font& vf = fonts[Role::Value700];
  // THE LABEL ELIDES, and it did not: it was drawn at full length from the left margin,
  // so a long one ran under the value and off the panel. Book details' labels are field
  // names and never overflowed, which is why this only surfaced when Contents put real
  // chapter names through it -- "PREMIÈRE PARTIE : À LIRE AVANT L'ACHAT" is wider than
  // the row.
  //
  // FIXED IN THE PRIMITIVE, not in the screen: a row that overflows its own box is
  // wrong on every screen that draws one, and the next caller would inherit it.
  //
  // AND THE DIVISION IS `labelShare`'s, WHICH IS WHERE THIS ROW'S OWN COPY OF IT
  // WENT (#82). It read `labelW = row - valueW`, which is the header band's old
  // rule verbatim and is correct HERE for a reason that is a fact about the
  // callers rather than about the primitive: every value this row is ever handed
  // is a literal from the screen (`NOW` on the chapter being read, `EPUB`, a
  // percentage) and every label is a fact about the card. Left as its own
  // expression it would be the second spelling of a rule the band has just had to
  // change -- so it is the same call, and it is pixel-identical for every input
  // any screen produces: `avail - valueNatural` is what the half-row floor
  // resolves to until BOTH runs are wider than half the row, which no caller can
  // reach with a two-to-four-character value.
  const int vNatural = value.empty() ? 0 : vf.measure(value);
  const int avail = fb.width() - 2 * kMargin - (value.empty() ? 0 : kBandGap);
  const int labelW = labelShare(lf.measure(label), vNatural, avail);
  drawText(fb, lf, kMargin, baselineIn(lf, y, contentH),
           elideToWidth(lf, label, labelW), ink, {}, plane);
  if (!value.empty())
    drawText(fb, vf, fb.width() - kMargin - vf.measure(value),
             baselineIn(vf, y, contentH), value, ink, {}, plane);
  return detailRowHeight(rule);
}

// --- An overlay's panel -----------------------------------------------------

void drawPanel(Framebuffer& fb, int x, int y, int w, int h) {
  fb.fillRect(x, y, w, h, true);
  outlineRect(fb, x, y, w, h, kPanelBorder);
}

Prose wrapPanelCaption(const FontSet& fonts, std::string_view label, int contentW,
                       WordBreak breaking) {
  const Font& lf = fonts[Role::Label500];
  // `line-height: normal` on the caption, so the line box is the face's own --
  // and the tracking is carried in the Prose, so the wrap and the draw measure
  // the same run.
  return wrapProseLead(lf, label, panelCaptionColumnW(contentW), pxToF26(lf.lineHeight()),
                       trackingEm(lf, kBandLabelEm), breaking);
}

int panelCaptionHeight(const FontSet& fonts, const Prose& label) {
  // The caption's own flex line: the wrapped label, and the value beside it. One
  // line of the label is the same 30px box the value's 27 fits inside on today's
  // ramp, so a one-line caption is the board's 74; a two-line one is 104, which
  // is DeleteConfirm's.
  const int content = maxOf(f26ToPx(label.heightF26()), fonts[Role::Meta400].lineHeight());
  return kPanelCaptionPadY + content + kPanelCaptionPadY + kPanelCaptionRuleH;
}

int drawPanelCaption(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                     const Prose& label, std::string_view value, Plane plane) {
  const Font& lf = fonts[Role::Label500];
  const Font& vf = fonts[Role::Meta400];
  const int capH = panelCaptionHeight(fonts, label);
  const int contentTop = y + kPanelCaptionPadY;
  const int contentH = capH - 2 * kPanelCaptionPadY - kPanelCaptionRuleH;
  const int colX = x + kPanelPadX;
  const int colW = w - 2 * kPanelPadX;
  // The label is a block in the flex line, so it starts at the line's top when
  // it is the tallest item and is centred when it is not.
  const int labelTopF26 = pxToF26(centreIn(contentTop, contentH, f26ToPx(label.heightF26())));
  drawProse(fb, lf, label, colX, colW, labelTopF26, Ink::Black, plane, ProseAlign::Left);
  if (!value.empty()) {
    // `justify-content: space-between`, so the value's right edge is the
    // caption's own padding edge -- not the panel's border and not the screen
    // margin.
    const int vw = vf.measure(value, trackingEm(vf, kHintEm));
    drawText(fb, vf, colX + colW - vw, baselineIn(vf, contentTop, contentH), value, Ink::Black,
             trackingEm(vf, kHintEm), plane);
  }
  fb.fillRect(x, y + capH - kPanelCaptionRuleH, w, kPanelCaptionRuleH, false);
  return capH;
}

int panelRowHeight(bool rule) { return kPanelRowContentH + (rule ? kPanelRowRuleH : 0); }

// --- A list's section header -------------------------------------------------

int sectionHeaderHeight(const FontSet& fonts, bool rule) {
  return (rule ? kSectionRuleH : 0) + kSectionPadTop + fonts[Role::Meta500].lineHeight() +
         kSectionPadBottom;
}

int drawSectionHeader(Framebuffer& fb, const FontSet& fonts, int y, int w,
                      std::string_view label, bool rule, Plane plane) {
  const Font& f = fonts[Role::Meta500];
  // POSITIONAL, not by identity: the rule separates a section from the content above
  // it, and at the top of a window the header band IS that separation whichever
  // section happens to be scrolled there. Drawing it unconditionally read as a stray
  // separator against the top bar on Settings, which is where this box was written.
  const int ruleH = rule ? kSectionRuleH : 0;
  if (rule) fb.fillRect(0, y, w, kSectionRuleH, false);
  const int textTop = y + ruleH + kSectionPadTop;
  drawText(fb, f, kMargin, baselineIn(f, textTop, f.lineHeight()), label, Ink::Black,
           trackingEm(f, kSectionEm), plane);
  // The height ACTUALLY DRAWN, so a caller advancing by it cannot disagree with what
  // is on glass -- a first header is shorter by its missing rule.
  // ONE EXPRESSION with the height a caller reserves, so the two cannot
  // disagree -- this used to subtract the difference from the nominal height,
  // which is the same answer arrived at separately.
  (void)ruleH;
  return sectionHeaderHeight(fonts, rule);
}

int drawPanelRow(Framebuffer& fb, const FontSet& fonts, int x, int y, int w,
                 std::string_view label, bool focused, bool discloses, bool rule, Plane plane,
                 std::string_view value, int labelTrackingEm1000) {
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(x, y, w, kPanelRowContentH, false);
  else if (rule)
    fb.fillRect(x, y + kPanelRowContentH, w, kPanelRowRuleH, false);
  // Value700 focused, Value500 otherwise -- the board's own declaration, and the
  // same weight-follows-focus rule a Library row's title has.
  const Font& lf = fonts[focused ? Role::Value700 : Role::Value500];
  drawText(fb, lf, x + kPanelPadX, baselineIn(lf, y, kPanelRowContentH), label, ink,
           labelTrackingEm1000 == 0 ? Tracking{} : trackingEm(lf, labelTrackingEm1000), plane);
  // A ROW STATES A QUANTITY OR DISCLOSES A SCREEN, NEVER BOTH -- the same rule Home's
  // menu rows follow, and the reader menu is where a panel row first needed the other
  // half of it: its `Bookmarks` row carried a count where its siblings carried chevrons.
  // That row is CUT (#55, bookmarks moved to V1.1), so this branch has no producer in
  // the firmware today and its behaviour is pinned directly instead -- test_components
  // .cpp, "a panel row states a quantity or discloses a screen, never both". Value700 in
  // both focus states, as the board drew it.
  if (!value.empty()) {
    const Font& vf = fonts[Role::Value700];
    drawText(fb, vf, x + w - kPanelPadX - vf.measure(value), baselineIn(vf, y, kPanelRowContentH),
             value, ink, {}, plane);
  } else if (discloses) {
    const Icon& chev = icons::kChevron;
    drawIcon(fb, chev, x + w - kPanelPadX - chev.w,
             iconTopIn(y, kPanelRowContentH, chev.h), ink, plane);
  }
  return panelRowHeight(rule);
}

void drawSignalBars(Framebuffer& fb, int x, int y, int level, Ink ink) {
  if (level < 1) level = 1;
  if (level > 3) level = 3;
  // The board's three rects, scaled from its `viewBox="0 0 17 13"` onto the
  // 30x23 box it draws them in: x at 0/6/12 and w 4 become 0/11/21 and 7, and
  // heights 5/9/13 become 9/16/23. Bottom-aligned, which is what makes them
  // read as a meter rather than three unrelated marks.
  constexpr int kBarX[3] = {0, 11, 21};
  constexpr int kBarW = 7;
  constexpr int kBarH[3] = {9, 16, kSignalH};
  const bool white = (ink == Ink::White);
  for (int i = 0; i < 3; ++i) {
    const int bx = x + kBarX[i];
    const int bh = kBarH[i];
    const int by = y + kSignalH - bh;
    if (i < level) {
      fb.fillRect(bx, by, kBarW, bh, white);
    } else {
      // `fill: none; stroke-width: 1` on a viewBox scaled ~1.77x, so the
      // painted stroke is ~2px. An unlit bar is an outline rather than an
      // absence: the meter's full extent is what says how many bars there
      // could be.
      outlineRect(fb, bx, by, kBarW, bh, 2, white);
    }
  }
}

}  // namespace reader
