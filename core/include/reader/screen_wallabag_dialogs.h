#pragma once
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// design/WallabagConnecting.dc.html AND design/WallabagFetching.dc.html -- ONE
// screen, TWO STAGES. WifiConnectScreen's shape.
//
// IT EXISTS IN TWO STAGES BECAUSE ONE CAPTION FOR NINETY SECONDS READS AS FROZEN.
// A sync of fifty EPUBs over an ESP32 radio is a minute or more, on glass that
// holds its last image with no power -- so the caption changes once files start
// arriving and the count advances per file. See the board note, which carries the
// measurement and the cancel contract.
class WallabagConnectingScreen : public FocusScreen {
 public:
  explicit WallabagConnectingScreen(std::string host);

  ScreenId id() const override { return ScreenId::WallabagConnecting; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const WallabagConnectingViewModel& vm() const { return vm_; }

  // STEP TO THE FETCHING STAGE, or advance its count. Returns whether anything
  // on the panel changed, so the shell can decide whether to spend a waveform --
  // a screen cannot mark the App dirty and must not try, which is why this
  // reports rather than acts. One paint per FILE, never per byte.
  bool setFetching(int done, int total);

  bool cancelled() const { return cancelled_; }

 protected:
  void syncVm() override {}

 private:
  std::string host_;
  WallabagConnectingViewModel vm_;
  bool cancelled_ = false;
};

// design/WallabagError.dc.html and its two siblings -- ONE SCREEN, THREE COPY
// SHAPES, which is BookError's argument and the join flow's precedent: a sync
// fails three distinguishable ways and one sentence would be a lie.
class WallabagErrorScreen : public FocusScreen {
 public:
  enum class Shape {
    // wallabag answered and refused the credentials. Deterministic -- the same
    // file produces the same answer -- so there is nothing to retry.
    SignIn,
    // The round trip never completed. This one CAN fail spuriously, which is
    // the whole reason it is the only shape with `TRY AGAIN`.
    Offline,
    // There is no saved Wi-Fi network to sync over. Also deterministic: the
    // saved list does not change between two presses of a slab.
    NoNetwork,
  };
  enum class Chosen { None, TryAgain, Ok };

  explicit WallabagErrorScreen(Shape shape);

  ScreenId id() const override { return ScreenId::WallabagError; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const WallabagErrorViewModel& vm() const { return vm_; }
  Shape shape() const { return shape_; }
  Chosen chosen() const { return chosen_; }

 private:
  static int slabCountFor(Shape s) { return s == Shape::Offline ? 2 : 1; }
  void syncVm() override;
  Shape shape_;
  WallabagErrorViewModel vm_;
  Chosen chosen_ = Chosen::None;
};

// design/ArticlesRemoveConfirm.dc.html -- what the account screen's
// `Remove downloaded articles...` row opens. DeleteConfirmScreen's shape, and
// it reuses DeleteConfirmViewModel because the board is that board with two
// strings changed.
//
// `CANCEL` IS THE FILLED SLAB AND THE FOCUS STARTS ON IT, which is
// DeleteConfirm's order: a confirmation whose default is the thing being
// confirmed is a second press of the button that opened it. The action is
// recoverable here -- the next sync brings the files back -- and the ordering
// still holds, because what makes it a confirm is having to move to reach the
// verb.
class ArticlesRemoveConfirmScreen : public FocusScreen {
 public:
  enum class Chosen { None, RemoveAll };

  ArticlesRemoveConfirmScreen();

  ScreenId id() const override { return ScreenId::ArticlesRemoveConfirm; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const DeleteConfirmViewModel& vm() const { return vm_; }
  Chosen chosen() const { return chosen_; }

  // CONSTANT, unlike the article-actions overlay's. This panel's height is a
  // caption, a paragraph and two slabs -- none of which moves with the focus,
  // because a slab's fill changes its ink and never its box. DeleteConfirm's own
  // answer, for its reason.
  uint32_t paintFootprint() const override { return 1; }

 private:
  enum Row { kCancel = 0, kRemove, kRowCount };
  void syncVm() override;
  DeleteConfirmViewModel vm_;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
