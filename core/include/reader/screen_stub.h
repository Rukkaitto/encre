#pragma once
#include <optional>
#include <string>
#include <vector>

#include "reader/app.h"
#include "reader/focus.h"
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
  // visible here rather than inferred from app.h.
  //
  // setFocus was left off here on the reasoning that 2C-3 replaces this
  // placeholder with the real Settings screen, so teaching a screen that is about
  // to be deleted to restore a focus is work thrown away. The reasoning was
  // sound and the conclusion still cost a user a bug: this is what Settings IS
  // until 2C-3 lands, so a wake from Settings reset to the first row for as long
  // as the placeholder lived. A screen that reports a focus accepts one back --
  // there is no "provisional" exemption, because the placeholder is what ships in
  // the meantime.
  int focus() const override { return vm_.focusedLine; }
  bool setFocus(int index) override;

 private:
  Action moveFocus(int delta);
  bool syncFocus(bool moved);

  ScreenId id_;
  std::vector<Row> rows_;
  StubViewModel vm_;
  // Noneless: every row here is a real destination and there is no CONTINUE
  // block below them. An EMPTY list still reports -1, which is Focus's rule and
  // not a second one.
  Focus focus_;
};

}  // namespace reader
