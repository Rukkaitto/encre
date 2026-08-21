#include "reader/components.h"

#include "reader/framebuffer.h"
#include "reader/text.h"

namespace reader {

namespace {
int maxOf(int a, int b) { return a > b ? a : b; }

// The tallest item in the header band's flex row: the label's line box, the
// value's, and -- because the battery sits inside the value's item rather than
// beside it -- the glyph's own height.
int bandContentH(const FontSet& fonts) {
  return maxOf(maxOf(fonts[Role::Label500].lineHeight(), fonts[Role::Value700].lineHeight()),
               icons::kBattery.h);
}

// One hint slot's height. The slot is a single flex row of the leading mark, the
// label and -- when the button has a long-press action -- the hollow hold ring,
// so it is as tall as the tallest of them. Nothing here adds a second line box:
// design 662557d made the hold a mark on this row rather than a line under it,
// so a hold cannot change a slot's height and therefore cannot change the bar's.
// The height is still *derived* from the content rather than pinned, which is
// what keeps it correct for a screen that sets its hints in a larger role or
// pairs them with a taller mark.
int hintSlotH(const Font& mf, const Hint& hint) {
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

int hintSlotW(const Font& mf, const Hint& hint, Tracking tracking) {
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

int headerBandHeight(const FontSet& fonts) {
  return kBandPadTop + bandContentH(fonts) + kBandPadBottom + kBandRuleH;
}

int hintBarHeight(const FontSet& fonts, const Hint hints[4]) {
  return kHintRuleH + kHintPadTop + hintContentH(fonts, hints) + kHintPadBottom;
}

int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value, Plane plane) {
  const Font& lf = fonts[Role::Label500];
  const Font& vf = fonts[Role::Value700];
  // The band's content box is the strip the board's padding leaves between the
  // top edge and the rule -- not "everything above the rule", which is what the
  // pinned height made it and which is why the rule sat 6px low. The label and
  // the value are two different roles (23px against 25px), so they get two
  // baselines: the board centres each flex item on its own line box, and one
  // shared baseline taken from the label would sit the value 1px low. Both come
  // out of the same box, so they still agree optically.
  const int bandH = headerBandHeight(fonts);
  const int contentH = bandContentH(fonts);
  const int labelBase = baselineIn(lf, kBandPadTop, contentH);
  const int valueBase = baselineIn(vf, kBandPadTop, contentH);
  drawText(fb, lf, kMargin, labelBase, label, Ink::Black, trackingEm(lf, kBandLabelEm), plane);
  // The value and the battery glyph are one right-aligned group: the icon's
  // right edge, not the text's, lands on the margin. Right-aligning the value
  // alone and hanging the icon off it would push the glyph past the margin.
  const Icon& bat = icons::kBattery;
  const int vw = vf.measure(value);
  const int groupW = vw + kBandGap + bat.w;
  const int groupX = fb.width() - kMargin - groupW;
  drawText(fb, vf, groupX, valueBase, value, Ink::Black, {}, plane);
  // Centred in the band's content box, which is what the design's
  // `align-items: center` does to that flex row and its nested value+battery
  // row alike: every child of a flex line, whatever its height, centres on the
  // line's one cross-axis centre. The battery is 21px where the marks elsewhere
  // are 25, so a constant offset cannot serve both -- and deriving the centre
  // from the value's baseline instead of from the box, which is what this used
  // to do, spent three integer divisions to reach the same number and landed
  // the glyph 1.5px below the percentage it belongs to.
  drawIcon(fb, bat, groupX + vw + kBandGap, iconTopIn(kBandPadTop, contentH, bat.h), Ink::Black,
           plane);
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
    const int slotTop = contentTop + (contentH - slotH) / 2;
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
                     std::string_view label, Plane plane) {
  const Font& lf = fonts[Role::Value700];
  fb.fillRect(x, y, w, kActionH, false);
  // Reversed out of the slab, which is what Ink::White is for: no scratch
  // buffer, no second pass. The label is centred on both axes because the
  // board's block is `align-items: center; justify-content: center` -- the same
  // two helpers, one per axis, that every other centred box on every screen
  // resolves against.
  const Tracking tracking = trackingEm(lf, kActionEm);
  const int labelW = lf.measure(label, tracking);
  drawText(fb, lf, centreIn(x, w, labelW), baselineIn(lf, y, kActionH), label, Ink::White,
           tracking, plane);
  return kActionH;
}

Prose wrapProse(const Font& font, std::string_view text, int maxW, int leadEm1000,
                Tracking tracking) {
  Prose out;
  out.tracking = tracking;
  // The board states the leading as a multiple of the font size, so it resolves
  // against the face exactly as letter-spacing does -- and lands on the same 1/64
  // px unit, for the same reason: 1.55 x 29 is 44.95, and three line boxes of a
  // pre-rounded 45 put the last line a pixel low.
  out.leadF26 = Tracking::em(font.ppem(), leadEm1000).f26();

  size_t lineStart = 0;   // first byte of the line being built
  size_t lineEnd = 0;     // one past its last non-space byte
  size_t i = 0;
  while (i < text.size()) {
    // One word, plus the run of spaces before it.
    while (i < text.size() && text[i] == ' ') ++i;
    const size_t wordStart = i;
    while (i < text.size() && text[i] != ' ') ++i;
    if (wordStart == i) break;  // trailing spaces only
    const bool lineEmpty = (lineEnd == lineStart);
    if (lineEmpty) {
      // The first word of a line goes on it whatever it measures: a word wider
      // than the column has nowhere better to be, and breaking inside it would
      // be a hyphenation decision this function is not making.
      lineStart = wordStart;
      lineEnd = i;
      continue;
    }
    // The candidate is measured from the line's start, spaces included, because
    // that is the run that will be drawn -- measuring the word alone and adding
    // a space's advance would lose every kern across the join.
    if (font.measure(text.substr(lineStart, i - lineStart), tracking) <= maxW) {
      lineEnd = i;
      continue;
    }
    out.lines.push_back(text.substr(lineStart, lineEnd - lineStart));
    lineStart = wordStart;
    lineEnd = i;
  }
  if (lineEnd > lineStart) out.lines.push_back(text.substr(lineStart, lineEnd - lineStart));
  return out;
}

int drawProse(Framebuffer& fb, const Font& font, const Prose& prose, int boxX, int boxW,
              int topF26, Ink ink, Plane plane) {
  for (int i = 0; i < prose.lineCount(); ++i) {
    const std::string_view line = prose.lines[static_cast<size_t>(i)];
    // Each line is centred in the column, on its own measured width -- which is
    // what `text-align: center` does. Not on the widest line's, and not on the
    // column's centre with a half-width offset: that spends a second division.
    const int x = centreIn(boxX, boxW, font.measure(line, prose.tracking));
    const int baseline = baselineInF26(font, topF26 + i * prose.leadF26, prose.leadF26);
    drawText(fb, font, x, baseline, line, ink, prose.tracking, plane);
  }
  return prose.heightF26();
}

}  // namespace reader
