#pragma once
#include "reader/text.h"

namespace reader {
class Framebuffer;
class FontSet;
struct HomeViewModel;

// Themes own the entire presentation, layout structure included (spec 3.3).
// The FontSet is supplied by the caller so device knowledge — which asset backs
// which role, any board uiScale — stays out of core/.
class Theme {
 public:
  virtual ~Theme() = default;
  // `plane` defaults to Plane::Bw so existing callers compile unchanged; the
  // caller is expected to invoke this three times, once per Plane, to produce
  // the base frame and the two grey bit-planes (see reader/text.h).
  virtual void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                          Plane plane = Plane::Bw) = 0;
};
}  // namespace reader
