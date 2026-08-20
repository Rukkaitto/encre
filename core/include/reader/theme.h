#pragma once
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
  virtual void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm) = 0;
};
}  // namespace reader
