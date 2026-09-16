#pragma once
#include <cstdint>
#include <string>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// design/ArticleActions.dc.html -- the overlay a HOLD on an article row opens.
// ItemActionsScreen's shape, and BUILT FROM FACTS rather than from an
// ArticlesScreen&, which is DeleteConfirmScreen's argument and the reason that
// screen was rewritten: a screen reference makes an overlay reachable from ONE
// parent, and the whole defect it fixed was `About this book` working from the
// Library and refusing from a Reader opened through Home. This overlay has two
// parents from the day it lands -- the list and the end screen -- so the
// reference form was never available.
class ArticleActionsScreen : public FocusScreen {
 public:
  struct Facts {
    int id = 0;
    std::string title;
    // The SECOND ROW'S LABEL, not a decoration: it reads `Unstar` when the
    // article is already starred. One row that names what it will do, rather
    // than a row plus a state the reader has to read off somewhere else.
    bool starred = false;
  };

  // What the last press asked for, read by the shell off this screen while it is
  // still on top -- Action::article() pops nothing for exactly that reason.
  enum class Chosen { None, Archive, Star };

  explicit ArticleActionsScreen(Facts facts);

  ScreenId id() const override { return ScreenId::ArticleActions; }
  bool isOverlay() const override { return true; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const ArticleActionsViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }
  Chosen chosen() const { return chosen_; }

  // NOT CONSTANT, AND THE PLAN SAID IT WOULD BE. ItemActions' own footprint
  // counts BORDERLESS rows for a reason that applies here unchanged: the panel's
  // height is the sum of its rows and `rowRuleFor` drops the rule for the focused
  // row AND for the last one. With TWO rows those two suppressions COINCIDE when
  // row 0 is focused -- row 0 is the focused one and row 1 is the last one -- so
  // the initial state is the SHORT panel and focusing row 1 gives row 0 its rule
  // back, making the panel a pixel taller and, being centred, a pixel higher.
  // Measured at 290 and 289 on the X4. The asymmetry is reachable on the very
  // first press, and its DIRECTION is the opposite of the obvious guess, which
  // is why the test renders rather than reasons.
  //
  // A constant here would let App::renderTopOnly repaint the overlay in place
  // across a move that changes the panel's extent, leaving the old top border
  // standing -- which is the 226-stray-pixel defect ItemActions recorded. It is
  // asserted by RENDERING both focus states and comparing the frames, not by
  // this arithmetic.
  uint32_t paintFootprint() const override {
    const int rows = static_cast<int>(vm_.actions.size());
    uint32_t borderless = 0;
    for (int i = 0; i < rows; ++i)
      if (i == vm_.focusedAction || i == rows - 1) ++borderless;
    return 1u + borderless;  // offset by one: zero is Screen's "no promise"
  }

  static constexpr int kArchive = 0;
  static constexpr int kStar = 1;
  static constexpr int kRowCount = 2;

 private:
  void syncVm() override;
  Facts facts_;
  ArticleActionsViewModel vm_;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
