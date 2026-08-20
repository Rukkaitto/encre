#pragma once
#include "reader/fontset.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, a 2px-ruled header band, hairline rows,
// black fill for focus. Layout follows the design canvas and derives every
// horizontal position from the framebuffer, so the same code composes correctly
// on the X4's 480x800 and the X3's 528x792.
class QuietTheme : public Theme {
 public:
  void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                  Plane plane = Plane::Bw) override;
};

}  // namespace reader
