#include "reader/theme_quiet.h"

#include "reader/framebuffer.h"
#include "reader/text.h"
#include "reader/viewmodel.h"

namespace reader {

namespace {
constexpr int kMargin = 24;
constexpr int kHeaderH = 52;
constexpr int kRowH = 56;
constexpr int kHintH = 46;
}  // namespace

bool QuietTheme::loadFonts(const uint8_t* labelData, size_t labelSize, const uint8_t* valueData,
                           size_t valueSize) {
  // Both or neither: a half-loaded theme would silently draw nothing for one
  // of the two roles.
  return label_.load(labelData, labelSize) && value_.load(valueData, valueSize);
}

void QuietTheme::textInverted(Framebuffer& fb, const Font& font, int x, int baseline,
                              std::string_view s) {
  // Draw white-on-black: render into place by flipping pixels the glyphs touch.
  // Simple approach: draw black text into a scratch fb, then invert-copy.
  Framebuffer scratch(fb.width(), fb.height());
  drawText(scratch, font, x, baseline, s);
  for (int y = 0; y < fb.height(); ++y)
    for (int xx = 0; xx < fb.width(); ++xx)
      if (!scratch.getPixel(xx, y)) fb.setPixel(xx, y, true);
}

void QuietTheme::headerBand(Framebuffer& fb, const char* label, const std::string& right) {
  const int baseline = 33;
  drawText(fb, label_, kMargin, baseline, label);
  // Measured with value_ because it is drawn with value_ - measuring with the
  // other face would push the string off the right margin by a few pixels.
  const int rw = value_.measure(right);
  drawText(fb, value_, fb.width() - kMargin - rw, baseline, right);
  fb.fillRect(0, kHeaderH - 2, fb.width(), 2, false);
}

void QuietTheme::hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints) {
  const int top = fb.height() - kHintH;
  fb.fillRect(0, top, fb.width(), 1, false);
  const int baseline = top + 30;
  // 4 fixed slots: Back left, Confirm center-left, Up, Down right.
  const int slotX[4] = {kMargin, 170, 330, 408};
  for (int i = 0; i < 4; ++i)
    if (!hints[i].empty()) drawText(fb, label_, slotX[i], baseline, hints[i]);
}

void QuietTheme::renderHome(Framebuffer& fb, const HomeViewModel& vm) {
  fb.clear(true);
  headerBand(fb, "NOW READING", std::to_string(vm.batteryPercent) + "%");

  // Vertical rhythm comes off the label face throughout: both faces are 16px
  // and share their metrics, and driving layout from one of them keeps the
  // row positions independent of which face a given string is drawn in.
  int y = kHeaderH + 30;
  drawText(fb, label_, kMargin, y + label_.ascent(), vm.title);
  y += label_.lineHeight() + 4;
  drawText(fb, label_, kMargin, y + label_.ascent(), vm.author);
  y += label_.lineHeight() + 18;

  const std::string pct = std::to_string(vm.percent) + "%";
  drawText(fb, label_, kMargin, y + label_.ascent(), pct);
  const std::string page =
      "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount);
  drawText(fb, label_, kMargin + 90, y + label_.ascent(), page);
  y += label_.lineHeight() + 6;
  drawText(fb, label_, kMargin, y + label_.ascent(), vm.chapterLabel);
  y += label_.lineHeight() + 22;

  // Progress bar: 1px border, filled portion black.
  const int barW = fb.width() - 2 * kMargin;
  fb.fillRect(kMargin, y, barW, 8, false);
  fb.fillRect(kMargin + 1, y + 1, barW - 2, 6, true);
  fb.fillRect(kMargin + 1, y + 1, (barW - 2) * vm.percent / 100, 6, false);
  y += 8 + 22;

  // Continue block (focused when focusedMenuIndex == -1): black fill + inverted text.
  const int blockH = 52;
  if (vm.focusedMenuIndex == -1) {
    fb.fillRect(kMargin, y, barW, blockH, false);
    // Focused: the primary action, so the heavier face.
    textInverted(fb, value_, kMargin + 20, y + 33, "CONTINUE");
  } else {
    fb.fillRect(kMargin, y, barW, 2, false);
    fb.fillRect(kMargin, y + blockH - 2, barW, 2, false);
    fb.fillRect(kMargin, y, 2, blockH, false);
    fb.fillRect(kMargin + barW - 2, y, 2, blockH, false);
    drawText(fb, label_, kMargin + 20, y + 33, "CONTINUE");
  }

  // Menu rows anchored above the hint bar.
  const int menuTop = fb.height() - kHintH - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    const int ry = menuTop + static_cast<int>(i) * kRowH;
    const bool focused = (static_cast<int>(i) == vm.focusedMenuIndex);
    // Row label is the label face, its right-hand value the value face; the
    // right edge is measured with value_ so it stays flush to the margin.
    if (focused) {
      fb.fillRect(0, ry, fb.width(), kRowH, false);
      textInverted(fb, label_, kMargin, ry + 35, vm.menu[i].label);
      const int rw = value_.measure(vm.menu[i].value);
      textInverted(fb, value_, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    } else {
      fb.fillRect(0, ry, fb.width(), 1, false);
      drawText(fb, label_, kMargin, ry + 35, vm.menu[i].label);
      const int rw = value_.measure(vm.menu[i].value);
      drawText(fb, value_, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    }
  }

  hintBar(fb, vm.hints);
}

}  // namespace reader
