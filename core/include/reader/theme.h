#pragma once
namespace reader {
class Framebuffer;
struct HomeViewModel;

// Themes own the entire presentation (spec 3.3). Phase 1: Home only;
// later phases extend this interface one screen at a time.
class Theme {
 public:
  virtual ~Theme() = default;
  virtual void renderHome(Framebuffer& fb, const HomeViewModel& vm) = 0;
};
}  // namespace reader
