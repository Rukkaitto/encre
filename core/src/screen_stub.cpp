#include "reader/screen_stub.h"

#include "reader/theme.h"

namespace reader {

StubScreen::StubScreen(ScreenId id, std::string title, std::vector<Row> rows)
    : id_(id), rows_(std::move(rows)) {
  vm_.title = std::move(title);
  vm_.note = "PLACEHOLDER \xE2\x80\x94 PHASE 2C";
  for (const Row& r : rows_) vm_.lines.push_back(r.label);
  focus_ = Focus(static_cast<int>(rows_.size()));
  vm_.focusedLine = focus_.index();  // -1 for an empty list, 0 otherwise
  vm_.batteryPercent = 87;
  vm_.hints = {"BACK", "OPEN", "UP", "DOWN"};
}

bool StubScreen::syncFocus(bool moved) {
  if (moved) vm_.focusedLine = focus_.index();
  return moved;
}

bool StubScreen::setFocus(int index) { return syncFocus(focus_.set(index)); }

Action StubScreen::moveFocus(int delta) {
  return syncFocus(focus_.move(delta)) ? Action::redraw() : Action::none();
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
