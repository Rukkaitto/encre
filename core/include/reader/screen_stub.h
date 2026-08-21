#pragma once
#include <optional>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/viewmodel.h"

namespace reader {

// One provisional screen, parameterised: a title, a list of rows, and for each
// row an optional screen it opens. Serves both the Library and Settings
// placeholders. Phase 2C deletes it.
class StubScreen : public Screen {
 public:
  struct Row {
    std::string label;
    std::optional<ScreenId> target;
  };

  StubScreen(ScreenId id, std::string title, std::vector<Row> rows);

  ScreenId id() const override { return id_; }
  ButtonMask longPressable() const override { return hintHoldMask(vm_.holds); }
  Action onEvent(const InputEvent& ev) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;

  // An override of Screen::focus() since the base declared one; marked so it is
  // visible here rather than inferred from app.h. No setFocus: 2C-3 replaces
  // this placeholder with the real Settings screen, and teaching a screen that
  // is about to be deleted to restore a focus is work thrown away.
  int focus() const override { return vm_.focusedLine; }

 private:
  Action moveFocus(int delta);

  ScreenId id_;
  std::vector<Row> rows_;
  StubViewModel vm_;
};

}  // namespace reader
