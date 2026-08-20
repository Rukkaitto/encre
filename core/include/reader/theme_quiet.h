#pragma once
#include "reader/font.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, 2px header rule, hairline rows,
// black fill = focus. Layout constants follow the design canvas.
class QuietTheme : public Theme {
 public:
  bool loadFonts(const uint8_t* uiFontData, size_t uiFontSize);
  void renderHome(Framebuffer& fb, const HomeViewModel& vm) override;

 private:
  Font ui_;
  void headerBand(Framebuffer& fb, const char* label, const std::string& right);
  void hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints);
  void textInverted(Framebuffer& fb, int x, int baseline, std::string_view s);
};

}  // namespace reader
