#include "reader/screen_stub.h"

#include "reader/theme.h"

namespace reader {

StubScreen::StubScreen(ScreenId id, std::string title, std::vector<Row> rows)
    : id_(id), rows_(std::move(rows)) {
  vm_.title = std::move(title);
  vm_.note = "PLACEHOLDER \xE2\x80\x94 PHASE 2C";
  for (const Row& r : rows_) vm_.lines.push_back(r.label);
  vm_.focusedLine = rows_.empty() ? -1 : 0;
  vm_.batteryPercent = 87;
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
}

Action StubScreen::moveFocus(int delta) {
  if (rows_.empty()) return Action::none();
  const int last = static_cast<int>(rows_.size()) - 1;
  int next = vm_.focusedLine + delta;
  if (next < 0) next = 0;
  if (next > last) next = last;
  if (next == vm_.focusedLine) return Action::none();
  vm_.focusedLine = next;
  return Action::redraw();
}

Action StubScreen::onEvent(const InputEvent& ev) {
  if (ev.kind != PressKind::Short) return Action::none();
  switch (ev.button) {
    case Button::Down:
      return moveFocus(+1);
    case Button::Up:
      return moveFocus(-1);
    case Button::Back:
      return Action::pop();
    case Button::Confirm: {
      const int i = vm_.focusedLine;
      if (i < 0 || i >= static_cast<int>(rows_.size())) return Action::none();
      const auto& target = rows_[static_cast<size_t>(i)].target;
      if (!target) return Action::none();
      return Action::push(*target);
    }
    default:
      return Action::none();
  }
}

void StubScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const {
  theme.renderStub(fb, fonts, vm_, plane);
}

}  // namespace reader
