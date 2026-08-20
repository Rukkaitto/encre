#include "reader/screen_input_monitor.h"

#include "reader/theme.h"

namespace reader {
namespace {

const char* buttonName(Button b) {
  switch (b) {
    case Button::Back: return "BACK";
    case Button::Confirm: return "CONFIRM";
    case Button::Left: return "LEFT";
    case Button::Right: return "RIGHT";
    case Button::Up: return "UP";
    case Button::Down: return "DOWN";
    case Button::Power: return "POWER";
    default: return "?";
  }
}

}  // namespace

InputMonitorScreen::InputMonitorScreen() {
  vm_.title = "INPUT MONITOR";
  vm_.note = "DIAGNOSTIC \xE2\x80\x94 HOLD CONFIRM";
  vm_.batteryPercent = 87;
  vm_.hints = {"BACK", "LOG", "UP", "DOWN"};
  // Confirm carries a hold, so this screen's bar shows the ring AND the
  // recognizer is allowed to fire Long for it. One array does both.
  vm_.holds = {false, true, false, false};
}

Action InputMonitorScreen::onEvent(const InputEvent& ev) {
  // Back short-presses out; every other press, including Back held, is logged.
  if (ev.button == Button::Back && ev.kind == PressKind::Short) return Action::pop();

  std::string line = buttonName(ev.button);
  line += ev.kind == PressKind::Long ? " \xC2\xB7 LONG" : " \xC2\xB7 SHORT";
  vm_.lines.insert(vm_.lines.begin(), std::move(line));
  if (vm_.lines.size() > kMaxLines) vm_.lines.pop_back();
  return Action::redraw();
}

void InputMonitorScreen::render(Framebuffer& fb, const FontSet& fonts, Theme& theme,
                                Plane plane) const {
  theme.renderStub(fb, fonts, vm_, plane);
}

}  // namespace reader
