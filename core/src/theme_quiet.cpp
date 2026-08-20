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

bool QuietTheme::loadFonts(const uint8_t* d, size_t n) { return ui_.load(d, n); }

void QuietTheme::textInverted(Framebuffer& fb, int x, int baseline, std::string_view s) {
  // Draw white-on-black: render into place by flipping pixels the glyphs touch.
  // Simple approach: draw black text into a scratch fb, then invert-copy.
  Framebuffer scratch(fb.width(), fb.height());
  drawText(scratch, ui_, x, baseline, s);
  for (int y = 0; y < fb.height(); ++y)
    for (int xx = 0; xx < fb.width(); ++xx)
      if (!scratch.getPixel(xx, y)) fb.setPixel(xx, y, true);
}

void QuietTheme::headerBand(Framebuffer& fb, const char* label, const std::string& right) {
  const int baseline = 33;
  drawText(fb, ui_, kMargin, baseline, label);
  const int rw = ui_.measure(right);
  drawText(fb, ui_, fb.width() - kMargin - rw, baseline, right);
  fb.fillRect(0, kHeaderH - 2, fb.width(), 2, false);
}

void QuietTheme::hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints) {
  const int top = fb.height() - kHintH;
  fb.fillRect(0, top, fb.width(), 1, false);
  const int baseline = top + 30;
  // 4 fixed slots: Back left, Confirm center-left, Up, Down right.
  const int slotX[4] = {kMargin, 170, 330, 408};
  for (int i = 0; i < 4; ++i)
    if (!hints[i].empty()) drawText(fb, ui_, slotX[i], baseline, hints[i]);
}

void QuietTheme::renderHome(Framebuffer& fb, const HomeViewModel& vm) {
  fb.clear(true);
  headerBand(fb, "NOW READING", std::to_string(vm.batteryPercent) + "%");

  int y = kHeaderH + 30;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.title);
  y += ui_.lineHeight() + 4;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.author);
  y += ui_.lineHeight() + 18;

  const std::string pct = std::to_string(vm.percent) + "%";
  drawText(fb, ui_, kMargin, y + ui_.ascent(), pct);
  const std::string page =
      "PAGE " + std::to_string(vm.currentPage) + " / " + std::to_string(vm.pageCount);
  drawText(fb, ui_, kMargin + 90, y + ui_.ascent(), page);
  y += ui_.lineHeight() + 6;
  drawText(fb, ui_, kMargin, y + ui_.ascent(), vm.chapterLabel);
  y += ui_.lineHeight() + 22;

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
    textInverted(fb, kMargin + 20, y + 33, "CONTINUE");
  } else {
    fb.fillRect(kMargin, y, barW, 2, false);
    fb.fillRect(kMargin, y + blockH - 2, barW, 2, false);
    fb.fillRect(kMargin, y, 2, blockH, false);
    fb.fillRect(kMargin + barW - 2, y, 2, blockH, false);
    drawText(fb, ui_, kMargin + 20, y + 33, "CONTINUE");
  }

  // Menu rows anchored above the hint bar.
  const int menuTop = fb.height() - kHintH - static_cast<int>(vm.menu.size()) * kRowH;
  for (size_t i = 0; i < vm.menu.size(); ++i) {
    const int ry = menuTop + static_cast<int>(i) * kRowH;
    const bool focused = (static_cast<int>(i) == vm.focusedMenuIndex);
    if (focused) {
      fb.fillRect(0, ry, fb.width(), kRowH, false);
      textInverted(fb, kMargin, ry + 35, vm.menu[i].label);
      const int rw = ui_.measure(vm.menu[i].value);
      textInverted(fb, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    } else {
      fb.fillRect(0, ry, fb.width(), 1, false);
      drawText(fb, ui_, kMargin, ry + 35, vm.menu[i].label);
      const int rw = ui_.measure(vm.menu[i].value);
      drawText(fb, ui_, fb.width() - kMargin - rw, ry + 35, vm.menu[i].value);
    }
  }

  hintBar(fb, vm.hints);
}

}  // namespace reader
