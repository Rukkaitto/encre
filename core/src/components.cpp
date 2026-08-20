#include "reader/components.h"

#include "reader/framebuffer.h"
#include "reader/text.h"

namespace reader {

int drawHeaderBand(Framebuffer& fb, const FontSet& fonts, std::string_view label,
                   std::string_view value) {
  const Font& lf = fonts[Role::Label];
  const Font& vf = fonts[Role::Value];
  const int baseline = kBandH / 2 + lf.ascent() / 2;
  drawText(fb, lf, kMargin, baseline, label, Ink::Black, kLabelTracking);
  const int vw = vf.measure(value);
  drawText(fb, vf, fb.width() - kMargin - vw, baseline, value);
  fb.fillRect(0, kBandH - 2, fb.width(), 2, false);
  return kBandH;
}

int drawRow(Framebuffer& fb, const FontSet& fonts, int y, std::string_view label,
            std::string_view value, bool focused) {
  const Font& lf = fonts[Role::Body];
  const Font& vf = fonts[Role::Value];
  const Ink ink = focused ? Ink::White : Ink::Black;
  if (focused)
    fb.fillRect(0, y, fb.width(), kRowH, false);
  else
    fb.fillRect(0, y, fb.width(), 1, false);  // hairline above
  const int baseline = y + kRowH / 2 + lf.ascent() / 2;
  drawText(fb, lf, kMargin, baseline, label, ink);
  if (!value.empty()) {
    const int vw = vf.measure(value);
    drawText(fb, vf, fb.width() - kMargin - vw, baseline, value, ink);
  }
  return kRowH;
}

int drawHintBar(Framebuffer& fb, const FontSet& fonts, const Hint hints[4], int slotXOut[4]) {
  const Font& mf = fonts[Role::Meta];
  const int top = fb.height() - kHintBarH;
  fb.fillRect(0, top, fb.width(), 1, false);

  // Measure every slot, then distribute the leftover space evenly. This is what
  // makes the bar correct on both 480 and 528 wide canvases: nothing is pinned.
  int widths[4] = {};
  int total = 0;
  for (int i = 0; i < 4; ++i) {
    const int iconW = hints[i].icon ? hints[i].icon->w + 6 : 0;
    const int textW = mf.measure(hints[i].label, kLabelTracking);
    const int holdW = hints[i].hold.empty() ? 0 : mf.measure(hints[i].hold, kLabelTracking);
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
  int x = kMargin;
  const int center = top + kHintBarH / 2 + mf.ascent() / 2;
  for (int i = 0; i < 4; ++i) {
    const int baseline = hints[i].hold.empty() ? center : center - mf.lineHeight() / 2;
    slotXOut[i] = x;
    int textX = x;
    if (hints[i].icon) {
      drawIcon(fb, *hints[i].icon, x, baseline - hints[i].icon->h + 2);
      textX += hints[i].icon->w + 6;
    }
    drawText(fb, mf, textX, baseline, hints[i].label, Ink::Black, kLabelTracking);
    if (!hints[i].hold.empty())
      drawText(fb, mf, textX, baseline + mf.lineHeight(), hints[i].hold, Ink::Black,
               kLabelTracking);
    x += widths[i] + gap;
  }
  return kHintBarH;
}

}  // namespace reader
