#pragma once
#include "reader/text.h"

namespace reader {
class Framebuffer;
class FontSet;
struct HomeViewModel;
struct SdMissingViewModel;
struct StubViewModel;

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
  // The no-card prompt. A product screen, so it gets its own typed method beside
  // renderHome rather than borrowing the placeholder surface below: it has no
  // header band, no rows and no battery reading, and nothing about it is a list.
  virtual void renderSdMissing(Framebuffer& fb, const FontSet& fonts,
                               const SdMissingViewModel& vm, Plane plane = Plane::Bw) = 0;
  // The provisional Phase 2B surface. A virtual on Theme rather than a screen
  // drawing its own pixels, because "screens never draw pixels directly" holds
  // for scaffolding too -- a diagnostic that bypassed the theme would be the
  // precedent that erodes the rule.
  virtual void renderStub(Framebuffer& fb, const FontSet& fonts, const StubViewModel& vm,
                          Plane plane = Plane::Bw) = 0;
};
}  // namespace reader
