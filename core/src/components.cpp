#include "reader/components.h"

#include "reader/framebuffer.h"
#include "reader/text.h"

namespace reader {

int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value, Plane plane) {
  const Font& lf = fonts[Role::Label];
  const Font& vf = fonts[Role::Value];
  // The band's content box is everything above its 2px rule. The label and the
  // value are two different roles (23px against 25px), so they get two
  // baselines: the board centres each flex item on its own line box, and one
  // shared baseline taken from the label would sit the value 1px low. Both come
  // out of the same box, so they still agree optically.
  const int boxH = kBandH - 2;
  const int labelBase = baselineIn(lf, 0, boxH);
  const int valueBase = baselineIn(vf, 0, boxH);
  drawText(fb, lf, kMargin, labelBase, label, Ink::Black, kBandLabelTracking, plane);
  // The value and the battery glyph are one right-aligned group: the icon's
  // right edge, not the text's, lands on the margin. Right-aligning the value
  // alone and hanging the icon off it would push the glyph past the margin.
  const Icon& bat = icons::kBattery;
  const int vw = vf.measure(value);
  const int groupW = vw + kBandGap + bat.w;
  const int groupX = fb.width() - kMargin - groupW;
  drawText(fb, vf, groupX, valueBase, value, Ink::Black, 0, plane);
  // Centred on the value it belongs to, which is what the design's
  // `align-items: center` does to that flex row -- not on the band, and not
  // hung off the baseline by a constant. The battery is 21px where the marks
  // elsewhere are 25, so a constant offset cannot serve both.
  drawIcon(fb, bat, groupX + vw + kBandGap, iconTopFor(vf, valueBase, bat.h), Ink::Black, plane);
  fb.fillRect(0, kBandH - 2, fb.width(), 2, false);
  return kBandH;
}

int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused, const Icon* trailing, Plane plane) {
  // Role::Label, not Role::Body: the boards set a menu row's label to
  // `--t-label` (11pt/23px, weight 500) with `letter-spacing: 0.18em`. Body is
  // 14pt/29px and is what a *list item's title* uses -- a different thing on a
  // different screen. Drawing rows in Body made LIBRARY and SETTINGS six pixels
  // too tall and, with tracking 0, visibly too tight.
  const Font& lf = fonts[Role::Label];
  const Font& vf = fonts[Role::Value];
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(0, y, fb.width(), kRowH, false);
  else
    fb.fillRect(0, y, fb.width(), 1, false);  // hairline above
  const int labelBase = baselineIn(lf, y, kRowH);
  const int valueBase = baselineIn(vf, y, kRowH);
  drawText(fb, lf, kMargin, labelBase, label, ink, kRowLabelTracking, plane);
  // A row carries a value, a trailing mark, or neither -- the design has one of
  // each (LIBRARY's count, SETTINGS' chevron). Both are right-aligned on the
  // margin; the icon takes the row's ink, so it inverts with a focused row.
  int rightEdge = fb.width() - kMargin;
  if (trailing) {
    drawIcon(fb, *trailing, rightEdge - trailing->w, iconTopFor(lf, labelBase, trailing->h), ink,
             plane);
    rightEdge -= trailing->w + kRowGap;
  }
  if (!value.empty())
    drawText(fb, vf, rightEdge - vf.measure(value), valueBase, value, ink, 0, plane);
  return kRowH;
}

int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4],
                Plane plane) {
  const Font& mf = fonts[Role::Meta];
  const int top = fb.height() - kHintBarH;
  fb.fillRect(0, top, fb.width(), 1, false);

  // Measure every slot, then distribute the leftover space evenly. This is what
  // makes the bar correct on both 480 and 528 wide canvases: nothing is pinned.
  int widths[4] = {};
  int total = 0;
  for (int i = 0; i < 4; ++i) {
    const int iconW = hints[i].icon ? hints[i].icon->w + kHintIconGap : 0;
    const int textW = mf.measure(hints[i].label, kHintTracking);
    const int holdW = hints[i].hold.empty() ? 0 : mf.measure(hints[i].hold, kHintTracking);
    widths[i] = iconW + (textW > holdW ? textW : holdW);
    total += widths[i];
  }
  const int usable = fb.width() - 2 * kMargin;
  const int gap = (usable > total && total > 0) ? (usable - total) / 3 : 0;

  // Every slot is centred on the bar's own centre line, which is what the
  // design's `align-items: center` does: a single-line slot sits on that line,
  // and a slot carrying a hold line straddles it so the two-line block stays
  // centred. The baseline is therefore per slot, not shared. One shared
  // baseline keyed on slot 0 would miss the real bars entirely — Home's and
  // Library's hold sits on the Confirm slot, so no shift would happen and the
  // hold line's baseline would land 1px above the bar's last row, clipping any
  // descender against the bottom edge; and when slot 0 *did* carry the hold it
  // would drag the single-line slots off centre with it.
  //
  // Both cases are now one expression: an n-line block of line boxes, centred
  // in the bar, with each line's baseline taken from its own box. At n == 1 that
  // reduces exactly to centring the single run in the whole bar, so the two
  // cases cannot drift apart.
  int x = kMargin;
  const int lineH = mf.lineHeight();
  for (int i = 0; i < 4; ++i) {
    const int lines = hints[i].hold.empty() ? 1 : 2;
    const int blockTop = top + (kHintBarH - lines * lineH) / 2;
    const int baseline = baselineIn(mf, blockTop, lineH);
    slotXOut[i] = x;
    int textX = x;
    if (hints[i].icon) {
      // Centred on the label it labels. The bar's marks are a uniform box today,
      // but the placement must not depend on that: Reader's bar pairs a 21px
      // battery with them, and a baseline-relative offset would put it low.
      drawIcon(fb, *hints[i].icon, x, iconTopFor(mf, baseline, hints[i].icon->h), Ink::Black,
               plane);
      textX += hints[i].icon->w + kHintIconGap;
    }
    drawText(fb, mf, textX, baseline, hints[i].label, Ink::Black, kHintTracking, plane);
    if (!hints[i].hold.empty())
      drawText(fb, mf, textX, baseline + lineH, hints[i].hold, Ink::Black, kHintTracking, plane);
    x += widths[i] + gap;
  }
  return kHintBarH;
}

}  // namespace reader
