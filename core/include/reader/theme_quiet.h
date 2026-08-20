#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "reader/font.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, 2px header rule, hairline rows,
// black fill = focus. Layout constants follow the design canvas.
//
// Two faces, same size, different weight: labels are 500 and values are 700.
// The split is what makes a value read as the answer and its label as the
// question, and at 16px on a 1-bit panel weight is the only lever available -
// there is no colour, no size ramp yet, and no anti-aliasing in the chrome.
class QuietTheme : public Theme {
 public:
  // Both blobs must outlive the theme (Font is a zero-copy view). Raw pointers
  // rather than paths: core/ does no file I/O, and the firmware compiles the
  // fonts in as byte arrays.
  bool loadFonts(const uint8_t* labelFontData, size_t labelFontSize,
                 const uint8_t* valueFontData, size_t valueFontSize);
  void renderHome(Framebuffer& fb, const HomeViewModel& vm) override;

 private:
  Font label_;  // weight 500: labels, titles, hints
  Font value_;  // weight 700: numeric values and the focused action
  void headerBand(Framebuffer& fb, const char* label, const std::string& right);
  void hintBar(Framebuffer& fb, const std::array<std::string, 4>& hints);
  void textInverted(Framebuffer& fb, const Font& font, int x, int baseline, std::string_view s);
};

}  // namespace reader
